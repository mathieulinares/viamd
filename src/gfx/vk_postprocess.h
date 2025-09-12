#pragma once

#include <core/md_vec_math.h>
#include <vulkan/vulkan.h>

#include <memory>

// Forward declarations
struct VmaAllocator_T;
typedef VmaAllocator_T* VmaAllocator;

namespace postprocessing {

// Vulkan Post-Processing Framework
// This provides the foundation for Agent C to implement post-processing effects

enum class EffectType {
    SSAO,
    DOF,
    FXAA,
    TEMPORAL_AA,
    TONE_MAPPING,
    CUSTOM
};

struct VulkanPostProcessDesc {
    struct {
        VkImage input = VK_NULL_HANDLE;
        VkImageView input_view = VK_NULL_HANDLE;
        VkImage output = VK_NULL_HANDLE;
        VkImageView output_view = VK_NULL_HANDLE;
        VkImage depth = VK_NULL_HANDLE;
        VkImageView depth_view = VK_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
    } render_target;

    struct {
        mat4_t view = {};
        mat4_t proj = {};
        mat4_t inv_view = {};
        mat4_t inv_proj = {};
    } matrix;

    struct {
        float near_plane = 0.1f;
        float far_plane = 1000.0f;
        float fov = 60.0f;
    } camera;

    // Effect-specific parameters
    union {
        struct {
            float radius = 0.5f;
            float bias = 0.025f;
            int sample_count = 16;
        } ssao;
        
        struct {
            float focus_distance = 10.0f;
            float aperture = 2.8f;
            float focal_length = 50.0f;
        } dof;
        
        struct {
            float contrast_threshold = 0.0312f;
            float relative_threshold = 0.063f;
            int subpixel_quality = 75;
        } fxaa;
    } params;
};

class VulkanPostProcessingPipeline {
public:
    VulkanPostProcessingPipeline();
    ~VulkanPostProcessingPipeline();

    // Initialization and cleanup
    bool initialize(VkDevice device, VmaAllocator allocator, VkDescriptorPool descriptor_pool);
    void shutdown();

    // Effect registration and management
    bool register_effect(EffectType type, const char* compute_shader_path);
    bool register_effect_from_spv(EffectType type, const uint32_t* spv_data, size_t spv_size);
    
    // Rendering
    void apply_effect(VkCommandBuffer command_buffer, EffectType effect, const VulkanPostProcessDesc& desc);
    void apply_effect_chain(VkCommandBuffer command_buffer, const EffectType* effects, size_t effect_count, const VulkanPostProcessDesc& desc);

private:
    struct EffectData {
        VkPipeline pipeline;
        VkPipelineLayout layout;
        VkShaderModule shader;
        VkDescriptorSetLayout descriptor_layout;
    };

    VkDevice m_device;
    VmaAllocator m_allocator;
    VkDescriptorPool m_descriptor_pool;
    
    // Effects registry
    EffectData m_effects[static_cast<size_t>(EffectType::CUSTOM)];
    bool m_effect_initialized[static_cast<size_t>(EffectType::CUSTOM)];
    
    // Common resources
    VkBuffer m_uniform_buffer;
    VmaAllocation m_uniform_allocation;
    void* m_uniform_mapped_ptr;
    
    VkSampler m_linear_sampler;
    VkSampler m_nearest_sampler;
    
    bool m_initialized;

    // Helper methods
    bool create_effect_pipeline(EffectType type, VkShaderModule shader);
    bool create_common_resources();
    void cleanup_vulkan_resources();
    void update_uniform_buffer(const VulkanPostProcessDesc& desc);
};

// Utility functions for post-processing texture management
namespace texture {

struct PostProcessTexture {
    VkImage image;
    VkImageView image_view;
    VmaAllocation allocation;
    uint32_t width, height;
    VkFormat format;
};

// Create intermediate textures for post-processing chain
bool create_post_process_texture(VkDevice device, VmaAllocator allocator,
                                uint32_t width, uint32_t height, VkFormat format,
                                PostProcessTexture& out_texture);

void destroy_post_process_texture(VkDevice device, VmaAllocator allocator, PostProcessTexture& texture);

} // namespace texture

} // namespace postprocessing