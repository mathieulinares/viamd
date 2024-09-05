/*-----------------------------------------------------------------------
  Copyright (c) 2014, NVIDIA. All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
   * Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
   * Neither the name of its contributors may be used to endorse
     or promote products derived from this software without specific
     prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
  OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
-----------------------------------------------------------------------*/

// Shaders for HBAO are based on nVidias examples and are copyright protected as stated above

#include <gfx/postprocessing_utils.h>

#include <core/md_str.h>
#include <core/md_log.h>
#include <core/md_hash.h>

#include <gfx/gl_utils.h>

#include <stdio.h>
#include <string.h>
#include <float.h>

#include <shaders.inl>

#define PUSH_GPU_SECTION(lbl)                                                                       \
    {                                                                                               \
        if (glPushDebugGroup) glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, GL_KHR_debug, -1, lbl); \
    }
#define POP_GPU_SECTION()                       \
    {                                           \
        if (glPopDebugGroup) glPopDebugGroup(); \
    }

namespace postprocessing {

// @TODO: Use half-res render targets for SSAO
// @TODO: Use shared textures for all postprocessing operations
// @TODO: Use some kind of unified pipeline for all post processing operations

static struct {
    GLuint vao = 0;
    GLuint v_shader_fs_quad = 0;
    GLuint tex_width = 0;
    GLuint tex_height = 0;

    struct {
        GLuint fbo = 0;
        GLuint tex_rgba8 = 0;
    } tmp;

    struct {
        GLuint fbo = 0;
        GLuint tex_color[2] = {0, 0};
        GLuint tex_temporal_buffer[2] = {0, 0};  // These are dedicated and cannot be use as intermediate buffers by other shaders
    } targets;

    struct {
        GLuint fbo = 0;
        GLuint tex_tilemax = 0;
        GLuint tex_neighbormax = 0;
        int32_t tex_width = 0;
        int32_t tex_height = 0;
    } velocity;

    struct {
        GLuint fbo = 0;
        GLuint texture = 0;
        GLuint program_persp = 0;
        GLuint program_ortho = 0;
        struct {
            GLint clip_info = -1;
            GLint tex_depth = -1;
        } uniform_loc;
    } linear_depth;

    struct {
        GLuint tex_random = 0;
        GLuint ubo_hbao_data = 0;
        GLuint fbo = 0;
        GLuint tex[2] = {};

        struct {
            GLuint program_persp = 0;
            GLuint program_ortho = 0;
        } hbao;

        struct {
            GLuint program = 0;
        } blur;
    } ssao;

    struct {
        GLuint program = 0;
        struct {
            GLint tex_half_res = -1;
            GLint tex_color = -1;
            GLint tex_depth = -1;
            GLint pixel_size = -1;
            GLint focus_point = -1;
            GLint focus_scale = -1;
            GLint time = -1;
        } uniform_loc;

        struct {
            GLuint fbo = 0;
            GLuint program = 0;
            struct {
                GLuint color_coc = 0;
            } tex;
            struct {
                GLint tex_depth = -1;
                GLint tex_color = -1;
                GLint focus_point = -1;
                GLint focus_scale = -1;
            } uniform_loc;
        } half_res;
    } bokeh_dof;

    struct {
        GLuint program = 0;
        struct {
            GLint mode = -1;
            GLint tex_color = -1;
        } uniform_loc;
    } tonemapping;

    struct {
        GLuint program = 0;
        struct {
            GLint tex_rgba = -1;
        } uniform_loc;
    } luma;

    struct {
        GLuint program = 0;
        struct {
            GLint tex_rgbl = -1;
            GLint rcp_res  = -1;
            GLint tc_scl   = -1;
        } uniform_loc;
    } fxaa;

    struct {
        struct {
            GLuint program = 0;
            struct {
                GLint tex_linear_depth = -1;
                GLint tex_main = -1;
                GLint tex_prev = -1;
                GLint tex_vel = -1;
                GLint tex_vel_neighbormax = -1;
                GLint texel_size = -1;
                GLint time = -1;
                GLint feedback_min = -1;
                GLint feedback_max = -1;
                GLint motion_scale = -1;
                GLint jitter_uv = -1;
            } uniform_loc;
        } with_motion_blur;
        struct {
            GLuint program = 0;
            struct {
                GLint tex_linear_depth = -1;
                GLint tex_main = -1;
                GLint tex_prev = -1;
                GLint tex_vel = -1;
                GLint texel_size = -1;
                GLint time = -1;
                GLint feedback_min = -1;
                GLint feedback_max = -1;
                GLint motion_scale = -1;
                GLint jitter_uv = -1;
            } uniform_loc;
        } no_motion_blur;
    } temporal;
} gl;

static constexpr str_t v_shader_src_fs_quad = STR_LIT(
R"(
#version 150 core

out vec2 tc;

uniform vec2 u_tc_scl = vec2(1,1);

void main() {
	uint idx = uint(gl_VertexID) % 3U;
	gl_Position = vec4(
		(float( idx     &1U)) * 4.0 - 1.0,
		(float((idx>>1U)&1U)) * 4.0 - 1.0,
		0, 1.0);
	tc = (gl_Position.xy * 0.5 + 0.5) * u_tc_scl;
}
)");

static constexpr str_t f_shader_src_linearize_depth = STR_LIT(
R"(
#ifndef PERSPECTIVE
#define PERSPECTIVE 1
#endif

// z_n * z_f,  z_n - z_f,  z_f, *not used*
uniform vec4 u_clip_info;
uniform sampler2D u_tex_depth;

float ReconstructCSZ(float d, vec4 clip_info) {
#if PERSPECTIVE
    return (clip_info[0] / (d*clip_info[1] + clip_info[2]));
#else
    return (clip_info[1] + clip_info[2] - d*clip_info[1]);
#endif
}

out vec4 out_frag;

void main() {
  float d = texelFetch(u_tex_depth, ivec2(gl_FragCoord.xy), 0).x;
  out_frag = vec4(ReconstructCSZ(d, u_clip_info));
}
)");

/*
static constexpr const char* f_shader_src_mip_map_min_depth = R"(
uniform sampler2D u_tex_depth;

out vec4 out_frag;

void main() {
	float d00 = texelFetch(u_tex_depth, ivec2(gl_FragCoord.xy) + ivec2(0,0), 0).x;
	float d01 = texelFetch(u_tex_depth, ivec2(gl_FragCoord.xy) + ivec2(0,1), 0).x;
	float d10 = texelFetch(u_tex_depth, ivec2(gl_FragCoord.xy) + ivec2(1,0), 0).x;
	float d11 = texelFetch(u_tex_depth, ivec2(gl_FragCoord.xy) + ivec2(1,1), 0).x;

	float dmin0 = min(d00, d01);
	float dmin1 = min(d10, d11);

	out_frag = vec4(min(dmin0, dmin1));
}
)";
*/

static GLuint setup_program_from_source(str_t name, str_t f_shader_src, str_t defines = {}) {
    GLuint f_shader = gl::compile_shader_from_source(f_shader_src, GL_FRAGMENT_SHADER, defines);
    GLuint program = 0;

    if (f_shader) {
        char buffer[1024];
        program = glCreateProgram();

        glAttachShader(program, gl.v_shader_fs_quad);
        glAttachShader(program, f_shader);
        glLinkProgram(program);
        if (gl::get_program_link_error(buffer, sizeof(buffer), program)) {
            MD_LOG_ERROR("Error while linking %.*s program:\n%s", (int)name.len, name.ptr, buffer);
            glDeleteProgram(program);
            return 0;
        }

        glDetachShader(program, gl.v_shader_fs_quad);
        glDetachShader(program, f_shader);
        glDeleteShader(f_shader);
    }

    return program;
}

static bool is_orthographic_proj_matrix(const mat4_t& proj_mat) { return proj_mat.elem[2][3] == 0.0f; }

namespace ssao {
#ifndef AO_RANDOM_TEX_SIZE
#define AO_RANDOM_TEX_SIZE 4
#endif

struct HBAOData {
    float radius_to_screen;
    float neg_inv_r2;
    float n_dot_v_bias;
    float z_max;

    vec2_t inv_full_res;
    float ao_multiplier;
    float pow_exponent;

    vec4_t proj_info;

