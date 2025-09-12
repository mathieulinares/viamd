#include <event.h>

#include <core/md_compiler.h>
#include <core/md_log.h>
#include <core/md_array.h>
#include <core/md_vec_math.h>
#include <core/md_bitfield.h>
#include <core/md_arena_allocator.h>
#include <core/md_os.h>

#include <md_script.h>
#include <md_util.h>

#include <viamd.h>
#include <serialization_utils.h>
#include <imgui_widgets.h>
#include <implot_widgets.h>
#include <implot_internal.h>

#include "gfx/gl.h"
#include "gfx/gl_utils.h"
#include "task_system.h"

#include <core/md_vec_math.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef MEGABYTES
#define MEGABYTES(x) ((x) * 1024 * 1024)
#endif

static const uint32_t density_tex_dim = 512;
static const uint32_t tex_dim = 1024;

// Vertex shader for correlation rendering (adapted from Ramachandran)
constexpr str_t v_fs_quad_src = STR_LIT(R"(
#version 150 core

void main() {
	uint idx = uint(gl_VertexID) % 3U;
	gl_Position = vec4(
		(float( idx     &1U)) * 4.0 - 1.0,
		(float((idx>>1U)&1U)) * 4.0 - 1.0,
		0, 1.0);
}
)");

// Fragment shader for colormap rendering
constexpr str_t f_shader_map_src = STR_LIT(R"(
#version 330 core

layout(location = 0) out vec4 out_frag;

uniform sampler2D u_tex_den;
uniform vec4 u_viewport;
uniform vec2 u_inv_res;
uniform vec4 u_map_colors[64];
uniform uint u_map_length;
uniform vec2 u_map_range;

vec4 map_density(float val) {
    uint length = u_map_length;
    vec2 range  = u_map_range;
    
    val = clamp((val - range.x) / (range.y - range.x), 0, 1);
    float s = val * float(length);
    float t = fract(s);
    uint i0 = clamp(uint(s) + 0U, 0U, length - 1U);
    uint i1 = clamp(uint(s) + 1U, 0U, length - 1U);
    return mix(u_map_colors[i0], u_map_colors[i1], t);
}

void main() {
    vec2 coords = vec2(gl_FragCoord.xy) * u_inv_res;
    vec2 uv = u_viewport.xy + coords * u_viewport.zw;
    float d = texture(u_tex_den, uv).r;
    out_frag = map_density(d);
}
)");

// Fragment shader for isolines/isolevels rendering
constexpr str_t f_shader_iso_src = STR_LIT(R"(
#version 330 core

layout(location = 0) out vec4 out_frag;

uniform sampler2D u_tex_den;
uniform vec4 u_viewport;
uniform vec2 u_inv_res;
uniform float u_iso_values[32];
uniform vec4  u_iso_level_colors[32];
uniform vec4  u_iso_contour_colors[32];
uniform uint  u_iso_length;
uniform float u_iso_contour_line_scale;

vec4 map_density(float val) {
    vec4 base = vec4(0,0,0,0);
    vec4 contour = vec4(0,0,0,0);
    uint length = u_iso_length;

    uint i = 0U;
    for (; i < length - 1U; ++i) {
        if (u_iso_values[i] <= val && val < u_iso_values[i + 1U]) break;
    }

    uint i0 = i;
    uint i1 = min(i + 1U, length - 1U);

    // We interpolate the colors between index i and i + 1
    float v0 = u_iso_values[i0];
    float v1 = u_iso_values[i1];

    vec4 b0 = u_iso_level_colors[i0];
    vec4 b1 = u_iso_level_colors[i1];

    float dv = fwidth(val);
    float band = dv * 2.0 * u_iso_contour_line_scale;
    base = mix(b0, b1, smoothstep(v1 - band, v1 + band, val));

    for (uint i = 0U; i < length; ++i) {
        float  v = u_iso_values[i]; 
        contour += u_iso_contour_colors[i] * smoothstep(v - band, v, val) * (1.0 - smoothstep(v, v + band, val));
    }

    return contour + base * (1.0 - contour.a);
}

void main() {
    vec2 coords = vec2(gl_FragCoord.xy) * u_inv_res;
    vec2 uv = u_viewport.xy + coords * u_viewport.zw;
    float d = texture(u_tex_den, uv).r;
    out_frag = map_density(d);
}
)");

// Blur function for density smoothing
static inline void blur_density_gaussian(float* data, int dim, float sigma) {
    if (sigma <= 0.0f) return;
    
    const int kernel_radius = (int)(3.0f * sigma);
    const int kernel_size = 2 * kernel_radius + 1;
    const float inv_sigma_sq = 1.0f / (sigma * sigma);
    
    // Create Gaussian kernel
    float* kernel = (float*)alloca(kernel_size * sizeof(float));
    float kernel_sum = 0.0f;
    for (int i = 0; i < kernel_size; ++i) {
        int x = i - kernel_radius;
        kernel[i] = expf(-0.5f * x * x * inv_sigma_sq);
        kernel_sum += kernel[i];
    }
    
    // Normalize kernel
    for (int i = 0; i < kernel_size; ++i) {
        kernel[i] /= kernel_sum;
    }
    
    // Temporary buffer
    float* temp = (float*)alloca(dim * dim * sizeof(float));
    memcpy(temp, data, dim * dim * sizeof(float));
    
    // Horizontal pass
    for (int y = 0; y < dim; ++y) {
        for (int x = 0; x < dim; ++x) {
            float sum = 0.0f;
            for (int k = 0; k < kernel_size; ++k) {
                int src_x = x + k - kernel_radius;
                if (src_x < 0) src_x = 0;
                if (src_x >= dim) src_x = dim - 1;
                sum += temp[y * dim + src_x] * kernel[k];
            }
            data[y * dim + x] = sum;
        }
    }
    
    // Vertical pass
    memcpy(temp, data, dim * dim * sizeof(float));
    for (int y = 0; y < dim; ++y) {
        for (int x = 0; x < dim; ++x) {
            float sum = 0.0f;
            for (int k = 0; k < kernel_size; ++k) {
                int src_y = y + k - kernel_radius;
                if (src_y < 0) src_y = 0;
                if (src_y >= dim) src_y = dim - 1;
                sum += temp[src_y * dim + x] * kernel[k];
            }
            data[y * dim + x] = sum;
        }
    }
}

enum CorrelationDisplayMode {
    Points,
    IsoLevels,
    IsoLines,
    Colormap,
};

// Correlation representation (similar to rama_rep_t)
struct corr_rep_t {
    uint32_t map_tex = 0;     // Colormap texture
    uint32_t iso_tex = 0;     // Isolines/isolevels texture
    float    den_sum = 0.0f;  // Density sum for normalization
    uint32_t den_tex = 0;     // Density texture (single channel)
    float    min_x = 0.0f;    // Data range for coordinate mapping
    float    max_x = 0.0f;
    float    min_y = 0.0f;
    float    max_y = 0.0f;
};

struct corr_colormap_t {
    const uint32_t* colors;
    uint32_t count;
    float min_value;
    float max_value;
};

struct corr_isomap_t {
    const float* values;
    const uint32_t* level_colors;
    const uint32_t* contour_colors;
    uint32_t count;
};

