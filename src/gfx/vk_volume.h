#pragma once

#include <core/md_vec_math.h>
#include <core/md_str.h>
#include <gfx/vk_utils.h>
#include <vulkan/vulkan.h>

#include <memory>

// Forward declarations
struct VmaAllocator_T;
typedef VmaAllocator_T* VmaAllocator;

namespace volume {

// Vulkan-specific render descriptor
struct VulkanRenderDesc {
    struct {
        VkImage color = VK_NULL_HANDLE;
        VkImageView color_view = VK_NULL_HANDLE;
        VkImage depth = VK_NULL_HANDLE;
        VkImageView depth_view = VK_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
        bool clear_color = false;
    } render_target;

    struct {
        VkImage volume = VK_NULL_HANDLE;
        VkImageView volume_view = VK_NULL_HANDLE;
        VkSampler volume_sampler = VK_NULL_HANDLE;
        
        VkImage transfer_function = VK_NULL_HANDLE;
        VkImageView transfer_function_view = VK_NULL_HANDLE;
        VkSampler transfer_function_sampler = VK_NULL_HANDLE;
        
        VkImage entry = VK_NULL_HANDLE;
        VkImageView entry_view = VK_NULL_HANDLE;
        
        VkImage exit = VK_NULL_HANDLE;
        VkImageView exit_view = VK_NULL_HANDLE;
    } texture;

    struct {
        mat4_t model = {};
        mat4_t view = {};
        mat4_t proj = {};
        mat4_t inv_proj = {};
    } matrix;

    struct {
        vec3_t min = {0, 0, 0};
        vec3_t max = {1, 1, 1};
    } clip_volume;

    struct {
        bool enabled = false;
    } temporal;

    struct {
        bool enabled = false;
        size_t count = 0;
        const float* values = nullptr;
        const vec4_t* colors = nullptr;
    } iso;

    struct {
        bool enabled = false;
        float min_tf_value = 0.0f;
        float max_tf_value = 1.0f;
    } dvr;

    struct {
        vec3_t env_radiance = {0,0,0};
        float roughness = 0.4f;
        vec3_t dir_radiance = {1,1,1};
        float ior = 1.5f;
    } shading;

    vec3_t voxel_spacing = {};
    uint32_t max_steps = 512;
};

// Vulkan Volume Renderer class
class VulkanVolumeRenderer {
public:
    VulkanVolumeRenderer();
    ~VulkanVolumeRenderer();

    // Initialization and cleanup
    bool initialize(VkDevice device, VmaAllocator allocator, VkDescriptorPool descriptor_pool);
    void shutdown();

    // Rendering
    void render_volume(VkCommandBuffer command_buffer, const VulkanRenderDesc& desc);

private:
    struct UniformData {
        mat4_t view_to_model_mat;
        mat4_t model_to_view_mat;
        mat4_t inv_proj_mat;
        mat4_t model_view_proj_mat;

        vec2_t inv_res;
        float time;
        uint32_t enable_depth;

        vec3_t clip_plane_min;
        float tf_min;
        vec3_t clip_plane_max;
        float tf_inv_ext;

        vec3_t gradient_spacing_world_space;
        uint32_t max_steps;

        mat4_t gradient_spacing_tex_space;

        vec3_t env_radiance;
        float roughness;
        vec3_t dir_radiance;
        float F0;

        uint32_t dvr_enabled;
        uint32_t iso_enabled;
        uint32_t temporal_enabled;
        uint32_t padding;
    };

    struct IsovalueData {
        float values[8];
        vec4_t colors[8];
        uint32_t count;
    };

    // Vulkan resources
    VkDevice m_device;
    VmaAllocator m_allocator;
    
    // Compute pipeline
    VkPipelineLayout m_pipeline_layout;
    VkPipeline m_compute_pipeline;
    
    // Shaders
    VkShaderModule m_compute_shader;
    
    // Descriptor sets
    VkDescriptorSetLayout m_descriptor_set_layout;
    VkDescriptorPool m_descriptor_pool;
    VkDescriptorSet m_descriptor_set;
    
    // Buffers
    VkBuffer m_uniform_buffer;
    VmaAllocation m_uniform_allocation;
    void* m_uniform_mapped_ptr;
    
    // Entry/Exit pass resources
    VkRenderPass m_entry_exit_render_pass;
    VkPipelineLayout m_entry_exit_pipeline_layout;
    VkPipeline m_entry_exit_pipeline;
    VkShaderModule m_entry_exit_vertex_shader;
    VkShaderModule m_entry_exit_fragment_shader;
    
    // State
    float m_time;
    bool m_initialized;

    // Private methods
    bool create_shaders();
    bool create_descriptor_set_layout();
    bool create_compute_pipeline();
    bool create_entry_exit_pipeline();
    bool create_uniform_buffer();
    bool create_render_pass();
    
    void update_uniform_data(const VulkanRenderDesc& desc);
    void update_descriptor_set(const VulkanRenderDesc& desc);
    
    void cleanup_vulkan_resources();
};

// Utility functions for texture management
namespace texture {
    
struct VulkanTexture3D {
    VkImage image;
    VkImageView image_view;
    VkSampler sampler;
    VmaAllocation allocation;
    uint32_t width, height, depth;
    VkFormat format;
};

struct VulkanTexture2D {
    VkImage image;
    VkImageView image_view;
    VkSampler sampler;
    VmaAllocation allocation;
    uint32_t width, height;
    VkFormat format;
};

// Create 3D texture for volume data
bool create_volume_texture(VkDevice device, VmaAllocator allocator, 
                          uint32_t width, uint32_t height, uint32_t depth,
                          VkFormat format, const void* data, size_t data_size,
                          VulkanTexture3D& out_texture);

// Create 2D texture for transfer function
bool create_transfer_function_texture(VkDevice device, VmaAllocator allocator,
                                     uint32_t width, VkFormat format,
                                     const void* data, size_t data_size,
                                     VulkanTexture2D& out_texture);

// Create entry/exit textures
bool create_entry_exit_textures(VkDevice device, VmaAllocator allocator,
                               uint32_t width, uint32_t height,
                               VulkanTexture2D& out_entry, VulkanTexture2D& out_exit);

// Cleanup functions
void destroy_texture_3d(VkDevice device, VmaAllocator allocator, VulkanTexture3D& texture);
void destroy_texture_2d(VkDevice device, VmaAllocator allocator, VulkanTexture2D& texture);

} // namespace texture

} // namespace volume