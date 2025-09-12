#version 450

// Vulkan entry/exit point vertex shader
layout(location = 0) out vec3 world_pos;
layout(location = 1) out vec2 tex_coord;

// Uniform buffer for matrices
layout(binding = 0, std140) uniform UniformData {
    mat4 u_view_to_model_mat;
    mat4 u_model_to_view_mat;
    mat4 u_inv_proj_mat;
    mat4 u_model_view_proj_mat;
    vec2 u_inv_res;
    float u_time;
    uint u_enable_depth;
    vec3 u_clip_plane_min;
    float u_tf_min;
    vec3 u_clip_plane_max;
    float u_tf_inv_ext;
    vec3 u_gradient_spacing_world_space;
    uint u_max_steps;
    mat4 u_gradient_spacing_tex_space;
    vec3 u_env_radiance;
    float u_roughness;
    vec3 u_dir_radiance;
    float u_F0;
    uint u_dvr_enabled;
    uint u_iso_enabled;
    uint u_temporal_enabled;
    uint u_padding;
};

// Generate cube vertices procedurally
void main() {
    // Generate cube vertices using vertex index
    // This approach creates a unit cube from -0.5 to 0.5
    uint vid = gl_VertexIndex;
    
    // Cube has 36 vertices (6 faces * 2 triangles * 3 vertices)
    // Define cube vertices for triangle strip or indexed rendering
    vec3 cube_vertices[8] = vec3[8](
        vec3(-0.5, -0.5, -0.5), // 0
        vec3( 0.5, -0.5, -0.5), // 1
        vec3( 0.5,  0.5, -0.5), // 2
        vec3(-0.5,  0.5, -0.5), // 3
        vec3(-0.5, -0.5,  0.5), // 4
        vec3( 0.5, -0.5,  0.5), // 5
        vec3( 0.5,  0.5,  0.5), // 6
        vec3(-0.5,  0.5,  0.5)  // 7
    );

    // Cube triangle indices for 12 triangles (36 vertices)
    uint cube_indices[36] = uint[36](
        // Front face
        0, 1, 2,   2, 3, 0,
        // Back face
        4, 6, 5,   6, 4, 7,
        // Left face
        4, 0, 3,   3, 7, 4,
        // Right face
        1, 5, 6,   6, 2, 1,
        // Bottom face
        4, 5, 1,   1, 0, 4,
        // Top face
        3, 2, 6,   6, 7, 3
    );

    uint vertex_index = cube_indices[vid];
    vec3 vertex_pos = cube_vertices[vertex_index];
    
    // Transform to [0,1] range for texture coordinates
    world_pos = vertex_pos + 0.5;
    tex_coord = world_pos.xy;
    
    // Transform to clip space
    gl_Position = u_model_view_proj_mat * vec4(vertex_pos, 1.0);
}