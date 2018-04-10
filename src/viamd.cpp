#include <imgui.h>
#include <core/platform.h>
#include <core/gl.h>
#include <core/types.h>
#include <core/hash.h>
#include <core/math_utils.h>
#include <core/camera.h>
#include <core/camera_utils.h>
#include <core/console.h>
#include <core/string_utils.h>
#include <mol/molecule.h>
#include <mol/trajectory.h>
#include <mol/trajectory_utils.h>
#include <mol/molecule_utils.h>
#include <mol/pdb_utils.h>
#include <mol/gro_utils.h>
#include <stats/stats.h>
#include <gfx/immediate_draw_utils.h>
#include <gfx/postprocessing_utils.h>

#include <stdio.h>

#ifdef _WIN32
constexpr Key::Key_t CONSOLE_KEY = Key::KEY_GRAVE_ACCENT;
#elif __APPLE__
constexpr Key::Key_t CONSOLE_KEY = Key::KEY_WORLD_1;
#else
// @TODO: Make sure this is right for Linux?
constexpr Key::Key_t CONSOLE_KEY = Key::KEY_GRAVE_ACCENT;
#endif
constexpr unsigned int NO_PICKING_IDX = 0xffffffff;

constexpr const char* caffeine_pdb = R"(
ATOM      1  N1  BENZ    1       5.040   1.944  -8.324                          
ATOM      2  C2  BENZ    1       6.469   2.092  -7.915                          
ATOM      3  C3  BENZ    1       7.431   0.865  -8.072                          
ATOM      4  C4  BENZ    1       6.916  -0.391  -8.544                          
ATOM      5  N5  BENZ    1       5.532  -0.541  -8.901                          
ATOM      6  C6  BENZ    1       4.590   0.523  -8.394                          
ATOM      7  C11 BENZ    1       4.045   3.041  -8.005                          
ATOM      8  H111BENZ    1       4.453   4.038  -8.264                          
ATOM      9  H112BENZ    1       3.101   2.907  -8.570                          
ATOM     10  H113BENZ    1       3.795   3.050  -6.926                          
ATOM     11  O21 BENZ    1       6.879   3.181  -7.503                          
ATOM     12  C51 BENZ    1       4.907  -1.659  -9.696                          
ATOM     13  H511BENZ    1       4.397  -1.273 -10.599                          
ATOM     14  H512BENZ    1       5.669  -2.391 -10.028                          
ATOM     15  H513BENZ    1       4.161  -2.209  -9.089                          
ATOM     16  O61 BENZ    1       3.470   0.208  -7.986                          
ATOM     17  N1  NSP3    1B      8.807   0.809  -7.799                          
ATOM     18  N1  NSP3    1C      7.982  -1.285  -8.604                          
ATOM     19  C1  CSP3    1D      9.015  -0.500  -8.152                          
ATOM     20  H1  CSP3    1D     10.007  -0.926  -8.079                          
ATOM     21  C1  CSP3    1E      9.756   1.835  -7.299                          
ATOM     22  H11 CSP3    1E     10.776   1.419  -7.199                          
ATOM     23  H12 CSP3    1E      9.437   2.207  -6.309                          
ATOM     24  H13 CSP3    1E      9.801   2.693  -7.994
)";

inline ImVec4 vec_cast(vec4 v) { return ImVec4(v.x, v.y, v.z, v.w); }
inline vec4 vec_cast(ImVec4 v) { return vec4(v.x, v.y, v.z, v.w); }
inline ImVec2 vec_cast(vec2 v) { return ImVec2(v.x, v.y); }
inline vec2 vec_cast(ImVec2 v) { return vec2(v.x, v.y); }

enum PlaybackInterpolationMode { NEAREST, LINEAR, LINEAR_PERIODIC, CUBIC, CUBIC_PERIODIC };

struct MainFramebuffer {
    GLuint id = 0;
    GLuint tex_depth = 0;
    GLuint tex_color = 0;
    GLuint tex_normal = 0;
    GLuint tex_picking = 0;
    int width = 0;
    int height = 0;
};

struct Representation {
    enum Type { VDW, LICORICE, RIBBONS };

    StringBuffer<128> name = "rep";
    StringBuffer<128> filter = "all";
    Type type = VDW;
    ColorMapping color_mapping = ColorMapping::CPK;
	Array<uint32> colors{};
	
	bool enabled = true;
	bool filter_is_ok = true;
    
	// Static color mode
	vec4 static_color = vec4(1);
    
	// VDW and Licorice
	float radii_scale = 1.f;

	// Ribbons and other spline primitives
    int num_subdivisions = 8;
	float tension = 0.5f;
	float width_scale = 1.f;
	float thickness_scale = 1.f;
};

struct AtomSelection {
    int32 atom_idx = -1;
    int32 residue_idx = -1;
    int32 chain_idx = -1;
};

struct MoleculeData {
    MoleculeDynamic dynamic{};
    DynamicArray<float> atom_radii{};
};

struct ApplicationData {
    // --- PLATFORM ---
    platform::Context ctx;

	struct {
		String molecule{};
		String trajectory{};
		String workspace{};
	} files;

    // --- CAMERA ---
    Camera camera;
    TrackballController controller;

    // --- MOL DATA ---
    MoleculeData mol_data;

    struct {
        bool show_window;
        DynamicArray<Representation> data;
    } representations;

    // --- ATOM SELECTION ---
    AtomSelection hovered;
    AtomSelection selected;

    // --- STATISTICAL DATA ---
    struct {
        bool show_property_window = false;
		bool show_timeline_window = false;
		bool show_distribution_window = false;
    } statistics;

    // Framebuffer
    MainFramebuffer fbo;
    unsigned int picking_idx = NO_PICKING_IDX;

    // --- PLAYBACK ---
    float64 time = 0.f;  // needs to be double precision
    float frames_per_second = 10.f;
    bool is_playing = false;
    PlaybackInterpolationMode interpolation = PlaybackInterpolationMode::LINEAR_PERIODIC;

    // --- VISUALS ---
    // SSAO
    struct {
        bool enabled = false;
        float intensity = 1.5f;
        float radius = 6.0f;
    } ssao;

    struct {
        bool show_window = false;
        float radius = 1.f;
        float opacity = 1.f;
        int frame_range_min = 0;
        int frame_range_max = 0;

        BackboneAnglesTrajectory backbone_angles{};
        Array<BackboneAngles> current_backbone_angles{};
    } ramachandran;

    struct {
        struct {
            bool enabled = false;
        } spline;

        struct {
            bool enabled = false;
        } backbone;
    } debug_draw;

    // --- CONSOLE ---
    Console console;
    bool show_console;
};

static void draw_main_menu(ApplicationData* data);
static void draw_representations_window(ApplicationData* data);
static void draw_property_window(ApplicationData* data);
static void draw_timeline_window(ApplicationData* data);
static void draw_distribution_window(ApplicationData* data);
static void draw_ramachandran(ApplicationData* data);
static void draw_atom_info(const MoleculeStructure& mol, int atom_idx, int x, int y);

static void init_main_framebuffer(MainFramebuffer* fbo, int width, int height);
static void destroy_main_framebuffer(MainFramebuffer* fbo);

static void reset_view(ApplicationData* data);
static float compute_avg_ms(float dt);
static uint32 get_picking_id(uint32 fbo, int32 x, int32 y);

static void load_molecule_data(ApplicationData* data, CString file);
static void load_workspace(ApplicationData* data, CString file);
static void save_workspace(ApplicationData* data, CString file);

static void create_default_representation(ApplicationData* data);
static void remove_representation(ApplicationData* data, int idx);
static void reset_representations(ApplicationData* data);