    vec4_t sample_pattern[32];
};

void setup_ubo_hbao_data(HBAOData* data, int width, int height, const vec2_t& inv_res, const mat4_t& proj_mat, float intensity, float radius, float bias) {
    ASSERT(data);

    // From intel ASSAO
    static constexpr float SAMPLE_PATTERN[] = {
        0.78488064,  0.56661671,  1.500000, -0.126083, 0.26022232,  -0.29575172, 1.500000, -1.064030, 0.10459357,  0.08372527,  1.110000, -2.730563, -0.68286800, 0.04963045,  1.090000, -0.498827,
        -0.13570161, -0.64190155, 1.250000, -0.532765, -0.26193795, -0.08205118, 0.670000, -1.783245, -0.61177456, 0.66664219,  0.710000, -0.044234, 0.43675563,  0.25119025,  0.610000, -1.167283,
        0.07884444,  0.86618668,  0.640000, -0.459002, -0.12790935, -0.29869005, 0.600000, -1.729424, -0.04031125, 0.02413622,  0.600000, -4.792042, 0.16201244,  -0.52851415, 0.790000, -1.067055,
        -0.70991218, 0.47301072,  0.640000, -0.335236, 0.03277707,  -0.22349690, 0.600000, -1.982384, 0.68921727,  0.36800742,  0.630000, -0.266718, 0.29251814,  0.37775412,  0.610000, -1.422520,
        -0.12224089, 0.96582592,  0.600000, -0.426142, 0.11071457,  -0.16131058, 0.600000, -2.165947, 0.46562141,  -0.59747696, 0.600000, -0.189760, -0.51548797, 0.11804193,  0.600000, -1.246800,
        0.89141309,  -0.42090443, 0.600000, 0.028192,  -0.32402530, -0.01591529, 0.600000, -1.543018, 0.60771245,  0.41635221,  0.600000, -0.605411, 0.02379565,  -0.08239821, 0.600000, -3.809046,
        0.48951152,  -0.23657045, 0.600000, -1.189011, -0.17611565, -0.81696892, 0.600000, -0.513724, -0.33930185, -0.20732205, 0.600000, -1.698047, -0.91974425, 0.05403209,  0.600000, 0.062246,
        -0.15064627, -0.14949332, 0.600000, -1.896062, 0.53180975,  -0.35210401, 0.600000, -0.758838, 0.41487166,  0.81442589,  0.600000, -0.505648, -0.24106961, -0.32721516, 0.600000, -1.665244};
    constexpr float METERS_TO_VIEWSPACE = 1.f;

    vec4_t proj_info;
    float proj_scl;
    float z_max;

    const float* proj_data = &proj_mat.elem[0][0];
    const bool ortho = is_orthographic_proj_matrix(proj_mat);
    const float x = proj_mat.elem[2][2];
    const float y = proj_mat.elem[3][2];
    if (!ortho) {
        // Persp
        proj_info = {
            2.0f / (proj_data[4 * 0 + 0]),                          // (x) * (R - L)/N
            2.0f / (proj_data[4 * 1 + 1]),                          // (y) * (T - B)/N
            -(1.0f - proj_data[4 * 2 + 0]) / proj_data[4 * 0 + 0],  // L/N
            -(1.0f + proj_data[4 * 2 + 1]) / proj_data[4 * 1 + 1]   // B/N
        };

        // proj_scl = float(height) / (math::tan(fovy * 0.5f) * 2.0f);
        proj_scl = float(height) * proj_data[4 * 1 + 1] * 0.5f;
        z_max = (float)(y / (x + 1.0));
    } else {
        // Ortho
        proj_info = {
            2.0f / (proj_data[4 * 0 + 0]),                          // ((x) * R - L)
            2.0f / (proj_data[4 * 1 + 1]),                          // ((y) * T - B)
            -(1.0f + proj_data[4 * 3 + 0]) / proj_data[4 * 0 + 0],  // L
            -(1.0f - proj_data[4 * 3 + 1]) / proj_data[4 * 1 + 1]   // B
        };
        proj_scl = float(height) / proj_info.y;
        z_max = (float)((-2.0 + y) / x);
    }

    float r = radius * METERS_TO_VIEWSPACE;

    data->radius_to_screen = r * 0.5f * proj_scl;
    data->neg_inv_r2 = -1.f / (r * r);
    data->n_dot_v_bias = CLAMP(bias, 0.f, 1.f - FLT_EPSILON);
    data->z_max = z_max * 0.99f;
    data->inv_full_res = inv_res;
    data->ao_multiplier = 1.f / (1.f - data->n_dot_v_bias);
    data->pow_exponent = MAX(intensity, 0.f);
    data->proj_info = proj_info;
    MEMCPY(&data->sample_pattern, SAMPLE_PATTERN, sizeof(SAMPLE_PATTERN));
}

void initialize_rnd_tex(GLuint rnd_tex) {
    const int buffer_size = AO_RANDOM_TEX_SIZE * AO_RANDOM_TEX_SIZE;
    int16_t buffer[buffer_size * 4];

    for (int i = 0; i < buffer_size; i++) {
#define SCALE ((1 << 15))
        float rand1 = md_halton(i + 1, 2);
        float rand2 = md_halton(i + 1, 3);
        float angle = 2.f * 3.1415926535f * rand1;

        buffer[i * 4 + 0] = (int16_t)(SCALE * cosf(angle));
        buffer[i * 4 + 1] = (int16_t)(SCALE * sinf(angle));
        buffer[i * 4 + 2] = (int16_t)(SCALE * rand2);
        buffer[i * 4 + 3] = (int16_t)(SCALE * 0);
#undef SCALE
    }

    glBindTexture(GL_TEXTURE_2D, rnd_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16_SNORM, AO_RANDOM_TEX_SIZE, AO_RANDOM_TEX_SIZE, 0, GL_RGBA, GL_SHORT, buffer);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
}

float compute_sharpness(float radius) { return 20.f / sqrtf(radius); }

void initialize(int width, int height) {
    gl.ssao.hbao.program_persp = setup_program_from_source(STR_LIT("ssao persp"), {(const char*)ssao_frag, ssao_frag_size}, STR_LIT("#define AO_PERSPECTIVE 1"));
    gl.ssao.hbao.program_ortho = setup_program_from_source(STR_LIT("ssao ortho"), {(const char*)ssao_frag, ssao_frag_size}, STR_LIT("#define AO_PERSPECTIVE 0"));
    gl.ssao.blur.program       = setup_program_from_source(STR_LIT("ssao blur"),  {(const char*)blur_frag, blur_frag_size});
    
    if (!gl.ssao.fbo) glGenFramebuffers(1, &gl.ssao.fbo);

    if (!gl.ssao.tex_random) glGenTextures(1, &gl.ssao.tex_random);
    if (!gl.ssao.tex[0])     glGenTextures(2, gl.ssao.tex);

    if (!gl.ssao.ubo_hbao_data) glGenBuffers(1, &gl.ssao.ubo_hbao_data);

    initialize_rnd_tex(gl.ssao.tex_random);

    glBindTexture(GL_TEXTURE_2D, gl.ssao.tex[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, gl.ssao.tex[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl.ssao.fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl.ssao.tex[0], 0);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gl.ssao.tex[1], 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    glBindBuffer(GL_UNIFORM_BUFFER, gl.ssao.ubo_hbao_data);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(HBAOData), nullptr, GL_DYNAMIC_DRAW);
}

void shutdown() {
    if (gl.ssao.fbo) glDeleteFramebuffers(1, &gl.ssao.fbo);
    if (gl.ssao.tex_random) glDeleteTextures(1, &gl.ssao.tex_random);
    if (gl.ssao.tex[0]) glDeleteTextures(2, gl.ssao.tex);
    if (gl.ssao.ubo_hbao_data) glDeleteBuffers(1, &gl.ssao.ubo_hbao_data);
    if (gl.ssao.hbao.program_persp) glDeleteProgram(gl.ssao.hbao.program_persp);
    if (gl.ssao.hbao.program_ortho) glDeleteProgram(gl.ssao.hbao.program_ortho);
    if (gl.ssao.blur.program) glDeleteProgram(gl.ssao.blur.program);
}

}  // namespace ssao

namespace fxaa {
void initialize() {
    gl.luma.program = setup_program_from_source(STR_LIT("luma"), {(const char*)luma_frag, luma_frag_size});
    gl.luma.uniform_loc.tex_rgba = glGetUniformLocation(gl.luma.program, "u_tex_rgba");

    str_t defines = STR_LIT("#define FXAA_PC 1\n#define FXAA_GLSL_130 1\n#define FXAA_QUALITY__PRESET 12");
    gl.fxaa.program = setup_program_from_source(STR_LIT("fxaa"), {(const char*)fxaa_frag, fxaa_frag_size}, defines);
    gl.fxaa.uniform_loc.tex_rgbl = glGetUniformLocation(gl.fxaa.program, "u_tex_rgbl");
    gl.fxaa.uniform_loc.rcp_res  = glGetUniformLocation(gl.fxaa.program, "u_rcp_res");
    gl.fxaa.uniform_loc.tc_scl   = glGetUniformLocation(gl.fxaa.program, "u_tc_scl");

}

void shutdown() {
    if (gl.luma.program) glDeleteProgram(gl.luma.program);
    if (gl.fxaa.program) glDeleteProgram(gl.fxaa.program);
}
}

namespace highlight {

static struct {
    GLuint program = 0;
    GLuint selection_texture = 0;
    struct {
        GLint texture_atom_idx = -1;
        GLint buffer_selection = -1;
        GLint highlight = -1;
        GLint selection = -1;
        GLint outline = -1;
    } uniform_loc;
} highlight;

void initialize() {
    highlight.program = setup_program_from_source(STR_LIT("highlight"), {(const char*)highlight_frag, highlight_frag_size});
    if (!highlight.selection_texture) glGenTextures(1, &highlight.selection_texture);
    highlight.uniform_loc.texture_atom_idx = glGetUniformLocation(highlight.program, "u_texture_atom_idx");
    highlight.uniform_loc.buffer_selection = glGetUniformLocation(highlight.program, "u_buffer_selection");
    highlight.uniform_loc.highlight = glGetUniformLocation(highlight.program, "u_highlight");
    highlight.uniform_loc.selection = glGetUniformLocation(highlight.program, "u_selection");
    highlight.uniform_loc.outline = glGetUniformLocation(highlight.program, "u_outline");
}

void shutdown() {
    if (highlight.program) glDeleteProgram(highlight.program);
}
}  // namespace highlight

namespace hsv {

static struct {
    GLuint program = 0;
    struct {
        GLint texture_color = -1;
        GLint hsv_scale = -1;
    } uniform_loc;
} gl;

void initialize() {
    gl.program = setup_program_from_source(STR_LIT("scale hsv"), {(const char*)scale_hsv_frag, scale_hsv_frag_size});
    gl.uniform_loc.texture_color = glGetUniformLocation(gl.program, "u_texture_atom_color");
    gl.uniform_loc.hsv_scale = glGetUniformLocation(gl.program, "u_hsv_scale");
}

void shutdown() {
    if (gl.program) glDeleteProgram(gl.program);
}
}  // namespace hsv

namespace compose {

struct ubo_data_t {
    mat4_t inv_proj_mat;
    vec3_t bg_color;
    float  time;
    vec3_t env_radiance;
    float  roughness;
    vec3_t dir_radiance;
    float  F0;
    vec3_t light_dir;
};

static struct {
    GLuint program = 0;
    GLuint ubo = 0;
    struct {
        GLint uniform_data = -1;
        GLint texture_depth = -1;
        GLint texture_color = -1;
        GLint texture_normal = -1;
    } uniform_loc;
} compose;

void initialize() {
    compose.program = setup_program_from_source(STR_LIT("compose deferred"), {(const char*)compose_deferred_frag, compose_deferred_frag_size});
    
    if (compose.ubo == 0) {
        glGenBuffers(1, &compose.ubo);
        glBindBuffer(GL_UNIFORM_BUFFER, compose.ubo);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(ubo_data_t), 0, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    compose.uniform_loc.texture_depth       = glGetUniformLocation(compose.program, "u_texture_depth");
    compose.uniform_loc.texture_color       = glGetUniformLocation(compose.program, "u_texture_color");
    compose.uniform_loc.texture_normal      = glGetUniformLocation(compose.program, "u_texture_normal");
    compose.uniform_loc.uniform_data        = glGetUniformBlockIndex(compose.program, "UniformData");
}

void shutdown() {
    if (compose.program)    glDeleteProgram(compose.program);
    if (compose.ubo)        glDeleteBuffers(1, &compose.ubo);
}
}  // namespace compose

namespace tonemapping {

static struct {
    GLuint program = 0;
    struct {
        GLint texture = -1;
    } uniform_loc;
} passthrough;

static struct {
    GLuint program = 0;
    struct {
        GLint texture = -1;
        GLint exposure = -1;
        GLint gamma = -1;
    } uniform_loc;
} exposure_gamma;

static struct {
    GLuint program = 0;
    struct {
        GLint texture = -1;
        GLint exposure = -1;
        GLint gamma = -1;
    } uniform_loc;
} filmic;

static struct {
    GLuint program = 0;
    struct {
        GLint texture = -1;
        GLint exposure = -1;
        GLint gamma = -1;
    } uniform_loc;
} aces;

static struct {
    GLuint program_forward = 0;
    GLuint program_inverse = 0;
    struct {
        GLint texture = -1;
    } uniform_loc;
} fast_reversible;

void initialize() {
    {
        // PASSTHROUGH
        passthrough.program = setup_program_from_source(STR_LIT("Passthrough"), {(const char*)passthrough_frag, passthrough_frag_size});
        passthrough.uniform_loc.texture = glGetUniformLocation(passthrough.program, "u_texture");
    }
    {
        // EXPOSURE GAMMA
        exposure_gamma.program = setup_program_from_source(STR_LIT("Exposure Gamma"), {(const char*)exposure_gamma_frag, exposure_gamma_frag_size});
        exposure_gamma.uniform_loc.texture = glGetUniformLocation(exposure_gamma.program, "u_texture");
        exposure_gamma.uniform_loc.exposure = glGetUniformLocation(exposure_gamma.program, "u_exposure");
        exposure_gamma.uniform_loc.gamma = glGetUniformLocation(exposure_gamma.program, "u_gamma");
    }
    {
        // UNCHARTED
        filmic.program = setup_program_from_source(STR_LIT("Filmic"), {(const char*)uncharted_frag, uncharted_frag_size});
        filmic.uniform_loc.texture = glGetUniformLocation(filmic.program, "u_texture");
        filmic.uniform_loc.exposure = glGetUniformLocation(filmic.program, "u_exposure");
        filmic.uniform_loc.gamma = glGetUniformLocation(filmic.program, "u_gamma");
    }
    {
        // ACES
        aces.program = setup_program_from_source(STR_LIT("ACES"), {(const char*)aces_frag, aces_frag_size});
        aces.uniform_loc.texture = glGetUniformLocation(filmic.program, "u_texture");
        aces.uniform_loc.exposure = glGetUniformLocation(filmic.program, "u_exposure");
        aces.uniform_loc.gamma = glGetUniformLocation(filmic.program, "u_gamma");
    }
    {
        // Fast Reversible (For AA) (Credits to Brian Karis: http://graphicrants.blogspot.com/2013/12/tone-mapping.html)
        fast_reversible.program_forward = setup_program_from_source(STR_LIT("Fast Reversible"), {(const char*)fast_reversible_frag, fast_reversible_frag_size}, STR_LIT("#define USE_INVERSE 0"));
        fast_reversible.program_inverse = setup_program_from_source(STR_LIT("Fast Reversible"), {(const char*)fast_reversible_frag, fast_reversible_frag_size}, STR_LIT("#define USE_INVERSE 1"));
        fast_reversible.uniform_loc.texture = glGetUniformLocation(fast_reversible.program_forward, "u_texture");
    }
}

void shutdown() {
    if (passthrough.program) glDeleteProgram(passthrough.program);
    if (exposure_gamma.program) glDeleteProgram(exposure_gamma.program);
    if (filmic.program) glDeleteProgram(filmic.program);
    if (aces.program) glDeleteProgram(aces.program);
    if (fast_reversible.program_forward) glDeleteProgram(fast_reversible.program_forward);
    if (fast_reversible.program_inverse) glDeleteProgram(fast_reversible.program_inverse);
}

}  // namespace tonemapping

namespace dof {
void initialize(int32_t width, int32_t height) {
    {
        gl.bokeh_dof.half_res.program = setup_program_from_source(STR_LIT("DOF prepass"), {(const char*)dof_half_res_prepass_frag, dof_half_res_prepass_frag_size});
        if (gl.bokeh_dof.half_res.program) {
            gl.bokeh_dof.half_res.uniform_loc.tex_depth   = glGetUniformLocation(gl.bokeh_dof.half_res.program, "u_tex_depth");
            gl.bokeh_dof.half_res.uniform_loc.tex_color   = glGetUniformLocation(gl.bokeh_dof.half_res.program, "u_tex_color");
            gl.bokeh_dof.half_res.uniform_loc.focus_point = glGetUniformLocation(gl.bokeh_dof.half_res.program, "u_focus_point");
            gl.bokeh_dof.half_res.uniform_loc.focus_scale = glGetUniformLocation(gl.bokeh_dof.half_res.program, "u_focus_scale");
        }
    }

    if (!gl.bokeh_dof.half_res.tex.color_coc) {
        glGenTextures(1, &gl.bokeh_dof.half_res.tex.color_coc);
    }
    glBindTexture(GL_TEXTURE_2D, gl.bokeh_dof.half_res.tex.color_coc);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width / 2, height / 2, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!gl.bokeh_dof.half_res.fbo) {
        glGenFramebuffers(1, &gl.bokeh_dof.half_res.fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl.bokeh_dof.half_res.fbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl.bokeh_dof.half_res.tex.color_coc, 0);
        GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            MD_LOG_ERROR("Something went wrong when generating framebuffer for DOF");
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }

