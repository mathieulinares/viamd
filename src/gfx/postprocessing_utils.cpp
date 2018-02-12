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

// Shaders are taken from nVidias examples and are copyright protected as stated above

#include "postprocessing_utils.h"
#include <core/types.h>
#include <core/common.h>
#include <core/math_utils.h>
#include <gfx/gl_utils.h>
#include <stdio.h>

namespace postprocessing {

// This is for the fullscreen quad
// TODO: Check how much is needed to trigger drawcall?
GLuint vao;
GLuint vbo;
GLuint v_shader_fs_quad;

static const char* v_shader_src_fs_quad = R"(
#version 150 core

out vec2 tc;

void main() {
	uint idx = uint(gl_VertexID) % 3U;
	gl_Position = vec4(
		(float( idx     &1U)) * 4.0 - 1.0,
		(float((idx>>1U)&1U)) * 4.0 - 1.0,
		0, 1.0);
	tc = gl_Position.xy * 0.5 + 0.5;
}
)";

void setup_program(GLuint* program, const char* defines, const char* f_shader_src, const char* name) {
	ASSERT(program);
	constexpr int BUFFER_SIZE = 1024;
	char buffer[BUFFER_SIZE];

	const char* sources[2] = { defines, f_shader_src };
	auto f_shader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(f_shader, 2, sources, 0);
	glCompileShader(f_shader);
	if (gl::get_shader_compile_error(buffer, BUFFER_SIZE, f_shader)) {
		printf("Error while compiling %s shader:\n%s\n", name, buffer);
	}

	if (!*program) {
		*program = glCreateProgram();
	}
	else {
		// TODO: DETATCH ANY SHADERS?
	}

	glAttachShader(*program, v_shader_fs_quad);
	glAttachShader(*program, f_shader);
	glLinkProgram(*program);
	if (gl::get_program_link_error(buffer, BUFFER_SIZE, *program)) {
		printf("Error while linking %s program:\n%s\n", name, buffer);
	}

	glDetachShader(*program, v_shader_fs_quad);
	glDetachShader(*program, f_shader);
	glDeleteShader(f_shader);
}

static bool is_orthographic_proj_matrix(const mat4& proj_mat) { return math::length2(vec3(proj_mat[3])) > 0.f; }

namespace ssao {
#ifndef AO_RANDOM_TEX_SIZE
#define AO_RANDOM_TEX_SIZE 4
#endif

#ifndef AO_MAX_SAMPLES
#define AO_MAX_SAMPLES 1
#endif

#ifndef AO_DIRS
#define AO_DIRS 8
#endif

#ifndef AO_SAMPLES
#define AO_SAMPLES 4
#endif

#ifndef AO_BLUR
#define AO_BLUR 1
#endif

static GLuint fbo_linear_depth = 0;
static GLuint fbo_hbao = 0;

static GLuint tex_random = 0;
static GLuint tex_linear_depth = 0;
static GLuint tex_blur = 0;

static GLuint prog_linearize_depth = 0;
static GLuint prog_hbao = 0;
static GLuint prog_blur_vert = 0;
static GLuint prog_blur_horiz = 0;

static GLuint ubo_hbao_data = 0;
// static vec4 hbao_random[AO_RANDOM_TEX_SIZE * AO_RANDOM_TEX_SIZE * AO_MAX_SAMPLES];

struct HBAOData {
    float radius_to_screen;
    float r2;
    float neg_inv_r2;
    float n_dot_v_bias;

    vec2 inv_full_res;
    vec2 inv_quarter_res;

    float ao_multiplier;
    float pow_exponent;
    vec2 _pad0;

    vec4 proj_info;

    vec2 proj_scale;
    int proj_ortho;
    int _pad1;

    //  vec4    offsets[AO_RANDOM_TEX_SIZE * AO_RANDOM_TEX_SIZE];
    //  vec4    jitters[AO_RANDOM_TEX_SIZE * AO_RANDOM_TEX_SIZE];
};

static const char* f_shader_src_linearize_depth = R"(
#ifndef AO_PERSP
#define AO_PERSP 1
#endif

// z_n * z_f,  z_n - z_f,  z_f
uniform vec3 u_clip_info;
uniform sampler2D u_texture;

out vec4 out_frag;

void main() {
  float d = texelFetch(u_texture, ivec2(gl_FragCoord.xy), 0).x;
#ifdef AO_PERSP
  out_frag = vec4(u_clip_info[0] / (u_clip_info[1] * d + u_clip_info[2]));
#else
  out_frag = vec4(u_clip_info[1]+u_clip_info[2] - d * u_clip_info[1]);
#endif
}
)";