// Shader program structures
struct shader_program_t {
    GLuint program = 0;
    GLint uniform_loc_tex_den = -1;
    GLint uniform_loc_viewport = -1;
    GLint uniform_loc_inv_res = -1;
    GLint uniform_loc_map_colors = -1;
    GLint uniform_loc_map_length = -1;
    GLint uniform_loc_map_range = -1;
    GLint uniform_loc_iso_values = -1;
    GLint uniform_loc_iso_level_colors = -1;
    GLint uniform_loc_iso_contour_colors = -1;
    GLint uniform_loc_iso_length = -1;
    GLint uniform_loc_iso_contour_line_scale = -1;
};

struct ScatterSeries {
    md_array(float) x_data = 0;
    md_array(float) y_data = 0;
    md_array(int) frame_indices = 0;
    char name[64] = "";
    ImVec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
};

struct Correlation : viamd::EventHandler {
    char error[256] = "";
    bool show_window = false;
    
    // Property selection using main application's display properties
    int x_property_idx = -1;
    int y_property_idx = -1;
    
    // Scatter plot data
    md_array(ScatterSeries) series = 0;
    
    // Interaction
    int hovered_point = -1;
    int clicked_frame = -1;
    
    // Layer system (similar to Ramachandran)
    CorrelationDisplayMode display_mode[2] = { Points, Points }; // Full trajectory, Filtered trajectory
    bool show_layer[3] = {true, false, true}; // Full trajectory, Filtered trajectory (default off), Current
    ImPlotColormap colormap[2] = { ImPlotColormap_Hot, ImPlotColormap_Plasma };
    ImVec4 isoline_colors[2] = { {1,1,1,1}, {1,1,1,1} };
    float full_alpha = 0.85f;
    float filt_alpha = 0.85f;
    
    // Enhanced iso-level configuration
    int num_iso_levels = 3;
    float iso_thresholds[8] = { 0.1f, 0.3f, 0.6f, 0.8f, 0.9f, 0.95f, 0.98f, 0.99f };
    bool preserve_series = true; // Whether to preserve individual series in advanced modes
    
    // Point style for current frame
    struct {
        ImVec4 outline = {1.0f, 1.0f, 1.0f, 1.0f};
        ImVec4 fill = {0.8f, 0.3f, 0.3f, 1.0f};
        float size = 5.0f;
    } current_style;
    
    // Advanced rendering infrastructure
    corr_rep_t corr_data_full;
    corr_rep_t corr_data_filt;
    shader_program_t map_shader;
    shader_program_t iso_shader;
    uint32_t fbo = 0;  // Framebuffer object
    uint32_t vao = 0;  // Vertex array object
    float blur_sigma = 1.5f;
    
    // Task system for async density computation
    task_system::ID compute_density_full = 0;
    task_system::ID compute_density_filt = 0;
    uint64_t full_fingerprint = 0;
    uint64_t filt_fingerprint = 0;
    
    md_allocator_i* arena = 0;
    ApplicationState* app_state = 0;

    Correlation() { viamd::event_system_register_handler(*this); }

    void process_events(const viamd::Event* events, size_t num_events) final {
        for (size_t i = 0; i < num_events; ++i) {
            const viamd::Event& e = events[i];

            switch (e.type) {
            case viamd::EventType_ViamdInitialize: {
                app_state = (ApplicationState*)e.payload;
                arena = md_arena_allocator_create(app_state->allocator.persistent, MEGABYTES(1));
                init_gl_resources();
                break;
            }
            case viamd::EventType_ViamdShutdown:
                cleanup_gl_resources();
                md_arena_allocator_destroy(arena);
                break;
            case viamd::EventType_ViamdFrameTick:
                update();
                draw_window();
                break;
            case viamd::EventType_ViamdWindowDrawMenu:
                ImGui::Checkbox("Correlator 3000X(TM)", &show_window);
                break;
            case viamd::EventType_ViamdDeserialize: {
                viamd::deserialization_state_t& state = *(viamd::deserialization_state_t*)e.payload;
                if (str_eq(viamd::section_header(state), STR_LIT("Correlation"))) {
                    str_t ident, arg;
                    while (viamd::next_entry(ident, arg, state)) {
                        if (str_eq(ident, STR_LIT("show_window"))) {
                            viamd::extract_bool(show_window, arg);
                        } else if (str_eq(ident, STR_LIT("x_property_idx"))) {
                            viamd::extract_int(x_property_idx, arg);
                        } else if (str_eq(ident, STR_LIT("y_property_idx"))) {
                            viamd::extract_int(y_property_idx, arg);
                        } else if (str_eq(ident, STR_LIT("display_mode_full"))) {
                            int mode;
                            viamd::extract_int(mode, arg);
                            display_mode[0] = (CorrelationDisplayMode)mode;
                        } else if (str_eq(ident, STR_LIT("display_mode_filt"))) {
                            int mode;
                            viamd::extract_int(mode, arg);
                            display_mode[1] = (CorrelationDisplayMode)mode;
                        } else if (str_eq(ident, STR_LIT("show_layer_full"))) {
                            viamd::extract_bool(show_layer[0], arg);
                        } else if (str_eq(ident, STR_LIT("show_layer_filt"))) {
                            viamd::extract_bool(show_layer[1], arg);
                        } else if (str_eq(ident, STR_LIT("show_layer_current"))) {
                            viamd::extract_bool(show_layer[2], arg);
                        } else if (str_eq(ident, STR_LIT("preserve_series"))) {
                            viamd::extract_bool(preserve_series, arg);
                        } else if (str_eq(ident, STR_LIT("num_iso_levels"))) {
                            viamd::extract_int(num_iso_levels, arg);
                        } else if (str_eq(ident, STR_LIT("iso_thresholds"))) {
                            // Parse array of floats
                            char* ptr = (char*)arg.ptr;
                            for (int i = 0; i < 8 && ptr < arg.ptr + arg.len; ++i) {
                                iso_thresholds[i] = strtof(ptr, &ptr);
                                if (*ptr == ',') ptr++;
                            }
                        }
                    }
                }
                break;
            }
            case viamd::EventType_ViamdSerialize: {
                viamd::serialization_state_t& state = *(viamd::serialization_state_t*)e.payload;
                viamd::write_section_header(state, STR_LIT("Correlation"));
                viamd::write_bool(state, STR_LIT("show_window"), show_window);
                viamd::write_int(state, STR_LIT("x_property_idx"), x_property_idx);
                viamd::write_int(state, STR_LIT("y_property_idx"), y_property_idx);
                viamd::write_int(state, STR_LIT("display_mode_full"), (int)display_mode[0]);
                viamd::write_int(state, STR_LIT("display_mode_filt"), (int)display_mode[1]);
                viamd::write_bool(state, STR_LIT("show_layer_full"), show_layer[0]);
                viamd::write_bool(state, STR_LIT("show_layer_filt"), show_layer[1]);
                viamd::write_bool(state, STR_LIT("show_layer_current"), show_layer[2]);
                viamd::write_bool(state, STR_LIT("preserve_series"), preserve_series);
                viamd::write_int(state, STR_LIT("num_iso_levels"), num_iso_levels);
                
                // Write iso thresholds as comma-separated values
                char threshold_str[256];
                int offset = 0;
                for (int i = 0; i < 8; ++i) {
                    if (i > 0) offset += snprintf(threshold_str + offset, sizeof(threshold_str) - offset, ",");
                    offset += snprintf(threshold_str + offset, sizeof(threshold_str) - offset, "%.3f", iso_thresholds[i]);
                }
                viamd::write_str(state, STR_LIT("iso_thresholds"), str_t{threshold_str, (size_t)offset});
                break;
            }
            default:
                break;
            }
        }
    }
    