    // DOF
    {
        gl.bokeh_dof.program = setup_program_from_source(STR_LIT("Bokeh DOF"), {(const char*)dof_frag, dof_frag_size});
        if (gl.bokeh_dof.program) {
            gl.bokeh_dof.uniform_loc.tex_color = glGetUniformLocation(gl.bokeh_dof.program, "u_half_res");
            gl.bokeh_dof.uniform_loc.tex_color = glGetUniformLocation(gl.bokeh_dof.program, "u_tex_color");
            gl.bokeh_dof.uniform_loc.tex_depth = glGetUniformLocation(gl.bokeh_dof.program, "u_tex_depth");
            gl.bokeh_dof.uniform_loc.pixel_size = glGetUniformLocation(gl.bokeh_dof.program, "u_texel_size");
            gl.bokeh_dof.uniform_loc.focus_point = glGetUniformLocation(gl.bokeh_dof.program, "u_focus_depth");
            gl.bokeh_dof.uniform_loc.focus_scale = glGetUniformLocation(gl.bokeh_dof.program, "u_focus_scale");
            gl.bokeh_dof.uniform_loc.time = glGetUniformLocation(gl.bokeh_dof.program, "u_time");
        }
    }
}

void shutdown() {}
}  // namespace dof

namespace blit {
static GLuint program_tex = 0;
static GLuint program_col = 0;
static GLint uniform_loc_texture = -1;
static GLint uniform_loc_color = -1;

constexpr str_t f_shader_src_tex = STR_LIT(R"(
#version 150 core

uniform sampler2D u_texture;

out vec4 out_frag;

void main() {
    out_frag = texelFetch(u_texture, ivec2(gl_FragCoord.xy), 0);
}
)");

constexpr str_t f_shader_src_col = STR_LIT(R"(
#version 150 core

uniform vec4 u_color;
out vec4 out_frag;

void main() {
	out_frag = u_color;
}
)");

void initialize() {
    program_tex = setup_program_from_source(STR_LIT("blit texture"), f_shader_src_tex);
    uniform_loc_texture = glGetUniformLocation(program_tex, "u_texture");

    program_col = setup_program_from_source(STR_LIT("blit color"), f_shader_src_col);
    uniform_loc_color = glGetUniformLocation(program_col, "u_color");
}

void shutdown() {
    if (program_tex) glDeleteProgram(program_tex);
    if (program_col) glDeleteProgram(program_col);
}
}  // namespace blit

namespace blur {
static GLuint program_gaussian = 0;
static GLuint program_box = 0;
static GLint uniform_loc_texture = -1;
static GLint uniform_loc_inv_res_dir = -1;

constexpr str_t f_shader_src_gaussian = STR_LIT(R"(
#version 150 core

#define KERNEL_RADIUS 5

uniform sampler2D u_texture;
uniform vec2      u_inv_res_dir;

in vec2 tc;
out vec4 out_frag;

float blur_weight(float r) {
    const float sigma = KERNEL_RADIUS * 0.5;
    const float falloff = 1.0 / (2.0*sigma*sigma);
    float w = exp2(-r*r*falloff);
    return w;
}

void main() {
    vec2 uv = tc;
    vec4  c_tot = texture(u_texture, uv);
    float w_tot = 1.0;

    for (float r = 1; r <= KERNEL_RADIUS; ++r) {
        float w = blur_weight(r);
        vec4  c = texture(u_texture, uv + u_inv_res_dir * r);
        c_tot += c * w;
        w_tot += w;
    }
    for (float r = 1; r <= KERNEL_RADIUS; ++r) {
        float w = blur_weight(r);
        vec4  c = texture(u_texture, uv - u_inv_res_dir * r);
        c_tot += c * w;
        w_tot += w;
    }

    out_frag = c_tot / w_tot;
}
)");

constexpr str_t f_shader_src_box = STR_LIT(R"(
#version 150 core

uniform sampler2D u_texture;
out vec4 out_frag;

void main() {
    vec4 c = vec4(0);
    c += texelFetch(u_texture, ivec2(gl_FragCoord.xy) + ivec2(-1, -1), 0);
    c += texelFetch(u_texture, ivec2(gl_FragCoord.xy) + ivec2( 0, -1), 0);
    c += texelFetch(u_texture, ivec2(gl_FragCoord.xy) + ivec2(+1, -1), 0);
    c += texelFetch(u_texture, ivec2(gl_FragCoord.xy) + ivec2(-1,  0), 0);
    c += texelFetch(u_texture, ivec2(gl_FragCoord.xy) + ivec2( 0,  0), 0);
    c += texelFetch(u_texture, ivec2(gl_FragCoord.xy) + ivec2(+1,  0), 0);
    c += texelFetch(u_texture, ivec2(gl_FragCoord.xy) + ivec2(-1, +1), 0);
    c += texelFetch(u_texture, ivec2(gl_FragCoord.xy) + ivec2( 0, +1), 0);
    c += texelFetch(u_texture, ivec2(gl_FragCoord.xy) + ivec2(+1, +1), 0);

    out_frag = c / 9.0;
}
)");

void initialize() {
    program_gaussian = setup_program_from_source(STR_LIT("gaussian blur"), f_shader_src_gaussian);
    uniform_loc_texture = glGetUniformLocation(program_gaussian, "u_texture");
    uniform_loc_inv_res_dir = glGetUniformLocation(program_gaussian, "u_inv_res_dir");

    program_box = setup_program_from_source(STR_LIT("box blur"), f_shader_src_box);
}

void shutdown() {
    if (program_gaussian) glDeleteProgram(program_gaussian);
    if (program_box) glDeleteProgram(program_box);
}
}  // namespace blit

namespace velocity {
#define VEL_TILE_SIZE 8

struct {
    GLuint program = 0;
    struct {
		GLint tex_depth = -1;
        GLint curr_clip_to_prev_clip_mat = -1;
        GLint jitter_uv = -1;
    } uniform_loc;
} blit_velocity;

struct {
    GLuint program = 0;
    struct {
        GLint tex_vel = -1;
        GLint tex_vel_texel_size = -1;
    } uniform_loc;
} blit_tilemax;

struct {
    GLuint program = 0;
    struct {
        GLint tex_vel = -1;
        GLint tex_vel_texel_size = -1;
    } uniform_loc;
} blit_neighbormax;

void initialize(int32_t width, int32_t height) {
    {
        blit_velocity.program = setup_program_from_source(STR_LIT("screen-space velocity"), {(const char*)blit_velocity_frag, blit_velocity_frag_size});
		blit_velocity.uniform_loc.tex_depth = glGetUniformLocation(blit_velocity.program, "u_tex_depth");
        blit_velocity.uniform_loc.curr_clip_to_prev_clip_mat = glGetUniformLocation(blit_velocity.program, "u_curr_clip_to_prev_clip_mat");
        blit_velocity.uniform_loc.jitter_uv = glGetUniformLocation(blit_velocity.program, "u_jitter_uv");

    }
    {
        str_t defines = STR_LIT("#define TILE_SIZE " STRINGIFY_VAL(VEL_TILE_SIZE));
        blit_tilemax.program = setup_program_from_source(STR_LIT("tilemax"), {(const char*)blit_tilemax_frag, blit_tilemax_frag_size}, defines);
        blit_tilemax.uniform_loc.tex_vel = glGetUniformLocation(blit_tilemax.program, "u_tex_vel");
        blit_tilemax.uniform_loc.tex_vel_texel_size = glGetUniformLocation(blit_tilemax.program, "u_tex_vel_texel_size");
    }
    {
        blit_neighbormax.program = setup_program_from_source(STR_LIT("neighbormax"), {(const char*)blit_neighbormax_frag, blit_neighbormax_frag_size});
        blit_neighbormax.uniform_loc.tex_vel = glGetUniformLocation(blit_neighbormax.program, "u_tex_vel");
        blit_neighbormax.uniform_loc.tex_vel_texel_size = glGetUniformLocation(blit_neighbormax.program, "u_tex_vel_texel_size");
    }

    if (!gl.velocity.tex_tilemax) {
        glGenTextures(1, &gl.velocity.tex_tilemax);
    }

    if (!gl.velocity.tex_neighbormax) {
        glGenTextures(1, &gl.velocity.tex_neighbormax);
    }

    gl.velocity.tex_width = width / VEL_TILE_SIZE;
    gl.velocity.tex_height = height / VEL_TILE_SIZE;

    glBindTexture(GL_TEXTURE_2D, gl.velocity.tex_tilemax);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, gl.velocity.tex_width, gl.velocity.tex_height, 0, GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindTexture(GL_TEXTURE_2D, gl.velocity.tex_neighbormax);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, gl.velocity.tex_width, gl.velocity.tex_height, 0, GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!gl.velocity.fbo) {
        glGenFramebuffers(1, &gl.velocity.fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl.velocity.fbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl.velocity.tex_tilemax, 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gl.velocity.tex_neighbormax, 0);
        GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            MD_LOG_ERROR("Something went wrong in creating framebuffer for velocity");
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }
}

void shutdown() {
    if (blit_velocity.program) glDeleteProgram(blit_velocity.program);
    if (gl.velocity.tex_tilemax) glDeleteTextures(1, &gl.velocity.tex_tilemax);
    if (gl.velocity.tex_neighbormax) glDeleteTextures(1, &gl.velocity.tex_neighbormax);
    if (gl.velocity.fbo) glDeleteFramebuffers(1, &gl.velocity.fbo);
}
}  // namespace velocity

namespace temporal {
void initialize() {
    {
        gl.temporal.with_motion_blur.program = setup_program_from_source(STR_LIT("temporal aa + motion-blur"), {(const char*)temporal_frag, temporal_frag_size});
        gl.temporal.no_motion_blur.program   = setup_program_from_source(STR_LIT("temporal aa"), {(const char*)temporal_frag, temporal_frag_size}, STR_LIT("#define USE_MOTION_BLUR 0\n"));

        gl.temporal.with_motion_blur.uniform_loc.tex_linear_depth = glGetUniformLocation(gl.temporal.with_motion_blur.program, "u_tex_linear_depth");
        gl.temporal.with_motion_blur.uniform_loc.tex_main = glGetUniformLocation(gl.temporal.with_motion_blur.program, "u_tex_main");
        gl.temporal.with_motion_blur.uniform_loc.tex_prev = glGetUniformLocation(gl.temporal.with_motion_blur.program, "u_tex_prev");
        gl.temporal.with_motion_blur.uniform_loc.tex_vel = glGetUniformLocation(gl.temporal.with_motion_blur.program, "u_tex_vel");
        gl.temporal.with_motion_blur.uniform_loc.tex_vel_neighbormax = glGetUniformLocation(gl.temporal.with_motion_blur.program, "u_tex_vel_neighbormax");
        gl.temporal.with_motion_blur.uniform_loc.texel_size = glGetUniformLocation(gl.temporal.with_motion_blur.program, "u_texel_size");
        gl.temporal.with_motion_blur.uniform_loc.jitter_uv = glGetUniformLocation(gl.temporal.with_motion_blur.program, "u_jitter_uv");
        gl.temporal.with_motion_blur.uniform_loc.time = glGetUniformLocation(gl.temporal.with_motion_blur.program, "u_time");
        gl.temporal.with_motion_blur.uniform_loc.feedback_min = glGetUniformLocation(gl.temporal.with_motion_blur.program, "u_feedback_min");
        gl.temporal.with_motion_blur.uniform_loc.feedback_max = glGetUniformLocation(gl.temporal.with_motion_blur.program, "u_feedback_max");
        gl.temporal.with_motion_blur.uniform_loc.motion_scale = glGetUniformLocation(gl.temporal.with_motion_blur.program, "u_motion_scale");

        gl.temporal.no_motion_blur.uniform_loc.tex_linear_depth = glGetUniformLocation(gl.temporal.no_motion_blur.program, "u_tex_linear_depth");
        gl.temporal.no_motion_blur.uniform_loc.tex_main = glGetUniformLocation(gl.temporal.no_motion_blur.program, "u_tex_main");
        gl.temporal.no_motion_blur.uniform_loc.tex_prev = glGetUniformLocation(gl.temporal.no_motion_blur.program, "u_tex_prev");
        gl.temporal.no_motion_blur.uniform_loc.tex_vel = glGetUniformLocation(gl.temporal.no_motion_blur.program, "u_tex_vel");
        gl.temporal.no_motion_blur.uniform_loc.texel_size = glGetUniformLocation(gl.temporal.no_motion_blur.program, "u_texel_size");
        gl.temporal.no_motion_blur.uniform_loc.jitter_uv = glGetUniformLocation(gl.temporal.no_motion_blur.program, "u_jitter_uv");
        gl.temporal.no_motion_blur.uniform_loc.time = glGetUniformLocation(gl.temporal.no_motion_blur.program, "u_time");
        gl.temporal.no_motion_blur.uniform_loc.feedback_min = glGetUniformLocation(gl.temporal.no_motion_blur.program, "u_feedback_min");
        gl.temporal.no_motion_blur.uniform_loc.feedback_max = glGetUniformLocation(gl.temporal.no_motion_blur.program, "u_feedback_max");
        gl.temporal.no_motion_blur.uniform_loc.motion_scale = glGetUniformLocation(gl.temporal.no_motion_blur.program, "u_motion_scale");
    }
}

void shutdown() {}
}  // namespace temporal

namespace sharpen {
static GLuint program = 0;
void initialize() {
    constexpr str_t f_shader_src_sharpen = STR_LIT(
 R"(#version 150 core

    uniform sampler2D u_tex;
    uniform float u_weight;
    out vec4 out_frag;

    void main() {
        vec3 cc = texelFetch(u_tex, ivec2(gl_FragCoord.xy), 0).rgb;
        vec3 cl = texelFetch(u_tex, ivec2(gl_FragCoord.xy) + ivec2(-1, 0), 0).rgb;
        vec3 ct = texelFetch(u_tex, ivec2(gl_FragCoord.xy) + ivec2( 0, 1), 0).rgb;
        vec3 cr = texelFetch(u_tex, ivec2(gl_FragCoord.xy) + ivec2( 1, 0), 0).rgb;
        vec3 cb = texelFetch(u_tex, ivec2(gl_FragCoord.xy) + ivec2( 0,-1), 0).rgb;

        float pos_weight = 1.0 + u_weight;
        float neg_weight = -u_weight * 0.25;
        out_frag = vec4(vec3(pos_weight * cc + neg_weight * (cl + ct + cr + cb)), 1.0);
    })");
    program = setup_program_from_source(STR_LIT("sharpen"), f_shader_src_sharpen);
}

void sharpen(GLuint in_texture, float weight = 1.0f) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, in_texture);