static const char* f_shader_src_hbao = R"(
#pragma optionNV(unroll all)

#ifndef AO_BLUR
#define AO_BLUR 0
#endif

#define M_PI 3.14159265

#ifndef AO_STEPS
#define AO_STEPS 4
#endif

#ifndef AO_DIRS
#define AO_DIRS 8
#endif

#ifndef AO_USE_NORMAL
#define AO_USE_NORMAL 1
#endif

#ifndef AO_RANDOM_TEX_SIZE
#define AO_RANDOM_TEX_SIZE 4
#endif

struct HBAOData {
  float   radius_to_screen;
  float   r2;
  float   neg_inv_r2;
  float   n_dot_v_bias;
 
  vec2    inv_full_res;
  vec2    inv_quarter_res;
  
  float   ao_multiplier;
  float   pow_exponent;
  vec2    _pad0;
  
  vec4    proj_info;

  vec2    proj_scale;
  int     proj_ortho;
  int     _pad1;
  
  //vec4    offsets[AO_RANDOM_TEX_SIZE*AO_RANDOM_TEX_SIZE];
  //vec4    jitters[AO_RANDOM_TEX_SIZE*AO_RANDOM_TEX_SIZE];
};

// tweakables
const float NUM_STEPS = AO_STEPS;
const float NUM_DIRECTIONS = AO_DIRS; // tex_random/jitter initialization depends on this

layout(std140) uniform u_control_buffer {
  HBAOData control;
};

uniform sampler2D tex_linear_depth;
uniform sampler2D tex_random;

in vec2 tc;
out vec4 out_frag;

void OutputColor(vec4 color) {
  out_frag = color;
}

//----------------------------------------------------------------------------------

vec3 UVToView(vec2 uv, float eye_z) {
  return vec3((uv * control.proj_info.xy + control.proj_info.zw) * (control.proj_ortho != 0 ? 1. : eye_z), eye_z);
}

vec3 FetchViewPos(vec2 UV) {
  float ViewDepth = textureLod(tex_linear_depth,UV,0).x;
  return UVToView(UV, ViewDepth);
}

vec3 MinDiff(vec3 P, vec3 Pr, vec3 Pl) {
  vec3 V1 = Pr - P;
  vec3 V2 = P - Pl;
  return (dot(V1,V1) < dot(V2,V2)) ? V1 : V2;
}

vec3 ReconstructNormal(vec2 UV, vec3 P) {
  vec3 Pr = FetchViewPos(UV + vec2(control.inv_full_res.x, 0));
  vec3 Pl = FetchViewPos(UV + vec2(-control.inv_full_res.x, 0));
  vec3 Pt = FetchViewPos(UV + vec2(0, control.inv_full_res.y));
  vec3 Pb = FetchViewPos(UV + vec2(0, -control.inv_full_res.y));
  return normalize(cross(MinDiff(P, Pr, Pl), MinDiff(P, Pt, Pb)));
}

//----------------------------------------------------------------------------------
float Falloff(float DistanceSquare) {
  // 1 scalar mad instruction
  return DistanceSquare * control.neg_inv_r2 + 1.0;
}

//----------------------------------------------------------------------------------
// P = view-space position at the kernel center
// N = view-space normal at the kernel center
// S = view-space position of the current sample
//----------------------------------------------------------------------------------
float ComputeAO(vec3 P, vec3 N, vec3 S) {
  vec3 V = S - P;
  float VdotV = dot(V, V);
  float NdotV = dot(N, V) * 1.0/sqrt(VdotV);

  // Use saturate(x) instead of max(x,0.f) because that is faster on Kepler
  return clamp(NdotV - control.n_dot_v_bias,0,1) * clamp(Falloff(VdotV),0,1);
}

//----------------------------------------------------------------------------------
vec2 RotateDirection(vec2 Dir, vec2 CosSin) {
  return vec2(Dir.x*CosSin.x - Dir.y*CosSin.y,
              Dir.x*CosSin.y + Dir.y*CosSin.x);
}