    void init_gl_resources() {
        // Initialize density textures
        gl::init_texture_2D(&corr_data_full.den_tex, density_tex_dim, density_tex_dim, GL_R32F);
        gl::init_texture_2D(&corr_data_filt.den_tex, density_tex_dim, density_tex_dim, GL_R32F);
        
        // Initialize render target textures
        gl::init_texture_2D(&corr_data_full.map_tex, tex_dim, tex_dim, GL_RGBA8);
        gl::init_texture_2D(&corr_data_full.iso_tex, tex_dim, tex_dim, GL_RGBA8);
        gl::init_texture_2D(&corr_data_filt.map_tex, tex_dim, tex_dim, GL_RGBA8);
        gl::init_texture_2D(&corr_data_filt.iso_tex, tex_dim, tex_dim, GL_RGBA8);
        
        // Initialize shaders
        GLuint vs = gl::compile_shader_from_source(v_fs_quad_src, GL_VERTEX_SHADER);
        GLuint fs_map = gl::compile_shader_from_source(f_shader_map_src, GL_FRAGMENT_SHADER);
        GLuint fs_iso = gl::compile_shader_from_source(f_shader_iso_src, GL_FRAGMENT_SHADER);
        
        if (vs && fs_map) {
            map_shader.program = glCreateProgram();
            GLuint shaders[] = {vs, fs_map};
            if (gl::attach_link_detach(map_shader.program, shaders, 2)) {
                map_shader.uniform_loc_tex_den = glGetUniformLocation(map_shader.program, "u_tex_den");
                map_shader.uniform_loc_viewport = glGetUniformLocation(map_shader.program, "u_viewport");
                map_shader.uniform_loc_inv_res = glGetUniformLocation(map_shader.program, "u_inv_res");
                map_shader.uniform_loc_map_colors = glGetUniformLocation(map_shader.program, "u_map_colors");
                map_shader.uniform_loc_map_length = glGetUniformLocation(map_shader.program, "u_map_length");
                map_shader.uniform_loc_map_range = glGetUniformLocation(map_shader.program, "u_map_range");
            }
        }
        
        if (vs && fs_iso) {
            iso_shader.program = glCreateProgram();
            GLuint shaders[] = {vs, fs_iso};
            if (gl::attach_link_detach(iso_shader.program, shaders, 2)) {
                iso_shader.uniform_loc_tex_den = glGetUniformLocation(iso_shader.program, "u_tex_den");
                iso_shader.uniform_loc_viewport = glGetUniformLocation(iso_shader.program, "u_viewport");
                iso_shader.uniform_loc_inv_res = glGetUniformLocation(iso_shader.program, "u_inv_res");
                iso_shader.uniform_loc_iso_values = glGetUniformLocation(iso_shader.program, "u_iso_values");
                iso_shader.uniform_loc_iso_level_colors = glGetUniformLocation(iso_shader.program, "u_iso_level_colors");
                iso_shader.uniform_loc_iso_contour_colors = glGetUniformLocation(iso_shader.program, "u_iso_contour_colors");
                iso_shader.uniform_loc_iso_length = glGetUniformLocation(iso_shader.program, "u_iso_length");
                iso_shader.uniform_loc_iso_contour_line_scale = glGetUniformLocation(iso_shader.program, "u_iso_contour_line_scale");
            }
        }
        
        // Cleanup temporary shaders
        if (vs) glDeleteShader(vs);
        if (fs_map) glDeleteShader(fs_map);
        if (fs_iso) glDeleteShader(fs_iso);
        
        // Initialize framebuffer and vertex array
        glGenFramebuffers(1, &fbo);
        glGenVertexArrays(1, &vao);
    }
    
    void cleanup_gl_resources() {
        // Interrupt any running tasks
        if (task_system::task_is_running(compute_density_full)) {
            task_system::task_interrupt(compute_density_full);
        }
        if (task_system::task_is_running(compute_density_filt)) {
            task_system::task_interrupt(compute_density_filt);
        }
        
        // Clean up textures
        gl::free_texture(&corr_data_full.den_tex);
        gl::free_texture(&corr_data_full.map_tex);
        gl::free_texture(&corr_data_full.iso_tex);
        gl::free_texture(&corr_data_filt.den_tex);
        gl::free_texture(&corr_data_filt.map_tex);
        gl::free_texture(&corr_data_filt.iso_tex);
        
        // Clean up shaders
        if (map_shader.program) glDeleteProgram(map_shader.program);
        if (iso_shader.program) glDeleteProgram(iso_shader.program);
        
        // Clean up framebuffer and vertex array
        if (fbo) glDeleteFramebuffers(1, &fbo);
        if (vao) glDeleteVertexArrays(1, &vao);
    }

    void update_scatter_data() {
        if (!app_state || x_property_idx < 0 || y_property_idx < 0) {
            return;
        }
        
        const int num_props = (int)md_array_size(app_state->display_properties);
        if (x_property_idx >= num_props || y_property_idx >= num_props) {
            return;
        }
        
        // Check that properties exist and have valid data
        if (x_property_idx >= num_props || y_property_idx >= num_props) {
            return;
        }
        
        // Only process temporal properties with valid data
        // Use helper functions to safely access DisplayProperty without complete type
        if (get_display_property_type_by_index(app_state, x_property_idx) != DisplayPropertyType_Temporal || 
            get_display_property_type_by_index(app_state, y_property_idx) != DisplayPropertyType_Temporal ||
            !has_display_property_data_by_index(app_state, x_property_idx) || 
            !has_display_property_data_by_index(app_state, y_property_idx)) {
            return;
        }
        
        // Clear existing series
        for (size_t i = 0; i < md_array_size(series); ++i) {
            md_array_free(series[i].x_data, arena);
            md_array_free(series[i].y_data, arena);
            md_array_free(series[i].frame_indices, arena);
        }
        md_array_resize(series, 0, arena);
        
        // Get property data using helper function
        const md_script_property_data_t* x_data = get_display_property_data_by_index(app_state, x_property_idx);
        const md_script_property_data_t* y_data = get_display_property_data_by_index(app_state, y_property_idx);
        
        const size_t num_frames = x_data->dim[0];
        const size_t x_values_per_frame = x_data->num_values / num_frames;
        const size_t y_values_per_frame = y_data->num_values / num_frames;
        
        if (num_frames != y_data->dim[0]) {
            strcpy(error, "Properties have different temporal dimensions");
            return;
        }
        
        // Clear any previous error
        error[0] = '\0';
        
        if (x_values_per_frame > 1 || y_values_per_frame > 1) {
            // Handle array properties - create multiple series
            const size_t max_series = ImMax(x_values_per_frame, y_values_per_frame);
            
            for (size_t s = 0; s < max_series; ++s) {
                ScatterSeries scatter = {};
                snprintf(scatter.name, sizeof(scatter.name), "%s[%zu] vs %s[%zu]", 
                    get_display_property_label_by_index(app_state, x_property_idx), x_values_per_frame > 1 ? s : 0,
                    get_display_property_label_by_index(app_state, y_property_idx), y_values_per_frame > 1 ? s : 0);
                
                // Assign a color based on series index
                scatter.color = ImPlot::GetColormapColor((int)s);
                
                for (size_t f = 0; f < num_frames; ++f) {
                    float x_val, y_val;
                    
                    if (x_values_per_frame > 1) {
                        if (s < x_values_per_frame) {
                            x_val = x_data->values[f * x_values_per_frame + s];
                        } else {
                            continue; // Skip this series if x doesn't have enough values
                        }
                    } else {
                        x_val = x_data->values[f];
                    }
                    
                    if (y_values_per_frame > 1) {
                        if (s < y_values_per_frame) {
                            y_val = y_data->values[f * y_values_per_frame + s];
                        } else {
                            continue; // Skip this series if y doesn't have enough values
                        }
                    } else {
                        y_val = y_data->values[f];
                    }
                    
                    md_array_push(scatter.x_data, x_val, arena);
                    md_array_push(scatter.y_data, y_val, arena);
                    md_array_push(scatter.frame_indices, (int)f, arena);
                }
                
                if (md_array_size(scatter.x_data) > 0) {
                    md_array_push(series, scatter, arena);
                }
            }
        } else {
            // Simple case - single series
            ScatterSeries scatter = {};
            snprintf(scatter.name, sizeof(scatter.name), "%s vs %s", 
                get_display_property_label_by_index(app_state, x_property_idx), get_display_property_label_by_index(app_state, y_property_idx));
            scatter.color = ImPlot::GetColormapColor(0);
            
            for (size_t f = 0; f < num_frames; ++f) {
                md_array_push(scatter.x_data, x_data->values[f], arena);
                md_array_push(scatter.y_data, y_data->values[f], arena);
                md_array_push(scatter.frame_indices, (int)f, arena);
            }
            
            md_array_push(series, scatter, arena);
        }
    }

