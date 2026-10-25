#pragma once

#include "gl.h"

#include <cstdint>

#define GL_COLOR_ATTACHMENT_COLOR        GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT_NORMAL       GL_COLOR_ATTACHMENT1
#define GL_COLOR_ATTACHMENT_VELOCITY     GL_COLOR_ATTACHMENT2
#define GL_COLOR_ATTACHMENT_PICKING      GL_COLOR_ATTACHMENT3
#define GL_COLOR_ATTACHMENT_TRANSPARENCY GL_COLOR_ATTACHMENT4

enum class GBufferFlags : uint32_t {
    None = 0,
    Depth = 1 << 0,
    Color = 1 << 1,
    Normal = 1 << 2,
    Velocity = 1 << 3,
    Picking = 1 << 4,
    Transparency = 1 << 5,
    History = 1 << 6,
};

struct MarksBuffer {
    uint32_t fbo = 0;
    // RGBA8: selection visible/all, highlight visible/all. R8: highlight group, as the native marks shader encodes it.
    uint32_t texture = 0;
    uint32_t group = 0;
    uint32_t width = 0, height = 0;
};

struct GBuffer {
    MarksBuffer marks;
    MarksBuffer contours;
    struct {
        uint32_t depth = 0;
        uint32_t color = 0;
        uint32_t normal = 0;
        uint32_t velocity = 0;
        uint32_t picking = 0;
        uint32_t transparency = 0;
        uint32_t transparency_hdr = 0;  // RGBA16F, premultiplied linear radiance blended over the scene before tone mapping (isosurfaces). Not attached to fbo.
        uint32_t history = 0;       // TAA history written this frame, at the output size
        uint32_t history_prev = 0;  // TAA history of the previous frame; swap the two after post-processing
        uint32_t image = 0;         // RGB10_A2 at the output size, of fbo_image: the image made of the G-buffer, before it is presented
    } tex;

    uint32_t fbo = 0;
    uint32_t fbo_image = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t out_width = 0;         // what post-processing makes of it: the size of the image shown, and of the history
    uint32_t out_height = 0;
};

// out_width / out_height: the size of the image post-processing makes, when it is upscaled from the G-buffer's;
// 0: the G-buffer's own
void gbuffer_init(GBuffer* gbuf, int width, int height, int out_width = 0, int out_height = 0);
void gbuffer_clear(GBuffer* gbuf);
void gbuffer_free(GBuffer* gbuf);

// The mark pass: begin binds the marks targets (cleared) at their size, blending by max, with neither depth
// test nor depth write nor scissor test, and the G-buffer's depth to md_gl's first user texture unit, where the marks
// snippet samples it (MD_GL_USER_TEXTURE_UNIT). end undoes the blending and the binding and turns the depth test back
// on; the framebuffer and viewport are left for the caller to set.
void gbuffer_marks_begin(GBuffer* gbuf);
void gbuffer_marks_end(GBuffer* gbuf);
// Independent annotation layers use the same mask format and rendering state as selection.
void gbuffer_marks_init(MarksBuffer* marks, int width, int height);
void gbuffer_marks_free(MarksBuffer* marks);
void gbuffer_marks_begin(GBuffer* gbuf, const MarksBuffer& marks);
