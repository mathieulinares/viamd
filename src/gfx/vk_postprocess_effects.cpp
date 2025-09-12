#include "vk_postprocess.h"
#include "vk_utils.h"
#include <core/md_log.h>

#include <array>
#include <cstring>

namespace postprocessing {

// Agent C Task 3.5: Enhanced post-processing effects implementation
// Specific implementations for SSAO, DOF, and FXAA

struct SSAOUniforms {
    mat4_t projection_matrix;
    mat4_t inv_projection_matrix;
    vec2_t screen_size;
    vec2_t inv_screen_size;
    
    float radius;
    float bias;
    float intensity;
    int sample_count;
    
    // Sample kernel (aligned to 16 bytes)
    vec3_t samples[64];
};

struct DOFUniforms {
    vec2_t screen_size;
    vec2_t inv_screen_size;
    float focus_distance;
    float aperture;
    float focal_length;
    float max_blur_size;
    float near_plane;
    float far_plane;
    mat4_t inv_projection_matrix;
};

struct FXAAUniforms {
    vec2_t screen_size;
    vec2_t inv_screen_size;
    float contrast_threshold;
    float relative_threshold;
    float subpixel_quality;
    float edge_threshold_min;
    float edge_threshold_max;
    float padding;
};

class VulkanPostProcessingEffects {
public:
    VulkanPostProcessingEffects();
    ~VulkanPostProcessingEffects();

    bool initialize(VkDevice device, VmaAllocator allocator, VkDescriptorPool descriptor_pool);
    void shutdown();

    // Specific effect implementations
    void apply_ssao(VkCommandBuffer cmd, const VulkanPostProcessDesc& desc);
    void apply_dof(VkCommandBuffer cmd, const VulkanPostProcessDesc& desc);
    void apply_fxaa(VkCommandBuffer cmd, const VulkanPostProcessDesc& desc);

    // Effect registration with built-in shaders
    bool register_built_in_effects();
    
private:
    VkDevice m_device;
    VmaAllocator m_allocator;
    VkDescriptorPool m_descriptor_pool;
    
    // SSAO resources
    struct {
        VkPipeline pipeline;
        VkPipelineLayout layout;
        VkShaderModule shader;
        VkDescriptorSetLayout descriptor_layout;
        VkDescriptorSet descriptor_set;
        VkBuffer uniform_buffer;
        VmaAllocation uniform_allocation;
        void* uniform_mapped_ptr;
        VkSampler noise_sampler;
        VkImage noise_texture;
        VkImageView noise_texture_view;
        VmaAllocation noise_allocation;
    } m_ssao;
    
    // DOF resources
    struct {
        VkPipeline pipeline;
        VkPipelineLayout layout;
        VkShaderModule shader;
        VkDescriptorSetLayout descriptor_layout;
        VkDescriptorSet descriptor_set;
        VkBuffer uniform_buffer;
        VmaAllocation uniform_allocation;
        void* uniform_mapped_ptr;
    } m_dof;
    
    // FXAA resources
    struct {
        VkPipeline pipeline;
        VkPipelineLayout layout;
        VkShaderModule shader;
        VkDescriptorSetLayout descriptor_layout;
        VkDescriptorSet descriptor_set;
        VkBuffer uniform_buffer;
        VmaAllocation uniform_allocation;
        void* uniform_mapped_ptr;
    } m_fxaa;
    
    // Common resources
    VkSampler m_linear_sampler;
    VkSampler m_nearest_sampler;
    
    bool m_initialized;

    // Helper methods
    bool create_ssao_resources();
    bool create_dof_resources();
    bool create_fxaa_resources();
    bool create_noise_texture();
    bool create_common_samplers();
    
    void generate_ssao_samples(vec3_t* samples, int count);
    void update_ssao_uniforms(const VulkanPostProcessDesc& desc);
    void update_dof_uniforms(const VulkanPostProcessDesc& desc);
    void update_fxaa_uniforms(const VulkanPostProcessDesc& desc);
    