//----------------------------------------------------------------------------------
vec4 GetJitter() {
  // (cos(Alpha),sin(Alpha),rand1,rand2)
  return textureLod( tex_random, (gl_FragCoord.xy / AO_RANDOM_TEX_SIZE), 0);
}

//----------------------------------------------------------------------------------
float ComputeCoarseAO(vec2 FullResUV, float RadiusPixels, vec4 Rand, vec3 ViewPosition, vec3 ViewNormal) {
  // Divide by NUM_STEPS+1 so that the farthest samples are not fully attenuated
  float StepSizePixels = RadiusPixels / (NUM_STEPS + 1);

  const float Alpha = 2.0 * M_PI / NUM_DIRECTIONS;
  float AO = 0;

  for (float DirectionIndex = 0; DirectionIndex < NUM_DIRECTIONS; ++DirectionIndex)
  {
    float Angle = Alpha * DirectionIndex;

    // Compute normalized 2D direction
    vec2 Direction = RotateDirection(vec2(cos(Angle), sin(Angle)), Rand.xy);

    // Jitter starting sample within the first step
    float RayPixels = (Rand.z * StepSizePixels + 1.0);

    for (float StepIndex = 0; StepIndex < NUM_STEPS; ++StepIndex)
    {
      vec2 SnappedUV = round(RayPixels * Direction) * control.inv_full_res + FullResUV;
      vec3 S = FetchViewPos(SnappedUV);

      RayPixels += StepSizePixels;

      AO += ComputeAO(ViewPosition, ViewNormal, S);
    }
  }

  AO *= control.ao_multiplier / (NUM_DIRECTIONS * NUM_STEPS);

  return clamp(1.0 - AO, 0, 1);
}

//----------------------------------------------------------------------------------
void main() {
  vec2 uv = tc;
  vec3 ViewPosition = FetchViewPos(uv);

  // Reconstruct view-space normal from nearest neighbors
#if AO_USE_NORMAL
  vec3 ViewNormal = -ReconstructNormal(uv, ViewPosition);
#else
  vec3 ViewNormal = vec3(0,0,-1);
#endif

  // Compute projection of disk of radius control.R into screen space
  float RadiusPixels = control.radius_to_screen / (control.proj_ortho != 0 ? 1.0 : ViewPosition.z);

  // Get jitter vector for the current full-res pixel
  vec4 Rand = GetJitter();

  float AO = ComputeCoarseAO(uv, RadiusPixels, Rand, ViewPosition, ViewNormal);

#if AO_BLUR
  OutputColor(vec4(pow(AO, control.pow_exponent), ViewPosition.z, 0, 1));
#else
  OutputColor(vec4(vec3(pow(AO, control.pow_exponent)), 1));
#endif 
}
)";

static const char* f_shader_src_hbao_blur = R"(
const float KERNEL_RADIUS = 3;
  
uniform float u_sharpness;
uniform vec2  u_inv_res_dir; // either set x to 1/width or y to 1/height
uniform sampler2D u_texture;

in vec2 tc;
out vec4 out_frag;

#ifndef AO_BLUR_PRESENT
#define AO_BLUR_PRESENT 1
#endif

//-------------------------------------------------------------------------

float BlurFunction(vec2 uv, float r, float center_c, float center_d, inout float w_total)
{
  vec2  aoz = texture( u_texture, uv ).xy;
  float c = aoz.x;
  float d = aoz.y;
  
  const float BlurSigma = float(KERNEL_RADIUS) * 0.5;
  const float BlurFalloff = 1.0 / (2.0*BlurSigma*BlurSigma);
  
  float ddiff = (d - center_d) * u_sharpness;
  float w = exp2(-r*r*BlurFalloff - ddiff*ddiff);
  w_total += w;

  return c*w;
}

void main()
{
  vec2  aoz = texture( u_texture, tc ).xy;
  float center_c = aoz.x;
  float center_d = aoz.y;
  
  float c_total = center_c;
  float w_total = 1.0;
  
  for (float r = 1; r <= KERNEL_RADIUS; ++r)
  {
    vec2 uv = tc + u_inv_res_dir * r;
    c_total += BlurFunction(uv, r, center_c, center_d, w_total);  
  }
  
  for (float r = 1; r <= KERNEL_RADIUS; ++r)
  {
    vec2 uv = tc - u_inv_res_dir * r;
    c_total += BlurFunction(uv, r, center_c, center_d, w_total);  
  }
  
#if AO_BLUR_PRESENT
  out_frag = vec4(vec3(c_total/w_total), 1);
#else
  out_frag = vec4(c_total/w_total, center_d, 0, 1);
#endif
}
)";

