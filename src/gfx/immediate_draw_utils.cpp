#include "immediate_draw_utils.h"
#include <core/types.h>
#include <core/array.h>
#include <core/gl.h>
#include <core/log.h>
#include <gfx/gl_utils.h>

#include <imgui.h>

namespace immediate {

struct Vertex {
    vec3 position;
    uint32 color;
};

using Index = uint16;

struct DrawCommand {
    Index offset;
    Index count;
    GLenum primitive_type;
    GLuint program;
    int view_matrix_idx = -1;
    int proj_matrix_idx = -1;
};

static DynamicArray<mat4> matrix_stack;

static DynamicArray<DrawCommand> commands;
static DynamicArray<Vertex> vertices;
static DynamicArray<Index> indices;

static GLuint vbo = 0;
static GLuint ibo = 0;
static GLuint vao = 0;

static GLuint v_shader = 0;
static GLuint f_shader = 0;
static GLuint program = 0;

static GLint attrib_loc_pos = -1;
static GLint attrib_loc_col = -1;
static GLint uniform_loc_mvp = -1;

static int curr_view_matrix_idx = -1;
static int curr_proj_matrix_idx = -1;

static const char* v_shader_src = R"(
#version 150 core
uniform mat4 u_mvp;

in vec3 in_pos;
in vec4 in_col;

out vec4 col;

void main() {
	gl_Position = u_mvp * vec4(in_pos, 1);
    gl_PointSize = max(2.f, 400.f / gl_Position.w);
	col = in_col;
}
)";

static const char* f_shader_src = R"(
#version 150 core

in vec4 col;

out vec4 out_frag;

void main() {
	out_frag = col;
}
)";

static inline void append_draw_command(Index offset, Index count, GLenum primitive_type) {
    if (commands.count > 0 && commands.back().primitive_type == primitive_type) {
        commands.back().count += count;
    } else {
        ASSERT(curr_view_matrix_idx > -1, "Immediate Mode View Matrix not set!");
        ASSERT(curr_proj_matrix_idx > -1, "Immediate Mode Proj Matrix not set!");

        DrawCommand cmd{offset, count, primitive_type, program, curr_view_matrix_idx, curr_proj_matrix_idx};
        commands.push_back(cmd);
    }
}

void initialize() {
    constexpr int BUFFER_SIZE = 1024;
    char buffer[BUFFER_SIZE];

    v_shader = glCreateShader(GL_VERTEX_SHADER);
    f_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(v_shader, 1, &v_shader_src, 0);
    glShaderSource(f_shader, 1, &f_shader_src, 0);
    glCompileShader(v_shader);
    if (gl::get_shader_compile_error(buffer, BUFFER_SIZE, v_shader)) {
        LOG_ERROR("Error while compiling immediate vertex shader:\n%s\n", buffer);
    }
    glCompileShader(f_shader);
    if (gl::get_shader_compile_error(buffer, BUFFER_SIZE, f_shader)) {
        LOG_ERROR("Error while compiling immediate fragment shader:\n%s\n", buffer);
    }

    program = glCreateProgram();
    glAttachShader(program, v_shader);
    glAttachShader(program, f_shader);
    glLinkProgram(program);
    if (gl::get_program_link_error(buffer, BUFFER_SIZE, program)) {
        LOG_ERROR("Error while linking immediate program:\n%s\n", buffer);
    }

    glDetachShader(program, v_shader);
    glDetachShader(program, f_shader);

    glDeleteShader(v_shader);
    glDeleteShader(f_shader);

    attrib_loc_pos = glGetAttribLocation(program, "in_pos");
    attrib_loc_col = glGetAttribLocation(program, "in_col");
    uniform_loc_mvp = glGetUniformLocation(program, "u_mvp");

    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ibo);

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    if (attrib_loc_pos != -1) {
        glEnableVertexAttribArray(attrib_loc_pos);
        glVertexAttribPointer(attrib_loc_pos, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const GLvoid*)0);
    }

    if (attrib_loc_col != -1) {
        glEnableVertexAttribArray(attrib_loc_col);
        glVertexAttribPointer(attrib_loc_col, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (const GLvoid*)12);
    }

    glBindVertexArray(0);

    vertices.reserve(100000);
    indices.reserve(100000);
}

void shutdown() {
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ibo) glDeleteBuffers(1, &ibo);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (program) glDeleteProgram(program);
}

void set_view_matrix(const mat4& model_view_matrix) {
    curr_view_matrix_idx = (int)matrix_stack.count;
    matrix_stack.push_back(model_view_matrix);
}

void set_proj_matrix(const mat4& proj_matrix) {
    curr_proj_matrix_idx = (int)matrix_stack.count;
    matrix_stack.push_back(proj_matrix);
}