    void cleanup_effect_resources();
};

VulkanPostProcessingEffects::VulkanPostProcessingEffects()
    : m_device(VK_NULL_HANDLE)
    , m_allocator(VK_NULL_HANDLE)
    , m_descriptor_pool(VK_NULL_HANDLE)
    , m_linear_sampler(VK_NULL_HANDLE)
    , m_nearest_sampler(VK_NULL_HANDLE)
    , m_initialized(false)
{
    // Initialize effect structures
    m_ssao = {};
    m_dof = {};
    m_fxaa = {};
}

VulkanPostProcessingEffects::~VulkanPostProcessingEffects() {
    shutdown();
}

bool VulkanPostProcessingEffects::initialize(VkDevice device, VmaAllocator allocator, VkDescriptorPool descriptor_pool) {
    m_device = device;
    m_allocator = allocator;
    m_descriptor_pool = descriptor_pool;

    // Create common samplers
    if (!create_common_samplers()) {
        MD_LOG_ERROR("Failed to create common samplers for post-processing");
        return false;
    }

    // Create noise texture for SSAO
    if (!create_noise_texture()) {
        MD_LOG_ERROR("Failed to create noise texture for SSAO");
        return false;
    }

    // Create resources for each effect
    if (!create_ssao_resources()) {
        MD_LOG_ERROR("Failed to create SSAO resources");
        return false;
    }

    if (!create_dof_resources()) {
        MD_LOG_ERROR("Failed to create DOF resources");
        return false;
    }

    if (!create_fxaa_resources()) {
        MD_LOG_ERROR("Failed to create FXAA resources");
        return false;
    }

    // Register built-in effect shaders
    if (!register_built_in_effects()) {
        MD_LOG_ERROR("Failed to register built-in effects");
        return false;
    }

    m_initialized = true;
    MD_LOG_INFO("VulkanPostProcessingEffects initialized successfully");
    return true;
}

void VulkanPostProcessingEffects::shutdown() {
    if (!m_initialized) return;

    cleanup_effect_resources();
    m_initialized = false;
}

void VulkanPostProcessingEffects::apply_ssao(VkCommandBuffer cmd, const VulkanPostProcessDesc& desc) {
    if (!m_initialized) return;

    // Update uniforms
    update_ssao_uniforms(desc);

    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ssao.pipeline);

    // Bind descriptor sets
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ssao.layout, 
                           0, 1, &m_ssao.descriptor_set, 0, nullptr);

    // Dispatch compute shader
    uint32_t group_count_x = (desc.render_target.width + 7) / 8;
    uint32_t group_count_y = (desc.render_target.height + 7) / 8;
    vkCmdDispatch(cmd, group_count_x, group_count_y, 1);

    // Memory barrier
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void VulkanPostProcessingEffects::apply_dof(VkCommandBuffer cmd, const VulkanPostProcessDesc& desc) {
    if (!m_initialized) return;

    // Update uniforms
    update_dof_uniforms(desc);

    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_dof.pipeline);

    // Bind descriptor sets
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_dof.layout, 
                           0, 1, &m_dof.descriptor_set, 0, nullptr);

    // Dispatch compute shader
    uint32_t group_count_x = (desc.render_target.width + 7) / 8;
    uint32_t group_count_y = (desc.render_target.height + 7) / 8;
    vkCmdDispatch(cmd, group_count_x, group_count_y, 1);

    // Memory barrier
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void VulkanPostProcessingEffects::apply_fxaa(VkCommandBuffer cmd, const VulkanPostProcessDesc& desc) {
    if (!m_initialized) return;

    // Update uniforms
    update_fxaa_uniforms(desc);

    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_fxaa.pipeline);

    // Bind descriptor sets
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_fxaa.layout, 
                           0, 1, &m_fxaa.descriptor_set, 0, nullptr);

    // Dispatch compute shader
    uint32_t group_count_x = (desc.render_target.width + 7) / 8;
    uint32_t group_count_y = (desc.render_target.height + 7) / 8;
    vkCmdDispatch(cmd, group_count_x, group_count_y, 1);

    // Memory barrier
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

bool VulkanPostProcessingEffects::create_common_samplers() {
    // Linear sampler
    VkSamplerCreateInfo sampler_info = {};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.anisotropyEnable = VK_FALSE;
    sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable = VK_FALSE;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(m_device, &sampler_info, nullptr, &m_linear_sampler) != VK_SUCCESS) {
        return false;
    }

    // Nearest sampler
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    if (vkCreateSampler(m_device, &sampler_info, nullptr, &m_nearest_sampler) != VK_SUCCESS) {
        return false;
    }

    return true;
}

void VulkanPostProcessingEffects::generate_ssao_samples(vec3_t* samples, int count) {
    // Generate hemisphere sample kernel for SSAO
    for (int i = 0; i < count; ++i) {
        float scale = static_cast<float>(i) / static_cast<float>(count);
        scale = 0.1f + scale * scale * 0.9f; // More samples closer to center
        
        // Random hemisphere sample
        samples[i] = {
            static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f,
            static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f,
            static_cast<float>(rand()) / RAND_MAX
        };
        
        // Normalize and scale
        float length = sqrtf(samples[i].x * samples[i].x + samples[i].y * samples[i].y + samples[i].z * samples[i].z);
        samples[i].x = (samples[i].x / length) * scale;
        samples[i].y = (samples[i].y / length) * scale;
        samples[i].z = (samples[i].z / length) * scale;
    }
}

// TODO: Implement remaining private methods (create_*_resources, update_*_uniforms, cleanup_effect_resources)

} // namespace postprocessing