    glUseProgram(program);
    glUniform1i(glGetUniformLocation(sharpen::program, "u_tex"), 0);
    glUniform1f(glGetUniformLocation(sharpen::program, "u_weight"), weight);

    glBindVertexArray(gl.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glUseProgram(0);
}

void shutdown() {
    if (program) glDeleteProgram(program);
}
}

void initialize(int width, int height) {
    if (!gl.vao) glGenVertexArrays(1, &gl.vao);

    gl.v_shader_fs_quad = gl::compile_shader_from_source(v_shader_src_fs_quad, GL_VERTEX_SHADER);

    // LINEARIZE DEPTH

    gl.linear_depth.program_persp = setup_program_from_source(STR_LIT("linearize depth persp"), f_shader_src_linearize_depth, STR_LIT("#version 150 core\n#define PERSPECTIVE 1"));
    gl.linear_depth.program_ortho = setup_program_from_source(STR_LIT("linearize depth ortho"), f_shader_src_linearize_depth, STR_LIT("#version 150 core\n#define PERSPECTIVE 0"));

    if (!gl.linear_depth.texture) glGenTextures(1, &gl.linear_depth.texture);
    glBindTexture(GL_TEXTURE_2D, gl.linear_depth.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, width, height, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!gl.linear_depth.fbo) {
        glGenFramebuffers(1, &gl.linear_depth.fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl.linear_depth.fbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl.linear_depth.texture, 0);
        GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            MD_LOG_ERROR("Something went wrong in creating framebuffer for depth linearization");
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }

    gl.linear_depth.uniform_loc.clip_info = glGetUniformLocation(gl.linear_depth.program_persp, "u_clip_info");
    gl.linear_depth.uniform_loc.tex_depth = glGetUniformLocation(gl.linear_depth.program_persp, "u_tex_depth");

    // COLOR
    if (!gl.targets.tex_color[0]) glGenTextures(2, gl.targets.tex_color);
    glBindTexture(GL_TEXTURE_2D, gl.targets.tex_color[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R11F_G11F_B10F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, gl.targets.tex_color[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R11F_G11F_B10F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!gl.targets.tex_temporal_buffer[0]) glGenTextures(2, gl.targets.tex_temporal_buffer);
    glBindTexture(GL_TEXTURE_2D, gl.targets.tex_temporal_buffer[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, gl.targets.tex_temporal_buffer[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!gl.targets.fbo) {
        glGenFramebuffers(1, &gl.targets.fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl.targets.fbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl.targets.tex_color[0], 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gl.targets.tex_color[1], 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, gl.targets.tex_temporal_buffer[0], 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, gl.targets.tex_temporal_buffer[1], 0);

        GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            MD_LOG_ERROR("Something went wrong in creating framebuffer for targets");
        }

        GLenum buffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
        glDrawBuffers(4, buffers);
        glClear(GL_COLOR_BUFFER_BIT);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }

    if (!gl.tmp.tex_rgba8) glGenTextures(1, &gl.tmp.tex_rgba8);
    glBindTexture(GL_TEXTURE_2D, gl.tmp.tex_rgba8);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!gl.tmp.fbo) {
        glGenFramebuffers(1, &gl.tmp.fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl.tmp.fbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl.tmp.tex_rgba8, 0);
    }

    gl.tex_width = width;
    gl.tex_height = height;

    ssao::initialize(width, height);
    dof::initialize(width, height);
    velocity::initialize(width, height);
    highlight::initialize();
    hsv::initialize();
    tonemapping::initialize();
    temporal::initialize();
    blit::initialize();
    blur::initialize();
    sharpen::initialize();
    compose::initialize();
    fxaa::initialize();
}

void shutdown() {
    ssao::shutdown();
    dof::shutdown();
    velocity::shutdown();
    highlight::shutdown();
    hsv::shutdown();
    tonemapping::shutdown();
    temporal::shutdown();
    blit::shutdown();
    blur::shutdown();
    sharpen::shutdown();
    compose::shutdown();
    fxaa::shutdown();

    if (gl.vao) glDeleteVertexArrays(1, &gl.vao);
    //if (gl.vbo) glDeleteBuffers(1, &gl.vbo);
    if (gl.v_shader_fs_quad) glDeleteShader(gl.v_shader_fs_quad);
    if (gl.tmp.fbo) glDeleteFramebuffers(1, &gl.tmp.fbo);
    if (gl.tmp.tex_rgba8) glDeleteTextures(1, &gl.tmp.tex_rgba8);
}

void compute_linear_depth(GLuint depth_tex, float near_plane, float far_plane, bool orthographic = false) {
    const vec4_t clip_info {near_plane * far_plane, near_plane - far_plane, far_plane, 0};

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, depth_tex);

    GLuint program = orthographic ? gl.linear_depth.program_ortho : gl.linear_depth.program_persp;
    glUseProgram(program);
    glUniform1i(gl.linear_depth.uniform_loc.tex_depth, 0);
    glUniform4fv(gl.linear_depth.uniform_loc.clip_info, 1, &clip_info.x);

    // ASSUME THAT THE APPROPRIATE FS_QUAD VAO IS BOUND
    glBindVertexArray(gl.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void compute_ssao(GLuint linear_depth_tex, GLuint normal_tex, const mat4_t& proj_matrix, float intensity, float radius, float bias) {
    ASSERT(glIsTexture(linear_depth_tex));
    ASSERT(glIsTexture(normal_tex));

    GLint last_fbo;
    GLint last_viewport[4];
    GLint last_draw_buffer;
    GLint last_scissor_box[4];
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &last_fbo);
    glGetIntegerv(GL_VIEWPORT, last_viewport);
    glGetIntegerv(GL_SCISSOR_BOX, last_scissor_box);
    glGetIntegerv(GL_DRAW_BUFFER, &last_draw_buffer);

    int width  = last_viewport[2];
    int height = last_viewport[3];

    const bool ortho = is_orthographic_proj_matrix(proj_matrix);
    const float sharpness = ssao::compute_sharpness(radius);
    const vec2_t inv_res = vec2_t{ 1.f / (float)width, 1.f / (float)height };

    glBindVertexArray(gl.vao);

    ssao::HBAOData ubo_data = {};
    ssao::setup_ubo_hbao_data(&ubo_data, width, height, inv_res, proj_matrix, intensity, radius, bias);
    glBindBuffer(GL_UNIFORM_BUFFER, gl.ssao.ubo_hbao_data);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ssao::HBAOData), &ubo_data);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl.ssao.fbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0, 0, width, height);
    glScissor(0, 0, width, height);
    glClearColor(1,1,1,1);
    glClear(GL_COLOR_BUFFER_BIT);

    GLuint program = ortho ? gl.ssao.hbao.program_ortho : gl.ssao.hbao.program_persp;

    PUSH_GPU_SECTION("HBAO")
    glUseProgram(program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, linear_depth_tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, normal_tex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, gl.ssao.tex_random);

    glBindBufferBase(GL_UNIFORM_BUFFER, 0, gl.ssao.ubo_hbao_data);
    glUniformBlockBinding(program, glGetUniformBlockIndex(program, "u_control_buffer"), 0);
    glUniform1i(glGetUniformLocation(program, "u_tex_linear_depth"), 0);
    glUniform1i(glGetUniformLocation(program, "u_tex_normal"), 1);
    glUniform1i(glGetUniformLocation(program, "u_tex_random"), 2);

    glDrawArrays(GL_TRIANGLES, 0, 3);
    POP_GPU_SECTION()

    PUSH_GPU_SECTION("BLUR");
    glUseProgram(gl.ssao.blur.program);

    glUniform1i(glGetUniformLocation(gl.ssao.blur.program, "u_tex_linear_depth"), 0);
    glUniform1i(glGetUniformLocation(gl.ssao.blur.program, "u_tex_ao"), 1);
    glUniform1f(glGetUniformLocation(gl.ssao.blur.program, "u_sharpness"), sharpness);
    glUniform1f(glGetUniformLocation(gl.ssao.blur.program, "u_zmax"), ubo_data.z_max);
    glUniform2f(glGetUniformLocation(gl.ssao.blur.program, "u_inv_res_dir"), inv_res.x, 0);

    glActiveTexture(GL_TEXTURE1);

    // BLUR FIRST
    PUSH_GPU_SECTION("1st")
    glDrawBuffer(GL_COLOR_ATTACHMENT1);
    glBindTexture(GL_TEXTURE_2D, gl.ssao.tex[0]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    POP_GPU_SECTION()

    glUniform2f(glGetUniformLocation(gl.ssao.blur.program, "u_inv_res_dir"), 0, inv_res.y);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_SRC_COLOR);

    // BLUR SECOND AND BLEND RESULT
    PUSH_GPU_SECTION("2nd")
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, last_fbo);
    glDrawBuffer(last_draw_buffer);
    glViewport(last_viewport[0], last_viewport[1], last_viewport[2], last_viewport[3]);
    glScissor(last_scissor_box[0], last_scissor_box[1], last_scissor_box[2], last_scissor_box[3]);
    glBindTexture(GL_TEXTURE_2D, gl.ssao.tex[1]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    POP_GPU_SECTION()

    glDisable(GL_BLEND);

    glBindVertexArray(0);
    POP_GPU_SECTION()
}