int main(int, char**) {
    ApplicationData data;

    auto dir_list = platform::list_directory(".");
    for (const auto& d : dir_list) {
        printf("%s\n", d.name.beg());
    }
    printf("%s\n", platform::get_cwd().beg());

    // Init platform
    platform::initialize(&data.ctx, 1920, 1080, "VIAMD");
    data.ctx.window.vsync = false;

    init_main_framebuffer(&data.fbo, data.ctx.framebuffer.width, data.ctx.framebuffer.height);

    // Init subsystems
    immediate::initialize();
    draw::initialize();
	ramachandran::initialize();
    stats::initialize();
    filter::initialize();
    postprocessing::initialize(data.fbo.width, data.fbo.height);

    // Setup style
    ImGui::StyleColorsClassic();

    bool show_demo_window = false;
    vec4 clear_color = vec4(1, 1, 1, 1);
    vec4 clear_index = vec4(1, 1, 1, 1);

#ifdef RELEASE_VERSION
    data.mol_data.dynamic = allocate_and_parse_pdb_from_string(caffeine_pdb);
    data.mol_data.atom_radii = compute_atom_radii(data.mol_data.dynamic.molecule->atom_elements);
#else
	stats::create_group("group1", "resname ALA");
	stats::create_property("b1", "dist group1 1 2");
	stats::create_property("a1", "angle group1 1 2 3");
	stats::create_property("d1", "dihedral group1 1 2 3 4");

    load_molecule_data(&data, PROJECT_SOURCE_DIR "/data/1ALA-250ns-2500frames.pdb");
#endif
    reset_view(&data);
    create_default_representation(&data);

    // Main loop
    while (!data.ctx.window.should_close) {
        platform::update(&data.ctx);

        // RESIZE FRAMEBUFFER?
        if (data.fbo.width != data.ctx.framebuffer.width || data.fbo.height != data.ctx.framebuffer.height) {
            init_main_framebuffer(&data.fbo, data.ctx.framebuffer.width, data.ctx.framebuffer.height);
            postprocessing::initialize(data.fbo.width, data.fbo.height);
        }

        // Setup fbo and clear textures
        glViewport(0, 0, data.fbo.width, data.fbo.height);

        const GLenum draw_buffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, data.fbo.id);

        // Clear color, normal and depth buffer
        glDrawBuffers(2, draw_buffers);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Clear picking buffer
        glDrawBuffer(GL_COLOR_ATTACHMENT2);
        glClearColor(clear_index.x, clear_index.y, clear_index.z, clear_index.w);
        glClear(GL_COLOR_BUFFER_BIT);

        // Enable all draw buffers
        glDrawBuffers(3, draw_buffers);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        if (data.ctx.input.key.hit[CONSOLE_KEY]) {
            data.console.visible = !data.console.visible;
        }

        float ms = compute_avg_ms(data.ctx.timing.dt);
        bool time_changed = false;

        ImGui::Begin("Misc");
        ImGui::Text("%.2f ms (%.1f fps)", ms, 1000.f / (ms));
        ImGui::Text("MouseVel: %g, %g", data.ctx.input.mouse.velocity.x, data.ctx.input.mouse.velocity.y);
        ImGui::Text("Camera Pos: %g, %g, %g", data.camera.position.x, data.camera.position.y, data.camera.position.z);
        ImGui::Checkbox("Show Demo Window", &show_demo_window);
        if (ImGui::Button("Reset View")) {
            reset_view(&data);
        }
        if (data.mol_data.dynamic.trajectory) {
            ImGui::Text("Num Frames: %i", data.mol_data.dynamic.trajectory.num_frames);
            float t = (float)data.time;
            if (ImGui::SliderFloat("Time", &t, 0, (float)(data.mol_data.dynamic.trajectory.num_frames - 1))) {
                time_changed = true;
                data.time = t;
            }
            ImGui::SliderFloat("Frames Per Second", &data.frames_per_second, 0.1f, 100.f, "%.3f", 4.f);
            if (data.is_playing) {
                if (ImGui::Button("Pause")) data.is_playing = false;
            } else {
                if (ImGui::Button("Play")) data.is_playing = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Stop")) {
                data.is_playing = false;
                data.time = 0.0;
                time_changed = true;
            }
            ImGui::Combo("type", (int*)(&data.interpolation), "Nearest\0Linear\0Linear Periodic\0Cubic\0Cubic Periodic\0\0");
        }
        ImGui::End();

        if (data.is_playing) {
            data.time += data.ctx.timing.dt * data.frames_per_second;
            time_changed = true;
        }

        if (data.mol_data.dynamic.trajectory && time_changed) {
            int last_frame = data.mol_data.dynamic.trajectory.num_frames - 1;
            data.time = math::clamp(data.time, 0.0, float64(last_frame));
            if (data.time == float64(last_frame)) data.is_playing = false;

            int frame = (int)data.time;
            int prev_frame_2 = math::max(0, frame - 1);
            int prev_frame_1 = math::max(0, frame);
            int next_frame_1 = math::min(frame + 1, last_frame);
            int next_frame_2 = math::min(frame + 2, last_frame);

            if (prev_frame_1 == next_frame_1) {
                copy_trajectory_positions(data.mol_data.dynamic.molecule.atom_positions, data.mol_data.dynamic.trajectory, prev_frame_1);
            } else {
                float t = (float)math::fract(data.time);

                // INTERPOLATE
                switch (data.interpolation) {
                    case PlaybackInterpolationMode::NEAREST:
                        // @NOTE THIS IS ACTUALLY FLOORING
                        copy_trajectory_positions(data.mol_data.dynamic.molecule.atom_positions, data.mol_data.dynamic.trajectory, prev_frame_1);
                        break;
                    case PlaybackInterpolationMode::LINEAR:
                    {
                        auto prev_frame = get_trajectory_frame(data.mol_data.dynamic.trajectory, prev_frame_1);
                        auto next_frame = get_trajectory_frame(data.mol_data.dynamic.trajectory, next_frame_1);
                        linear_interpolation(data.mol_data.dynamic.molecule.atom_positions, prev_frame.atom_positions, next_frame.atom_positions, t);
                        break;
                    }
                    case PlaybackInterpolationMode::LINEAR_PERIODIC:
                    {
                        auto prev_frame = get_trajectory_frame(data.mol_data.dynamic.trajectory, prev_frame_1);
                        auto next_frame = get_trajectory_frame(data.mol_data.dynamic.trajectory, next_frame_1);
                        linear_interpolation_periodic(data.mol_data.dynamic.molecule.atom_positions, prev_frame.atom_positions, next_frame.atom_positions, t,
                                                      prev_frame.box);
                        break;
                    }
                    case PlaybackInterpolationMode::CUBIC:
					{
						auto prev_2 = get_trajectory_frame(data.mol_data.dynamic.trajectory, prev_frame_2);
						auto prev_1 = get_trajectory_frame(data.mol_data.dynamic.trajectory, prev_frame_1);
						auto next_1 = get_trajectory_frame(data.mol_data.dynamic.trajectory, next_frame_1);
						auto next_2 = get_trajectory_frame(data.mol_data.dynamic.trajectory, next_frame_2);
						spline_interpolation(data.mol_data.dynamic.molecule.atom_positions, prev_2.atom_positions, prev_1.atom_positions, next_1.atom_positions, next_2.atom_positions, t);
						break;
					}
                    case PlaybackInterpolationMode::CUBIC_PERIODIC:
                    {
                        auto prev_2 = get_trajectory_frame(data.mol_data.dynamic.trajectory, prev_frame_2);
                        auto prev_1 = get_trajectory_frame(data.mol_data.dynamic.trajectory, prev_frame_1);
                        auto next_1 = get_trajectory_frame(data.mol_data.dynamic.trajectory, next_frame_1);
                        auto next_2 = get_trajectory_frame(data.mol_data.dynamic.trajectory, next_frame_2);
                        spline_interpolation_periodic(data.mol_data.dynamic.molecule.atom_positions, prev_2.atom_positions, prev_1.atom_positions, next_1.atom_positions, next_2.atom_positions, t, prev_1.box);
                        break;
                    }

                    default:
                    break;
                }
            }
        }

        if (!ImGui::GetIO().WantCaptureMouse) {
            data.controller.input.rotate_button = data.ctx.input.mouse.down[0];
            data.controller.input.pan_button = data.ctx.input.mouse.down[1];
            data.controller.input.dolly_button = data.ctx.input.mouse.down[2];
            data.controller.input.mouse_coord_prev = data.ctx.input.mouse.coord_prev;
            data.controller.input.mouse_coord_curr = data.ctx.input.mouse.coord_curr;
            data.controller.input.screen_size = vec2(data.ctx.window.width, data.ctx.window.height);
            data.controller.input.dolly_delta = data.ctx.input.mouse.scroll.y;
            data.controller.update();
            data.camera.position = data.controller.position;
            data.camera.orientation = data.controller.orientation;

            if (data.ctx.input.mouse.release[0] && data.ctx.input.mouse.velocity == vec2(0)) {
                data.selected = data.hovered;
            }
        }

        // RENDER TO FBO
        mat4 view_mat = compute_world_to_view_matrix(data.camera);
        mat4 proj_mat = compute_perspective_projection_matrix(data.camera, data.fbo.width, data.fbo.height);
        mat4 inv_proj_mat = math::inverse(proj_mat);

        for (const auto& rep : data.representations.data) {
			if (!rep.enabled) continue;
            switch (rep.type) {
                case Representation::VDW:
                    draw::draw_vdw(data.mol_data.dynamic.molecule.atom_positions, data.mol_data.atom_radii, rep.colors, view_mat, proj_mat,
                                   rep.radii_scale);
                    break;
                case Representation::LICORICE:
                    draw::draw_licorice(data.mol_data.dynamic.molecule.atom_positions, data.mol_data.dynamic.molecule.bonds, rep.colors, view_mat,
                                        proj_mat, rep.radii_scale);
                    break;
                case Representation::RIBBONS:
                    draw::draw_ribbons(data.mol_data.dynamic.molecule.backbone_segments, data.mol_data.dynamic.molecule.chains,
                                       data.mol_data.dynamic.molecule.atom_positions, rep.colors, view_mat, proj_mat, rep.num_subdivisions, rep.tension, rep.width_scale, rep.thickness_scale);
                    break;
            }
        }
        // draw::draw_vdw(data.dynamic.molecule->atom_positions, data.atom_radii, data.atom_colors, view_mat, proj_mat, radii_scale);
        // draw::draw_licorice(data.mol_struct->atom_positions, data.mol_struct->bonds, data.atom_colors, view_mat, proj_mat, radii_scale);
        // draw::draw_ribbons(current_spline, view_mat, proj_mat);

        // PICKING
        {
            ivec2 coord = {data.ctx.input.mouse.coord_curr.x, data.ctx.framebuffer.height - data.ctx.input.mouse.coord_curr.y};
            data.picking_idx = get_picking_id(data.fbo.id, coord.x, coord.y);

            data.hovered = {};
            if (data.picking_idx != NO_PICKING_IDX) {
                data.hovered.atom_idx = data.picking_idx;
                if (-1 < data.hovered.atom_idx && data.hovered.atom_idx < data.mol_data.dynamic.molecule.atom_residue_indices.count) {
                    data.hovered.residue_idx = data.mol_data.dynamic.molecule.atom_residue_indices[data.hovered.atom_idx];
                }
                if (-1 < data.hovered.residue_idx && data.hovered.residue_idx < data.mol_data.dynamic.molecule.residues.count) {
                    data.hovered.chain_idx = data.mol_data.dynamic.molecule.residues[data.hovered.residue_idx].chain_idx;
                }
            }
        }

        // Activate backbuffer
        glDisable(GL_DEPTH_TEST);
        glDepthFunc(GL_ALWAYS);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glDrawBuffer(GL_BACK);
        glClear(GL_COLOR_BUFFER_BIT);

        // Render deferred
        postprocessing::render_deferred(data.fbo.tex_depth, data.fbo.tex_color, data.fbo.tex_normal, inv_proj_mat);

        // Apply post processing
        // postprocessing::apply_tonemapping(data.fbo.tex_color);
        if (data.ssao.enabled) {
            postprocessing::apply_ssao(data.fbo.tex_depth, data.fbo.tex_normal, proj_mat, data.ssao.intensity, data.ssao.radius);
        }

        /*
if (data.debug_draw.backbone.enabled) {
    draw::draw_backbone(backbone, data.mol_data.dynamic.molecule->atom_positions, view_mat, proj_mat);
}
if (data.debug_draw.spline.enabled) {
    draw::draw_spline(current_spline, view_mat, proj_mat);
}
        */

        // GUI ELEMENTS
        data.console.Draw("VIAMD", data.ctx.window.width, data.ctx.window.height, data.ctx.timing.dt);

        draw_main_menu(&data);

        if (data.representations.show_window) draw_representations_window(&data);
        if (data.statistics.show_property_window) draw_property_window(&data);
		if (data.statistics.show_timeline_window) draw_timeline_window(&data);
		if (data.statistics.show_distribution_window) draw_distribution_window(&data);

		if (data.ramachandran.show_window) {
			if (data.mol_data.dynamic.trajectory && data.mol_data.dynamic.trajectory.is_loading) {
				static int32 prev_frame = 0;
				if (get_backbone_angles_trajectory_current_frame_count(data.ramachandran.backbone_angles) - prev_frame > 100) {
					compute_backbone_angles_trajectory(&data.ramachandran.backbone_angles, data.mol_data.dynamic);
					prev_frame = data.ramachandran.backbone_angles.num_frames;
					data.ramachandran.frame_range_max = data.ramachandran.backbone_angles.num_frames;
				}
			}
			draw_ramachandran(&data);
		}

		if (!ImGui::GetIO().WantCaptureMouse) {
            if (data.picking_idx != NO_PICKING_IDX) {
                ivec2 pos = data.ctx.input.mouse.coord_curr;
                draw_atom_info(data.mol_data.dynamic.molecule, data.picking_idx, pos.x, pos.y);
            }
        }

        // Show the ImGui demo window. Most of the sample code is in ImGui::ShowDemoWindow().
        if (show_demo_window) {
            ImGui::SetNextWindowPos(ImVec2(650, 20),
                                    ImGuiCond_FirstUseEver);  // Normally user code doesn't need/want to call this because positions are saved in .ini
                                                              // file anyway. Here we just want to make the demo initial state a bit more friendly!
            ImGui::ShowDemoWindow(&show_demo_window);
        }
        ImGui::Render();

        // Swap buffers
        platform::swap_buffers(&data.ctx);
    }

    destroy_main_framebuffer(&data.fbo);

    platform::shutdown(&data.ctx);

    return 0;
}

