#include "vk_postprocess.h"
#include "vk_context.h"
#include "vk_utils.h"

#include <core/md_log.h>
#include <vk_mem_alloc.h>

namespace postprocessing {

VulkanPostProcessingPipeline::VulkanPostProcessingPipeline()
    : m_device(VK_NULL_HANDLE)
    , m_allocator(VK_NULL_HANDLE)
    , m_descriptor_pool(VK_NULL_HANDLE)
    , m_uniform_buffer(VK_NULL_HANDLE)
    , m_uniform_allocation(VK_NULL_HANDLE)
    , m_uniform_mapped_ptr(nullptr)
    , m_linear_sampler(VK_NULL_HANDLE)
    , m_nearest_sampler(VK_NULL_HANDLE)
    , m_initialized(false)
{
    // Initialize effects arrays
    for (size_t i = 0; i < static_cast<size_t>(EffectType::CUSTOM); ++i) {
        m_effects[i] = {};
        m_effect_initialized[i] = false;
    }
}

VulkanPostProcessingPipeline::~VulkanPostProcessingPipeline() {
    shutdown();
}

bool VulkanPostProcessingPipeline::initialize(VkDevice device, VmaAllocator allocator, VkDescriptorPool descriptor_pool) {
    m_device = device;
    m_allocator = allocator;
    m_descriptor_pool = descriptor_pool;

    if (!create_common_resources()) {
        MD_LOG_ERROR("Failed to create common post-processing resources");
        return false;
    }

    m_initialized = true;
    MD_LOG_INFO("VulkanPostProcessingPipeline initialized successfully");
    return true;
}

void VulkanPostProcessingPipeline::shutdown() {
    if (!m_initialized) return;

    cleanup_vulkan_resources();
    m_initialized = false;
}

bool VulkanPostProcessingPipeline::register_effect(EffectType type, const char* compute_shader_path) {
    if (type >= EffectType::CUSTOM) {
        MD_LOG_ERROR("Invalid effect type for registration");
        return false;
    }

    // TODO: Load and compile shader from file
    // For now, this is a placeholder that would:
    // 1. Load GLSL file from compute_shader_path
    // 2. Compile to SPIR-V using glslc or shaderc
    // 3. Create shader module
    // 4. Create pipeline for the effect

    MD_LOG_INFO("Post-processing effect registration placeholder: %s", compute_shader_path);
    return true;
}

bool VulkanPostProcessingPipeline::register_effect_from_spv(EffectType type, const uint32_t* spv_data, size_t spv_size) {
    if (type >= EffectType::CUSTOM) {
        MD_LOG_ERROR("Invalid effect type for registration");
        return false;
    }

    size_t effect_index = static_cast<size_t>(type);
    
    // Create shader module from SPIR-V data
    VkShaderModuleCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = spv_size;
    create_info.pCode = spv_data;

    if (vkCreateShaderModule(m_device, &create_info, nullptr, &m_effects[effect_index].shader) != VK_SUCCESS) {
        MD_LOG_ERROR("Failed to create shader module for post-processing effect");
        return false;
    }

    // Create pipeline for this effect
    if (!create_effect_pipeline(type, m_effects[effect_index].shader)) {
        vkDestroyShaderModule(m_device, m_effects[effect_index].shader, nullptr);
        m_effects[effect_index].shader = VK_NULL_HANDLE;
        return false;
    }

    m_effect_initialized[effect_index] = true;
    return true;
}

void VulkanPostProcessingPipeline::apply_effect(VkCommandBuffer command_buffer, EffectType effect, const VulkanPostProcessDesc& desc) {
    if (!m_initialized) {
        MD_LOG_ERROR("Post-processing pipeline not initialized");
        return;
    }

    size_t effect_index = static_cast<size_t>(effect);
    if (effect >= EffectType::CUSTOM || !m_effect_initialized[effect_index]) {
        MD_LOG_ERROR("Effect not registered or invalid");
        return;
    }

    // Update uniform buffer with current parameters
    update_uniform_buffer(desc);

    // Bind compute pipeline for the effect
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_effects[effect_index].pipeline);

    // TODO: Bind descriptor sets and dispatch compute shader
    // This would include:
    // 1. Update descriptor set with input/output textures
    // 2. Bind descriptor sets
    // 3. Dispatch appropriate work groups based on texture size

    uint32_t group_count_x = (desc.render_target.width + 7) / 8;
    uint32_t group_count_y = (desc.render_target.height + 7) / 8;
    vkCmdDispatch(command_buffer, group_count_x, group_count_y, 1);

    // Memory barrier to ensure compute writes are visible
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void VulkanPostProcessingPipeline::apply_effect_chain(VkCommandBuffer command_buffer, const EffectType* effects, size_t effect_count, const VulkanPostProcessDesc& desc) {
    // Apply multiple effects in sequence
    // This would typically use ping-pong textures to chain effects together
    for (size_t i = 0; i < effect_count; ++i) {
        apply_effect(command_buffer, effects[i], desc);
    }
}

bool VulkanPostProcessingPipeline::create_effect_pipeline(EffectType type, VkShaderModule shader) {
    size_t effect_index = static_cast<size_t>(type);

    // Create descriptor set layout for this effect
    VkDescriptorSetLayoutBinding bindings[4] = {};

    // Input texture
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Output image
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Depth texture (optional)
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Uniform buffer
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 4;
    layout_info.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(m_device, &layout_info, nullptr, &m_effects[effect_index].descriptor_layout) != VK_SUCCESS) {
        MD_LOG_ERROR("Failed to create descriptor set layout for post-processing effect");
        return false;
    }

    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipeline_layout_info = {};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &m_effects[effect_index].descriptor_layout;