void setup_ubo_hbao_data(GLuint ubo, int width, int height, const mat4& proj_mat, float fovy, bool persp_proj, float radius, float intensity,
                         float bias) {
    constexpr float METERS_TO_VIEWSPACE = 1.f;
    const float* proj_data = &proj_mat[0][0];

    vec4 proj_info;
    float proj_scl;
    if (persp_proj) {
        proj_info = vec4(2.0f / (proj_data[4 * 0 + 0]),                          // (x) * (R - L)/N
                         2.0f / (proj_data[4 * 1 + 1]),                          // (y) * (T - B)/N
                         -(1.0f - proj_data[4 * 2 + 0]) / proj_data[4 * 0 + 0],  // L/N
                         -(1.0f + proj_data[4 * 2 + 1]) / proj_data[4 * 1 + 1]   // B/N
        );
        proj_scl = float(height) / (math::tan(fovy * 0.5f) * 2.0f);
    } else {
        proj_info = vec4(2.0f / (proj_data[4 * 0 + 0]),                          // ((x) * R - L)
                         2.0f / (proj_data[4 * 1 + 1]),                          // ((y) * T - B)
                         -(1.0f + proj_data[4 * 3 + 0]) / proj_data[4 * 0 + 0],  // L
                         -(1.0f - proj_data[4 * 3 + 1]) / proj_data[4 * 1 + 1]   // B
        );
        proj_scl = float(height) / proj_info[1];
    }

    float r = radius * METERS_TO_VIEWSPACE;

    HBAOData d;
    d.radius_to_screen = r * 0.5f * proj_scl;
    d.r2 = r * r;
    d.neg_inv_r2 = -1.f / (r * r);
    d.n_dot_v_bias = math::clamp(bias, 0.f, 1.f);

    d.inv_full_res = vec2(1.f / float(width), 1.f / float(height));
    d.inv_quarter_res = vec2(1.f / float((width + 3) / 4), 1.f / float((height + 3) / 4));

    d.ao_multiplier = 1.f / (1.f - d.n_dot_v_bias);
    d.pow_exponent = math::max(intensity, 0.f);

    d.proj_info = proj_info;
    // @NOTE: Is one needed?
    d.proj_scale = vec2(proj_scl);
    d.proj_ortho = !persp_proj;
}

void initialize_rnd_tex(GLuint rnd_tex, int num_direction) {
    ASSERT(AO_MAX_SAMPLES == 1);
    constexpr int BUFFER_SIZE = AO_RANDOM_TEX_SIZE * AO_RANDOM_TEX_SIZE * AO_MAX_SAMPLES;
    signed short buffer[BUFFER_SIZE * 4];

    for (int i = 0; i < BUFFER_SIZE; i++) {
#define SCALE ((1 << 15))
        float rand1 = math::rnd();
        float rand2 = math::rnd();
        float angle = 2.f * math::PI * rand1 / (float)num_direction;

        buffer[i * 4 + 0] = (signed short)(SCALE * math::cos(angle));
        buffer[i * 4 + 1] = (signed short)(SCALE * math::sin(angle));
        buffer[i * 4 + 2] = (signed short)(SCALE * rand2);
        buffer[i * 4 + 3] = (signed short)(SCALE * 0);
#undef SCALE
    }

    // @TODO: If MSAA and AO_MAX_SAMPLES > 1, then this probably has to go into a texture array
    glBindTexture(GL_TEXTURE_2D, rnd_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16_SNORM, AO_RANDOM_TEX_SIZE, AO_RANDOM_TEX_SIZE, 0, GL_RGBA, GL_SHORT, buffer);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void initialize(int width, int height) {
    // TODO: dynamically create this
    const char* defines = R"(
        #version 150 core
        #define AO_RANDOM_TEX_SIZE 4
        #define AO_PERSP 1
        #define AO_BLUR 0
        #define AO_STEPS 4
        #define AO_DIRS 8
        #define AO_USE_NORMAL 1
    )";

    const char* define_horiz = R"(
        #version 150 core
        #define AO_BLUR_PRESENT 0
    )";

    const char* define_vert = R"(
        #version 150 core
        #define AO_BLUR_PRESENT 1
    )";

    setup_program(&prog_linearize_depth, defines, f_shader_src_linearize_depth, "linearize depth");
    setup_program(&prog_hbao, defines, f_shader_src_hbao, "hbao");
    setup_program(&prog_blur_horiz, define_horiz, f_shader_src_hbao_blur, "hbao horizontal blur");
    setup_program(&prog_blur_vert, define_vert, f_shader_src_hbao_blur, "hbao vertical blur");

    if (!fbo_linear_depth) glGenFramebuffers(1, &fbo_linear_depth);
    if (!fbo_hbao) glGenFramebuffers(1, &fbo_hbao);

    if (!tex_random) glGenTextures(1, &tex_random);
    if (!tex_linear_depth) glGenTextures(1, &tex_linear_depth);
    if (!tex_blur) glGenTextures(1, &tex_blur);

    initialize_rnd_tex(tex_random, AO_DIRS);

    glBindTexture(GL_TEXTURE_2D, tex_linear_depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, width, height, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, tex_blur);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, width, height, 0, GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_linear_depth);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_linear_depth, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_hbao);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_blur, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (!ubo_hbao_data) glGenBuffers(1, &ubo_hbao_data);
    glBindBuffer(GL_UNIFORM_BUFFER, ubo_hbao_data);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(HBAOData), nullptr, GL_DYNAMIC_DRAW);
}