void flush() {
    /*
ImGui::SetNextWindowPos(ImVec2(0, 0));
ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
ImGui::SetNextWindowBgAlpha(0);
ImGui::Begin("##ImmediateFull", nullptr,
             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);

ImDrawList* dl = ImGui::GetWindowDrawList();

dl->AddCircle(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), 200.f, ImColor(1, 0, 0), 36, 2.f);

ImGui::End();
    */

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.count * sizeof(Vertex), vertices.data, GL_STREAM_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.count * sizeof(Index), indices.data, GL_STREAM_DRAW);

    glEnable(GL_PROGRAM_POINT_SIZE);

    glUseProgram(program);
    glPointSize(10.f);
    glLineWidth(2.f);

    for (const auto& cmd : commands) {
        mat4 mvp_matrix = matrix_stack[cmd.proj_matrix_idx] * matrix_stack[cmd.view_matrix_idx];
        glUniformMatrix4fv(uniform_loc_mvp, 1, GL_FALSE, &mvp_matrix[0][0]);

        glDrawElements(cmd.primitive_type, cmd.count, sizeof(Index) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(cmd.offset * sizeof(Index)));
    }

    glBindVertexArray(0);
    glUseProgram(0);

    glDisable(GL_PROGRAM_POINT_SIZE);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    vertices.clear();
    indices.clear();
    commands.clear();
    matrix_stack.clear();
    curr_view_matrix_idx = -1;
    curr_proj_matrix_idx = -1;
}

// PRIMITIVES
void draw_point(const float pos[3], const uint32 color) {
    const Index idx = (Index)vertices.count;
    const Vertex v = {{pos[0], pos[1], pos[2]}, color};

    vertices.push_back(v);
    indices.push_back(idx);

    append_draw_command(idx, 1, GL_POINTS);
}

void draw_line(const float from[3], const float to[3], const uint32 color) {
    const Index idx = (Index)vertices.count;
    const Vertex v[2] = {{{from[0], from[1], from[2]}, color}, {{to[0], to[1], to[2]}, color}};

    vertices.push_back(v[0]);
    vertices.push_back(v[1]);

    indices.push_back(idx);
    indices.push_back(idx + 1);

    append_draw_command(idx, 2, GL_LINES);
}

void draw_triangle(const float p0[3], const float p1[3], const float p2[3], const uint32 color) {
    const Index idx = (Index)vertices.count;
    const Vertex v[3] = {{{p0[0], p0[1], p0[2]}, color}, {{p1[0], p1[1], p1[2]}, color}, {{p2[0], p2[1], p2[2]}, color}};

    vertices.push_back(v[0]);
    vertices.push_back(v[1]);
    vertices.push_back(v[2]);

    indices.push_back(idx);
    indices.push_back(idx + 1);
    indices.push_back(idx + 2);

    append_draw_command(idx, 3, GL_TRIANGLES);
}

void draw_aabb(const float min_box[3], const float max_box[3], const uint32 color) {
    // Z = min
    draw_line(vec3(min_box[0], min_box[1], min_box[2]), vec3(max_box[0], min_box[1], min_box[2]), color);
    draw_line(vec3(min_box[0], min_box[1], min_box[2]), vec3(min_box[0], max_box[1], min_box[2]), color);
    draw_line(vec3(max_box[0], min_box[1], min_box[2]), vec3(max_box[0], max_box[1], min_box[2]), color);
    draw_line(vec3(min_box[0], max_box[1], min_box[2]), vec3(max_box[0], max_box[1], min_box[2]), color);

    // Z = max
    draw_line(vec3(min_box[0], min_box[1], max_box[2]), vec3(max_box[0], min_box[1], max_box[2]), color);
    draw_line(vec3(min_box[0], min_box[1], max_box[2]), vec3(min_box[0], max_box[1], max_box[2]), color);
    draw_line(vec3(max_box[0], min_box[1], max_box[2]), vec3(max_box[0], max_box[1], max_box[2]), color);
    draw_line(vec3(min_box[0], max_box[1], max_box[2]), vec3(max_box[0], max_box[1], max_box[2]), color);

    // Z min to Z max
    draw_line(vec3(min_box[0], min_box[1], min_box[2]), vec3(min_box[0], min_box[1], max_box[2]), color);
    draw_line(vec3(min_box[0], max_box[1], min_box[2]), vec3(min_box[0], max_box[1], max_box[2]), color);
    draw_line(vec3(max_box[0], min_box[1], min_box[2]), vec3(max_box[0], min_box[1], max_box[2]), color);
    draw_line(vec3(max_box[0], max_box[1], min_box[2]), vec3(max_box[0], max_box[1], max_box[2]), color);
}

}  // namespace immediate