    // Compute 2D density for correlation data
    task_system::ID compute_density(corr_rep_t* rep, const ScatterSeries* series, size_t num_series, uint32_t frame_beg, uint32_t frame_end) {
        struct UserData {
            uint64_t alloc_size;
            float* density_tex;
            corr_rep_t* rep;
            const ScatterSeries* series;
            size_t num_series;
            uint32_t frame_beg;
            uint32_t frame_end;
            float sigma;
            md_allocator_i* alloc;
            volatile int complete;
        };

        const size_t density_data_size = density_tex_dim * density_tex_dim * sizeof(float);
        const size_t alloc_size = sizeof(UserData) + density_data_size;
        
        UserData* user_data = (UserData*)md_alloc(app_state->allocator.persistent, alloc_size);
        user_data->density_tex = (float*)((char*)user_data + sizeof(UserData));
        user_data->alloc_size = alloc_size;
        user_data->rep = rep;
        user_data->series = series;
        user_data->num_series = num_series;
        user_data->frame_beg = frame_beg;
        user_data->frame_end = frame_end;
        user_data->sigma = blur_sigma;
        user_data->alloc = app_state->allocator.persistent;
        user_data->complete = 0;

        // Clear density texture
        memset(user_data->density_tex, 0, density_data_size);

        task_system::ID async_task = task_system::create_pool_task(STR_LIT("Correlation density"), [data = user_data]() {
            // Find data ranges for coordinate mapping
            float min_x = FLT_MAX, max_x = -FLT_MAX;
            float min_y = FLT_MAX, max_y = -FLT_MAX;
            
            for (size_t s = 0; s < data->num_series; ++s) {
                const ScatterSeries& series = data->series[s];
                for (size_t i = 0; i < md_array_size(series.x_data); ++i) {
                    if (series.frame_indices[i] >= (int)data->frame_beg && series.frame_indices[i] < (int)data->frame_end) {
                        min_x = MIN(min_x, series.x_data[i]);
                        max_x = MAX(max_x, series.x_data[i]);
                        min_y = MIN(min_y, series.y_data[i]);
                        max_y = MAX(max_y, series.y_data[i]);
                    }
                }
            }
            
            // Store ranges for later use
            data->rep->min_x = min_x;
            data->rep->max_x = max_x;
            data->rep->min_y = min_y;
            data->rep->max_y = max_y;
            
            // Add small padding to avoid edge cases
            float x_range = max_x - min_x;
            float y_range = max_y - min_y;
            if (x_range < 1e-6f) x_range = 1.0f;
            if (y_range < 1e-6f) y_range = 1.0f;
            
            min_x -= x_range * 0.05f;
            max_x += x_range * 0.05f;
            min_y -= y_range * 0.05f;
            max_y += y_range * 0.05f;
            
            x_range = max_x - min_x;
            y_range = max_y - min_y;
            
            double sum = 0.0;
            
            // Populate density histogram
            for (size_t s = 0; s < data->num_series; ++s) {
                const ScatterSeries& series = data->series[s];
                for (size_t i = 0; i < md_array_size(series.x_data); ++i) {
                    if (series.frame_indices[i] >= (int)data->frame_beg && series.frame_indices[i] < (int)data->frame_end) {
                        float x = series.x_data[i];
                        float y = series.y_data[i];
                        
                        // Map to texture coordinates [0, 1]
                        float u = (x - min_x) / x_range;
                        float v = (y - min_y) / y_range;
                        
                        // Convert to pixel coordinates
                        uint32_t px = (uint32_t)(u * density_tex_dim);
                        uint32_t py = (uint32_t)(v * density_tex_dim);
                        
                        // Clamp to valid range
                        px = MIN(px, density_tex_dim - 1);
                        py = MIN(py, density_tex_dim - 1);
                        
                        data->density_tex[py * density_tex_dim + px] += 1.0f;
                        sum += 1.0;
                    }
                }
            }

            // Apply Gaussian blur for smooth density
            blur_density_gaussian(data->density_tex, density_tex_dim, data->sigma);

            data->rep->den_sum = (float)sum;
            data->complete = 1;
        });

        task_system::ID main_task = task_system::create_main_task(STR_LIT("##Update correlation texture"), [data = user_data]() {
            if (data->complete) {
                gl::set_texture_2D_data(data->rep->den_tex, data->density_tex, GL_R32F);
            }
            md_free(data->alloc, data, data->alloc_size);
        });

        task_system::set_task_dependency(main_task, async_task);
        task_system::enqueue_task(async_task);

        return async_task;
    }
    
    // Update density computation when data changes
    void update() {
        if (show_window && md_array_size(series) > 0) {
            const size_t num_frames = md_array_size(series[0].frame_indices);
            if (num_frames > 0) {
                // Compute fingerprint based on data state
                uint64_t data_fingerprint = 0;
                for (size_t s = 0; s < md_array_size(series); ++s) {
                    data_fingerprint += md_array_size(series[s].x_data);
                    data_fingerprint += md_array_size(series[s].y_data);
                }
                data_fingerprint += x_property_idx;
                data_fingerprint += y_property_idx;
                
                if (full_fingerprint != data_fingerprint) {
                    if (!task_system::task_is_running(compute_density_full)) {
                        full_fingerprint = data_fingerprint;
                        
                        const uint32_t frame_beg = 0;
                        const uint32_t frame_end = (uint32_t)num_frames;
                        
                        compute_density_full = compute_density(&corr_data_full, series, md_array_size(series), frame_beg, frame_end);
                    } else {
                        task_system::task_interrupt(compute_density_full);
                    }
                }
                
                // For filtered trajectory, use timeline filter if available and enabled
                if (app_state && app_state->timeline.filter.enabled && app_state->timeline.filter.fingerprint != filt_fingerprint) {
                    if (!task_system::task_is_running(compute_density_filt)) {
                        filt_fingerprint = app_state->timeline.filter.fingerprint;
                        
                        const uint32_t frame_beg = (uint32_t)app_state->timeline.filter.beg_frame;
                        const uint32_t frame_end = (uint32_t)app_state->timeline.filter.end_frame + 1;
                        
                        compute_density_filt = compute_density(&corr_data_filt, series, md_array_size(series), frame_beg, frame_end);
                    } else {
                        task_system::task_interrupt(compute_density_filt);
                    }
                }
            }
        }
    }
    
