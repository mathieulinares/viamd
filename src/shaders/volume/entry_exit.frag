#version 450

// Vulkan entry/exit point fragment shader
layout(location = 0) in vec3 world_pos;
layout(location = 1) in vec2 tex_coord;

layout(location = 0) out vec4 entry_color;
layout(location = 1) out vec4 exit_color;

// Optional depth texture for depth testing
layout(binding = 5) uniform sampler2D u_tex_depth;

// Uniform buffer
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

vec4 depth_to_view_coord(vec2 tc, float depth) {
    vec4 clip_coord = vec4(vec3(tc, depth) * 2.0 - 1.0, 1.0);
    vec4 view_coord = u_inv_proj_mat * clip_coord;
    return view_coord / view_coord.w;
}

void main() {
    vec3 position = world_pos;
    
    // Check if we're within the clip volume
    if (any(lessThan(position, u_clip_plane_min)) || 
        any(greaterThan(position, u_clip_plane_max))) {
        discard;
    }

    // Depth testing if enabled
    if (u_enable_depth != 0u) {
        vec2 screen_tc = gl_FragCoord.xy * u_inv_res;
        float depth_sample = texture(u_tex_depth, screen_tc).r;
        
        if (depth_sample < 1.0) {
            vec4 depth_view = depth_to_view_coord(screen_tc, depth_sample);
            vec4 depth_model = u_view_to_model_mat * depth_view;
            
            // If the current fragment is behind the depth buffer, discard
            vec4 current_view = u_model_to_view_mat * vec4(position, 1.0);
            if (current_view.z > depth_view.z) {
                discard;
            }
        }
    }

    // Determine if this is a front face or back face
    // Front faces will be entry points, back faces will be exit points
    if (gl_FrontFacing) {
        // Entry point
        entry_color = vec4(position, 1.0);
        exit_color = vec4(0.0);
    } else {
        // Exit point
        entry_color = vec4(0.0);
        exit_color = vec4(position, 1.0);
    }
}