static void compose_deferred(GLuint depth_tex, GLuint color_tex, GLuint normal_tex, const mat4_t& inv_proj_matrix, const vec3_t bg_color, float time) {
    ASSERT(glIsTexture(depth_tex));
    ASSERT(glIsTexture(color_tex));
    ASSERT(glIsTexture(normal_tex));

    const vec3_t env_radiance = bg_color * 0.25f;
    const vec3_t dir_radiance = {10, 10, 10};
    const float roughness = 0.4f;
    const float F0 = 0.04f;
    const vec3_t L = {0.57735026918962576451f, 0.57735026918962576451f, 0.57735026918962576451f}; // 1.0 / sqrt(3)

    compose::ubo_data_t data = {
        .inv_proj_mat = inv_proj_matrix,
        .bg_color = bg_color,
        .time = time,
        .env_radiance = env_radiance,
        .roughness = roughness,
        .dir_radiance = dir_radiance,
        .F0 = F0,
        .light_dir = L,
    };

    glBindBuffer(GL_UNIFORM_BUFFER, compose::compose.ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(data), &data);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, depth_tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, color_tex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, normal_tex);

    GLuint program = compose::compose.program;

    glUseProgram(program);

    glBindBufferBase(GL_UNIFORM_BUFFER, 0, compose::compose.ubo);
    glUniformBlockBinding(program, compose::compose.uniform_loc.uniform_data, 0);
    glUniform1i (compose::compose.uniform_loc.texture_depth, 0);
    glUniform1i (compose::compose.uniform_loc.texture_color, 1);
    glUniform1i (compose::compose.uniform_loc.texture_normal, 2);

    glBindVertexArray(gl.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glUseProgram(0);
}

void highlight_selection(GLuint atom_idx_tex, GLuint selection_buffer, const vec3_t& highlight, const vec3_t& selection, const vec3_t& outline) {
    ASSERT(glIsTexture(atom_idx_tex));
    ASSERT(glIsBuffer(selection_buffer));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atom_idx_tex);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, highlight::highlight.selection_texture);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R8UI, selection_buffer);

    glUseProgram(highlight::highlight.program);
    glUniform1i(highlight::highlight.uniform_loc.texture_atom_idx, 0);
    glUniform1i(highlight::highlight.uniform_loc.buffer_selection, 1);
    glUniform3fv(highlight::highlight.uniform_loc.highlight, 1, &highlight.x);
    glUniform3fv(highlight::highlight.uniform_loc.selection, 1, &selection.x);
    glUniform3fv(highlight::highlight.uniform_loc.outline, 1, &outline.x);
    glBindVertexArray(gl.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
}

