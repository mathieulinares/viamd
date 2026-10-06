#include <core/md_compiler.h>

#if MD_COMPILER_MSVC
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#   pragma warning( disable : 26812 4244 )
#endif

#define IMGUI_DEFINE_MATH_OPERATORS


#include <md_util.h>
#include <md_gl.h>
#include <md_gfx.h>
#include <md_filter.h>
#include <md_script.h>
#include <md_system.h>
#include <md_xvg.h>
#include <md_csv.h>
#include <md_lammps.h>
#include <md_pdb.h>

#include <core/md_log.h>
#include <core/md_str.h>
#include <core/md_array.h>
#include <core/md_allocator.h>
#include <core/md_arena_allocator.h>
#include <core/md_ring_allocator.h>
#include <core/md_tracking_allocator.h>
#include <core/md_simd.h>
#include <core/md_os.h>
#include <core/md_unit.h>
#include <core/md_str_builder.h>
#include <core/md_parse.h>
#include <core/md_hash.h>

#include <gfx/gl.h>
#include <gfx/gl_utils.h>
#include <gfx/camera.h>
#include <gfx/camera_utils.h>
#include <gfx/immediate_draw_utils.h>
#include <gfx/postprocessing_utils.h>
#include <gfx/volumerender_utils.h>

#include <resource_path.h>
#include <app_settings.h>
#include <display_units.h>
#include <imgui_widgets.h>
#include <implot_widgets.h>
#include <task_system.h>
#include <color_utils.h>
#include <image.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <app/imgui_impl_opengl3.h>

#include <implot.h>
#include <implot_internal.h>

#include <float.h>

#include <stdio.h>
#include <cmath>
#include <string>
#include <algorithm>

#include <viamd.h>
#include <script_reference.h>
#include <viamd_event.h>
#include <event.h>

#define EXPERIMENTAL_GFX_API 0
#define COMPILATION_TIME_DELAY_IN_SECONDS 1.0
#define NOTIFICATION_DISPLAY_TIME_IN_SECONDS 5.0
#define MEASURE_EVALUATION_TIME 1
#define VIAMD_RECOMPUTE_ORBITAL_PER_FRAME 0
#define VIS_FLAGS (MD_SCRIPT_VISUALIZE_ATOMS | MD_SCRIPT_VISUALIZE_GEOMETRY | MD_SCRIPT_VISUALIZE_TEXT)

// Global data for application
static md_allocator_i* frame_alloc = 0; // Linear allocator for scratch data which only is valid for the frame and then is reset
static md_allocator_i* persistent_alloc = 0;

static bool use_gfx = false;

constexpr str_t shader_output_snippet = STR_LIT(R"(
layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec4 out_velocity;
layout(location = 3) out vec4 out_atom_index;

vec2 encode_normal (vec3 n) {
   float p = sqrt(n.z * 8 + 8);
   return n.xy / p + 0.5;
}

vec4 encode_index(uint index) {
    return vec4(
        (index & 0x000000FFU) >> 0U,
        (index & 0x0000FF00U) >> 8U,
        (index & 0x00FF0000U) >> 16U,
        (index & 0xFF000000U) >> 24U) / 255.0;
}

vec2 compute_ss_vel(vec3 view_coord, vec3 view_vel) {
    vec3 prev_view_coord = view_coord - view_vel;
    vec4 prev_clip_coord = u_curr_view_to_prev_clip * vec4(prev_view_coord, 1);

    vec4 clip_coord = u_view_to_clip * vec4(view_coord, 1);
    vec2 curr_ndc = clip_coord.xy / clip_coord.w;
    vec2 prev_ndc = prev_clip_coord.xy / prev_clip_coord.w;
    vec2 ss_vel = (curr_ndc - prev_ndc) * 0.5 + (u_jitter_uv.xy - u_jitter_uv.zw);
    return ss_vel;
}

void write_fragment(vec3 view_coord, vec3 view_vel, vec3 view_normal, vec4 color, uint atom_index) {
   out_color  = color;
   out_normal = vec4(encode_normal(view_normal), 0, 0);
   out_velocity = vec4(compute_ss_vel(view_coord, view_vel), 0, 0);
   out_atom_index = encode_index(atom_index);
}
)");

constexpr str_t shader_output_snippet_lean_and_mean = STR_LIT(R"(
void write_fragment(vec3 view_coord, vec3 view_vel, vec3 view_normal, vec4 color, uint atom_index) {
}
)");


static double frame_to_time(double frame, const ApplicationState& data) {
    const int64_t num_frames = md_array_size(data.timeline.x_values);
    ASSERT(num_frames);
    const int64_t f0 = CLAMP((int64_t)frame + 0, 0, num_frames - 1);
    const int64_t f1 = CLAMP((int64_t)frame + 1, 0, num_frames - 1);
    return lerp(data.timeline.x_values[f0], data.timeline.x_values[f1], fract(frame));
}

// Try to map time t back into frame
static double time_to_frame(double time, const md_array(float) frame_times) {
    const int64_t num_frames = md_array_size(frame_times);
    if (!num_frames) return 0.0;

    const double beg = frame_times[0];
    const double end = frame_times[num_frames - 1];
    time = CLAMP(time, beg, end);

    // Estimate the frame
    const double frame_est = CLAMP(((time - beg) / (end-beg)) * (num_frames - 1), 0, num_frames - 1);

    int64_t prev_frame_idx = CLAMP((int64_t)frame_est,     0, num_frames - 1);
    int64_t next_frame_idx = CLAMP((int64_t)frame_est + 1, 0, num_frames - 1);

    if (time < (double)frame_times[prev_frame_idx]) {
        // Linear search down
        for (prev_frame_idx = prev_frame_idx - 1; prev_frame_idx >= 0; --prev_frame_idx) {
            next_frame_idx = prev_frame_idx + 1;
            if ((double)frame_times[prev_frame_idx] <= time && time <= (double)frame_times[next_frame_idx])
                break;
        }
    }
    else if (time > (double)frame_times[next_frame_idx]) {
        // Linear search up
        for (next_frame_idx = next_frame_idx + 1; next_frame_idx < num_frames; ++next_frame_idx) {
            prev_frame_idx = next_frame_idx - 1;
            if ((double)frame_times[prev_frame_idx] <= time && time <= (double)frame_times[next_frame_idx])
                break;
        }
    }

    // Compute true fraction between timestamps
    double t = (time - (double)frame_times[prev_frame_idx]) / ((double)frame_times[next_frame_idx] - (double)frame_times[prev_frame_idx]);
    t = CLAMP(t, 0.0, 1.0);

    // Compose frame value (base + fraction)
    return (double)prev_frame_idx + t;
}

static void update_view_param(ApplicationState* state);

//static void update_density_volume_texture(ApplicationState* state);

static void render(ApplicationState* state);
static void draw_representations_opaque(ApplicationState* state);
static void draw_representations_opaque_lean_and_mean(ApplicationState* state, uint32_t mask = 0xFFFFFFFFU);
static void draw_representations_transparent(ApplicationState* state);

static void draw_load_dataset_window(ApplicationState* state);
static void draw_main_menu(ApplicationState* state);
static void draw_context_popup(ApplicationState* state, const PickingHit& hit);
static void draw_selection_query_window(ApplicationState* state);
static void draw_selection_grow_window(ApplicationState* state);
static void draw_animation_window(ApplicationState* state);
static void draw_representations_window(ApplicationState* state);
static void draw_timeline_window(ApplicationState* state);
static void draw_distribution_window(ApplicationState* state);
static void draw_async_task_window(ApplicationState* state);
static void draw_script_editor_window(ApplicationState* state);
static void draw_script_reference_window(ApplicationState* state);
static void open_script_reference(ApplicationState* state, str_t topic, bool take_focus = true);
static void draw_coordinate_system_widget_window(ViewTransform* target, const ViewTransform& current);

static void draw_debug_window(ApplicationState* state);
static void draw_property_export_window(ApplicationState* state);
static void draw_structure_export_window(ApplicationState* state);
static void draw_notifications_window();

static void update_md_buffers(ApplicationState* state);

static bool export_xvg(const float* column_data[], const char* column_labels[], size_t num_columns, size_t num_rows, str_t filename);
static bool export_csv(const float* column_data[], const char* column_labels[], size_t num_columns, size_t num_rows, str_t filename);

static void create_screenshot(str_t path);

static void movie_recording_start(ApplicationState* state);
static void movie_recording_stop(ApplicationState* state);
static void movie_shutdown(ApplicationState* state);
static void movie_add_keyframe(ApplicationState* state);
static void update_movie_recording(ApplicationState* state);
static void update_movie_preview(ApplicationState* state);
static void update_movie_history(ApplicationState* state);
static void movie_undo(ApplicationState* state);
static void movie_redo(ApplicationState* state);
static void draw_movie_window(ApplicationState* state);
static void draw_movie_timeline_window(ApplicationState* state);
static void draw_movie_recording_banner(ApplicationState* state);
static void movie_draw_camera_path(ApplicationState* state);
static float dof_focus_depth(const ApplicationState* state, const ViewTransform& view);
static bool movie_key_look_at_atom(ApplicationState* state, int key_idx, int32_t atom);
static bool movie_draw_timeline_markers(ApplicationState* state);
static void movie_blit_preview(ApplicationState* state);
static void movie_capture_frame(ApplicationState* state);
static void movie_overlays_draw(ImDrawList* dl, ImVec2 pos, ImVec2 size, double time, const ApplicationState* state);
static void movie_sort_keyframes(ApplicationState* state);
static void movie_apply_time(ApplicationState* state, double time, bool apply_camera);
static void movie_follow_update(ApplicationState* state);
static double movie_duration(const ApplicationState* state);
static double movie_trajectory_frame(const ApplicationState* state, double time);
static int movie_num_frames(const ApplicationState* state);
static void movie_restore_state(ApplicationState* state);

// The sizes offered in the Settings menu. The stored setting is the size itself, not an
// index into this table, so the table can change without invalidating anyone's .ini.
static const float font_sizes[] = { 10.0f, 12.0f, 14.0f, 16.0f, 18.0f, 20.0f, 24.0f, 30.0f, 36.0f, 48.0f, 64.0f, 72.0f };
static const char* font_size_names[] = { "10", "12", "14", "16", "18", "20", "24", "30", "36", "48", "64", "72" };

static int nearest_font_size_index(float size) {
    int   best_idx  = 0;
    float best_dist = FLT_MAX;
    for (int i = 0; i < (int)ARRAY_SIZE(font_sizes); ++i) {
        const float dist = fabsf(font_sizes[i] - size);
        if (dist < best_dist) {
            best_dist = dist;
            best_idx  = i;
        }
    }
    return best_idx;
}

// Also the app_settings apply hook, so a size read from the .ini takes effect on the first frame.
static void apply_font_size(void* user_data) {
    ApplicationState* state = (ApplicationState*)user_data;
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontSizeBase = state->settings.font_size;
    style._NextFrameFontSizeBase = style.FontSizeBase;  // From the demo, seems like a temporary fix
}

// timeline.x_values and view_range are stored in display units, so a change to the time preference
// has to be carried into them. The raw frame times are still with the trajectory, so this re-derives
// rather than compounding factors, and the visible range is moved by the ratio so the user keeps
// looking at the same stretch of trajectory.
static void update_timeline_time_unit(ApplicationState* state) {
    ASSERT(state);

    if (state->timeline.units_version == display_units::version()) {
        return;
    }
    state->timeline.units_version = display_units::version();

    const double prev_scl = state->timeline.time_scale;
    const double time_scl = display_units::factor(&state->timeline.time_unit, run_time_unit(state));
    state->timeline.time_scale = time_scl;

    if (time_scl == prev_scl) {
        return;
    }

    const double* frame_times = run_frame_times(state);
    const size_t  num_frames  = md_array_size(state->timeline.x_values);
    if (frame_times) {
        for (size_t i = 0; i < num_frames; ++i) {
            state->timeline.x_values[i] = (float)(frame_times[i] * time_scl);
        }
    }

    const double ratio = prev_scl != 0.0 ? time_scl / prev_scl : 1.0;
    state->timeline.view_range.beg_x *= ratio;
    state->timeline.view_range.end_x *= ratio;
}

static void modify_selection(ApplicationState* state, md_bitfield_t* atom_mask, SelectionOperator op = SelectionOperator::Set) {
    ASSERT(state);
    modify_field(&state->selection.selection_mask, atom_mask, op);
}

int main(int argc, char** argv) {
#if DEBUG
    persistent_alloc = md_tracking_allocator_create(md_get_heap_allocator());
#elif RELEASE
    persistent_alloc = md_get_heap_allocator();
#else
#error "Must define DEBUG or RELEASE"
#endif
    frame_alloc = md_vm_arena_create(GIGABYTES(4));

    struct NotificationState {
        md_mutex_t lock;
        uint64_t hash;
        md_tick_t time;
    };

    NotificationState notification_state = {
        .lock = md_mutex_create(),
        .hash = 0,
        .time = 0
    };

    md_logger_i notification_logger = {
        (md_logger_o*)&notification_state,
        [](struct md_logger_o* inst, enum md_log_type_t log_type, const char* msg) {
            NotificationState& state = *(NotificationState*)inst;            

            // Prevent spamming the logger with the same message by comparing its hash
            const md_tick_t time = md_tick_now();
            const uint64_t hash = md_hash64(msg, strlen(msg), 0);

            if (md_tick_to_seconds(time - state.time) < 1.0 && hash == state.hash) {
                return;
            }
            state.hash = hash;
            state.time = time;
            
            ImGuiToastType toast_type = ImGuiToastType_None;
            switch (log_type) {
            case MD_LOG_TYPE_INFO:
                toast_type = ImGuiToastType_Info;
                break;
            case MD_LOG_TYPE_ERROR:
                toast_type = ImGuiToastType_Error;
                break;
            case MD_LOG_TYPE_DEBUG:
            default:
                break;
            }
            if (toast_type != ImGuiToastType_None) {
                // @NOTE: This needs to be protected with a mutex as it pushes to an internal vector
                md_mutex_lock(&state.lock);
                ImGui::InsertNotification(ImGuiToast(toast_type, (uint64_t)(NOTIFICATION_DISPLAY_TIME_IN_SECONDS * 1000), msg));
                md_mutex_unlock(&state.lock);
            }
        }
    };

    md_log_register(&notification_logger);

    ApplicationState state;
    ViamdEventHandler event_handler(&state);

    state.allocator.persistent = persistent_alloc;
    state.allocator.frame = frame_alloc;
    state.file_queue.ring = md_ring_allocator_create(md_alloc(persistent_alloc, MEGABYTES(1)), MEGABYTES(1));
    // One arena per dataset, held by the system and the state that share it. Both are rewound
    // rather than destroyed between loads, so this handle is set once and never reassigned.
    state.mold.sys.alloc   = md_arena_allocator_create(persistent_alloc, MEGABYTES(1));
    state.mold.state.alloc = state.mold.sys.alloc;

    md_temp_arena_system_init();

    md_bitfield_init(&state.selection.selection_mask, persistent_alloc);
    md_bitfield_init(&state.selection.highlight_mask, persistent_alloc);
    md_bitfield_init(&state.selection.query.mask, persistent_alloc);
    md_bitfield_init(&state.selection.grow.mask, persistent_alloc);
    md_bitfield_init(&state.operations.selection_mask, persistent_alloc);
    md_bitfield_init(&state.movie.follow_mask, persistent_alloc);
    md_bitfield_init(&state.operations.recenter_query.mask, persistent_alloc);
    md_bitfield_init(&state.representation.visibility_mask, persistent_alloc);

    // Init platform
    VIAMD_LOG_DEBUG("Initializing GL...");
    if (!application::initialize(&state.app, 0, 0, STR_LIT("VIAMD"))) {
        VIAMD_LOG_ERROR("Could not initialize application...\n");
        return -1;
    }

    // Application settings live in the ImGui .ini. Bind first, then initialize: that call
    // reads the file, so the bound values are current by the time the loop starts.
    app_settings::bind(STR_LIT("keep_representations"), &state.settings.keep_representations);
    app_settings::bind(STR_LIT("font_size"), &state.settings.font_size);
    app_settings::on_apply(apply_font_size, &state);
    display_units::register_settings();
    app_settings::initialize();

#if MD_ENABLE_GPU
    VIAMD_LOG_DEBUG("Initializing GPU device...");
    state.gpu_device = md_gpu_device_create(nullptr);
    if (!state.gpu_device) {
        const char* reason = md_gpu_last_error();
        VIAMD_LOG_ERROR("Failed to create GPU device: %s", reason ? reason : "unknown");
    } else {
        // The scratch belongs to the device, not to whichever component happens to evaluate
        // first. Components borrow it; see the note on ApplicationState.
        state.gpu_stream = md_gpu_stream_default(state.gpu_device, MD_GPU_STREAM_COMPUTE);

        md_gpu_texture_desc_t vol_desc = {
            .type            = MD_GPU_TEX_3D,
            .format          = MD_GPU_FORMAT_R32_FLOAT,
            .usage           = MD_GPU_TEX_STORAGE,
            .width           = 512,
            .height          = 512,
            .depth_or_layers = 512,
            .label           = "Evaluation volume",
        };
        state.gpu_volume = md_gpu_texture_create(state.gpu_stream, &vol_desc);
        if (!state.gpu_volume) {
            VIAMD_LOG_ERROR("Failed to create the GPU evaluation volume: %s", md_gpu_last_error());
        }
    }
#endif

    state.app.window.vsync = true;
    state.app.file_drop.user_data = &state;
    state.app.file_drop.callback = [](size_t num_files, const str_t file_paths[], void* user_data) {
        ApplicationState* state = (ApplicationState*)user_data;
        ASSERT(state);

        for (size_t i = 0; i < num_files; ++i) {
            file_queue_push(&state->file_queue, file_paths[i], FileFlags_None);
        }
    };

    VIAMD_LOG_DEBUG("Initializing framebuffer...");
    gbuffer_init(&state.gbuffer, state.app.framebuffer.width, state.app.framebuffer.height);
    picking_surface_init(&state.picking_surface, interaction_surface_main);

    for (int i = 0; i < (int)ARRAY_SIZE(state.view.jitter.sequence); ++i) {
        state.view.jitter.sequence[i].x = md_halton(i + 1, 2);
        state.view.jitter.sequence[i].y = md_halton(i + 1, 3);
    }



    // Init subsystems
    VIAMD_LOG_DEBUG("Initializing immediate draw...");
    immediate::initialize();
    state.gfx.world   = immediate::queue_create("world");
	state.gfx.overlay = immediate::queue_create("overlay");

    VIAMD_LOG_DEBUG("Initializing post processing...");

    postprocess_pipeline::initialize(state.gbuffer.width, state.gbuffer.height);
    VIAMD_LOG_DEBUG("Initializing volume...");
    volume::initialize();
    VIAMD_LOG_DEBUG("Initializing task system...");
    md_os_sys_info_t sys_info = {0};
	md_os_sys_info_query(&sys_info);

    int num_threads = VIAMD_NUM_WORKER_THREADS == 0 ? sys_info.num_physical_cores : VIAMD_NUM_WORKER_THREADS;
	num_threads = CLAMP(num_threads, 2, sys_info.num_physical_cores);
    task_system::initialize((size_t)num_threads);

    md_gl_initialize();
    state.gl.shaders                = md_gl_shaders_create(shader_output_snippet);
    state.gl.shaders_lean_and_mean  = md_gl_shaders_create(shader_output_snippet_lean_and_mean);

    // Which of these are open is part of a workspace; the components register their own on initialize
    workspace_register_window("Timelines",       &state.timeline.show_window);
    workspace_register_window("Distributions",   &state.distributions.show_window);
    workspace_register_window("Representations", &state.representation.show_window);
    workspace_register_window("ScriptEditor",    &state.show_script_window);
    workspace_register_window("Animation",       &state.animation.show_window);
    workspace_register_window("Movie",           &state.movie.show_window);
    workspace_register_window("MovieTimeline",   &state.movie.show_timeline_window);

    viamd::event_system_broadcast_event(viamd::EventType_ViamdInitialize, viamd::EventPayloadType_ApplicationState, &state);

#if EXPERIMENTAL_GFX_API
    md_gfx_initialize(state.gbuffer.width, state.gbuffer.height, 0);
#endif

    ImGui::init_theme();

    state.editor.SetLanguage(script_editor::language());
    state.editor.SetPalette(TextEditor::GetDarkPalette());
    state.editor.SetInsertSpacesOnTabs(true);
    script_editor::enable_autocomplete(state.editor, &state.mold.sys);

    {
#ifdef VIAMD_DEFAULT_DATASET
        {
            md_strb_t sb = md_strb_create(frame_alloc);
            str_t path = viamd::resource_path(&sb, STR_LIT(VIAMD_DEFAULT_DATASET));
            if (md_path_is_valid(path)) {
                // @NOTE: We want explicitly to disable writing of cache files for the default dataset
                // The motivation is that the dataset may reside in a shared folder on the system that has no write access.
                file_queue_push(&state.file_queue, path, FileFlags_DisableCacheWrite);
                state.editor.SetText("s1 = resname(\"ALA\")[2:8];\nd1 = distance(10,30);\na1 = angle(2,1,3) in resname(\"ALA\");\nr = rdf(element('C'), element('H'), 10.0);\nv = sdf(s1, element('H'), 10.0);\n{lin,plan,iso} = shape_weights(all);");
            }
        }
#endif
        if (argc > 1) {
            // Assume argv[1..] are files to load
            // Currently we do not support any command line flags
            // So anything here which is a file path is assumed to be a file to load
            for (int i = 1; i < argc; ++i) {
                str_t path = str_from_cstr(argv[i]);
                if (md_path_is_valid(path)) {
                    file_queue_push(&state.file_queue, path);
                }
            }
        }
    }

#if EXPERIMENTAL_SDF == 1
    draw::scan::test_scan();
#endif
    bool time_changed = true;
    bool time_stopped = true;

    //bool demo_window = true;

    // Main loop
    while (!state.app.window.should_close) {
        application::update(&state.app);
        
        // This needs to happen first (in imgui events) to enable docking of imgui windows
#if VIAMD_IMGUI_ENABLE_DOCKSPACE
        ImGui::CreateDockspace();
#endif

        const size_t num_frames  = run_num_frames(&state);
        const size_t last_frame  = num_frames > 0 ? num_frames - 1 : 0;
        const double   max_frame = (double)last_frame;

        file_queue_process(&state);

        state.script.vis = {0};
        md_script_vis_init(&state.script.vis, state.allocator.frame);

        picking_handler_new_frame(&state.picking_handler);
        viamd::event_system_broadcast_event(viamd::EventType_ViamdPickingRangeReserve, viamd::EventPayloadType_PickingSpace, picking_handler_current_space(&state.picking_handler));

#if MD_ENABLE_GPU
        // Retires stream-ordered frees and fires completion callbacks for work
        // that finished since the last frame. Callbacks run on the calling
        // thread, so this must stay on the GL thread: they issue GL calls.
        // Not optional -- without it, freed device memory is never reclaimed.
        if (state.gpu_device) {
            md_gpu_device_poll(state.gpu_device);
        }
#endif

        viamd::event_system_broadcast_event(viamd::EventType_ViamdFrameTick, viamd::EventPayloadType_ApplicationState, &state);

        // GUI
        if (state.show_script_window) draw_script_editor_window(&state);
        if (state.show_script_reference_window) draw_script_reference_window(&state);
        if (state.load_dataset.show_window) draw_load_dataset_window(&state);
        if (state.representation.show_window) draw_representations_window(&state);
        if (state.distributions.show_window) draw_distribution_window(&state);
        if (state.timeline.show_window) draw_timeline_window(&state);
        series_cache_gc(&state);
        if (state.selection.query.show_window) draw_selection_query_window(&state);
        if (state.selection.grow.show_window) draw_selection_grow_window(&state);
        if (state.show_property_export_window) draw_property_export_window(&state);
        if (state.structure_export.show_window) draw_structure_export_window(&state);
        if (state.show_debug_window) draw_debug_window(&state);
        if (state.animation.show_window) draw_animation_window(&state);
        if (state.movie.show_window) draw_movie_window(&state);
        if (state.movie.show_timeline_window) draw_movie_timeline_window(&state);
        draw_movie_recording_banner(&state);

        draw_async_task_window(&state);
        draw_main_menu(&state);
        draw_notifications_window();

        //ImGui::ShowDemoWindow();

        draw_coordinate_system_widget_window(&state.view.target, state.view.camera);
            
        ImGui::BeginCanvas("Main interaction window", true);
        ImVec2 view_size = ImGui::GetContentRegionAvail();
        InteractionSurfaceState surface_state = interaction_surface(interaction_surface_main, vec_cast(view_size));
        ImGui::EndCanvas();

        // The recording drives the frame and the camera, and the viewport shows the frames at the movie's
        // resolution rather than the scene under the mouse, so picking and navigation are off meanwhile
        const bool movie_recording = state.movie.state == MovieRecordingState::Recording;

        const mat4_t clip_to_world = camera_view_to_world_matrix(state.view.camera) * state.view.param.matrix.inv.proj;
        const mat4_t world_to_clip = state.view.param.matrix.curr.proj * state.view.param.matrix.curr.view;

        PickingHit hit = {};

        // A keyframe waits for an atom: the click picks it, so it must not select or rotate as well
        const bool look_pick = state.movie.look_pick_key >= 0 && !movie_recording;
        if (look_pick && ImGui::IsKeyPressed(ImGuiKey_Escape)) state.movie.look_pick_key = -1;

        if (surface_state.hovered && !movie_recording) {
            InteractionSurfaceHitArgs args = {
                .picking_surface = &state.picking_surface,
                .picking_handler = state.picking_handler,
                .fbo = state.gbuffer.fbo,
                .width = state.gbuffer.width,
                .height = state.gbuffer.height,
                .clip_to_world = clip_to_world,
            };

            interaction_surface_hit_extract(&hit, surface_state, args);

            if (look_pick) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hit.domain == PickingDomain_Atom) {
                    movie_key_look_at_atom(&state, state.movie.look_pick_key, (int32_t)hit.local_idx);
                    state.movie.look_pick_key = -1;
                }
            }

            InteractionSurfaceEvent event = {};
            interaction_surface_event_extract(&event, surface_state, hit);

            event.clip_to_world = clip_to_world;
            event.world_to_clip = world_to_clip;

            if (look_pick) {
                // handled above
            } else if (event.kind == InteractionSurfaceEventKind::RegionSelect) {
                const md_bitfield_t* candidate_mask = &state.representation.visibility_mask;
                if (event.selection_mode == InteractionSelectionMode::Remove) {
                    // When removing, only consider currently selected atoms as candidates for region selection
                    candidate_mask = &state.selection.selection_mask;
                }
                point_set_region_mask_compute(&state.selection.highlight_mask,
                    state.mold.state.xyz, state.mold.state.num_atoms,
                    candidate_mask, world_to_clip, event.region_min, event.region_max, event.surface_size);

                grow_mask_by_selection_granularity(&state.selection.highlight_mask, state.selection.granularity, state.mold.sys);
                if (event.region_phase == InteractionSurfaceEventPhase::Commit) {
                    // Merge highlight into selection
                    if (event.selection_mode == InteractionSelectionMode::Append) {
                        md_bitfield_or_inplace(&state.selection.selection_mask, &state.selection.highlight_mask);
                    }
                    else if (event.selection_mode == InteractionSelectionMode::Remove) {
                        md_bitfield_andnot_inplace(&state.selection.selection_mask, &state.selection.highlight_mask);
                    }
                    md_bitfield_clear(&state.selection.highlight_mask);
                    // A region select is not a sequence of individual picks, so whatever order was
                    // recorded before no longer describes the selection.
                    single_selection_sequence_clear(&state.selection.single_selection_sequence);
                }
            } else if (event.kind == InteractionSurfaceEventKind::ContextMenu) {
                ImGui::OpenPopup("Context Popup");
            }
            
            // Since this is the main interaction view, we still broadcast all kinds of events, even if we have handled some explicitly here.
            if (!look_pick) viamd::event_system_broadcast_event(viamd::EventType_ViamdInteractionSurface, viamd::EventPayloadType_InteractionSurfaceEvent, &event);
        }

        InteractionSurfaceViewTransformArgs view_args = {
            .camera = state.view.camera,
            .trackball_param = state.view.trackball_param,
        };

        InteractionSurfaceViewTransformResult view_result = {};
        if (!movie_recording && !look_pick) {
            view_result = interaction_surface_view_transform_apply(&state.view.target, surface_state, view_args);
        }
        if (view_result.reset_requested) {
            ViewTransform reset_transform = {};
            if (hit.depth < 1.0f) {
                reset_transform.distance = state.view.target.distance;
                reset_transform.orientation = state.view.camera.orientation;
                reset_transform.position = hit.world_pos + state.view.camera.orientation * vec3_set(0, 0, state.view.target.distance);
            } else {
                reset_view(&reset_transform, state.mold.state, &state.representation.visibility_mask);
            }
            state.view.target = reset_transform;
        }

        draw_context_popup(&state, hit);

        camera_animate(&state.view.camera, state.view.target, state.app.timing.delta_s);

        ImGuiWindow* win = ImGui::GetCurrentContext()->HoveredWindow;
        if (win && strcmp(win->Name, "Main interaction window") == 0) {
            script_set_hovered_property(&state,  STR_LIT(""));
        }

        // Capture non-window specific keyboard events
        if (!ImGui::GetIO().WantCaptureKeyboard) {
#if EXPERIMENTAL_GFX_API
            if (ImGui::IsKeyPressed(ImGuiKey_F1)) {
                use_gfx = !use_gfx;
            }
#endif

            if (ImGui::IsKeyPressed(KEY_SHOW_DEBUG_WINDOW)) {
                state.show_debug_window = true;
            }

            if (movie_recording && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                movie_recording_stop(&state);
            }

            if ((state.movie.show_window || state.movie.show_timeline_window) && !movie_recording && ImGui::IsKeyPressed(KEY_ADD_MOVIE_KEYFRAME, false)) {
                movie_add_keyframe(&state);
            }

            if ((state.movie.show_window || state.movie.show_timeline_window) && !movie_recording) {
                if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
                    movie_undo(&state);
                } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) {
                    movie_redo(&state);
                }
            }

            if (ImGui::IsKeyPressed(KEY_RECOMPILE_SHADERS)) {
                VIAMD_LOG_INFO("Recompiling shaders and re-initializing volume");
                postprocess_pipeline::initialize(state.gbuffer.width, state.gbuffer.height);
                volume::initialize();
                md_gl_shaders_destroy(state.gl.shaders);
                state.gl.shaders = md_gl_shaders_create(shader_output_snippet);
            }

            if (!movie_recording && ImGui::IsKeyPressed(KEY_PLAY_PAUSE)) {
                switch (state.animation.mode) {
                    case PlaybackMode::Playing:
                        state.animation.mode = PlaybackMode::Stopped;
                        state.mold.dirty_gpu_buffers |= MolBit_ClearVelocity;
                        break;
                    case PlaybackMode::Stopped:
                        state.animation.mode = PlaybackMode::Playing;
                        if (state.animation.frame == max_frame && state.animation.fps > 0) {
                            state.animation.frame = 0;
                        } else if (state.animation.frame == 0 && state.animation.fps < 0) {
                            state.animation.frame = max_frame;
                        }
                        break;
                    default:
                        ASSERT(false);
                }

            }

            if (!movie_recording && (ImGui::IsKeyPressed(KEY_SKIP_TO_PREV_FRAME) || ImGui::IsKeyPressed(KEY_SKIP_TO_NEXT_FRAME))) {
                double step = ImGui::IsKeyDown(ImGuiMod_Ctrl) ? 10.0 : 1.0;
                if (ImGui::IsKeyPressed(KEY_SKIP_TO_PREV_FRAME)) step = -step;
                state.animation.frame = CLAMP(state.animation.frame + step, 0.0, max_frame);
            }
        }

        if (state.animation.mode == PlaybackMode::Playing) {
            state.animation.frame += state.app.timing.delta_s * state.animation.fps;
            state.animation.frame = CLAMP(state.animation.frame, 0.0, max_frame);
            if (state.animation.frame >= max_frame) {
                state.animation.mode = PlaybackMode::Stopped;
                state.animation.frame = max_frame;
            } else if (state.animation.frame <= 0) {
                state.animation.mode = PlaybackMode::Stopped;
                state.animation.frame = 0;
            }
        }

        // Must run before the time_changed check below so a frame set here is interpolated
        // and uploaded to the GPU within this same loop iteration, before render() is called.
        update_movie_preview(&state);
        update_movie_recording(&state);
        update_movie_history(&state);

        {
            static auto prev_frame = state.animation.frame;
            if (state.animation.frame != prev_frame) {
                time_changed = true;
                prev_frame = state.animation.frame;
            }
            else {
                time_changed = false;
            }
        }

        if (state.timeline.filter.temporal_window.enabled) {
            const double pre_beg = state.timeline.filter.beg_frame;
            const double pre_end = state.timeline.filter.end_frame;
            const double half_window_ext = state.timeline.filter.temporal_window.extent_in_frames * 0.5;
            state.timeline.filter.beg_frame = CLAMP(round(state.animation.frame - half_window_ext), 0.0, max_frame);
            state.timeline.filter.end_frame = CLAMP(round(state.animation.frame + half_window_ext), 0.0, max_frame);
            if (state.script.ir && (state.timeline.filter.beg_frame != pre_beg || state.timeline.filter.end_frame != pre_end)) {
                state.script.evaluate_filt = true;
            }
        }

        if (state.timeline.filter.enabled) {
            static auto prev_filter_beg = state.timeline.filter.beg_frame;
            static auto prev_filter_end = state.timeline.filter.end_frame;
            if (state.timeline.filter.beg_frame != prev_filter_beg || state.timeline.filter.end_frame != prev_filter_end) {
                prev_filter_beg = state.timeline.filter.beg_frame;
                prev_filter_end = state.timeline.filter.end_frame;
                state.timeline.filter.fingerprint = generate_fingerprint();
            }
        }

        if (time_changed) {
            state.mold.interpolate_system_state = true;
            time_stopped = false;

            PUSH_CPU_SECTION("Flag dynamic representations for update")
            for (size_t i = 0; i < md_array_size(state.representation.reps); ++i) {
                auto& rep = state.representation.reps[i];
                if (rep.enabled && (rep.dynamic_evaluation || rep.color_mapping == ColorMapping::SecondaryStructure)) {
					flag_representation_as_dirty(&rep);
                }
            }
            POP_CPU_SECTION()
        } else {
            if (!time_stopped) {
                time_stopped = true;
                state.mold.dirty_gpu_buffers |= MolBit_DirtyPosition;
            }
        }

        if (state.mold.interpolate_system_state) {
			state.mold.interpolate_system_state = false;
            if (run_num_frames(&state) > 0) {
                PUSH_CPU_SECTION("Interpolate System State")
                interpolate_system_state(&state);
                POP_CPU_SECTION()
                viamd::event_system_enqueue_event(viamd::EventType_ViamdSystemStateChanged, viamd::EventPayloadType_ApplicationState, &state);
            }
        }

        {
            std::string text = state.editor.GetText();
            state.script.text_hash = md_hash64(text.c_str(), text.length(), 0);
        }

        if (state.script.compile_ir) {
            state.script.time_since_last_change += state.app.timing.delta_s;

            script_editor::markers_clear(&state.editor_markers);

            if (state.script.time_since_last_change > COMPILATION_TIME_DELAY_IN_SECONDS) {
                // We cannot recompile while it is evaluating.
                // Need to interrupt and wait for tasks to finish.
                if (state.script.full_eval) md_script_eval_interrupt(state.script.full_eval);
                if (state.script.filt_eval) md_script_eval_interrupt(state.script.filt_eval);

                // Try aquire all semaphores
                {
                    defer {
                        flag_all_representations_as_dirty(&state);
                    };

                    // Now we hold all semaphores for the script
                    state.script.compile_ir = false;
                    state.script.time_since_last_change = 0;

                    if (state.script.ir && state.script.ir != state.script.eval_ir) {
                        md_script_ir_free(state.script.ir);
                    }
                    
                    state.script.ir = md_script_ir_create(persistent_alloc);

                    std::string src = state.editor.GetText();
                    str_t src_str {src.data(), src.length()};

                    char buf[1024];
                    size_t len = md_path_write_cwd(buf, sizeof(buf));
                    str_t old_cwd = {buf, len};
                    defer {
                        md_path_set_cwd(old_cwd);
                    };
                    
                    str_t cwd = {};
                    if (state.files.workspace[0] != '\0') {
                        extract_folder_path(&cwd, str_from_cstr(state.files.workspace));
                    } else if (state.files.trajectory[0] != '\0') {
                        extract_folder_path(&cwd, str_from_cstr(state.files.trajectory));
                    } else if (state.files.molecule[0] != '\0') {
                        extract_folder_path(&cwd, str_from_cstr(state.files.molecule));
                    }
                    if (!str_empty(cwd)) {
                        md_path_set_cwd(cwd);
                    }

                    const size_t num_stored_selections = md_array_size(state.selection.stored_selections);                       
                    for (size_t i = 0; i < num_stored_selections; ++i) {
                        str_t name = str_from_cstr(state.selection.stored_selections[i].name);
                        const md_bitfield_t* bf = &state.selection.stored_selections[i].atom_mask;
                        md_script_ir_add_identifier_bitfield(state.script.ir, name, bf);
                    }
                    
                    if (src_str) {
                        md_script_ir_compile_from_source(state.script.ir, src_str, &state.mold.sys, NULL);

                        // The markers copy what they need from the IR (it is freed below if it did not compile)
                        script_editor::Markers* markers = &state.editor_markers;
                        script_editor::markers_set_source(markers, src_str);

                        const size_t num_errors = md_script_ir_num_errors(state.script.ir);
                        const md_log_token_t* errors = md_script_ir_errors(state.script.ir);
                        for (size_t i = 0; i < num_errors; ++i) {
                            // Errors win over everything they overlap
                            script_editor::markers_add(markers, script_editor::MarkerType_Error, INT32_MAX, errors[i].range, errors[i].text, errors[i].context);
                        }

                        const size_t num_warnings = md_script_ir_num_warnings(state.script.ir);
                        const md_log_token_t* warnings = md_script_ir_warnings(state.script.ir);
                        for (size_t i = 0; i < num_warnings; ++i) {
                            script_editor::markers_add(markers, script_editor::MarkerType_Warning, INT32_MAX - 1, warnings[i].range, warnings[i].text, warnings[i].context);
                        }

                        const size_t num_tokens = md_script_ir_num_vis_tokens(state.script.ir);
                        const md_script_vis_token_t* vis_tokens = md_script_ir_vis_tokens(state.script.ir);
                        for (size_t i = 0; i < num_tokens; ++i) {
                            const md_script_vis_token_t& tok = vis_tokens[i];
                            script_editor::markers_add(markers, script_editor::MarkerType_Visualization, tok.depth, tok.range, tok.text, nullptr, tok.payload);
                        }

                        if (md_script_ir_valid(state.script.ir)) {
                            uint64_t ir_figerprint = md_script_ir_fingerprint(state.script.ir);
                            if (state.script.ir_fingerprint != ir_figerprint) {
                                state.script.ir_fingerprint = ir_figerprint;
                            }
                        } else {
                            md_script_ir_free(state.script.ir);
                            state.script.ir = nullptr;
                        }
                    }
                }
            }
        }

        if (num_frames > 0) {
            if (state.script.eval_init) {
                if (task_system::task_is_running(state.tasks.evaluate_full)) md_script_eval_interrupt(state.script.full_eval);
                if (task_system::task_is_running(state.tasks.evaluate_filt)) md_script_eval_interrupt(state.script.filt_eval);
                    
                if (task_system::task_is_running(state.tasks.evaluate_full) == false &&
                    task_system::task_is_running(state.tasks.evaluate_filt) == false) {
                    state.script.eval_init = false;

                    if (state.script.full_eval) {
                        md_script_eval_free(state.script.full_eval);
                    }
                    if (state.script.filt_eval) {
                        md_script_eval_free(state.script.filt_eval);
                    }
                
                    if (md_script_ir_valid(state.script.ir)) {
                        if (state.script.ir != state.script.eval_ir) {
                            md_script_ir_free(state.script.eval_ir);
                            state.script.eval_ir = state.script.ir;
                        }
                        state.script.full_eval = md_script_eval_create(num_frames, state.script.eval_ir, state.allocator.persistent);
                        state.script.filt_eval = md_script_eval_create(num_frames, state.script.eval_ir, state.allocator.persistent);
                    }

                    // The evaluations' tables were freed and new ones allocated, possibly in the
                    // same place: nothing derived from the old ones may be taken for the new.
                    series_cache_free(&state);

                    state.script.evaluate_filt = true;
                    state.script.evaluate_full = true;
                }
            }

            if (state.script.full_eval && state.script.evaluate_full) {
                if (task_system::task_is_running(state.tasks.evaluate_full)) {
                    md_script_eval_interrupt(state.script.full_eval);
                } else {
                    if (md_script_ir_valid(state.script.eval_ir) &&
                        md_script_eval_ir_fingerprint(state.script.full_eval) == md_script_ir_fingerprint(state.script.eval_ir))
                    {
                        state.script.evaluate_full = false;
                        md_script_eval_clear_data(state.script.full_eval);

                        if (md_script_ir_property_count(state.script.eval_ir) > 0) {
                            state.tasks.evaluate_full = task_system::create_pool_task(STR_LIT("Eval Full"), (uint32_t)num_frames, [&state](uint32_t frame_beg, uint32_t frame_end, uint32_t thread_num) {
                                (void)thread_num;
                                md_script_eval_frame_range(state.script.full_eval, state.script.eval_ir, &state.mold.sys, str_from_cstr(state.mold.run), frame_beg, frame_end);
                            });
                            
#if MEASURE_EVALUATION_TIME
                            uint64_t time = (uint64_t)md_tick_now();
                            task_system::ID time_task = task_system::create_pool_task(STR_LIT("##Time Eval Full"), [t0 = time]() {
                                uint64_t t1 = md_tick_now();
                                double s = md_tick_to_seconds(t1 - t0);
                                VIAMD_LOG_INFO("Evaluation completed in: %.3fs", s);
                            });
#endif
                            task_system::set_task_dependency(time_task, state.tasks.evaluate_full);
                            task_system::enqueue_task(state.tasks.evaluate_full);
                        }
                    }
                }
            }

            if (state.script.filt_eval && state.script.evaluate_filt && state.timeline.filter.enabled) {
                if (task_system::task_is_running(state.tasks.evaluate_filt)) {
                    md_script_eval_interrupt(state.script.filt_eval);
                } else {
                    if (md_script_ir_valid(state.script.eval_ir) &&
                        md_script_eval_ir_fingerprint(state.script.filt_eval) == md_script_ir_fingerprint(state.script.eval_ir))
                    {
                        state.script.evaluate_filt = false;
                        md_script_eval_clear_data(state.script.filt_eval);

                        if (md_script_ir_property_count(state.script.eval_ir) > 0) {
                            const uint32_t traj_frames = (uint32_t)run_num_frames(&state);
                            const uint32_t beg_frame = CLAMP((uint32_t)state.timeline.filter.beg_frame, 0, traj_frames-1);
                            const uint32_t end_frame = CLAMP((uint32_t)state.timeline.filter.end_frame + 1, beg_frame + 1, traj_frames);
                            if (beg_frame != end_frame) {
                                state.tasks.evaluate_filt = task_system::create_pool_task(STR_LIT("Eval Filt"), end_frame - beg_frame, [offset = beg_frame, &state](uint32_t beg, uint32_t end, uint32_t thread_num) {
                                    (void)thread_num;
                                    md_script_eval_frame_range(state.script.filt_eval, state.script.eval_ir, &state.mold.sys, str_from_cstr(state.mold.run), offset + beg, offset + end);
                                });
                                task_system::enqueue_task(state.tasks.evaluate_filt);
                            }
                        }
                    }
                }
            }
        }

        recenter_update(&state);

		// Perform once per-frame updates of representations (if required)
		update_all_representations(&state);

        if (state.representation.atom_visibility_mask_dirty) {
            recompute_atom_visibility_mask(&state);
            state.representation.atom_visibility_mask_dirty = false;
        }

        if (state.script.vis.text) {
            PUSH_CPU_SECTION("Draw vis text");
            ImGuiWindow* window = ImGui::FindWindowByName("Main interaction window");
            if (window) {
                ImDrawList* dl = window->DrawList;
                ASSERT(dl);

                const vec2_t res = { (float)state.app.window.width, (float)state.app.window.height };
                const mat4_t mvp = state.view.param.matrix.curr.proj_no_jitter * state.view.param.matrix.curr.view;

                // Script text
                const ImU32 text_color = convert_color(state.script.text_color);
                const ImU32 rect_color = convert_color(state.script.text_bg_color);
                const float rect_rounding = 5.f;
                const ImVec2 rect_padding = ImVec2(4.f, 2.f);

                size_t num_text = md_array_size(state.script.vis.text);
                for (size_t i = 0; i < num_text; ++i) {
                    const md_script_vis_text_t& vis_text = state.script.vis.text[i];

                    const vec4_t p = mat4_mul_vec4(mvp, vec4_from_vec3(vis_text.pos, 1.0f));
                    const vec4_t c = p / p.w;

                    // A label carrying a unit is a quantity, so it is shown in the same unit as the
                    // plots and re-formatted here rather than taken as mdlib spelled it. Anything
                    // else is drawn as it came.
                    char buf[64];
                    str_t str = vis_text.str;
                    if (!md_unit_is_none(vis_text.unit)) {
                        char unit_buf[32];
                        const double scl = display_units::factor_print(unit_buf, sizeof(unit_buf), vis_text.unit);
                        // The degree sign hugs the number the way the convention has it, everything else takes a space.
                        const char* sep = strcmp(unit_buf, "\xC2\xB0") == 0 ? "" : " ";
                        const int len = snprintf(buf, sizeof(buf), "%.2f%s%s", vis_text.value * scl, sep, unit_buf);
                        str = {buf, (size_t)CLAMP(len, 0, (int)sizeof(buf) - 1)};
                    }

                    const ImVec2 text_size = ImGui::CalcTextSize(str.beg(), str.end());

                    if (-1 < c.x && c.x < 1 && -1 < c.y && c.y < 1 && -1 < c.z && c.z < 1) {
                        ImVec2 tc = {(c.x * 0.5f + 0.5f) * res.x, (-c.y * 0.5f + 0.5f) * res.y};
                        ImVec2 p0 = tc - text_size * 0.5f;
                        ImVec2 p1 = tc + text_size * 0.5f;
                        dl->AddRectFilled(p0 - rect_padding, p1 + rect_padding, rect_color, rect_rounding);
                        dl->AddText(p0, text_color, str.beg(), str.end());
                    }
                }
            }
            POP_CPU_SECTION();
        }

        if (state.movie.show_overlay_preview && !state.movie.overlays.empty() && state.movie.state != MovieRecordingState::Recording &&
            (state.movie.show_window || state.movie.show_timeline_window)) {
            // The movie's overlays at the preview time, laid out for the viewport
            ImGuiWindow* window = ImGui::FindWindowByName("Main interaction window");
            if (window) {
                movie_overlays_draw(window->DrawList, ImVec2(0, 0), ImVec2((float)state.app.window.width, (float)state.app.window.height),
                    (double)state.movie.playhead, &state);
            }
        }

        if (ImGui::IsKeyPressed(KEY_RECENTER_ON_HIGHLIGHT) && !state.editor_focused) {
			ViewFitRequest fit_request = {
				.app = state,
				.surface_id = interaction_surface_main,
                .xyzw = nullptr,
                .alloc = state.allocator.frame,
				.round = ViewFitRound_Highlight,
            };
			viamd::event_system_broadcast_event(viamd::EventType_ViamdViewFit, viamd::EventPayloadType_ViewFitRequest, &fit_request);

			if (fit_request.xyzw == nullptr) {
                // If no highlight, fallback to selection
                fit_request.round = ViewFitRound_Selection;
                viamd::event_system_broadcast_event(viamd::EventType_ViamdViewFit, viamd::EventPayloadType_ViewFitRequest, &fit_request);
			}
			if (fit_request.xyzw == nullptr) {
                // If no selection, fallback to all
                fit_request.round = ViewFitRound_Visible;
                viamd::event_system_broadcast_event(viamd::EventType_ViamdViewFit, viamd::EventPayloadType_ViewFitRequest, &fit_request);
            }

            size_t count = md_array_size(fit_request.xyzw);
            if (count > 0) {
                // Extract OBB from returned pointcloud
                vec3_t com = md_util_com_compute_vec4(fit_request.xyzw, 0, count, 0);
                mat3_t cov = mat3_covariance_matrix_vec4(fit_request.xyzw, 0, count, com);
                mat3_eigen_t eigen = mat3_eigen(cov);
                mat3_t PCA = mat3_orthonormalize(mat3_extract_rotation(eigen.vectors));

                // Compute min and maximum extent along the PCA axes
                mat3_t basis = mat3_transpose(PCA);
                vec3_t min_obb = vec3_set1(FLT_MAX);
                vec3_t max_obb = vec3_set1(-FLT_MAX);

                // Transform the gto (x,y,z,cutoff) into the PCA frame to find the min and max extend within it
                for (size_t i = 0; i < count; ++i) {
                    vec3_t xyz = vec3_from_vec4(fit_request.xyzw[i]);
                    xyz = mat3_mul_vec3(PCA, xyz);
                    min_obb = vec3_min(min_obb, xyz);
                    max_obb = vec3_max(max_obb, xyz);
                }

				vec3_t ext = max_obb - min_obb;
				float max_ext = MAX(ext.x, MAX(ext.y, ext.z));

                if (max_ext > 3.0f) {
                    vec3_t half_ext = (max_obb - min_obb) * 0.5f;
                    state.view.target = compute_optimal_view(com, half_ext, basis);
                } else {
                    state.view.target.position = com + state.view.target.orientation * vec3_set(0, 0, state.view.target.distance);
                }
                state.view.target.position = mat4_mul_vec3(state.mold.unitcell_transform, state.view.target.position, 1.0f);
            }
            else {
                reset_view(&state.view.target, state.mold.state);
            }
        }

        // The motivation for doing this is to reduce the frequency at which we invalidate and upload the atom flag field to the GPU
        // For large systems, this can be a costly operation: Consider a system of 100'000'000 atoms
        // The size of each bitfield to represent the mask would be 12.5 MB
        // Given a throughput of 10GB/s results in a time of 1.25 ms per bitfield.

        uint64_t v_hash = state.representation.visibility_mask_hash;
        uint64_t h_hash = md_bitfield_hash64(&state.selection.highlight_mask, 0);
        uint64_t s_hash = md_bitfield_hash64(&state.selection.selection_mask, 0);
        uint64_t f_hash = md_hash64_combine(v_hash, md_hash64_combine(h_hash, s_hash));
        
        uint64_t r_hash = 0;
        for (size_t i = 0; i < md_array_size(state.representation.reps); ++i) {
			md_hash64(&state.representation.reps[i], sizeof(Representation), r_hash);
		}

        // These represent the 'current' state so we can compare against it to see if they were modified
        static uint64_t highlight_hash = 0;
        static uint64_t selection_hash = 0;
        static uint64_t representation_hash = 0;
        static uint64_t flag_hash = 0;

        if (h_hash != highlight_hash) {
            highlight_hash = h_hash;
            // enqueue the event here and do not broadcast (stall)
            viamd::event_system_enqueue_event(viamd::EventType_ViamdHighlightMaskChanged, viamd::EventPayloadType_ApplicationState, &state);
        }

        if (s_hash != selection_hash) {
            selection_hash = s_hash;
            // enqueue the event here and do not broadcast (stall)
            viamd::event_system_enqueue_event(viamd::EventType_ViamdSelectionMaskChanged, viamd::EventPayloadType_ApplicationState, &state);
        }

        if (f_hash != flag_hash) {
            flag_hash = f_hash;
            state.mold.dirty_gpu_buffers |= MolBit_DirtyFlags;
        }

        if (r_hash != representation_hash) {
            representation_hash = r_hash;
            // enqueue the event here and do not broadcast (stall)
            viamd::event_system_enqueue_event(viamd::EventType_ViamdRepresentationChanged, viamd::EventPayloadType_ApplicationState, &state);
        }

        // Process the queued events BEFORE uploading to the GPU and rendering.
        // The ViamdSystemStateChanged handler applies the toggled operations (recenter, apply
        // pbc, unwrap) directly to mold.state coordinates. update_md_buffers uploads those
        // coordinates and then clears dirty_gpu_buffers, so anything processed after it is not
        // merely rendered a frame late - it is discarded outright, because the next frame
        // re-interpolates mold.state from the trajectory before it is ever uploaded.
        viamd::event_system_process_event_queue();

        movie_follow_update(&state);
        update_md_buffers(&state);
        update_timeline_time_unit(&state);

        render(&state);

        task_system::execute_main_task_queue();

        // Reset frame allocator
        md_vm_arena_reset(frame_alloc);

        // Swap buffers
        application::swap_buffers(&state.app);
    }

    movie_shutdown(&state);
    interrupt_async_tasks(&state);
    series_cache_free(&state);

    viamd::event_system_broadcast_event(viamd::EventType_ViamdShutdown);

    // shutdown subsystems
    VIAMD_LOG_DEBUG("Shutting down immediate draw...");
    immediate::queue_destroy(state.gfx.world);
    immediate::queue_destroy(state.gfx.overlay);
    immediate::shutdown();

    state.gfx.world = nullptr;
    state.gfx.overlay = nullptr;

    VIAMD_LOG_DEBUG("Shutting down post processing...");
    postprocess_pipeline::shutdown();
    VIAMD_LOG_DEBUG("Shutting down volume...");
    volume::shutdown();
    VIAMD_LOG_DEBUG("Shutting down task system...");
    task_system::shutdown();

    gbuffer_free(&state.gbuffer);
#if MD_ENABLE_GPU
    if (state.gpu_device) {
        // A queued readback writes into a GL texture and frees its staging memory, so nothing
        // below may go until the queue has run out.
        gpu_volume_jobs_drain(&state);
        system_gpu_data_free(&state);

        md_gpu_free(state.gpu_stream, state.gpu_coeff);
        md_gpu_texture_destroy(state.gpu_volume);
        state.gpu_coeff = 0;
        state.gpu_coeff_capacity = 0;
        state.gpu_volume = nullptr;
        state.gpu_stream = nullptr;

        md_gpu_device_destroy(state.gpu_device);
        state.gpu_device = nullptr;
    }
#endif
    application::shutdown(&state.app);

    return 0;
}

// #misc
static void update_view_param(ApplicationState* data) {
    ViewParam& param = data->view.param;
    param.matrix.prev = param.matrix.curr;
    param.jitter.prev = param.jitter.curr;

    param.clip_planes.near = data->view.camera.near_plane;
    param.clip_planes.far = data->view.camera.far_plane;
    param.fov_y = data->view.camera.fov_y;
    param.resolution = {(float)data->gbuffer.width, (float)data->gbuffer.height};

    param.matrix.curr.view = camera_world_to_view_matrix(data->view.camera) * data->mold.unitcell_transform;
    param.matrix.inv.view  = mat4_inverse(data->mold.unitcell_transform) * camera_view_to_world_matrix(data->view.camera);

    const float n = data->view.camera.near_plane;
    const float f = data->view.camera.far_plane;
    const float aspect_ratio = (float)data->gbuffer.width / (float)data->gbuffer.height;

    if (data->visuals.temporal_aa.enabled && data->visuals.temporal_aa.jitter) {
        static uint32_t i = 0;
        i = (i+1) % (uint32_t)ARRAY_SIZE(data->view.jitter.sequence);
        param.jitter.curr = data->view.jitter.sequence[i] - 0.5f;
        if (data->view.mode == CameraMode::Perspective) {
            const vec2_t j = param.jitter.curr;
            param.matrix.curr.proj = camera_view_to_clip_matrix_persp(data->view.camera, data->gbuffer.width, data->gbuffer.height, j.x, j.y);
            param.matrix.inv.proj  = camera_clip_to_view_matrix_persp(data->view.camera, data->gbuffer.width, data->gbuffer.height, j.x, j.y);
            param.matrix.curr.proj_no_jitter = camera_view_to_clip_matrix_persp(data->view.camera, aspect_ratio);
        } else {
            const float h = data->view.camera.distance * tanf(data->view.camera.fov_y * 0.5f);
            const float w = aspect_ratio * h;
            const vec2_t scl = {w / data->gbuffer.width * 2.0f, h / data->gbuffer.height * 2.0f};
            const vec2_t j = param.jitter.curr * scl;
            param.matrix.curr.proj = camera_view_to_clip_matrix_ortho(-w + j.x, w + j.x, -h + j.y, h + j.y, n, f);
            param.matrix.inv.proj  = camera_clip_to_view_matrix_ortho(-w + j.x, w + j.x, -h + j.y, h + j.y, n, f);
            param.matrix.curr.proj_no_jitter = camera_view_to_clip_matrix_ortho(-w, w, -h, h, n, f);

        }
    } else {
        param.jitter.curr = {0,0};
        if (data->view.mode == CameraMode::Perspective) {
            param.matrix.curr.proj = camera_view_to_clip_matrix_persp(data->view.camera, aspect_ratio);
            param.matrix.inv.proj = camera_clip_to_view_matrix_persp(data->view.camera, (float)data->gbuffer.width / (float)data->gbuffer.height);
        } else {
            const float h = data->view.camera.distance * tanf(data->view.camera.fov_y * 0.5f);
            const float w = aspect_ratio * h;
            param.matrix.curr.proj = camera_view_to_clip_matrix_ortho(-w, w, -h, h, n, f);
            param.matrix.inv.proj = camera_clip_to_view_matrix_ortho(-w, w, -h, h, n, f);
        }
        param.matrix.curr.proj_no_jitter = param.matrix.curr.proj;
    }

    param.matrix.curr.norm = mat4_transpose(param.matrix.inv.view);
}

// ### DRAW WINDOWS ###
static void draw_main_menu(ApplicationState* data) {
    ASSERT(data);
    bool new_clicked = false;
    char path_buf[2048] = "";

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open File...", "CTRL+L")) {
                if (application::file_dialog(path_buf, sizeof(path_buf), application::FileDialogFlag_Open)) {
                    file_queue_push(&data->file_queue, str_from_cstr(path_buf), FileFlags_ShowDialogue);
                }
            }
            if (ImGui::MenuItem("Open Workspace...", "CTRL+O")) {
                if (application::file_dialog(path_buf, sizeof(path_buf), application::FileDialogFlag_Open, WORKSPACE_FILE_EXTENSION)) {
                    load_workspace(data, str_from_cstr(path_buf));
                }
            }
            if (ImGui::MenuItem("Save Workspace", "CTRL+S")) {
                if (strnlen(data->files.workspace, sizeof(data->files.workspace)) == 0) {
                    if (application::file_dialog(path_buf, sizeof(path_buf), application::FileDialogFlag_Save, WORKSPACE_FILE_EXTENSION)) {
                        save_workspace(data, {path_buf, strnlen(path_buf, sizeof(path_buf))});
                    }
                } else {
                    save_workspace(data, str_from_cstr(data->files.workspace));
                }
            }
            if (ImGui::MenuItem("Save Workspace As...")) {
                if (application::file_dialog(path_buf, sizeof(path_buf), application::FileDialogFlag_Save, WORKSPACE_FILE_EXTENSION)) {
                    save_workspace(data, {path_buf, strnlen(path_buf, sizeof(path_buf))});
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "ALT+F4")) {
                data->app.window.should_close = true;
            }
            ImGui::EndMenu();
        }
        /*
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
        */
        if (ImGui::BeginMenu("Visuals")) {
            if (ImGui::Button("Reset View")) {
                reset_view(&data->view.target, data->mold.state, &data->representation.visibility_mask);
            }
            ImGui::Separator();
            ImGui::Checkbox("VSync", &data->app.window.vsync);
            ImGui::SetItemTooltip("Limit the frame rate to the display's refresh rate");
            ImGui::Separator();

            ImGui::BeginGroup();
            ImGui::Text("Camera");
            {
                ImGui::Combo("Mode", (int*)(&data->view.mode), "Perspective\0Orthographic\0");
                if (data->view.mode == CameraMode::Perspective) {
                    float fov = RAD_TO_DEG(data->view.camera.fov_y);
                    if (ImGui::SliderFloat("Field of View", &fov, 12.5f, 80.0f, "%.1f deg")) {
                        data->view.camera.fov_y = DEG_TO_RAD(fov);
                    }
                }
            }
            ImGui::EndGroup();

            ImGui::BeginGroup();
            ImGui::Text("Background");
            ImGui::ColorEdit3Minimal("Color", data->visuals.background.color.elem);
            ImGui::SameLine();
            ImGui::SliderFloat("##Intensity", &data->visuals.background.intensity, 0.f, 100.f);
            ImGui::SetItemTooltip("Background brightness");
            ImGui::EndGroup();
            ImGui::Separator();
            ImGui::Checkbox("Anti-Aliasing (FXAA)", &data->visuals.fxaa.enabled);
            ImGui::SetItemTooltip("Smooth jagged edges in the rendered image");
            // Temporal
            ImGui::BeginGroup();
            {
                ImGui::Checkbox("Temporal Anti-Aliasing", &data->visuals.temporal_aa.enabled);
                ImGui::SetItemTooltip("Smooth edges by blending each frame with the previous ones");
                if (data->visuals.temporal_aa.enabled) {
                    // ImGui::Checkbox("Jitter Samples", &data->visuals.temporal_reprojection.jitter);
                    ImGui::SliderFloat("History Min", &data->visuals.temporal_aa.feedback_min, 0.5f, 1.0f);
                    ImGui::SetItemTooltip("How much of the previous frames is kept where the image changes: lower is sharper in motion, higher is smoother");
                    ImGui::SliderFloat("History Max", &data->visuals.temporal_aa.feedback_max, 0.5f, 1.0f);
                    ImGui::SetItemTooltip("How much of the previous frames is kept where the image is still");
                    ImGui::Checkbox("Motion Blur", &data->visuals.temporal_aa.motion_blur.enabled);
                    if (data->visuals.temporal_aa.motion_blur.enabled) {
                        ImGui::SliderFloat("Motion Blur Strength", &data->visuals.temporal_aa.motion_blur.motion_scale, 0.f, 2.0f);
                    }
                    ImGui::Checkbox("Sharpen", &data->visuals.sharpen.enabled);
                    if (data->visuals.sharpen.enabled) {
                        ImGui::SliderFloat("Sharpen Strength", &data->visuals.sharpen.weight, 0.0f, 4.0f);
                    }
                }
            }
            ImGui::EndGroup();
            ImGui::Separator();

            // SSAO
            ImGui::BeginGroup();
            ImGui::PushID("SSAO");
            ImGui::Checkbox("Ambient Occlusion", &data->visuals.ssao.enabled);
            ImGui::SetItemTooltip("Darken creases and cavities, where less light reaches (SSAO)");
            if (data->visuals.ssao.enabled) {
                ImGui::SliderFloat("Intensity", &data->visuals.ssao.intensity, 0.5f, 12.f);
                ImGui::SliderFloat("Radius", &data->visuals.ssao.radius, 1.f, 30.f);
                ImGui::SliderFloat("Bias", &data->visuals.ssao.bias, 0.0f, 1.0f);
            }
            ImGui::PopID();
            ImGui::EndGroup();
            ImGui::Separator();

#if EXPERIMENTAL_CONE_TRACED_AO == 1
            // Cone Trace
            ImGui::BeginGroup();
            ImGui::PushID("Cone Trace");
            ImGui::Checkbox("Cone Traced AO", &data->visuals.cone_traced_ao.enabled);
            if (data->visuals.cone_traced_ao.enabled) {
                ImGui::SliderFloat("Intensity", &data->visuals.cone_traced_ao.intensity, 0.01f, 5.f);
                ImGui::SliderFloat("Step Scale", &data->visuals.cone_traced_ao.step_scale, 0.25f, 8.f);
            }
            ImGui::PopID();
            ImGui::EndGroup();
            ImGui::Separator();
#endif

            // DOF
            ImGui::BeginGroup();
            ImGui::Checkbox("Depth of Field", &data->visuals.dof.enabled);
            if (data->visuals.dof.enabled) {
                int mode = (int)data->visuals.dof.focus_mode;
                if (ImGui::Combo("Focus", &mode, "Look-at point\0Distance\0Follow target\0")) {
                    data->visuals.dof.focus_mode = (DofFocusMode)mode;
                }
                ImGui::SetItemTooltip("What is sharp.\n"
                    "Look-at point: what the camera looks at (the point it orbits).\n"
                    "Distance: a distance from the camera that you set, and can key in the Movie window.\n"
                    "Follow target: the middle of the movie's follow target, even when the camera looks elsewhere.");
                if (data->visuals.dof.focus_mode == DofFocusMode::Distance) {
                    ImGui::SliderFloat("Focus distance", &data->visuals.dof.focus_distance, 0.01f, 1000.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("From view")) {
                        data->visuals.dof.focus_distance = data->view.camera.distance;
                    }
                    ImGui::SetItemTooltip("Take the distance to what the camera looks at now");
                } else if (data->visuals.dof.focus_mode == DofFocusMode::Target && md_bitfield_empty(&data->movie.follow_mask)) {
                    ImGui::TextDisabled("No follow target set: using the look-at point");
                }
                ImGui::TextDisabled("Focus at %.2f from the camera", dof_focus_depth(data, data->view.camera));
                ImGui::SliderFloat("Blur Strength", &data->visuals.dof.focus_scale, 0.001f, 100.f);
                ImGui::SetItemTooltip("How strongly what is out of focus is blurred");
            }
            ImGui::EndGroup();
            ImGui::Separator();

            // Tonemapping
            ImGui::BeginGroup();
            ImGui::Checkbox("Tone Mapping", &data->visuals.tonemapping.enabled);
            ImGui::SetItemTooltip("Map the lighting into the colors a display can show");
            if (data->visuals.tonemapping.enabled) {
                // ImGui::Combo("Function", &data->visuals.tonemapping.tonemapper, "Passthrough\0Exposure Gamma\0Filmic\0\0");
                ImGui::SliderFloat("Exposure", &data->visuals.tonemapping.exposure, 0.01f, 10.f);
                ImGui::SliderFloat("Gamma", &data->visuals.tonemapping.gamma, 1.0f, 3.0f);
            }
            ImGui::EndGroup();
            ImGui::Separator();

            ImGui::BeginGroup();
            ImGui::Checkbox("Simulation Box", &data->simulation_box.enabled);
            ImGui::SetItemTooltip("Draw the outline of the periodic box");
            if (data->simulation_box.enabled) {
                ImGui::SameLine();
                ImGui::ColorEdit4Minimal("##Box-Color", data->simulation_box.color.elem);
            }
            ImGui::EndGroup();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Windows")) {
            ImGui::Checkbox("Animation", &data->animation.show_window);
            ImGui::Checkbox("Movie", &data->movie.show_window);
            ImGui::Checkbox("Movie Timeline", &data->movie.show_timeline_window);
            ImGui::Checkbox("Representations", &data->representation.show_window);
            ImGui::Checkbox("Script Editor", &data->show_script_window);
            ImGui::Checkbox("Script Reference", &data->show_script_reference_window);
            ImGui::Checkbox("Timelines", &data->timeline.show_window);
            ImGui::Checkbox("Distributions", &data->distributions.show_window);
            ImGui::Checkbox("Structure Export", &data->structure_export.show_window);

            viamd::event_system_broadcast_event(viamd::EventType_ViamdWindowDrawMenu);

            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Selection")) {
            ImGui::Combo("Select by", (int*)(&data->selection.granularity), selection_granularity_str, (int)SelectionGranularity::Count);
            ImGui::SetItemTooltip("What a click or a dragged region in the viewport selects: single atoms,\nwhole components (residues) or whole instances (chains, molecules)");
            size_t num_selected_atoms = md_bitfield_popcount(&data->selection.selection_mask);
            if (ImGui::MenuItem("Invert")) {
                md_bitfield_not_inplace(&data->selection.selection_mask, 0, data->mold.sys.atom.count);
            }
            if (ImGui::IsItemHovered()) {
                md_bitfield_not(&data->selection.highlight_mask, &data->selection.selection_mask, 0, data->mold.sys.atom.count);
            }
            if (ImGui::MenuItem("Query")) data->selection.query.show_window = true;
            if (num_selected_atoms == 0) ImGui::PushDisabled();
            if (ImGui::MenuItem("Grow"))  data->selection.grow.show_window = true;
            if (num_selected_atoms == 0) ImGui::PopDisabled();
            if (ImGui::MenuItem("Clear")) {
                md_bitfield_clear(&data->selection.selection_mask);
                single_selection_sequence_clear(&data->selection.single_selection_sequence);
            }
            ImGui::Spacing();
            ImGui::Separator();

            // STORED SELECTIONS
            {
                md_bitfield_clear(&data->selection.highlight_mask);
                // @NOTE(Robin): This ImGui ItemFlag can be used to force the menu to remain open after buttons are pressed.
                // Leave it here as a comment if we feel that it is needed in the future
                //ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);

                ImGui::Text("Stored Selections");
                for (int i = 0; i < (int)md_array_size(data->selection.stored_selections); i++) {
                    auto& sel = data->selection.stored_selections[i];
                    const str_t name_str = str_from_cstr(sel.name);
                    bool is_valid = md_script_identifier_name_valid(name_str);
                    char error[64] = "";
                    if (!is_valid) {
                        snprintf(error, sizeof(error), "'%s' is not a valid identifier.", sel.name);
                    }

                    for (int j = 0; j < i; ++j) {
                        if (str_eq_cstr(name_str, data->selection.stored_selections[j].name)) {
                            is_valid = false;
                            snprintf(error, sizeof(error), "identifier '%s' is already taken.", sel.name);
                            break;
                        }
                    }

                    ImGui::PushID(i);
                    ImGui::InputQuery("##label", sel.name, sizeof(sel.name), is_valid, error);
                    ImGui::SameLine();
                    if (ImGui::Button("Load")) {
                        md_bitfield_copy(&data->selection.selection_mask, &sel.atom_mask);
                        flag_all_representations_as_dirty(data);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Load the stored selection as the active selection");
                        md_bitfield_copy(&data->selection.highlight_mask, &sel.atom_mask);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Store")) {
                        ImGui::SetTooltip("Store the active selection into this selection");
                        md_bitfield_copy(&sel.atom_mask, &data->selection.selection_mask);
                        data->script.compile_ir = true;
                        flag_all_representations_as_dirty(data);
                    }
                    ImGui::SameLine();
                    if (ImGui::DeleteButton("Remove")) {
                        ImGui::SetTooltip("Remove this selection");
                        remove_selection(data, i);
                    }
                    ImGui::PopID();
                }

                if (ImGui::Button("Create New")) {
                    char name_buf[64];
                    snprintf(name_buf, sizeof(name_buf), "sel%i", (int)md_array_size(data->selection.stored_selections) + 1);
                    create_selection(data, str_from_cstr(name_buf), &data->selection.selection_mask);
                }

                //ImGui::PopItemFlag();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Screenshot")) {
            ImGui::Checkbox("Hide GUI", &data->screenshot.hide_gui);
            if (data->screenshot.hide_gui) {
                data->screenshot.resolution = (ScreenshotResolution)MIN((int)data->screenshot.resolution, (int)ScreenshotResolution::Count - 1);
                if (ImGui::BeginCombo("Resolution", screenshot_resolution_str[(int)data->screenshot.resolution])) {
                    for (int i = 0; i < (int)ScreenshotResolution::Count; ++i) {
                        if (ImGui::Selectable(screenshot_resolution_str[i], (i == (int)data->screenshot.resolution))) {
                            data->screenshot.resolution = (ScreenshotResolution)i;
                        }
                    }
                    ImGui::EndCombo();
                }

                switch (data->screenshot.resolution) {
                case ScreenshotResolution::Window:
                    data->screenshot.res_x = data->gbuffer.width;
                    data->screenshot.res_y = data->gbuffer.height;
                    break;
                case ScreenshotResolution::FHD:
                    data->screenshot.res_x = 1920;
                    data->screenshot.res_y = 1080;
                    break;
                case ScreenshotResolution::QHD:
                    data->screenshot.res_x = 2560;
                    data->screenshot.res_y = 1440;
                    break;
                case ScreenshotResolution::UHD_4K:
                    data->screenshot.res_x = 3840;
                    data->screenshot.res_y = 2160;
                    break;
                case ScreenshotResolution::UHD_8K:
                    data->screenshot.res_x = 7680;
                    data->screenshot.res_y = 4320;
                    break;
                case ScreenshotResolution::Custom:
                    ImGui::InputInt("Res X", &data->screenshot.res_x);
                    ImGui::InputInt("Res Y", &data->screenshot.res_y);

                    data->screenshot.res_x = CLAMP(data->screenshot.res_x, 640, 16384);
                    data->screenshot.res_y = CLAMP(data->screenshot.res_y, 480, 16384);
                    break;
                default:
                    ASSERT(false);
                }
            } else {
                data->screenshot.res_x = data->gbuffer.width;
                data->screenshot.res_y = data->gbuffer.height;
            }
            if (ImGui::MenuItem("Take Screenshot", nullptr, false, data->movie.state != MovieRecordingState::Recording)) {
                if (application::file_dialog(path_buf, sizeof(path_buf), application::FileDialogFlag_Save, STR_LIT("jpg,png,bmp"))) {
                    size_t path_len = strnlen(path_buf, sizeof(path_buf));
                    str_t ext;
                    if (!extract_ext(&ext, {path_buf, path_len})) {
                        path_len += snprintf(path_buf + path_len, sizeof(path_buf) - path_len, ".jpg");
                        ext = STR_LIT("jpg");
                    }
                    if (str_eq_cstr_ignore_case(ext, "jpg") || str_eq_cstr_ignore_case(ext, "png") || str_eq_cstr_ignore_case(ext, "bmp")) {
                        data->screenshot.path_to_file = str_copy({path_buf, path_len}, persistent_alloc);
                        if (data->visuals.temporal_aa.enabled) {
                            data->screenshot.sample_target = JITTER_SEQUENCE_SIZE;
                        } else {
                            data->screenshot.sample_target = 1;
                        }
                    }
                    else {
                        VIAMD_LOG_ERROR("Supplied image extension is not supported");
                    }
                }
                ImGui::GetCurrentWindow()->Hidden = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Operations")) {
            bool do_recenter = false;
            bool do_pbc = false;
            bool do_unwrap = false;
            bool do_bonds = false;

            // Each operation can be applied to the frame shown now, or to every frame as it is shown.
            // The labels say what happens to the atoms, not what the operation is called internally.
            ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;
            if (ImGui::BeginTable("##table", 3, flags)) {
                if (ImGui::IsWindowHovered()) {
                    md_bitfield_clear(&data->selection.highlight_mask);
                }
                ImGui::TableSetupColumn("Now");
                ImGui::TableSetupColumn("Every frame");
                ImGui::TableSetupColumn("Options");
                ImGui::TableHeadersRow();

                const float button_width = (ImGui::GetFontSize() / 20.f) * 175.f;
                const md_bitfield_t& target_mask = recenter_get_active_target_mask(data);
                const bool recenter_available = !md_bitfield_empty(&target_mask);
                const char* no_target_tip =
                    "There is nothing to center on yet. Select atoms and choose 'Set as Centering Target' in the\n"
                    "right-click menu of the viewport, or tick " ICON_FA_COMMENT_DOTS " and pick the target with a query.";

                // ## Centering
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                if (!recenter_available) {
                    ImGui::PushDisabled();
                }
                if (ImGui::Button("Center Target", ImVec2(button_width,0))) {
                    do_recenter = true;
                }
                if (!recenter_available) {
                    ImGui::PopDisabled();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    if (!recenter_available) {
                        ImGui::SetTooltip("%s", no_target_tip);
                    } else {
                        ImGui::SetTooltip("Move everything so the target (highlighted) is in the middle of the box,\nor at the origin when there is no box");
                        // Highlight the target atoms that the system will be recentered around
                        md_bitfield_copy (&data->selection.highlight_mask, &target_mask);
                    }
                }

                ImGui::TableSetColumnIndex(1);
                if (!recenter_available) {
                    ImGui::PushDisabled();
                }
                if (ImGui::Checkbox("##recenter", &data->operations.recenter) && data->operations.recenter) {
                    do_recenter = true;
                }
                if (!recenter_available) {
                    ImGui::PopDisabled();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("%s", recenter_available ? "Keep the target in the middle of the box in every frame" : no_target_tip);
                }

                ImGui::TableSetColumnIndex(2);
                ImGui::Checkbox(ICON_FA_ANCHOR_LOCK "##keep-orientation", &data->operations.fixate_orientation);
                ImGui::SetItemTooltip("Keep orientation: when centering, also turn everything so the target\nkeeps the orientation it has in the first frame");

                ImGui::SameLine();
                ImGui::Checkbox(ICON_FA_COMMENT_DOTS "##target-query", &data->operations.recenter_query.enabled);
                ImGui::SetItemTooltip("Target by query: center on the atoms a query picks (evaluated every frame\nwhen it depends on it) instead of the target set from a selection");

                if (data->operations.recenter_query.enabled) {
                    auto& recenter_query = data->operations.recenter_query;
                    ImGui::SameLine();
                    if (ImGui::InputQuery("##recenter-query", recenter_query.query, sizeof(recenter_query.query), recenter_query.valid, recenter_query.error)) {
                        recenter_mark_query_dirty(data);
                    }
                }

                // ## Periodic box
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                if (ImGui::Button("Wrap into Box", ImVec2(button_width,0))) {
                    do_pbc = true;
                }
                ImGui::SetItemTooltip("Move every atom that is outside the periodic box back in through the opposite side\n(apply periodic boundary conditions). Molecules across the boundary are split.");

                ImGui::TableSetColumnIndex(1);
                if (ImGui::Checkbox("##pbc", &data->operations.apply_pbc) && data->operations.apply_pbc) {
                    do_pbc = true;
                }
                ImGui::SetItemTooltip("Wrap every atom into the periodic box in every frame");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Button("Make Whole", ImVec2(button_width, 0))) {
                    do_unwrap = true;
                }
                ImGui::SetItemTooltip("Join molecules that are split across the periodic box boundary,\nso each one is drawn in one piece (unwrap)");

                ImGui::TableSetColumnIndex(1);
                if (ImGui::Checkbox("##unwrap", &data->operations.unwrap_structures) && data->operations.unwrap_structures) {
                    do_unwrap = true;
                }
                ImGui::SetItemTooltip("Keep every molecule whole in every frame");

                // ## Bonds
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Button("Recompute Bonds", ImVec2(button_width, 0))) {
                    do_bonds = true;
                }
                ImGui::SetItemTooltip("Guess the covalent bonds again from the distances between atoms in this frame,\nfor when bonds form or break, or the file's bonds are wrong");

                ImGui::TableSetColumnIndex(1);
                if (ImGui::Checkbox("##bonds", &data->operations.recalc_bonds) && data->operations.recalc_bonds) {
                    do_bonds = true;
                }
                ImGui::SetItemTooltip("Guess the covalent bonds from the distances in every frame (slower)");
                ImGui::EndTable();
            }

            if (do_recenter) {
                mat4_t T = mat4_ident();
                recenter_calculate_transform(T.elem, data);

                // Batch transform all atoms
                const uint32_t grain_size = 1024;
                task_system::ID apply_transform_task = task_system::create_pool_task(STR_LIT("## Recenter"), (uint32_t)data->mold.state.num_atoms, [T, data](uint32_t range_beg, uint32_t range_end, uint32_t thread_num) {
                    (void)thread_num;
                    size_t count = range_end - range_beg;
                    mat4_batch_transform_inplace(data->mold.state.xyz + range_beg, 1.0f, count, T);
                }, grain_size);
                task_system::enqueue_task(apply_transform_task);
                task_system::task_wait_for(apply_transform_task);
                data->mold.dirty_gpu_buffers |= MolBit_DirtyPosition | MolBit_ClearVelocity;
            }

            if (do_pbc) {
                md_util_system_pbc(&data->mold.state);
                data->mold.dirty_gpu_buffers |= MolBit_DirtyPosition | MolBit_ClearVelocity;
            }

            if (do_unwrap) {
                md_util_unwrap_system(&data->mold.state, &data->mold.sys);
                data->mold.dirty_gpu_buffers |= MolBit_DirtyPosition | MolBit_ClearVelocity;
            }

            if (do_bonds) {
                if (!task_system::task_is_running(data->tasks.evaluate_full) && !task_system::task_is_running(data->tasks.evaluate_filt)) {
                    const auto& mol = data->mold.sys;

                    vec3_t* xyz = NULL;

                    if (run_num_frames(data) > 0) {
                        // Closest frame to the current animation time
                        uint32_t frame_idx = (uint32_t)(data->animation.frame + 0.5);
                        md_temp_scope_t temp_pos = md_temp_begin_in(frame_alloc);
                        defer { md_temp_end(temp_pos); };

                        xyz = (vec3_t*)md_vm_arena_push(frame_alloc, ALIGN_TO(mol.atom.count, 16) * sizeof(vec3_t));
                        md_system_state_t frame_state = {};
                        frame_state.num_atoms = mol.atom.count;
                        frame_state.xyz = xyz;
                        if (!extract_frame(data, frame_idx, &frame_state)) {
                            MD_LOG_ERROR("Failed to extract frame data");
                        } 
                    } else {
						// No trajectory, use current positions
						xyz = data->mold.state.xyz;
                    }

                    if (xyz) {
                        MD_LOG_DEBUG("RECALCULATING BONDS");
                        md_util_infer_covalent_bonds(&data->mold.sys.bond, &data->mold.state, &data->mold.sys, data->mold.sys.alloc);
                        md_bond_build_connectivity(&data->mold.sys.bond, data->mold.sys.atom.count, data->mold.sys.alloc);
                        data->mold.dirty_gpu_buffers |= MolBit_DirtyBonds;
                    }
                } else {
                    MD_LOG_INFO("Cannot recalculate bonds while evaluation is occuring.");
                }
            }

            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Settings")) {
            if (ImGui::Checkbox("Keep Representations", &data->settings.keep_representations)) {
                app_settings::mark_dirty();
            }
            ImGui::SetItemTooltip("Keep representations when loading new topology (does not apply for workspaces)\n");

            // Font
            int font_size_idx = nearest_font_size_index(data->settings.font_size);
            if (ImGui::Combo("Font Size", &font_size_idx, font_size_names, (int)ARRAY_SIZE(font_size_names))) {
                data->settings.font_size = font_sizes[font_size_idx];
                apply_font_size(data);
                app_settings::mark_dirty();
            }

			if (ImGui::TreeNode("Units")) {
                display_units::draw_settings_menu_items();
				ImGui::TreePop();
			}

            ImGui::EndMenu();
        }
        {
            // Fps counter
            static int num_frames = 0;
            static double acc_ms  = 0;
            static double avg_ms  = 0;

            double ms = (data->app.timing.delta_s * 1000);
            acc_ms += ms;
            num_frames += 1;

            if (acc_ms > 500) {
                avg_ms = acc_ms / num_frames;
                acc_ms = 0;
                num_frames = 0;
            }

            char fps_buf[64];
            snprintf(fps_buf, ARRAY_SIZE(fps_buf), "%.2f ms (%.1f fps)", avg_ms, 1000.f / avg_ms);
            const float w = ImGui::CalcTextSize(fps_buf).x;
            ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - w);
            ImGui::Text("%s", fps_buf);
        }
        ImGui::EndMainMenuBar();
    }

    if (new_clicked) ImGui::OpenPopup("Warning New");
}

void draw_notifications_window() {
    // Render toasts on top of everything, at the end of your code!
    // You should push style vars here
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(43.f / 255.f, 43.f / 255.f, 43.f / 255.f, 100.f / 255.f));
    ImGui::RenderNotifications();
    ImGui::PopStyleVar(1); // Don't forget to Pop()
    ImGui::PopStyleColor(1);
}

void draw_load_dataset_window(ApplicationState* data) {
    LoadDatasetWindowState& state = data->load_dataset;
    
    ImGui::SetNextWindowSize(ImVec2(300, 215), ImGuiCond_FirstUseEver);
    if (state.show_window) {
        ImGui::OpenPopup("Load Dataset");
    }

    if (ImGui::BeginPopupModal("Load Dataset", &state.show_window)) {
        const bool path_invalid = !state.path_is_valid && state.path_buf[0] != '\0';
        const int  loader_count = LoaderType_COUNT;

        if (path_invalid) ImGui::PushInvalid();
        if (ImGui::InputText("##path", state.path_buf, sizeof(state.path_buf))) {
            state.path_changed = true;
        }
        if (path_invalid) ImGui::PopInvalid();


        // @WORKAROUND(Robin): This show_file_dialog is only here to circumvent the issue that if you open a file dialog
        // Within the same frame as the button is clicked, the dialogue will open again after the closing the dialog.
        ImGui::SameLine();
        if (ImGui::Button("Browse") && !state.show_file_dialog) {
            state.show_file_dialog = true;
        }

        if (state.show_file_dialog) {
            state.show_file_dialog = false;
            if (application::file_dialog(state.path_buf, sizeof(state.path_buf), application::FileDialogFlag_Open)) {
                state.path_changed = true;
            }
        }

        str_t path = str_from_cstr(state.path_buf);

        if (state.path_changed) {
            state.path_changed = false;
            state.path_is_valid = md_path_is_valid(path) && !md_path_is_directory(path);

            // Try to assign loader_idx from extension
            state.loader_idx = 0;
            str_t ext;
            if (extract_ext(&ext, path)) {
                state.loader_idx = (int)loader::type_from_ext(ext);
            }
        }

        if (ImGui::BeginCombo("Loader", loader::type_name(state.loader_idx).ptr)) {
            for (int i = 1; i < loader_count; ++i) {
                const char* str = loader::type_name(i).ptr;
                if (!str) continue;
                if (ImGui::Selectable(str, state.loader_idx == i)) {
                    state.loader_idx = i;
                }
            }
            ImGui::EndCombo();
        }

        // True if the button should be enabled
        bool load_button_enabled = (state.path_is_valid && state.loader_idx > -1);

        LoaderType   type = (LoaderType)(state.loader_idx);
        LoaderFlags flags = loader::type_flags(type);

        // Draw Options
        bool show_cg = state.path_is_valid && (flags & LoaderFlag_System);
        if (show_cg) {
            ImGui::Checkbox("Coarse Grained", &state.coarse_grained);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Enable if the data should be interpreted as coarse grained particles");
            }
        }

        bool show_lammps_atom_format = state.path_is_valid && (type == LoaderType_LAMMPSDATA);
        if (show_lammps_atom_format) {
            const char** atom_format_names = md_lammps_atom_format_names();
            const char** atom_format_strings = md_lammps_atom_format_strings();

            if (state.atom_format_idx == -1) {
                // Try to determine format from file
                md_lammps_atom_format_t format = md_lammps_atom_format_from_file(path);
                state.atom_format_idx = format;
				const char* format_str = atom_format_strings[format];
                snprintf(state.atom_format_buf, sizeof(state.atom_format_buf), "%s", format_str);
            }
            state.atom_format_idx = CLAMP(state.atom_format_idx, 0, MD_LAMMPS_ATOM_FORMAT_COUNT - 1);

            if (ImGui::BeginCombo("Atom Format", state.atom_format_idx > 0 ? atom_format_names[state.atom_format_idx] : "user defined")) {
                for (int i = 0; i < MD_LAMMPS_ATOM_FORMAT_COUNT; ++i) {
                    if (ImGui::Selectable(i > 0 ? atom_format_names[i] : "user defined", state.atom_format_idx == i)) {
                        state.atom_format_idx = i;
                        int source_idx = i > 0 ? i : MD_LAMMPS_ATOM_FORMAT_FULL;
                        const char* format_str = atom_format_strings[source_idx];
                        snprintf(state.atom_format_buf, sizeof(state.atom_format_buf), "%s", format_str);
                    }
                }
                ImGui::EndCombo();
            }

            if (state.atom_format_idx == MD_LAMMPS_ATOM_FORMAT_UNKNOWN) {
                bool valid = state.atom_format_valid;
                if (!valid) ImGui::PushInvalid();
                if (ImGui::InputText("##atom_format", state.atom_format_buf, sizeof(state.atom_format_buf))) {
                    state.atom_format_valid = md_lammps_validate_atom_format(state.err_buf, sizeof(state.err_buf), state.atom_format_buf);
                }
                if (!valid) ImGui::PopInvalid();
                if (ImGui::IsItemHovered() && !valid) {
                    ImGui::SetTooltip("%s", state.err_buf);
                }
            }
            else {
                ImGui::PushDisabled();
                ImGui::InputText("##atom_format", state.atom_format_buf, sizeof(state.atom_format_buf), ImGuiInputTextFlags_ReadOnly);
                ImGui::PopDisabled();
            }

            if (state.atom_format_idx < 0 || (state.atom_format_idx == MD_LAMMPS_ATOM_FORMAT_UNKNOWN && !state.atom_format_valid)) {
                load_button_enabled = false;
            }
        }

        enum Action {
            Action_None,
            Action_Cancel,
            Action_Load,
        };
        Action action = Action_None;

        if (!load_button_enabled) ImGui::PushDisabled();
        if (ImGui::Button("Load")) {
            action = Action_Load;
        }
        if (!load_button_enabled) ImGui::PopDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            action = Action_Cancel;
        }

        switch (action) {
        case Action_Load: {
            loader::LoaderState load_state = {};
            load_state.type = type;
            load_state.flags = flags;
            load_state.flags |= state.coarse_grained ? LoaderFlag_CoarseGrained : 0;
            if (type == LoaderType_LAMMPSDATA) {
                load_state.arg = state.atom_format_buf;
            }

            if (load_data_from_file(data, path, load_state)) {
                if ((flags & LoaderFlag_System) && !data->settings.keep_representations) {
                    remove_all_representations(data);
                    create_default_representations(data);
                }
                data->animation = {};
                reset_view(&data->view.target, data->mold.state, &data->representation.visibility_mask);
            }
            [[fallthrough]];
        }
        case Action_Cancel:
            // Reset state
            // @NOTE(Robin): Don't change this to {}, it won't work on GCC 9
            state = LoadDatasetWindowState();
            [[fallthrough]];
        case Action_None:
        default:
            break;
        }

        ImGui::EndPopup();
    }
}

/*
// TODO: Move these functions to dataset component
void clear_atom_elem_mappings(ApplicationState* data) {
    md_array_shrink(data->dataset.atom_element_remappings, 0);
}

AtomElementMapping* add_atom_elem_mapping(ApplicationState* data, str_t lbl, md_element_t elem) {
    // Check if we already have a mapping for the label -> overwrite
    size_t i = 0;
    for (; i < md_array_size(data->dataset.atom_element_remappings); ++i) {
        if (str_eq_cstr(lbl, data->dataset.atom_element_remappings[i].lbl)) break;
    }
    if (i == md_array_size(data->dataset.atom_element_remappings)) {
        AtomElementMapping mapping = {
            .elem = elem,
        };
        str_copy_to_char_buf(mapping.lbl, sizeof(mapping.lbl), lbl);
        md_array_push(data->dataset.atom_element_remappings, mapping, persistent_alloc);
        return md_array_last(data->dataset.atom_element_remappings);
    } else {
        data->dataset.atom_element_remappings[i].elem = elem;
        return &data->dataset.atom_element_remappings[i];
    }
}

void apply_atom_elem_mappings(ApplicationState* data) {
    if (data->mold.sys.atom.count == 0 || !data->mold.sys.atom.element) {
        return;
    }

    for (size_t j = 0; j < md_array_size(data->dataset.atom_element_remappings); ++j) {
        str_t lbl = str_from_cstr(data->dataset.atom_element_remappings[j].lbl);
        md_element_t elem = data->dataset.atom_element_remappings[j].elem;
        float radius = md_util_element_vdw_radius(elem);
        float mass = md_util_element_atomic_mass(elem);

        for (size_t i = 0; i < data->mold.sys.atom.count; ++i) {
            if (str_eq(lbl, data->mold.sys.atom.type[i])) {
                data->mold.sys.atom.element[i] = elem;
                data->mold.sys.atom.radius[i] = radius;
                data->mold.sys.atom.mass[i] = mass;
                data->mold.dirty_buffers |= MolBit_DirtyRadius;
            }
        }
    }
    md_system_t* mol = &data->mold.sys;
    
    
    md_array_free(mol->bond.pairs, data->mold.sys.alloc);
    md_array_free(mol->bond.order, data->mold.sys.alloc);

    md_array_free(mol->bond.conn.atom_idx, data->mold.sys.alloc);
    md_array_free(mol->bond.conn.bond_idx, data->mold.sys.alloc);

    md_index_data_free(&mol->structure);
    md_index_data_free(&mol->ring);
    
    md_system_state_t mol_state = {0};
    md_util_system_infer(mol, &mol_state, data->mold.sys.alloc, MD_UTIL_INFER_BOND_BIT | MD_UTIL_INFER_STRUCTURE_BIT);
    data->mold.dirty_buffers |= MolBit_DirtyBonds;

    flag_all_representations_as_dirty(data);
}
*/

// Create a textual script describing a selection from a bitfield with respect to some reference index
// @TODO(Robin): Clean this up, it is a mess. Just provide complete suggestions based on bitfield and molecule input.
static void write_script_range(md_strb_t& sb, const int* indices, size_t num_indices, int ref_idx = 0) {
    if (num_indices == 0) return;
    if (num_indices == 1) {
        md_strb_fmt(&sb, "%i", indices[0] - ref_idx + 1);
        return;
    }
    
    int range_beg = indices[0];
    int prev_idx  = -1;

    md_temp_scope_t temp = md_temp_begin();
    md_allocator_i* temp_alloc = md_temp_allocator(temp);

    md_array(md_irange_t) items = 0;
    
    for (size_t i = 0; i < num_indices; ++i) {
        int idx = indices[i];
        
        if (idx - prev_idx > 1) {
            if (prev_idx > range_beg) {
                md_irange_t item = {range_beg - ref_idx + 1, prev_idx - ref_idx + 1};
                md_array_push(items, item, temp_alloc);
            } else if (prev_idx != -1) {
                md_irange_t item = {prev_idx - ref_idx + 1, prev_idx - ref_idx + 1};
                md_array_push(items, item, temp_alloc);
            }
            range_beg = idx;
        }

        prev_idx = idx;
    }

    if (prev_idx - range_beg > 0) {
        md_irange_t item = {range_beg - ref_idx + 1, prev_idx - ref_idx + 1};
        md_array_push(items, item, temp_alloc);
    } else if (prev_idx != -1) {
        md_irange_t item = {prev_idx - ref_idx + 1, prev_idx - ref_idx + 1};
        md_array_push(items, item, temp_alloc);
    }

    const int64_t num_items = (int64_t)md_array_size(items);
    if (num_items > 1) sb += "{";
    for (int64_t i = 0; i < num_items; ++i) {
        md_irange_t item = items[i];
        if (item.beg == item.end) {
            md_strb_fmt(&sb, "%i", item.beg);
        }
        else {
            md_strb_fmt(&sb, "%i:%i", item.beg, item.end);
        }
        if (i < num_items - 1) {
            sb += ',';
        }
    }
    if (num_items > 1) sb += "}";
    md_temp_end(temp);
}

static md_array(str_t) generate_script_selection_suggestions(str_t ident, const md_bitfield_t* bf, const md_system_t* sys) {
    md_array(str_t) suggestions = 0;

    bool within_same_comp = true;
    bool within_same_inst = true;

    md_instance_idx_t inst_idx = -1;
    md_component_idx_t comp_idx = -1;

    md_bitfield_iter_t it = md_bitfield_iter_create(bf);
    while (md_bitfield_iter_next(&it)) {
        uint64_t a_idx = md_bitfield_iter_idx(&it);
        int32_t  i_idx = md_system_instance_find_by_atom_idx(sys, a_idx);
        int32_t  c_idx = md_system_component_find_by_atom_idx(sys, a_idx);

        if (inst_idx == -1 && comp_idx != -1) {
            inst_idx = i_idx;
        } else if (c_idx != -1 && inst_idx != i_idx) {
            within_same_inst = false;
        }

        if (comp_idx == -1 && c_idx != -1) {
            comp_idx = c_idx;
        } else if (c_idx != -1 && comp_idx != c_idx) {
            within_same_comp = false;                        
        }

        if (!within_same_comp && !within_same_inst) {
            break;
        }
    }
    
    const size_t popcount = md_bitfield_popcount(bf);
    
    md_strb_t sb = md_strb_create(frame_alloc);
    defer { md_strb_free(&sb); };

    auto write_atom_remainder = [](md_strb_t& sb, const md_bitfield_t* bf, int ref_idx = 0) {
        // Add any remainder
        const uint64_t remainder = md_bitfield_popcount(bf);
        if (remainder) {
            int* indices = (int*)md_alloc(frame_alloc, remainder * sizeof(int));
            defer { md_free(frame_alloc, indices, remainder * sizeof(int)); };

            md_bitfield_iter_extract_indices(indices, remainder, md_bitfield_iter_create(bf));
            sb += "atom(";
            write_script_range(sb, indices, remainder, ref_idx);
            sb += ")";
        }
    };

    if (comp_idx != -1 && within_same_comp) {
        const md_urange_t range = md_component_atom_range(&sys->component, comp_idx);
        if (popcount != range.end - range.beg) {
            md_strb_reset(&sb);
            sb += ident;
            sb += " = ";

            // Subset of residue is selected
            write_atom_remainder(sb, bf, range.beg);
            if (md_strb_len(sb) < 512) {
                str_t resname = md_component_name(&sys->component, comp_idx);
                md_strb_fmt(&sb, " in resname(\"" STR_FMT "\");", STR_ARG(resname));
                md_array_push(suggestions, str_copy(md_strb_to_str(sb), frame_alloc), frame_alloc);
            }
        }
    }

    else if (inst_idx != -1 && within_same_inst) {
        const md_urange_t range = md_system_instance_atom_range(sys, inst_idx);
        if (popcount != range.end - range.beg) {
            md_strb_reset(&sb);
            sb += ident;
            sb += " = ";

            // Subset of chain is selected
            write_atom_remainder(sb, bf, range.beg);
            if (md_strb_len(sb) < 512) {
				str_t inst_id = md_instance_id(&sys->instance, inst_idx);
				md_strb_fmt(&sb, " in chain(\"" STR_FMT "\");", STR_ARG(inst_id));
                md_array_push(suggestions, str_copy(md_strb_to_str(sb), frame_alloc), frame_alloc);
            }
        }
    }

    md_bitfield_t tmp_bf = md_bitfield_create(frame_alloc);
    md_bitfield_copy(&tmp_bf, bf);
    
    md_array(int) complete_chains = 0;
    md_array(int) complete_residues = 0;
    
    if (sys->instance.count) {
        for (size_t i = 0; i < sys->instance.count; ++i) {    
            const md_urange_t range = md_system_instance_atom_range(sys, i);
            if (md_bitfield_test_all_range(&tmp_bf, range.beg, range.end)) {
                md_array_push(complete_chains, (int)i, frame_alloc);
                md_bitfield_clear_range(&tmp_bf, range.beg, range.end);
            }
        }
    }

    if (sys->component.count) {
        for (size_t i = 0; i < sys->component.count; ++i) {    
            const md_urange_t range = md_component_atom_range(&sys->component, i);
            if (md_bitfield_test_all_range(&tmp_bf, range.beg, range.end)) {
                md_array_push(complete_residues, (int)i, frame_alloc);
                md_bitfield_clear_range(&tmp_bf, range.beg, range.end);
            }
        }
    }

    const uint64_t atom_remainder_count = md_bitfield_popcount(&tmp_bf);
    
    if (complete_chains) {
        md_strb_reset(&sb);
        sb += ident;
        sb += " = chain(";
        write_script_range(sb, complete_chains, md_array_size(complete_chains));
        sb += ")";

        if (complete_residues) {
            sb += " or residue(";
            write_script_range(sb, complete_residues, md_array_size(complete_residues));
            sb += ")";
        }

        if (atom_remainder_count) {
            sb += " or ";
            write_atom_remainder(sb, &tmp_bf);
        }
        
        if (md_strb_len(sb) < 512) {
            md_strb_push_char(&sb, ';');
            md_array_push(suggestions, str_copy(md_strb_to_str(sb), frame_alloc), frame_alloc);
        }
        
        md_strb_reset(&sb);
        sb += ident;
        sb += " = residue(";
        for (size_t i = 0; i < md_array_size(complete_chains); ++i) {
            md_urange_t range = md_instance_component_range(&sys->instance, complete_chains[i]);
            md_strb_fmt(&sb, "%i:%i,", range.beg + 1, range.end);
        }
        if (complete_residues) {
            write_script_range(sb, complete_residues, md_array_size(complete_residues));
        } else {
            md_strb_pop(&sb, 1);
        }
        sb += ")";

        if (atom_remainder_count) {
            sb += " or ";
            write_atom_remainder(sb, &tmp_bf);
        }
        
        if (md_strb_len(sb) < 512) {
            md_strb_push_char(&sb, ';');
            md_array_push(suggestions, str_copy(md_strb_to_str(sb), frame_alloc), frame_alloc);
        }
    }

    if (complete_residues) {
        md_strb_reset(&sb);
        sb += ident;
        sb += " = residue(";
        write_script_range(sb, complete_residues, md_array_size(complete_residues));
        sb += ")";

        if (atom_remainder_count) {
            sb += " or ";
            write_atom_remainder(sb, &tmp_bf);
        }

        if (md_strb_len(sb) < 512) {
            md_strb_push_char(&sb, ';');
            md_array_push(suggestions, str_copy(md_strb_to_str(sb), frame_alloc), frame_alloc);
        }
    }

    if (popcount) {
        md_strb_reset(&sb);
        sb += ident;
        sb += " = ";
        write_atom_remainder(sb, bf);
        if (md_strb_len(sb) < 512) {
            md_strb_push_char(&sb, ';');
            md_array_push(suggestions, str_copy(md_strb_to_str(sb), frame_alloc), frame_alloc);
        }
    }
    
    return suggestions;
}

static int64_t find_identifier(const md_script_ir_t* ir, str_t ident) {
    const int64_t num_ident = md_script_ir_num_identifiers(ir);
    const str_t* idents = md_script_ir_identifiers(ir);
    for (int64_t i = 0; i < num_ident; ++i) {
        if (str_eq(ident, idents[i])) return i;
    }
    return -1;
}

static str_t create_unique_identifier(const md_script_ir_t* ir, str_t base, md_allocator_i* alloc) {
    char buf[128];
    for (int64_t i = 1; i < 10; ++i) {
        int res = snprintf(buf, sizeof(buf), "%.*s%i", (int)base.len, base.ptr, (int)i);
        if (res > 0) {
            str_t ident = {buf, (size_t)res};
            if (find_identifier(ir, ident) == -1) {
                return str_copy(ident, alloc);
            }
        }
    }
    return str_t();
}

// # context_menu
void draw_context_popup(ApplicationState* state, const PickingHit& hit) {
    // @Robin: Will hit ever be used here? (Possibly remove argument)
    (void)hit;
    ASSERT(state);

    if (!state->mold.sys.atom.count) return;

    const size_t sss_count = single_selection_sequence_count(&state->selection.single_selection_sequence);
    const size_t num_atoms_selected = md_bitfield_popcount(&state->selection.selection_mask);

    if (ImGui::BeginPopup("Context Popup")) {
        if (num_atoms_selected == 2) {
            // Suggest construction of covalent bond
            int idx[2];
            md_bitfield_iter_extract_indices(idx, 2, md_bitfield_iter_create(&state->selection.selection_mask));

            md_bond_idx_t bond_idx = md_system_bond_find(&state->mold.sys, idx[0], idx[1]);
            if (bond_idx == -1) {
                char buf[256];
                snprintf(buf, sizeof(buf), "Create Bond (%i, %i)", idx[0] + 1, idx[1] + 1);
                if (ImGui::MenuItem(buf)) {
                    md_system_bond_insert(&state->mold.sys, idx[0], idx[1], MD_BOND_FLAG_USER_DEFINED);
                    md_system_bond_build_connectivity(&state->mold.sys);
                    state->mold.dirty_gpu_buffers |= MolBit_DirtyBonds;
                    ImGui::CloseCurrentPopup();
                }
            } else {
				md_bond_flags_t flags = md_system_bond_flags(&state->mold.sys, bond_idx);
                if (flags & MD_BOND_FLAG_USER_DEFINED) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "Remove Bond (%i, %i)", idx[0] + 1, idx[1] + 1);
                    if (ImGui::MenuItem(buf)) {
                        md_system_bond_remove(&state->mold.sys, bond_idx);
                        md_system_bond_build_connectivity(&state->mold.sys);
                        state->mold.dirty_gpu_buffers |= MolBit_DirtyBonds;
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
        }
        if (ImGui::BeginMenu("Script")) {
            bool any_suggestions = false;
            if (num_atoms_selected <= 4 && sss_count > 1) {
                int idx[4];
                MEMCPY(idx, state->selection.single_selection_sequence.idx, sizeof(idx));
                // Check if all selected atoms are within the same residue
                md_component_idx_t res_idx = md_component_find_by_atom_idx(&state->mold.sys.component, idx[0]);
                for (size_t i = 1; i < sss_count; ++i) {
                    md_component_idx_t ri = md_component_find_by_atom_idx(&state->mold.sys.component, idx[i]);
                    if (res_idx != ri) {
                        res_idx = -1;
                        break;
                    }
                }

                any_suggestions = true;
                char buf[256] = "";
                if (sss_count == 2) {
                    str_t ident = create_unique_identifier(state->script.ir, STR_LIT("dist"), frame_alloc);

                    snprintf(buf, sizeof(buf), STR_FMT " = distance(%i, %i);", STR_ARG(ident), idx[0]+1, idx[1]+1);
                    if (ImGui::MenuItem(buf)) {
                        script_editor::append_line(state->editor, str_from_cstr(buf));
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered()) {
                        str_t str = str_from_cstr(buf);
                        script_visualize_str(state, str, VIS_FLAGS);
                    }

                    if (res_idx != -1) {
                        const md_urange_t range = md_component_atom_range(&state->mold.sys.component, res_idx);
                        idx[0] -= range.beg;
                        idx[1] -= range.beg;

                        snprintf(buf, sizeof(buf), STR_FMT " = distance(%i, %i) in residue(%i);", STR_ARG(ident), idx[0]+1, idx[1]+1, res_idx+1);
                        if (ImGui::MenuItem(buf)) {
                            script_editor::append_line(state->editor, str_from_cstr(buf));
                            ImGui::CloseCurrentPopup();
                        }
                        if (ImGui::IsItemHovered()) {
                            str_t str = str_from_cstr(buf);
                            script_visualize_str(state, str, VIS_FLAGS);
                        }

                        const int32_t resid = md_component_seq_id(&state->mold.sys.component, res_idx);
                        snprintf(buf, sizeof(buf), STR_FMT " = distance(%i, %i) in resid(%i);", STR_ARG(ident), idx[0]+1, idx[1]+1, resid);
                        if (ImGui::MenuItem(buf)) {
                            script_editor::append_line(state->editor, str_from_cstr(buf));
                            ImGui::CloseCurrentPopup();
                        }
                        if (ImGui::IsItemHovered()) {
                            str_t str = str_from_cstr(buf);
                            script_visualize_str(state, str, VIS_FLAGS);
                        }

                        str_t resname = md_component_name(&state->mold.sys.component, res_idx);
                        if (resname) {
                            snprintf(buf, sizeof(buf), STR_FMT " = distance(%i, %i) in resname(\"" STR_FMT "\");", STR_ARG(ident), idx[0]+1, idx[1]+1, STR_ARG(resname));
                            if (ImGui::MenuItem(buf)) {
                                script_editor::append_line(state->editor, str_from_cstr(buf));
                                ImGui::CloseCurrentPopup();
                            }
                            if (ImGui::IsItemHovered()) {
                                str_t str = str_from_cstr(buf);
                                script_visualize_str(state, str, VIS_FLAGS);
                            }
                        }
                    }
                }
                else if(sss_count == 3) {
                    str_t ident = create_unique_identifier(state->script.ir, STR_LIT("ang"), frame_alloc);

                    snprintf(buf, sizeof(buf), STR_FMT " = angle(%i, %i, %i);", STR_ARG(ident), idx[0]+1, idx[1]+1, idx[2]+1);
                    if (ImGui::MenuItem(buf)) {
                        script_editor::append_line(state->editor, str_from_cstr(buf));
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered()) {
                        str_t str = str_from_cstr(buf);
                        script_visualize_str(state, str, VIS_FLAGS);
                    }

                    if (res_idx != -1) {
                        const md_urange_t range = md_component_atom_range(&state->mold.sys.component, res_idx);
                        idx[0] -= range.beg;
                        idx[1] -= range.beg;
                        idx[2] -= range.beg;

                        snprintf(buf, sizeof(buf), STR_FMT " = angle(%i, %i, %i) in residue(%i);", STR_ARG(ident), idx[0]+1, idx[1]+1, idx[2]+1, res_idx+1);
                        if (ImGui::MenuItem(buf)) {
                            script_editor::append_line(state->editor, str_from_cstr(buf));
                            ImGui::CloseCurrentPopup();
                        }
                        if (ImGui::IsItemHovered()) {
                            str_t str = str_from_cstr(buf);
                            script_visualize_str(state, str, VIS_FLAGS);
                        }

                        int32_t resid = md_component_seq_id(&state->mold.sys.component, res_idx);
                        snprintf(buf, sizeof(buf), STR_FMT " = angle(%i, %i, %i) in resid(%i);", STR_ARG(ident), idx[0]+1, idx[1]+1, idx[2]+1, resid);
                        if (ImGui::MenuItem(buf)) {
                            script_editor::append_line(state->editor, str_from_cstr(buf));
                            ImGui::CloseCurrentPopup();
                        }
                        if (ImGui::IsItemHovered()) {
                            str_t str = str_from_cstr(buf);
                            script_visualize_str(state, str, VIS_FLAGS);
                        }

                        str_t resname = md_component_name(&state->mold.sys.component, res_idx);
                        if (resname) {
                            snprintf(buf, sizeof(buf), STR_FMT " = angle(%i, %i, %i) in resname(\"" STR_FMT "\");", STR_ARG(ident), idx[0]+1, idx[1]+1, idx[2]+1, STR_ARG(resname));
                            if (ImGui::MenuItem(buf)) {
                                script_editor::append_line(state->editor, str_from_cstr(buf));
                                ImGui::CloseCurrentPopup();
                            }
                            if (ImGui::IsItemHovered()) {
                                str_t str = str_from_cstr(buf);
                                script_visualize_str(state, str, VIS_FLAGS);
                            }
                        }
                    }
                }
                else if(sss_count == 4) {
                    str_t ident = create_unique_identifier(state->script.ir, STR_LIT("dih"), frame_alloc);

                    snprintf(buf, sizeof(buf), "%.*s = dihedral(%i, %i, %i, %i);", STR_ARG(ident), idx[0]+1, idx[1]+1, idx[2]+1, idx[3]+1);
                    if (ImGui::MenuItem(buf)) {
                        script_editor::append_line(state->editor, str_from_cstr(buf));
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered()) {
                        str_t str = str_from_cstr(buf);
                        script_visualize_str(state, str, VIS_FLAGS);
                    }

                    if (res_idx != -1) {
                        const md_urange_t range = md_component_atom_range(&state->mold.sys.component, res_idx);
                        idx[0] -= range.beg;
                        idx[1] -= range.beg;
                        idx[2] -= range.beg;
                        idx[3] -= range.beg;

                        snprintf(buf, sizeof(buf), STR_FMT " = dihedral(%i, %i, %i, %i) in residue(%i);", STR_ARG(ident), idx[0]+1, idx[1]+1, idx[2]+1, idx[3]+1, res_idx+1);
                        if (ImGui::MenuItem(buf)) {
                            script_editor::append_line(state->editor, str_from_cstr(buf));
                            ImGui::CloseCurrentPopup();
                        }
                        if (ImGui::IsItemHovered()) {
                            str_t str = str_from_cstr(buf);
                            script_visualize_str(state, str, VIS_FLAGS);
                        }

                        int32_t resid = md_component_seq_id(&state->mold.sys.component, res_idx);
                        snprintf(buf, sizeof(buf), STR_FMT " = dihedral(%i, %i, %i, %i) in resid(%i);", STR_ARG(ident), idx[0]+1, idx[1]+1, idx[2]+1, idx[3]+1, resid);
                        if (ImGui::MenuItem(buf)) {
                            script_editor::append_line(state->editor, str_from_cstr(buf));
                            ImGui::CloseCurrentPopup();
                        }
                        if (ImGui::IsItemHovered()) {
                            str_t str = str_from_cstr(buf);
                            script_visualize_str(state, str, VIS_FLAGS);
                        }

                        str_t resname = md_component_name(&state->mold.sys.component, res_idx);
                        if (resname) {
                            snprintf(buf, sizeof(buf), STR_FMT " = dihedral(%i, %i, %i, %i) in resname(\"" STR_FMT "\");", STR_ARG(ident), idx[0]+1, idx[1]+1, idx[2]+1, idx[3]+1, STR_ARG(resname));
                            if (ImGui::MenuItem(buf)) {
                                script_editor::append_line(state->editor, str_from_cstr(buf));
                                ImGui::CloseCurrentPopup();
                            }
                            if (ImGui::IsItemHovered()) {
                                str_t str = str_from_cstr(buf);
                                script_visualize_str(state, str, VIS_FLAGS);
                            }
                        }
                    }
                }
            }
            if (num_atoms_selected >= 1) {
                const md_bitfield_t* bf = &state->selection.selection_mask;
                str_t ident = create_unique_identifier(state->script.ir, STR_LIT("sel"), frame_alloc);
                
                md_array(str_t) suggestions = generate_script_selection_suggestions(ident, bf, &state->mold.sys);

			    char buf[128]; // Buffer for limiting menu item size
                for (size_t i = 0; i < md_array_size(suggestions); ++i) {
					const char* ptr = suggestions[i].ptr;
                    str_t s = suggestions[i];
                    if (str_len(s) > 120) {
						snprintf(buf, sizeof(buf), "%.*s...", 117, s.ptr);
						ptr = buf;
                    }
                    if (ImGui::MenuItem(ptr)) {
                        script_editor::append_line(state->editor, s);
                        ImGui::CloseCurrentPopup();
                    }
                }

                any_suggestions = any_suggestions || md_array_size(suggestions) > 0;
            }
            if (!any_suggestions) {
                ImGui::Text("No suggestions for current selection");
            }
            ImGui::EndMenu();
        }

        /*
        if (data->selection.atom_idx.right_click != -1 && data->mold.sys.atom.element) {
            int idx = data->selection.atom_idx.right_click;
            if (0 <= idx && idx < (int)data->mold.sys.atom.count) {
                char label[64] = "";
                str_t type = data->mold.sys.atom.type[idx];
                snprintf(label, sizeof(label), "Remap Element for '%.*s'", (int)type.len, type.ptr);
                if (ImGui::BeginMenu(label)) {
                    static char input_buf[32] = "";
                    md_element_t elem = data->mold.sys.atom.element[idx];
                    str_t name = md_util_element_name(elem);
                    str_t sym  = md_util_element_symbol(elem);

                    ImGui::Text("Current Element: %.*s (%.*s)", (int)name.len, name.ptr, (int)sym.len, sym.ptr);

                    str_t elem_str = {input_buf, strnlen(input_buf, sizeof(input_buf))};
                    md_element_t new_elem = md_util_element_lookup(elem_str);
                    const bool is_valid = new_elem != 0;

                    ImGui::InputQuery("##Symbol", input_buf, sizeof(input_buf), is_valid, "Cannot recognize Element symbol");
                    str_t new_name = md_util_element_name(new_elem);
                    str_t new_sym  = md_util_element_symbol(new_elem);
                    ImGui::Text("New Element: %.*s (%.*s)", (int)new_name.len, new_name.ptr, (int)new_sym.len, new_sym.ptr);
                    if (!is_valid) ImGui::PushDisabled();
                    if (ImGui::Button("Apply") && is_valid) {
                        add_atom_elem_mapping(data, type, new_elem);
                        apply_atom_elem_mappings(data);
                        ImGui::CloseCurrentPopup();
                    }
                    if (!is_valid) ImGui::PopDisabled();
                    ImGui::EndMenu();
                }
            }
        }
        */

        if (ImGui::BeginMenu("Selection")) {
            if (ImGui::MenuItem("Invert")) {
                md_bitfield_not_inplace(&state->selection.selection_mask, 0, state->mold.sys.atom.count);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) {
                md_bitfield_not(&state->selection.highlight_mask, &state->selection.selection_mask, 0, state->mold.sys.atom.count);
            }
            if (ImGui::MenuItem("Query")) {
                state->selection.query.show_window = true;
                ImGui::CloseCurrentPopup();
            }
            if (num_atoms_selected > 0) {
                if (ImGui::MenuItem("Grow")) {
                    state->selection.grow.show_window = true;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Clear")) {
                    md_bitfield_clear(&state->selection.selection_mask);
                    single_selection_sequence_clear(&state->selection.single_selection_sequence);
                }
            }
            ImGui::EndMenu();
        }
        
        if (num_atoms_selected > 0) {
            if (ImGui::MenuItem("Set as Centering Target")) {
                md_bitfield_clear(&state->operations.selection_mask);
                md_bitfield_copy(&state->operations.selection_mask, &state->selection.selection_mask);
                recenter_mark_selection_dirty(state);
                state->operations.recenter_query.enabled = false;
                recenter_update_target_data(state);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) {
                md_bitfield_clear(&state->selection.highlight_mask);
                md_bitfield_copy(&state->selection.highlight_mask, &state->selection.selection_mask);
            }
        }

        ImGui::EndPopup();
    }
}

static void draw_selection_grow_window(ApplicationState* data) {
    ImGui::SetNextWindowSize(ImVec2(300,150), ImGuiCond_Always);
    if (ImGui::Begin("Selection Grow", &data->selection.grow.show_window, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ImGui::PushItemWidth(-1);
        static uint64_t sel_popcount = 0;
        const uint64_t popcount = md_bitfield_popcount(&data->selection.selection_mask);
        const bool mode_changed = ImGui::Combo("##Mode", (int*)(&data->selection.grow.mode), "Covalent Bond\0Radial\0\0");
        const char* fmt = (data->selection.grow.mode == SelectionGrowthMode::CovalentBond) ? "%.0f" : "%.2f";
        const bool extent_changed = ImGui::SliderFloat("##Extent", &data->selection.grow.extent, 1.0f, 20.f, fmt);
        const bool appearing = ImGui::IsWindowAppearing();
        const bool sel_changed = popcount != sel_popcount;
        ImGui::PopItemWidth();

        const bool apply = ImGui::Button("Apply");

        // Need to invalidate when selection changes
        data->selection.grow.mask_invalid |= (mode_changed || extent_changed || appearing || sel_changed);

        if (data->selection.grow.mask_invalid) {
            sel_popcount = popcount;
            data->selection.grow.mask_invalid = false;
            md_bitfield_copy(&data->selection.grow.mask, &data->selection.selection_mask);

            switch (data->selection.grow.mode) {
            case SelectionGrowthMode::CovalentBond:
                md_util_mask_grow_by_bonds(&data->selection.grow.mask, &data->mold.sys, (int)data->selection.grow.extent, &data->representation.visibility_mask);
                break;
            case SelectionGrowthMode::Radial: {
                md_util_mask_grow_by_radius(&data->selection.grow.mask, &data->mold.state, data->selection.grow.extent, &data->representation.visibility_mask);
                break;
            }
            default:
                ASSERT(false);
            }

            grow_mask_by_selection_granularity(&data->selection.grow.mask, data->selection.granularity, data->mold.sys);
        }

        const bool show_preview =   (ImGui::GetHoveredID() == ImGui::GetID("##Extent")) ||
                                    (ImGui::GetActiveID()  == ImGui::GetID("##Extent")) ||
                                    (ImGui::GetHoveredID() == ImGui::GetID("Apply"));

        if (show_preview) {
            md_bitfield_copy(&data->selection.highlight_mask, &data->selection.grow.mask);
        }
        if (apply) {
            md_bitfield_copy(&data->selection.selection_mask, &data->selection.grow.mask);
            data->selection.grow.mask_invalid = true;
        }
    }
    ImGui::End();
}

static void draw_selection_query_window(ApplicationState* data) {
    ImGui::SetNextWindowSize(ImVec2(300,100), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Selection Query", &data->selection.query.show_window, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {

        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            data->selection.query.show_window = false;
            return;
        }

        static double query_frame = 0.0;

        ImGui::PushItemWidth(-1);
        bool apply = ImGui::InputQuery("##query", data->selection.query.buf, sizeof(data->selection.query.buf), data->selection.query.query_ok, data->selection.query.error, ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopItemWidth();

        if (ImGui::IsItemEdited() || data->animation.frame != query_frame) {
            data->selection.query.query_invalid = true;
        }
        bool preview = ImGui::IsItemFocused() || ImGui::IsItemHovered();

        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere(-1);
        }

        if (!data->selection.query.query_ok) ImGui::PushDisabled();
        apply |= ImGui::Button("Apply");
        if (!data->selection.query.query_ok) ImGui::PopDisabled();

        preview |= ImGui::IsItemHovered();

        if (data->selection.query.query_invalid) {
            data->selection.query.query_invalid = false;
            data->selection.query.query_ok = md_filter(&data->selection.query.mask, str_from_cstr(data->selection.query.buf), &data->mold.sys, &data->mold.state, data->script.ir, NULL, data->selection.query.error, sizeof(data->selection.query.error));
            query_frame = data->animation.frame;

            if (data->selection.query.query_ok) {
                grow_mask_by_selection_granularity(&data->selection.query.mask, data->selection.granularity, data->mold.sys);
            } else {
                md_bitfield_clear(&data->selection.query.mask);
            }
        }

        if (preview) {
            md_bitfield_copy(&data->selection.highlight_mask, &data->selection.query.mask);
        }

        if (apply && data->selection.query.query_ok) {
            md_bitfield_copy(&data->selection.selection_mask, &data->selection.query.mask);
            data->selection.query.show_window = false;
        }
    }
    ImGui::End();
}

static void draw_animation_window(ApplicationState* data) {
    ASSERT(data);
    size_t num_frames = run_num_frames(data);
    if (num_frames == 0) return;

    ASSERT(data->timeline.x_values);
    ASSERT(md_array_size(data->timeline.x_values) == num_frames);

    ImGui::SetNextWindowSize({300,200}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Animation", &data->animation.show_window, ImGuiWindowFlags_NoFocusOnAppearing)) {
        // The recording sets the frame itself
        ImGui::BeginDisabled(data->movie.state == MovieRecordingState::Recording);
        ImGui::Text("Num Frames: %zu", num_frames);
        md_unit_t time_unit = data->timeline.time_unit;
        double t   = frame_to_time(data->animation.frame, *data);
        double min = data->timeline.x_values[0];
        double max = data->timeline.x_values[num_frames - 1];
        char time_label[64];
        if (md_unit_is_none(time_unit)) {
            snprintf(time_label, sizeof(time_label), "Time");
        } else {
            char unit_buf[32];
            md_unit_print(unit_buf, sizeof(unit_buf), time_unit);
            snprintf(time_label, sizeof(time_label), "Time (%s)", unit_buf);
        }
        const float w = ImGui::CalcTextSize("Time (ps)").x;
        const float item_width = MAX(ImGui::GetContentRegionAvail().x - w, 100.f);
        ImGui::PushItemWidth(item_width);
        if (ImGui::BeginCombo("Interp.", interpolation_mode_str[(int)data->animation.interpolation])) {
            for (int i = 0; i < (int)InterpolationMode::Count; ++i) {
                if (ImGui::Selectable(interpolation_mode_str[i], (int)data->animation.interpolation == i)) {
                    data->animation.interpolation = (InterpolationMode)i;
					data->mold.interpolate_system_state = true;
                    data->mold.dirty_gpu_buffers |= MolBit_ClearVelocity;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Interpolation Mode for Atom Positions");
        }
        if (ImGui::SliderScalar(time_label, ImGuiDataType_Double, &t, &min, &max, "%.2f")) {
            data->animation.frame = time_to_frame(t, data->timeline.x_values);
        }
        ImGui::SliderFloat("Speed", &data->animation.fps, -200.0f, 200.f, "%.2f", ImGuiSliderFlags_Logarithmic);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Animation Speed in Frames Per Second");
        }
        if (data->animation.interpolation == InterpolationMode::CubicSpline) {
            ImGui::SliderFloat("Tension", &data->animation.tension, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Tension of the Cubic Spline");
            }
        }
        switch (data->animation.mode) {
            case PlaybackMode::Playing:
                if (ImGui::Button((const char*)ICON_FA_PAUSE)) {
                    data->animation.mode = PlaybackMode::Stopped;
                }
                break;
            case PlaybackMode::Stopped:
                if (ImGui::Button((const char*)ICON_FA_PLAY)) {
                    data->animation.mode = PlaybackMode::Playing;
                }
                break;
            default:
                ASSERT(false);
        }
        ImGui::SameLine();
        if (ImGui::Button((const char*)ICON_FA_STOP)) {
            data->animation.mode = PlaybackMode::Stopped;
            data->animation.frame = 0.0;
        }
        ImGui::PopItemWidth();
        ImGui::EndDisabled();
    }
    ImGui::End();
}

static bool draw_representations_window_electronic_structure(ApplicationState* state, Representation& rep) {
    ImGuiComboFlags flags = 0;
    ElectronicStructureRepresentation& es = rep.electronic_structure;

    // The key is an opaque hash now, so it is no longer worth showing beside the label: a property
    // with no label is named by its path's last segment already, which is the readable thing.
    auto density_property_combo_label = [](char* buf, size_t buf_cap, const DensityProperty* prop) {
        ASSERT(buf);
        ASSERT(buf_cap > 0);
        if (!prop || str_empty(prop->label)) {
            snprintf(buf, buf_cap, "None");
            return;
        }
        snprintf(buf, buf_cap, "%.*s", (int)prop->label.len, prop->label.ptr);
    };

    bool advanced = state->representation.advanced_mode;
    bool update_rep = false;

    const ElectronicStructureSourceFlags source_mask = es_source_mask(state->mold.sys);

    if (ImGui::BeginCombo("volume src", electronic_structure_source_str[(int)es.source], flags)) {
        for (int n = 0; n < (int)ElectronicStructureSource::Count; n++) {
            ElectronicStructureSource source = (ElectronicStructureSource)n;
            bool is_selected = (es.source == source);
            bool disabled = !electronic_structure_source_supported(source_mask, source);

            if (disabled) ImGui::PushDisabled();
            if (ImGui::Selectable(electronic_structure_source_str[n], is_selected)) {
                es.source = source;
                electronic_structure_set_source_defaults(&es);
                update_rep = true;
            }
            if (disabled) ImGui::PopDisabled();

            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (advanced) {
        if (ImGui::Combo("volume res", (int*)&es.resolution, volume_resolution_str, IM_ARRAYSIZE(volume_resolution_str))) {
            update_rep = true;
        }
    }

    if (electronic_structure_is_density_property(es)) {
        // Gathered here rather than kept in a list somewhere: it is a handful of entries borrowed
        // from the attribute table, and a stored copy would be one more thing to invalidate on load.
        DensityProperty density_props[64];
        const int num_density_props = (int)MIN(density_properties_gather(density_props, ARRAY_SIZE(density_props), state->mold.sys), ARRAY_SIZE(density_props));
        if (num_density_props > 0) {
            // A key that names nothing in the current list - a fresh representation, or a workspace
            // saved against a file whose properties differ - falls back to the first one rather
            // than drawing nothing.
            const DensityProperty* selected = nullptr;
            for (int n = 0; n < num_density_props; ++n) {
                if (density_props[n].key == es.density_property_key) {
                    selected = &density_props[n];
                    break;
                }
            }
            if (!selected) {
                selected = &density_props[0];
                es.density_property_key = selected->key;
                update_rep = true;
            }

            char preview[256];
            density_property_combo_label(preview, sizeof(preview), selected);
            if (ImGui::BeginCombo("property", preview)) {
                for (int n = 0; n < num_density_props; ++n) {
                    char label[256];
                    density_property_combo_label(label, sizeof(label), &density_props[n]);
                    const bool is_selected = (es.density_property_key == density_props[n].key);
                    if (ImGui::Selectable(label, is_selected)) {
                        if (es.density_property_key != density_props[n].key) {
                            es.density_property_key = density_props[n].key;
                            update_rep = true;
                        }
                    }

                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        } else {
            ImGui::LabelText("property", "No density properties available");
        }
    }

    const bool show_spin = electronic_structure_uses_spin(es);
    if (show_spin) {
        // Asked of the table: a second, DISTINCT set of coefficients. This used to be true for a
        // restricted file too, because beta is published there as an alias of alpha and the fan-in
        // only checked that the column existed - so the selector appeared and toggled between two
        // names for one datum.
        const bool has_beta = es_has_distinct_beta_orbitals(state->mold.sys);
        if (es.source == ElectronicStructureSource::MolecularOrbital) {
            if (es.spin != ElectronicStructureSpin::Alpha && es.spin != ElectronicStructureSpin::Beta) {
                es.spin = ElectronicStructureSpin::Alpha;
                update_rep = true;
            }
            if (has_beta) {
                const char* spin_options[] = { "Alpha", "Beta" };
                int spin_idx = es.spin == ElectronicStructureSpin::Beta ? 1 : 0;
                if (ImGui::Combo("spin", &spin_idx, spin_options, IM_ARRAYSIZE(spin_options))) {
                    es.spin = spin_idx == 1 ? ElectronicStructureSpin::Beta : ElectronicStructureSpin::Alpha;
                    update_rep = true;
                }
            }
        }
        else if (es.source == ElectronicStructureSource::ElectronDensity) {
            if (has_beta) {
                const ElectronicStructureSpin spin_options[] = {
                    ElectronicStructureSpin::Total,
                    ElectronicStructureSpin::Alpha,
                    ElectronicStructureSpin::Beta,
                    ElectronicStructureSpin::Difference,
                };
                const char* spin_labels[] = { "Total", "Alpha", "Beta", "Difference" };
                int spin_idx = 0;
                for (int i = 0; i < (int)IM_ARRAYSIZE(spin_options); ++i) {
                    if (es.spin == spin_options[i]) {
                        spin_idx = i;
                        break;
                    }
                }
                if (es.spin != spin_options[spin_idx]) {
                    es.spin = spin_options[spin_idx];
                    update_rep = true;
                }
                if (ImGui::Combo("spin", &spin_idx, spin_labels, IM_ARRAYSIZE(spin_labels))) {
                    es.spin = spin_options[spin_idx];
                    if (es.spin != ElectronicStructureSpin::Difference) {
                        es.use_magnitude = false;
                    }
                    update_rep = true;
                }
            } else if (es.spin != ElectronicStructureSpin::Total) {
                es.spin = ElectronicStructureSpin::Total;
                es.use_magnitude = false;
                update_rep = true;
            }
        }
    }

    if (es.source == ElectronicStructureSource::NaturalTransitionOrbital) {
        if (ImGui::Combo("component", (int*)&es.nto_component, electronic_structure_nto_component_str, IM_ARRAYSIZE(electronic_structure_nto_component_str))) {
            update_rep = true;
        }
    }

    if (es.source == ElectronicStructureSource::TransitionDensity) {
        if (ImGui::Combo("component", (int*)&es.transition_density_component, electronic_structure_transition_density_component_str, IM_ARRAYSIZE(electronic_structure_transition_density_component_str))) {
            update_rep = true;
        }
    }

    const bool show_molecular_orbitals = electronic_structure_uses_orbital_idx(es);
    const bool show_exited_states = electronic_structure_uses_excited_state_idx(es);
    const bool show_lambdas = electronic_structure_uses_nto_lambda_idx(es);

    if (show_molecular_orbitals) {
        // The (homo)/(lumo) markers sit at different indices for the two spins in an unrestricted
        // calculation, and the rendered volume already follows es.spin - so the frontier has to
        // follow it too, or the entry marked (homo) under spin=Beta is alpha's HOMO index.
        //
        // Formatted here rather than read out of a precomputed list: it is an index, an energy and
        // two markers, and a copy of it would be one more thing to keep in step with the table.
        const bool use_beta = (es.spin == ElectronicStructureSpin::Beta) && es_has_distinct_beta_orbitals(state->mold.sys);

        size_t num_orbitals = 0;
        OrbitalFrontier frontier = {};
        if (es_orbital_extent(state->mold.sys, &num_orbitals, nullptr) && num_orbitals > 0) {
            es_orbital_frontier(&frontier, state->mold.sys, use_beta ? es_path::beta_occupation : es_path::alpha_occupation);

            // Energies alongside the index, so the list can be read as an orbital diagram. A column
            // that does not span the orbital set is not one to index with, so it is dropped whole
            // rather than bounds checked per row - the list then falls back to bare indices.
            size_t num_energies = 0;
            const double* energy = es_orbital_energies(&num_energies, state->mold.sys, use_beta ? es_path::beta_energy : es_path::alpha_energy);
            if (num_energies != num_orbitals) energy = nullptr;

            // The UI font is monospaced (Dejavu Sans Mono), so a fixed field width per column is all
            // the alignment this needs: right aligned index, then a fixed number of decimals, which
            // puts every decimal point of the list on one column. Widths are passed in rather than
            // baked into the format so the preview - a single line, nothing to align against - can
            // ask for the same text unpadded.
            int idx_width = 1;
            for (size_t m = num_orbitals; m >= 10; m /= 10) idx_width++;

            auto orbital_label = [&](char* buf, size_t cap, int n, int idx_w, int ene_w) {
                const char* mark = (n == frontier.homo_idx) ? " (homo)" : (n == frontier.lumo_idx) ? " (lumo)" : "";
                if (energy) {
                    snprintf(buf, cap, "%*i  %*.4f%s", idx_w, n + 1, ene_w, energy[n], mark);
                } else {
                    snprintf(buf, cap, "%*i%s", idx_w, n + 1, mark);
                }
            };

            es.orbital_idx = CLAMP(es.orbital_idx, 0, (int)num_orbitals - 1);

            char preview[48];
            orbital_label(preview, sizeof(preview), es.orbital_idx, 0, 0);
            if (ImGui::BeginCombo("orbital idx", preview)) {
                // Highest index first, so the lowest lying orbital sits at the bottom - the
                // conventional orbital diagram, and the same order the Molecular Orbitals table
                // defaults to. Canonical MOs come out of the SCF in ascending energy, so descending
                // index IS descending energy; sorting on the energies instead would only differ for
                // a non-aufbau set, and would then disagree with that table.
                for (int n = (int)num_orbitals - 1; n >= 0; n--) {
                    bool is_selected = (es.orbital_idx == n);
                    char label[48];
                    orbital_label(label, sizeof(label), n, idx_width, 10);
                    if (ImGui::Selectable(label, is_selected)) {
                        if (es.orbital_idx != n) {
                            update_rep = true;
                        }
                        es.orbital_idx = n;
                    }

                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
    }

    if (show_exited_states) {
        const int num_excited_states = (int)es_excited_state_count(state->mold.sys);
        if (num_excited_states > 0) {
            es.excited_state_idx = CLAMP(es.excited_state_idx, 0, num_excited_states - 1);
            char preview[16];
            snprintf(preview, sizeof(preview), "%i", es.excited_state_idx + 1);
            if (ImGui::BeginCombo("state idx", preview)) {
                for (int n = 0; n < num_excited_states; n++) {
                    const bool is_selected = (es.excited_state_idx == n);
                    char label[16];
                    snprintf(label, sizeof(label), "%i", n + 1);
                    if (ImGui::Selectable(label, is_selected)) {
                        if (es.excited_state_idx != n) {
                            update_rep = true;
                        }
                        es.excited_state_idx = n;
                    }

                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            if (show_lambdas) {
                // The same 1e-3 the rest of the tree stops at, and the same place the zero padded
                // tail of the ragged lambda axis stops it.
                const double LAMBDA_CUTOFF = 1.0e-3;
                double lambdas[32];
                const int num_lambdas = (int)es_nto_lambdas(lambdas, ARRAY_SIZE(lambdas), state->mold.sys,
                                                            (size_t)es.excited_state_idx, LAMBDA_CUTOFF);
                if (num_lambdas > 0) {
                    es.nto_lambda_idx = CLAMP(es.nto_lambda_idx, 0, num_lambdas - 1);

                    auto lambda_label = [&lambdas](char* buf, size_t cap, int n) {
                        // The bytes rather than a u8 literal: u8"" is const char8_t* in C++20 and
                        // a format string has to be a plain char array. \xce\xbb is U+03BB, lambda.
                        snprintf(buf, cap, "\xce\xbb[%i] (%.3f)", n + 1, lambdas[n]);
                    };

                    char preview[32];
                    lambda_label(preview, sizeof(preview), es.nto_lambda_idx);
                    if (ImGui::BeginCombo("lambda idx", preview)) {
                        for (int n = 0; n < num_lambdas; n++) {
                            const bool is_selected = (es.nto_lambda_idx == n);
                            char label[32];
                            lambda_label(label, sizeof(label), n);
                            if (ImGui::Selectable(label, is_selected)) {
                                if (es.nto_lambda_idx != n) {
                                    update_rep = true;
                                }
                                es.nto_lambda_idx = n;
                            }

                            if (is_selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
            }
        }
    }
#if 0
    // Currently we do not expose DVR, since we do not have a good way of exposing the alpha ramp for the transfer function...
    ImGui::Checkbox("Enable DVR", &rep.electronic_structure.dvr.enabled);
    if (rep.electronic_structure.dvr.enabled) {
        const ImVec2 button_size = {160, 0};
        if (ImPlot::ColormapButton(ImPlot::GetColormapName(rep.electronic_structure.dvr.colormap), button_size, rep.electronic_structure.dvr.colormap)) {
            ImGui::OpenPopup("Colormap Selector");
        }
        if (ImGui::BeginPopup("Colormap Selector")) {
            for (int map = 4; map < ImPlot::GetColormapCount(); ++map) {
                if (ImPlot::ColormapButton(ImPlot::GetColormapName(map), button_size, map)) {
                    rep.electronic_structure.dvr.colormap = map;
                    update_rep = true;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
    }
#endif
    if (electronic_structure_uses_magnitude_toggle(es)) {
        const char* magnitude_label = es.source == ElectronicStructureSource::ElectronDensity ? (const char*)u8"magnitude |ρ|" : (const char*)u8"magnitude |Ψ|";
        if (ImGui::Checkbox(magnitude_label, &es.use_magnitude)) {
            update_rep = true;
        }
    }
    
    const double min_tau = 0.0;
    const double max_tau = 1.0;
    
    const double min_power = 2.0;
    const double max_power = 20.0;

    const double iso_min = 1.0e-8;
    const double iso_max = 5.0;

    if (electronic_structure_is_density_property(es)) {
        int num_isos = CLAMP(es.density_property.num_isos, 1, (int)ARRAY_SIZE(es.density_property.values));
        if (num_isos != es.density_property.num_isos) {
            es.density_property.num_isos = num_isos;
            update_rep = true;
        }

        if (ImGui::SliderInt("iso count", &num_isos, 1, (int)ARRAY_SIZE(es.density_property.values))) {
            es.density_property.num_isos = num_isos;
            update_rep = true;
        }

        for (int i = 0; i < es.density_property.num_isos; ++i) {
            char value_label[32];
            char color_label[32];
            snprintf(value_label, sizeof(value_label), "iso %d value", i + 1);
            snprintf(color_label, sizeof(color_label), "iso %d color", i + 1);
            update_rep |= ImGui::DragScalar(value_label, ImGuiDataType_Double, &es.density_property.values[i], 0.001f, NULL, NULL, "%.8f");
            update_rep |= ImGui::ColorEdit4(color_label, es.density_property.colors[i].elem);
        }

        if (advanced) {
            ImGui::SliderScalar((const char*)u8"iso τ", ImGuiDataType_Double, &es.iso_optical_density, &min_tau, &max_tau, "%.4f", ImGuiSliderFlags_Logarithmic);
            ImGui::SetItemTooltip("Optical density shared by all custom isosurfaces");
        }

        return update_rep;
    }
    
    const char* iso_label = electronic_structure_iso_value_label();
    
    if (electronic_structure_is_signed(es)) {
        ImGui::SliderScalar(iso_label, ImGuiDataType_Double, &rep.electronic_structure.iso_value, &iso_min, &iso_max, "%.8f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetItemTooltip("%s", electronic_structure_iso_value_tooltip(rep.electronic_structure));
        if (advanced) {
            ImGui::SliderScalar((const char*)u8"iso τ", ImGuiDataType_Double, &rep.electronic_structure.iso_optical_density, &min_tau, &max_tau, "%.4f", ImGuiSliderFlags_Logarithmic);
            ImGui::SetItemTooltip("Optical density of the isosurfaces");
        }
        if (rep.electronic_structure.use_atom_colors) {
            ImGui::ColorEdit4("tint positive", rep.electronic_structure.tint_psi_pos.elem);
            ImGui::ColorEdit4("tint negative", rep.electronic_structure.tint_psi_neg.elem);
        } else {
            ImGui::ColorEdit4("color positive", rep.electronic_structure.col_psi_pos.elem);
            ImGui::ColorEdit4("color negative", rep.electronic_structure.col_psi_neg.elem);
        }
        if (advanced || rep.electronic_structure.use_atom_colors) {
            update_rep |= ImGui::Checkbox("use atom colors", &rep.electronic_structure.use_atom_colors);
        }

        if (advanced && rep.electronic_structure.use_atom_colors) {
            update_rep |= ImGui::SliderScalar("gaussian power", ImGuiDataType_Double, &rep.electronic_structure.gaussian_splatting_power, &min_power, &max_power, "%.2f");
        }
    }
    else {
        ImGui::SliderScalar(iso_label, ImGuiDataType_Double, &rep.electronic_structure.iso_value, &iso_min, &iso_max, "%.8f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetItemTooltip("%s", electronic_structure_iso_value_tooltip(rep.electronic_structure));
        if (advanced) {
            ImGui::SliderScalar((const char*)u8"iso τ", ImGuiDataType_Double, &rep.electronic_structure.iso_optical_density, &min_tau, &max_tau, "%.4f", ImGuiSliderFlags_Logarithmic);
            ImGui::SetItemTooltip("Optical density of the isosurfaces");
        }
        if (rep.electronic_structure.use_atom_colors) {
            if (es.source == ElectronicStructureSource::TransitionDensity && es.transition_density_component == ElectronicStructureTransitionDensityComponent::Attachment) {
                ImGui::ColorEdit4("tint attachment", rep.electronic_structure.tint_att.elem);
            }
            else if (es.source == ElectronicStructureSource::TransitionDensity && es.transition_density_component == ElectronicStructureTransitionDensityComponent::Detachment) {
                ImGui::ColorEdit4("tint detachment", rep.electronic_structure.tint_det.elem);
            } else {
                ImGui::ColorEdit4("tint density", rep.electronic_structure.tint_den.elem);
            }
        } else {
            if (es.source == ElectronicStructureSource::TransitionDensity && es.transition_density_component == ElectronicStructureTransitionDensityComponent::Attachment) {
                ImGui::ColorEdit4("color attachment", rep.electronic_structure.col_att.elem);
            }
            else if (es.source == ElectronicStructureSource::TransitionDensity && es.transition_density_component == ElectronicStructureTransitionDensityComponent::Detachment) {
                ImGui::ColorEdit4("color detachment", rep.electronic_structure.col_det.elem);
            } else {
                ImGui::ColorEdit4("color density",  rep.electronic_structure.col_den.elem);
            }
        }
        if (advanced || rep.electronic_structure.use_atom_colors) {
            update_rep |= ImGui::Checkbox("use atom colors", &rep.electronic_structure.use_atom_colors);
        }
        if (advanced && rep.electronic_structure.use_atom_colors) {
            update_rep |= ImGui::SliderScalar("gaussian power", ImGuiDataType_Double, &rep.electronic_structure.gaussian_splatting_power, &min_power, &max_power, "%.2f");
        }
    }

    return update_rep;
}

static void draw_representations_window(ApplicationState* state) {
    if (!state->representation.show_window) return;

    ImGui::SetNextWindowSize({300,200}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Representations", &state->representation.show_window, ImGuiWindowFlags_NoFocusOnAppearing);
    if (ImGui::Button("create new")) {
        create_representation(state);
    }
    ImGui::SameLine();
    if (ImGui::DeleteButton("remove all")) {
        remove_all_representations(state);
    }
    ImGui::SameLine();

    const char* advanced_label = ICON_FA_SLIDERS;
    const float checkbox_width =
        ImGui::GetFrameHeight() +
        ImGui::GetStyle().ItemInnerSpacing.x +
        ImGui::CalcTextSize(advanced_label).x;
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - checkbox_width);
    ImGui::Checkbox(advanced_label, &state->representation.advanced_mode);
    ImGui::SetItemTooltip("Advanced mode (show all representation settings)");

    bool advanced = state->representation.advanced_mode;

    ImGui::Spacing();
    ImGui::Separator();
    for (int rep_idx = 0; rep_idx < (int)md_array_size(state->representation.reps); rep_idx++) {
        bool update_rep = false;
        Representation& rep = state->representation.reps[rep_idx];

        char label[128];
        snprintf(label, sizeof(label), "%s###ID", rep.name);

        ImGui::PushID(rep_idx);
        
        const float pad = 3.0f;
        const float size = ImGui::GetFontSize() + pad * 2;
        const float spacing = 2.f;
        const float total_button_size = (size + 1) * 3;

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, pad));
        bool draw_content = ImGui::TreeNodeEx("##label", ImGuiTreeNodeFlags_FramePadding);
        ImGui::PopStyleVar();

        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - total_button_size);
        ImGui::InputText("##name", rep.name, sizeof(rep.name));

        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - total_button_size, spacing);
        const char* eye_icon = rep.enabled ? ICON_FA_EYE : ICON_FA_EYE_SLASH;
        
        const ImVec2 btn_size = {size, size};
        if (ImGui::Button(eye_icon, btn_size)) {
            rep.enabled = !rep.enabled;
            state->representation.atom_visibility_mask_dirty = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Show/Hide");
        }
        ImGui::SameLine(0, spacing);
        if (ImGui::Button(ICON_FA_COPY, btn_size)) {
            clone_representation(state, rep);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Duplicate");
        }
        ImGui::SameLine(0, spacing);
        if (ImGui::DeleteButton(ICON_FA_XMARK, btn_size)) {
            remove_representation(state, rep_idx);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Remove");
        }

        if (draw_content) {
            ImVec4 sub_group_color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            sub_group_color.w = 0.5f;

            const float inner_item_width = MAX(ImGui::GetContentRegionAvail().x - ImGui::GetStyle().IndentSpacing - ImGui::CalcTextSize("helix scale").x, 100.f);
            ImGui::PushItemWidth(inner_item_width);

            // @TODO: Only display the representations which can be used for the current dataset
            if (!rep.type_is_valid) ImGui::PushInvalid();
            if (ImGui::BeginCombo("type", representation_type_str[(int)rep.type])) {
                for (int i = 0; i < (int)RepresentationType::Count; ++i) {
                    if (i == (int)RepresentationType::ElectronicStructure) {
                        // Do not enlist Electronic Structure if there are no orbitals available
                        size_t num_orbitals = 0;
                        if (!es_orbital_extent(state->mold.sys, &num_orbitals, nullptr) || num_orbitals == 0) continue;
                    }
                    if (ImGui::Selectable(representation_type_str[(int)i], i == (int)rep.type)) {
                        rep.type = (RepresentationType)i;
                        update_rep = true;
                    }
                }
                ImGui::EndCombo();
            }
            if (!rep.type_is_valid) ImGui::PopInvalid();

			const double dipole_min = 0.01;
			const double dipole_max = 100.0;

            switch (rep.type) {
            case RepresentationType::ElectronicStructure:
                update_rep |= draw_representations_window_electronic_structure(state, rep);
                break;
            case RepresentationType::DipoleMoment:
            {
                // The combo lists GROUPS, which is what the file publishes. A group holding one
                // moment per excited state is then picked apart by the index widget below, rather
                // than flattening every (group, element) pair into one list nobody can scan.
                DipoleGroup groups[16];
                const size_t num_groups = MIN(dipole_groups_gather(groups, ARRAY_SIZE(groups), state->mold.sys), ARRAY_SIZE(groups));

                // Asked of the key the representation holds, not searched for in the list: a key
                // that names nothing - a workspace pointed at a file without that group - simply
                // leaves the preview empty and the index widget away.
                DipoleGroup selected = {};
                const bool has_selection = dipole_group_from_key(&selected, state->mold.sys, rep.dipole.dipole_key);

                // A group can come back shorter than it was (fewer excited states), which would
                // otherwise leave the representation pointing past its end.
                if (has_selection && rep.dipole.dipole_index >= selected.count) {
                    rep.dipole.dipole_index = 0;
                    update_rep = true;
                }

                char preview[64] = "";
                if (has_selection) dipole_label_pretty(preview, sizeof(preview), selected.label);

                if (ImGui::BeginCombo("dipole", preview)) {
                    for (size_t i = 0; i < num_groups; ++i) {
                        char lbl[64];
                        dipole_label_pretty(lbl, sizeof(lbl), groups[i].label);
                        const bool is_selected = groups[i].key == rep.dipole.dipole_key;
                        if (ImGui::Selectable(lbl, is_selected)) {
                            if (!is_selected) {
                                rep.dipole.dipole_key   = groups[i].key;
                                rep.dipole.dipole_index = 0;   // a different group; the old element means nothing in it
                                update_rep = true;
                            }
                        }
                        if (is_selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                // Only an array of moments has an index to choose. One moment is addressed by the
                // group alone, and a slider from 1 to 1 is a control that cannot do anything.
                if (has_selection && selected.count > 1) {
                    int index = (int)rep.dipole.dipole_index + 1;
                    if (ImGui::SliderInt("index", &index, 1, (int)selected.count)) {
                        rep.dipole.dipole_index = (uint32_t)CLAMP(index - 1, 0, (int)selected.count - 1);
                        update_rep = true;
                    }
                }
            }
            ImGui::ColorEdit4("color", rep.dipole.color.elem);
				ImGui::SliderScalar("scale", ImGuiDataType_Double, &rep.dipole.scale, &dipole_min, &dipole_max, "%.3f");
				ImGui::SliderFloat("radius", &rep.dipole.radius, 0.01f, 0.5f);
				ImGui::DragFloat3("offset", rep.dipole.offset.elem, 0.01f, -100.0f, 100.0f);
                break;
			default:
                if (ImGui::InputQuery("filter", rep.filt, sizeof(rep.filt), rep.filt_is_valid, rep.filt_error)) {
                    rep.filt_is_dirty = true;
                    update_rep = true;
                }
                break;
            }

            if (representation_uses_atom_colors(rep)) {
                if (ImGui::Combo("atom color", (int*)(&rep.color_mapping), color_mapping_str, IM_ARRAYSIZE(color_mapping_str))) {
                    update_rep = true;
                }

                if (rep.color_mapping == ColorMapping::Property) {
                    // The list of per atom fields IS the system's attribute table under atom/.
                    // Queried here rather than cached anywhere, so it cannot disagree with the data.
                    md_attribute_id_t prop_ids[64];
                    size_t num_props = MIN(atom_property_query(prop_ids, ARRAY_SIZE(prop_ids), state->mold.sys), ARRAY_SIZE(prop_ids));

                    const md_attributes_t& attributes = state->mold.sys.attributes;
                    const md_attribute_t* selected_prop = md_attributes_get(&attributes, rep.atomic_property.key);

                    // A key the table no longer holds - a reload which dropped that field - falls
                    // back to the first available rather than leaving the representation blank.
                    if (!selected_prop && num_props > 0) {
                        atom_property_select(&rep.atomic_property, prop_ids[0], state->mold.sys);
                        selected_prop = md_attributes_get(&attributes, rep.atomic_property.key);
                        update_rep = true;
                    }

                    if (num_props > 0 && selected_prop) {
                        if (ImGui::BeginCombo("property", atom_property_label(selected_prop).ptr)) {
                            for (size_t i = 0; i < num_props; ++i) {
                                const md_attribute_t* attr = md_attributes_get(&attributes, prop_ids[i]);
                                if (!attr) continue;
                                bool selected = prop_ids[i] == rep.atomic_property.key;
                                if (ImGui::Selectable(atom_property_label(attr).ptr, selected)) {
                                    atom_property_select(&rep.atomic_property, prop_ids[i], state->mold.sys);
                                    update_rep = true;
                                }
                            }
                            ImGui::EndCombo();
                        }

                        const int num_variants = atom_property_variant_count(selected_prop);
                        if (num_variants > 1) {
                            int idx = rep.atomic_property.variant_idx + 1;
                            const int min = 1;
                            const int max = num_variants;
                            if (ImGui::SliderInt("index", &idx, min, max)) {
                                update_rep = true;
                            }
                            rep.atomic_property.variant_idx = CLAMP(idx - 1, 0, num_variants - 1);
                        }
                        
                        if (ImPlot::ColormapButton(ImPlot::GetColormapName(rep.atomic_property.colormap), ImVec2(inner_item_width,0), rep.atomic_property.colormap)) {
                            ImGui::OpenPopup("Color Map Selector");
                        }

                        // The data's own span, taken when the field was selected. Not recomputed
                        // here: it is what the user's range is measured against, and a value which
                        // moved underneath the slider would move the slider.
						const float value_pad = MAX(fabsf(rep.atomic_property.value_min), fabsf(rep.atomic_property.value_max));
                        const float value_min = rep.atomic_property.value_min - value_pad;
                        const float value_max = rep.atomic_property.value_max + value_pad;

						// Otherwise, we allow independent scaling of the min and max values
                        // Scale a bit outside of the default range
                        update_rep |= ImGui::RangeSliderFloat("min / max", &rep.atomic_property.range_beg, &rep.atomic_property.range_end, value_min, value_max);

                        if (ImGui::BeginPopup("Color Map Selector")) {
                            for (int map = 0; map < ImPlot::GetColormapCount(); ++map) {
                                if (ImPlot::ColormapButton(ImPlot::GetColormapName(map), ImVec2(inner_item_width,0), map)) {
                                    rep.atomic_property.colormap = map;
                                    update_rep = true;
                                    ImGui::CloseCurrentPopup();
                                }
                            }
                            ImGui::EndPopup();
                        }
                    } else {
                        ImGui::Text("no properties available");
                    }
                }
                if (rep.filt_is_dynamic || rep.color_mapping == ColorMapping::Property) {
                    if (advanced) {
                        update_rep |= ImGui::Checkbox("auto-update", &rep.dynamic_evaluation);
                        if (!rep.dynamic_evaluation) {
                            ImGui::SameLine();
                            if (ImGui::Button("update")) {
                                rep.filt_is_dirty = true;
                                update_rep = true;
                            }
                        }
                    }
                } else {
                    rep.dynamic_evaluation = false;
                }

                if (rep.color_mapping == ColorMapping::Uniform) {
                    update_rep |= ImGui::ColorEdit3("##atom_base_color", rep.base_color.elem);
                }

                if (advanced) {
                    if (rep.type == RepresentationType::Licorice || rep.type == RepresentationType::BallAndStick) {
                        // Draw options for how bonds should be colored
					    update_rep |= ImGui::Combo("bond color", (int*)(&rep.bond_color), bond_color_mode_str, IM_ARRAYSIZE(bond_color_mode_str));
                        if (rep.bond_color == BondColorMode::SmoothAtom) {
                            ImGui::SliderFloat("sharpness", &rep.bond_sharpness, 0.0f, 1.0f);
                        } else if (rep.bond_color == BondColorMode::Uniform) {
                            ImGui::ColorEdit3("##bond_base_color", rep.bond_base_color.elem);
					    }
                    }
                }
            }

            if (advanced && representation_uses_atom_colors(rep)) {
                if (rep.type != RepresentationType::ElectronicStructure) {
                    ImGui::Spacing();
                    ImGui::TextColored(sub_group_color, "Geometric Scaling");
                    switch (rep.type) {
                    case RepresentationType::SpaceFill:
                        update_rep |= ImGui::SliderFloat("radius scale", &rep.scale[0], 0.1f, 4.f);
                        break;
                    case RepresentationType::Licorice:
                        update_rep |= ImGui::SliderFloat("radius scale", &rep.scale[0], 0.1f, 4.0f);
                        break;
                    case RepresentationType::BallAndStick:
                        update_rep |= ImGui::SliderFloat("ball scale",   &rep.scale[0], 0.1f, 4.f);
                        update_rep |= ImGui::SliderFloat("bond scale",   &rep.scale[1], 0.1f, 4.f);
                        break;
                    case RepresentationType::Ribbons:
                        update_rep |= ImGui::SliderFloat("width",        &rep.scale[0], 0.1f, 3.f);
                        update_rep |= ImGui::SliderFloat("thickness",    &rep.scale[1], 0.1f, 3.f);
                        break;
                    case RepresentationType::Cartoon:
                        update_rep |= ImGui::SliderFloat("coil",   &rep.scale[0], 0.1f, 3.f);
                        update_rep |= ImGui::SliderFloat("sheet",  &rep.scale[1], 0.1f, 3.f);
                        update_rep |= ImGui::SliderFloat("helix",  &rep.scale[2], 0.1f, 3.f);
                        break;
                    default:
                        break;
                    }
                }
                if (rep.color_mapping == ColorMapping::SecondaryStructure) {
                    ImGui::Spacing();
                    ImGui::TextColored(sub_group_color, "Secondary Structure Color");
                    update_rep |= ImGui::ColorEdit3("unknown", rep.secondary_structure.color_unknown.elem);
                    update_rep |= ImGui::ColorEdit3("coil",    rep.secondary_structure.color_coil.elem);
                    update_rep |= ImGui::ColorEdit3("helix",   rep.secondary_structure.color_helix.elem);
                    update_rep |= ImGui::ColorEdit3("sheet",   rep.secondary_structure.color_sheet.elem);
                }
                ImGui::Spacing();

                ImGui::TextColored(sub_group_color, "Post-Processing");
                update_rep |= ImGui::ColorEdit3("tint color", rep.tint_color.elem, ImGuiColorEditFlags_PickerHueWheel);
                update_rep |= ImGui::SliderFloat("tint scale", &rep.tint_scale, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                update_rep |= ImGui::SliderFloat("saturation", &rep.saturation, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            }

            ImGui::PopItemWidth();
            ImGui::Spacing();
            ImGui::TreePop();
        }

        ImGui::PopID();

        if (update_rep) {
            flag_representation_as_dirty(&rep);
        }
    }

    ImGui::End();
}



static void draw_async_task_window(ApplicationState* data) {
    constexpr float WIDTH = 300.f;
    constexpr float MARGIN = 10.f;

    task_system::ID tasks[256];
    size_t num_tasks = task_system::pool_running_tasks(tasks, ARRAY_SIZE(tasks));
    bool any_task_label_visible = false;
    for (size_t i = 0; i < num_tasks; i++) {
        str_t label = task_system::task_label(tasks[i]);
        if (!label || label[0] == '\0' || (label[0] == '#' && label[1] == '#')) continue;
        any_task_label_visible = true;
    }
    
    if (any_task_label_visible) {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos + ImVec2(data->app.window.width - WIDTH - MARGIN,
                                                       ImGui::GetCurrentContext()->FontSize + ImGui::GetStyle().FramePadding.y * 2.f + MARGIN));
        ImGui::SetNextWindowSize(ImVec2(WIDTH, 0));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.5f));
        ImGui::Begin("##Async Info", 0,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);

        const float pad = 3.0f;
        const float size = ImGui::GetFontSize() + pad * 2;

        char buf[64];
        for (size_t i = 0; i < MIN(num_tasks, 8); i++) {
            const auto id = tasks[i];
            str_t label = task_system::task_label(id);
            float fract = task_system::task_fraction_complete(id);

            if (!label || label[0] == '\0' || (label[0] == '#' && label[1] == '#')) continue;

            snprintf(buf, sizeof(buf), "%.*s %.1f%%", (int)label.len, label.ptr, fract * 100.f);
            ImGui::ProgressBar(fract, ImVec2(ImGui::GetContentRegionAvail().x - (size + pad),0), buf);
            ImGui::SameLine();
            if (ImGui::DeleteButton((const char*)ICON_FA_XMARK, ImVec2(size, size))) {
                task_system::task_interrupt(id);
                if (id == data->tasks.evaluate_full) {
                    md_script_eval_interrupt(data->script.full_eval);
                }
                else if(id == data->tasks.evaluate_filt) {
                    md_script_eval_interrupt(data->script.filt_eval);
                }
            }
        }

        ImGui::End();
        ImGui::PopStyleColor();
    }
}

struct TimelineArgs {
    const char* lbl;
    uint32_t col;
    int plot_height;

    struct {
        int count;
        int dim_y;

        const float* x;
        const float* y;
        const float* y_mean;
        const float* y_var;
        const float* y_min;
        const float* y_max;

        float min_y;
        float max_y;
        str_t unit;
    } values;

    struct {
        double* beg;
        double* end;
        double  min;
        double  max;
    } view_range;

    struct {
        bool* is_dragging;
        bool* is_selecting;
    } input;

    struct {
        bool show;
        bool enabled;

        double* beg;
        double* end;

        double min;
        double max;
    } filter;

    struct {
        bool enabled;
        double min;
        double max;
    } value_filter;

    double* time;
};

struct TimePayload {
    const TimelineArgs* args;
    int y_idx;
};

static ImPlotPoint get_time_point(int index, void* user_data) {
    const TimePayload* payload = (const TimePayload*)user_data;
    const TimelineArgs* args = payload->args;
    return ImPlotPoint(args->values.x[index], args->values.y[index * args->values.dim_y + payload->y_idx]);
}

bool draw_property_timeline(const ApplicationState& data, const TimelineArgs& args) {
    const ImPlotAxisFlags axis_flags = ImPlotAxisFlags_NoSideSwitch | ImPlotAxisFlags_NoHighlight;
    const ImPlotAxisFlags axis_flags_x = axis_flags;
    const ImPlotAxisFlags axis_flags_y = axis_flags | ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_NoLabel |ImPlotAxisFlags_NoTickLabels;
    
    const ImPlotFlags flags = ImPlotFlags_NoBoxSelect | ImPlotFlags_NoFrame;
    
    const float pad_x = ImPlot::GetStyle().PlotPadding.x;
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(pad_x, 0));
    defer { ImPlot::PopStyleVar(1); };

    if (ImPlot::BeginPlot("##Timeline", ImVec2(-1,args.plot_height), flags)) {
        ImPlot::SetupAxisLinks(ImAxis_X1, args.view_range.beg, args.view_range.end);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, args.view_range.min, args.view_range.max);
        ImPlot::SetupAxes(0, 0, axis_flags_x, axis_flags_y);
        ImPlot::SetupFinish();

        bool active = ImGui::IsItemActive();
        bool print_timeline_tooltip = false;

        if (args.value_filter.enabled) {
            float* y_vals = (float*)md_alloc(frame_alloc, args.values.count * sizeof(float));
            for (int i = 0; i < args.values.count; ++i) {
                float val = args.values.y[i];
                y_vals[i] = (args.value_filter.min < val && val < args.value_filter.max) ? args.values.max_y : -FLT_MAX;
            }

            const ImVec4 filter_frame_color = ImVec4(1, 1, 1, 1);
            ImPlot::SetNextFillStyle(filter_frame_color, 0.15f);
            ImPlot::PlotShaded("##value_filter", args.values.x, y_vals, args.values.count, -FLT_MAX);
        }

        if (args.filter.show) {
            if (!args.filter.enabled) ImGui::PushDisabled();
            ImPlot::DragRangeX("Time Filter", args.filter.beg, args.filter.end, args.filter.min, args.filter.max);
            if (!args.filter.enabled) ImGui::PopDisabled();
            *args.filter.beg = CLAMP(*args.filter.beg, args.filter.min, args.filter.max);
            *args.filter.end = CLAMP(*args.filter.end, args.filter.min, args.filter.max);
        }

        ImVec4 line_col = ImGui::ColorConvertU32ToFloat4(args.col);
        ImPlot::SetNextLineStyle(line_col);

        if (args.values.count > 0) {
            ASSERT(args.values.x);
            ASSERT(args.values.y);
            if (args.values.y_var) {
                ASSERT(args.values.y_min);
                ASSERT(args.values.y_max);
                char lbl[32];

                snprintf(lbl, sizeof(lbl), "%s", args.lbl);
                TimePayload payload = {
                    .args = &args,
                    .y_idx = 0,
                };
                for (int i = 0; i < args.values.dim_y; ++i) {
                    payload.y_idx = i;
                    ImPlot::PlotLineG(lbl, get_time_point, &payload, args.values.count);
                }

                ImPlot::SetNextLineStyle(line_col);
                snprintf(lbl, sizeof(lbl), "%s (mean)", args.lbl);
                ImPlot::PlotLine(lbl, args.values.x, args.values.y_mean, args.values.count);

                ImPlot::SetNextFillStyle(line_col, 0.4f);
                snprintf(lbl, sizeof(lbl), "%s (var)", args.lbl);
                ImPlot::PlotShadedG(lbl,
                    [](int idx, void* payload) -> ImPlotPoint {
                        TimelineArgs* args = (TimelineArgs*)payload;
                        return ImPlotPoint(args->values.x[idx], args->values.y_mean[idx] - args->values.y_var[idx]);
                    },
                    (void*)&args,
                    [](int idx, void* payload) -> ImPlotPoint {
                        TimelineArgs* args = (TimelineArgs*)payload;
                        return ImPlotPoint(args->values.x[idx], args->values.y_mean[idx] + args->values.y_var[idx]);
                    },
                    (void*)&args,
                    args.values.count
                );

                ImPlot::SetNextFillStyle(line_col, 0.2f);
                snprintf(lbl, sizeof(lbl), "%s (min,max)", args.lbl);
                ImPlot::PlotShadedG(lbl,
                    [](int idx, void* payload) -> ImPlotPoint {
                        TimelineArgs* args = (TimelineArgs*)payload;
                        return ImPlotPoint(args->values.x[idx], args->values.y_min[idx]);
                    },
                    (void*)&args,
                        [](int idx, void* payload) -> ImPlotPoint {
                        TimelineArgs* args = (TimelineArgs*)payload;
                        return ImPlotPoint(args->values.x[idx], args->values.y_max[idx]);
                    },
                    (void*)&args,
                    args.values.count
                );
            } 
            else {
                ImPlot::PlotLine(args.lbl, args.values.x, args.values.y, args.values.count);
            }
        }
        
        if (*args.input.is_dragging) {
            *args.time = ImPlot::GetPlotMousePos().x;
        }
        else if (*args.input.is_selecting) {
            *args.filter.end = MAX(ImPlot::GetPlotMousePos().x, *args.filter.beg);
        }
        else if (ImPlot::IsPlotHovered()) {
            if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {           
                if (ImGui::GetIO().KeyMods == ImGuiMod_Shift) {
                    if (args.filter.show && args.filter.enabled) {
                        *args.input.is_selecting = true;
                        *args.filter.beg = ImPlot::GetPlotMousePos().x;
                    }
                } else {
                    *args.input.is_dragging = true;
                }
            }
        }

        if (ImPlot::IsPlotHovered()) {
            print_timeline_tooltip = true;
        }
        
        if (ImPlot::DragLineX(0, args.time, ImVec4(1,1,0,1))) {
            *args.time = CLAMP(*args.time, args.filter.min, args.filter.max);
        }
        if (ImGui::IsItemHovered()) {
            print_timeline_tooltip = true;
        }

        if (print_timeline_tooltip) {
            ImPlotPoint plot_pos = ImPlot::GetPlotMousePos();
            ImVec2 screen_pos = ImPlot::PlotToPixels(plot_pos);
            ImVec2 p0 = {screen_pos.x, ImPlot::GetPlotPos().y};
            ImVec2 p1 = {screen_pos.x, ImPlot::GetPlotPos().y + ImPlot::GetPlotSize().y};
            ImPlot::PushPlotClipRect();
            ImPlot::GetPlotDrawList()->AddLine(p0, p1, IM_COL32(255, 255, 255, 120));
            ImPlot::PopPlotClipRect();

            char buf[128] = "";
            int len = 0;

            double time = plot_pos.x;
            int frame_idx = CLAMP((int)(time_to_frame(time, data.timeline.x_values) + 0.5), 0, (int)md_array_size(data.timeline.x_values)-1);
            len += snprintf(buf + len, MAX(0, (int)sizeof(buf) - len), "time: %.2f", time);

            md_unit_t time_unit = data.timeline.time_unit;
            if (!md_unit_is_none(time_unit)) {
                char unit_buf[32];
                md_unit_print(unit_buf, sizeof(unit_buf), time_unit);
                len += snprintf(buf + len, MAX(0, (int)sizeof(buf) - len), " (%s)", unit_buf);
            }

            if (0 <= frame_idx && frame_idx < args.values.count) {
                const char* value_lbl = args.values.y_var ? "mean" : "value";
                if (args.values.y) {
                    len += snprintf(buf + len, MAX(0, (int)sizeof(buf) - len), ", %s: %.2f", value_lbl, args.values.y[frame_idx]);
                }
                if (args.values.y_var) {
                    ASSERT(args.values.y_min);
                    ASSERT(args.values.y_max);
                    len += snprintf(buf + len, MAX(0, (int)sizeof(buf) - len), ", var: %.2f, min: %.2f, max: %.2f",
                        args.values.y_var[frame_idx],
                        args.values.y_min[frame_idx],
                        args.values.y_max[frame_idx]);
                }

                if (!str_empty(args.values.unit)) {
                    len += snprintf(buf + len, MAX(0, (int)sizeof(buf) - len), " (%.*s)", (int)args.values.unit.len, args.values.unit.ptr);
                }
            }
            ImGui::SetTooltip("%.*s", len, buf);
        }

        ImPlot::EndPlot();
    }

    return true;
}

static double distance_to_linesegment(ImPlotPoint p0, ImPlotPoint p1, ImPlotPoint p) {
    double vx = p1.x - p0.x;
    double vy = p1.y - p0.y;

    double ux = p.x - p0.x;
    double uy = p.y - p0.y;

    double d_uv = ux*vx + uy*vy;
    double d_vv = vx*vx + vy*vy;

    if (d_vv < 1.0e-7) {
        double d_uu = ux*ux + uy*uy;
        return sqrt(d_uu);
    }

    double t = d_uv / d_vv;

    if (t < 0.0) {
        double d_uu = ux*ux + uy*uy;
        return sqrt(d_uu);
    } else if (t > 1.0) {
        double wx = p.x - p1.x;
        double wy = p.y - p1.y;
        double d_ww = wx*wx + wy*wy;
        return sqrt(d_ww);
    } else {
        double wx = p.x - (p0.x + vx * t);
        double wy = p.y - (p0.y + vy * t);
        double d_ww = wx*wx + wy*wy;
        return sqrt(d_ww);
    }
}

// #plots
//
// The Timelines and Distributions windows. Both draw series named by attribute path (see
// plot_series.h) into subplots that own them, and share the lists, the drag and drop and the
// styling of a legend entry.

// The colour of population member k of an entry drawn with a population of population_size
static ImVec4 plot_series_member_color(const PlotSeries& s, int k, int population_size) {
    ImVec4 color = s.color;
    if (s.use_colormap && population_size > 1) {
        if (ImPlot::ColormapQualitative(s.colormap)) {
            color = ImPlot::GetColormapColor(k, s.colormap);
        } else {
            color = ImPlot::SampleColormap((float)k / (float)(population_size - 1), s.colormap);
        }
        color.w *= s.colormap_alpha;
    }
    return color;
}

static ImVec4 plot_highlight(ImVec4 color) {
    const float scl = 1.5f;
    return ImVec4(ImSaturate(color.x * scl), ImSaturate(color.y * scl), ImSaturate(color.z * scl), color.w);
}

// The part of a legend entry's popup both windows share: colour or colormap, and which members of
// a population are drawn. A member hovered in the popup is written to hovered_pop_idx.
static void plot_series_style_popup(ApplicationState* data, PlotSeries& s, int dim, const char* script_ident, const md_script_vis_payload_o* vis_payload, int* hovered_pop_idx) {
    if (dim > 1) {
        const char* color_type_labels[] = {"Solid", "Colormap"};
        int color_type = s.use_colormap ? 1 : 0;
        if (ImGui::Combo("Color Type", &color_type, color_type_labels, IM_ARRAYSIZE(color_type_labels))) {
            s.use_colormap = color_type == 1;
        }
    }
    if (s.use_colormap && dim > 1) {
        ImPlot::ColormapSelection("##Colormap", &s.colormap);
        ImGui::SliderFloat("Alpha", &s.colormap_alpha, 0.0f, 1.0f);
    } else {
        ImGui::ColorEdit4("Color", &s.color.x);
    }

    if (dim > 1) {
        ImGui::Separator();
        if (ImGui::Button("Set All")) {
            s.population_mask.set();
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear All")) {
            s.population_mask.reset();
        }

        const float sz = ImGui::GetFontSize() * 1.5f;
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
        for (int k = 0; k < MIN(dim, PLOT_MAX_POPULATION); ++k) {
            char lbl[32];
            snprintf(lbl, sizeof(lbl), "%d", k+1);
            if (ImGui::Selectable(lbl, s.population_mask.test(k), ImGuiSelectableFlags_DontClosePopups, ImVec2(sz, sz))) {
                s.population_mask.flip(k);
            }
            if (ImGui::IsItemHovered()) {
                if (script_ident[0] != '\0') {
                    script_visualize_payload(data, vis_payload, k, MD_SCRIPT_VISUALIZE_ATOMS | MD_SCRIPT_VISUALIZE_GEOMETRY);
                    script_set_hovered_property(data, str_from_cstr(script_ident), k);
                }
                *hovered_pop_idx = k;
            }
            if (!k || ((k+1) % 10)) {
                ImGui::SameLine();
            }
        }
        ImGui::PopStyleVar();
    }
}

// One entry in a list of series: a click toggles it in the first subplot, a drag puts it in any
static void plot_series_list_item(ApplicationState* data, const char* dnd_type, PlotSubplot& first, const SeriesKey& key) {
    char label[96];
    series_label(label, sizeof(label), data, key);
    const ImVec4 color = series_default_color(data, key);
    const int idx = plot_find_series(first, key);

    ImGui::PushID(key.path);
    ImGui::PushID((int)key.variant * SeriesSource_Count + (int)key.source);
    ImPlot::ItemIcon(color);
    ImGui::SameLine();
    if (ImGui::Selectable(label, idx != -1, ImGuiSelectableFlags_DontClosePopups)) {
        if (idx != -1) {
            plot_remove_series(first, idx);
        } else {
            plot_add_series(data, first, key);
        }
    }
    if (ImGui::IsItemHovered()) {
        const str_t ident = series_script_ident(key);
        if (!str_empty(ident)) {
            const md_script_vis_payload_o* vis = data->script.eval_ir ? md_script_ir_property_vis_payload(data->script.eval_ir, ident) : nullptr;
            script_visualize_payload(data, vis, -1, MD_SCRIPT_VISUALIZE_ATOMS | MD_SCRIPT_VISUALIZE_GEOMETRY);
            script_set_hovered_property(data, ident);
        } else {
            ImGui::SetTooltip("%s\nClick to show in the first subplot, drag into any", key.path);
        }
    }
    if (ImGui::BeginDragDropSource()) {
        series_set_drag_payload(dnd_type, key, -1, label, color);
        ImGui::EndDragDropSource();
    }
    ImGui::PopID();
    ImGui::PopID();
}

// The series loaded along the run, one submenu per file
static void plot_system_series_menu(ApplicationState* data, const char* dnd_type, PlotSubplot& first) {
    str_t groups[64];
    const size_t num_groups = MIN(system_series_groups(groups, ARRAY_SIZE(groups), data), ARRAY_SIZE(groups));
    size_t num_listed = 0;
    for (size_t g = 0; g < num_groups; ++g) {
        md_temp_scope_t temp = md_temp_begin();
        const size_t num = system_series_members(nullptr, 0, data, groups[g]);
        str_t* paths = md_temp_alloc_array(temp, str_t, num + 1);
        system_series_members(paths, num, data, groups[g]);
        if (num > 0) {
            char group_label[256];
            system_series_group_label(group_label, sizeof(group_label), data, groups[g]);
            if (ImGui::BeginMenu(group_label)) {
                for (size_t i = 0; i < num; ++i) {
                    plot_series_list_item(data, dnd_type, first, series_key(SeriesSource_System, paths[i]));
                }
                ImGui::EndMenu();
            }
            num_listed += 1;
        }
        md_temp_end(temp);
    }
    if (num_listed == 0) {
        ImGui::TextDisabled("Nothing loaded");
    }
}

// The members of a script property's population, if it has one: the extent of its value axis
static size_t script_property_population(const ApplicationState* data, const SeriesKey& key) {
    const md_attributes_t* table = series_table(data, key.source);
    const md_attribute_t* attr = table ? md_attributes_find(table, str_from_cstr(key.path)) : nullptr;
    if (!attr || attr->format.rank < 1 || attr->format.shape[0] == 0) return 0;
    return md_attribute_element_count(&attr->format) / attr->format.shape[0];
}

// #timeline

// What a getter is handed: the resolved series, and which member of its population
struct TemporalGetterPayload {
    const SeriesTemporalView* view;
    int k;
};

static ImPlotPoint temporal_getter_line(int i, void* payload) {
    const TemporalGetterPayload* p = (const TemporalGetterPayload*)payload;
    return series_temporal_point(*p->view, i, p->k);
}

static ImPlotPoint temporal_getter_band_lo(int i, void* payload) {
    const TemporalGetterPayload* p = (const TemporalGetterPayload*)payload;
    double lo, hi;
    series_temporal_band(*p->view, i, &lo, &hi);
    return ImPlotPoint(p->view->x[i], lo);
}

static ImPlotPoint temporal_getter_band_hi(int i, void* payload) {
    const TemporalGetterPayload* p = (const TemporalGetterPayload*)payload;
    double lo, hi;
    series_temporal_band(*p->view, i, &lo, &hi);
    return ImPlotPoint(p->view->x[i], hi);
}

static void draw_timeline_properties_menu(ApplicationState* data) {
    PlotSubplot& first = data->timeline.subplots[0];

    ImGui::SeparatorText("Script");
    int num_listed = 0;
    series_for_each_script_property(data, SeriesSource_Script, MD_SCRIPT_PROPERTY_FLAG_TEMPORAL, [&](const SeriesKey& key) {
        plot_series_list_item(data, TIMELINE_SERIES_DND, first, key);
        num_listed += 1;

        // A property with a population also has its summary over it
        char mean_path[SERIES_PATH_CAP + 8];
        const int len = snprintf(mean_path, sizeof(mean_path), "%s/mean", key.path);
        const md_attributes_t* table = series_table(data, key.source);
        if (table && len > 0 && md_attributes_find(table, str_t{mean_path, (size_t)len})) {
            ImGui::Indent();
            const SeriesVariant variants[] = { SeriesVariant_Mean, SeriesVariant_Sigma, SeriesVariant_Extent };
            for (SeriesVariant v : variants) {
                SeriesKey summary = key;
                summary.variant = v;
                plot_series_list_item(data, TIMELINE_SERIES_DND, first, summary);
            }
            ImGui::Unindent();
        }
    });
    if (num_listed == 0) {
        ImGui::TextDisabled("No temporal properties, define and evaluate them in the script editor");
    }

    ImGui::SeparatorText("Loaded along the run");
    plot_system_series_menu(data, TIMELINE_SERIES_DND, first);
}

static void draw_timeline_window(ApplicationState* data) {
    ASSERT(data);
    ImGui::SetNextWindowSize(ImVec2(600, 300), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Timelines", &data->timeline.show_window, ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_MenuBar)) {
        int& num_subplots = data->timeline.num_subplots;
        num_subplots = CLAMP(num_subplots, 1, PLOT_MAX_SUBPLOTS);

        double pre_filter_min = data->timeline.filter.beg_frame;
        double pre_filter_max = data->timeline.filter.end_frame;

        const float* x_values   = data->timeline.x_values;
        const int num_x_values  = (int)md_array_size(data->timeline.x_values);
        const float min_x_value = num_x_values > 0 ? x_values[0] : 0.0f;
        const float max_x_value = num_x_values > 0 ? x_values[num_x_values - 1] : 1.0f;

        ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(ImPlot::GetStyle().PlotPadding.x, 2));
        defer { ImPlot::PopStyleVar(); };

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Properties")) {
                draw_timeline_properties_menu(data);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Filter")) {
                ImGui::Checkbox("Enabled", &data->timeline.filter.enabled);
                if (data->timeline.filter.enabled) {
                    ImGui::Checkbox("Temporal Window", &data->timeline.filter.temporal_window.enabled);
                    if (data->timeline.filter.temporal_window.enabled) {
                        const double extent_min = 1.0;
                        const double extent_max = num_x_values / 2.0;
                        ImGui::SliderScalar("Extent (frames)", ImGuiDataType_Double, &data->timeline.filter.temporal_window.extent_in_frames, &extent_min, &extent_max, "%1.0f");
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Subplots")) {
                ImGui::SliderInt("Num Subplots", &num_subplots, 1, PLOT_MAX_SUBPLOTS);
                if (ImGui::Button("Add Subplot")) {
                    num_subplots = CLAMP(num_subplots + 1, 1, PLOT_MAX_SUBPLOTS);
                }
                if (ImGui::Button("Clear All")) {
                    plot_clear(data->timeline.subplots, PLOT_MAX_SUBPLOTS);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(KEY_PLAY_PAUSE, false)) {
            data->animation.mode = data->animation.mode == PlaybackMode::Playing ? PlaybackMode::Stopped : PlaybackMode::Playing;
        }

        if (num_x_values > 0) {
            ImPlotInputMap old_map = ImPlot::GetInputMap();

            static bool is_dragging  = false;
            static bool is_selecting = false;

            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                is_dragging = false;
                is_selecting = false;
            }
            if (!ImGui::IsKeyDown(ImGuiKey_LeftCtrl)) {
                is_dragging = false;
            }
            if (!ImGui::IsKeyDown(ImGuiKey_LeftShift)) {
                is_selecting = false;
            }

            // Create a temporary 'time' representation of the filters min and max value
            // The visualization uses time units while we store 'frame' units
            double filter_beg = frame_to_time(data->timeline.filter.beg_frame, *data);
            double filter_end = frame_to_time(data->timeline.filter.end_frame, *data);
            double time = frame_to_time(data->animation.frame, *data);

            ImPlot::BeginSubplots("##Temporal", num_subplots, 1, ImVec2(-1,-1));

            const ImPlotFlags plot_flags = ImPlotFlags_NoBoxSelect | ImPlotFlags_NoFrame;
            const ImPlotAxisFlags axis_flags   = ImPlotAxisFlags_NoSideSwitch;
            const ImPlotAxisFlags axis_flags_y = axis_flags | ImPlotAxisFlags_Opposite | ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit;

            char x_label[64] = "Frame";
            char x_unit_str[32] = "";
            md_unit_t x_unit = data->timeline.time_unit;
            if (!md_unit_is_none(x_unit)) {
                md_unit_print(x_unit_str, sizeof(x_unit_str), x_unit);
                snprintf(x_label, sizeof(x_label), "Time (%s)", x_unit_str);
            }

            for (int i = 0; i < num_subplots; ++i) {
                PlotSubplot& sp = data->timeline.subplots[i];

                // Every entry is resolved once per plot, and everything below reads the views
                SeriesTemporalView views[PLOT_MAX_SERIES_PER_SUBPLOT];
                bool resolved[PLOT_MAX_SERIES_PER_SUBPLOT] = {};
                for (int j = 0; j < sp.count; ++j) {
                    resolved[j] = series_resolve_temporal(&views[j], data, sp.series[j].key);
                }

                int remove_idx = -1;

                if (ImPlot::BeginPlot("", ImVec2(), plot_flags)) {
                    ImPlot::SetupAxisLinks(ImAxis_X1, &data->timeline.view_range.beg_x, &data->timeline.view_range.end_x);
                    ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, min_x_value, max_x_value);

                    ImPlotAxisFlags axis_flags_x = axis_flags | ImPlotAxisFlags_NoLabel;
                    if (i < num_subplots - 1) {
                        // Only show label and ticklabels for the last plot, since they are all synced on x-axis
                        axis_flags_x |= ImPlotAxisFlags_NoTickLabels;
                    }

                    // When every series in the subplot is shown in the same unit, that unit labels the y axis
                    char y_unit_str[32] = "";
                    for (int j = 0; j < sp.count; ++j) {
                        if (!resolved[j]) continue;
                        if (y_unit_str[0] == '\0') {
                            str_copy_to_char_buf(y_unit_str, sizeof(y_unit_str), str_from_cstr(views[j].unit_str));
                        } else if (strcmp(y_unit_str, views[j].unit_str) != 0) {
                            y_unit_str[0] = '\0';
                            break;
                        }
                    }

                    char y_label[64] = "";
                    if (y_unit_str[0] != '\0') {
                        snprintf(y_label, sizeof(y_label), "(%s)", y_unit_str);
                    }

                    ImPlot::SetupAxes(x_label, y_label, axis_flags_x, axis_flags_y);
                    ImPlot::SetupFinish();

                    if (data->timeline.filter.enabled) {
                        bool disabled = data->timeline.filter.temporal_window.enabled;
                        ImPlotDragRangeFlags flags = ImPlotDragToolFlags_NoFit;
                        if (i < num_subplots - 1) {
                            flags |= ImPlotDragRangeFlags_NoBar;
                        }
                        if (disabled) ImGui::PushDisabled();
                        ImPlot::DragRangeX("Time Filter", &filter_beg, &filter_end, min_x_value, max_x_value, flags);
                        if (disabled) ImGui::PopDisabled();
                    }

                    // The entry, and the member of its population, under the mouse
                    int  hovered_idx      = -1;
                    int  hovered_pop_idx  = -1;
                    char hovered_label[128] = "";

                    bool print_timeline_tooltip = false;

                    if (ImPlot::IsPlotHovered()) {
                        md_bitfield_clear(&data->selection.highlight_mask);
                        script_set_hovered_property(data,  STR_LIT(""));

                        print_timeline_tooltip = true;
                        const ImPlotPoint mouse_pos = ImPlot::GetPlotMousePos();
                        const ImVec2 mouse_coord = ImPlot::PlotToPixels(mouse_pos);
                        const float max_rad = 20; // 20 pixels
                        const float area_dist = max_rad * 0.2f;

                        float min_dist = max_rad;

                        for (int j = 0; j < sp.count; ++j) {
                            if (!resolved[j]) continue;
                            const PlotSeries& s = sp.series[j];
                            const SeriesTemporalView& v = views[j];

                            ImPlotItem* item = ImPlot::GetItem(v.plot_id);
                            if (!item || !item->Show) {
                                continue;
                            }

                            // Each series has an axis of its own, so the samples around the mouse are its own
                            const int n = v.num_samples;
                            const double fi = series_temporal_index_at(v, mouse_pos.x);
                            const int fn = CLAMP((int)(fi + 0.5), 0, n - 1);  // Nearest index
                            const int f[4] = {
                                CLAMP((int)fi - 1, 0, n - 1),
                                CLAMP((int)fi,     0, n - 1),
                                CLAMP((int)fi + 1, 0, n - 1),
                                CLAMP((int)fi + 2, 0, n - 1),
                            };

                            const int dim = CLAMP(v.dim, 1, PLOT_MAX_POPULATION);
                            for (int k = 0; k < dim; ++k) {
                                if (dim > 1 && !s.population_mask.test(k)) {
                                    continue;
                                }
                                TemporalGetterPayload payload = { &v, k };
                                double d = DBL_MAX;

                                if (v.band) {
                                    const ImVec2 p_min[4] = {
                                        ImPlot::PlotToPixels(temporal_getter_band_lo(f[0], &payload)),
                                        ImPlot::PlotToPixels(temporal_getter_band_lo(f[1], &payload)),
                                        ImPlot::PlotToPixels(temporal_getter_band_lo(f[2], &payload)),
                                        ImPlot::PlotToPixels(temporal_getter_band_lo(f[3], &payload)),
                                    };
                                    const ImVec2 p_max[4] = {
                                        ImPlot::PlotToPixels(temporal_getter_band_hi(f[0], &payload)),
                                        ImPlot::PlotToPixels(temporal_getter_band_hi(f[1], &payload)),
                                        ImPlot::PlotToPixels(temporal_getter_band_hi(f[2], &payload)),
                                        ImPlot::PlotToPixels(temporal_getter_band_hi(f[3], &payload)),
                                    };
                                    // Each segment forms a trapezoid with its left and right sides parallel to the y axis:
                                    // clamp the mouse to it and measure the distance to the clamped point
                                    for (int l = 0; l < 2; ++l) {
                                        const float x_min = MIN(p_min[l].x, p_min[l+1].x);
                                        const float x_max = MAX(p_min[l].x, p_min[l+1].x);
                                        const float t = (x_max > x_min) ? CLAMP((mouse_coord.x - x_min) / (x_max - x_min), 0.0f, 1.0f) : 0.0f;
                                        const float y[2] = {
                                            lerp(p_min[l].y, p_min[l+1].y, t),
                                            lerp(p_max[l].y, p_max[l+1].y, t)
                                        };
                                        const ImVec2 p = {
                                            CLAMP(mouse_coord.x, x_min, x_max),
                                            CLAMP(mouse_coord.y, MIN(y[0], y[1]), MAX(y[0], y[1]))
                                        };
                                        d = MIN(d, sqrt(ImLengthSqr(mouse_coord - p)));
                                    }
                                    d += area_dist;
                                } else if (s.plot_type == PlotType_Scatter) {
                                    ImVec2 p = ImPlot::PlotToPixels(temporal_getter_line(fn, &payload));
                                    d = sqrt(ImLengthSqr(mouse_coord - p));
                                } else {
                                    // Distance to the segments before, at and after the mouse:
                                    // the current one alone is not enough near a vertex
                                    ImVec2 p[4] = {
                                        ImPlot::PlotToPixels(temporal_getter_line(f[0], &payload)),
                                        ImPlot::PlotToPixels(temporal_getter_line(f[1], &payload)),
                                        ImPlot::PlotToPixels(temporal_getter_line(f[2], &payload)),
                                        ImPlot::PlotToPixels(temporal_getter_line(f[3], &payload)),
                                    };
                                    d = distance_to_linesegment(p[0], p[1], mouse_coord);
                                    d = MIN(distance_to_linesegment(p[1], p[2], mouse_coord), d);
                                    d = MIN(distance_to_linesegment(p[2], p[3], mouse_coord), d);
                                }

                                if (d < min_dist) {
                                    min_dist = (float)d;
                                    char value_buf[64] = "";
                                    series_temporal_print_value(value_buf, sizeof(value_buf), v, fn, k);

                                    hovered_idx = j;
                                    hovered_pop_idx = k;
                                    if (v.dim > 1) {
                                        snprintf(hovered_label, sizeof(hovered_label), "%s[%i]: %s %s", v.label, k + 1, value_buf, v.unit_str);
                                    } else {
                                        snprintf(hovered_label, sizeof(hovered_label), "%s: %s %s", v.label, value_buf, v.unit_str);
                                    }
                                }
                            }
                        }

                        if (hovered_idx != -1 && views[hovered_idx].script_ident[0] != '\0') {
                            script_set_hovered_property(data, str_from_cstr(views[hovered_idx].script_ident), hovered_pop_idx);
                        }
                    } else if (data->hovered_property_label[0] != '\0') {
                        // Hovered in another view of it (the other plot window, a list): light up its values here
                        for (int j = 0; j < sp.count; ++j) {
                            if (!resolved[j] || sp.series[j].key.variant != SeriesVariant_Values) continue;
                            if (strcmp(data->hovered_property_label, views[j].script_ident) == 0) {
                                hovered_idx = j;
                                hovered_pop_idx = data->hovered_property_pop_idx;
                                break;
                            }
                        }
                    }

                    if (is_dragging) {
                        time = ImPlot::GetPlotMousePos().x;
                    } else if (is_selecting) {
                        filter_end = MAX(ImPlot::GetPlotMousePos().x, filter_beg);
                        filter_beg = MIN(filter_beg, filter_end);
                    } else if (ImPlot::IsPlotHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl)) {
                            is_dragging = true;
                        } else if (ImGui::IsKeyDown(ImGuiKey_LeftShift)) {
                            filter_beg = ImPlot::GetPlotMousePos().x;
                            is_selecting = true;
                        }
                    }

                    for (int j = 0; j < sp.count; ++j) {
                        PlotSeries& s = sp.series[j];
                        const SeriesTemporalView& v = views[j];

                        if (ImPlot::IsLegendEntryHovered(v.plot_id)) {
                            if (v.script_ident[0] != '\0') {
                                script_visualize_payload(data, v.vis_payload, -1, MD_SCRIPT_VISUALIZE_ATOMS | MD_SCRIPT_VISUALIZE_GEOMETRY);
                                script_set_hovered_property(data, str_from_cstr(v.script_ident));
                            }
                            if (!resolved[j]) {
                                ImGui::SetTooltip("Nothing to plot at '%s' right now", s.key.path);
                            }
                            hovered_idx = j;
                            hovered_pop_idx = -1;
                        }

                        // legend context menu
                        if (ImPlot::BeginLegendPopup(v.plot_id)) {
                            if (ImGui::DeleteButton("Remove")) {
                                remove_idx = j;
                                ImGui::CloseCurrentPopup();
                            }

                            if (resolved[j] && !v.band) {
                                const char* plot_type_names[] = {"Line", "Scatter"};
                                int plot_type = s.plot_type == PlotType_Scatter ? 1 : 0;
                                if (ImGui::Combo("Plot Type", &plot_type, plot_type_names, IM_ARRAYSIZE(plot_type_names))) {
                                    s.plot_type = plot_type == 1 ? PlotType_Scatter : PlotType_Line;
                                }
                                if (s.plot_type == PlotType_Scatter) {
                                    if (ImGui::BeginCombo("Marker", ImPlot::GetMarkerName(s.marker))) {
                                        for (int k = 0; k < ImPlotMarker_COUNT; ++k) {
                                            if (ImGui::Selectable(ImPlot::GetMarkerName(k), s.marker == k)) {
                                                s.marker = (ImPlotMarker)k;
                                            }
                                        }
                                        ImGui::EndCombo();
                                    }
                                    ImGui::SliderFloat("Marker Size", &s.marker_size, 0.1f, 10.0f, "%.2f");
                                }
                            }

                            int popup_hovered_pop = -1;
                            plot_series_style_popup(data, s, resolved[j] ? v.dim : 1, v.script_ident, v.vis_payload, &popup_hovered_pop);
                            if (popup_hovered_pop != -1) {
                                hovered_idx = j;
                                hovered_pop_idx = popup_hovered_pop;
                            }
                            ImPlot::EndLegendPopup();
                        }

                        if (!resolved[j]) {
                            // Still in the legend, so it can be seen and removed
                            ImPlot::PlotDummy(v.plot_id);
                        } else {
                            const int population_size = CLAMP(v.dim, 1, PLOT_MAX_POPULATION);
                            auto plot = [&](int k) {
                                ImVec4 color = plot_series_member_color(s, k, population_size);
                                ImVec4 marker_line_color = {};
                                float  marker_line_weight = 0;
                                float  fill_alpha = 1.0f;
                                float  weight = 1.0f;

                                if (hovered_idx == j) {
                                    if (hovered_pop_idx == -1 || hovered_pop_idx == k) {
                                        color = plot_highlight(color);
                                        fill_alpha = 1.25f;
                                        marker_line_color = {1,1,1,1};
                                        marker_line_weight = 1.0f;
                                    }
                                    if (hovered_pop_idx == k) {
                                        weight = 2.0f;
                                    }
                                }

                                TemporalGetterPayload payload = { &v, k };
                                if (v.band) {
                                    ImPlot::SetNextFillStyle(color, fill_alpha);
                                    ImPlot::PlotShadedG(v.plot_id, temporal_getter_band_lo, &payload, temporal_getter_band_hi, &payload, v.num_samples);
                                } else if (s.plot_type == PlotType_Scatter) {
                                    ImPlot::SetNextMarkerStyle(s.marker, s.marker_size, color, marker_line_weight, marker_line_color);
                                    ImPlot::PlotScatterG(v.plot_id, temporal_getter_line, &payload, v.num_samples);
                                } else {
                                    ImPlot::SetNextLineStyle(color, weight);
                                    ImPlot::PlotLineG(v.plot_id, temporal_getter_line, &payload, v.num_samples);
                                }
                            };

                            for (int k = 0; k < population_size; ++k) {
                                if (population_size > 1 && !s.population_mask.test(k)) continue;
                                if (hovered_idx == j && hovered_pop_idx == k) continue;
                                plot(k);
                            }
                            // The hovered member last, on top
                            if (hovered_idx == j && hovered_pop_idx != -1 && hovered_pop_idx < population_size) {
                                plot(hovered_pop_idx);
                            }
                        }

                        if (ImPlot::BeginDragDropSourceItem(v.plot_id)) {
                            series_set_drag_payload(TIMELINE_SERIES_DND, s.key, i, v.label, s.color);
                            ImPlot::EndDragDropSource();
                        }
                    }

                    if (ImPlot::DragLineX(0, &time, ImVec4(1,1,0,1), 1.0f, ImPlotDragToolFlags_NoFit)) {
                        time = CLAMP(time, min_x_value, max_x_value);
                    }

                    if (movie_draw_timeline_markers(data)) {
                        print_timeline_tooltip = false;
                    }

                    if (ImPlot::IsPlotHovered() && hovered_idx != -1 && views[hovered_idx].script_ident[0] != '\0') {
                        const SeriesTemporalView& v = views[hovered_idx];
                        const int pop_idx = v.dim > 1 ? hovered_pop_idx : -1;
                        script_visualize_payload(data, v.vis_payload, pop_idx, ~MD_SCRIPT_VISUALIZE_SDF);
                        script_set_hovered_property(data, str_from_cstr(v.script_ident), hovered_pop_idx);
                    }

                    if (ImPlot::BeginDragDropTargetPlot()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(TIMELINE_SERIES_DND)) {
                            ASSERT(payload->DataSize == sizeof(SeriesDragPayload));
                            const SeriesDragPayload* dnd = (const SeriesDragPayload*)(payload->Data);
                            if (dnd->src_subplot >= 0 && dnd->src_subplot < PLOT_MAX_SUBPLOTS) {
                                // Out of one plot and into another moves it, keeping its style
                                plot_move_series(data, data->timeline.subplots[dnd->src_subplot], sp, dnd->key);
                            } else {
                                plot_add_series(data, sp, dnd->key);
                            }
                        }
                        ImPlot::EndDragDropTarget();
                    }

                    if (print_timeline_tooltip) {
                        ImPlotPoint plot_pos = ImPlot::GetPlotMousePos();
                        ImVec2 screen_pos = ImPlot::PlotToPixels(plot_pos);
                        ImVec2 p0 = {screen_pos.x, ImPlot::GetPlotPos().y};
                        ImVec2 p1 = {screen_pos.x, ImPlot::GetPlotPos().y + ImPlot::GetPlotSize().y};
                        ImPlot::PushPlotClipRect();
                        ImPlot::GetPlotDrawList()->AddLine(p0, p1, IM_COL32(255, 255, 255, 120));
                        ImPlot::PopPlotClipRect();

                        double t = plot_pos.x;
                        if (md_unit_is_none(x_unit)) {
                            int32_t frame_idx = CLAMP((int)(time_to_frame(t, x_values) + 0.5), 0, num_x_values-1);
                            ImGui::SetTooltip("Frame: %i\n%s", frame_idx, hovered_label);
                        } else {
                            ImGui::SetTooltip("Time: %.2f (%s)\n%s", t, x_unit_str, hovered_label);
                        }
                    }

                    ImPlot::EndPlot();
                }

                if (remove_idx != -1) {
                    plot_remove_series(sp, remove_idx);
                }
            }

            ImPlot::EndSubplots();

            time       = CLAMP(time, (double)min_x_value, (double)max_x_value);
            filter_beg = CLAMP(filter_beg, min_x_value, max_x_value);
            filter_end = CLAMP(filter_end, min_x_value, max_x_value);

            data->animation.frame = time_to_frame(time, data->timeline.x_values);
            data->timeline.filter.beg_frame = time_to_frame(filter_beg, data->timeline.x_values);
            data->timeline.filter.end_frame = time_to_frame(filter_end, data->timeline.x_values);

            ImPlot::GetInputMap() = old_map;
        }

        if (data->timeline.filter.enabled && (data->timeline.filter.beg_frame != pre_filter_min || data->timeline.filter.end_frame != pre_filter_max)) {
            data->script.evaluate_filt = true;
        }

        // A series dragged out of a plot and dropped outside any of them is taken out of that plot
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            const ImGuiPayload* payload = ImGui::GetDragDropPayload();
            if (payload && payload->IsDataType(TIMELINE_SERIES_DND) && !ImGui::IsDragDropPayloadBeingAccepted()) {
                const SeriesDragPayload* dnd = (const SeriesDragPayload*)(payload->Data);
                if (dnd && dnd->src_subplot >= 0 && dnd->src_subplot < PLOT_MAX_SUBPLOTS) {
                    PlotSubplot& src = data->timeline.subplots[dnd->src_subplot];
                    plot_remove_series(src, plot_find_series(src, dnd->key));
                }
            }
        }
    }
    ImGui::End();
}

// #distribution_window

struct HistogramGetterPayload {
    const SeriesHistogramView* view;
    int k;
};

static ImPlotPoint histogram_getter(int i, void* payload) {
    const HistogramGetterPayload* p = (const HistogramGetterPayload*)payload;
    return series_histogram_point(*p->view, i, p->k);
}

static ImPlotPoint histogram_getter_zero(int i, void* payload) {
    const HistogramGetterPayload* p = (const HistogramGetterPayload*)payload;
    return ImPlotPoint(series_histogram_point(*p->view, i, p->k).x, 0.0);
}

static void draw_distribution_properties_menu(ApplicationState* data) {
    PlotSubplot& first = data->distributions.subplots[0];
    const uint32_t kinds = MD_SCRIPT_PROPERTY_FLAG_TEMPORAL | MD_SCRIPT_PROPERTY_FLAG_DISTRIBUTION;

    // A temporal property is binned over its frames, a distribution is shown as evaluated. Over the
    // timeline filter's frames too, while there is a filter.
    auto script_section = [&](SeriesSource source) -> int {
        int num_listed = 0;
        series_for_each_script_property(data, source, kinds, [&](const SeriesKey& key) {
            plot_series_list_item(data, DISTRIBUTION_SERIES_DND, first, key);
            num_listed += 1;
            const bool temporal = md_script_ir_property_flags(data->script.eval_ir, series_script_ident(key)) & MD_SCRIPT_PROPERTY_FLAG_TEMPORAL;
            if (temporal && script_property_population(data, key) > 1) {
                SeriesKey agg = key;
                agg.variant = SeriesVariant_Aggregate;
                ImGui::Indent();
                plot_series_list_item(data, DISTRIBUTION_SERIES_DND, first, agg);
                ImGui::Unindent();
            }
        });
        return num_listed;
    };

    ImGui::SeparatorText("Script");
    if (script_section(SeriesSource_Script) == 0) {
        ImGui::TextDisabled("No temporal or distribution properties, define and evaluate them in the script editor");
    }
    if (data->timeline.filter.enabled) {
        ImGui::SeparatorText("Script, over the timeline filter");
        script_section(SeriesSource_ScriptFiltered);
    }

    ImGui::SeparatorText("Loaded along the run");
    plot_system_series_menu(data, DISTRIBUTION_SERIES_DND, first);
}

static void draw_distribution_window(ApplicationState* data) {
    ImGui::SetNextWindowSize(ImVec2(200, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Distributions", &data->distributions.show_window, ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_MenuBar)) {
        int& num_subplots = data->distributions.num_subplots;
        num_subplots = CLAMP(num_subplots, 1, PLOT_MAX_SUBPLOTS);

        // Refit a subplot's axes to what it holds, after it gained a series or its binning changed
        static bool fit_subplot[PLOT_MAX_SUBPLOTS] = {};

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Properties")) {
                draw_distribution_properties_menu(data);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Subplots")) {
                ImGui::SliderInt("Num Subplots", &num_subplots, 1, PLOT_MAX_SUBPLOTS);
                if (ImGui::Button("Add Subplot")) {
                    num_subplots = CLAMP(num_subplots + 1, 1, PLOT_MAX_SUBPLOTS);
                }
                if (ImGui::Button("Clear All")) {
                    plot_clear(data->distributions.subplots, PLOT_MAX_SUBPLOTS);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        const ImPlotAxisFlags axis_flags   = ImPlotAxisFlags_NoSideSwitch | ImPlotAxisFlags_NoHighlight;
        const ImPlotAxisFlags axis_flags_x = axis_flags;
        const ImPlotAxisFlags axis_flags_y = axis_flags | ImPlotAxisFlags_AutoFit;
        const ImPlotFlags     plot_flags   = ImPlotFlags_NoBoxSelect | ImPlotFlags_NoFrame;

        if (ImPlot::BeginSubplots("##distribution_plots", num_subplots, 1, ImVec2(-1,-1))) {
            for (int i = 0; i < num_subplots; ++i) {
                PlotSubplot& sp = data->distributions.subplots[i];

                SeriesHistogramView views[PLOT_MAX_SERIES_PER_SUBPLOT];
                bool resolved[PLOT_MAX_SERIES_PER_SUBPLOT] = {};
                for (int j = 0; j < sp.count; ++j) {
                    resolved[j] = series_resolve_histogram(&views[j], data, sp.series[j].key, sp.series[j].num_bins);
                }

                int remove_idx = -1;

                if (fit_subplot[i]) {
                    ImPlot::SetNextAxesToFit();
                    fit_subplot[i] = false;
                }

                if (ImPlot::BeginPlot("", ImVec2(-1,0), plot_flags)) {
                    // Labelled with what the series are shown in; empty when there is no unit, or several
                    char x_label[64] = "";
                    char y_label[64] = "";
                    bool first_label = true;
                    for (int j = 0; j < sp.count; ++j) {
                        if (!resolved[j]) continue;
                        if (first_label) {
                            str_copy_to_char_buf(x_label, sizeof(x_label), str_from_cstr(views[j].x_unit_str));
                            str_copy_to_char_buf(y_label, sizeof(y_label), str_from_cstr(views[j].y_unit_str));
                            first_label = false;
                        } else {
                            if (strcmp(x_label, views[j].x_unit_str) != 0) x_label[0] = '\0';
                            if (strcmp(y_label, views[j].y_unit_str) != 0) y_label[0] = '\0';
                        }
                    }

                    ImPlot::SetupAxes(x_label, y_label, axis_flags_x, axis_flags_y);
                    ImPlot::SetupFinish();

                    int  hovered_idx       = -1;
                    int  hovered_pop_idx   = -1;
                    char hovered_label[128] = "";

                    if (ImPlot::IsPlotHovered()) {
                        script_set_hovered_property(data, STR_LIT(""));

                        const ImPlotPoint mouse_pos = ImPlot::GetPlotMousePos();
                        const ImVec2 mouse_coord = ImPlot::PlotToPixels(mouse_pos);

                        const double max_rad = 20; // 20 pixels
                        const double area_dist = max_rad * 0.2;
                        double min_dist = max_rad;

                        for (int j = 0; j < sp.count; ++j) {
                            if (!resolved[j]) continue;
                            const PlotSeries& s = sp.series[j];
                            const SeriesHistogramView& v = views[j];
                            if (v.num_bins <= 0 || v.x_max <= v.x_min) continue;

                            ImPlotItem* item = ImPlot::GetItem(v.plot_id);
                            if (!item || !item->Show) continue;

                            const int nb = v.num_bins;
                            const double x = series_histogram_index_at(v, mouse_pos.x);
                            const int xn = CLAMP((int)(x + 0.5), 0, nb - 1);
                            const int xi[4] = {
                                CLAMP((int)x - 1, 0, nb - 1),
                                CLAMP((int)x,     0, nb - 1),
                                CLAMP((int)x + 1, 0, nb - 1),
                                CLAMP((int)x + 2, 0, nb - 1),
                            };

                            const int dim = CLAMP(v.dim, 1, PLOT_MAX_POPULATION);
                            for (int k = 0; k < dim; ++k) {
                                if (dim > 1 && !s.population_mask.test(k)) continue;
                                HistogramGetterPayload payload = { &v, k };
                                // Later entries lie on top: prefer them a little when two are equally near
                                const double layer = j + k / (double)(PLOT_MAX_POPULATION - 1);
                                const double layer_dist = ((sp.count - layer) / sp.count) * (max_rad * 0.1);
                                double d = DBL_MAX;

                                if (s.plot_type == PlotType_Bars) {
                                    const double half_width = 0.5 * series_histogram_bin_width(v) * s.bar_width;
                                    for (int l = 0; l < 3; ++l) {
                                        const ImPlotPoint top = histogram_getter(xi[l], &payload);
                                        const ImVec2 p0 = ImPlot::PlotToPixels(ImPlotPoint(top.x - half_width, 0.0));
                                        const ImVec2 p1 = ImPlot::PlotToPixels(ImPlotPoint(top.x + half_width, top.y));
                                        const ImVec2 ll = ImMin(p0, p1);
                                        const ImVec2 ur = ImMax(p0, p1);
                                        d = MIN(d, sqrt(ImLengthSqr(mouse_coord - ImClamp(mouse_coord, ll, ur))));
                                    }
                                    d += area_dist + layer_dist;
                                } else if (s.plot_type == PlotType_Area) {
                                    for (int l = 0; l < 2; ++l) {
                                        const ImVec2 a = ImPlot::PlotToPixels(histogram_getter(xi[l], &payload));
                                        const ImVec2 b = ImPlot::PlotToPixels(histogram_getter(xi[l + 1], &payload));
                                        const ImVec2 z = ImPlot::PlotToPixels(histogram_getter_zero(xi[l], &payload));
                                        const float x_min = MIN(a.x, b.x);
                                        const float x_max = MAX(a.x, b.x);
                                        const float t = (x_max > x_min) ? CLAMP((mouse_coord.x - x_min) / (x_max - x_min), 0.0f, 1.0f) : 0.0f;
                                        const float y_top = lerp(a.y, b.y, t);
                                        const ImVec2 p = {
                                            CLAMP(mouse_coord.x, x_min, x_max),
                                            CLAMP(mouse_coord.y, MIN(y_top, z.y), MAX(y_top, z.y))
                                        };
                                        d = MIN(d, sqrt(ImLengthSqr(mouse_coord - p)));
                                    }
                                    d += area_dist + layer_dist;
                                } else {
                                    ImVec2 p[4] = {
                                        ImPlot::PlotToPixels(histogram_getter(xi[0], &payload)),
                                        ImPlot::PlotToPixels(histogram_getter(xi[1], &payload)),
                                        ImPlot::PlotToPixels(histogram_getter(xi[2], &payload)),
                                        ImPlot::PlotToPixels(histogram_getter(xi[3], &payload)),
                                    };
                                    d = distance_to_linesegment(p[0], p[1], mouse_coord);
                                    d = MIN(distance_to_linesegment(p[1], p[2], mouse_coord), d);
                                    d = MIN(distance_to_linesegment(p[2], p[3], mouse_coord), d);
                                    d += layer_dist;
                                }

                                if (d < min_dist) {
                                    min_dist = d;
                                    const ImPlotPoint pt = histogram_getter(xn, &payload);
                                    hovered_idx = j;
                                    hovered_pop_idx = dim > 1 ? k : -1;
                                    if (dim > 1) {
                                        snprintf(hovered_label, sizeof(hovered_label), "%s[%i]: %.4g at %.4g %s", v.label, k + 1, pt.y, pt.x, v.x_unit_str);
                                    } else {
                                        snprintf(hovered_label, sizeof(hovered_label), "%s: %.4g at %.4g %s", v.label, pt.y, pt.x, v.x_unit_str);
                                    }
                                }
                            }
                        }

                        if (hovered_idx != -1) {
                            const SeriesHistogramView& v = views[hovered_idx];
                            if (v.script_ident[0] != '\0') {
                                script_set_hovered_property(data, str_from_cstr(v.script_ident), hovered_pop_idx);
                                script_visualize_payload(data, v.vis_payload, hovered_pop_idx, MD_SCRIPT_VISUALIZE_ATOMS | MD_SCRIPT_VISUALIZE_GEOMETRY);
                            }
                            if (hovered_label[0] != '\0') {
                                ImGui::SetTooltip("%s", hovered_label);
                            }
                        }
                    } else if (data->hovered_property_label[0] != '\0') {
                        for (int j = 0; j < sp.count; ++j) {
                            if (!resolved[j]) continue;
                            if (strcmp(data->hovered_property_label, views[j].script_ident) == 0) {
                                hovered_idx = j;
                                hovered_pop_idx = data->hovered_property_pop_idx;
                                break;
                            }
                        }
                    }

                    for (int j = 0; j < sp.count; ++j) {
                        PlotSeries& s = sp.series[j];
                        const SeriesHistogramView& v = views[j];

                        if (ImPlot::IsLegendEntryHovered(v.plot_id)) {
                            if (v.script_ident[0] != '\0') {
                                script_visualize_payload(data, v.vis_payload, -1, MD_SCRIPT_VISUALIZE_ATOMS | MD_SCRIPT_VISUALIZE_GEOMETRY);
                                script_set_hovered_property(data, str_from_cstr(v.script_ident));
                            }
                            if (!resolved[j]) {
                                ImGui::SetTooltip("Nothing to show for '%s' right now", s.key.path);
                            }
                            hovered_idx = j;
                            hovered_pop_idx = -1;
                        }

                        if (ImPlot::BeginLegendPopup(v.plot_id)) {
                            if (ImGui::DeleteButton("Remove")) {
                                remove_idx = j;
                                ImGui::CloseCurrentPopup();
                            }

                            const char* plot_type_names[] = {"Line", "Area", "Bars"};
                            const PlotType plot_types[]   = {PlotType_Line, PlotType_Area, PlotType_Bars};
                            int plot_type = 0;
                            for (int k = 0; k < IM_ARRAYSIZE(plot_types); ++k) {
                                if (s.plot_type == plot_types[k]) plot_type = k;
                            }
                            if (ImGui::Combo("Plot Type", &plot_type, plot_type_names, IM_ARRAYSIZE(plot_type_names))) {
                                s.plot_type = plot_types[plot_type];
                            }
                            if (s.plot_type == PlotType_Bars) {
                                ImGui::SliderFloat("Bar Width", &s.bar_width, 0.01f, 1.0f, "%.3f");
                            }

                            // Powers of two: a script distribution only divides into those
                            int bins_exp = (int)log2((double)MAX(s.num_bins, 2));
                            char bins_fmt[32];
                            snprintf(bins_fmt, sizeof(bins_fmt), "%d", 1 << bins_exp);
                            if (ImGui::SliderInt("Num Bins", &bins_exp, 5, 10, bins_fmt)) {
                                s.num_bins = 1 << bins_exp;
                                fit_subplot[i] = true;
                            }
                            if (resolved[j] && v.num_bins != s.num_bins) {
                                ImGui::TextDisabled("Evaluated at %d bins", v.num_bins);
                            }

                            int popup_hovered_pop = -1;
                            plot_series_style_popup(data, s, resolved[j] ? v.dim : 1, v.script_ident, v.vis_payload, &popup_hovered_pop);
                            if (popup_hovered_pop != -1) {
                                hovered_idx = j;
                                hovered_pop_idx = popup_hovered_pop;
                            }
                            ImPlot::EndLegendPopup();
                        }

                        if (!resolved[j] || v.num_bins <= 0) {
                            ImPlot::PlotDummy(v.plot_id);
                        } else {
                            const int population_size = CLAMP(v.dim, 1, PLOT_MAX_POPULATION);
                            auto plot = [&](int k) {
                                ImVec4 color = plot_series_member_color(s, k, population_size);
                                float  fill_alpha = 1.0f;
                                float  weight = 1.0f;
                                if (hovered_idx == j) {
                                    if (hovered_pop_idx == -1 || hovered_pop_idx == k) {
                                        color = plot_highlight(color);
                                        fill_alpha = 1.25f;
                                    }
                                    if (hovered_pop_idx == k) {
                                        weight = 2.0f;
                                    }
                                }

                                HistogramGetterPayload payload = { &v, k };
                                switch (s.plot_type) {
                                case PlotType_Area:
                                    ImPlot::SetNextFillStyle(color, fill_alpha);
                                    ImPlot::PlotShadedG(v.plot_id, histogram_getter_zero, &payload, histogram_getter, &payload, v.num_bins);
                                    break;
                                case PlotType_Bars:
                                    ImPlot::SetNextFillStyle(color, fill_alpha);
                                    ImPlot::PlotBarsG(v.plot_id, histogram_getter, &payload, v.num_bins, series_histogram_bin_width(v) * s.bar_width);
                                    break;
                                default:
                                    ImPlot::SetNextLineStyle(color, weight);
                                    ImPlot::PlotLineG(v.plot_id, histogram_getter, &payload, v.num_bins);
                                    break;
                                }
                            };

                            for (int k = 0; k < population_size; ++k) {
                                if (population_size > 1 && !s.population_mask.test(k)) continue;
                                if (hovered_idx == j && hovered_pop_idx == k) continue;
                                plot(k);
                            }
                            if (hovered_idx == j && hovered_pop_idx != -1 && hovered_pop_idx < population_size) {
                                plot(hovered_pop_idx);
                            }
                        }

                        if (ImPlot::BeginDragDropSourceItem(v.plot_id)) {
                            series_set_drag_payload(DISTRIBUTION_SERIES_DND, s.key, i, v.label, s.color);
                            ImPlot::EndDragDropSource();
                        }
                    }

                    if (ImPlot::BeginDragDropTargetPlot()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DISTRIBUTION_SERIES_DND)) {
                            ASSERT(payload->DataSize == sizeof(SeriesDragPayload));
                            const SeriesDragPayload* dnd = (const SeriesDragPayload*)(payload->Data);
                            if (dnd->src_subplot >= 0 && dnd->src_subplot < PLOT_MAX_SUBPLOTS) {
                                plot_move_series(data, data->distributions.subplots[dnd->src_subplot], sp, dnd->key);
                            } else {
                                plot_add_series(data, sp, dnd->key);
                            }
                            fit_subplot[i] = true;
                        } else if (const ImGuiPayload* tl_payload = ImGui::AcceptDragDropPayload(TIMELINE_SERIES_DND)) {
                            // Something plotted over time, dropped here: the distribution of its
                            // values, whichever view of them (a mean, a spread) was dragged
                            ASSERT(tl_payload->DataSize == sizeof(SeriesDragPayload));
                            SeriesKey key = ((const SeriesDragPayload*)tl_payload->Data)->key;
                            key.variant = SeriesVariant_Values;
                            plot_add_series(data, sp, key);
                            fit_subplot[i] = true;
                        }
                        ImPlot::EndDragDropTarget();
                    }

                    if (ImPlot::IsPlotHovered()) {
                        ImPlotPoint plot_pos = ImPlot::GetPlotMousePos();
                        ImVec2 screen_pos = ImPlot::PlotToPixels(plot_pos);
                        ImVec2 p0 = {screen_pos.x, ImPlot::GetPlotPos().y};
                        ImVec2 p1 = {screen_pos.x, ImPlot::GetPlotPos().y + ImPlot::GetPlotSize().y};
                        ImPlot::PushPlotClipRect();
                        ImPlot::GetPlotDrawList()->AddLine(p0, p1, IM_COL32(255, 255, 255, 120));
                        ImPlot::PopPlotClipRect();
                    }

                    ImPlot::EndPlot();
                }

                if (remove_idx != -1) {
                    plot_remove_series(sp, remove_idx);
                }
            }
            ImPlot::EndSubplots();
        }

        // A series dragged out of a plot and dropped outside any of them is taken out of that plot
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            const ImGuiPayload* payload = ImGui::GetDragDropPayload();
            if (payload && payload->IsDataType(DISTRIBUTION_SERIES_DND) && !ImGui::IsDragDropPayloadBeingAccepted()) {
                const SeriesDragPayload* dnd = (const SeriesDragPayload*)(payload->Data);
                if (dnd && dnd->src_subplot >= 0 && dnd->src_subplot < PLOT_MAX_SUBPLOTS) {
                    PlotSubplot& src = data->distributions.subplots[dnd->src_subplot];
                    plot_remove_series(src, plot_find_series(src, dnd->key));
                }
            }
        }
    }
    ImGui::End();
}

static void draw_debug_window(ApplicationState* data) {
    ASSERT(data);

    ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Debug", &data->show_debug_window)) {       
        task_system::ID tasks[256]; 
        size_t num_tasks = task_system::pool_running_tasks(tasks, ARRAY_SIZE(tasks));
        if (num_tasks > 0) {
            ImGui::Text("Running Pool Tasks:");
            for (size_t i = 0; i < num_tasks; ++i) {
                str_t lbl = task_system::task_label(tasks[i]);
                ImGui::Text("[%i]: %.*s", (int)i, (int)lbl.len, lbl.ptr);
            }
        }

        ImGuiID active = ImGui::GetActiveID();
        ImGuiID hover  = ImGui::GetHoveredID();
        ImGui::Text("Active ID: %u, Hover ID: %u", active, hover);

        ImGui::Text("Mouse Pos: (%.3f, %.3f)", ImGui::GetMousePos().x, ImGui::GetMousePos().y);
        ImGui::Text("Camera Position: (%g, %g, %g)", data->view.camera.position.x, data->view.camera.position.y, data->view.camera.position.z);
        ImGui::Text("Camera Orientation: (%g, %g, %g, %g)", data->view.camera.orientation.x, data->view.camera.orientation.y, data->view.camera.orientation.z, data->view.camera.orientation.w);

        mat4_t P = data->view.param.matrix.curr.proj;
        ImGui::Text("proj_matrix:");
        ImGui::Text("[%g %g %g %g]", P.col[0].x, P.col[0].y, P.col[0].z, P.col[0].w);
        ImGui::Text("[%g %g %g %g]", P.col[1].x, P.col[1].y, P.col[1].z, P.col[1].w);
        ImGui::Text("[%g %g %g %g]", P.col[2].x, P.col[2].y, P.col[2].z, P.col[2].w);
        ImGui::Text("[%g %g %g %g]", P.col[3].x, P.col[3].y, P.col[3].z, P.col[3].w);
    }
    ImGui::End();
}

// Opens the script reference at topic (a procedure name, alias or heading anchor). Anything else is searched for,
// and an empty topic puts the focus in the search box (which always takes the focus). Without take_focus the keyboard
// focus stays where it is, so lookups from the script editor keep its caret.
static void open_script_reference(ApplicationState* state, str_t topic, bool take_focus) {
    ASSERT(state);
    if (str_empty(topic)) {
        script_reference::focus_search();
        take_focus = true;
    } else if (!script_reference::show(topic)) {
        script_reference::search(topic);
    }
    state->show_script_reference_window = true;
    script_reference::reveal(take_focus);
}

static void draw_script_reference_window(ApplicationState* state) {
    ASSERT(state);
    const script_reference::Action action = script_reference::draw_window(&state->show_script_reference_window);
    if (!str_empty(action.insert_code)) {
        // Examples go in as whole lines at the cursor of the script editor
        script_editor::insert_lines_at_cursor(state->editor, action.insert_code);
        state->show_script_window = true;
    }
}

static void draw_script_editor_window(ApplicationState* state) {
    ASSERT(state);

    ImGui::SetNextWindowSize({300,200}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Script Editor", &state->show_script_window, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::SetWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File")) {
                char path_buf[1024] = "";
                if (ImGui::MenuItem("Load")) {
                    if (application::file_dialog(path_buf, sizeof(path_buf), application::FileDialogFlag_Open, STR_LIT("txt"))) {
                        str_t txt = load_textfile(str_from_cstr(path_buf), frame_alloc);
                        std::string str(txt.ptr, txt.len);
                        state->editor.SetText(str);
                    }
                }
                if (ImGui::MenuItem("Save")) {
                    auto textToSave = state->editor.GetText();
                    if (application::file_dialog(path_buf, sizeof(path_buf), application::FileDialogFlag_Save, STR_LIT("txt"))) {
                        str_t path = str_t{path_buf, strnlen(path_buf, sizeof(path_buf))};
                        md_file_t file = {0};
                        if (md_file_open(&file, path, MD_FILE_WRITE | MD_FILE_CREATE | MD_FILE_TRUNCATE)) {
                            md_file_write(file, textToSave.c_str(), textToSave.length());
                            md_file_close(&file);
                        } else {
                            VIAMD_LOG_ERROR("Failed to open file '%s' for saving script", path_buf);
                        }
                    }
                }
                if (ImGui::MenuItem("Export")) {
                    state->show_property_export_window = true;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                bool ro = state->editor.IsReadOnlyEnabled();
                if (ImGui::MenuItem("Read-only mode", nullptr, &ro))
                    state->editor.SetReadOnlyEnabled(ro);
                ImGui::Separator();

                const bool sel = state->editor.AnyCursorHasSelection();
                if (ImGui::MenuItem("Undo", "Ctrl-Z", nullptr, !ro && state->editor.CanUndo()))
                    state->editor.Undo();
                if (ImGui::MenuItem("Redo", "Ctrl-Y", nullptr, !ro && state->editor.CanRedo()))
                    state->editor.Redo();

                ImGui::Separator();

                if (ImGui::MenuItem("Copy", "Ctrl-C", nullptr, sel))
                    state->editor.Copy();
                if (ImGui::MenuItem("Cut", "Ctrl-X", nullptr, !ro && sel))
                    state->editor.Cut();
                if (ImGui::MenuItem("Delete", "Del", nullptr, !ro && sel))
                    state->editor.ReplaceTextInAllCursors("");
                if (ImGui::MenuItem("Paste", "Ctrl-V", nullptr, !ro && ImGui::GetClipboardText() != nullptr))
                    state->editor.Paste();

                ImGui::Separator();

                if (ImGui::MenuItem("Select all", "Ctrl-A"))
                    state->editor.SelectAll();
                if (ImGui::MenuItem("Find / Replace", "Ctrl-F"))
                    state->editor.OpenFindReplaceWindow();

                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Settings")) {
                if (ImGui::MenuItem("Dark palette"))
                    state->editor.SetPalette(TextEditor::GetDarkPalette());
                if (ImGui::MenuItem("Light palette"))
                    state->editor.SetPalette(TextEditor::GetLightPalette());
                if (ImGui::MenuItem("Retro blue palette"))
                    state->editor.SetPalette(script_editor::retro_blue_palette());
                ImGui::Separator();
                ImGui::ColorEdit4("Point Color",    state->script.point_color.elem);
                ImGui::ColorEdit4("Line Color",     state->script.line_color.elem);
                ImGui::ColorEdit4("Triangle Color", state->script.triangle_color.elem);
                ImGui::ColorEdit4("Text Color",     state->script.text_color.elem);
                ImGui::ColorEdit4("Text Bg Color",  state->script.text_bg_color.elem);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("Script reference", "F1")) {
                    open_script_reference(state, {});
                }
                const std::string word = script_editor::word_at_cursor(state->editor);
                char label[128];
                if (word.empty()) snprintf(label, sizeof(label), "Look up word under cursor");
                else snprintf(label, sizeof(label), "Look up '%s'", word.c_str());
                if (ImGui::MenuItem(label, "F1", nullptr, !word.empty())) {
                    open_script_reference(state, {word.data(), word.size()}, false);
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }

        const ImVec2 content_size = ImGui::GetContentRegionAvail();
        const char* btn_text = "Evaluate";
        const ImVec2 label_size = ImGui::CalcTextSize(btn_text, NULL, true) * ImVec2(1.4, 1.0);
        const ImVec2 btn_size = ImGui::CalcItemSize(ImVec2(0,0), label_size.x + ImGui::GetStyle().FramePadding.x * 2.0f, label_size.y + ImGui::GetStyle().FramePadding.y * 2.0f);
        const ImVec2 text_size(content_size - ImVec2(0, btn_size.y + ImGui::GetStyle().ItemSpacing.y));

        // Shift+Enter evaluates the script. The editor binds it to "insert line above", so claim it before the editor
        // sees it (shortcut routes are resolved from the previous frame, hence the focus from the last draw).
        bool eval = false;
        if (ImGui::Shortcut(KEY_SCRIPT_EVALUATE, ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_RouteOverFocused)) {
            eval = true;
        }

        // While a visualization is hovered, the mouse wheel steps through its elements instead of scrolling
        const script_editor::Marker* prev_hovered = script_editor::markers_hovered(&state->editor_markers);
        ImGuiWindowFlags editor_flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_HorizontalScrollbar;
        if (prev_hovered && prev_hovered->type == script_editor::MarkerType_Visualization) {
            editor_flags |= ImGuiWindowFlags_NoScrollWithMouse;
        }

        if (state->editor.Render("TextEditor", text_size, ImGuiChildFlags_None, editor_flags)) {
            state->script.compile_ir = true;
            state->script.time_since_last_change = 0;
        }
        const bool editor_hovered = ImGui::IsItemHovered();
        state->editor_focused = script_editor::has_focus_after_render();
        const script_editor::Marker* hovered_marker = script_editor::markers_update(&state->editor_markers, &state->editor, editor_hovered);

        // F1 looks up the identifier under the mouse in the script reference, or else the one at the text cursor.
        // Hovering does not require focus: opening the reference takes the focus, and a second F1 over another
        // identifier would otherwise be ignored until the editor is clicked again.
        if ((editor_hovered || state->editor_focused) && ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
            std::string word;
            if (editor_hovered) {
                word = script_editor::word_at_mouse(state->editor, ImGui::GetMousePos());
            }
            if (word.empty() && state->editor_focused) {
                word = script_editor::word_at_cursor(state->editor);
            }
            open_script_reference(state, {word.data(), word.size()}, false);
        }

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + content_size.x - btn_size.x);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().ItemSpacing.y);

        const bool valid = md_script_ir_valid(state->script.ir) && run_num_frames(state) > 0;  
        if (!valid) ImGui::PushDisabled();
        if (ImGui::Button(btn_text, btn_size)) {
            eval = true;
        }
        if (!valid) ImGui::PopDisabled();

        if (eval && valid) {
            state->script.eval_init = true;
        }

        if (editor_hovered) {
            md_bitfield_clear(&state->selection.highlight_mask);
        }

        if (hovered_marker) {
            if (!str_empty(hovered_marker->text)) {
                ImGui::BeginTooltip();
                ImGui::Text(STR_FMT, STR_ARG(hovered_marker->text));
                if (state->script.sub_idx != -1) {
                    ImGui::Text("Currently inspected idx: %i", state->script.sub_idx + 1);
                }
                ImGui::EndTooltip();
            }
            if (hovered_marker->atoms || hovered_marker->payload) {
                if (hovered_marker->atoms) {
                    // Errors and warnings: the atoms the message is about
                    md_bitfield_copy(&state->selection.highlight_mask, hovered_marker->atoms);
                }
                else if (hovered_marker->type == script_editor::MarkerType_Visualization) {
                    // Clear hovered property
                    script_set_hovered_property(state, STR_LIT(""));
                    if (md_script_ir_valid(state->script.ir)) {
                        const md_script_vis_payload_o* payload = hovered_marker->payload;
                        str_t payload_ident = md_script_payload_ident(payload);
                        int payload_dim  = md_script_payload_dim(payload); 

                        if (payload_dim > 1) {
                            int delta = (int)ImGui::GetIO().MouseWheel;
                            if (ImGui::IsKeyDown(ImGuiMod_Shift)) {
                                delta *= 10;
                            }
                            state->script.sub_idx += delta;
                            state->script.sub_idx = CLAMP(state->script.sub_idx, -1, (int)payload_dim - 1);
                        }

                        script_visualize_payload(state, payload, state->script.sub_idx, 0);
                        script_set_hovered_property(state, payload_ident, state->script.sub_idx);
                    }
                }

                bool lm_click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                bool rm_click = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
                if (ImGui::IsKeyDown(ImGuiMod_Shift) && (lm_click || rm_click)) {
                    SelectionOperator op = lm_click ? SelectionOperator::Or : SelectionOperator::AndNot;
                    modify_selection(state, &state->selection.highlight_mask, op);
                }
            }
        } else {
            state->script.sub_idx = -1;
        }
    }
    ImGui::End();
}

static bool export_xvg(const float* column_data[], const char* column_labels[], size_t num_columns, size_t num_rows, str_t filename) {
    ASSERT(column_data);
    ASSERT(column_labels);

    md_file_t file = {0};
    if (!md_file_open(&file, filename, MD_FILE_WRITE | MD_FILE_CREATE | MD_FILE_TRUNCATE)) {
        VIAMD_LOG_ERROR("Failed to open file '" STR_FMT "' to write data.", STR_ARG(filename));
        return false;
    }    

    time_t t;
    struct tm* info;
    time(&t);
    info = localtime(&t);

    // Print Header
    md_file_printf(file, "# This file was created %s", asctime(info));
    md_file_printf(file, "# Created by:\n");
    md_file_printf(file, "# VIAMD \n");

    // Print Legend Meta
    md_file_printf(file, "@    title \"VIAMD Properties\"\n");
    md_file_printf(file, "@    xaxis  label \"Time\"\n");
    md_file_printf(file, "@ TYPE xy\n");
    md_file_printf(file, "@ view 0.15, 0.15, 0.75, 0.85\n");
    md_file_printf(file, "@ legend on\n");
    md_file_printf(file, "@ legend box on\n");
    md_file_printf(file, "@ legend loctype view\n");
    md_file_printf(file, "@ legend 0.78, 0.8\n");
    md_file_printf(file, "@ legend length %i\n", num_columns);

    for (size_t j = 0; j < num_columns; ++j) {
        md_file_printf(file, "@ s%zu legend \"%s\"\n", j, column_labels[j]);
    }

    for (size_t i = 0; i < num_rows; ++i) {
        for (size_t j = 0; j < num_columns; ++j) {
            md_file_printf(file, "%12.6f ", column_data[j][i]);
        }
        md_file_printf(file, "\n");
    }

    md_file_close(&file);
    VIAMD_LOG_SUCCESS("Successfully exported XVG file to '%.*s'", (int)filename.len, filename.ptr);
    return true;
}

static bool export_csv(const float* column_data[], const char* column_labels[], size_t num_columns, size_t num_rows, str_t filename) {
    ASSERT(column_data);
    ASSERT(column_labels);

    md_file_t file = {0};
    if (!md_file_open(&file, filename, MD_FILE_WRITE | MD_FILE_CREATE | MD_FILE_TRUNCATE)) {
        VIAMD_LOG_ERROR("Failed to open file '%.*s' to write data.", (int)filename.len, filename.ptr);
        return false;
    }
    
    for (size_t i = 0; i < num_columns; ++i) {
        md_file_printf(file, "%s,", column_labels[i]);
    }
    md_file_printf(file, "\n");

    for (size_t i = 0; i < num_rows; ++i) {
        for (size_t j = 0; j < num_columns; ++j) {
            md_file_printf(file, "%.6g,", column_data[j][i]);
        }
        md_file_printf(file, "\n");
    }

    md_file_close(&file);
    VIAMD_LOG_SUCCESS("Successfully exported CSV file to '%.*s'", (int)filename.len, filename.ptr);
    return true;
}

static bool export_cube(const ApplicationState& data, const md_attribute_t* attr, const md_script_vis_payload_o* vis_payload, str_t filename) {
    // @NOTE: First we need to extract some meta data for the cube format, we need the atom indices/bits for any SDF
    // And the origin + extent of the volume in spatial coordinates (Ångström)

    const float* volume_values = (const float*)md_attribute_view(attr, MD_ATTRIBUTE_TYPE_F32, 1, 3);
    if (!volume_values) {
        VIAMD_LOG_ERROR("Export Cube: The property to be exported did not exist");
        return false;
    }

    if (!vis_payload) {
        VIAMD_LOG_ERROR("Export Cube: Missing input visualization data");
        return false;
    }

	md_temp_scope_t temp = md_temp_begin_in(frame_alloc);
	defer { md_temp_end(temp); };

    const md_system_t& sys = data.mold.sys;
    md_system_state_t state = { .alloc = temp.arena };
    if (!extract_frame(&data, 0, &state)) {
        return false;
    }

    bool result = false;
    md_script_vis_t vis = { 0 };
    md_script_vis_init(&vis, temp.arena);
        
    if (md_script_ir_valid(data.script.eval_ir)) {
        md_script_vis_ctx_t ctx = {
            .ir = data.script.eval_ir,
            .sys = &data.mold.sys,
            .state = &data.mold.state,
        };
        result = md_script_vis_eval_payload(&vis, vis_payload, 0, &ctx, MD_SCRIPT_VISUALIZE_ATOMS | MD_SCRIPT_VISUALIZE_SDF);
    }

    if (result == true) {
        md_file_t file = { 0 };
        if (!md_file_open(&file, filename, MD_FILE_WRITE | MD_FILE_CREATE | MD_FILE_TRUNCATE)) {
            VIAMD_LOG_ERROR("Failed to open file '%.*s' in order to write to it.", (int)filename.len, filename.ptr);
            return false;
        }

        // Two comment lines
        md_file_printf(file, "EXPORTED DENSITY VOLUME FROM VIAMD, UNITS IN BOHR\n");
        md_file_printf(file, "OUTER LOOP: X, MIDDLE LOOP: Y, INNER LOOP: Z\n");

        if (md_array_size(vis.sdf.structures) > 0) {
            const float angstrom_to_bohr = (float)(1.0 / 0.529177210903);

            // transformation matrix from world to volume
            mat4_t M = vis.sdf.matrices[0];
            const md_bitfield_t* bf = &vis.sdf.structures[0];
            const int num_atoms = (int)md_bitfield_popcount(bf);
            const int vol_dim[3] = {(int)attr->format.shape[0], (int)attr->format.shape[1], (int)attr->format.shape[2]};
            const float* values = volume_values;
            const double extent = vis.sdf.extent * 2.0 * angstrom_to_bohr;
            const double voxel_ext[3] = {
                (double)extent / (double)vol_dim[0],
                (double)extent / (double)vol_dim[1],
                (double)extent / (double)vol_dim[2],
            };

            const double half_ext = extent * 0.5;

            md_file_printf(file, "%5i %12.6f %12.6f %12.6f\n", -num_atoms, -half_ext, -half_ext, -half_ext);
            md_file_printf(file, "%5i %12.6f %12.6f %12.6f\n", vol_dim[0], voxel_ext[0], 0.0, 0.0);
            md_file_printf(file, "%5i %12.6f %12.6f %12.6f\n", vol_dim[1], 0.0, voxel_ext[1], 0.0);
            md_file_printf(file, "%5i %12.6f %12.6f %12.6f\n", vol_dim[2], 0.0, 0.0, voxel_ext[2]);

            const float scl = angstrom_to_bohr;
            M = mat4_mul(mat4_scale(scl, scl, scl), M);

            size_t beg_bit = bf->beg_bit;
            size_t end_bit = bf->end_bit;
            while ((beg_bit = md_bitfield_scan(bf, beg_bit, end_bit)) != 0) {
                size_t i = beg_bit - 1;
                vec3_t coord = md_state_coord(&state, i);
                coord = mat4_mul_vec3(M, coord, 1.0f);
                int anum     = md_atom_atomic_number(&sys.atom, i);
                float charge = (float)anum;
                md_file_printf(file, "%5i %12.6f %12.6f %12.6f %12.6f\n", anum, charge, coord.x, coord.y, coord.z);
            }

            // This entry somehow relates to the number of densities
            md_file_printf(file, "%5i %5i\n", 1, 1);

            // Write density data
            int count = 0;
            for (int x = 0; x < vol_dim[0]; ++x) {
                for (int y = 0; y < vol_dim[1]; ++y) {
                    for (int z = 0; z < vol_dim[2]; ++z) {
                        int idx = z * vol_dim[0] * vol_dim[1] + y * vol_dim[0] + x;
                        float val = values[idx];
                        md_file_printf(file, " %12.6E", val);
                        if (++count % 6 == 0) md_file_printf(file, "\n");
                    }
                }
            }
        }

        md_file_close(&file);
    } else {
        VIAMD_LOG_ERROR("Failed to visualize volume for export.");
        return false;
    }

    return true;
}

#define APPEND_BUF(buf, len, fmt, ...) (len += snprintf(buf + len, MAX(0, (int)sizeof(buf) - len), fmt, ##__VA_ARGS__) + 1)

static void draw_property_export_window(ApplicationState* data) {
    ASSERT(data);

    struct ExportFormat {
        str_t lbl;
        str_t ext;
    };

    ExportFormat table_formats[] {
        {STR_INIT("XVG"), STR_INIT("xvg")},
        {STR_INIT("CSV"), STR_INIT("csv")}
    };

    ExportFormat volume_formats[] {
        {STR_INIT("Gaussian Cube"), STR_INIT("cube")},
    };

    enum ExportKind {
        ExportKind_Temporal = 0,
        ExportKind_Distribution,
        ExportKind_Volume,
    };

    if (ImGui::Begin("Property Export", &data->show_property_export_window)) {
        static int kind = ExportKind_Temporal;
        static SeriesKey selected = {};
        static int table_format  = 0;
        static int volume_format = 0;
        static int bins_exp      = 7;   // 128 bins

        if (task_system::task_is_running(data->tasks.evaluate_full)) {
            ImGui::Text("The properties are currently being evaluated, please wait...");
            ImGui::End();
            return;
        }

        md_temp_scope_t temp = md_temp_begin_in(frame_alloc);
        defer { md_temp_end(temp); };
        md_allocator_i* alloc = md_temp_allocator(temp);

        // What there is to export of the chosen kind: the same series the plots offer
        md_array(SeriesKey) candidates = 0;
        {
            const uint32_t script_kinds =
                kind == ExportKind_Temporal     ? (uint32_t)MD_SCRIPT_PROPERTY_FLAG_TEMPORAL :
                kind == ExportKind_Distribution ? (uint32_t)(MD_SCRIPT_PROPERTY_FLAG_TEMPORAL | MD_SCRIPT_PROPERTY_FLAG_DISTRIBUTION) :
                                                  (uint32_t)MD_SCRIPT_PROPERTY_FLAG_VOLUME;
            auto push_script = [&](SeriesSource source) {
                series_for_each_script_property(data, source, script_kinds, [&](const SeriesKey& key) {
                    md_array_push(candidates, key, alloc);
                    const bool temporal = md_script_ir_property_flags(data->script.eval_ir, series_script_ident(key)) & MD_SCRIPT_PROPERTY_FLAG_TEMPORAL;
                    if (kind == ExportKind_Distribution && temporal && script_property_population(data, key) > 1) {
                        SeriesKey agg = key;
                        agg.variant = SeriesVariant_Aggregate;
                        md_array_push(candidates, agg, alloc);
                    }
                });
            };
            push_script(SeriesSource_Script);
            if (data->timeline.filter.enabled && kind != ExportKind_Temporal) {
                push_script(SeriesSource_ScriptFiltered);
            }
            if (kind != ExportKind_Volume) {
                str_t groups[64];
                const size_t num_groups = MIN(system_series_groups(groups, ARRAY_SIZE(groups), data), ARRAY_SIZE(groups));
                for (size_t g = 0; g < num_groups; ++g) {
                    const size_t num = system_series_members(nullptr, 0, data, groups[g]);
                    str_t* paths = md_temp_alloc_array(temp, str_t, num + 1);
                    system_series_members(paths, num, data, groups[g]);
                    for (size_t i = 0; i < num; ++i) {
                        md_array_push(candidates, series_key(SeriesSource_System, paths[i]), alloc);
                    }
                }
            }
        }

        bool selected_valid = false;
        for (size_t i = 0; i < md_array_size(candidates); ++i) {
            if (series_key_equal(candidates[i], selected)) { selected_valid = true; break; }
        }
        if (!selected_valid) {
            selected = md_array_size(candidates) ? candidates[0] : SeriesKey{};
            selected_valid = md_array_size(candidates) > 0;
        }

        ImGui::PushItemWidth(200);
        ImGui::Combo("Data Type", &kind, "Temporal\0Distribution\0Density Volume\0");
        ImGui::Separator();

        char selected_label[96] = "";
        if (selected_valid) {
            series_label(selected_label, sizeof(selected_label), data, selected);
        }
        if (ImGui::BeginCombo("Property", selected_label)) {
            for (size_t i = 0; i < md_array_size(candidates); ++i) {
                char label[96];
                series_label(label, sizeof(label), data, candidates[i]);
                ImGui::PushID((int)i);
                if (ImGui::Selectable(label, series_key_equal(candidates[i], selected))) {
                    selected = candidates[i];
                }
                if (ImGui::IsItemHovered() && candidates[i].source == SeriesSource_System) {
                    ImGui::SetTooltip("%s", candidates[i].path);
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (md_array_size(candidates) == 0) {
            ImGui::TextDisabled("Nothing of this kind, evaluate the script or load an energy file");
        }

        if (kind == ExportKind_Distribution) {
            char bins_fmt[32];
            snprintf(bins_fmt, sizeof(bins_fmt), "%d", 1 << bins_exp);
            ImGui::SliderInt("Num Bins", &bins_exp, 5, 10, bins_fmt);
        }

        str_t file_extension = {};
        if (kind == ExportKind_Volume) {
            if (ImGui::BeginCombo("File Format", volume_formats[volume_format].lbl.ptr)) {
                for (int i = 0; i < (int)ARRAY_SIZE(volume_formats); ++i) {
                    if (ImGui::Selectable(volume_formats[i].lbl.ptr, volume_format == i)) {
                        volume_format = i;
                    }
                }
                ImGui::EndCombo();
            }
            file_extension = volume_formats[volume_format].ext;
        } else {
            if (ImGui::BeginCombo("File Format", table_formats[table_format].lbl.ptr)) {
                for (int i = 0; i < (int)ARRAY_SIZE(table_formats); ++i) {
                    if (ImGui::Selectable(table_formats[i].lbl.ptr, table_format == i)) {
                        table_format = i;
                    }
                }
                ImGui::EndCombo();
            }
            file_extension = table_formats[table_format].ext;
        }

        if (!selected_valid) ImGui::PushDisabled();
        const bool export_clicked = ImGui::Button("Export");
        if (!selected_valid) ImGui::PopDisabled();
        ImGui::PopItemWidth();

        char path_buf[1024];
        if (export_clicked && application::file_dialog(path_buf, sizeof(path_buf), application::FileDialogFlag_Save, file_extension)) {
            const str_t path = {path_buf, strnlen(path_buf, sizeof(path_buf))};
            char label[96];
            series_label(label, sizeof(label), data, selected);

            if (kind == ExportKind_Volume) {
                SeriesVolumeView view;
                if (!series_resolve_volume(&view, data, selected)) {
                    VIAMD_LOG_ERROR("'%s' has no volume to export", label);
                } else if (str_eq(file_extension, STR_LIT("cube")) && export_cube(*data, view.attr, view.vis_payload, path)) {
                    VIAMD_LOG_SUCCESS("Successfully exported property '%s' to '" STR_FMT "'", label, STR_ARG(path));
                }
            } else {
                // Columns in the units the plots show them in, so a column and its axis label agree
                md_array(const float*) column_data   = 0;
                md_array(str_t)        column_labels = 0;
                md_array(str_t)        legends       = 0;
                str_t x_label = {};
                str_t y_label = str_from_cstr(label);
                size_t num_rows = 0;
                bool ok = false;

                auto push_columns = [&](int dim, auto value) {
                    for (int k = 0; k < dim; ++k) {
                        float* col = md_temp_alloc_array(temp, float, num_rows);
                        for (size_t r = 0; r < num_rows; ++r) {
                            col[r] = value(r, k);
                        }
                        md_array_push(column_data, col, alloc);
                        if (dim > 1) {
                            const str_t legend = str_printf(alloc, "%s[%i]", label, k + 1);
                            md_array_push(legends, legend, alloc);
                            md_array_push(column_labels, legend, alloc);
                        } else {
                            md_array_push(column_labels, y_label, alloc);
                        }
                    }
                };

                if (kind == ExportKind_Temporal) {
                    SeriesTemporalView view;
                    if (series_resolve_temporal(&view, data, selected)) {
                        num_rows = (size_t)view.num_samples;
                        char time_unit_str[32] = "";
                        md_unit_print(time_unit_str, sizeof(time_unit_str), data->timeline.time_unit);
                        x_label = md_unit_is_none(data->timeline.time_unit) ? STR_LIT("Frame") : str_printf(alloc, "Time (%s)", time_unit_str);
                        if (view.unit_str[0]) y_label = str_printf(alloc, "%s (%s)", label, view.unit_str);

                        float* x = md_temp_alloc_array(temp, float, num_rows);
                        MEMCPY(x, view.x, num_rows * sizeof(float));
                        md_array_push(column_data, x, alloc);
                        md_array_push(column_labels, x_label, alloc);
                        push_columns(view.dim, [&](size_t r, int k) { return (float)series_temporal_point(view, (int)r, k).y; });
                        ok = true;
                    }
                } else {
                    SeriesHistogramView view;
                    if (series_resolve_histogram(&view, data, selected, 1 << bins_exp)) {
                        num_rows = (size_t)view.num_bins;
                        x_label = str_copy(str_from_cstr(view.x_unit_str), alloc);
                        if (view.y_unit_str[0]) y_label = str_printf(alloc, "%s (%s)", label, view.y_unit_str);

                        // The centre of each bin, where the plot draws it
                        float* x = md_temp_alloc_array(temp, float, num_rows);
                        for (size_t r = 0; r < num_rows; ++r) {
                            x[r] = (float)series_histogram_point(view, (int)r, 0).x;
                        }
                        md_array_push(column_data, x, alloc);
                        md_array_push(column_labels, x_label, alloc);
                        push_columns(view.dim, [&](size_t r, int k) { return (float)series_histogram_point(view, (int)r, k).y; });
                        ok = true;
                    }
                }

                if (!ok) {
                    VIAMD_LOG_ERROR("'%s' has nothing to export right now", label);
                } else {
                    str_t out_str = {};
                    if (str_eq(file_extension, STR_LIT("xvg"))) {
                        str_t header = md_xvg_format_header(str_from_cstr(label), x_label, y_label, md_array_size(legends), legends, alloc);
                        out_str = md_xvg_format(header, md_array_size(column_data), num_rows, column_data, alloc);
                    } else if (str_eq(file_extension, STR_LIT("csv"))) {
                        out_str = md_csv_write_to_str(column_data, column_labels, md_array_size(column_data), num_rows, alloc);
                    }
                    md_file_t file = {0};
                    if (!str_empty(out_str) && md_file_open(&file, path, MD_FILE_WRITE | MD_FILE_CREATE | MD_FILE_TRUNCATE)) {
                        md_file_write(file, out_str.ptr, out_str.len);
                        md_file_close(&file);
                        VIAMD_LOG_SUCCESS("Successfully exported property '%s' to '" STR_FMT "'", label, STR_ARG(path));
                    } else {
                        VIAMD_LOG_ERROR("Failed to write '" STR_FMT "'", STR_ARG(path));
                    }
                }
            }
        }
    }
    ImGui::End();
}

md_array(int) extract_bitfield_indices(const md_bitfield_t* bf, md_allocator_i* alloc) {
    size_t count = md_bitfield_popcount(bf);
    md_array(int) indices = md_array_create(int, count, alloc);
    md_bitfield_iter_extract_indices(indices, count, md_bitfield_iter_create(bf));
    return indices;
}

static void xyz_write_frame(md_file_t file, const int* atomic_nr, const md_system_state_t* state, const int32_t* atom_indices, size_t num_atoms) {
    md_file_printf(file, "%zu\n", num_atoms);
    md_file_printf(file, "XYZ format exported from VIAMD\n");
    for (size_t i = 0; i < num_atoms; ++i) {
        int idx = atom_indices ? atom_indices[i] : (int)i;
        md_file_printf(file, "%-2d %12.6f %12.6f %12.6f\n", atomic_nr[idx], state->xyz[idx].x, state->xyz[idx].y, state->xyz[idx].z);
    }
}

void draw_structure_export_window(ApplicationState* data) {
    ASSERT(data);

    if (ImGui::Begin("Structure Export", &data->structure_export.show_window)) {
        md_temp_scope_t temp = md_temp_begin_in(frame_alloc);
        defer { md_temp_end(temp); };

        const md_system_t* sys = &data->mold.sys;
        const md_system_state_t* state = &data->mold.state;
        const bool traj = run_num_frames(data) > 0;
        auto& struct_exp = data->structure_export;

        static const char* atom_mask_options[] = {
            "Active Selection",
            "Visible Atoms",
            "Query",
            "All Atoms",
        };

        static const char* frame_mask_options[] = {
            "All Frames",
            "Current Frame",
            "Active Frame Range"
        };

        static const char* file_formats[] = {
            "xyz",
            "pdb"
        };

        //ImGui::PushItemWidth(200);

        struct_exp.selected_atom_filter = CLAMP(struct_exp.selected_atom_filter, 0, (int)ARRAY_SIZE(atom_mask_options) - 1);
        ImGui::Combo("Atoms to Export", &struct_exp.selected_atom_filter, atom_mask_options, (int)ARRAY_SIZE(atom_mask_options));

        if (struct_exp.selected_atom_filter == 2) { // Query
            auto& query = struct_exp.query;
            bool valid = query.is_valid;

            if (!md_bitfield_validate(&query.mask)) {
                md_bitfield_init(&query.mask, persistent_alloc);
            }
            
            if (!valid) ImGui::PushInvalid();
            ImGui::InputQuery("Query", query.buf, sizeof(query.buf), query.is_valid, query.error);
            if (!valid) ImGui::PopInvalid();
            
            query.requires_evaluation |= ImGui::IsItemEdited();
            bool preview = ImGui::IsItemFocused() || ImGui::IsItemHovered();
            
            if (query.requires_evaluation) {
                md_bitfield_clear(&query.mask);
                query.is_valid = md_filter(
                    &query.mask,
                    str_from_cstr(query.buf),
                    &data->mold.sys,
                    &data->mold.state,
                    data->script.ir,
                    &query.is_dynamic,
                    query.error,
                    sizeof(query.error)
                );
                query.requires_evaluation = false;
            }

            if (ImGui::IsWindowHovered()) {
                // Clear highlight mask
                md_bitfield_clear(&data->selection.highlight_mask);
            }

            if (query.is_valid && preview) {
                md_bitfield_copy(&data->selection.highlight_mask, &query.mask);
            }
        }

        if (traj) {
            struct_exp.selected_traj_filter = CLAMP(struct_exp.selected_traj_filter, 0, (int)ARRAY_SIZE(frame_mask_options) - 1);
            ImGui::Combo("Frames to Export", &struct_exp.selected_traj_filter, frame_mask_options, (int)ARRAY_SIZE(frame_mask_options));
        }

        struct_exp.selected_file_format = CLAMP(struct_exp.selected_file_format, 0, (int)ARRAY_SIZE(file_formats) - 1);
        ImGui::Combo("File Format", &struct_exp.selected_file_format, file_formats, (int)ARRAY_SIZE(file_formats));

        //ImGui::PopItemWidth();

        auto extract_atom_indices = [data](int* out_atom_indices, size_t atom_index_cap, const md_system_state_t* state) -> size_t {
            switch (data->structure_export.selected_atom_filter) {
                case 0: { // Active Selection
                    return md_bitfield_iter_extract_indices(out_atom_indices, atom_index_cap, md_bitfield_iter_create(&data->selection.selection_mask));
                } break;
                case 1: { // Visible Atoms
                    return md_bitfield_iter_extract_indices(out_atom_indices, atom_index_cap, md_bitfield_iter_create(&data->representation.visibility_mask));
                } break;
                case 2: { // Query
                    if (data->structure_export.query.is_dynamic) {
                        // IMPORTANT: Clear the previous mask before re-evaluating a dynamic query.
                        // Otherwise atoms that drop out in subsequent frames remain set from earlier frames.
                        md_bitfield_clear(&data->structure_export.query.mask);
                        data->structure_export.query.is_valid = md_filter(
                            &data->structure_export.query.mask,
                            str_from_cstr(data->structure_export.query.buf),
                            &data->mold.sys,
                            state,
                            data->script.ir,
                            &data->structure_export.query.is_dynamic,
                            data->structure_export.query.error,
                            sizeof(data->structure_export.query.error)
                        );
                    }
                    if (!data->structure_export.query.is_valid) {
                        VIAMD_LOG_ERROR("Cannot export structure, the query expression is invalid: " STR_FMT, STR_ARG(str_from_cstr(data->structure_export.query.error)));
                        return 0;
                    } else {
                        return md_bitfield_iter_extract_indices(out_atom_indices, atom_index_cap, md_bitfield_iter_create(&data->structure_export.query.mask));
                    }
                } break;
                case 3: { // All Atoms
                    for (size_t i = 0; i < data->mold.sys.atom.count && i < atom_index_cap; ++i) {
                        out_atom_indices[i] = (int)i;
                    }
                    return data->mold.sys.atom.count;
                } break;
                default: {
                    VIAMD_LOG_ERROR("Invalid atom filter selection.");
                } break;
            }
            return 0;
        };

        if (ImGui::Button("Export")) {
            md_array(int32_t) frame_indices = 0;
            if (traj) {
                switch (struct_exp.selected_traj_filter) {
                case 0: { // All Frames
                    size_t num_frames = run_num_frames(data);
                    for (size_t i = 0; i < num_frames; ++i) {
                        md_array_push(frame_indices, (int32_t)i, frame_alloc);
                    }
                } break;
                case 1: { // Current Frame
                    int32_t current_frame = (int)(data->animation.frame + 0.5); // Round to nearest
                    md_array_push(frame_indices, current_frame, frame_alloc);
                } break;
                case 2: { // Active Frame Range
                    int beg = data->timeline.filter.beg_frame;
                    int end = data->timeline.filter.end_frame;
                    for (int i = beg; i <= end; ++i) {
                        md_array_push(frame_indices, (int32_t)i, frame_alloc);
                    }
                } break;
                default: {
                    VIAMD_LOG_ERROR("Invalid trajectory filter selection.");
                } break;
                }
            }

            char path_buf[1024] = { 0 };
            str_t ext = str_from_cstr(file_formats[struct_exp.selected_file_format]);
            if (application::file_dialog(path_buf, sizeof(path_buf), application::FileDialogFlag_Save, ext)) {
                str_t path = str_from_cstrn(path_buf, sizeof(path_buf));
                md_file_t file = {0};
                if (md_file_open(&file, path, MD_FILE_WRITE | MD_FILE_CREATE | MD_FILE_TRUNCATE)) {
                    // Write header for file format
                    switch (struct_exp.selected_file_format) {
                        case 0: { // XYZ
                            // No header needed
                        } break;
                        case 1: { // PDB
                            md_file_printf(file, "HEADER    EXPORTED FROM VIAMD\n");
                            md_file_printf(file, "REMARK    Exported from VIAMD\n");
                            
                            // Write CRYST1 record with unit cell if available
                            if (traj) {
                                // Coordinates are not needed, only the cell of the first exported
                                // frame, which is delivered on the state.
                                md_system_state_t frame_state = {};
                                int first_frame = md_array_size(frame_indices) > 0 ? frame_indices[0] : 0;
                                if (extract_frame(data, first_frame, &frame_state)) {
                                    if (frame_state.unitcell.flags != 0) {
                                        double a, b, c, alpha, beta, gamma;
                                        md_unitcell_extract_extent_angles(&a, &b, &c, &alpha, &beta, &gamma, &frame_state.unitcell);
                                        md_file_printf(file, "CRYST1%9.3f%9.3f%9.3f%7.2f%7.2f%7.2f P 1           1\n", a, b, c, alpha, beta, gamma);
                                    }
                                }
                            } else if (state->unitcell.flags != 0) {
                                double a, b, c, alpha, beta, gamma;
                                md_unitcell_extract_extent_angles(&a, &b, &c, &alpha, &beta, &gamma, &state->unitcell);
                                md_file_printf(file, "CRYST1%9.3f%9.3f%9.3f%7.2f%7.2f%7.2f P 1           1\n", a, b, c, alpha, beta, gamma);
                            }
                        } break;
                        default: {
                            VIAMD_LOG_ERROR("Unsupported export format: '" STR_FMT "'", STR_ARG(ext));
                        } break;
                    }

                    // Write body
                    md_array(int32_t) atom_indices = 0;
                    md_array_ensure(atom_indices, sys->atom.count, frame_alloc);

                    md_array(int) atomic_numbers = md_array_create(int, sys->atom.count, frame_alloc);
                    for (size_t i = 0; i < sys->atom.count; ++i) {
                        atomic_numbers[i] = md_atom_atomic_number(&sys->atom, i);
                    }

                    size_t num_frames = md_array_size(frame_indices);
                    if (num_frames > 0) {
						md_system_state_t frame_state = { .alloc = frame_alloc };
                        md_system_state_init(&frame_state, sys->atom.count);

                        // Many frames: one context for all of them, so the run's files stay open.
                        const str_t paths[] = { STR_INIT("atom/position"), STR_INIT("unitcell") };
                        md_system_extract_t* ex = md_system_extract_begin(sys, str_from_cstr(data->mold.run), paths, ARRAY_SIZE(paths), md_get_heap_allocator());
                        defer { md_system_extract_end(ex); };

                        for (size_t f = 0; f < num_frames; ++f) {
                            int frame_idx = frame_indices[f];
                            if (!ex || !md_system_extract_frame(ex, frame_idx, &frame_state)) {
                                VIAMD_LOG_ERROR("Failed to load frame %d from trajectory for structure export.", frame_idx);
                                continue;
                            }

                            size_t num_atoms = extract_atom_indices(atom_indices, md_array_capacity(atom_indices), &frame_state);
                            
                            if (struct_exp.selected_file_format == 0) { // XYZ
                                xyz_write_frame(file, atomic_numbers, &frame_state, atom_indices, num_atoms);
                            } else if (struct_exp.selected_file_format == 1) { // PDB
                                int model_num = (num_frames > 1) ? (int)(f + 1) : 0;
                                md_pdb_system_write_state_to_file(file, sys, &frame_state, atom_indices, num_atoms, model_num);
                            }
                        }
                    }
                    else {
                        // Single frame from system
                        size_t num_atoms = extract_atom_indices(atom_indices, md_array_capacity(atom_indices), &data->mold.state);
                        
                        if (struct_exp.selected_file_format == 0) { // XYZ
                            xyz_write_frame(file, atomic_numbers, state, atom_indices, num_atoms);
                        } else if (struct_exp.selected_file_format == 1) { // PDB
                            md_pdb_system_write_state_to_file(file, sys, state, atom_indices, num_atoms, 0);
                        }
                    }

                    VIAMD_LOG_INFO("Successfully exported structure to: '" STR_FMT "'", STR_ARG(path));
                    md_file_close(&file);
                } else {
                    VIAMD_LOG_ERROR("Failed to open file '" STR_FMT "' for writing.", STR_ARG(path));
                }
            }
        }
    }
    ImGui::End();
}

static void update_md_buffers(ApplicationState* data) {
    ASSERT(data);
    const md_system_t& sys = data->mold.sys;
	const md_system_state_t& state = data->mold.state;

    if (sys.atom.count == 0) return;

    if (data->mold.dirty_gpu_buffers & MolBit_DirtyPosition) {
        vec3_t pbc_ext = { 0 };
        md_unitcell_diag_extract_float(pbc_ext.elem, &state.unitcell);
        md_gl_mol_set_atom_position(data->mold.gl_mol, 0, (uint32_t)state.num_atoms, state.xyz);
        if (!(data->mold.dirty_gpu_buffers & MolBit_ClearVelocity)) {
            md_gl_mol_compute_velocity(data->mold.gl_mol, pbc_ext.elem);
        }
#if EXPERIMENTAL_GFX_API
        md_gfx_structure_set_atom_position(data->mold.gfx_structure, state.xyz, (uint32_t)state.num_atoms, 0);
        md_gfx_structure_set_aabb(data->mold.gfx_structure, &data->mold.sys_aabb_min, &data->mold.sys_aabb_max);
#endif
    }

    if (data->mold.dirty_gpu_buffers & MolBit_ClearVelocity) {
        md_gl_mol_zero_velocity(data->mold.gl_mol);
    }

    if (data->mold.dirty_gpu_buffers & MolBit_DirtyRadius) {
        md_temp_scope_t tmp = md_temp_begin_in(frame_alloc);
        defer { md_temp_end(tmp); };

        float* radii = (float*)md_vm_arena_push(frame_alloc, sys.atom.count * sizeof(float));
        md_atom_extract_radii(radii, 0, sys.atom.count, &sys.atom);

        md_gl_mol_set_atom_radius(data->mold.gl_mol, 0, (uint32_t)sys.atom.count, radii, 0);
#if EXPERIMENTAL_GFX_API
        md_gfx_structure_set_atom_radius(data->mold.gfx_structure, 0, (uint32_t)sys.atom.count, sys.atom.radius, 0);
#endif
    }

    if (data->mold.dirty_gpu_buffers & MolBit_DirtyFlags) {
        md_temp_scope_t tmp = md_temp_begin_in(frame_alloc);
        defer { md_temp_end(tmp); };

        uint8_t* flags = (uint8_t*)md_vm_arena_push(frame_alloc, sys.atom.count * sizeof(uint8_t));
        MEMSET(flags, 0, sys.atom.count * sizeof(uint8_t));

        {
            md_bitfield_iter_t it = md_bitfield_iter_create(&data->selection.highlight_mask);
            while (md_bitfield_iter_next(&it)) {
                uint64_t idx = md_bitfield_iter_idx(&it);
                flags[idx] |= AtomBit_Highlighted;
            }
        }
        {
            md_bitfield_iter_t it = md_bitfield_iter_create(&data->selection.selection_mask);
            while (md_bitfield_iter_next(&it)) {
                uint64_t idx = md_bitfield_iter_idx(&it);
                flags[idx] |= AtomBit_Selected;
            }
        }
        {
            md_bitfield_iter_t it = md_bitfield_iter_create(&data->representation.visibility_mask);
            while (md_bitfield_iter_next(&it)) {
                uint64_t idx = md_bitfield_iter_idx(&it);
                flags[idx] |= AtomBit_Visible;
            }
        }
        md_gl_mol_set_atom_flags(data->mold.gl_mol, 0, (uint32_t)sys.atom.count, flags, 0);
    }

    if (data->mold.dirty_gpu_buffers & MolBit_DirtyBonds) {
        md_gl_mol_set_bonds(data->mold.gl_mol, 0, (uint32_t)sys.bond.count, sys.bond.pairs, sizeof(md_atom_pair_t));
    }

    if (data->mold.dirty_gpu_buffers & MolBit_DirtySecondaryStructure) {
        const md_gl_secondary_structure_t* ss_arr = data->mold.interpolated_properties.secondary_structure;
        size_t ss_len = md_array_size(ss_arr);
        if (ss_len > 0) {
            md_gl_mol_set_backbone_secondary_structure(data->mold.gl_mol, 0, (uint32_t)ss_len, ss_arr, 0);
        }
    }

    data->mold.dirty_gpu_buffers = 0;
}

void create_screenshot(str_t path) {
    md_temp_scope_t tmp = md_temp_begin_in(frame_alloc);
    defer { md_temp_end(tmp); };

    int viewport[4] = {};
    int fbo = 0;
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);

    int width  = viewport[2];
    int height = viewport[3];

    size_t bytes = width * height * sizeof(uint32_t);
    uint32_t* rgba = (uint32_t*)md_vm_arena_push(frame_alloc, bytes);
    defer { md_vm_arena_pop(frame_alloc, bytes); };

    GLenum read_buffer = fbo ? GL_COLOR_ATTACHMENT0 : GL_BACK;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glReadBuffer(read_buffer);

    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    {
        // @NOTE: Swap Rows to flip image with respect to y-axis
        const uint32_t row_byte_size = width * sizeof(uint32_t);
        uint32_t* row_t = (uint32_t*)md_alloc(frame_alloc, row_byte_size);
        defer { md_free(frame_alloc, row_t, row_byte_size); };
        for (uint32_t i = 0; i < (uint32_t)height / 2; ++i) {
            uint32_t* row_a = rgba + i * width;
            uint32_t* row_b = rgba + (height - 1 - i) * width;
            if (row_a != row_b) {
                MEMCPY(row_t, row_a, row_byte_size);  // tmp = a;
                MEMCPY(row_a, row_b, row_byte_size);  // a = b;
                MEMCPY(row_b, row_t, row_byte_size);  // b = tmp;
            }
        }
    }

    str_t ext = {};
    extract_ext(&ext, path);

    if (str_eq_cstr_ignore_case(ext, "jpg")) {
        const int quality = 95;
        image_write_jpg(path, rgba, width, height, quality);
    } else if (str_eq_cstr_ignore_case(ext, "png")) {
        image_write_png(path, rgba, width, height);
    } else if (str_eq_cstr_ignore_case(ext, "bmp")) {
        image_write_bmp(path, rgba, width, height);
    } else {
        VIAMD_LOG_ERROR("Non supported file-extension '" STR_FMT "' when saving screenshot", STR_ARG(ext));
        return;
    }

    VIAMD_LOG_SUCCESS("Screenshot saved to: '" STR_FMT "'", STR_ARG(path));
}

// Length of the movie in seconds
static double movie_duration(const ApplicationState* state) {
    if (state->movie.duration_auto) {
        const double fps = fabs((double)state->animation.fps);
        return fps > 0.0 ? fabs(state->movie.end_frame - state->movie.start_frame) / fps : 0.0;
    }
    return (double)state->movie.duration;
}

static int movie_num_frames(const ApplicationState* state) {
    return (int)floor(movie_duration(state) * (double)state->movie.fps + 1.0e-6) + 1;
}

// Trajectory frame shown at a time on the movie timeline: from the keyframes that have one, otherwise
// the trajectory plays linearly between the times of the movie's timeline settings
static double movie_trajectory_frame(const ApplicationState* state, double time) {
    double keyed;
    if (camera_keyframes_evaluate_frame(&keyed, state->movie.keyframes, md_array_size(state->movie.keyframes), time)) {
        const double last = (double)(run_num_frames(state) > 0 ? run_num_frames(state) - 1 : 0);
        return CLAMP(keyed, 0.0, last);
    }
    const double t0 = state->movie.duration_auto ? 0.0 : (double)state->movie.traj_begin;
    const double t1 = state->movie.duration_auto ? movie_duration(state) : (double)state->movie.traj_end;
    const double u = t1 > t0 ? CLAMP((time - t0) / (t1 - t0), 0.0, 1.0) : (time < t0 ? 0.0 : 1.0);
    return state->movie.start_frame + (state->movie.end_frame - state->movie.start_frame) * u;
}

static void movie_sort_keyframes(ApplicationState* state) {
    std::stable_sort(state->movie.keyframes, state->movie.keyframes + md_array_size(state->movie.keyframes),
        [](const CameraKeyframe& a, const CameraKeyframe& b) { return a.time < b.time; });
}

// The look parameters that can be keyed. The id is what a key stores and what a workspace saves, so an
// id never changes meaning: new parameters go at the end.
enum MovieParamId : int {
    MovieParam_BackgroundColor = 0,
    MovieParam_BackgroundIntensity,
    MovieParam_SsaoIntensity,
    MovieParam_SsaoRadius,
    MovieParam_Exposure,
    MovieParam_DofStrength,
    MovieParam_NearClip,
    MovieParam_FarClip,
    MovieParam_FocusDistance,
};

struct MovieParamDesc {
    int         id;
    const char* label;
    int         comps;       // 3 for a colour
    bool        color;
    float       lo, hi;
    bool        log;         // Shown on a logarithmic axis, where the range spans orders of magnitude
    float*    (*ptr)(ApplicationState*);
    const char* tip;
};

static const MovieParamDesc movie_param_table[] = {
    { MovieParam_BackgroundColor,     "Background color",     3, true,  0.0f, 1.0f,     false, [](ApplicationState* s) { return s->visuals.background.color.elem; }, nullptr },
    { MovieParam_BackgroundIntensity, "Background intensity", 1, false, 0.0f, 100.0f,   false, [](ApplicationState* s) { return &s->visuals.background.intensity; }, nullptr },
    { MovieParam_SsaoIntensity,       "Ambient occlusion",    1, false, 0.0f, 10.0f,    false, [](ApplicationState* s) { return &s->visuals.ssao.intensity; }, "The intensity of the ambient occlusion. It has to be enabled." },
    { MovieParam_SsaoRadius,          "Occlusion radius",     1, false, 0.1f, 50.0f,    true,  [](ApplicationState* s) { return &s->visuals.ssao.radius; }, nullptr },
    { MovieParam_Exposure,            "Exposure",             1, false, 0.05f, 10.0f,   true,  [](ApplicationState* s) { return &s->visuals.tonemapping.exposure; }, "The exposure of the tonemapping. It has to be enabled." },
    { MovieParam_DofStrength,         "Depth of field blur",  1, false, 0.001f, 100.0f, true,  [](ApplicationState* s) { return &s->visuals.dof.focus_scale; }, "The blur strength. Depth of field has to be enabled; it focuses on what the camera looks at." },
    { MovieParam_NearClip,            "Near clipping plane",  1, false, 0.01f, 5000.0f, true,  [](ApplicationState* s) { return &s->view.camera.near_plane; }, "Distance from the camera to where things start to show. Raise it to cut into the structure." },
    { MovieParam_FarClip,             "Far clipping plane",   1, false, 1.0f, 100000.0f,true,  [](ApplicationState* s) { return &s->view.camera.far_plane; }, "Distance from the camera to where things stop showing." },
    { MovieParam_FocusDistance,       "Focus distance",       1, false, 0.01f, 1000.0f, true,  [](ApplicationState* s) { return &s->visuals.dof.focus_distance; }, "How far from the camera depth of field is sharp, so focus can be pulled during the movie. Depth of field has to be enabled and its Focus set to Distance." },
};
static_assert(sizeof(movie_param_table) / sizeof(movie_param_table[0]) <= MOVIE_MAX_PARAMS, "more parameters than MOVIE_MAX_PARAMS");

static const MovieParamDesc* movie_param_desc(int id) {
    for (const MovieParamDesc& d : movie_param_table) {
        if (d.id == id) return &d;
    }
    return nullptr;
}

static const char* key_ease_str[(int)KeyEase::Count] = {
    "Smooth",
    "Ease in/out",
    "Linear",
    "Hold",
};

// Puts the keyed parameters at their values at a time. A parameter is taken hold of when it first has a key
// to follow, and what it was is kept, to be put back when the keys let go of it or the recording is over.
static void movie_params_apply(ApplicationState* state, double time) {
    auto& m = state->movie;
    for (const MovieParamDesc& d : movie_param_table) {
        float v[3] = {};
        const bool keyed = m.animate_params && param_keys_evaluate(v, d.comps, m.param_keys.data(), m.param_keys.size(), d.id, time);
        float* dst = d.ptr(state);
        if (keyed) {
            if (!m.param_saved_valid[d.id]) {
                for (int c = 0; c < d.comps; ++c) m.param_saved[d.id][c] = dst[c];
                m.param_saved_valid[d.id] = true;
            }
            for (int c = 0; c < d.comps; ++c) dst[c] = v[c];
        } else if (m.param_saved_valid[d.id]) {
            for (int c = 0; c < d.comps; ++c) dst[c] = m.param_saved[d.id][c];
            m.param_saved_valid[d.id] = false;
        }
    }
}

static void movie_params_restore(ApplicationState* state) {
    auto& m = state->movie;
    for (const MovieParamDesc& d : movie_param_table) {
        if (!m.param_saved_valid[d.id]) continue;
        float* dst = d.ptr(state);
        for (int c = 0; c < d.comps; ++c) dst[c] = m.param_saved[d.id][c];
        m.param_saved_valid[d.id] = false;
    }
}

// Where the follow target is now, in the space the camera is in. False when there is no target, or it
// holds atoms that are not in the system.
static bool movie_follow_center(const ApplicationState* state, vec3_t* out) {
    const md_bitfield_t* mask = &state->movie.follow_mask;
    const size_t count = md_bitfield_popcount(mask);
    const size_t num_atoms = state->mold.sys.atom.count;
    if (count == 0 || state->mold.state.num_atoms != num_atoms) return false;
    uint64_t first = 0, last = 0;
    if (!md_bitfield_get_range(&first, &last, mask) || last >= num_atoms) return false;

    md_temp_scope_t temp = md_temp_begin_in(state->allocator.frame);
    defer { md_temp_end(temp); };
    vec4_t* xyzw = md_temp_alloc_array(temp, vec4_t, count);
    md_util_system_extract_xyzw_from_mask(xyzw, mask, &state->mold.sys, &state->mold.state);

    vec3_t com = vec3_zero();
    md_util_deperiodize_self_vec4(xyzw, count, &state->mold.state.unitcell, &com);
    *out = mat4_mul_vec3(state->mold.unitcell_transform, com, 1.0f);
    return true;
}

// Where an atom is now, in the space the camera is in
static bool movie_atom_position(const ApplicationState* state, int32_t atom, vec3_t* out) {
    if (atom < 0 || (size_t)atom >= state->mold.sys.atom.count || state->mold.state.num_atoms != state->mold.sys.atom.count) return false;
    *out = mat4_mul_vec3(state->mold.unitcell_transform, state->mold.state.xyz[atom], 1.0f);
    return true;
}

// Makes a keyframe look at an atom and track it through the trajectory. The eye stays where it is.
static bool movie_key_look_at_atom(ApplicationState* state, int key_idx, int32_t atom) {
    auto& m = state->movie;
    vec3_t pos;
    if (key_idx < 0 || key_idx >= (int)md_array_size(m.keyframes) || !movie_atom_position(state, atom, &pos)) return false;
    CameraKeyframe& key = m.keyframes[key_idx];
    ViewTransform t = key.transform;
    if (!camera_aim_at(&t, pos)) return false;
    key.transform = t;
    key.follow = true;
    key.follow_atom = atom;
    key.follow_center = pos;
    return true;
}

// How far from the camera depth of field is sharp, for a camera, from the focus mode
static float dof_focus_depth(const ApplicationState* state, const ViewTransform& view) {
    const auto& dof = state->visuals.dof;
    float depth = view.distance;
    if (dof.focus_mode == DofFocusMode::Distance) {
        depth = dof.focus_distance;
    } else if (dof.focus_mode == DofFocusMode::Target) {
        vec3_t center;
        if (movie_follow_center(state, &center)) depth = camera_depth_of_point(view, center);
    }
    return MAX(depth, 1.0e-3f);
}

static bool movie_keys_follow(const CameraKeyframe* keys, size_t num_keys) {
    for (size_t i = 0; i < num_keys; ++i) {
        if (keys[i].follow) return true;
    }
    return false;
}

static bool movie_keys_track_atoms(const CameraKeyframe* keys, size_t num_keys) {
    for (size_t i = 0; i < num_keys; ++i) {
        if (keys[i].follow && keys[i].follow_atom >= 0) return true;
    }
    return false;
}

static void movie_camera_apply(ApplicationState* state, double time, const CameraKeyframe* keys, size_t num_keys, const vec3_t* follow_now) {
    ViewTransform vt;
    float fov_y;
    // Keys that look at an atom of their own are moved by where that atom is now. One that cannot be found stays put.
    std::vector<vec3_t> atom_now;
    if (movie_keys_track_atoms(keys, num_keys)) {
        atom_now.resize(num_keys);
        for (size_t i = 0; i < num_keys; ++i) {
            atom_now[i] = keys[i].follow_center;
            if (keys[i].follow && keys[i].follow_atom >= 0 ) movie_atom_position(state, keys[i].follow_atom, &atom_now[i]);
        }
    }
    camera_keyframes_evaluate(&vt, &fov_y, keys, num_keys, time, state->movie.loop, follow_now, atom_now.empty() ? nullptr : atom_now.data());
    state->view.target = vt;
    state->view.camera = vt;
    state->view.camera.fov_y = fov_y;
}

// Both view targets are set so the exponential smoothing in camera_animate does not lag behind.
// The keys must be sorted by time.
static void movie_apply_time_with_keys(ApplicationState* state, double time, bool apply_camera, const CameraKeyframe* keys, size_t num_keys) {
    auto& m = state->movie;
    state->animation.frame = movie_trajectory_frame(state, time);
    movie_params_apply(state, time);
    m.follow_pending = false;
    if (apply_camera && m.animate_camera && num_keys > 0) {
        if (movie_keys_track_atoms(keys, num_keys) || (movie_keys_follow(keys, num_keys) && !md_bitfield_empty(&m.follow_mask))) {
            // The target is not where it will be until the trajectory frame has been loaded, so the camera waits for that
            m.follow_keys.assign(keys, keys + num_keys);
            m.follow_time = time;
            m.follow_pending = true;
        } else {
            movie_camera_apply(state, time, keys, num_keys, nullptr);
        }
    }
}

// Runs once the frame of the movie's time is in the system state: puts the camera where the keys say, around the target
static void movie_follow_update(ApplicationState* state) {
    auto& m = state->movie;
    if (!m.follow_pending) return;
    m.follow_pending = false;
    vec3_t center;
    const bool have = movie_follow_center(state, &center);
    movie_camera_apply(state, m.follow_time, m.follow_keys.data(), m.follow_keys.size(), have ? &center : nullptr);
}

// Shows the movie at a time on its timeline: the trajectory frame and, if enabled, the camera.
static void movie_apply_time(ApplicationState* state, double time, bool apply_camera) {
    movie_apply_time_with_keys(state, time, apply_camera, state->movie.keyframes, md_array_size(state->movie.keyframes));
}

static void movie_restore_state(ApplicationState* state) {
    state->animation.mode = state->movie.prev_playback_mode;
    state->movie.follow_pending = false;
    movie_params_restore(state);
    if (state->movie.camera_was_animated) {
        state->view.target = state->movie.prev_view_target;
        state->view.camera = state->movie.prev_view_target;
        state->view.camera.fov_y = state->movie.prev_fov_y;
        state->movie.camera_was_animated = false;
    }
}

static void movie_frame_size(const ApplicationState* state, int* w, int* h) {
    switch (state->movie.resolution) {
    case ScreenshotResolution::Window:
        *w = (int)state->app.framebuffer.width;
        *h = (int)state->app.framebuffer.height;
        break;
    case ScreenshotResolution::FHD:    *w = 1920; *h = 1080; break;
    case ScreenshotResolution::QHD:    *w = 2560; *h = 1440; break;
    case ScreenshotResolution::UHD_4K: *w = 3840; *h = 2160; break;
    case ScreenshotResolution::UHD_8K: *w = 7680; *h = 4320; break;
    default:
        *w = CLAMP(state->movie.res_x, 640, 16384);
        *h = CLAMP(state->movie.res_y, 480, 16384);
        break;
    }
}

static void movie_pbo_free(ApplicationState* state) {
    auto& m = state->movie;
    for (int i = 0; i < MOVIE_RING_SIZE; ++i) {
        if (m.fence[i]) {
            glDeleteSync(m.fence[i]);
            m.fence[i] = nullptr;
        }
    }
    if (m.pbo[0]) {
        glDeleteBuffers(MOVIE_RING_SIZE, m.pbo);
        for (int i = 0; i < MOVIE_RING_SIZE; ++i) m.pbo[i] = 0;
    }
    m.pbo_head = 0;
    m.pbo_count = 0;
    m.pbo_bytes = 0;
}

static void movie_pbo_alloc(ApplicationState* state, size_t bytes) {
    movie_pbo_free(state);
    auto& m = state->movie;
    glGenBuffers(MOVIE_RING_SIZE, m.pbo);
    for (int i = 0; i < MOVIE_RING_SIZE; ++i) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m.pbo[i]);
        glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)bytes, nullptr, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    m.pbo_bytes = bytes;
}

// Hands the oldest read back frame to the sink. Without wait it only does so if the GPU is done with
// it and the sink has a buffer to spare, otherwise it leaves everything as it was and returns false.
static bool movie_pbo_pop(ApplicationState* state, bool wait) {
    auto& m = state->movie;
    if (m.pbo_count == 0 || !m.sink) return false;

    const int slot = m.pbo_head;
    if (!wait && m.fence[slot]) {
        const GLenum r = glClientWaitSync(m.fence[slot], 0, 0);
        if (r != GL_ALREADY_SIGNALED && r != GL_CONDITION_SATISFIED) return false;
    }

    uint8_t* dst = frame_sink::acquire(m.sink, wait);
    if (!dst) return false;

    glBindBuffer(GL_PIXEL_PACK_BUFFER, m.pbo[slot]);
    const void* src = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)m.pbo_bytes, GL_MAP_READ_BIT);
    if (src) {
        memcpy(dst, src, m.pbo_bytes);
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        frame_sink::submit(m.sink, dst, m.pbo_frame[slot]);
    } else {
        VIAMD_LOG_ERROR("Could not read back movie frame %d from the GPU", m.pbo_frame[slot]);
        frame_sink::release(m.sink, dst);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    if (m.fence[slot]) {
        glDeleteSync(m.fence[slot]);
        m.fence[slot] = nullptr;
    }
    m.pbo_head = (m.pbo_head + 1) % MOVIE_RING_SIZE;
    m.pbo_count -= 1;
    return true;
}

static void movie_pbo_flush(ApplicationState* state) {
    while (movie_pbo_pop(state, true)) {}
}

static const char* movie_overlay_type_str[(int)MovieOverlayType::Count] = {
    "Text",
    "Time stamp",
    "Scale bar",
};

static const char* movie_overlay_anchor_str[(int)MovieOverlayAnchor::Count] = {
    "Top left", "Top center", "Top right",
    "Middle left", "Center", "Middle right",
    "Bottom left", "Bottom center", "Bottom right",
};

// The overlays that are visible at a movie time, drawn into the rectangle (pos, size) of a frame. Everything scales with the
// height of the frame, so a frame looks the same at any resolution.
static void movie_overlays_draw(ImDrawList* dl, ImVec2 pos, ImVec2 size, double time, const ApplicationState* state) {
    const auto& m = state->movie;
    ImFont* font = ImGui::GetFont();
    if (!font || size.x <= 0.0f || size.y <= 0.0f) return;

    const float margin = 0.035f * size.y;
    for (const MovieOverlay& o : m.overlays) {
        const float alpha = movie_overlay_alpha(o, time);
        if (alpha <= 0.0f) continue;

        const float font_px = MAX(o.size * size.y, 4.0f);
        const ImU32 col    = ImGui::ColorConvertFloat4ToU32(ImVec4(o.color[0], o.color[1], o.color[2], o.color[3] * alpha));
        const ImU32 shadow = IM_COL32(0, 0, 0, (int)(160.0f * o.color[3] * alpha));
        const float soff   = MAX(font_px * 0.05f, 1.0f);

        char buf[160] = "";
        float bar_px = 0.0f;   // Scale bar only
        switch (o.type) {
        case MovieOverlayType::Text:
            snprintf(buf, sizeof(buf), "%s", o.text);
            break;
        case MovieOverlayType::Timestamp: {
            const double frame = movie_trajectory_frame(state, time);
            if (md_array_size(state->timeline.x_values) > 0) {
                char unit_buf[32] = "";
                if (!md_unit_is_none(state->timeline.time_unit)) md_unit_print(unit_buf, sizeof(unit_buf), state->timeline.time_unit);
                snprintf(buf, sizeof(buf), "%.1f %s", frame_to_time(frame, *state), unit_buf);
            } else {
                snprintf(buf, sizeof(buf), "Frame %d", (int)(frame + 0.5));
            }
            break;
        }
        case MovieOverlayType::ScaleBar: {
            const double upp = movie_units_per_pixel(state->view.camera.distance, state->view.camera.fov_y, size.y);
            const float len = o.length > 0.0f ? o.length : movie_scale_bar_length(upp, size.x, 0.2);
            if (len <= 0.0f || upp <= 0.0) continue;
            bar_px = (float)((double)len / upp);
            snprintf(buf, sizeof(buf), "%g \xC3\x85", (double)len);
            break;
        }
        default: break;
        }

        const ImVec2 text_size = font->CalcTextSizeA(font_px, FLT_MAX, 0.0f, buf);
        const float bar_h = MAX(font_px * 0.18f, 2.0f);
        const float gap   = font_px * 0.2f;
        const ImVec2 block = o.type == MovieOverlayType::ScaleBar
            ? ImVec2(MAX(bar_px, text_size.x), text_size.y + gap + bar_h)
            : text_size;

        const int ai = (int)o.anchor;
        const float fx = 0.5f * (float)(ai % 3);
        const float fy = 0.5f * (float)(ai / 3);
        const ImVec2 p0 = ImVec2(pos.x + margin + (size.x - 2.0f * margin - block.x) * fx,
                                 pos.y + margin + (size.y - 2.0f * margin - block.y) * fy);

        if (o.type == MovieOverlayType::ScaleBar) {
            const float tx = p0.x + 0.5f * (block.x - text_size.x);
            dl->AddText(font, font_px, ImVec2(tx + soff, p0.y + soff), shadow, buf);
            dl->AddText(font, font_px, ImVec2(tx, p0.y), col, buf);
            const float bx = p0.x + 0.5f * (block.x - bar_px);
            const float by = p0.y + text_size.y + gap;
            dl->AddRectFilled(ImVec2(bx + soff, by + soff), ImVec2(bx + bar_px + soff, by + bar_h + soff), shadow);
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bar_px, by + bar_h), col);
        } else {
            dl->AddText(font, font_px, ImVec2(p0.x + soff, p0.y + soff), shadow, buf);
            dl->AddText(font, font_px, p0, col, buf);
        }
    }
}

// Draws the overlays onto the frame in the G-buffer, after post processing and before it is read back
static void movie_overlays_render(ApplicationState* state, double time) {
    if (state->movie.overlays.empty()) return;

    const float w = (float)state->gbuffer.width;
    const float h = (float)state->gbuffer.height;

    ImDrawList dl(ImGui::GetDrawListSharedData());
    dl._ResetForNewFrame();
    dl.PushTexture(ImGui::GetIO().Fonts->TexRef);
    dl.PushClipRect(ImVec2(0, 0), ImVec2(w, h));
    movie_overlays_draw(&dl, ImVec2(0, 0), ImVec2(w, h), time, state);
    if (dl.VtxBuffer.Size == 0) return;

    ImDrawData dd;
    dd.Valid = true;
    dd.CmdLists.push_back(&dl);
    dd.CmdListsCount = 1;
    dd.TotalVtxCount = dl.VtxBuffer.Size;
    dd.TotalIdxCount = dl.IdxBuffer.Size;
    dd.DisplayPos = ImVec2(0, 0);
    dd.DisplaySize = ImVec2(w, h);
    dd.FramebufferScale = ImVec2(1, 1);
    dd.Textures = &ImGui::GetPlatformIO().Textures;

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, state->gbuffer.fbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    ImGui_ImplOpenGL3_RenderDrawData(&dd);
}

// Starts the read back of the frame that was just rendered. The pixels reach the sink some frames later,
// from movie_pbo_pop, so the GPU is not waited for here.
static void movie_capture_frame(ApplicationState* state) {
    auto& m = state->movie;
    ASSERT(m.pbo_count < MOVIE_RING_SIZE);

    if ((int)state->gbuffer.width != m.rec_w || (int)state->gbuffer.height != m.rec_h) {
        VIAMD_LOG_ERROR("Movie recording stopped: the render target is %dx%d, not the %dx%d of the movie",
            (int)state->gbuffer.width, (int)state->gbuffer.height, m.rec_w, m.rec_h);
        movie_recording_stop(state);
        return;
    }

    const int slot = (m.pbo_head + m.pbo_count) % MOVIE_RING_SIZE;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, state->gbuffer.fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m.pbo[slot]);
    glReadPixels(0, 0, m.rec_w, m.rec_h, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    if (m.fence[slot]) glDeleteSync(m.fence[slot]);
    m.fence[slot] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    m.pbo_frame[slot] = m.frame_index;
    m.pbo_count += 1;
}

// Shows the frame that was just rendered in the viewport, scaled to fit. The GUI is drawn over it.
static void movie_blit_preview(ApplicationState* state) {
    const int fw = (int)state->app.framebuffer.width;
    const int fh = (int)state->app.framebuffer.height;
    const int gw = (int)state->gbuffer.width;
    const int gh = (int)state->gbuffer.height;

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    glViewport(0, 0, fw, fh);
    const float background[4] = {0.07f, 0.07f, 0.07f, 1.0f};
    glClearBufferfv(GL_COLOR, 0, background);

    if (fw <= 0 || fh <= 0 || gw <= 0 || gh <= 0) return;

    const float scale = MIN((float)fw / (float)gw, (float)fh / (float)gh);
    const int dw = MAX(1, (int)(gw * scale));
    const int dh = MAX(1, (int)(gh * scale));
    const int x0 = (fw - dw) / 2;
    const int y0 = (fh - dh) / 2;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, state->gbuffer.fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBlitFramebuffer(0, 0, gw, gh, x0, y0, x0 + dw, y0 + dh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
}

// Begins a movie recording: the trajectory is stepped deterministically (independent of real
// render speed) from movie.start_frame to movie.end_frame. One frame is rendered per output frame
// at the movie's resolution and handed to a frame_sink, which writes it in the background.
static void movie_recording_start(ApplicationState* state) {
    ASSERT(state);
    auto& m = state->movie;

    if (m.state == MovieRecordingState::Recording) {
        return;
    }

    if (m.sink) {
        VIAMD_LOG_ERROR("Cannot start movie recording: the previous movie is still being written");
        return;
    }

    if (str_empty(m.output_dir)) {
        VIAMD_LOG_ERROR("Cannot start movie recording: no output directory selected");
        return;
    }

    const double max_frame = (double)(run_num_frames(state) > 0 ? run_num_frames(state) - 1 : 0);
    m.start_frame = CLAMP(m.start_frame, 0.0, max_frame);
    m.end_frame   = CLAMP(m.end_frame,   0.0, max_frame);
    if (m.duration_auto && m.end_frame == m.start_frame) {
        VIAMD_LOG_ERROR("Cannot start movie recording: the start and end frames are the same, so the movie has no duration");
        return;
    }
    if (movie_duration(state) <= 0.0) {
        VIAMD_LOG_ERROR("Cannot start movie recording: the movie has no duration");
        return;
    }

    if (m.filename_prefix[0] == '\0') {
        snprintf(m.filename_prefix, sizeof(m.filename_prefix), "frame");
    }

    int w = 0, h = 0;
    movie_frame_size(state, &w, &h);
    if (w <= 0 || h <= 0) {
        VIAMD_LOG_ERROR("Cannot start movie recording: the window has no size");
        return;
    }

    frame_sink::Desc desc;
    desc.kind   = m.output == MovieOutput::Mp4 ? frame_sink::Kind::Ffmpeg : frame_sink::Kind::PngSequence;
    desc.dir    = m.output_dir;
    desc.prefix = str_from_cstr(m.filename_prefix);
    desc.width  = w;
    desc.height = h;
    desc.fps    = m.fps;
    desc.crf    = m.crf;
    desc.ffmpeg = str_from_cstr(m.ffmpeg_path);

    char err[256] = {};
    m.sink = frame_sink::create(desc, err, sizeof(err));
    if (!m.sink) {
        VIAMD_LOG_ERROR("Cannot start movie recording: %s%s", err,
            m.output == MovieOutput::Mp4 ? ". The PNG sequence output does not need ffmpeg" : "");
        return;
    }

    movie_pbo_alloc(state, (size_t)w * (size_t)h * 4);

    if (m.output == MovieOutput::Mp4) {
        snprintf(m.rec_result, sizeof(m.rec_result), STR_FMT "/%s.mp4", STR_ARG(m.output_dir), m.filename_prefix);
    } else {
        snprintf(m.rec_result, sizeof(m.rec_result), STR_FMT, STR_ARG(m.output_dir));
    }

    m.rec_w = w;
    m.rec_h = h;
    m.rec_output = m.output;
    m.cur_time = 0.0;
    m.frame_index = 0;
    m.samples_done = 0;
    m.sample_target = state->visuals.temporal_aa.enabled ? JITTER_SEQUENCE_SIZE : 1;
    m.can_capture = true;

    m.prev_playback_mode = state->animation.mode;
    state->animation.mode = PlaybackMode::Stopped;

    m.camera_was_animated = m.animate_camera && md_array_size(m.keyframes) > 0;
    m.prev_view_target = state->view.target;
    m.prev_fov_y = state->view.camera.fov_y;

    m.state = MovieRecordingState::Recording;

    VIAMD_LOG_INFO("Recording %d movie frame(s) (%.2f s, %dx%d) to '%s'", movie_num_frames(state), movie_duration(state), w, h, m.rec_result);
}

// Ends the recording. The frames captured so far are kept: the sink writes what it has and is
// closed, so a stopped movie is a shorter movie.
static void movie_recording_stop(ApplicationState* state) {
    ASSERT(state);
    auto& m = state->movie;

    if (m.state != MovieRecordingState::Recording) {
        return;
    }

    m.state = MovieRecordingState::Idle;
    movie_restore_state(state);

    movie_pbo_flush(state);
    movie_pbo_free(state);
    if (m.sink) frame_sink::close(m.sink);

    VIAMD_LOG_INFO("Movie recording stopped after %d frame(s)", m.frame_index);
}

static void movie_shutdown(ApplicationState* state) {
    movie_recording_stop(state);
    movie_pbo_free(state);
    if (state->movie.sink) {
        // Waits for whatever is still being written
        frame_sink::destroy(state->movie.sink);
        state->movie.sink = nullptr;
    }
}

// Plays the movie in the viewport at real time, with the trajectory and the camera as they will be recorded
static void update_movie_preview(ApplicationState* state) {
    auto& m = state->movie;
    if (!m.preview_playing) return;

    const float len = (float)movie_duration(state);
    if (m.state == MovieRecordingState::Recording || len <= 0.0f || run_num_frames(state) == 0) {
        m.preview_playing = false;
        return;
    }

    state->animation.mode = PlaybackMode::Stopped;
    m.playhead += (float)state->app.timing.delta_s;
    if (m.playhead >= len) {
        if (m.preview_loop) {
            m.playhead = fmodf(m.playhead, len);
        } else {
            m.playhead = len;
            m.preview_playing = false;
        }
    }
    movie_apply_time(state, (double)m.playhead, true);
}

// Drives the movie recording. Called once per application loop iteration, before render(): it sets the
// scene to the time of the frame being captured (and keeps it there while that frame's samples are
// accumulated), and passes finished read backs on to the sink.
static void update_movie_recording(ApplicationState* state) {
    ASSERT(state);
    auto& m = state->movie;

    if (m.state != MovieRecordingState::Recording) {
        // The recording is over but the sink may still be writing. Report when it is done.
        if (m.sink) {
            const frame_sink::Status st = frame_sink::status(m.sink);
            if (st.done) {
                if (st.ok) {
                    if (m.rec_output == MovieOutput::Mp4) {
                        VIAMD_LOG_SUCCESS("Movie saved to '%s' (%d frames)", m.rec_result, st.written);
                    } else {
                        VIAMD_LOG_SUCCESS("%d PNG frame(s) written to '%s'", st.written, m.rec_result);
                    }
                } else {
                    VIAMD_LOG_ERROR("Writing the movie failed: %s (%d of %d frame(s) written)", st.message, st.written, st.submitted);
                }
                frame_sink::destroy(m.sink);
                m.sink = nullptr;
            }
        }
        return;
    }

    // A writer that has failed (ffmpeg gone, disk full) ends the recording rather than rendering for nothing
    const frame_sink::Status st = frame_sink::status(m.sink);
    if (!st.ok) {
        VIAMD_LOG_ERROR("Movie recording stopped: %s", st.message);
        movie_recording_stop(state);
        return;
    }

    state->animation.mode = PlaybackMode::Stopped;

    while (movie_pbo_pop(state, false)) {}
    m.can_capture = m.pbo_count < MOVIE_RING_SIZE;

    if (m.frame_index >= movie_num_frames(state)) {
        const int num_frames = m.frame_index;
        movie_pbo_flush(state);
        movie_pbo_free(state);
        frame_sink::close(m.sink);
        m.state = MovieRecordingState::Idle;
        movie_restore_state(state);
        VIAMD_LOG_INFO("Movie recording finished, %d frame(s) captured. Writing the rest in the background", num_frames);
        return;
    }

    // Computed from the index rather than accumulated, so the time does not drift. Applied on every
    // iteration so that nothing done in the meantime moves the scene away from this frame.
    m.cur_time = (double)m.frame_index / (double)m.fps;
    movie_apply_time(state, m.cur_time, m.camera_was_animated);
}

// ### MOVIE WINDOW ###

constexpr float MOVIE_DEG_TO_RAD = 3.14159265358979f / 180.0f;
constexpr float MOVIE_RAD_TO_DEG = 180.0f / 3.14159265358979f;

static const char* spin_axis_str[(int)SpinAxis::Count] = {
    "Camera up",
    "World Y",
    "World X",
    "World Z",
};

// At the playhead. A second key at the same time would leave the path undefined, so it replaces the first.
static void movie_add_keyframe(ApplicationState* state) {
    auto& m = state->movie;
    CameraKeyframe key = {};
    key.transform = state->view.target;
    key.fov_y = state->view.camera.fov_y;
    key.time = (double)m.playhead;
    if (m.key_includes_frame) {
        key.use_frame = true;
        key.frame = state->animation.frame;
    }
    if (m.key_follow && movie_follow_center(state, &key.follow_center)) {
        key.follow = true;
    }

    for (size_t i = 0; i < md_array_size(m.keyframes); ++i) {
        const CameraKeyframe& old = m.keyframes[i];
        if (fabs(old.time - key.time) < 1.0e-3) {
            // The pose is what is replaced; the spin stays, and so does the frame unless there is a new one
            key.spin_turns = old.spin_turns;
            key.spin_axis = old.spin_axis;
            key.spin_constant_speed = old.spin_constant_speed;
            if (!m.key_includes_frame) {
                key.use_frame = old.use_frame;
                key.frame = old.frame;
            }
            m.keyframes[i] = key;
            return;
        }
    }
    md_array_push(m.keyframes, key, state->allocator.persistent);
    movie_sort_keyframes(state);
}

// Two keys with the view as it is now, the second after the orbit's duration with whole turns around it.
// The camera leaves and comes back to the same pose.
static void movie_add_orbit(ApplicationState* state) {
    auto& m = state->movie;
    if (m.orbit_turns == 0) return;

    const double t0 = (double)m.playhead;
    const double t1 = t0 + (double)MAX(m.orbit_duration, 0.1f);
    if (t1 > movie_duration(state) + 1.0e-6) {
        if (m.duration_auto) {
            VIAMD_LOG_ERROR("The orbit ends at %.2f s, after the end of the movie. Move the preview time earlier, shorten the orbit or set a duration of your own", t1);
            return;
        }
        m.duration = (float)MIN(t1, 3600.0);
    }

    movie_add_keyframe(state);

    CameraKeyframe end = {};
    for (size_t i = 0; i < md_array_size(m.keyframes); ++i) {
        if (fabs(m.keyframes[i].time - t0) < 1.0e-3) end = m.keyframes[i];
    }
    end.time = t1;
    end.spin_turns = m.orbit_turns;
    end.spin_axis = m.orbit_axis;
    end.spin_constant_speed = false;

    bool replaced = false;
    for (size_t i = 0; i < md_array_size(m.keyframes); ++i) {
        if (fabs(m.keyframes[i].time - t1) < 1.0e-3) {
            m.keyframes[i] = end;
            replaced = true;
        }
    }
    if (!replaced) {
        md_array_push(m.keyframes, end, state->allocator.persistent);
        movie_sort_keyframes(state);
    }
    m.playhead = (float)t1;
}

// Makes the camera path a loop: the movie ends in the pose it starts in, and the path is smooth across the seam
static void movie_close_loop(ApplicationState* state) {
    auto& m = state->movie;
    const size_t n = md_array_size(m.keyframes);
    if (n < 2) {
        VIAMD_LOG_ERROR("A loop needs at least two keyframes");
        return;
    }

    const CameraKeyframe first = m.keyframes[0];
    const double len = movie_duration(state);
    CameraKeyframe& last = m.keyframes[n - 1];
    if (last.time < len - 1.0e-3) {
        CameraKeyframe end = first;
        end.time = len;
        end.use_frame = false;
        end.spin_turns = 0;
        end.ease = KeyEase::Smooth;
        md_array_push(m.keyframes, end, state->allocator.persistent);
    } else {
        // The last key is at the end already: it becomes the first again
        last.transform = first.transform;
        last.fov_y = first.fov_y;
    }
    m.loop = true;
}

static void movie_param_sort(ApplicationState* state) {
    std::stable_sort(state->movie.param_keys.begin(), state->movie.param_keys.end(), [](const ParamKey& a, const ParamKey& b) {
        return a.param != b.param ? a.param < b.param : a.time < b.time;
    });
}

// Keys what the parameter is now, at the playhead
static void movie_key_param(ApplicationState* state, int id) {
    auto& m = state->movie;
    const MovieParamDesc* d = movie_param_desc(id);
    if (!d) return;

    ParamKey key;
    key.param = id;
    key.time = (double)m.playhead;
    const float* src = d->ptr(state);
    for (int c = 0; c < d->comps; ++c) key.value[c] = src[c];

    for (ParamKey& k : m.param_keys) {
        if (k.param == id && fabs(k.time - key.time) < 1.0e-3) {
            key.ease = k.ease;
            k = key;
            return;
        }
    }
    m.param_keys.push_back(key);
    movie_param_sort(state);
}

static void movie_undo(ApplicationState* state) {
    MovieKeys keys = movie_keys_snapshot(state);
    if (state->movie.history.undo(&keys)) movie_keys_restore(state, keys);
}

static void movie_redo(ApplicationState* state) {
    MovieKeys keys = movie_keys_snapshot(state);
    if (state->movie.history.redo(&keys)) movie_keys_restore(state, keys);
}

// Once per frame. What is edited is not reported, the history sees the keys change.
static void update_movie_history(ApplicationState* state) {
    if (state->movie.state == MovieRecordingState::Recording) return;
    const bool editing = ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left);
    state->movie.history.update(movie_keys_snapshot(state), editing);
}

static void movie_goto_keyframe(ApplicationState* state, size_t idx) {
    auto& m = state->movie;
    if (idx >= md_array_size(m.keyframes)) return;
    const CameraKeyframe& key = m.keyframes[idx];
    state->view.target = key.transform;
    state->view.camera.fov_y = key.fov_y;
    m.playhead = CLAMP((float)key.time, 0.0f, (float)movie_duration(state));
    state->animation.frame = movie_trajectory_frame(state, key.time);
}

static void movie_draw_frustum(immediate::Queue* q, const ViewTransform& vt, float fov_y, float aspect, float length, uint32_t color) {
    const vec3_t eye   = vt.position;
    const vec3_t fwd   = vt.orientation * vec3_t{0, 0, -1};
    const vec3_t right = vt.orientation * vec3_t{1, 0, 0};
    const vec3_t up    = vt.orientation * vec3_t{0, 1, 0};
    const float  hh = tanf(fov_y * 0.5f) * length;
    const float  hw = hh * aspect;
    const vec3_t c  = eye + fwd * length;
    const vec3_t p[4] = {
        c - right * hw - up * hh,
        c + right * hw - up * hh,
        c + right * hw + up * hh,
        c - right * hw + up * hh,
    };
    for (int i = 0; i < 4; ++i) {
        immediate::line(q, eye, p[i], color);
        immediate::line(q, p[i], p[(i + 1) % 4], color);
    }
    // A roof on the top edge, so that it can be told which way is up
    const vec3_t roof = c + up * (hh * 1.4f);
    immediate::line(q, p[2], roof, color);
    immediate::line(q, p[3], roof, color);
}

// The camera path of the keyframes in the viewport: the eye, what it looks at, and the camera at each key.
// Not part of what is recorded, which is why this is not called for frames that are captured.
static void movie_draw_camera_path(ApplicationState* state) {
    auto& m = state->movie;
    const size_t n = md_array_size(m.keyframes);
    if (!m.show_window || !m.show_path || n == 0) return;

    int w = 0, h = 0;
    movie_frame_size(state, &w, &h);
    const float aspect = h > 0 ? (float)w / (float)h : 1.0f;

    const uint32_t col_eye  = IM_COL32(90, 200, 255, 255);
    const uint32_t col_look = IM_COL32(255, 200, 60, 255);
    const uint32_t col_key  = IM_COL32(255, 255, 255, 230);
    const uint32_t col_sel  = IM_COL32(255, 110, 40, 255);
    const uint32_t col_head = IM_COL32(80, 255, 120, 255);
    const uint32_t col_dim  = IM_COL32(255, 255, 255, 70);

    immediate::Scope scope(state->gfx.overlay, "movie camera path");
    immediate::Queue* q = scope;
    const CameraKeyframe* keys = m.keyframes;

    if (n >= 2) {
        const int samples = CLAMP((int)n * 48, 64, 1024);
        const double t0 = keys[0].time;
        const double t1 = keys[n - 1].time;
        vec3_t prev_eye = {}, prev_look = {};
        for (int i = 0; i <= samples; ++i) {
            ViewTransform vt;
            float fov_y;
            camera_keyframes_evaluate(&vt, &fov_y, keys, n, t0 + (t1 - t0) * (double)i / (double)samples, m.loop);
            const vec3_t eye  = vt.position;
            const vec3_t look = camera_get_look_at(vt);
            if (i > 0) {
                immediate::line(q, prev_eye,  eye,  col_eye);
                immediate::line(q, prev_look, look, col_look);
            }
            prev_eye = eye;
            prev_look = look;
        }
    }

    for (size_t i = 0; i < n; ++i) {
        const CameraKeyframe& k = keys[i];
        const bool selected = fabs(k.time - (double)m.playhead) < 1.0e-3;
        movie_draw_frustum(q, k.transform, k.fov_y, aspect, k.transform.distance * 0.25f, selected ? col_sel : col_key);

        // What the camera looks at, and the line of sight to it
        const vec3_t look = camera_get_look_at(k.transform);
        const float s = k.transform.distance * 0.03f;
        immediate::line(q, k.transform.position, look, col_dim);
        immediate::line(q, look - vec3_t{s, 0, 0}, look + vec3_t{s, 0, 0}, col_look);
        immediate::line(q, look - vec3_t{0, s, 0}, look + vec3_t{0, s, 0}, col_look);
        immediate::line(q, look - vec3_t{0, 0, s}, look + vec3_t{0, 0, s}, col_look);
    }

    // The camera at the playhead
    ViewTransform vt;
    float fov_y;
    camera_keyframes_evaluate(&vt, &fov_y, keys, n, (double)m.playhead, m.loop);
    movie_draw_frustum(q, vt, fov_y, aspect, vt.distance * 0.18f, col_head);

    if (state->visuals.dof.enabled) {
        // Where depth of field is sharp: a frame the size of the view at that depth, and a cross where the camera looks
        const uint32_t col_focus = IM_COL32(255, 90, 220, 255);
        const float depth = dof_focus_depth(state, vt);
        const vec3_t fwd   = vt.orientation * vec3_t{0, 0, -1};
        const vec3_t right = vt.orientation * vec3_t{1, 0, 0};
        const vec3_t up    = vt.orientation * vec3_t{0, 1, 0};
        const vec3_t c = vt.position + fwd * depth;
        const float hh = depth * tanf(fov_y * 0.5f);
        const float hw = hh * aspect;
        const vec3_t p[4] = { c - right * hw - up * hh, c + right * hw - up * hh, c + right * hw + up * hh, c - right * hw + up * hh };
        for (int i = 0; i < 4; ++i) immediate::line(q, p[i], p[(i + 1) % 4], col_focus);
        const float s = hh * 0.1f;
        immediate::line(q, c - right * s, c + right * s, col_focus);
        immediate::line(q, c - up * s,    c + up * s,    col_focus);
        immediate::line(q, vt.position, c, col_dim);
    }
}

// Keyframes in the Timelines plots, which are in trajectory time. A keyframe is at a time of the movie,
// so it is put where the trajectory is at that time; keys that fall on the same spot (the trajectory is
// held) are stacked. Read only: they are edited in the Movie window. Returns whether one is hovered.
static bool movie_draw_timeline_markers(ApplicationState* data) {
    auto& m = data->movie;
    const size_t n = md_array_size(m.keyframes);
    if ((n == 0 && !m.show_window) || md_array_size(data->timeline.x_values) == 0) return false;

    ImDrawList* dl = ImPlot::GetPlotDrawList();
    const ImVec2 plot_pos  = ImPlot::GetPlotPos();
    const ImVec2 plot_size = ImPlot::GetPlotSize();
    const float  r = ImGui::GetFontSize() * 0.4f;
    const ImU32  col = IM_COL32(255, 120, 40, 255);

    int hovered_key = -1;
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool plot_hovered = ImPlot::IsPlotHovered();

    ImPlot::PushPlotClipRect();

    if (m.show_window) {
        // The part of the trajectory that the movie plays
        const double f0 = MIN(m.start_frame, m.end_frame);
        const double f1 = MAX(m.start_frame, m.end_frame);
        const float x0 = ImPlot::PlotToPixels(frame_to_time(f0, *data), 0.0).x;
        const float x1 = ImPlot::PlotToPixels(frame_to_time(f1, *data), 0.0).x;
        dl->AddRectFilled(ImVec2(x0, plot_pos.y), ImVec2(MAX(x1, x0 + 1.0f), plot_pos.y + plot_size.y), IM_COL32(255, 170, 40, 26));
    }

    std::vector<float> xs(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = frame_to_time(movie_trajectory_frame(data, m.keyframes[i].time), *data);
        const float x = ImPlot::PlotToPixels(t, 0.0).x;
        xs[i] = x;

        int stack = 0;
        for (size_t j = 0; j < i; ++j) {
            if (fabsf(xs[j] - x) < 2.0f * r) stack += 1;
        }

        const ImVec2 c(x, plot_pos.y + r + 2.0f + (float)stack * (2.0f * r + 2.0f));
        dl->AddLine(ImVec2(x, plot_pos.y), ImVec2(x, plot_pos.y + plot_size.y), IM_COL32(255, 120, 40, 90));
        dl->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), col);

        char label[16];
        snprintf(label, sizeof(label), "%d", (int)i + 1);
        dl->AddText(ImVec2(c.x + r + 2.0f, c.y - ImGui::GetFontSize() * 0.5f), IM_COL32(255, 200, 150, 255), label);

        if (plot_hovered) {
            const float dx = mouse.x - c.x;
            const float dy = mouse.y - c.y;
            if (dx * dx + dy * dy < (r * 1.6f) * (r * 1.6f)) hovered_key = (int)i;
        }
    }

    ImPlot::PopPlotClipRect();

    if (hovered_key >= 0) {
        const CameraKeyframe& k = m.keyframes[hovered_key];
        ImGui::SetTooltip("Camera keyframe %d\nMovie time: %.2f s\nField of view: %.1f deg\nClick to go to it",
            hovered_key + 1, k.time, k.fov_y * MOVIE_RAD_TO_DEG);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m.state != MovieRecordingState::Recording) {
            movie_goto_keyframe(data, (size_t)hovered_key);
        }
    }
    return hovered_key >= 0;
}

// Time along the movie with the keyframes on it, which can be dragged in time. The curves show what the
// camera does between them: the distance to what it looks at and the field of view, each scaled to fit.
static void draw_movie_strip(ApplicationState* data, float movie_len, bool locked, ImVec2 size) {
    auto& m = data->movie;
    if (movie_len <= 0.0f) return;
    const size_t n = md_array_size(m.keyframes);

    // Evaluated on a sorted copy, the keys themselves are only re-sorted when a drag ends
    std::vector<CameraKeyframe> sorted(m.keyframes, m.keyframes + n);
    std::stable_sort(sorted.begin(), sorted.end(), [](const CameraKeyframe& a, const CameraKeyframe& b) { return a.time < b.time; });

    constexpr int N = 200;
    float xs[N], dist[N], fov[N];
    float d_lo = FLT_MAX, d_hi = -FLT_MAX, f_lo = FLT_MAX, f_hi = -FLT_MAX;
    if (n > 0) {
        for (int i = 0; i < N; ++i) {
            ViewTransform vt;
            float fov_y;
            const double t = (double)movie_len * (double)i / (double)(N - 1);
            camera_keyframes_evaluate(&vt, &fov_y, sorted.data(), n, t, m.loop);
            xs[i] = (float)t;
            dist[i] = vt.distance;
            fov[i] = fov_y * MOVIE_RAD_TO_DEG;
            d_lo = MIN(d_lo, dist[i]); d_hi = MAX(d_hi, dist[i]);
            f_lo = MIN(f_lo, fov[i]);  f_hi = MAX(f_hi, fov[i]);
        }
    }
    auto norm = [](float v, float lo, float hi) { return hi - lo > 1.0e-6f ? 0.15f + 0.7f * CLAMP((v - lo) / (hi - lo), 0.0f, 1.0f) : 0.5f; };
    for (int i = 0; i < N && n > 0; ++i) {
        dist[i] = norm(dist[i], d_lo, d_hi);
        fov[i]  = norm(fov[i], f_lo, f_hi);
    }

    // The trajectory frame over the movie, against the whole trajectory (not scaled to fit, so that a point
    // can be dragged to a frame)
    const double last_frame = (double)(run_num_frames(data) > 0 ? run_num_frames(data) - 1 : 0);
    const double frame_scale = last_frame > 0.0 ? last_frame : 1.0;
    float tx[N], frm[N];
    for (int i = 0; i < N; ++i) {
        const double t = (double)movie_len * (double)i / (double)(N - 1);
        tx[i] = (float)t;
        frm[i] = 0.15f + 0.7f * (float)(movie_trajectory_frame(data, t) / frame_scale);
    }
    double unused_frame;
    const bool has_frame_keys = camera_keyframes_evaluate_frame(&unused_frame, m.keyframes, n, 0.0);

    const ImPlotDragToolFlags drag_flags = ImPlotDragToolFlags_NoFit | (locked ? ImPlotDragToolFlags_NoInputs : 0);
    static bool resort_pending = false;

    const ImPlotFlags plot_flags = ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText | ImPlotFlags_NoTitle;
    if (ImPlot::BeginPlot("##movie_strip", size, plot_flags)) {
        ImPlot::SetupAxes("Movie time (s)", nullptr, ImPlotAxisFlags_Lock, ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoDecorations);
        ImPlot::SetupAxisLimits(ImAxis_X1, -0.03 * movie_len, 1.03 * movie_len, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -0.12, 1.15, ImPlotCond_Always);
        ImPlot::SetupLegend(ImPlotLocation_North, ImPlotLegendFlags_Outside | ImPlotLegendFlags_Horizontal);

        {
            // When the trajectory plays, if the keyframes do not say
            const double tb = m.duration_auto ? 0.0 : (double)m.traj_begin;
            const double te = m.duration_auto ? (double)movie_len : (double)m.traj_end;
            if (!has_frame_keys) {
                const ImVec2 p0 = ImPlot::PlotToPixels(tb, -0.12);
                const ImVec2 p1 = ImPlot::PlotToPixels(MAX(te, tb), 0.0);
                ImPlot::PushPlotClipRect();
                ImPlot::GetPlotDrawList()->AddRectFilled(p0, ImVec2(MAX(p1.x, p0.x + 1.0f), p1.y), IM_COL32(90, 160, 255, 90));
                ImPlot::PopPlotClipRect();
                const bool held = m.start_frame == m.end_frame;
                ImPlot::PlotText(held ? "trajectory (held)" : "trajectory", 0.5 * (tb + te), -0.06);
            }
        }

        ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
        ImPlot::PlotLine("Trajectory frame", tx, frm, N);
        ImPlot::PopStyleColor();

        if (!m.param_keys.empty()) {
            // Where look parameters have keys, they are edited in the table below
            std::vector<float> px(m.param_keys.size()), py(m.param_keys.size(), -0.05f);
            for (size_t i = 0; i < m.param_keys.size(); ++i) px[i] = (float)m.param_keys[i].time;
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, 5.0f, ImVec4(0.75f, 0.5f, 1.0f, 1.0f));
            ImPlot::PlotScatter("Look parameters", px.data(), py.data(), (int)px.size());
        }

        if (n >= 2) {
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.35f, 0.8f, 1.0f, 1.0f));
            ImPlot::PlotLine("Distance", xs, dist, N);
            ImPlot::PopStyleColor();
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1.0f, 0.8f, 0.25f, 1.0f));
            ImPlot::PlotLine("Field of view", xs, fov, N);
            ImPlot::PopStyleColor();
        }

        bool any_moved = false;
        bool any_held = false;
        for (size_t i = 0; i < n; ++i) {
            CameraKeyframe& key = m.keyframes[i];
            double x = key.time;
            double y = n >= 2 ? norm(key.transform.distance, d_lo, d_hi) : 0.5;
            bool hovered = false, held = false;
            if (ImPlot::DragPoint(2000 + (int)i, &x, &y, ImVec4(1.0f, 0.45f, 0.15f, 1.0f), 7.0f, drag_flags, nullptr, &hovered, &held)) {
                key.time = CLAMP(x, 0.0, (double)movie_len);
                any_moved = true;
            }
            if (held && !locked) {
                any_held = true;
                m.playhead = (float)key.time;
            }
            char label[16];
            snprintf(label, sizeof(label), "%d", (int)i + 1);
            ImPlot::PlotText(label, key.time, y, ImVec2(0, -14));
            if (hovered && !held) {
                ImGui::SetTooltip("Keyframe %d\n%.2f s\nDrag to change its time", (int)i + 1, key.time);
            }

            if (key.use_frame) {
                // Where the trajectory is at this key: drag up and down for the frame, sideways for the time
                double fx = key.time;
                double fy = 0.15 + 0.7 * CLAMP(key.frame / frame_scale, 0.0, 1.0);
                bool fhovered = false, fheld = false;
                if (ImPlot::DragPoint(3000 + (int)i, &fx, &fy, ImVec4(0.4f, 0.9f, 0.4f, 1.0f), 6.0f, drag_flags, nullptr, &fhovered, &fheld)) {
                    key.time = CLAMP(fx, 0.0, (double)movie_len);
                    key.frame = CLAMP((fy - 0.15) / 0.7 * frame_scale, 0.0, last_frame);
                    any_moved = true;
                }
                if (fheld && !locked) {
                    any_held = true;
                    m.playhead = (float)key.time;
                }
                if (fhovered && !fheld) {
                    ImGui::SetTooltip("Keyframe %d shows frame %.0f at %.2f s\nDrag up and down to change the frame", (int)i + 1, key.frame, key.time);
                }
            }
        }

        double playhead = (double)m.playhead;
        if (ImPlot::DragLineX(1000, &playhead, ImVec4(1, 1, 0, 1), 1.5f, drag_flags)) {
            m.playhead = CLAMP((float)playhead, 0.0f, movie_len);
            if (!locked) movie_apply_time_with_keys(data, (double)m.playhead, true, sorted.data(), n);
        }

        if (locked) {
            double cur = m.cur_time;
            ImPlot::DragLineX(1001, &cur, ImVec4(1.0f, 0.3f, 0.3f, 1), 1.5f, ImPlotDragToolFlags_NoInputs | ImPlotDragToolFlags_NoFit);
        }

        ImPlot::EndPlot();

        if (any_held) {
            movie_apply_time_with_keys(data, (double)m.playhead, true, sorted.data(), n);
        }
        if (any_moved) resort_pending = true;
    }
    if (resort_pending && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        movie_sort_keyframes(data);
        resort_pending = false;
    }
}

// A look parameter over the movie, as a curve whose keys can be dragged: sideways for their time, up and down
// for their value. A double click adds a key, a right click on one removes it. Fills the space that is left.
static void draw_movie_param_lane(ApplicationState* data, float movie_len, bool locked) {
    auto& m = data->movie;
    if (movie_len <= 0.0f) return;
    const int num_params = (int)(sizeof(movie_param_table) / sizeof(movie_param_table[0]));
    m.param_selected = CLAMP(m.param_selected, 0, num_params - 1);
    const MovieParamDesc& d = movie_param_table[m.param_selected];

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
    if (ImGui::BeginCombo("##lane_param", d.label)) {
        for (int i = 0; i < num_params; ++i) {
            if (ImGui::Selectable(movie_param_table[i].label, i == m.param_selected)) m.param_selected = i;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Double click to add a key. Drag a key to change it, right click to remove it.");

    std::vector<int> mine;
    for (int i = 0; i < (int)m.param_keys.size(); ++i) {
        if (m.param_keys[i].param == d.id) mine.push_back(i);
    }

    constexpr int N = 200;
    float xs[N], ys[N];
    if (!d.color && !mine.empty()) {
        for (int i = 0; i < N; ++i) {
            const double t = (double)movie_len * (double)i / (double)(N - 1);
            float v = 0.0f;
            param_keys_evaluate(&v, 1, m.param_keys.data(), m.param_keys.size(), d.id, t);
            xs[i] = (float)t;
            ys[i] = v;
        }
    }

    const ImPlotDragToolFlags drag_flags = ImPlotDragToolFlags_NoFit | (locked ? ImPlotDragToolFlags_NoInputs : 0);
    static bool resort_pending = false;
    bool any_moved = false, any_held = false, any_hovered = false;
    int  remove_idx = -1;
    bool add_key = false;
    double add_time = 0.0, add_value = 0.0;

    const ImPlotFlags plot_flags = ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText | ImPlotFlags_NoTitle | ImPlotFlags_NoLegend;
    if (ImPlot::BeginPlot("##param_lane", ImVec2(-1, -1), plot_flags)) {
        ImPlot::SetupAxes("Movie time (s)", d.color ? nullptr : d.label, ImPlotAxisFlags_Lock, ImPlotAxisFlags_Lock | (d.color ? ImPlotAxisFlags_NoDecorations : 0));
        ImPlot::SetupAxisLimits(ImAxis_X1, -0.03 * movie_len, 1.03 * movie_len, ImPlotCond_Always);
        if (d.color) {
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 1.0, ImPlotCond_Always);
        } else if (d.log) {
            ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
            ImPlot::SetupAxisLimits(ImAxis_Y1, d.lo * 0.8, d.hi * 1.25, ImPlotCond_Always);
        } else {
            const double pad = 0.05 * (d.hi - d.lo);
            ImPlot::SetupAxisLimits(ImAxis_Y1, d.lo - pad, d.hi + pad, ImPlotCond_Always);
        }

        if (!d.color && !mine.empty()) {
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.75f, 0.5f, 1.0f, 1.0f));
            ImPlot::PlotLine(d.label, xs, ys, N);
            ImPlot::PopStyleColor();
        }

        for (int ki : mine) {
            ParamKey& key = m.param_keys[ki];
            double x = key.time;
            double y = d.color ? 0.5 : (double)key.value[0];
            const ImVec4 col = d.color ? ImVec4(key.value[0], key.value[1], key.value[2], 1.0f) : ImVec4(0.75f, 0.5f, 1.0f, 1.0f);
            bool hovered = false, held = false;
            if (ImPlot::DragPoint(4000 + ki, &x, &y, col, 7.0f, drag_flags, nullptr, &hovered, &held)) {
                key.time = CLAMP(x, 0.0, (double)movie_len);
                if (!d.color) key.value[0] = (float)CLAMP(y, (double)d.lo, (double)d.hi);
                any_moved = true;
            }
            if (held && !locked) {
                any_held = true;
                m.playhead = (float)key.time;
            }
            if (hovered) {
                any_hovered = true;
                if (!held) {
                    if (d.color) ImGui::SetTooltip("%.2f s\nDrag to change its time. Its color is edited in the table of the Movie window.", key.time);
                    else         ImGui::SetTooltip("%.2f s, %.3g\nDrag to change it, right click to remove it", key.time, key.value[0]);
                }
                if (!locked && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) remove_idx = ki;
            }
        }

        if (!locked && !any_hovered && ImPlot::IsPlotHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            const ImPlotPoint p = ImPlot::GetPlotMousePos();
            add_key = true;
            add_time = CLAMP(p.x, 0.0, (double)movie_len);
            add_value = CLAMP(p.y, (double)d.lo, (double)d.hi);
        }

        double playhead = (double)m.playhead;
        if (ImPlot::DragLineX(1000, &playhead, ImVec4(1, 1, 0, 1), 1.5f, drag_flags)) {
            m.playhead = CLAMP((float)playhead, 0.0f, movie_len);
            if (!locked) movie_apply_time(data, (double)m.playhead, true);
        }

        if (locked) {
            double cur = m.cur_time;
            ImPlot::DragLineX(1001, &cur, ImVec4(1.0f, 0.3f, 0.3f, 1), 1.5f, ImPlotDragToolFlags_NoInputs | ImPlotDragToolFlags_NoFit);
        }

        ImPlot::EndPlot();
    }

    if (any_held) {
        movie_apply_time(data, (double)m.playhead, true);
    }
    if (any_moved) resort_pending = true;
    if (resort_pending && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        movie_param_sort(data);
        resort_pending = false;
    }

    if (remove_idx >= 0) {
        m.param_keys.erase(m.param_keys.begin() + remove_idx);
        // The parameter lets go of its values if this was its last key
        movie_params_apply(data, (double)m.playhead);
    }
    if (add_key) {
        ParamKey key;
        key.param = d.id;
        key.time = add_time;
        if (d.color) {
            // The colour it has at that time, or as it is now if there are no keys yet
            const float* now = d.ptr(data);
            float v[3] = {now[0], now[1], now[2]};
            param_keys_evaluate(v, 3, m.param_keys.data(), m.param_keys.size(), d.id, add_time);
            for (int c = 0; c < 3; ++c) key.value[c] = v[c];
        } else {
            key.value[0] = (float)add_value;
        }
        m.param_keys.push_back(key);
        movie_param_sort(data);
        movie_apply_time(data, (double)m.playhead, true);
    }
}

// The timeline of the movie in a window of its own, to be docked wide: the camera and the trajectory
// frame on top, one look parameter below.
static void draw_movie_timeline_window(ApplicationState* data) {
    auto& m = data->movie;
    const bool recording = m.state == MovieRecordingState::Recording;

    ImGui::SetNextWindowSize(ImVec2(960, 460), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Movie Timeline", &m.show_timeline_window, ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::End();
        return;
    }

    const float movie_len = (float)movie_duration(data);
    m.playhead = CLAMP(m.playhead, 0.0f, movie_len);
    const float fs = ImGui::GetFontSize();

    ImGui::BeginDisabled(recording);
    if (ImGui::Button(m.preview_playing ? "Pause Preview" : "Play Preview")) {
        m.preview_playing = !m.preview_playing;
        if (m.preview_playing && m.playhead >= movie_len) m.playhead = 0.0f;
    }
    ImGui::SetItemTooltip("Plays the movie in the viewport at the speed it will have, without recording.");
    ImGui::SameLine();
    ImGui::Checkbox("Repeat", &m.preview_loop);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(MAX(ImGui::GetContentRegionAvail().x - fs * 14.0f, fs * 8.0f));
    if (ImGui::SliderFloat("##timeline_playhead", &m.playhead, 0.0f, movie_len, "%.2f s")) {
        movie_apply_time(data, (double)m.playhead, true);
    }
    ImGui::SetItemTooltip("Scrub the movie: shows the trajectory frame, the camera with 'Animate camera' on, and the keyed look parameters.");
    ImGui::SameLine();
    if (ImGui::Button("Add Keyframe")) {
        movie_add_keyframe(data);
    }
    ImGui::SetItemTooltip("Adds a keyframe of the current view at the playhead. Shortcut: K");
    ImGui::EndDisabled();

    if (movie_len <= 0.0f) {
        ImGui::TextDisabled("The movie has no duration yet.");
        ImGui::End();
        return;
    }

    const float avail = ImGui::GetContentRegionAvail().y;
    draw_movie_strip(data, movie_len, recording, ImVec2(-1, MAX(avail * 0.58f, fs * 8.0f)));
    draw_movie_param_lane(data, movie_len, recording);

    ImGui::End();
}

static void draw_movie_overlay_section(ApplicationState* data, float movie_len) {
    auto& m = data->movie;

    auto add = [&](MovieOverlayType type) {
        MovieOverlay o;
        o.type = type;
        o.begin = (double)m.playhead;
        o.end = (double)MAX(movie_len, m.playhead);
        switch (type) {
        case MovieOverlayType::Text:      snprintf(o.text, sizeof(o.text), "Title"); o.anchor = MovieOverlayAnchor::BottomCenter; break;
        case MovieOverlayType::Timestamp: o.anchor = MovieOverlayAnchor::TopRight; break;
        case MovieOverlayType::ScaleBar:  o.anchor = MovieOverlayAnchor::BottomLeft; break;
        default: break;
        }
        m.overlays.push_back(o);
    };

    if (ImGui::Button("Add Text")) add(MovieOverlayType::Text);
    ImGui::SameLine();
    if (ImGui::Button("Add Time Stamp")) add(MovieOverlayType::Timestamp);
    ImGui::SetItemTooltip("The time of the trajectory frame that is shown, in the unit of the timeline");
    ImGui::SameLine();
    if (ImGui::Button("Add Scale Bar")) add(MovieOverlayType::ScaleBar);
    ImGui::SetItemTooltip("A bar of a known length in the structure. It is as long on the frame as that length is at the\ndistance the camera looks at, so it follows the zoom.");
    ImGui::SameLine();
    ImGui::Checkbox("Show in viewport", &m.show_overlay_preview);
    ImGui::SetItemTooltip("Shows them at the preview time. The recorded frames have the proportions of the movie's size,\nso where they sit is only exact when the viewport has them too.");

    int remove_idx = -1;
    for (int i = 0; i < (int)m.overlays.size(); ++i) {
        MovieOverlay& o = m.overlays[i];
        ImGui::PushID(i);
        char label[192];
        snprintf(label, sizeof(label), "%d  %s%s%s###overlay", i + 1, movie_overlay_type_str[(int)o.type],
            o.type == MovieOverlayType::Text ? ": " : "", o.type == MovieOverlayType::Text ? o.text : "");
        const bool open = ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() * 6.0f);
        ImGui::Checkbox("##enabled", &o.enabled);
        ImGui::SetItemTooltip("Shown in the movie");
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) remove_idx = i;
        if (open) {
            int type = (int)o.type;
            if (ImGui::Combo("Type", &type, movie_overlay_type_str, (int)MovieOverlayType::Count)) o.type = (MovieOverlayType)type;
            if (o.type == MovieOverlayType::Text) {
                ImGui::InputText("Text", o.text, sizeof(o.text));
            }
            if (o.type == MovieOverlayType::ScaleBar) {
                ImGui::DragFloat("Length (\xC3\x85)", &o.length, 0.1f, 0.0f, 10000.0f, o.length > 0.0f ? "%.2f" : "automatic");
                ImGui::SetItemTooltip("0 chooses a length that suits the frame: 1, 2 or 5 times a power of ten");
                o.length = MAX(o.length, 0.0f);
            }

            float range[2] = {(float)o.begin, (float)o.end};
            if (ImGui::DragFloatRange2("Shown (s)", &range[0], &range[1], 0.05f, 0.0f, movie_len, "from %.2f", "to %.2f")) {
                o.begin = range[0];
                o.end = range[1];
            }
            if (ImGui::SmallButton("Start at preview time")) o.begin = MIN((double)m.playhead, o.end);
            ImGui::SameLine();
            if (ImGui::SmallButton("End at preview time")) o.end = MAX((double)m.playhead, o.begin);

            ImGui::DragFloat("Fade in (s)", &o.fade_in, 0.01f, 0.0f, 10.0f, "%.2f");
            ImGui::DragFloat("Fade out (s)", &o.fade_out, 0.01f, 0.0f, 10.0f, "%.2f");

            int anchor = (int)o.anchor;
            if (ImGui::Combo("Position", &anchor, movie_overlay_anchor_str, (int)MovieOverlayAnchor::Count)) o.anchor = (MovieOverlayAnchor)anchor;
            ImGui::SliderFloat("Size", &o.size, 0.01f, 0.3f, "%.3f", ImGuiSliderFlags_Logarithmic);
            ImGui::SetItemTooltip("The height of the text, as a part of the height of the frame");
            ImGui::ColorEdit4("Color", o.color, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (remove_idx >= 0) m.overlays.erase(m.overlays.begin() + remove_idx);
}

static void draw_movie_keyframe_table(ApplicationState* data, float movie_len, bool locked) {
    auto& m = data->movie;
    if (md_array_size(m.keyframes) == 0) {
        ImGui::TextDisabled("Move the view, then 'Add Keyframe' (K) at a time on the timeline.");
        return;
    }

    bool resort = false;
    int  remove_idx = -1;
    int  dup_idx = -1;
    int  move_from = -1;  // Dragged to the place of move_to
    int  move_to = -1;

    const double last_frame = (double)(run_num_frames(data) > 0 ? run_num_frames(data) - 1 : 0);
    double prev_frame = -1.0;

    if (ImGui::BeginTable("##keyframes", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 1.8f);
        ImGui::TableSetupColumn("Time (s)");
        ImGui::TableSetupColumn("FOV (deg)");
        ImGui::TableSetupColumn("Ease");
        ImGui::TableSetupColumn("Frame");
        ImGui::TableSetupColumn("Spin", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 17.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)md_array_size(m.keyframes); ++i) {
            CameraKeyframe& key = m.keyframes[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            char num[16];
            snprintf(num, sizeof(num), "%d", i + 1);
            ImGui::Selectable(num, false, ImGuiSelectableFlags_None);
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                ImGui::SetDragDropPayload("MOVIE_KEYFRAME", &i, sizeof(int));
                ImGui::Text("Keyframe %d", i + 1);
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MOVIE_KEYFRAME")) {
                    move_from = *(const int*)payload->Data;
                    move_to = i;
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SetItemTooltip("Drag to another row to move this keyframe, with its pose, easing and frame, to that place.\nThe times stay where they are in the list.");

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            double t = key.time;
            if (ImGui::InputDouble("##time", &t, 0.0, 0.0, "%.2f")) {
                key.time = CLAMP(t, 0.0, (double)movie_len);
            }
            resort |= ImGui::IsItemDeactivatedAfterEdit();

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            float fov_deg = key.fov_y * MOVIE_RAD_TO_DEG;
            if (ImGui::DragFloat("##fov", &fov_deg, 0.1f, 1.0f, 170.0f, "%.1f")) {
                key.fov_y = fov_deg * MOVIE_DEG_TO_RAD;
            }

            ImGui::TableNextColumn();
            if (i == 0) {
                ImGui::TextDisabled("-");
                ImGui::SetItemTooltip("How the movie gets to a keyframe is set on that keyframe, so the first has none.");
            } else {
                ImGui::SetNextItemWidth(-FLT_MIN);
                int ease = (int)key.ease;
                if (ImGui::Combo("##ease", &ease, key_ease_str, (int)KeyEase::Count)) key.ease = (KeyEase)ease;
                ImGui::SetItemTooltip("How the camera, and the trajectory frame if it is keyed, move in the stretch leading to this keyframe.\n"
                    "Smooth: through the keys without stopping. Ease in/out: starts and ends slowly.\n"
                    "Linear: constant speed. Hold: stays as it was until the key, then jumps.");
            }

            ImGui::TableNextColumn();
            bool use_frame = key.use_frame;
            if (ImGui::Checkbox("##useframe", &use_frame)) {
                // Taken from the movie as it is, so that switching it on does not change anything
                if (use_frame) key.frame = movie_trajectory_frame(data, key.time);
                key.use_frame = use_frame;
            }
            ImGui::SetItemTooltip("Key the trajectory frame shown at this time. With two or more, the trajectory plays\nfrom one to the next, so the speed can change between them.");
            if (key.use_frame) {
                const bool backward = i > 0 && prev_frame >= 0.0 && key.frame < prev_frame - 0.5;
                const double zero = 0.0;
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragScalar("##frame", ImGuiDataType_Double, &key.frame, 0.5f, &zero, &last_frame, "%.0f");
                if (backward) {
                    ImGui::SetItemTooltip("Behind the previous frame: the trajectory plays backward to here.");
                }
                prev_frame = key.frame;
            }

            ImGui::TableNextColumn();
            if (i == 0) {
                ImGui::TextDisabled("-");
                ImGui::SetItemTooltip("A spin is made in the stretch leading to a keyframe, so the first has none.");
            } else {
                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.0f);
                ImGui::DragInt("##spin", &key.spin_turns, 0.05f, -16, 16, "%d x");
                ImGui::SetItemTooltip("Extra whole turns of the camera around what it looks at, made in the stretch leading to this\nkeyframe. Positive is counter-clockwise seen from the tip of the axis.");
                if (key.spin_turns != 0) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("...")) ImGui::OpenPopup("##spin_options");
                    if (ImGui::BeginPopup("##spin_options")) {
                        int axis = (int)key.spin_axis;
                        if (ImGui::Combo("Around", &axis, spin_axis_str, (int)SpinAxis::Count)) key.spin_axis = (SpinAxis)axis;
                        ImGui::Checkbox("Constant speed", &key.spin_constant_speed);
                        ImGui::SetItemTooltip("Otherwise it starts and ends slowly.");
                        ImGui::EndPopup();
                    }
                }
            }

            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Go To")) movie_goto_keyframe(data, (size_t)i);
            ImGui::SetItemTooltip("Move the view to this keyframe");
            ImGui::SameLine();
            const bool picking_this = m.look_pick_key == i;
            if (ImGui::SmallButton(picking_this ? "Click atom" : "Look at")) m.look_pick_key = picking_this ? -1 : i;
            if (key.follow && key.follow_atom >= 0) ImGui::SetItemTooltip("Looks at atom %d, tracked through the trajectory.\nClick to pick another atom in the viewport. Esc cancels.", key.follow_atom + 1);
            else ImGui::SetItemTooltip("Click an atom in the viewport for this keyframe to look at. It is tracked through the trajectory. Esc cancels.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Update position")) {
                // The camera moves to the current view's eye, still looking at the point the keyframe looks at
                const vec3_t look = camera_get_look_at(key.transform);
                ViewTransform t = data->view.target;
                if (camera_aim_at(&t, look)) key.transform = t;
                key.fov_y = data->view.camera.fov_y;
                if (key.use_frame) key.frame = data->animation.frame;
            }
            ImGui::SetItemTooltip("Move this keyframe's camera to the current view's position. What it looks at stays.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Dup")) dup_idx = i;
            ImGui::SetItemTooltip("Copy it to one second later");
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) remove_idx = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (md_array_size(m.keyframes) > 0) {
        // The camera in 3D at the preview time, which is what the distance alone does not say
        ViewTransform vt;
        float fov_y;
        camera_keyframes_evaluate(&vt, &fov_y, m.keyframes, md_array_size(m.keyframes), (double)m.playhead, m.loop);
        const vec3_t look = camera_get_look_at(vt);
        ImGui::TextDisabled("At %.2f s: eye (%.2f, %.2f, %.2f)  looks at (%.2f, %.2f, %.2f)  distance %.2f",
            m.playhead, vt.position.x, vt.position.y, vt.position.z, look.x, look.y, look.z, vt.distance);
        if (data->visuals.dof.enabled) {
            ImGui::TextDisabled("Focus at %.2f from the eye", dof_focus_depth(data, vt));
        }
    }

    if (locked) return;

    if (move_from >= 0 && move_from != move_to) {
        const int n = (int)md_array_size(m.keyframes);
        if (move_from < n && 0 <= move_to && move_to < n) {
            // The times stay with their places in the list: it is the keys that are moved along them
            std::vector<double> times(n);
            for (int k = 0; k < n; ++k) times[k] = m.keyframes[k].time;
            const CameraKeyframe moved = m.keyframes[move_from];
            if (move_from < move_to) memmove(m.keyframes + move_from, m.keyframes + move_from + 1, (size_t)(move_to - move_from) * sizeof(CameraKeyframe));
            else                     memmove(m.keyframes + move_to + 1, m.keyframes + move_to, (size_t)(move_from - move_to) * sizeof(CameraKeyframe));
            m.keyframes[move_to] = moved;
            for (int k = 0; k < n; ++k) m.keyframes[k].time = times[k];
        }
    }
    if (dup_idx >= 0) {
        CameraKeyframe copy = m.keyframes[dup_idx];
        copy.time = MIN(copy.time + 1.0, (double)movie_len);
        copy.spin_turns = 0;
        md_array_push(m.keyframes, copy, data->allocator.persistent);
        resort = true;
    }
    if (remove_idx >= 0) {
        // Keeps the order, unlike swap-and-pop
        CameraKeyframe* keys = m.keyframes;
        const size_t n = md_array_size(keys);
        memmove(keys + remove_idx, keys + remove_idx + 1, (n - remove_idx - 1) * sizeof(CameraKeyframe));
        md_array_pop(keys);
    }
    if (resort) {
        movie_sort_keyframes(data);
    }
}

// Look parameters keyed over the movie: you set one up in the Settings as it should look at some time, and key it.
static void draw_movie_param_section(ApplicationState* data, float movie_len) {
    auto& m = data->movie;
    const int num_params = (int)(sizeof(movie_param_table) / sizeof(movie_param_table[0]));

    if (ImGui::Checkbox("Animate parameters", &m.animate_params)) {
        // Turning it off lets go of them
        movie_params_apply(data, (double)m.playhead);
    }
    ImGui::SetItemTooltip("The keyed parameters follow their keys when the movie is scrubbed, previewed or recorded.\nOff, they stay as they are.");

    m.param_selected = CLAMP(m.param_selected, 0, num_params - 1);
    const MovieParamDesc& sel = movie_param_table[m.param_selected];
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
    if (ImGui::BeginCombo("##param", sel.label)) {
        for (int i = 0; i < num_params; ++i) {
            if (ImGui::Selectable(movie_param_table[i].label, i == m.param_selected)) m.param_selected = i;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Key Now")) {
        movie_key_param(data, sel.id);
    }
    ImGui::SetItemTooltip("Keys the value the parameter has now at the preview time.\nSet it up in the Settings first, then key it.");
    if (sel.tip) ImGui::TextDisabled("%s", sel.tip);
    ImGui::TextDisabled("The keys can be dragged in the Movie Timeline window.");

    if (m.param_keys.empty()) return;

    bool resort = false;
    int  remove_idx = -1;
    int  prev_param = -1;

    if (ImGui::BeginTable("##param_keys", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Parameter");
        ImGui::TableSetupColumn("Time (s)");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("Ease");
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4.5f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)m.param_keys.size(); ++i) {
            ParamKey& key = m.param_keys[i];
            const MovieParamDesc* d = movie_param_desc(key.param);
            const bool first_of_param = key.param != prev_param;
            prev_param = key.param;
            ImGui::PushID(i);
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(d ? d->label : "(unknown)");

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            double t = key.time;
            if (ImGui::InputDouble("##time", &t, 0.0, 0.0, "%.2f")) {
                key.time = CLAMP(t, 0.0, (double)movie_len);
            }
            resort |= ImGui::IsItemDeactivatedAfterEdit();

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (d && d->color) {
                ImGui::ColorEdit3("##value", key.value, ImGuiColorEditFlags_NoInputs);
            } else if (d) {
                ImGui::DragFloat("##value", &key.value[0], (d->hi - d->lo) * 0.005f, d->lo, d->hi, "%.3g");
            }

            ImGui::TableNextColumn();
            if (first_of_param) {
                ImGui::TextDisabled("-");
            } else {
                ImGui::SetNextItemWidth(-FLT_MIN);
                int ease = (int)key.ease;
                if (ImGui::Combo("##ease", &ease, key_ease_str, (int)KeyEase::Count)) key.ease = (KeyEase)ease;
                ImGui::SetItemTooltip("How the value moves in the stretch leading to this key");
            }

            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Remove")) remove_idx = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (remove_idx >= 0) {
        m.param_keys.erase(m.param_keys.begin() + remove_idx);
        // The parameter lets go of its values if this was its last key
        movie_params_apply(data, (double)m.playhead);
    }
    if (resort) {
        movie_param_sort(data);
    }
}

static void draw_movie_window(ApplicationState* data) {
    ASSERT(data);
    auto& m = data->movie;
    const bool recording = m.state == MovieRecordingState::Recording;
    char path_buf[2048] = "";

    ImGui::SetNextWindowSize(ImVec2(560, 780), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Movie", &m.show_window, ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::End();
        return;
    }

    const double max_frame = (double)(run_num_frames(data) > 0 ? run_num_frames(data) - 1 : 0);
    if (m.end_frame <= 0.0 && m.start_frame <= 0.0) {
        m.end_frame = max_frame;
    }

    int frame_w = 0, frame_h = 0;
    movie_frame_size(data, &frame_w, &frame_h);

    // --- Recording ---
    if (!recording) {
        const bool can_start = !str_empty(m.output_dir) && !m.sink;
        ImGui::BeginDisabled(!can_start);
        if (ImGui::Button("Start Recording")) {
            movie_recording_start(data);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (str_empty(m.output_dir)) {
            ImGui::TextDisabled("Select an output folder first");
        } else if (m.sink) {
            const frame_sink::Status st = frame_sink::status(m.sink);
            ImGui::TextDisabled("Still writing the previous movie, %d frame(s) left", st.queued);
        } else {
            ImGui::TextDisabled("%d frames, %.2f s, %dx%d", movie_num_frames(data), movie_duration(data), frame_w, frame_h);
        }
    } else {
        if (ImGui::Button("Stop Recording")) {
            movie_recording_stop(data);
        }
        ImGui::SameLine();
        ImGui::Text("Recording frame %d / %d (%.2f s)", m.frame_index, movie_num_frames(data), m.cur_time);
    }

    ImGui::BeginDisabled(recording);
    ImGui::BeginDisabled(!m.history.can_undo());
    if (ImGui::Button("Undo")) movie_undo(data);
    ImGui::SetItemTooltip("Undo the last change to the keyframes or the look parameters. Ctrl+Z");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!m.history.can_redo());
    if (ImGui::Button("Redo")) movie_redo(data);
    ImGui::SetItemTooltip("Ctrl+Y or Ctrl+Shift+Z");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("Timeline window", &m.show_timeline_window);
    ImGui::SetItemTooltip("The timeline with the keyframes and the look parameters, which can be dragged. Dock it wide, below the viewport.");

    ImGui::BeginDisabled(recording);

    if (ImGui::CollapsingHeader("Output", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Select Output Folder...")) {
            if (application::file_dialog(path_buf, sizeof(path_buf), application::FileDialogFlag_Dir)) {
                size_t path_len = strnlen(path_buf, sizeof(path_buf));
                str_free(m.output_dir, data->allocator.persistent);
                m.output_dir = str_copy({path_buf, path_len}, data->allocator.persistent);
            }
        }
        if (!str_empty(m.output_dir)) {
            ImGui::TextWrapped(STR_FMT, STR_ARG(m.output_dir));
        } else {
            ImGui::TextDisabled("No output folder selected");
        }

        ImGui::InputText("Filename Prefix", m.filename_prefix, sizeof(m.filename_prefix));

        m.output = (MovieOutput)CLAMP((int)m.output, 0, (int)MovieOutput::Count - 1);
        if (ImGui::BeginCombo("Format", movie_output_str[(int)m.output])) {
            for (int i = 0; i < (int)MovieOutput::Count; ++i) {
                if (ImGui::Selectable(movie_output_str[i], i == (int)m.output)) {
                    m.output = (MovieOutput)i;
                }
            }
            ImGui::EndCombo();
        }
        if (m.output == MovieOutput::Mp4) {
            ImGui::SliderInt("Quality (CRF)", &m.crf, 0, 51);
            ImGui::SetItemTooltip("x264 constant rate factor. Lower is better and larger: 18 is close to lossless, 23 is the x264 default.");
            ImGui::InputText("ffmpeg", m.ffmpeg_path, sizeof(m.ffmpeg_path));
            ImGui::SetItemTooltip("The ffmpeg executable. Found on the PATH if it is only a name.\nFrames are piped straight into it, no image files are written.");
        } else {
            ImGui::TextWrapped("Writes a numbered PNG sequence ('%s_00000.png', ...), to be encoded into a video with ffmpeg.", m.filename_prefix);
            ImGui::BeginDisabled(str_empty(m.output_dir));
            if (ImGui::Button("Copy ffmpeg command")) {
                char cmd[2048];
                // Quoted for a shell; the folder is only copied, never executed from here. libx264 with yuv420p needs even dimensions, hence the scale.
                snprintf(cmd, sizeof(cmd), "ffmpeg -framerate %g -i \"" STR_FMT "/%s_%%05d.png\" -vf \"scale=trunc(iw/2)*2:trunc(ih/2)*2\" -c:v libx264 -pix_fmt yuv420p \"" STR_FMT "/%s.mp4\"",
                    m.fps, STR_ARG(m.output_dir), m.filename_prefix,
                    STR_ARG(m.output_dir), m.filename_prefix);
                ImGui::SetClipboardText(cmd);
            }
            ImGui::EndDisabled();
        }

        m.resolution = (ScreenshotResolution)MIN((int)m.resolution, (int)ScreenshotResolution::Count - 1);
        if (ImGui::BeginCombo("Resolution", screenshot_resolution_str[(int)m.resolution])) {
            for (int i = 0; i < (int)ScreenshotResolution::Count; ++i) {
                if (ImGui::Selectable(screenshot_resolution_str[i], (i == (int)m.resolution))) {
                    m.resolution = (ScreenshotResolution)i;
                }
            }
            ImGui::EndCombo();
        }
        if (m.resolution == ScreenshotResolution::Custom) {
            ImGui::InputInt("Res X", &m.res_x);
            ImGui::InputInt("Res Y", &m.res_y);
            m.res_x = CLAMP(m.res_x, 640, 16384);
            m.res_y = CLAMP(m.res_y, 480, 16384);
        } else if (m.resolution == ScreenshotResolution::Window) {
            ImGui::TextDisabled("%dx%d, the size of the window when recording starts", frame_w, frame_h);
        }

        ImGui::InputFloat("Output FPS", &m.fps, 1.0f, 5.0f, "%.1f");
        m.fps = CLAMP(m.fps, 1.0f, 240.0f);
    }

    if (ImGui::CollapsingHeader("Timeline", ImGuiTreeNodeFlags_DefaultOpen)) {
        double frame_range[2] = { m.start_frame, m.end_frame };
        const double min_frame = 0.0;
        if (ImGui::SliderScalarN("Trajectory Frames", ImGuiDataType_Double, frame_range, 2, &min_frame, &max_frame, "%.0f")) {
            // Start after end plays the trajectory backwards
            m.start_frame = CLAMP(frame_range[0], 0.0, max_frame);
            m.end_frame   = CLAMP(frame_range[1], 0.0, max_frame);
        }

        double unused_frame;
        if (camera_keyframes_evaluate_frame(&unused_frame, m.keyframes, md_array_size(m.keyframes), 0.0)) {
            ImGui::TextWrapped("Keyframes with a frame decide how the trajectory plays, so the frames and times below are not used. "
                "Only the length of the movie is: turn off 'Trajectory at Animation speed' to set it.");
        }

        ImGui::Checkbox("Trajectory at Animation speed", &m.duration_auto);
        ImGui::SetItemTooltip("On: the movie lasts as long as the trajectory takes to play at the Animation panel's speed.\n"
            "Off: set the duration yourself and choose when the trajectory plays within it.\n"
            "Set both trajectory frames equal to hold the trajectory still while the camera moves.");
        if (m.duration_auto) {
            ImGui::TextDisabled("%.2f s, %d frames", movie_duration(data), movie_num_frames(data));
        } else {
            ImGui::InputFloat("Duration (s)", &m.duration, 0.5f, 5.0f, "%.2f");
            m.duration = CLAMP(m.duration, 0.01f, 3600.0f);
            ImGui::SliderFloat("Trajectory starts (s)", &m.traj_begin, 0.0f, m.duration, "%.2f");
            ImGui::SliderFloat("Trajectory ends (s)", &m.traj_end, 0.0f, m.duration, "%.2f");
            m.traj_begin = CLAMP(m.traj_begin, 0.0f, m.duration);
            m.traj_end   = CLAMP(m.traj_end,   m.traj_begin, m.duration);
            ImGui::TextDisabled("%d frames", movie_num_frames(data));
        }
    }

    const float movie_len = (float)movie_duration(data);
    m.playhead = CLAMP(m.playhead, 0.0f, movie_len);

    ImGui::EndDisabled();

    if (ImGui::CollapsingHeader("Camera Keyframes", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginDisabled(recording);
        ImGui::Checkbox("Animate camera", &m.animate_camera);
        ImGui::SetItemTooltip("Record with the camera following the keyframes, instead of staying where it is.");
        ImGui::SameLine();
        ImGui::Checkbox("Show path in viewport", &m.show_path);
        ImGui::SetItemTooltip("The path of the camera (blue) and of what it looks at (yellow), with the camera at each keyframe.\nThe green camera is where the playhead is.");

        ImGui::Checkbox("Seamless loop", &m.loop);
        ImGui::SetItemTooltip("The camera path is cyclic: it moves through the end into the start without a corner.\nFor that the movie has to end in the pose it starts in, 'Close Loop' sets that up.");
        ImGui::SameLine();
        if (ImGui::Button("Close Loop")) {
            movie_close_loop(data);
        }
        ImGui::SetItemTooltip("Ends the movie in the pose of the first keyframe and turns the loop on.");

        if (ImGui::Button(m.preview_playing ? "Pause Preview" : "Play Preview")) {
            m.preview_playing = !m.preview_playing;
            if (m.preview_playing && m.playhead >= movie_len) m.playhead = 0.0f;
        }
        ImGui::SetItemTooltip("Plays the movie in the viewport at the speed it will have, without recording.");
        ImGui::SameLine();
        ImGui::Checkbox("Repeat", &m.preview_loop);
        if (m.preview_playing && !m.animate_camera && md_array_size(m.keyframes) > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("'Animate camera' is off, only the trajectory plays");
        }

        if (ImGui::SliderFloat("Preview time (s)", &m.playhead, 0.0f, movie_len, "%.2f")) {
            movie_apply_time(data, (double)m.playhead, true);
        }
        ImGui::SetItemTooltip("Scrub the movie: shows the trajectory frame and, with 'Animate camera' on, the camera at this time.");

        if (ImGui::Button("Add Keyframe (current view)")) {
            movie_add_keyframe(data);
        }
        ImGui::SetItemTooltip("Adds a keyframe of the current view at the preview time. Shortcut: K");
        ImGui::SameLine();
        ImGui::Checkbox("with trajectory frame", &m.key_includes_frame);
        ImGui::SetItemTooltip("Also key the trajectory frame shown now. Keys with a frame decide how the trajectory plays,\nso the speed can change between them.");

        {
            const size_t follow_count = md_bitfield_popcount(&m.follow_mask);
            if (ImGui::Button("Set Follow Target")) {
                md_bitfield_copy(&m.follow_mask, &data->selection.selection_mask);
                m.key_follow = md_bitfield_popcount(&m.follow_mask) > 0;
            }
            ImGui::SetItemTooltip("Uses the atoms selected now as what the camera can follow: it then looks at their middle.");
            ImGui::SameLine();
            ImGui::BeginDisabled(follow_count == 0);
            if (ImGui::Button("Clear##follow")) {
                md_bitfield_clear(&m.follow_mask);
                m.key_follow = false;
            }
            ImGui::SameLine();
            ImGui::Checkbox("keys follow target", &m.key_follow);
            ImGui::SetItemTooltip("New keyframes look at a point that moves with the target, kept where it is relative to the target.\nBetween a following key and a fixed one the camera blends from one to the other.");
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (follow_count > 0) ImGui::TextDisabled("%zu atoms", follow_count);
            else                  ImGui::TextDisabled("no target");
        }

        const float fs = ImGui::GetFontSize();
        ImGui::SetNextItemWidth(fs * 5.5f);
        ImGui::DragInt("##orbit_turns", &m.orbit_turns, 0.05f, -16, 16, "%d turn(s)");
        ImGui::SetItemTooltip("Whole turns around what the camera looks at. Positive is counter-clockwise seen from the tip of the axis.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs * 5.0f);
        ImGui::InputFloat("##orbit_duration", &m.orbit_duration, 0.0f, 0.0f, "%.1f s");
        m.orbit_duration = CLAMP(m.orbit_duration, 0.1f, 3600.0f);
        ImGui::SetItemTooltip("How long the orbit takes");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs * 7.0f);
        int orbit_axis = (int)m.orbit_axis;
        if (ImGui::Combo("##orbit_axis", &orbit_axis, spin_axis_str, (int)SpinAxis::Count)) m.orbit_axis = (SpinAxis)orbit_axis;
        ImGui::SetItemTooltip("What the camera turns around: its own up direction, or a world axis");
        ImGui::SameLine();
        if (ImGui::Button("Add Orbit")) {
            movie_add_orbit(data);
        }
        ImGui::SetItemTooltip("Adds a keyframe of the current view at the preview time and another after the orbit's duration,\nwhere the camera is back in the same place after its turns.");
        ImGui::EndDisabled();

        ImGui::BeginDisabled(recording);
        draw_movie_keyframe_table(data, movie_len, recording);
        ImGui::EndDisabled();
    }

    if (ImGui::CollapsingHeader("Look Parameters")) {
        ImGui::BeginDisabled(recording);
        draw_movie_param_section(data, movie_len);
        ImGui::EndDisabled();
    }

    if (ImGui::CollapsingHeader("Overlays")) {
        ImGui::BeginDisabled(recording);
        draw_movie_overlay_section(data, movie_len);
        ImGui::EndDisabled();
    }

    ImGui::End();
}

// Over the viewport while recording, and while the last frames are still being written afterwards
static void draw_movie_recording_banner(ApplicationState* state) {
    auto& m = state->movie;
    const bool recording = m.state == MovieRecordingState::Recording;
    if (!recording && !m.sink) return;

    const frame_sink::Status st = frame_sink::status(m.sink);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + ImGui::GetFontSize()), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.88f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##movie_recording_banner", nullptr, flags)) {
        const float bar_width = ImGui::GetFontSize() * 22.0f;
        if (recording) {
            const int n = movie_num_frames(state);
            ImGui::Text("Recording movie, frame %d / %d  (%.2f s)", MIN(m.frame_index, n), n, m.cur_time);
            ImGui::ProgressBar(n > 0 ? (float)m.frame_index / (float)n : 0.0f, ImVec2(bar_width, 0));
            ImGui::TextDisabled("%d written, %d waiting to be written", st.written, st.queued);
            if (ImGui::Button("Stop (Esc)")) {
                movie_recording_stop(state);
            }
        } else {
            ImGui::Text("Writing the movie, %d frame(s) left", st.queued);
            ImGui::ProgressBar(st.submitted > 0 ? (float)(st.written + st.failed) / (float)st.submitted : 1.0f, ImVec2(bar_width, 0));
        }
    }
    ImGui::End();
}

static void draw_coordinate_system_widget_window(ViewTransform* target, const ViewTransform& current) {
    const int def_size = 150;
    const int min_size = 100;
    const int max_size = 500;

    ImGui::SetNextWindowSize(ImVec2(def_size, def_size), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(min_size, min_size), ImVec2(max_size, max_size), [](ImGuiSizeCallbackData* data) {
        // Enforce a square aspect ratio
        data->DesiredSize = ImVec2(ImMax(data->DesiredSize.x, data->DesiredSize.y), ImMax(data->DesiredSize.x, data->DesiredSize.y));
    });

    // Scale down backround color to only show a faint outline
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg) * ImVec4(1.0f, 1.0f ,1.0f ,0.1f));
    defer { ImGui::PopStyleColor(); };

    const bool editable = ImGui::IsKeyDown(ImGuiMod_Alt);

    int window_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;
    if (!editable) {
        window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoNavFocus;
    }

    if (ImGui::Begin("Coordinate Widget", nullptr, window_flags)) {
        quat_t out_orientation = target->orientation;
        if (ImGui::CoordinateSystemWidget(&out_orientation, current.orientation, ImGui::GetContentRegionAvail())) {
            const vec3_t look_at = camera_get_look_at(*target);
            target->orientation = quat_normalize(out_orientation);
            target->position = camera_position_from_look_at(look_at, target->orientation, target->distance);
        }
    }
    ImGui::End();
}

static void render(ApplicationState* state) {
    // Frames of a recording are rendered at the movie's size into the G-buffer for as long as it lasts
    const bool movie_capture = state->movie.state == MovieRecordingState::Recording;
    bool do_screenshot = !str_empty(state->screenshot.path_to_file) && !movie_capture;

    uint32_t gbuffer_target_width  = state->app.framebuffer.width;
    uint32_t gbuffer_target_height = state->app.framebuffer.height;
    if (movie_capture) {
        gbuffer_target_width  = (uint32_t)state->movie.rec_w;
        gbuffer_target_height = (uint32_t)state->movie.rec_h;
    } else if (do_screenshot) {
        gbuffer_target_width  = state->screenshot.res_x;
        gbuffer_target_height = state->screenshot.res_y;
    }

    // Resize Framebuffer
    if ((state->gbuffer.width != gbuffer_target_width || state->gbuffer.height != gbuffer_target_height) &&
        (gbuffer_target_width != 0 && gbuffer_target_height != 0)) {
        gbuffer_init(&state->gbuffer, gbuffer_target_width, gbuffer_target_height);
        postprocess_pipeline::initialize(state->gbuffer.width, state->gbuffer.height);
    }

    update_view_param(state);

    gbuffer_clear(&state->gbuffer);

    const GLenum draw_buffers_opaque[] = {GL_COLOR_ATTACHMENT_COLOR, GL_COLOR_ATTACHMENT_NORMAL, GL_COLOR_ATTACHMENT_VELOCITY,
        GL_COLOR_ATTACHMENT_PICKING, GL_COLOR_ATTACHMENT_TRANSPARENCY };

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // Enable all draw buffers
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, state->gbuffer.fbo);
    glDrawBuffers((int)ARRAY_SIZE(draw_buffers_opaque), draw_buffers_opaque);

    PUSH_GPU_SECTION("G-Buffer fill")

    if (state->simulation_box.enabled && state->mold.state.unitcell.flags != 0) {
        mat3_t A = { 0 };
		md_unitcell_A_extract_float(A.elem, &state->mold.state.unitcell);
        immediate::Scope scope(state->gfx.world, "simulation box");
        immediate::box_wireframe(scope, {0,0,0}, {1,1,1}, mat4_from_mat3(A), convert_color(state->simulation_box.color));
    }

    if (!movie_capture && !do_screenshot) {
        movie_draw_camera_path(state);
    }

    {
        immediate::Scope vis_scope(state->gfx.overlay, "visualization");
        immediate::Scope vis_scope_depth(state->gfx.world, "visualization with depth");

        const md_script_vis_t& vis = state->script.vis;

        if (vis.points) {
            immediate::points(vis_scope, (immediate::Vertex*)vis.points, md_array_size(vis.points), state->script.point_color);
        }

        if (vis.triangles) {
            immediate::triangles(vis_scope, (immediate::Vertex*)vis.triangles, md_array_size(vis.triangles), state->script.triangle_color);
        }

        if (vis.lines) {
            immediate::lines(vis_scope, (immediate::Vertex*)vis.lines, md_array_size(vis.lines), state->script.line_color);
        }

        const size_t num_matrices = md_array_size(vis.sdf.matrices);
        const size_t num_structures = md_array_size(vis.sdf.structures);
        const size_t num_sdf_items = MIN(MIN(num_matrices, num_structures), 100);

        if (num_matrices != num_structures) {
            VIAMD_LOG_DEBUG("SDF visualization returned mismatched matrix and structure counts (%zu, %zu)", num_matrices, num_structures);
        }
        if (num_sdf_items > 0) {
            const vec4_t col_x = { 1, 0, 0, 0.7f };
            const vec4_t col_y = { 0, 1, 0, 0.7f };
            const vec4_t col_z = { 0, 0, 1, 0.7f };
            const float ext = vis.sdf.extent * 0.25f;
            const vec3_t box_ext = vec3_set1(vis.sdf.extent);
            for (size_t i = 0; i < num_sdf_items; ++i) {
                const mat4_t model_matrix = mat4_inverse(vis.sdf.matrices[i]);
                immediate::basis(vis_scope, model_matrix, ext, col_x, col_y, col_z);
                immediate::box_wireframe(vis_scope_depth, -box_ext, box_ext, model_matrix);
            }
        }
    }

    if (!use_gfx) {
        // DRAW VELOCITY OF STATIC OBJECTS
        PUSH_GPU_SECTION("Blit Static Velocity")
        glDrawBuffer(GL_COLOR_ATTACHMENT_VELOCITY);
        glDepthMask(0);
        postprocessing::blit_static_velocity(state->gbuffer.tex.depth, state->view.param);
        glDepthMask(1);
        POP_GPU_SECTION()
    }
    glDepthMask(1);
    glColorMask(1, 1, 1, 1);

    // DRAW REPRESENTATIONS
    PUSH_GPU_SECTION("Draw Opaque")
    glDrawBuffers((int)ARRAY_SIZE(draw_buffers_opaque), draw_buffers_opaque);
    draw_representations_opaque(state);
    viamd::event_system_broadcast_event(viamd::EventType_ViamdRenderOpaque, viamd::EventPayloadType_ApplicationState, state);
    POP_GPU_SECTION()

    immediate::RenderParams params = {};
    params.view = state->view.param.matrix.curr.view;
    params.proj = state->view.param.matrix.curr.proj;

    // standard alpha blending
    PUSH_GPU_SECTION("Immediate world")
    //glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    immediate::render(state->gfx.world, params);
    //glDisable(GL_BLEND);
    POP_GPU_SECTION()

    glDrawBuffer(GL_COLOR_ATTACHMENT_TRANSPARENCY);

    if (!use_gfx) {
        PUSH_GPU_SECTION("Selection")
        const bool atom_selection_empty = md_bitfield_empty(&state->selection.selection_mask);
        const bool atom_highlight_empty = md_bitfield_empty(&state->selection.highlight_mask);

        glDepthMask(0);

        // @NOTE(Robin): This is a b*tch to get right, What we want is to separate in a single pass, the visible selected from the
        // non visible selected. In order to achieve this, we start with a cleared stencil of value 1 then either set it to zero selected and not visible
        // and to two if it is selected and visible. But the visible atoms should always be able to write over a non visible 0, but not the other way around.
        // Hence the GL_GREATER stencil test against the reference value of 2.

        if (!atom_selection_empty) {
            glColorMask(0, 0, 0, 0);

            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_EQUAL);

            glEnable(GL_STENCIL_TEST);
            glStencilMask(0xFF);

            glClearStencil(1);
            glClear(GL_STENCIL_BUFFER_BIT);

            glStencilFunc(GL_GREATER, 0x02, 0xFF);
            glStencilOp(GL_KEEP, GL_ZERO, GL_REPLACE);
            draw_representations_opaque_lean_and_mean(state, AtomBit_Selected | AtomBit_Visible);

            glDisable(GL_DEPTH_TEST);

            glStencilMask(0x0);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            glColorMask(1, 1, 1, 1);

            glStencilFunc(GL_EQUAL, 2, 0xFF);
            postprocessing::blit_color(state->selection.color.selection.visible);

            glStencilFunc(GL_EQUAL, 0, 0xFF);
            postprocessing::blit_color(state->selection.color.selection.hidden);
        }

        if (!atom_highlight_empty) {
            glColorMask(0, 0, 0, 0);

            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_EQUAL);

            glEnable(GL_STENCIL_TEST);
            glStencilMask(0xFF);

            glClearStencil(1);
            glClear(GL_STENCIL_BUFFER_BIT);

            glStencilFunc(GL_GREATER, 0x02, 0xFF);
            glStencilOp(GL_KEEP, GL_ZERO, GL_REPLACE);
            draw_representations_opaque_lean_and_mean(state, AtomBit_Highlighted | AtomBit_Visible);

            glDisable(GL_DEPTH_TEST);

            glStencilMask(0x0);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            glColorMask(1, 1, 1, 1);

            glStencilFunc(GL_EQUAL, 2, 0xFF);
            vec4_t col_vis = state->selection.color.highlight.visible;
            col_vis.w += sin(ImGui::GetTime() * HIGHLIGHT_PULSE_TIME_SCALE) * HIGHLIGHT_PULSE_ALPHA_SCALE;

            postprocessing::blit_color(col_vis);

            glStencilFunc(GL_EQUAL, 0, 0xFF);
            postprocessing::blit_color(state->selection.color.highlight.hidden);
        }

        glDisable(GL_STENCIL_TEST);

        if (!atom_selection_empty) {
            PUSH_GPU_SECTION("Desaturate")
            const float saturation = state->selection.color.saturation;
            glDrawBuffer(GL_COLOR_ATTACHMENT_COLOR);
            postprocessing::scale_hsv(state->gbuffer.tex.color, vec3_t{1, saturation, 1});
            POP_GPU_SECTION()
        }

        glDepthFunc(GL_LESS);
        glDepthMask(0);
        glColorMask(1,1,1,1);
        POP_GPU_SECTION()
    }

    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glColorMask(1, 1, 1, 1);

    glDrawBuffer(GL_COLOR_ATTACHMENT_TRANSPARENCY);

    draw_representations_transparent(state);
    viamd::event_system_broadcast_event(viamd::EventType_ViamdRenderTransparent, viamd::EventPayloadType_ApplicationState, state);

    const GLenum draw_buffers_transparent[] = { GL_COLOR_ATTACHMENT_TRANSPARENCY, 0, GL_COLOR_ATTACHMENT_VELOCITY, GL_COLOR_ATTACHMENT_PICKING };
	glDrawBuffers(ARRAY_SIZE(draw_buffers_transparent), draw_buffers_transparent);

    glDisable(GL_CULL_FACE);

    // Blend colour only. glEnable(GL_BLEND) applies to every attachment, and
    // this pass also writes velocity and picking: alpha-blending those means a
    // translucent overlay fragment scales the velocity underneath it by
    // (1 - alpha) and mixes the picking index with whatever was already there,
    // neither of which is a meaningful operation on that data.
    glEnablei(GL_BLEND, 0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisablei(GL_BLEND, 1);
    glDisablei(GL_BLEND, 2);
    glDisablei(GL_BLEND, 3);

    PUSH_GPU_SECTION("Immediate overlay")
    glDisable(GL_DEPTH_TEST);
    immediate::render(state->gfx.overlay, params);
    POP_GPU_SECTION()

    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);

    POP_GPU_SECTION()  // G-buffer

    if (movie_capture || (do_screenshot && state->screenshot.hide_gui)) {
        // Activate gbuffer to store the frame
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, state->gbuffer.fbo);
        glViewport(0, 0, state->gbuffer.width, state->gbuffer.height);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
    } else {
        // Activate backbuffer
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glViewport(0, 0, state->app.framebuffer.width, state->app.framebuffer.height);
        glDrawBuffer(GL_BACK);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    PUSH_GPU_SECTION("Postprocessing")
    postprocess_pipeline::Settings settings = {};
    postprocess_pipeline::Inputs inputs = {};

    settings.background_color = state->visuals.background.color * state->visuals.background.intensity;

    settings.ssao.enabled = state->visuals.ssao.enabled;
    settings.ssao.intensity = state->visuals.ssao.intensity;
    settings.ssao.radius = state->visuals.ssao.radius;
    settings.ssao.bias = state->visuals.ssao.bias;

    settings.tonemap.enabled = state->visuals.tonemapping.enabled;
    settings.tonemap.mode = state->visuals.tonemapping.tonemapper;
    settings.tonemap.exposure = state->visuals.tonemapping.exposure;
    settings.tonemap.gamma = state->visuals.tonemapping.gamma;

    settings.dof.enabled = state->visuals.dof.enabled;
    state->visuals.dof.focus_depth = dof_focus_depth(state, state->view.camera);
    settings.dof.focus_depth = state->visuals.dof.focus_depth;
    settings.dof.focus_scale = state->visuals.dof.focus_scale;

    settings.fxaa.enabled = state->visuals.fxaa.enabled;

    constexpr float MOTION_BLUR_REFERENCE_DT = 1.0f / 60.0f;
    const float dt_compensation = MOTION_BLUR_REFERENCE_DT / (float)state->app.timing.delta_s;
    const float motion_scale = state->visuals.temporal_aa.motion_blur.motion_scale * dt_compensation;
    settings.taa.enabled = state->visuals.temporal_aa.enabled;
    settings.taa.feedback_min = state->visuals.temporal_aa.feedback_min;
    settings.taa.feedback_max = state->visuals.temporal_aa.feedback_max;
    settings.taa.motion_blur.enabled = state->visuals.temporal_aa.motion_blur.enabled;
    settings.taa.motion_blur.motion_scale = motion_scale;

    settings.sharpen.enabled = state->visuals.temporal_aa.enabled && state->visuals.sharpen.enabled;
    settings.sharpen.weight = state->visuals.sharpen.weight;

    inputs.depth = state->gbuffer.tex.depth;
    inputs.color = state->gbuffer.tex.color;
    inputs.normal = state->gbuffer.tex.normal;
    inputs.velocity = state->gbuffer.tex.velocity;
    inputs.transparency = state->gbuffer.tex.transparency;
    inputs.history = settings.taa.enabled ? state->gbuffer.tex.history : 0;

    postprocess_pipeline::execute(inputs, settings, state->view.param);
    POP_GPU_SECTION()

    if (movie_capture) {
        // Samples of one output frame are accumulated by temporal AA, the last of them is the frame
        auto& m = state->movie;
        if (m.can_capture) {
            m.samples_done += 1;
            if (m.samples_done >= m.sample_target) {
                movie_overlays_render(state, m.cur_time);
                movie_capture_frame(state);
                m.frame_index += 1;
                m.samples_done = 0;
            }
        }
        movie_blit_preview(state);
    }

    if (do_screenshot && state->screenshot.hide_gui) {
        state->screenshot.sample_count += 1;

        ImGui::OpenPopup("Capturing Frame");
        ImVec2 size = ImGui::CalcTextSize("Capturing Frame");
        ImGui::SetNextWindowSize(size * ImVec2(2, 5), ImGuiCond_Always);
        if (ImGui::BeginPopupModal("Capturing Frame", 0, ImGuiWindowFlags_NoResize)) {
            float fraction = (float)state->screenshot.sample_count / (float)state->screenshot.sample_target;
            ImGui::ProgressBar(fraction);
            ImGui::EndPopup();
        }

        if (state->screenshot.sample_count == state->screenshot.sample_target) {
            create_screenshot(state->screenshot.path_to_file);
            str_free(state->screenshot.path_to_file, state->allocator.persistent);
            state->screenshot.path_to_file = {};
            state->screenshot.sample_count  = 0;
            state->screenshot.sample_target = 0;
            ImGui::CloseCurrentPopup();
        }

        // Activate backbuffer
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glViewport(0, 0, state->app.framebuffer.width, state->app.framebuffer.height);
        glDrawBuffer(GL_BACK);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    PUSH_GPU_SECTION("Imgui render")
    application::render_imgui(&state->app);
    POP_GPU_SECTION()

    if (do_screenshot && !state->screenshot.hide_gui) {
        create_screenshot(state->screenshot.path_to_file);
        str_free(state->screenshot.path_to_file, state->allocator.persistent);
        state->screenshot.path_to_file = {};
    }
}

static void draw_representations_opaque(ApplicationState* state) {
    ASSERT(state);

    if (state->mold.sys.atom.count == 0) {
        return;
    }

#if EXPERIMENTAL_GFX_API
    if (use_gfx) {
        const uint32_t instance_count = 10000;
        static mat4_t* transforms = 0;

        if (transforms == 0) {
            auto rnd = []() -> float {
                return (float)rand() / RAND_MAX;
            };
            for (uint32_t i = 0; i < instance_count; ++i) {
                vec3_t axis = {rnd(), rnd(), rnd()};
                quat_t ori = quat_angle_axis(rnd() * TWO_PI, vec3_normalize(axis));
                mat4_t R = mat4_from_quat(ori);
                mat4_t T = mat4_translate(rnd() * 4000, rnd() * 4000, rnd() * 4000);
                mat4_t M = T * R;
                md_urange_t range = {0, (int32_t)state->mold.sys.atom.count};
                md_array_push(transforms, M, persistent_alloc);
            }
        }

        md_gfx_draw_op_t* draw_ops = 0;
        for (int64_t i = 0; i < md_array_size(state->representation.reps); ++i) {
            if (state->representation.reps[i].enabled) {
                md_gfx_draw_op_t op;
                op.structure = state->mold.gfx_structure;
                op.representation = state->representation.reps[i].gfx_rep;
                op.model_mat = NULL;
                md_array_push(draw_ops, op, frame_alloc);
                
                for (uint32_t j = 0; j < instance_count; ++j) {
                    md_gfx_draw_op_t op;
                    op.structure = state->mold.gfx_structure;
                    op.representation = state->representation.reps[i].gfx_rep;
                    op.model_mat = &transforms[j];
                    md_array_push(draw_ops, op, frame_alloc);
                }
                
            }
        }

        md_gfx_draw((uint32_t)md_array_size(draw_ops), draw_ops, &state->view.param.matrix.curr.proj, &state->view.param.matrix.curr.view, &state->view.param.matrix.inv.proj, &state->view.param.matrix.inv.view);
    } else {
#endif
        const size_t num_representations = md_array_size(state->representation.reps);
        if (num_representations == 0) return;

		glEnable(GL_DEPTH_TEST);

        md_array(md_gl_draw_op_t) draw_ops = 0;
        for (size_t i = 0; i < num_representations; ++i) {
            const Representation& rep = state->representation.reps[i];

            if (RepresentationType::SpaceFill <= rep.type && rep.type <= RepresentationType::Cartoon) {
                if (rep.enabled && rep.type_is_valid) {
                    md_gl_draw_op_t op = {
                        .type = (md_gl_rep_type_t)rep.type,
                        .args = {},
                        .rep = state->representation.reps[i].md_rep,
                        .model_matrix = NULL,
                    };
                    switch (rep.type) {
                    case RepresentationType::SpaceFill:
                        op.args.space_fill.radius_scale = rep.scale.x;
                        break;
                    case RepresentationType::Licorice:
                        op.args.licorice.radius = rep.scale.x;
                        op.args.licorice.color_mode = (md_gl_bond_mode_t)rep.bond_color;
                        op.args.licorice.sharpness = rep.bond_sharpness;
                        op.args.licorice.uniform_color = convert_color(rep.bond_base_color);
                        if (rep.tint_scale > 0.0f || rep.saturation < 1.0f) {
                            tint_colors(&op.args.licorice.uniform_color, 1, convert_color(rep.tint_color), rep.tint_scale, rep.saturation);
                        }
                        break;
                    case RepresentationType::BallAndStick:
                        op.args.ball_and_stick.ball_scale = rep.scale.x;
                        op.args.ball_and_stick.stick_radius = rep.scale.y;
                        op.args.ball_and_stick.color_mode = (md_gl_bond_mode_t)rep.bond_color;
                        op.args.ball_and_stick.sharpness = rep.bond_sharpness;
                        op.args.ball_and_stick.uniform_color = convert_color(rep.bond_base_color);
                        if (rep.tint_scale > 0.0f || rep.saturation < 1.0f) {
                            tint_colors(&op.args.ball_and_stick.uniform_color, 1, convert_color(rep.tint_color), rep.tint_scale, rep.saturation);
                        }
                        break;
                    case RepresentationType::Ribbons:
                        op.args.ribbons.width_scale = rep.scale.x;
                        op.args.ribbons.thickness_scale = rep.scale.y;
                        break;
                    case RepresentationType::Cartoon:
                        op.args.cartoon.coil_scale = rep.scale.x;
                        op.args.cartoon.sheet_scale = rep.scale.y;
                        op.args.cartoon.helix_scale = rep.scale.z;
                        break;
                    default:
                        break;
                    }
                    md_array_push(draw_ops, op, frame_alloc);
                }
            } else if (rep.type == RepresentationType::DipoleMoment) {
                // immediate draw of dipole moment as arrow
                vec3_t dipole_vec = {0, 0, 0};
                vec3_t dipole_org = {0, 0, 0};
                if (dipole_moment_read(&dipole_vec, &dipole_org, state->mold.sys, rep.dipole.dipole_key, rep.dipole.dipole_index)) {
                    // The representation's own (key, index) IS the picking address: the group's
                    // range was reserved under that key this frame, and the shader adds the base to
                    // the per primitive index, so the element index is what goes on the primitive.
                    // A group that did not fit in the picking space draws with INVALID_PICKING_IDX,
                    // which the shader passes through untouched - visible, just not pickable.
                    const PickingSpace* space = picking_handler_current_space(&state->picking_handler);
                    const PickingRange* range = space ? picking_space_find_range(*space, PickingDomain_Dipole, rep.dipole.dipole_key) : nullptr;

                    immediate::Scope scope(state->gfx.world, "debug_dipole_moment");
                    immediate::set_picking_base_idx(scope, range ? range->beg : 0);

                    const vec3_t org = dipole_org;
                    const vec3_t vec = dipole_vec * (float)rep.dipole.scale;

                    // cylinder body
                    const float body_scale = 0.8f;

                    const float body_radius = rep.dipole.radius;
                    const float head_radius = body_radius * 1.5f;

                    vec3_t cyl_beg = rep.dipole.offset + org;
                    vec3_t cyl_end = rep.dipole.offset + org + vec * body_scale;
                    vec3_t arrow_end = rep.dipole.offset + org + vec;

                    uint32_t color_u32 = convert_color(rep.dipole.color);

                    uint32_t picking_idx = range ? rep.dipole.dipole_index : INVALID_PICKING_IDX;

                    immediate::cylinder(scope, cyl_beg, cyl_end, body_radius, color_u32, picking_idx);
                    immediate::cone(scope, cyl_end, arrow_end, head_radius, color_u32, picking_idx);
                }
            }
        }

		// MIN HALF BOX EXTENT AS MAX BOND LENGTH APPROXIMATION
        float max_bond_length = 10.0f;
        if (md_unitcell_flags(&state->mold.state.unitcell) == 0) {
            vec3_t aabb_ext = vec3_sub(state->mold.sys_aabb_max, state->mold.sys_aabb_min);
            max_bond_length = MAX(3.0f, vec3_length(aabb_ext) * 0.5f); // Max bond length should not exceed half the diagonal of the bounding box
        }
        else {
            vec3_t ext = { 0 };
            md_unitcell_diag_extract_float(ext.elem, &state->mold.state.unitcell);
            float min_half_box_ext = vec3_reduce_min(vec3_mul1(ext, 0.5f));
            max_bond_length = MAX(3.0, min_half_box_ext);
        }

        md_gl_draw_args_t args = {
            .shaders = state->gl.shaders,
            .draw_operations = {
                .count = (uint32_t)md_array_size(draw_ops),
                .ops = draw_ops,
            },
            .view_transform = {
                .view_matrix = &state->view.param.matrix.curr.view.elem[0][0],
                .proj_matrix = &state->view.param.matrix.curr.proj.elem[0][0],
                // These two are for temporal anti-aliasing reprojection (optional)
                .prev_view_matrix = &state->view.param.matrix.prev.view.elem[0][0],
                .prev_proj_matrix = &state->view.param.matrix.prev.proj.elem[0][0],
            },
            .picking_offset = {
                .atom_base = state->picking_range_atom.beg,
                .bond_base = state->picking_range_bond.beg,
            },
            .max_bond_length = max_bond_length,
        };

        md_gl_draw(&args);
#if EXPERIMENTAL_GFX_API
    }
#endif
}

static void draw_representations_transparent(ApplicationState* state) {
    ASSERT(state);
    if (state->mold.sys.atom.count == 0) return;

    const size_t num_representations = md_array_size(state->representation.reps);
    if (num_representations == 0) return;

    for (size_t i = 0; i < num_representations; ++i) {
        const Representation& rep = state->representation.reps[i];
        if (!rep.enabled) continue;
        if (rep.type == RepresentationType::ElectronicStructure) {
            IsoDesc iso;
            electronic_structure_iso_desc_init(&iso, rep.electronic_structure);

#if VIAMD_RECOMPUTE_ORBITAL_PER_FRAME
            flag_representation_as_dirty(&state->representation.reps[i]);
#endif

            volume::RenderDesc desc = {
                .render_target = {
                    .depth = state->gbuffer.tex.depth,
                    .color = state->gbuffer.tex.transparency,
                    .width = state->gbuffer.width,
                    .height = state->gbuffer.height,
                },
                .texture = {
                    .density_volume = rep.electronic_structure.density_vol.tex_id,
                    .color_volume = rep.electronic_structure.color_vol.tex_id,
                    .transfer_function = rep.electronic_structure.dvr.tf_tex,
                },
                .matrix = {
                    .model = rep.electronic_structure.density_vol.texture_to_world,
                    .view = state->view.param.matrix.curr.view,
                    .proj = state->view.param.matrix.curr.proj,
                    .inv_proj = state->view.param.matrix.inv.proj,
                },
                .clip_volume = {
                    .min = {0,0,0},
                    .max = {1,1,1},
                },
                .temporal = {
                    .enabled = state->visuals.temporal_aa.enabled,
                },
                .iso = {
                    .enabled = true,
                    .count = iso.count,
                    .values = iso.values,
                    .colors = iso.colors,
                    .optical_densities = iso.optical_densities,
                    .use_color_volume = rep.electronic_structure.use_atom_colors,
                },
                .dvr = {
                    .enabled = rep.electronic_structure.dvr.enabled,
                    .min_tf_value = -1.0f,
                    .max_tf_value = 1.0f,
                },
                .shading = {
                    .env_radiance = state->visuals.background.color * state->visuals.background.intensity * 0.25,
                    .roughness = 0.3f,
                    .dir_radiance = {10,10,10},
                    .ior = 1.5f,
                    .exposure = state->visuals.tonemapping.exposure,
                    .gamma = state->visuals.tonemapping.gamma,
            },
                .voxel_spacing = rep.electronic_structure.density_vol.voxel_size,
            };

            volume::render_volume(desc);

#if DEBUG
            {
				immediate::Scope scope(state->gfx.world, "debug_electronic_structure");
                immediate::box_wireframe(scope, { 0,0,0 }, { 1,1,1 }, rep.electronic_structure.density_vol.texture_to_world, immediate::COLOR_BLACK);
            }
#endif
        }
    }
}

static void draw_representations_opaque_lean_and_mean(ApplicationState* data, uint32_t mask) {
    md_gl_draw_op_t* draw_ops = 0;
    for (size_t i = 0; i < md_array_size(data->representation.reps); ++i) {
        const Representation& rep = data->representation.reps[i];

        if (rep.type > RepresentationType::Cartoon) continue;

        if (rep.enabled && rep.type_is_valid) {
            md_gl_draw_op_t op = {
                .type = (md_gl_rep_type_t)rep.type,
                .args = {},
                .rep = data->representation.reps[i].md_rep,
                .model_matrix = NULL,
            };
            MEMCPY(&op.args, &rep.scale, sizeof(op.args));
            md_array_push(draw_ops, op, frame_alloc);
        }
    }

    // MIN HALF BOX EXTENT AS MAX BOND LENGTH APPROXIMATION
    float max_bond_length = 10.0f;
    if (md_unitcell_flags(&data->mold.state.unitcell) == 0) {
        vec3_t aabb_ext = vec3_sub(data->mold.sys_aabb_max, data->mold.sys_aabb_min);
        max_bond_length = MAX(3.0f, vec3_length(aabb_ext) * 0.5f); // Max bond length should not exceed half the diagonal of the bounding box
    }
    else {
        vec3_t ext = { 0 };
        md_unitcell_diag_extract_float(ext.elem, &data->mold.state.unitcell);
        float min_half_box_ext = vec3_reduce_min(vec3_mul1(ext, 0.5f));
        max_bond_length = MAX(3.0, min_half_box_ext);
    }

    md_gl_draw_args_t args = {
        .shaders = data->gl.shaders_lean_and_mean,
        .draw_operations = {
            .count = md_array_size(draw_ops),
            .ops = draw_ops,
        },
        .view_transform = {
            .view_matrix = &data->view.param.matrix.curr.view.elem[0][0],
            .proj_matrix = &data->view.param.matrix.curr.proj.elem[0][0],
            // These two are for temporal anti-aliasing reprojection
            //.prev_model_view_matrix = &data->view.param.matrix.previous.view[0][0],
            //.prev_projection_matrix = &data->view.param.matrix.previous.proj[0][0],
        },
        .atom_mask = mask,
		.max_bond_length = max_bond_length,
    };

    md_gl_draw(&args);
}