void draw_random_triangles(const mat4& mvp) {
    immediate::set_view_matrix(mvp);
    immediate::set_proj_matrix(mat4(1));
    math::set_rnd_seed(0);
    for (int i = 0; i < 500; i++) {
        vec3 v0 = vec3(math::rnd(), math::rnd(), math::rnd()) * 50.f - 50.f;
        vec3 v1 = vec3(math::rnd(), math::rnd(), math::rnd()) * 50.f - 50.f;
        vec3 v2 = vec3(math::rnd(), math::rnd(), math::rnd()) * 50.f - 50.f;
        immediate::draw_triangle(&v0[0], &v1[0], &v2[0], immediate::COLOR_RED);
    }
    immediate::flush();
}

// @NOTE: Perhaps this can be done with a simple running mean?
static float compute_avg_ms(float dt) {
    constexpr float interval = 0.5f;
    static float avg = 0.f;
    static int num_frames = 0;
    static float t = 0;
    t += dt;
    num_frames++;

    if (t > interval) {
        avg = t / num_frames * 1000.f;
        t = 0;
        num_frames = 0;
    }

    return avg;

    /*
    constexpr int num_frames = 100;
    static float ms_buffer[num_frames] = {};
    static int next_idx = 0;
    next_idx = (next_idx + 1) % num_frames;
    ms_buffer[next_idx] = dt * 1000.f; // seconds to milliseconds

    float avg = 0.f;
    for (int i = 0; i < num_frames; i++) {
            avg += ms_buffer[i];
    }
    return avg / (float)num_frames;
    */
}