void half_res_color_coc(GLuint linear_depth_tex, GLuint color_tex, float focus_point, float focus_scale) {
    PUSH_GPU_SECTION("DOF Prepass");
    GLint last_viewport[4];
    glGetIntegerv(GL_VIEWPORT, last_viewport);
    glViewport(0, 0, gl.tex_width / 2, gl.tex_height / 2);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl.bokeh_dof.half_res.fbo);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, linear_depth_tex);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, color_tex);

    glUseProgram(gl.bokeh_dof.half_res.program);

    glUniform1i(gl.bokeh_dof.half_res.uniform_loc.tex_depth, 0);
    glUniform1i(gl.bokeh_dof.half_res.uniform_loc.tex_color, 1);
    glUniform1f(gl.bokeh_dof.half_res.uniform_loc.focus_point, focus_point);
    glUniform1f(gl.bokeh_dof.half_res.uniform_loc.focus_scale, focus_scale);

    glBindVertexArray(gl.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glViewport(last_viewport[0], last_viewport[1], last_viewport[2], last_viewport[3]);
    POP_GPU_SECTION();
}

void apply_dof(GLuint linear_depth_tex, GLuint color_tex, float focus_point, float focus_scale, float time) {
    ASSERT(glIsTexture(linear_depth_tex));
    ASSERT(glIsTexture(color_tex));

    const vec2_t pixel_size = vec2_t{1.f / gl.tex_width, 1.f / gl.tex_height};

    GLint last_fbo;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &last_fbo);

    half_res_color_coc(linear_depth_tex, color_tex, focus_point, focus_scale);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, last_fbo);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gl.bokeh_dof.half_res.tex.color_coc);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, linear_depth_tex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, color_tex);

    glUseProgram(gl.bokeh_dof.program);
    glUniform1i(gl.bokeh_dof.uniform_loc.tex_half_res, 0);
    glUniform1i(gl.bokeh_dof.uniform_loc.tex_depth, 1);
    glUniform1i(gl.bokeh_dof.uniform_loc.tex_color, 2);
    glUniform2f(gl.bokeh_dof.uniform_loc.pixel_size, pixel_size.x, pixel_size.y);
    glUniform1f(gl.bokeh_dof.uniform_loc.focus_point, focus_point);
    glUniform1f(gl.bokeh_dof.uniform_loc.focus_scale, focus_scale);
    glUniform1f(gl.bokeh_dof.uniform_loc.time, time);

    glBindVertexArray(gl.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
}

void apply_tonemapping(GLuint color_tex, Tonemapping tonemapping, float exposure, float gamma) {
    ASSERT(glIsTexture(color_tex));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, color_tex);

    switch (tonemapping) {
        case Tonemapping_ExposureGamma:
            glUseProgram(tonemapping::exposure_gamma.program);
            glUniform1i(tonemapping::exposure_gamma.uniform_loc.texture, 0);
            glUniform1f(tonemapping::exposure_gamma.uniform_loc.exposure, exposure);
            glUniform1f(tonemapping::exposure_gamma.uniform_loc.gamma, gamma);
            break;
        case Tonemapping_Filmic:
            glUseProgram(tonemapping::filmic.program);
            glUniform1i(tonemapping::filmic.uniform_loc.texture, 0);
            glUniform1f(tonemapping::filmic.uniform_loc.exposure, exposure);
            glUniform1f(tonemapping::filmic.uniform_loc.gamma, gamma);
            break;
        case Tonemapping_ACES:
            glUseProgram(tonemapping::aces.program);
            glUniform1i(tonemapping::aces.uniform_loc.texture, 0);
            glUniform1f(tonemapping::aces.uniform_loc.exposure, exposure);
            glUniform1f(tonemapping::aces.uniform_loc.gamma, gamma);
            break;
        case Tonemapping_Passthrough:
        default:
            glUseProgram(tonemapping::passthrough.program);
            glUniform1i(tonemapping::passthrough.uniform_loc.texture, 0);
            break;
    }

    glBindVertexArray(gl.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
}

void apply_aa_tonemapping(GLuint color_tex) {
    ASSERT(glIsTexture(color_tex));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, color_tex);

    glUseProgram(tonemapping::fast_reversible.program_forward);
    glUniform1i(tonemapping::fast_reversible.uniform_loc.texture, 0);

    glBindVertexArray(gl.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
}

void apply_inverse_aa_tonemapping(GLuint color_tex) {
    ASSERT(glIsTexture(color_tex));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, color_tex);

    glUseProgram(tonemapping::fast_reversible.program_inverse);
    glUniform1i(tonemapping::fast_reversible.uniform_loc.texture, 0);

    glBindVertexArray(gl.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
}

void blit_static_velocity(GLuint depth_tex, const ViewParam& view_param) {

    //mat4_t curr_clip_to_prev_clip_mat = view_param.matrix.previous.view_proj * view_param.matrix.inverse.view_proj;
    mat4_t curr_clip_to_prev_clip_mat = view_param.matrix.prev.proj * view_param.matrix.prev.view * view_param.matrix.inv.view * view_param.matrix.inv.proj;

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, depth_tex);

    const vec2_t res = view_param.resolution;
    const vec2_t jitter_uv_cur = view_param.jitter.curr / res;
    const vec2_t jitter_uv_prev = view_param.jitter.prev / res;
    const vec4_t jitter_uv = {jitter_uv_cur.x, jitter_uv_cur.y, jitter_uv_prev.x, jitter_uv_prev.y};

    glUseProgram(velocity::blit_velocity.program);
	glUniform1i(velocity::blit_velocity.uniform_loc.tex_depth, 0);
    glUniformMatrix4fv(velocity::blit_velocity.uniform_loc.curr_clip_to_prev_clip_mat, 1, GL_FALSE, &curr_clip_to_prev_clip_mat.elem[0][0]);
    glUniform4fv(velocity::blit_velocity.uniform_loc.jitter_uv, 1, &jitter_uv.x);
    glBindVertexArray(gl.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
}

void blit_tilemax(GLuint velocity_tex, int tex_width, int tex_height) {
    ASSERT(glIsTexture(velocity_tex));
    const vec2_t texel_size = {1.f / tex_width, 1.f / tex_height};

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, velocity_tex);

    glUseProgram(velocity::blit_tilemax.program);
    glUniform1i(velocity::blit_tilemax.uniform_loc.tex_vel, 0);
    glUniform2fv(velocity::blit_tilemax.uniform_loc.tex_vel_texel_size, 1, &texel_size.x);
    glBindVertexArray(gl.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
}

void blit_neighbormax(GLuint velocity_tex, int tex_width, int tex_height) {
    ASSERT(glIsTexture(velocity_tex));
    const vec2_t texel_size = {1.f / tex_width, 1.f / tex_height};

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, velocity_tex);

    glUseProgram(velocity::blit_neighbormax.program);
    glUniform1i(velocity::blit_neighbormax.uniform_loc.tex_vel, 0);
    glUniform2fv(velocity::blit_neighbormax.uniform_loc.tex_vel_texel_size, 1, &texel_size.x);
    glBindVertexArray(gl.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
}

void apply_temporal_aa(GLuint linear_depth_tex, GLuint color_tex, GLuint velocity_tex, GLuint velocity_neighbormax_tex, const vec2_t& curr_jitter, const vec2_t& prev_jitter, float feedback_min,
                       float feedback_max, float motion_scale, float time) {
    ASSERT(glIsTexture(linear_depth_tex));
    ASSERT(glIsTexture(color_tex));
    ASSERT(glIsTexture(velocity_tex));
    ASSERT(glIsTexture(velocity_neighbormax_tex));

    static int target = 0;
    target = (target + 1) % 2;

    const int dst_buf = target;
    const int src_buf = (target + 1) % 2;

    const vec2_t res = {(float)gl.tex_width, (float)gl.tex_height};
    const vec2_t inv_res = 1.0f / res;
    const vec4_t texel_size = vec4_t{inv_res.x, inv_res.y, res.x, res.y};
    const vec2_t jitter_uv_curr = curr_jitter / res;
    const vec2_t jitter_uv_prev = prev_jitter / res;
    const vec4_t jitter_uv = vec4_t{jitter_uv_curr.x, jitter_uv_curr.y, jitter_uv_prev.x, jitter_uv_prev.y};

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, linear_depth_tex);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, color_tex);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, gl.targets.tex_temporal_buffer[src_buf]);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, velocity_tex);

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, velocity_neighbormax_tex);

    GLint bound_buffer;
    glGetIntegerv(GL_DRAW_BUFFER, &bound_buffer);

    GLenum draw_buffers[2];
    draw_buffers[0] = GL_COLOR_ATTACHMENT2 + dst_buf;  // tex_temporal_buffer[0 or 1]
    draw_buffers[1] = bound_buffer;                    // assume that this is part of the same gbuffer

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl.targets.fbo);
    glViewport(0, 0, gl.tex_width, gl.tex_height);
    glDrawBuffers(2, draw_buffers);

    if (motion_scale != 0.f) {
        glUseProgram(gl.temporal.with_motion_blur.program);

        glUniform1i(gl.temporal.with_motion_blur.uniform_loc.tex_linear_depth, 0);
        glUniform1i(gl.temporal.with_motion_blur.uniform_loc.tex_main, 1);
        glUniform1i(gl.temporal.with_motion_blur.uniform_loc.tex_prev, 2);
        glUniform1i(gl.temporal.with_motion_blur.uniform_loc.tex_vel, 3);
        glUniform1i(gl.temporal.with_motion_blur.uniform_loc.tex_vel_neighbormax, 4);

        glUniform4fv(gl.temporal.with_motion_blur.uniform_loc.texel_size, 1, &texel_size.x);
        glUniform4fv(gl.temporal.with_motion_blur.uniform_loc.jitter_uv, 1, &jitter_uv.x);
        glUniform1f(gl.temporal.with_motion_blur.uniform_loc.time, time);
        glUniform1f(gl.temporal.with_motion_blur.uniform_loc.feedback_min, feedback_min);
        glUniform1f(gl.temporal.with_motion_blur.uniform_loc.feedback_max, feedback_max);
        glUniform1f(gl.temporal.with_motion_blur.uniform_loc.motion_scale, motion_scale);
    } else {
        glUseProgram(gl.temporal.no_motion_blur.program);

        glUniform1i(gl.temporal.no_motion_blur.uniform_loc.tex_linear_depth, 0);
        glUniform1i(gl.temporal.no_motion_blur.uniform_loc.tex_main, 1);
        glUniform1i(gl.temporal.no_motion_blur.uniform_loc.tex_prev, 2);
        glUniform1i(gl.temporal.no_motion_blur.uniform_loc.tex_vel, 3);

        glUniform4fv(gl.temporal.no_motion_blur.uniform_loc.texel_size, 1, &texel_size.x);
        glUniform4fv(gl.temporal.no_motion_blur.uniform_loc.jitter_uv, 1, &jitter_uv.x);
        glUniform1f(gl.temporal.no_motion_blur.uniform_loc.time, time);
        glUniform1f(gl.temporal.no_motion_blur.uniform_loc.feedback_min, feedback_min);
        glUniform1f(gl.temporal.no_motion_blur.uniform_loc.feedback_max, feedback_max);
        glUniform1f(gl.temporal.no_motion_blur.uniform_loc.motion_scale, motion_scale);
    }

    glBindVertexArray(gl.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
}