void shutdown() {
    if (fbo_linear_depth) glDeleteFramebuffers(1, &fbo_linear_depth);
    if (fbo_hbao) glDeleteFramebuffers(1, &fbo_hbao);

    if (tex_random) glDeleteTextures(1, &tex_random);
    if (tex_linear_depth) glDeleteTextures(1, &tex_linear_depth);
    if (tex_blur) glDeleteTextures(1, &tex_blur);

    if (ubo_hbao_data) glDeleteBuffers(1, &ubo_hbao_data);

    if (prog_linearize_depth) glDeleteProgram(prog_linearize_depth);
    if (prog_hbao) glDeleteProgram(prog_hbao);
    if (prog_blur_horiz) glDeleteProgram(prog_blur_horiz);
    if (prog_blur_vert) glDeleteProgram(prog_blur_vert);
}

}  // namespace ssao

namespace tonemapping {
static GLuint prog_tonemap = 0;
static const char* f_shader_src_tonemap = R"(
#version 150 core

uniform sampler2D u_texture;
out vec4 out_frag;

vec3 reinhard(vec3 c) {
	return c / (c + 1.0);
}

void main() {
	vec4 color = texelFetch(u_texture, ivec2(gl_FragCoord.xy), 0);
	out_frag = vec4(reinhard(color.rgb), color.a);
}
)";

void initialize() {
	if (!prog_tonemap) setup_program(&prog_tonemap, nullptr, f_shader_src_tonemap, "tonemap");
}

void shutdown() {
	if (prog_tonemap) glDeleteProgram(prog_tonemap);
}

}  // namespace tonemapping

void initialize(int width, int height) {
    constexpr int BUFFER_SIZE = 1024;
    char buffer[BUFFER_SIZE];

    if (!vao) glGenVertexArrays(1, &vao);
    if (!vbo) glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (const GLvoid*)0);
    glBindVertexArray(0);

    if (!v_shader_fs_quad) {
        v_shader_fs_quad = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(v_shader_fs_quad, 1, &v_shader_src_fs_quad, 0);
        glCompileShader(v_shader_fs_quad);
        if (gl::get_shader_compile_error(buffer, BUFFER_SIZE, v_shader_fs_quad)) {
            printf("Error while compiling postprocessing fs-quad vertex shader:\n%s\n", buffer);
        }
    }

    ssao::initialize(width, height);
}

void shutdown() {
    ssao::shutdown();

    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (v_shader_fs_quad) glDeleteShader(v_shader_fs_quad);
}

void apply_ssao(GLuint depth_tex, const mat4& proj_matrix, float radius, float strength, float bias) {}

void apply_tonemapping(GLuint color_tex) {}

}  // namespace postprocessing