    if (vkCreatePipelineLayout(m_device, &pipeline_layout_info, nullptr, &m_effects[effect_index].layout) != VK_SUCCESS) {
        MD_LOG_ERROR("Failed to create pipeline layout for post-processing effect");
        return false;
    }

    // Create compute pipeline
    VkComputePipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline_info.stage.module = shader;
    pipeline_info.stage.pName = "main";
    pipeline_info.layout = m_effects[effect_index].layout;

    if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &m_effects[effect_index].pipeline) != VK_SUCCESS) {
        MD_LOG_ERROR("Failed to create compute pipeline for post-processing effect");
        return false;
    }

    return true;
}

bool VulkanPostProcessingPipeline::create_common_resources() {
    // Create uniform buffer
    VkDeviceSize buffer_size = 256; // Enough for common uniform data

    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size;
    buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocation_info;
    if (vmaCreateBuffer(m_allocator, &buffer_info, &alloc_info, &m_uniform_buffer, &m_uniform_allocation, &allocation_info) != VK_SUCCESS) {
        MD_LOG_ERROR("Failed to create post-processing uniform buffer");
        return false;
    }

    m_uniform_mapped_ptr = allocation_info.pMappedData;

    // Create samplers
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
        MD_LOG_ERROR("Failed to create linear sampler for post-processing");
        return false;
    }

    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    if (vkCreateSampler(m_device, &sampler_info, nullptr, &m_nearest_sampler) != VK_SUCCESS) {
        MD_LOG_ERROR("Failed to create nearest sampler for post-processing");
        return false;
    }

    return true;
}

void VulkanPostProcessingPipeline::update_uniform_buffer(const VulkanPostProcessDesc& desc) {
    if (!m_uniform_mapped_ptr) return;

    // Pack common uniform data
    struct UniformData {
        mat4_t view_matrix;
        mat4_t proj_matrix;
        mat4_t inv_view_matrix;
        mat4_t inv_proj_matrix;
        vec2_t screen_size;
        float near_plane;
        float far_plane;
    } uniform_data;

    uniform_data.view_matrix = desc.matrix.view;
    uniform_data.proj_matrix = desc.matrix.proj;
    uniform_data.inv_view_matrix = desc.matrix.inv_view;
    uniform_data.inv_proj_matrix = desc.matrix.inv_proj;
    uniform_data.screen_size = {(float)desc.render_target.width, (float)desc.render_target.height};
    uniform_data.near_plane = desc.camera.near_plane;
    uniform_data.far_plane = desc.camera.far_plane;

    memcpy(m_uniform_mapped_ptr, &uniform_data, sizeof(UniformData));
}

void VulkanPostProcessingPipeline::cleanup_vulkan_resources() {
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);

        // Cleanup effects
        for (size_t i = 0; i < static_cast<size_t>(EffectType::CUSTOM); ++i) {
            if (m_effect_initialized[i]) {
                if (m_effects[i].pipeline != VK_NULL_HANDLE) {
                    vkDestroyPipeline(m_device, m_effects[i].pipeline, nullptr);
                }
                if (m_effects[i].layout != VK_NULL_HANDLE) {
                    vkDestroyPipelineLayout(m_device, m_effects[i].layout, nullptr);
                }
                if (m_effects[i].shader != VK_NULL_HANDLE) {
                    vkDestroyShaderModule(m_device, m_effects[i].shader, nullptr);
                }
                if (m_effects[i].descriptor_layout != VK_NULL_HANDLE) {
                    vkDestroyDescriptorSetLayout(m_device, m_effects[i].descriptor_layout, nullptr);
                }
                m_effects[i] = {};
                m_effect_initialized[i] = false;
            }
        }

        // Cleanup common resources
        if (m_uniform_buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_allocator, m_uniform_buffer, m_uniform_allocation);
        }

        if (m_linear_sampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_device, m_linear_sampler, nullptr);
        }

        if (m_nearest_sampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_device, m_nearest_sampler, nullptr);
        }
    }

    // Reset all handles
    m_device = VK_NULL_HANDLE;
    m_allocator = VK_NULL_HANDLE;
    m_descriptor_pool = VK_NULL_HANDLE;
    m_uniform_buffer = VK_NULL_HANDLE;
    m_uniform_allocation = VK_NULL_HANDLE;
    m_uniform_mapped_ptr = nullptr;
    m_linear_sampler = VK_NULL_HANDLE;
    m_nearest_sampler = VK_NULL_HANDLE;
}

// Texture management implementation
namespace texture {

bool create_post_process_texture(VkDevice device, VmaAllocator allocator,
                                uint32_t width, uint32_t height, VkFormat format,
                                PostProcessTexture& out_texture) {
    // Create 2D image for post-processing
    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = width;
    image_info.extent.height = height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = format;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &image_info, &alloc_info, &out_texture.image, &out_texture.allocation, nullptr) != VK_SUCCESS) {
        MD_LOG_ERROR("Failed to create post-processing texture");
        return false;
    }

    // Create image view
    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = out_texture.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &view_info, nullptr, &out_texture.image_view) != VK_SUCCESS) {
        MD_LOG_ERROR("Failed to create post-processing texture image view");
        vmaDestroyImage(allocator, out_texture.image, out_texture.allocation);
        return false;
    }

    out_texture.width = width;
    out_texture.height = height;
    out_texture.format = format;

    return true;
}

void destroy_post_process_texture(VkDevice device, VmaAllocator allocator, PostProcessTexture& texture) {
    if (texture.image_view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, texture.image_view, nullptr);
        texture.image_view = VK_NULL_HANDLE;
    }
    if (texture.image != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, texture.image, texture.allocation);
        texture.image = VK_NULL_HANDLE;
        texture.allocation = VK_NULL_HANDLE;
    }
}

} // namespace texture

} // namespace postprocessing