void scale_hsv(GLuint color_tex, vec3_t hsv_scale) {
    GLint last_fbo;
    GLint last_viewport[4];
    GLint last_scissor_box[4];
    GLint last_draw_buffer;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &last_fbo);
    glGetIntegerv(GL_VIEWPORT, last_viewport);
    glGetIntegerv(GL_SCISSOR_BOX, last_scissor_box);
    glGetIntegerv(GL_DRAW_BUFFER, &last_draw_buffer);

    GLint w, h;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, color_tex);

    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH,  &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);

    glBindVertexArray(gl.vao);

    glViewport(0, 0, w, h);
    glScissor(0, 0, w, h);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl.tmp.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl.tmp.tex_rgba8, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, color_tex);

    glUseProgram(hsv::gl.program);
    glUniform1i(hsv::gl.uniform_loc.texture_color, 0);
    glUniform3fv(hsv::gl.uniform_loc.hsv_scale, 1, &hsv_scale.x);
    glBindVertexArray(gl.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, color_tex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT1);
    blit_texture(gl.tmp.tex_rgba8);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, last_fbo);
    glViewport(last_viewport[0], last_viewport[1], last_viewport[2], last_viewport[3]);
    glScissor(last_scissor_box[0], last_scissor_box[1], last_scissor_box[2], last_scissor_box[3]);
    glDrawBuffer(last_draw_buffer);
}

void blit_texture(GLuint tex) {
    ASSERT(glIsTexture(tex));
    glUseProgram(blit::program_tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(blit::uniform_loc_texture, 0);
    glBindVertexArray(gl.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
}

void blit_color(vec4_t color) {
    glUseProgram(blit::program_col);
    glUniform4fv(blit::uniform_loc_color, 1, &color.x);
    glBindVertexArray(gl.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
}

void blur_texture_gaussian(GLuint tex, int num_passes) {
    ASSERT(glIsTexture(tex));
    ASSERT(num_passes > 0);

    GLint last_fbo;
    GLint last_viewport[4];
    GLint last_draw_buffer[8];
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &last_fbo);
    glGetIntegerv(GL_VIEWPORT, last_viewport);
    for (int i = 0; i < 8; ++i) glGetIntegerv(GL_DRAW_BUFFER0 + i, &last_draw_buffer[i]);

    GLint w, h;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);

    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);

    glBindVertexArray(gl.vao);

    glUseProgram(blur::program_gaussian);
    glUniform1i(blur::uniform_loc_texture, 0);

    glViewport(0, 0, w, h);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl.tmp.fbo);

    for (int i = 0; i < num_passes; ++i) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl.tmp.tex_rgba8, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glUniform2f(blur::uniform_loc_inv_res_dir, 1.0f / w, 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glBindTexture(GL_TEXTURE_2D, gl.tmp.tex_rgba8);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glUniform2f(blur::uniform_loc_inv_res_dir, 0.0f, 1.0f / h);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    glUseProgram(0);
    glBindVertexArray(0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, last_fbo);
    glViewport(last_viewport[0], last_viewport[1], last_viewport[2], last_viewport[3]);
    for (int i = 0; i < 8; ++i) glDrawBuffers(8, (GLenum*)last_draw_buffer);
}

void blur_texture_box(GLuint tex, int num_passes) {
    ASSERT(glIsTexture(tex));
    ASSERT(num_passes > 0);

    GLint last_fbo;
    GLint last_viewport[4];
    GLint last_draw_buffer[8];
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &last_fbo);
    glGetIntegerv(GL_VIEWPORT, last_viewport);
    for (int i = 0; i < 8; ++i) glGetIntegerv(GL_DRAW_BUFFER0 + i, &last_draw_buffer[i]);

    GLint w, h;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);

    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);

    glBindVertexArray(gl.vao);

    glUseProgram(blur::program_box);

    glViewport(0, 0, w, h);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl.tmp.fbo);

    for (int i = 0; i < num_passes; ++i) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl.tmp.tex_rgba8, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glBindTexture(GL_TEXTURE_2D, gl.tmp.tex_rgba8);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    glUseProgram(0);
    glBindVertexArray(0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, last_fbo);
    glViewport(last_viewport[0], last_viewport[1], last_viewport[2], last_viewport[3]);
    for (int i = 0; i < 8; ++i) glDrawBuffers(8, (GLenum*)last_draw_buffer);
}

static void compute_luma(GLuint tex) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);

    glBindVertexArray(gl.vao);
    glUseProgram(gl.luma.program);
    glUniform1i(gl.luma.uniform_loc.tex_rgba, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glUseProgram(0);
    glBindVertexArray(0);
}

static void compute_fxaa(GLuint tex_rgbl, int width, int height) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_rgbl);

    int w, h;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);

    float scl_x = (float)width  / (float)w;
    float scl_y = (float)height / (float)h;

    glBindVertexArray(gl.vao);
    glUseProgram(gl.fxaa.program);
    glUniform2f(gl.fxaa.uniform_loc.tc_scl, scl_x, scl_y);
    glUniform1i(gl.fxaa.uniform_loc.tex_rgbl, 0);
    glUniform2f(gl.fxaa.uniform_loc.rcp_res, 1.0f / (float)w, 1.0f / (float)h);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glUseProgram(0);
    glBindVertexArray(0);
}

void shade_and_postprocess(const Descriptor& desc, const ViewParam& view_param) {
    ASSERT(glIsTexture(desc.input_textures.depth));
    ASSERT(glIsTexture(desc.input_textures.color));
    ASSERT(glIsTexture(desc.input_textures.normal));
    if (desc.temporal_aa.enabled) {
        ASSERT(glIsTexture(desc.input_textures.velocity));
    }

    // For seeding noise
    static float time = 0.f;
    time = time + 0.01f;
    if (time > 100.f) time -= 100.f;
    //static unsigned int frame = 0;
    //frame = frame + 1;

    const auto near_dist = view_param.clip_planes.near;
    const auto far_dist = view_param.clip_planes.far;
    const auto ortho = is_orthographic_proj_matrix(view_param.matrix.curr.proj);

    GLint last_fbo;
    GLint last_viewport[4];
    GLint last_draw_buffer;
    GLint last_scissor_box[4];
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &last_fbo);
    glGetIntegerv(GL_VIEWPORT, last_viewport);
    glGetIntegerv(GL_SCISSOR_BOX, last_scissor_box);
    glGetIntegerv(GL_DRAW_BUFFER, &last_draw_buffer);

    int width = last_viewport[2];
    int height = last_viewport[3];

    if (width > (int)gl.tex_width || height > (int)gl.tex_height) {
        initialize(width, height);
    }

    //glViewport(0, 0, gl.tex_width, gl.tex_height);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, width, height);
    glViewport(0, 0, width, height);
    glBindVertexArray(gl.vao);

    PUSH_GPU_SECTION("Linearize Depth") {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl.linear_depth.fbo);
        glClearColor(far_dist,0,0,0);
        glClear(GL_COLOR_BUFFER_BIT);
        compute_linear_depth(desc.input_textures.depth, near_dist, far_dist, ortho);
    }
    POP_GPU_SECTION()

    if (desc.ambient_occlusion.enabled) {
        PUSH_GPU_SECTION("Generate Linear Depth Mipmaps") {
            glBindTexture(GL_TEXTURE_2D, gl.linear_depth.texture);
            glGenerateMipmap(GL_TEXTURE_2D);
        }
        POP_GPU_SECTION()
    }

    if (desc.temporal_aa.enabled) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl.velocity.fbo);
        glViewport(0, 0, gl.velocity.tex_width, gl.velocity.tex_height);
        glScissor(0, 0, gl.velocity.tex_width, gl.velocity.tex_height);

        PUSH_GPU_SECTION("Velocity: Tilemax") {
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            blit_tilemax(desc.input_textures.velocity, gl.tex_width, gl.tex_height);
        }
        POP_GPU_SECTION()

        PUSH_GPU_SECTION("Velocity: Neighbormax") {
            glDrawBuffer(GL_COLOR_ATTACHMENT1);
            blit_neighbormax(gl.velocity.tex_tilemax, gl.velocity.tex_width, gl.velocity.tex_height);
        }
        POP_GPU_SECTION()
    }

    const GLenum draw_buffers[2] = {
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1,
    };
     
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl.targets.fbo);
    glViewport(0, 0, width, height);
    glScissor(0, 0, width, height);
    glDrawBuffers(2, draw_buffers);
    glClearColor(0,0,0,0);
    glClear(GL_COLOR_BUFFER_BIT);

    GLenum dst_buffer = GL_COLOR_ATTACHMENT1;
    GLuint src_texture = gl.targets.tex_color[0];

    auto swap_target = [&dst_buffer, &src_texture]() {
        dst_buffer = dst_buffer == GL_COLOR_ATTACHMENT0 ? GL_COLOR_ATTACHMENT1 : GL_COLOR_ATTACHMENT0;
        src_texture = src_texture == gl.targets.tex_color[0] ? gl.targets.tex_color[1] : gl.targets.tex_color[0];
    };
    glDrawBuffer(dst_buffer);

    PUSH_GPU_SECTION("Compose")
    compose_deferred(desc.input_textures.depth, desc.input_textures.color, desc.input_textures.normal, view_param.matrix.inv.proj, desc.background.color, time);
    POP_GPU_SECTION()

    if (desc.ambient_occlusion.enabled) {
        PUSH_GPU_SECTION("SSAO")
        compute_ssao(gl.linear_depth.texture, desc.input_textures.normal, view_param.matrix.curr.proj, desc.ambient_occlusion.intensity, desc.ambient_occlusion.radius, desc.ambient_occlusion.bias);
        POP_GPU_SECTION()
    }

    PUSH_GPU_SECTION("Tonemapping")
    swap_target();
    glDrawBuffer(dst_buffer);
    Tonemapping tonemapper = desc.tonemapping.enabled ? desc.tonemapping.mode : Tonemapping_Passthrough;
    apply_tonemapping(src_texture, tonemapper, desc.tonemapping.exposure, desc.tonemapping.gamma);
    POP_GPU_SECTION()

    if (desc.depth_of_field.enabled) {
        swap_target();
        glDrawBuffer(dst_buffer);
        PUSH_GPU_SECTION("DOF")
        apply_dof(gl.linear_depth.texture, src_texture, desc.depth_of_field.focus_depth, desc.depth_of_field.focus_scale, time);
        POP_GPU_SECTION()
    }

    if (desc.input_textures.transparency) {
        PUSH_GPU_SECTION("Add Transparency")
            glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        blit_texture(desc.input_textures.transparency);
        glDisable(GL_BLEND);
        POP_GPU_SECTION()
    }

    if (desc.fxaa.enabled) {
        swap_target();
        PUSH_GPU_SECTION("Luma")
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl.tmp.fbo);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        compute_luma(src_texture);
        POP_GPU_SECTION()
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl.targets.fbo);
        glDrawBuffer(dst_buffer);
        PUSH_GPU_SECTION("FXAA")
        compute_fxaa(gl.tmp.tex_rgba8, width, height);
        POP_GPU_SECTION()
    }

    if (desc.temporal_aa.enabled) {
        swap_target();
        glDrawBuffer(dst_buffer);
        const float feedback_min = desc.temporal_aa.feedback_min;
        const float feedback_max = desc.temporal_aa.feedback_max;
        const float motion_scale = desc.temporal_aa.motion_blur.enabled ? desc.temporal_aa.motion_blur.motion_scale : 0.f;
        if (motion_scale != 0.f)
            PUSH_GPU_SECTION("Temporal AA + Motion Blur")
        else
            PUSH_GPU_SECTION("Temporal AA")

        apply_temporal_aa(gl.linear_depth.texture, src_texture, desc.input_textures.velocity, gl.velocity.tex_neighbormax, view_param.jitter.curr, view_param.jitter.prev, feedback_min, feedback_max, motion_scale, time);
        POP_GPU_SECTION()
    }
     
    if (desc.sharpen.enabled) {
        PUSH_GPU_SECTION("Sharpen")
        swap_target();
        glDrawBuffer(dst_buffer);
        sharpen::sharpen(src_texture, desc.sharpen.weight);
        POP_GPU_SECTION()
    }

    // Activate backbuffer or whatever was bound before
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, last_fbo);
    glDrawBuffer(last_draw_buffer);
    glViewport(last_viewport[0], last_viewport[1], last_viewport[2], last_viewport[3]);
    glScissor(last_scissor_box[0], last_scissor_box[1], last_scissor_box[2], last_scissor_box[3]);
    glDisable(GL_SCISSOR_TEST);

    swap_target();
    glDepthMask(0);
    blit_texture(src_texture);

    glDepthMask(1);
    glColorMask(1, 1, 1, 1);
}

}  // namespace postprocessing