uint32 get_picking_id(uint32 fbo_id, int32 x, int32 y) {
    unsigned char color[4];
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_id);
    glReadBuffer(GL_COLOR_ATTACHMENT2);
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, color);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    return color[0] + (color[1] << 8) + (color[2] << 16) + (color[3] << 24);
}

static void draw_main_menu(ApplicationData* data) {
    ASSERT(data);
    bool new_clicked = false;
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New", "CTRL+N")) new_clicked = true;
            if (ImGui::MenuItem("Load Data", "CTRL+L")) {
                auto res = platform::open_file_dialog("pdb,gro,xtc");
				if (res.action == platform::FileDialogResult::OK) {
					load_molecule_data(data, res.path);
					if (data->representations.data.count > 0) {
						reset_representations(data);
					}
					else {
						create_default_representation(data);
					}
					reset_view(data);
				}
            }
            if (ImGui::MenuItem("Open", "CTRL+O")) {
				auto res = platform::open_file_dialog("vwf");
				if (res.action == platform::FileDialogResult::OK) {
					
				}
            }
            if (ImGui::MenuItem("Save", "CTRL+S")) {

            }
            if (ImGui::MenuItem("Save As")) {
				auto res = platform::save_file_dialog("vwf");
				if (res.action == platform::FileDialogResult::OK) {
					save_workspace(data, res.path);
				}
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "ALT+F4")) {
                data->ctx.window.should_close = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "CTRL+Z")) {
            }
            if (ImGui::MenuItem("Redo", "CTRL+Y", false, false)) {
            }  // Disabled item
            ImGui::Separator();
            if (ImGui::MenuItem("Cut", "CTRL+X")) {
            }
            if (ImGui::MenuItem("Copy", "CTRL+C")) {
            }
            if (ImGui::MenuItem("Paste", "CTRL+V")) {
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Visuals")) {

            // SSAO
            ImGui::BeginGroup();
            ImGui::Checkbox("SSAO", &data->ssao.enabled);
            if (data->ssao.enabled) {
                ImGui::SliderFloat("Intensity", &data->ssao.intensity, 0.5f, 6.f);
                ImGui::SliderFloat("Radius", &data->ssao.radius, 1.f, 30.f);
            }
            ImGui::EndGroup();
            ImGui::Separator();

            // DEBUG DRAW
            ImGui::BeginGroup();
            ImGui::Checkbox("Spline", &data->debug_draw.spline.enabled);
            ImGui::Checkbox("Backbone", &data->debug_draw.backbone.enabled);
            ImGui::EndGroup();

            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Windows")) {
            ImGui::Checkbox("Representations", &data->representations.show_window);
            ImGui::Checkbox("Ramachandran", &data->ramachandran.show_window);
			ImGui::Checkbox("Properties", &data->statistics.show_property_window);
			ImGui::Checkbox("Timelines", &data->statistics.show_timeline_window);
			ImGui::Checkbox("Distributions", &data->statistics.show_distribution_window);

            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    if (new_clicked) ImGui::OpenPopup("Warning New");
    if (ImGui::BeginPopupModal("Warning New")) {
        ImGui::Text("By creating a new workspace you will loose any unsaved progress.");
        ImGui::Text("Are you sure?");
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static void draw_representations_window(ApplicationData* data) {
	constexpr uint32 FILTER_ERROR_COLOR = 0xdd2222bb;

    ImGui::Begin("Representations", &data->representations.show_window, ImGuiWindowFlags_NoFocusOnAppearing);

    if (ImGui::Button("create new")) {
        create_default_representation(data);
    }
	ImGui::SameLine();
	if (ImGui::Button("clear all")) {
		data->representations.data.clear();
	}
	ImGui::Spacing();
    for (int i = 0; i < data->representations.data.count; i++) {
        auto& rep = data->representations.data[i];
        ImGui::Separator();
        ImGui::BeginGroup();

        bool recompute_colors = false;
        ImGui::PushID(i);
		ImGui::Checkbox("enabled", &rep.enabled);
		ImGui::SameLine();
		if (ImGui::Button("remove")) {
			remove_representation(data, i);
		}
		ImGui::SameLine();
		if (ImGui::Button("clone")) {
			Representation clone = rep;
			clone.colors = { (uint32*)MALLOC(rep.colors.size_in_bytes()), rep.colors.count };
			memcpy(clone.colors.data, rep.colors.data, rep.colors.size_in_bytes());
			data->representations.data.insert(&rep, clone);
		}

		const float item_width = math::clamp(ImGui::GetWindowContentRegionWidth() - 80.f, 100.f, 300.f);

		ImGui::PushItemWidth(item_width);
        ImGui::InputText("name", rep.name.buffer, rep.name.MAX_LENGTH);
		if (!rep.filter_is_ok) ImGui::PushStyleColor(ImGuiCol_FrameBg, FILTER_ERROR_COLOR);
        if (ImGui::InputText("filter", rep.filter.buffer, rep.filter.MAX_LENGTH, ImGuiInputTextFlags_EnterReturnsTrue)) {
            recompute_colors = true;
        }
		
		if (!rep.filter_is_ok) ImGui::PopStyleColor();
        if (ImGui::Combo("color mapping", (int*)(&rep.color_mapping), "Static Color\0CPK\0Res Id\0Res Idx\0Chain Id\0Chain Idx\0\0")) {
            recompute_colors = true;
        }
		ImGui::PopItemWidth();
		if (rep.color_mapping == ColorMapping::STATIC_COLOR) {
			ImGui::SameLine();
			if (ImGui::ColorEdit4("color", (float*)&rep.static_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
				recompute_colors = true;
			}
		}
		ImGui::PushItemWidth(item_width);
		ImGui::Combo("type", (int*)(&rep.type), "VDW\0Licorice\0Ribbons\0\0");
        if (rep.type == Representation::VDW || rep.type == Representation::LICORICE) {
            ImGui::SliderFloat("radii scale", &rep.radii_scale, 0.1f, 2.f);
        }
        if (rep.type == Representation::RIBBONS) {
            ImGui::SliderInt("spline subdivisions", &rep.num_subdivisions, 1, 16);
			ImGui::SliderFloat("spline tension", &rep.tension, 0.f, 1.f);
			ImGui::SliderFloat("spline width", &rep.width_scale, 0.1f, 2.f);
			ImGui::SliderFloat("spline thickness", &rep.thickness_scale, 0.1f, 2.f);
        }
		ImGui::PopItemWidth();

        ImGui::PopID();
        ImGui::EndGroup();
        ImGui::Spacing();

        if (recompute_colors) {
            compute_atom_colors(rep.colors, data->mol_data.dynamic.molecule, rep.color_mapping,
                                ImGui::ColorConvertFloat4ToU32(vec_cast(rep.static_color)));
            DynamicArray<bool> mask(data->mol_data.dynamic.molecule.atom_elements.count, false);
            rep.filter_is_ok = filter::compute_filter_mask(mask, data->mol_data.dynamic, rep.filter.buffer);
            filter::filter_colors(rep.colors, mask);
        }
    }

    ImGui::End();
}

static void draw_property_window(ApplicationData* data) {
	constexpr uint32 DEL_BTN_COLOR		  = 0xff1111cc;
	constexpr uint32 DEL_BTN_HOVER_COLOR  = 0xff3333dd;
	constexpr uint32 DEL_BTN_ACTIVE_COLOR = 0xff5555ff;

    constexpr uint32 ERROR_COLOR		 = 0xaa222299;

	bool compute_stats = false;

	auto group_args_callback = [](ImGuiTextEditCallbackData* data) -> int {
		switch (data->EventFlag)
		{
		case ImGuiInputTextFlags_CallbackCompletion:
		{
			// Example of TEXT COMPLETION

			// Locate beginning of current word
			const char* word_end = data->Buf + data->CursorPos;
			const char* word_start = word_end;
			while (word_start > data->Buf) {
				const char c = word_start[-1];
				if (c == ' ' || c == '\t' || c == ',' || c == ';')
					break;
				word_start--;
			}

			// Build a list of candidates
			ImVector<const char*> candidates;

			for (int i = 0; i < stats::get_group_command_count(); i++) {
                CString cmd = stats::get_group_command_keyword(i);
				if (compare_n(cmd, word_start, (int)(word_end - word_start)))
					candidates.push_back(cmd.beg());
            }

			if (candidates.Size == 0)
			{
				// No match
				//AddLog("No match for \"%.*s\"!\n", (int)(word_end - word_start), word_start);
			}
			else if (candidates.Size == 1)
			{
				// Single match. Delete the beginning of the word and replace it entirely so we've got nice casing
				data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
				data->InsertChars(data->CursorPos, candidates[0]);
				data->InsertChars(data->CursorPos, " ");
			}
			else
			{
				// Multiple matches. Complete as much as we can, so inputing "C" will complete to "CL" and display "CLEAR" and "CLASSIFY"
				int match_len = (int)(word_end - word_start);
				for (;;)
				{
					int c = 0;
					bool all_candidates_matches = true;
					for (int i = 0; i < candidates.Size && all_candidates_matches; i++)
						if (i == 0)
							c = toupper(candidates[i][match_len]);
						else if (c == 0 || c != toupper(candidates[i][match_len]))
							all_candidates_matches = false;
					if (!all_candidates_matches)
						break;
					match_len++;
				}

				if (match_len > 0)
				{
					data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
					data->InsertChars(data->CursorPos, candidates[0], candidates[0] + match_len);
				}

				// List matches
				//AddLog("Possible matches:\n");
				//for (int i = 0; i < candidates.Size; i++)
				//	AddLog("- %s\n", candidates[i]);
			}

			break;
		}
		}
		return 0;
	};

	auto property_callback = [](ImGuiTextEditCallbackData* data) -> int {
        return 0;
	};

	ImGui::Begin("Properties", &data->statistics.show_property_window, ImGuiWindowFlags_NoFocusOnAppearing);

	ImGui::PushID("GROUPS");
    ImGui::Text("GROUPS");
    if (ImGui::Button("create new")) {
        stats::create_group();
    }
    ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Button, DEL_BTN_COLOR);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, DEL_BTN_HOVER_COLOR);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, DEL_BTN_ACTIVE_COLOR);
    if (ImGui::Button("clear all")) {

    }
	ImGui::PopStyleColor(3);
	ImGui::Spacing();

	ImGui::Columns(3, "columns", true);
	ImGui::Separator();
	ImGui::SetColumnWidth(0, ImGui::GetWindowContentRegionWidth() * 0.4f);
	ImGui::SetColumnWidth(1, ImGui::GetWindowContentRegionWidth() * 0.5f);
	ImGui::SetColumnWidth(2, ImGui::GetWindowContentRegionWidth() * 0.1f);

	ImGui::Text("name"); ImGui::NextColumn();
	ImGui::Text("args"); ImGui::NextColumn();
	ImGui::NextColumn();
	
    for (int i = 0; i < stats::get_group_count(); i++) {
        auto group_id = stats::get_group(i);
        auto name_buf = stats::get_group_name_buf(group_id);
        auto args_buf = stats::get_group_args_buf(group_id);
		auto valid    = stats::get_group_valid(group_id);
		bool update   = false;

		ImGui::Separator();
        ImGui::PushID(i);
		if (!valid) ImGui::PushStyleColor(ImGuiCol_FrameBg, ERROR_COLOR);
		ImGui::PushItemWidth(-1);
		if (ImGui::InputText("##name", name_buf->buffer, name_buf->MAX_LENGTH, ImGuiInputTextFlags_EnterReturnsTrue)) {
			update = true;
		}
		ImGui::PopItemWidth();
		ImGui::NextColumn();
		ImGui::PushItemWidth(-1);
		if (ImGui::InputText("##args", args_buf->buffer, args_buf->MAX_LENGTH, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion, group_args_callback)) {
            update = true;
        }
		ImGui::PopItemWidth();
		if (!valid) ImGui::PopStyleColor();
		ImGui::NextColumn();

		ImGui::PushStyleColor(ImGuiCol_Button, DEL_BTN_COLOR);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, DEL_BTN_HOVER_COLOR);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, DEL_BTN_ACTIVE_COLOR);
        if (ImGui::Button("del")) {
            stats::remove_group(group_id);
			compute_stats = true;
        }
		ImGui::PopStyleColor(3);

		ImGui::NextColumn();
        ImGui::PopID();

        if (update) {
			stats::clear_group(group_id);
            compute_stats = true;
        }
    }
	ImGui::Columns(1);
	ImGui::Separator();
    ImGui::PopID();

    ImGui::Spacing();
	ImGui::Spacing();

	ImGui::PushID("PROPERTIES");
    ImGui::Text("PROPERTIES");
    if (ImGui::Button("create new")) {
        stats::create_property();
    }
    ImGui::SameLine();

	ImGui::PushStyleColor(ImGuiCol_Button, DEL_BTN_COLOR);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, DEL_BTN_HOVER_COLOR);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, DEL_BTN_ACTIVE_COLOR);
    if (ImGui::Button("clear all")) {
    }
	ImGui::PopStyleColor(3);
	ImGui::Spacing();

	ImGui::Columns(3, "columns", true);
	ImGui::Separator();
	ImGui::SetColumnWidth(0, ImGui::GetWindowContentRegionWidth() * 0.4f);
	ImGui::SetColumnWidth(1, ImGui::GetWindowContentRegionWidth() * 0.5f);
	ImGui::SetColumnWidth(2, ImGui::GetWindowContentRegionWidth() * 0.1f);

	ImGui::Text("name"); ImGui::NextColumn();
	ImGui::Text("args"); ImGui::NextColumn();
	ImGui::NextColumn();
    
    for (int i = 0; i < stats::get_property_count(); i++) {
        auto prop_id  = stats::get_property(i);
        auto name_buf = stats::get_property_name_buf(prop_id);
        auto args_buf = stats::get_property_args_buf(prop_id);
		auto valid	  = stats::get_property_valid(prop_id);
        bool update   = false;

        ImGui::Separator();
        ImGui::PushID(i);

		if (!valid) ImGui::PushStyleColor(ImGuiCol_FrameBg, ERROR_COLOR);
		ImGui::PushItemWidth(-1);
		if (ImGui::InputText("##name", name_buf->buffer, name_buf->MAX_LENGTH, ImGuiInputTextFlags_EnterReturnsTrue)) {
			update = true;
		}
		ImGui::PopItemWidth();
		ImGui::NextColumn();
		ImGui::PushItemWidth(-1);
		if (ImGui::InputText("##args", args_buf->buffer, args_buf->MAX_LENGTH, ImGuiInputTextFlags_EnterReturnsTrue)) {
			update = true;
		}
		ImGui::PopItemWidth();
		if (!valid) ImGui::PopStyleColor();
        ImGui::NextColumn();
		ImGui::PushStyleColor(ImGuiCol_Button, DEL_BTN_COLOR);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, DEL_BTN_HOVER_COLOR);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, DEL_BTN_ACTIVE_COLOR);
        if (ImGui::Button("del")) {
            stats::remove_property(prop_id);
        }
		ImGui::PopStyleColor(3);
		ImGui::NextColumn();
        ImGui::PopID();

        if (update) {
			stats::clear_property(prop_id);
            compute_stats = true;
        }
    }
	ImGui::Columns(1);
	ImGui::Separator();
    ImGui::PopID();
    ImGui::End();
    
    if (compute_stats) {
        stats::compute_stats(data->mol_data.dynamic);
    }
}