    // Render correlation data as colormap
    void render_colormap(corr_rep_t* rep, const float viewport[4], const corr_colormap_t& colormap) {
        if (!map_shader.program) return;
        
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        
        glViewport(0, 0, tex_dim, tex_dim);
        
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
        glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, rep->map_tex, 0);
        
        GLenum draw_buffers[] = { GL_COLOR_ATTACHMENT0 };
        glDrawBuffers(1, draw_buffers);
        
        vec4_t vp = {viewport[0], viewport[1], viewport[2] - viewport[0], viewport[3] - viewport[1]};
        vec4_t colors[64] = {0};
        
        for (uint32_t i = 0; i < colormap.count && i < 64; ++i) {
            uint32_t color = colormap.colors[i];
            colors[i] = vec4_t{
                ((color >> 16) & 0xFF) / 255.0f,  // R
                ((color >> 8) & 0xFF) / 255.0f,   // G
                (color & 0xFF) / 255.0f,          // B
                ((color >> 24) & 0xFF) / 255.0f   // A
            };
        }
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, rep->den_tex);
        
        glUseProgram(map_shader.program);
        glUniform1i(map_shader.uniform_loc_tex_den, 0);
        glUniform2f(map_shader.uniform_loc_inv_res, 1.0f / tex_dim, 1.0f / tex_dim);
        glUniform4fv(map_shader.uniform_loc_viewport, 1, vp.elem);
        glUniform4fv(map_shader.uniform_loc_map_colors, (int)colormap.count, colors[0].elem);
        glUniform1ui(map_shader.uniform_loc_map_length, colormap.count);
        glUniform2f(map_shader.uniform_loc_map_range, colormap.min_value, colormap.max_value);
        
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        
        glBindVertexArray(0);
        glUseProgram(0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }
    
    // Render correlation data as isolines/isolevels
    void render_isolines(corr_rep_t* rep, const float viewport[4], const corr_isomap_t& isomap) {
        if (!iso_shader.program) return;
        
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        
        glViewport(0, 0, tex_dim, tex_dim);
        
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
        glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, rep->iso_tex, 0);
        
        GLenum draw_buffers[] = { GL_COLOR_ATTACHMENT0 };
        glDrawBuffers(1, draw_buffers);
        
        vec4_t vp = {viewport[0], viewport[1], viewport[2] - viewport[0], viewport[3] - viewport[1]};
        
        const uint32_t cap = 32;
        vec4_t level_colors[cap] = {0};
        vec4_t contour_colors[cap] = {0};
        float values[cap] = {0};
        
        for (uint32_t i = 0; i < isomap.count && i < cap; ++i) {
            if (isomap.level_colors) {
                uint32_t color = isomap.level_colors[i];
                level_colors[i] = vec4_t{
                    ((color >> 16) & 0xFF) / 255.0f,
                    ((color >> 8) & 0xFF) / 255.0f,
                    (color & 0xFF) / 255.0f,
                    ((color >> 24) & 0xFF) / 255.0f
                };
            }
            if (isomap.contour_colors) {
                uint32_t color = isomap.contour_colors[i];
                contour_colors[i] = vec4_t{
                    ((color >> 16) & 0xFF) / 255.0f,
                    ((color >> 8) & 0xFF) / 255.0f,
                    (color & 0xFF) / 255.0f,
                    ((color >> 24) & 0xFF) / 255.0f
                };
            }
            values[i] = isomap.values[i];
        }
        
        float contour_line_scale = (float)tex_dim / 512.0f;
        
        glUseProgram(iso_shader.program);
        glUniform1i(iso_shader.uniform_loc_tex_den, 0);
        glUniform4fv(iso_shader.uniform_loc_viewport, 1, vp.elem);
        glUniform2f(iso_shader.uniform_loc_inv_res, 1.0f / tex_dim, 1.0f / tex_dim);
        glUniform1fv(iso_shader.uniform_loc_iso_values, (int)isomap.count, values);
        glUniform4fv(iso_shader.uniform_loc_iso_level_colors, (int)isomap.count, level_colors[0].elem);
        glUniform4fv(iso_shader.uniform_loc_iso_contour_colors, (int)isomap.count, contour_colors[0].elem);
        glUniform1ui(iso_shader.uniform_loc_iso_length, isomap.count);
        glUniform1f(iso_shader.uniform_loc_iso_contour_line_scale, contour_line_scale);
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, rep->den_tex);
        
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        
        glBindVertexArray(0);
        glUseProgram(0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }

    void draw_window() {
        if (!show_window) return;
        
        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
        
        if (ImGui::Begin("Correlator 3000X(TM)", &show_window, ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_MenuBar)) {
            if (!app_state) {
                ImGui::Text("Application not initialized");
                ImGui::End();
                return;
            }
            
            // Add layers menu similar to Ramachandran
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("Layers")) {
                    constexpr const char* layer_labels[2] = { "Full Trajectory", "Filtered Trajectory" };
                    constexpr const char* option_labels[4] = { "Points", "IsoLevels", "IsoLines", "Colormap" };
                    
                    ImGui::Text("Layers");
                    ImGui::Separator();
                    
                    // Full Trajectory and Filtered Trajectory layers
                    for (int i = 0; i < 2; ++i) {
                        ImGui::Checkbox(layer_labels[i], &show_layer[i]);
                        if (show_layer[i]) {
                            ImGui::PushID(i);
                            if (ImGui::BeginCombo(layer_labels[i], option_labels[display_mode[i]])) {
                                if (ImGui::Selectable(option_labels[Points], display_mode[i] == Points)) display_mode[i] = Points;
                                if (ImGui::Selectable(option_labels[IsoLevels], display_mode[i] == IsoLevels)) display_mode[i] = IsoLevels;
                                if (ImGui::Selectable(option_labels[IsoLines], display_mode[i] == IsoLines)) display_mode[i] = IsoLines;
                                if (ImGui::Selectable(option_labels[Colormap], display_mode[i] == Colormap)) display_mode[i] = Colormap;
                                ImGui::EndCombo();
                            }
                            if (display_mode[i] == Colormap) {
                                ImPlot::ColormapSelection("Color Map", &colormap[i]);
                            } else if (display_mode[i] == IsoLines) {
                                ImGui::ColorEdit4Minimal("Line Color", &isoline_colors[i].x);
                            }
                            ImGui::PopID();
                        }
                        ImGui::Separator();
                    }
                    
                    // Current frame layer
                    ImGui::Checkbox("Current", &show_layer[2]);
                    if (show_layer[2]) {
                        ImGui::SliderFloat("Point Size", &current_style.size, 1.0f, 10.0f);
                        ImGui::ColorEdit4Minimal("Point Outline", &current_style.outline.x);
                        ImGui::ColorEdit4Minimal("Point Fill", &current_style.fill.x);
                    }
                    
                    ImGui::Separator();
                    
                    // Advanced configuration
                    ImGui::Text("Advanced Settings");
                    ImGui::Checkbox("Preserve Series", &preserve_series);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Show separate isolines/colormaps for each series");
                    }
                    
                    ImGui::SliderInt("ISO Levels", &num_iso_levels, 1, 8);
                    if (ImGui::TreeNode("Custom Thresholds")) {
                        for (int i = 0; i < num_iso_levels; ++i) {
                            ImGui::PushID(i);
                            ImGui::SliderFloat("", &iso_thresholds[i], 0.01f, 0.99f, "%.2f");
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                    
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            
            const int num_props = (int)md_array_size(app_state->display_properties);
            
            // Get available space for plot area
            ImVec2 plot_avail = ImGui::GetContentRegionAvail();
            float y_button_width = 150.0f;
            float x_button_height = 40.0f;
            float clear_button_width = 80.0f;
            
            // Y-axis property selection button on the left
            ImGui::BeginChild("YAxisButton", ImVec2(y_button_width, plot_avail.y - x_button_height), false);
            
            ImGui::SetCursorPosY(ImGui::GetContentRegionAvail().y * 0.5f - 20.0f);
            ImGui::Text("Y-Axis Property:");
            
            // Y-axis property selection button
            char y_display_text[128] = "Select Property";
            
            if (y_property_idx >= 0) {
                const char* y_label = get_display_property_label_by_index(app_state, y_property_idx);
                const char* y_unit = get_display_property_unit_by_index(app_state, y_property_idx, 1);
                if (strlen(y_unit) > 0) {
                    snprintf(y_display_text, sizeof(y_display_text), "%s (%s)", y_label, y_unit);
                } else {
                    snprintf(y_display_text, sizeof(y_display_text), "%s", y_label);
                }
            }
            
            if (ImGui::Button(y_display_text, ImVec2(-1, 0))) {
                ImGui::OpenPopup("Y_Property_Selector");
            }
            
            if (ImGui::BeginPopup("Y_Property_Selector")) {
                ImGui::Text("Select Y-Axis Property:");
                ImGui::Separator();
                
                bool has_temporal_props = false;
                for (int i = 0; i < num_props; ++i) {
                    if (get_display_property_type_by_index(app_state, i) == DisplayPropertyType_Temporal) {
                        has_temporal_props = true;
                        const char* label = get_display_property_label_by_index(app_state, i);
                        ImVec4 color = get_display_property_color_by_index(app_state, i);
                        
                        ImPlot::ItemIcon(color);
                        ImGui::SameLine();
                        
                        if (ImGui::Selectable(label, y_property_idx == i)) {
                            y_property_idx = i;
                            update_scatter_data();
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
                
                if (!has_temporal_props) {
                    ImGui::Text("No temporal properties available.");
                    ImGui::Text("Define and evaluate properties in the script editor.");
                }
                
                ImGui::EndPopup();
            }
            
            ImGui::EndChild();
            
            ImGui::SameLine();
            
            // Plot area in the center
            ImGui::BeginChild("PlotArea", ImVec2(plot_avail.x - y_button_width - clear_button_width, plot_avail.y - x_button_height), false);
            
            // Always display the scatter plot if we have data
            if (x_property_idx >= 0 && y_property_idx >= 0 && md_array_size(series) > 0) {
                if (ImPlot::BeginPlot("Property Correlation", ImVec2(-1, -1))) {
                    
                    // Set axis labels with units
                    if (x_property_idx >= 0 && x_property_idx < num_props) {
                        const char* x_unit = get_display_property_unit_by_index(app_state, x_property_idx, 0);
                        char x_axis_label[128];
                        if (strlen(x_unit) > 0) {
                            snprintf(x_axis_label, sizeof(x_axis_label), "%s (%s)", 
                                get_display_property_label_by_index(app_state, x_property_idx), x_unit);
                        } else {
                            snprintf(x_axis_label, sizeof(x_axis_label), "%s", 
                                get_display_property_label_by_index(app_state, x_property_idx));
                        }
                        ImPlot::SetupAxis(ImAxis_X1, x_axis_label);
                    }
                    if (y_property_idx >= 0 && y_property_idx < num_props) {
                        const char* y_unit = get_display_property_unit_by_index(app_state, y_property_idx, 1);
                        char y_axis_label[128];
                        if (strlen(y_unit) > 0) {
                            snprintf(y_axis_label, sizeof(y_axis_label), "%s (%s)", 
                                get_display_property_label_by_index(app_state, y_property_idx), y_unit);
                        } else {
                            snprintf(y_axis_label, sizeof(y_axis_label), "%s", 
                                get_display_property_label_by_index(app_state, y_property_idx));
                        }
                        ImPlot::SetupAxis(ImAxis_Y1, y_axis_label);
                    }
                    
                    // Plot layers based on layer settings
                    ImPlotRect plot_rect = ImPlot::GetPlotLimits();
                    float viewport[4] = {
                        (float)plot_rect.X.Min, (float)plot_rect.Y.Min,
                        (float)plot_rect.X.Max, (float)plot_rect.Y.Max
                    };
                    
                    // Render advanced display modes using textures
                    if (show_layer[0]) { // Full Trajectory
                        if (display_mode[0] == Points) {
                            // Plot all points with trajectory transparency
                            for (size_t s = 0; s < md_array_size(series); ++s) {
                                const ScatterSeries& scatter = series[s];
                                if (md_array_size(scatter.x_data) > 0) {
                                    ImVec4 color = scatter.color;
                                    color.w *= full_alpha;
                                    ImPlot::PushStyleColor(ImPlotCol_MarkerFill, color);
                                    ImPlot::PlotScatter(scatter.name, 
                                        scatter.x_data, scatter.y_data, 
                                        (int)md_array_size(scatter.x_data));
                                    ImPlot::PopStyleColor();
                                }
                            }
                        } else if (display_mode[0] == Colormap) {
                            // Render colormap if density data is available
                            if (corr_data_full.den_tex && corr_data_full.den_sum > 0) {
                                uint32_t colors[32] = {0};
                                uint32_t num_colors = ImPlot::GetColormapSize(colormap[0]);
                                for (uint32_t j = 0; j < num_colors && j < 32; ++j) {
                                    colors[j] = ImPlot::GetColormapColorU32(j, colormap[0]);
                                }
                                // Make first color transparent
                                colors[0] = colors[0] & 0x00FFFFFFU;
                                
                                corr_colormap_t corr_colormap = {
                                    .colors = colors,
                                    .count = num_colors,
                                    .min_value = 0.0f,
                                    .max_value = corr_data_full.den_sum * 0.5f / (density_tex_dim * density_tex_dim)
                                };
                                
                                render_colormap(&corr_data_full, viewport, corr_colormap);
                                
                                // Display as image overlay
                                ImPlot::PlotImage("Full Trajectory Heatmap", (void*)(intptr_t)corr_data_full.map_tex,
                                    ImPlotPoint(corr_data_full.min_x, corr_data_full.min_y),
                                    ImPlotPoint(corr_data_full.max_x, corr_data_full.max_y));
                            }
                        } else if (display_mode[0] == IsoLevels || display_mode[0] == IsoLines) {
                            // Render isolines/isolevels if density data is available
                            if (corr_data_full.den_tex && corr_data_full.den_sum > 0) {
                                const float density_scale = corr_data_full.den_sum / (density_tex_dim * density_tex_dim);
                                float iso_values[8] = {0};
                                
                                // Use custom thresholds
                                for (int i = 0; i < num_iso_levels; ++i) {
                                    iso_values[i] = density_scale * iso_thresholds[i];
                                }
                                
                                uint32_t level_colors[8] = {0};
                                uint32_t contour_colors[8] = {0};
                                
                                if (display_mode[0] == IsoLevels) {
                                    // Generate colors based on series or use default gradient
                                    if (preserve_series && md_array_size(series) > 1) {
                                        // Use series colors for iso levels
                                        for (int i = 0; i < num_iso_levels; ++i) {
                                            uint32_t series_idx = i % md_array_size(series);
                                            ImVec4 series_color = series[series_idx].color;
                                            level_colors[i] = IM_COL32(
                                                (int)(series_color.x * 255), (int)(series_color.y * 255), 
                                                (int)(series_color.z * 255), (int)(series_color.w * 128));
                                        }
                                    } else {
                                        // Use default gradient colors
                                        for (int i = 0; i < num_iso_levels; ++i) {
                                            float t = (float)i / (float)MAX(1, num_iso_levels - 1);
                                            level_colors[i] = IM_COL32(
                                                (int)(255 * t), (int)(255 * (1-t)), (int)(255 * 0.5f), 
                                                (int)(128 + 127 * t)); // Increasing opacity
                                        }
                                    }
                                    memcpy(contour_colors, level_colors, sizeof(level_colors));
                                } else {
                                    // IsoLines - use line color for all levels
                                    uint32_t line_color = ImGui::ColorConvertFloat4ToU32(isoline_colors[0]);
                                    for (int i = 0; i < num_iso_levels; ++i) {
                                        contour_colors[i] = line_color;
                                    }
                                }
                                
                                corr_isomap_t corr_isomap = {
                                    .values = iso_values,
                                    .level_colors = level_colors,
                                    .contour_colors = contour_colors,
                                    .count = (uint32_t)num_iso_levels
                                };
                                
                                render_isolines(&corr_data_full, viewport, corr_isomap);
                                
                                // Display as image overlay
                                const char* iso_name = display_mode[0] == IsoLevels ? "Full Trajectory IsoLevels" : "Full Trajectory IsoLines";
                                ImPlot::PlotImage(iso_name, (void*)(intptr_t)corr_data_full.iso_tex,
                                    ImPlotPoint(corr_data_full.min_x, corr_data_full.min_y),
                                    ImPlotPoint(corr_data_full.max_x, corr_data_full.max_y));
                            }
                        }
                    }
                    
                    // Filtered Trajectory layer
                    if (show_layer[1] && app_state && app_state->timeline.filter.enabled) { // Filtered Trajectory  
                        if (display_mode[1] == Points) {
                            // Show only data points within the timeline filter range
                            const int filter_beg = (int)app_state->timeline.filter.beg_frame;
                            const int filter_end = (int)app_state->timeline.filter.end_frame;
                            
                            for (size_t s = 0; s < md_array_size(series); ++s) {
                                const ScatterSeries& scatter = series[s];
                                if (md_array_size(scatter.x_data) > 0) {
                                    // Collect filtered points
                                    static md_array(float) filtered_x = 0;
                                    static md_array(float) filtered_y = 0;
                                    
                                    md_array_resize(filtered_x, 0, app_state->allocator.frame);
                                    md_array_resize(filtered_y, 0, app_state->allocator.frame);
                                    
                                    for (size_t i = 0; i < md_array_size(scatter.x_data); ++i) {
                                        int frame = scatter.frame_indices[i];
                                        if (frame >= filter_beg && frame <= filter_end) {
                                            md_array_push(filtered_x, scatter.x_data[i], app_state->allocator.frame);
                                            md_array_push(filtered_y, scatter.y_data[i], app_state->allocator.frame);
                                        }
                                    }
                                    
                                    if (md_array_size(filtered_x) > 0) {
                                        ImVec4 color = ImVec4(0.8f, 0.3f, 0.8f, filt_alpha); // Different color for filtered
                                        ImPlot::PushStyleColor(ImPlotCol_MarkerFill, color);
                                        char filtered_name[128];
                                        snprintf(filtered_name, sizeof(filtered_name), "%s (Filtered)", scatter.name);
                                        ImPlot::PlotScatter(filtered_name, 
                                            filtered_x, filtered_y, 
                                            (int)md_array_size(filtered_x));
                                        ImPlot::PopStyleColor();
                                    }
                                }
                            }
                        } else if (display_mode[1] == Colormap) {
                            // Render filtered colormap
                            if (corr_data_filt.den_tex && corr_data_filt.den_sum > 0) {
                                uint32_t colors[32] = {0};
                                uint32_t num_colors = ImPlot::GetColormapSize(colormap[1]);
                                for (uint32_t j = 0; j < num_colors && j < 32; ++j) {
                                    colors[j] = ImPlot::GetColormapColorU32(j, colormap[1]);
                                }
                                colors[0] = colors[0] & 0x00FFFFFFU;
                                
                                corr_colormap_t corr_colormap = {
                                    .colors = colors,
                                    .count = num_colors,
                                    .min_value = 0.0f,
                                    .max_value = corr_data_filt.den_sum * 0.5f / (density_tex_dim * density_tex_dim)
                                };
                                
                                render_colormap(&corr_data_filt, viewport, corr_colormap);
                                
                                ImPlot::PlotImage("Filtered Trajectory Heatmap", (void*)(intptr_t)corr_data_filt.map_tex,
                                    ImPlotPoint(corr_data_filt.min_x, corr_data_filt.min_y),
                                    ImPlotPoint(corr_data_filt.max_x, corr_data_filt.max_y));
                            }
                        } else if (display_mode[1] == IsoLevels || display_mode[1] == IsoLines) {
                            // Render filtered isolines/isolevels
                            if (corr_data_filt.den_tex && corr_data_filt.den_sum > 0) {
                                const float density_scale = corr_data_filt.den_sum / (density_tex_dim * density_tex_dim);
                                float iso_values[8] = {0};
                                
                                // Use custom thresholds
                                for (int i = 0; i < num_iso_levels; ++i) {
                                    iso_values[i] = density_scale * iso_thresholds[i];
                                }
                                
                                uint32_t level_colors[8] = {0};
                                uint32_t contour_colors[8] = {0};
                                
                                if (display_mode[1] == IsoLevels) {
                                    // Generate colors based on series or use default gradient (different colors from full trajectory)
                                    if (preserve_series && md_array_size(series) > 1) {
                                        // Use series colors for iso levels
                                        for (int i = 0; i < num_iso_levels; ++i) {
                                            uint32_t series_idx = i % md_array_size(series);
                                            ImVec4 series_color = series[series_idx].color;
                                            // Make filtered trajectory slightly bluer
                                            series_color.z = MIN(1.0f, series_color.z + 0.3f);
                                            level_colors[i] = IM_COL32(
                                                (int)(series_color.x * 255), (int)(series_color.y * 255), 
                                                (int)(series_color.z * 255), (int)(series_color.w * 128));
                                        }
                                    } else {
                                        // Use blue gradient for filtered trajectory
                                        for (int i = 0; i < num_iso_levels; ++i) {
                                            float t = (float)i / (float)MAX(1, num_iso_levels - 1);
                                            level_colors[i] = IM_COL32(
                                                0, (int)(255 * (1-t)), (int)(255 * t), 
                                                (int)(128 + 127 * t)); // Increasing opacity
                                        }
                                    }
                                    memcpy(contour_colors, level_colors, sizeof(level_colors));
                                } else {
                                    // IsoLines - use line color for all levels
                                    uint32_t line_color = ImGui::ColorConvertFloat4ToU32(isoline_colors[1]);
                                    for (int i = 0; i < num_iso_levels; ++i) {
                                        contour_colors[i] = line_color;
                                    }
                                }
                                
                                corr_isomap_t corr_isomap = {
                                    .values = iso_values,
                                    .level_colors = level_colors,
                                    .contour_colors = contour_colors,
                                    .count = (uint32_t)num_iso_levels
                                };
                                
                                render_isolines(&corr_data_filt, viewport, corr_isomap);
                                
                                const char* iso_name = display_mode[1] == IsoLevels ? "Filtered Trajectory IsoLevels" : "Filtered Trajectory IsoLines";
                                ImPlot::PlotImage(iso_name, (void*)(intptr_t)corr_data_filt.iso_tex,
                                    ImPlotPoint(corr_data_filt.min_x, corr_data_filt.min_y),
                                    ImPlotPoint(corr_data_filt.max_x, corr_data_filt.max_y));
                            }
                        }
                    }
                    
                    // Current frame layer
                    if (show_layer[2] && app_state) { // Current frame
                        // Find data point for current frame
                        int current_frame = (int)app_state->animation.frame;
                        for (size_t s = 0; s < md_array_size(series); ++s) {
                            const ScatterSeries& scatter = series[s];
                            for (size_t i = 0; i < md_array_size(scatter.frame_indices); ++i) {
                                if (scatter.frame_indices[i] == current_frame) {
                                    float x = scatter.x_data[i];
                                    float y = scatter.y_data[i];
                                    
                                    // Plot current frame as a larger, highlighted point
                                    ImPlot::PushStyleVar(ImPlotStyleVar_MarkerSize, current_style.size);
                                    ImPlot::PushStyleColor(ImPlotCol_MarkerFill, current_style.fill);
                                    ImPlot::PushStyleColor(ImPlotCol_MarkerOutline, current_style.outline);
                                    ImPlot::PlotScatter("Current Frame", &x, &y, 1);
                                    ImPlot::PopStyleColor(2);
                                    ImPlot::PopStyleVar();
                                    break;
                                }
                            }
                        }
                    }
                    
                    // Handle interaction for all layers (only if plot is hovered)
                    if (ImPlot::IsPlotHovered()) {
                        for (size_t s = 0; s < md_array_size(series); ++s) {
                            const ScatterSeries& scatter = series[s];
                            if (md_array_size(scatter.x_data) > 0) {
                                // Find closest point in screen space
                                float min_dist_sq = FLT_MAX;
                                int closest_point = -1;
                                
                                for (size_t p = 0; p < md_array_size(scatter.x_data); ++p) {
                                    // Convert plot coordinates to screen space for distance calculation
                                    ImVec2 screen_pos = ImPlot::PlotToPixels(scatter.x_data[p], scatter.y_data[p]);
                                    ImPlotPoint mouse_plot = ImPlot::GetPlotMousePos();
                                    ImVec2 mouse_pixel = ImPlot::PlotToPixels(mouse_plot.x, mouse_plot.y);
                                    
                                    float dx = screen_pos.x - mouse_pixel.x;
                                    float dy = screen_pos.y - mouse_pixel.y;
                                    float dist_sq = dx * dx + dy * dy;
                                    
                                    if (dist_sq < min_dist_sq) {
                                        min_dist_sq = dist_sq;
                                        closest_point = (int)p;
                                    }
                                }
                                
                                // Check if close enough to hover (within 10 pixels)
                                if (closest_point >= 0 && min_dist_sq < 100.0f) {
                                    hovered_point = closest_point;
                                    
                                    // Show tooltip
                                    ImGui::BeginTooltip();
                                    ImGui::Text("Frame: %d", scatter.frame_indices[closest_point]);
                                    ImGui::Text("X: %.3f", scatter.x_data[closest_point]);
                                    ImGui::Text("Y: %.3f", scatter.y_data[closest_point]);
                                    ImGui::Text("Click to jump to this frame");
                                    ImGui::EndTooltip();
                                    
                                    // Handle click to jump to frame
                                    if (ImGui::IsMouseClicked(0)) {
                                        clicked_frame = scatter.frame_indices[closest_point];
                                        app_state->animation.frame = (double)clicked_frame;
                                    }
                                }
                            }
                        }
                    }
                    
                    ImPlot::EndPlot();
                }
            } else {
                // Show empty plot with instructions
                if (ImPlot::BeginPlot("Property Correlation", ImVec2(-1, -1))) {
                    ImPlot::SetupAxes("X Property", "Y Property");
                    ImPlot::PlotText("Select temporal properties using the property buttons\nto create correlation plot", 0.5, 0.5);
                    ImPlot::EndPlot();
                }
            }
            
            ImGui::EndChild();
            
            ImGui::SameLine();
            
            // Clear button to the right of the plot
            ImGui::BeginChild("ClearButton", ImVec2(clear_button_width, plot_avail.y - x_button_height), false);
            ImGui::SetCursorPosY(ImGui::GetContentRegionAvail().y * 0.5f - 15.0f);
            if (ImGui::Button("Clear Plot", ImVec2(-1, 30))) {
                x_property_idx = -1;
                y_property_idx = -1;
                // Clear series data
                for (size_t i = 0; i < md_array_size(series); ++i) {
                    md_array_free(series[i].x_data, arena);
                    md_array_free(series[i].y_data, arena);
                    md_array_free(series[i].frame_indices, arena);
                }
                md_array_resize(series, 0, arena);
                error[0] = '\0';
            }
            ImGui::EndChild();
            
            // X-axis property selection button below the plot
            ImGui::SetCursorPosX(y_button_width);
            ImGui::BeginChild("XAxisButton", ImVec2(plot_avail.x - y_button_width - clear_button_width, x_button_height), false);
            
            ImGui::Text("X-Axis Property:");
            ImGui::SameLine();
            
            // X-axis property selection button
            char x_display_text[128] = "Select Property";
            
            if (x_property_idx >= 0) {
                const char* x_label = get_display_property_label_by_index(app_state, x_property_idx);
                const char* x_unit = get_display_property_unit_by_index(app_state, x_property_idx, 0);
                if (strlen(x_unit) > 0) {
                    snprintf(x_display_text, sizeof(x_display_text), "%s (%s)", x_label, x_unit);
                } else {
                    snprintf(x_display_text, sizeof(x_display_text), "%s", x_label);
                }
            }
            
            if (ImGui::Button(x_display_text, ImVec2(-1, 0))) {
                ImGui::OpenPopup("X_Property_Selector");
            }
            
            if (ImGui::BeginPopup("X_Property_Selector")) {
                ImGui::Text("Select X-Axis Property:");
                ImGui::Separator();
                
                bool has_temporal_props = false;
                for (int i = 0; i < num_props; ++i) {
                    if (get_display_property_type_by_index(app_state, i) == DisplayPropertyType_Temporal) {
                        has_temporal_props = true;
                        const char* label = get_display_property_label_by_index(app_state, i);
                        ImVec4 color = get_display_property_color_by_index(app_state, i);
                        
                        ImPlot::ItemIcon(color);
                        ImGui::SameLine();
                        
                        if (ImGui::Selectable(label, x_property_idx == i)) {
                            x_property_idx = i;
                            update_scatter_data();
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
                
                if (!has_temporal_props) {
                    ImGui::Text("No temporal properties available.");
                    ImGui::Text("Define and evaluate properties in the script editor.");
                }
                
                ImGui::EndPopup();
            }
            
            ImGui::EndChild();
            
            // Show error messages if any
            if (strlen(error) > 0) {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error: %s", error);
            }
        }
        ImGui::End();
    }
};

static Correlation instance = {};