// #gbuffer
void init_gbuffer(GBuffer* gbuf, int width, int height) {
    ASSERT(gbuf);

    bool attach_textures_deferred = false;
    if (!gbuf->fbo) {
        glGenFramebuffers(1, &gbuf->fbo);
        attach_textures_deferred = true;
    }

    if (!gbuf->tex.depth) glGenTextures(1, &gbuf->tex.depth);
    if (!gbuf->tex.color) glGenTextures(1, &gbuf->tex.color);
    if (!gbuf->tex.normal) glGenTextures(1, &gbuf->tex.normal);
    if (!gbuf->tex.velocity) glGenTextures(1, &gbuf->tex.velocity);
    if (!gbuf->tex.transparency) glGenTextures(1, &gbuf->tex.transparency);
    if (!gbuf->tex.picking) glGenTextures(1, &gbuf->tex.picking);
    if (!gbuf->pbo_picking.color[0]) glGenBuffers((int)ARRAY_SIZE(gbuf->pbo_picking.color), gbuf->pbo_picking.color);
    if (!gbuf->pbo_picking.depth[0]) glGenBuffers((int)ARRAY_SIZE(gbuf->pbo_picking.depth), gbuf->pbo_picking.depth);

    glBindTexture(GL_TEXTURE_2D, gbuf->tex.depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, gbuf->tex.color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, gbuf->tex.normal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, width, height, 0, GL_RG, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, gbuf->tex.velocity);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, width, height, 0, GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, gbuf->tex.transparency);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, gbuf->tex.picking);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /*
    glBindTexture(GL_TEXTURE_2D, gbuf->tex.temporal_accumulation[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, gbuf->tex.temporal_accumulation[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    */

    for (uint32_t i = 0; i < ARRAY_SIZE(gbuf->pbo_picking.color); ++i) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, gbuf->pbo_picking.color[i]);
        glBufferData(GL_PIXEL_PACK_BUFFER, 4, NULL, GL_DYNAMIC_READ);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }

    for (uint32_t i = 0; i < ARRAY_SIZE(gbuf->pbo_picking.depth); ++i) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, gbuf->pbo_picking.depth[i]);
        glBufferData(GL_PIXEL_PACK_BUFFER, 4, NULL, GL_DYNAMIC_READ);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    gbuf->width = width;
    gbuf->height = height;

    const GLenum draw_buffers[] = {GL_COLOR_ATTACHMENT_COLOR, GL_COLOR_ATTACHMENT_NORMAL, GL_COLOR_ATTACHMENT_VELOCITY,
        GL_COLOR_ATTACHMENT_TRANSPARENCY, GL_COLOR_ATTACHMENT_PICKING};

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gbuf->fbo);
    if (attach_textures_deferred) {
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gbuf->tex.depth, 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, gbuf->tex.depth, 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT_COLOR, GL_TEXTURE_2D, gbuf->tex.color, 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT_NORMAL, GL_TEXTURE_2D, gbuf->tex.normal, 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT_VELOCITY, GL_TEXTURE_2D, gbuf->tex.velocity, 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT_TRANSPARENCY, GL_TEXTURE_2D, gbuf->tex.transparency, 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT_PICKING, GL_TEXTURE_2D, gbuf->tex.picking, 0);
    }
    ASSERT(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glDrawBuffers((int)ARRAY_SIZE(draw_buffers), draw_buffers);
    glClearColor(0, 0, 0, 0);
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

void clear_gbuffer(GBuffer* gbuffer) {
    const GLenum draw_buffers[] = {GL_COLOR_ATTACHMENT_COLOR, GL_COLOR_ATTACHMENT_NORMAL, GL_COLOR_ATTACHMENT_VELOCITY, GL_COLOR_ATTACHMENT_PICKING, GL_COLOR_ATTACHMENT_TRANSPARENCY};

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gbuffer->fbo);
    glViewport(0, 0, gbuffer->width, gbuffer->height);

    glDepthMask(1);
    glColorMask(1, 1, 1, 1);
    glStencilMask(0xFF);
    const vec4_t zero    = {0,0,0,0};
    const vec4_t picking = {1,1,1,1};

    // Setup gbuffer and clear textures
    PUSH_GPU_SECTION("Clear G-buffer") {
        // Clear color+alpha, normal, velocity, emissive, post_tonemap and depth+stencil
        glDrawBuffers((int)ARRAY_SIZE(draw_buffers), draw_buffers);
        glClearBufferfv(GL_COLOR, 0, zero.elem);
        glClearBufferfv(GL_COLOR, 1, zero.elem);
        glClearBufferfv(GL_COLOR, 2, zero.elem);
        glClearBufferfv(GL_COLOR, 3, picking.elem);
        glClearBufferfv(GL_COLOR, 4, zero.elem);
        glClearBufferfi(GL_DEPTH_STENCIL, 0, 1.0f, 0x01);
    }
    POP_GPU_SECTION()
}

void destroy_gbuffer(GBuffer* gbuf) {
    ASSERT(gbuf);
    if (gbuf->fbo) glDeleteFramebuffers(1, &gbuf->fbo);
    if (gbuf->tex.depth) glDeleteTextures(1, &gbuf->tex.depth);
    if (gbuf->tex.color) glDeleteTextures(1, &gbuf->tex.color);
    if (gbuf->tex.normal) glDeleteTextures(1, &gbuf->tex.normal);
    if (gbuf->tex.transparency) glDeleteTextures(1, &gbuf->tex.transparency);
    if (gbuf->tex.picking) glDeleteTextures(1, &gbuf->tex.picking);
    if (gbuf->tex.temporal_accumulation) glDeleteTextures((int)ARRAY_SIZE(gbuf->tex.temporal_accumulation), gbuf->tex.temporal_accumulation);

    if (gbuf->pbo_picking.color[0]) glDeleteBuffers((int)ARRAY_SIZE(gbuf->pbo_picking.color), gbuf->pbo_picking.color);
    if (gbuf->pbo_picking.depth[0]) glDeleteBuffers((int)ARRAY_SIZE(gbuf->pbo_picking.depth), gbuf->pbo_picking.depth);
}

// #picking
void extract_picking_data(uint32_t* out_idx, float* out_depth, GBuffer* gbuf, int x, int y) {
    uint32_t idx = 0;
    float depth = 0;

#if EXPERIMENTAL_GFX_API
    if (use_gfx) {
        idx = md_gfx_get_picking_idx();
        depth = md_gfx_get_picking_depth();
        md_gfx_query_picking((uint32_t)x, (uint32_t)y);
    }
    else {
#endif
        ASSERT(gbuf);
        uint32_t N = (uint32_t)ARRAY_SIZE(gbuf->pbo_picking.color);
        uint32_t frame = gbuf->pbo_picking.frame++;
        uint32_t queue = (frame) % N;
        uint32_t read  = (frame + N-1) % N;

        uint8_t  color[4];

        PUSH_GPU_SECTION("READ PICKING DATA")
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gbuf->fbo);
        glReadBuffer(GL_COLOR_ATTACHMENT_PICKING);

        // Queue async reads from current frame to pixel pack buffer
        glBindBuffer(GL_PIXEL_PACK_BUFFER, gbuf->pbo_picking.color[queue]);
        glReadPixels(x, y, 1, 1, GL_BGRA, GL_UNSIGNED_BYTE, 0);

        glBindBuffer(GL_PIXEL_PACK_BUFFER, gbuf->pbo_picking.depth[queue]);
        glReadPixels(x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, 0);

        // Read values from previous frames pixel pack buffer
        glBindBuffer(GL_PIXEL_PACK_BUFFER, gbuf->pbo_picking.color[read]);
        glGetBufferSubData(GL_PIXEL_PACK_BUFFER, 0, sizeof(color), color);

        glBindBuffer(GL_PIXEL_PACK_BUFFER, gbuf->pbo_picking.depth[read]);
        glGetBufferSubData(GL_PIXEL_PACK_BUFFER, 0, sizeof(depth), &depth);

        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        POP_GPU_SECTION()

        // BGRA
        idx = (color[0] << 16) | (color[1] << 8) | (color[2] << 0) | (color[3] << 24);

#if EXPERIMENTAL_GFX_API
    }
#endif

    if (out_idx) {
        *out_idx = idx;
    }
    if (out_depth) {
        *out_depth = depth;
    }
}