static void draw_atom_info(const MoleculeStructure& mol, int atom_idx, int x, int y) {

    // @TODO: Assert things and make this failproof
    if (atom_idx < 0 || atom_idx >= mol.atom_positions.count) return;

    int res_idx = mol.atom_residue_indices[atom_idx];
    const Residue& res = mol.residues[res_idx];
    const char* res_id = res.name;
    int local_idx = atom_idx - res.beg_atom_idx;
    const char* label = mol.atom_labels[atom_idx];
    const char* elem = element::name(mol.atom_elements[atom_idx]);
    const char* symbol = element::symbol(mol.atom_elements[atom_idx]);

    int chain_idx = res.chain_idx;
    const char* chain_id = 0;
    if (chain_idx != -1) {
        const Chain& chain = mol.chains[chain_idx];
        chain_id = chain.id;
        chain_idx = res.chain_idx;
    }

    // External indices begin with 1 not 0
    res_idx += 1;
    chain_idx += 1;
    atom_idx += 1;
    local_idx += 1;

    char buff[256];
    int len = snprintf(buff, 256, "atom[%i][%i]: %s %s %s\nres[%i]: %s", atom_idx, local_idx, label, elem, symbol, res_idx, res_id);
    if (chain_idx) {
        snprintf(buff + len, 256 - len, "\nchain[%i]: %s\n", chain_idx, chain_id);
    }

    ImVec2 text_size = ImGui::CalcTextSize(buff);
    ImGui::SetNextWindowPos(ImVec2(x + 10.f, y + 10.f));
    ImGui::SetNextWindowSize(ImVec2(text_size.x + 20.f, text_size.y + 15.f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.5f));
    ImGui::Begin("##Atom Info", 0,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::Text("%s", buff);
    ImGui::End();
    ImGui::PopStyleColor();
}

static void draw_timeline_window(ApplicationData* data) {
    // if (!stats) return;

    ImGui::Begin("Timelines", &data->statistics.show_timeline_window, ImGuiWindowFlags_NoFocusOnAppearing);
    //auto group_id = stats::get_group("group1");

    int32 frame_idx = (int32)data->time;

    for (int i = 0; i < stats::get_property_count(); i++) {
        auto prop_id = stats::get_property(i);
        auto prop_data = stats::get_property_data(prop_id, 0);
        if (!prop_data) continue;
        auto frame = ImGui::BeginPlotFrame(stats::get_property_name(prop_id), ImVec2(0, 100), 0, (int32)prop_data.count, -2.f, 2.f);
        ImGui::PlotFrameLine(frame, "group1", prop_data.data, ImGui::FrameLineStyle(), frame_idx);
        int32 new_frame_idx = ImGui::EndPlotFrame(frame, frame_idx);
        if (new_frame_idx != -1) {
            frame_idx = new_frame_idx;
            data->time = (float64)frame_idx;
        }
    }

    // stats::get_group_properties();
    /*
    if (stats->properties.size() > 0) {
            const auto& g = stats->groups.front();
            const auto& i = stats->instances[g.instance_avg_idx];
            const auto& p = stats->properties[i.property_beg_idx];
            auto frame = ImGui::BeginPlotFrame("Prop0", ImVec2(0, 100), 0, p.count, -2.f, 2.f);
            ImGui::PlotFrameLine(frame, "najs", (float*)p.data);
            ImGui::EndPlotFrame(frame);
    }
    */
    ImGui::End();
}

static void draw_distribution_window(ApplicationData* data) {
	ImGui::Begin("Distributions", &data->statistics.show_distribution_window, ImGuiWindowFlags_NoFocusOnAppearing);
	for (int i = 0; i < stats::get_property_count(); i++) {
		ImGui::PushID(i);
		auto prop_id = stats::get_property(i);
		auto hist = stats::get_property_histogram(prop_id, 0);
		if (!hist) continue;
		//ImGui::PlotHistogram(stats::get_property_name(prop_id), [](void* data, int32 idx) -> float { return ((float*)data)[idx]; }, prop_data->bins.data, prop_data->bins.count);
		ImGui::PlotHistogramExtended(stats::get_property_name(prop_id), hist->bins.data, (int32)hist->bins.count, 0, 0, 0, 0, hist->bin_range.x, hist->bin_range.y, ImVec2(0, 100));
		ImGui::PopID();
	}
	ImGui::End();
}

static void draw_ramachandran(ApplicationData* data) {
	constexpr vec2 res(512, 512);
	ImGui::SetNextWindowContentSize(ImVec2(res.x, res.y));
	ImGui::Begin("Ramachandran", &data->ramachandran.show_window, ImGuiWindowFlags_NoFocusOnAppearing);

	int32 num_frames = data->mol_data.dynamic.trajectory ? data->mol_data.dynamic.trajectory.num_frames : 0;
	float range_min = (float)data->ramachandran.frame_range_min;
	float range_max = (float)data->ramachandran.frame_range_max;

	ImGui::SliderFloat("opacity", &data->ramachandran.opacity, 0.f, 2.f);
	ImGui::SliderFloat("radius", &data->ramachandran.radius, 0.1f, 2.f);
	ImGui::RangeSliderFloat("framerange", &range_min, &range_max, 0, (float)math::max(0, num_frames - 1));
	ImGui::SameLine();
	if (ImGui::Button("reset")) {
		range_min = 0;
		range_max = num_frames - 1;
	}
	data->ramachandran.frame_range_min = (int32)range_min;
	data->ramachandran.frame_range_max = (int32)range_max;

	int32 frame = (int32)data->time;
	Array<BackboneAngles> accumulated_angles = get_backbone_angles(data->ramachandran.backbone_angles, data->ramachandran.frame_range_min, data->ramachandran.frame_range_max - data->ramachandran.frame_range_min);
	Array<BackboneAngles> current_angles = get_backbone_angles(data->ramachandran.backbone_angles, frame);

	ramachandran::clear_accumulation_texture();

	const vec4 ordinary_color(1.f, 1.f, 1.f, 0.1f * data->ramachandran.opacity);
	ramachandran::compute_accumulation_texture(accumulated_angles, ordinary_color, data->ramachandran.radius);

	const vec4 highlight_color(1.f, 1.f, 0.f, 1.0f);
	ramachandran::compute_accumulation_texture(current_angles, highlight_color, data->ramachandran.radius * 2.f, 0.1f);

	float dim = math::min(ImGui::GetWindowWidth(), ImGui::GetWindowHeight());
	ImVec2 win_pos = ImGui::GetCursorScreenPos();
	ImVec2 canvas_size(dim, dim);
	ImDrawList* dl = ImGui::GetWindowDrawList();

	ImVec2 x0 = win_pos;
	ImVec2 x1(win_pos.x + canvas_size.x, win_pos.y + canvas_size.y);

	dl->ChannelsSplit(2);
	dl->ChannelsSetCurrent(0);
	// ImGui::Image((ImTextureID)ramachandran::segmentation_tex, canvas_size);
	dl->AddImage((ImTextureID)(intptr_t)ramachandran::get_segmentation_texture(), x0, x1);
	dl->ChannelsSetCurrent(1);
	// ImGui::Image((ImTextureID)ramachandran::accumulation_tex, canvas_size);
	dl->AddImage((ImTextureID)(intptr_t)ramachandran::get_accumulation_texture(), x0, x1);
	dl->ChannelsMerge();

	dl->ChannelsSetCurrent(0);

	ImGui::End();
}

static void reset_view(ApplicationData* data) {
    ASSERT(data);
    if (!data->mol_data.dynamic.molecule) return;

    vec3 min_box, max_box;
    compute_bounding_box(&min_box, &max_box, data->mol_data.dynamic.molecule.atom_positions);
    vec3 size = max_box - min_box;
    vec3 cent = (min_box + max_box) * 0.5f;

    data->controller.look_at(cent, cent + size * 2.f);
    data->camera.position = data->controller.position;
    data->camera.orientation = data->controller.orientation;
    data->camera.near_plane = 1.f;
    data->camera.far_plane = math::length(size) * 10.f;
}

static void init_main_framebuffer(MainFramebuffer* fbo, int width, int height) {
    ASSERT(fbo);

    bool attach_textures = false;
    if (!fbo->id) {
        glGenFramebuffers(1, &fbo->id);
        attach_textures = true;
    }

    if (!fbo->tex_depth) glGenTextures(1, &fbo->tex_depth);
    if (!fbo->tex_color) glGenTextures(1, &fbo->tex_color);
    if (!fbo->tex_normal) glGenTextures(1, &fbo->tex_normal);
    if (!fbo->tex_picking) glGenTextures(1, &fbo->tex_picking);

    glBindTexture(GL_TEXTURE_2D, fbo->tex_depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, fbo->tex_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, fbo->tex_normal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16, width, height, 0, GL_RG, GL_UNSIGNED_SHORT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, fbo->tex_picking);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    fbo->width = width;
    fbo->height = height;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo->id);
    if (attach_textures) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, fbo->tex_depth, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo->tex_color, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, fbo->tex_normal, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, fbo->tex_picking, 0);
    }

    ASSERT(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void destroy_main_framebuffer(MainFramebuffer* fbo) {
    ASSERT(fbo);
    if (fbo->id) glDeleteFramebuffers(1, &fbo->id);
    if (fbo->tex_depth) glDeleteTextures(1, &fbo->tex_depth);
    if (fbo->tex_color) glDeleteTextures(1, &fbo->tex_color);
    if (fbo->tex_normal) glDeleteTextures(1, &fbo->tex_normal);
    if (fbo->tex_picking) glDeleteTextures(1, &fbo->tex_picking);
}

static void free_mol_data(ApplicationData* data) {
    if (data->mol_data.dynamic.molecule) {
        free_molecule_structure(&data->mol_data.dynamic.molecule);
    }
    if (data->mol_data.dynamic.trajectory) {
		free_trajectory(&data->mol_data.dynamic.trajectory);
    }
    free_backbone_angles_trajectory(&data->ramachandran.backbone_angles);
    data->ramachandran.backbone_angles = {};
    data->ramachandran.current_backbone_angles = {};
	stats::clear_instances();
	stats::clear_property_data();
}

static void load_molecule_data(ApplicationData* data, CString file) {
    ASSERT(data);
    if (file.count > 0) {
        CString ext = get_file_extension(file);
        printf("'%s'\n", ext.beg());
        if (compare_n(ext, "pdb", 3, true)) {
            free_mol_data(data);
			free_string(&data->files.molecule);
			free_string(&data->files.trajectory);
            allocate_and_load_pdb_from_file(&data->mol_data.dynamic, file);

            if (!data->mol_data.dynamic.molecule) {
                printf("ERROR! Failed to load pdb file.\n");
                return;
            }

			data->files.molecule = allocate_string(file);
            data->mol_data.atom_radii = compute_atom_radii(data->mol_data.dynamic.molecule.atom_elements);
            if (data->mol_data.dynamic.trajectory) {
                init_backbone_angles_trajectory(&data->ramachandran.backbone_angles, data->mol_data.dynamic);
                compute_backbone_angles_trajectory(&data->ramachandran.backbone_angles, data->mol_data.dynamic);
				stats::compute_stats(data->mol_data.dynamic);
            }
        } else if (compare_n(ext, "gro", 3, true)) {
            free_mol_data(data);
			free_string(&data->files.molecule);
			free_string(&data->files.trajectory);
            allocate_and_load_gro_from_file(&data->mol_data.dynamic.molecule, file);

            if (!data->mol_data.dynamic.molecule) {
                printf("ERROR! Failed to load gro file.\n");
				return;
            }

			data->files.molecule = allocate_string(file);
            data->mol_data.atom_radii = compute_atom_radii(data->mol_data.dynamic.molecule.atom_elements);
        } else if (compare_n(ext, "xtc", 3, true)) {
            if (!data->mol_data.dynamic.molecule) {
                printf("ERROR! Must have molecule loaded before trajectory can be loaded!\n");
            } else {
				if (data->mol_data.dynamic.trajectory) free_trajectory(&data->mol_data.dynamic.trajectory);
                if (!load_and_allocate_trajectory(&data->mol_data.dynamic.trajectory, file)) {
                    printf("ERROR! Problem loading trajectory\n");
                    return;
                }
                if (data->mol_data.dynamic.trajectory) {
					if (data->mol_data.dynamic.trajectory.num_atoms != data->mol_data.dynamic.molecule.atom_positions.count) {
						printf("ERROR! The number of atoms in the molecule does not match the number of atoms in the trajectory\n");
						free_trajectory(&data->mol_data.dynamic.trajectory);
						data->mol_data.dynamic.trajectory = {};
						return;
					}
					data->files.trajectory = allocate_string(file);
					init_backbone_angles_trajectory(&data->ramachandran.backbone_angles, data->mol_data.dynamic);
                    read_trajectory_async(&data->mol_data.dynamic.trajectory, [data]() {
						compute_backbone_angles_trajectory(&data->ramachandran.backbone_angles, data->mol_data.dynamic);
						stats::compute_stats(data->mol_data.dynamic);
					});
                }
            }
        } else {
            printf("ERROR! file extension not supported!\n");
        }
    }
}

static Representation::Type get_rep_type(CString str) {
    if (compare(str, "VDW")) return Representation::VDW;
    else if (compare(str, "LICORICE")) return Representation::LICORICE;
    else if (compare(str, "RIBBONS")) return Representation::RIBBONS;
    else return Representation::VDW;
}

static CString get_rep_type_name(Representation::Type type) {
    switch(type) {
        case Representation::VDW: return "VDW";
        case Representation::LICORICE: return "LICORICE";
        case Representation::RIBBONS: return "RIBBONS";
        default: return "UNKNOWN";
    }
}

static ColorMapping get_color_mapping(CString str) {
    if (compare(str, "STATIC_COLOR")) return ColorMapping::STATIC_COLOR;
    else if (compare(str, "CPK")) return ColorMapping::CPK;
    else if (compare(str, "RES_ID")) return ColorMapping::RES_ID;
    else if (compare(str, "RES_INDEX")) return ColorMapping::RES_INDEX;
    else if (compare(str, "CHAIN_ID")) return ColorMapping::CHAIN_ID;
    else if (compare(str, "CHAIN_INDEX")) return ColorMapping::CHAIN_INDEX;
    else return ColorMapping::CPK;
}

static CString get_color_mapping_name(ColorMapping mapping) {
    switch(mapping) {
        case ColorMapping::STATIC_COLOR: return "STATIC_COLOR";
        case ColorMapping::CPK: return "CPK";
        case ColorMapping::RES_ID: return "RES_ID";
        case ColorMapping::RES_INDEX: return "RES_INDEX";
        case ColorMapping::CHAIN_ID: return "CHAIN_ID";
        case ColorMapping::CHAIN_INDEX: return "CHAIN_INDEX";
        default: return "UNKNOWN";
    }
}

static void load_workspace(ApplicationData* data, CString file) {
	*data = {};

	String txt = allocate_and_read_textfile(file);
	CString c_txt = txt;
	CString line;
	while (extract_line(line, c_txt)) {
		if (compare(line, "[Files]")) {
			while (c_txt.beg() != c_txt.end() && c_txt[0] != '[') {
				extract_line(line, c_txt);
				if (compare(line, "MoleculeFile=")) data->files.molecule = allocate_string(line.substr(13));
				if (compare(line, "TrajectoryFile=")) data->files.trajectory = allocate_string(line.substr(15));
			}
		}
		else if (compare(line, "[Representation]")) {

		}
		else if (compare(line, "[Group]")) {

		}
		else if (compare(line, "[Property]")) {

		}
		else if (compare(line, "[RenderSettings]")) {

		}
	}

	// Store Loaded Molecule File Relative Path
	// (Store Loaded Trajectory File Relative Path)
	// Store Representations
	// Store Groups and Properties
	// Store Rendersettings
	// ...

	if (txt.data) FREE(txt.data);
}

static void save_workspace(ApplicationData* data, CString file) {
	FILE* fptr = fopen(file.beg(), "w");
	if (!fptr) {
		printf("ERROR! Could not save workspace to file '%s'\n", file.beg());
        return;
	}

    // @TODO: Make relative paths
	fprintf(fptr, "[Files]\n");
	fprintf(fptr, "MoleculeFile=%s\n", data->files.molecule ? data->files.molecule.beg() : "");
    fprintf(fptr, "TrajectoryFile=%s\n", data->files.trajectory ? data->files.trajectory.beg() : "");
	fprintf(fptr, "\n");

	// REPRESENTATIONS
    for (const auto& rep : data->representations.data) {
        fprintf(fptr, "[Representation]\n");
		fprintf(fptr, "Name=%s\n", rep.name.beg());
		fprintf(fptr, "Filter=%s\n", rep.name.beg());
		fprintf(fptr, "Type=%s\n", get_rep_type_name(rep.type).beg());
		fprintf(fptr, "ColorMapping=%s\n", get_color_mapping_name(rep.color_mapping).beg());
		fprintf(fptr, "Enabled=%i\n", rep.enabled);
		if (rep.color_mapping == ColorMapping::STATIC_COLOR)
			fprintf(fptr, "StaticColor=%i\n", rep.enabled);
		if (rep.type == Representation::VDW || Representation::LICORICE)
			fprintf(fptr, "RadiiScale=%g\n", rep.radii_scale);
		else if (rep.type == Representation::RIBBONS) {
			fprintf(fptr, "NumSubdivisions=%i\n", rep.num_subdivisions);
			fprintf(fptr, "Tension=%g\n", rep.tension);
		}
		fprintf(fptr, "\n");
    }

	// GROUPS
	for (int i = 0; i < stats::get_group_count(); i++) {
		auto id = stats::get_group(i);
		fprintf(fptr, "[Group]\n");
		fprintf(fptr, "Name=%s\n", stats::get_group_name_buf(id)->beg());
		fprintf(fptr, "Args=%s\n", stats::get_group_args_buf(id)->beg());
		fprintf(fptr, "\n");
	}

	// PROPERTIES
	for (int i = 0; i < stats::get_property_count(); i++) {
		auto id = stats::get_property(i);
		fprintf(fptr, "[Property]\n");
		fprintf(fptr, "Name=%s\n", stats::get_property_name_buf(id)->beg());
		fprintf(fptr, "Args=%s\n", stats::get_property_args_buf(id)->beg());
		fprintf(fptr, "\n");
	}

	fprintf(fptr, "[RenderSettings]\n");
	fprintf(fptr, "SsaoEnabled=%i\n", data->ssao.enabled ? 1 : 0);
	fprintf(fptr, "SsaoIntensity=%g\n", data->ssao.intensity);
	fprintf(fptr, "SsaoRadius=%g\n", data->ssao.radius);
	fprintf(fptr, "\n");

	fprintf(fptr, "[Camera]\n");
	fprintf(fptr, "Pos:%g,%g,%g\n", data->camera.position.x, data->camera.position.y, data->camera.position.z);
	fprintf(fptr, "Rot:%g,%g,%g,%g\n", data->camera.orientation.x, data->camera.orientation.y, data->camera.orientation.z, data->camera.orientation.w);
	fprintf(fptr, "\n");

	fclose(fptr);
}

static void create_default_representation(ApplicationData* data) {
    ASSERT(data);
    if (!data->mol_data.dynamic.molecule) return;

    auto& rep = data->representations.data.push_back({});
    rep.colors.count = data->mol_data.dynamic.molecule.atom_positions.count;
    rep.colors.data = (uint32*)MALLOC(rep.colors.count * sizeof(uint32));
    compute_atom_colors(rep.colors, data->mol_data.dynamic.molecule, rep.color_mapping);
}

static void remove_representation(ApplicationData* data, int idx) {
    ASSERT(idx < data->representations.data.count);
    auto& rep = data->representations.data[idx];
    if (rep.colors) {
        FREE(rep.colors.data);
    }
    data->representations.data.remove(&rep);
}

static void reset_representations(ApplicationData* data) {
    ASSERT(data);
    for (auto& rep : data->representations.data) {
        if (rep.colors) {
            FREE(rep.colors.data);
        }
        rep.colors.count = data->mol_data.dynamic.molecule.atom_positions.count;
        rep.colors.data = (uint32*)MALLOC(rep.colors.count * sizeof(uint32));

        compute_atom_colors(rep.colors, data->mol_data.dynamic.molecule, rep.color_mapping,
                            ImGui::ColorConvertFloat4ToU32(vec_cast(rep.static_color)));
        DynamicArray<bool> mask(data->mol_data.dynamic.molecule.atom_elements.count, false);
        filter::compute_filter_mask(mask, data->mol_data.dynamic, rep.filter.buffer);
        filter::filter_colors(rep.colors, mask);
    }
}

