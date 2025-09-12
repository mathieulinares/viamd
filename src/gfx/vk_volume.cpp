#include "vk_volume.h"
#include "vk_volume_shaders.h"
#include "vk_context.h"
#include "vk_utils.h"
#include "vk_buffer.h"

#include <core/md_log.h>
#include <core/md_common.h>

#include <vk_mem_alloc.h>

// Include SPIR-V compiled shaders
namespace volume_shaders {
    extern const uint32_t raycaster_comp_spv[];
    extern const size_t raycaster_comp_spv_size;
    
    extern const uint32_t entry_exit_vert_spv[];
    extern const size_t entry_exit_vert_spv_size;
    
    extern const uint32_t entry_exit_frag_spv[];
    extern const size_t entry_exit_frag_spv_size;
}

namespace volume {

VulkanVolumeRenderer::VulkanVolumeRenderer()
    : m_device(VK_NULL_HANDLE)
    , m_allocator(VK_NULL_HANDLE)
    , m_pipeline_layout(VK_NULL_HANDLE)
    , m_compute_pipeline(VK_NULL_HANDLE)
    , m_compute_shader(VK_NULL_HANDLE)
    , m_descriptor_set_layout(VK_NULL_HANDLE)
    , m_descriptor_pool(VK_NULL_HANDLE)
    , m_descriptor_set(VK_NULL_HANDLE)
    , m_uniform_buffer(VK_NULL_HANDLE)
    , m_uniform_allocation(VK_NULL_HANDLE)
    , m_uniform_mapped_ptr(nullptr)
    , m_entry_exit_render_pass(VK_NULL_HANDLE)
    , m_entry_exit_pipeline_layout(VK_NULL_HANDLE)
    , m_entry_exit_pipeline(VK_NULL_HANDLE)
    , m_entry_exit_vertex_shader(VK_NULL_HANDLE)
    , m_entry_exit_fragment_shader(VK_NULL_HANDLE)
    , m_time(0.0f)
    , m_initialized(false)
{
}

VulkanVolumeRenderer::~VulkanVolumeRenderer() {
    shutdown();
}

bool VulkanVolumeRenderer::initialize(VkDevice device, VmaAllocator allocator, VkDescriptorPool descriptor_pool) {
    m_device = device;
    m_allocator = allocator;
    m_descriptor_pool = descriptor_pool;

    if (!create_shaders()) {
        MD_LOG_ERROR("Failed to create volume rendering shaders");
        return false;
    }

    if (!create_descriptor_set_layout()) {
        MD_LOG_ERROR("Failed to create descriptor set layout");
        return false;
    }

    if (!create_uniform_buffer()) {
        MD_LOG_ERROR("Failed to create uniform buffer");
        return false;
    }

    if (!create_compute_pipeline()) {
        MD_LOG_ERROR("Failed to create compute pipeline");
        return false;
    }

    if (!create_render_pass()) {
        MD_LOG_ERROR("Failed to create render pass");
        return false;
    }

    if (!create_entry_exit_pipeline()) {
        MD_LOG_ERROR("Failed to create entry/exit pipeline");
        return false;
    }

    m_initialized = true;
    return true;
}

void VulkanVolumeRenderer::shutdown() {
    if (!m_initialized) return;

    cleanup_vulkan_resources();
    m_initialized = false;
}

bool VulkanVolumeRenderer::create_shaders() {
    // Create compute shader for volume ray-casting
    VkShaderModuleCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = volume_shaders::raycaster_comp_spv_size;
    create_info.pCode = volume_shaders::raycaster_comp_spv;

    if (vkCreateShaderModule(m_device, &create_info, nullptr, &m_compute_shader) != VK_SUCCESS) {
        MD_LOG_ERROR("Failed to create compute shader module");
        return false;
    }

    // Create entry/exit shaders
    create_info.codeSize = volume_shaders::entry_exit_vert_spv_size;
    create_info.pCode = volume_shaders::entry_exit_vert_spv;

    if (vkCreateShaderModule(m_device, &create_info, nullptr, &m_entry_exit_vertex_shader) != VK_SUCCESS) {
        MD_LOG_ERROR("Failed to create entry/exit vertex shader module");
        return false;
    }

    create_info.codeSize = volume_shaders::entry_exit_frag_spv_size;
    create_info.pCode = volume_shaders::entry_exit_frag_spv;

    if (vkCreateShaderModule(m_device, &create_info, nullptr, &m_entry_exit_fragment_shader) != VK_SUCCESS) {
        MD_LOG_ERROR("Failed to create entry/exit fragment shader module");
        return false;
    }

    return true;
}

bool VulkanVolumeRenderer::create_descriptor_set_layout() {
    VkDescriptorSetLayoutBinding bindings[7] = {};

    // Output image
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Entry texture
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Exit texture
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Volume texture
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Transfer function texture
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Depth texture (optional)
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Uniform buffer
    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 7;
    layout_info.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(m_device, &layout_info, nullptr, &m_descriptor_set_layout) != VK_SUCCESS) {
        MD_LOG_ERROR("Failed to create descriptor set layout");
        return false;
    }

    return true;
}

bool VulkanVolumeRenderer::create_compute_pipeline() {
    // Push constant range for isovalue data
    VkPushConstantRange push_constant_range = {};
    push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(IsovalueData);

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipeline_layout_info = {};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &m_descriptor_set_layout;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &push_constant_range;

    if (vkCreatePipelineLayout(m_device, &pipeline_layout_info, nullptr, &m_pipeline_layout) != VK_SUCCESS) {
        MD_LOG_ERROR("Failed to create pipeline layout");
        return false;
    }

    // Compute pipeline
    VkComputePipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline_info.stage.module = m_compute_shader;
    pipeline_info.stage.pName = "main";
    pipeline_info.layout = m_pipeline_layout;

    if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &m_compute_pipeline) != VK_SUCCESS) {
        MD_LOG_ERROR("Failed to create compute pipeline");
        return false;
    }

    return true;
}

bool VulkanVolumeRenderer::create_uniform_buffer() {
    VkDeviceSize buffer_size = sizeof(UniformData);

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
        MD_LOG_ERROR("Failed to create uniform buffer");
        return false;
    }

    m_uniform_mapped_ptr = allocation_info.pMappedData;
    return true;
}

bool VulkanVolumeRenderer::create_render_pass() {
    VkAttachmentDescription color_attachments[2] = {};

    // Entry attachment
    color_attachments[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    color_attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Exit attachment
    color_attachments[1] = color_attachments[0];

    VkAttachmentReference color_attachment_refs[2] = {};
    color_attachment_refs[0].attachment = 0;
    color_attachment_refs[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment_refs[1].attachment = 1;
    color_attachment_refs[1].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 2;
    subpass.pColorAttachments = color_attachment_refs;

    VkRenderPassCreateInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 2;
    render_pass_info.pAttachments = color_attachments;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;

    if (vkCreateRenderPass(m_device, &render_pass_info, nullptr, &m_entry_exit_render_pass) != VK_SUCCESS) {
        MD_LOG_ERROR("Failed to create entry/exit render pass");
        return false;
    }

    return true;
}

bool VulkanVolumeRenderer::create_entry_exit_pipeline() {
    // This would be a simplified pipeline for generating entry/exit points
    // In a full implementation, this would create the graphics pipeline
    // for rendering the volume bounding geometry
    
    // For now, create a placeholder pipeline layout
    VkPipelineLayoutCreateInfo pipeline_layout_info = {};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &m_descriptor_set_layout;

    if (vkCreatePipelineLayout(m_device, &pipeline_layout_info, nullptr, &m_entry_exit_pipeline_layout) != VK_SUCCESS) {
        MD_LOG_ERROR("Failed to create entry/exit pipeline layout");
        return false;
    }

    // TODO: Create full graphics pipeline for entry/exit point generation
    // This would include vertex input state, rasterization state, etc.
    
    return true;
}

void VulkanVolumeRenderer::update_uniform_data(const VulkanRenderDesc& desc) {
    if (!m_uniform_mapped_ptr) return;

    // Update time for temporal effects
    m_time += 1.0f / 100.0f;
    if (m_time > 100.0f) m_time -= 100.0f;
    if (!desc.temporal.enabled) m_time = 0.0f;

    mat4_t model_to_view_matrix = desc.matrix.view * desc.matrix.model;
    
    float tf_min = desc.dvr.min_tf_value;
    float tf_max = desc.dvr.max_tf_value;
    float tf_ext = tf_max - tf_min;
    float inv_tf_ext = tf_ext == 0 ? 1.0f : 1.0f / tf_ext;

    const float n1 = 1.0f;
    const float n2 = desc.shading.ior;
    const float F0 = powf((n1-n2)/(n1+n2), 2.0f);

    UniformData uniform_data = {};
    uniform_data.view_to_model_mat = mat4_inverse(model_to_view_matrix);
    uniform_data.model_to_view_mat = model_to_view_matrix;
    uniform_data.inv_proj_mat = desc.matrix.inv_proj;
    uniform_data.model_view_proj_mat = desc.matrix.proj * model_to_view_matrix;
    uniform_data.inv_res = {1.0f / (float)desc.render_target.width, 1.0f / (float)desc.render_target.height};
    uniform_data.time = m_time;
    uniform_data.enable_depth = desc.render_target.depth != VK_NULL_HANDLE ? 1 : 0;
    uniform_data.clip_plane_min = desc.clip_volume.min;
    uniform_data.tf_min = tf_min;
    uniform_data.clip_plane_max = desc.clip_volume.max;
    uniform_data.tf_inv_ext = inv_tf_ext;
    uniform_data.gradient_spacing_world_space = desc.voxel_spacing;
    uniform_data.max_steps = desc.max_steps;
    uniform_data.gradient_spacing_tex_space = uniform_data.view_to_model_mat * mat4_scale(desc.voxel_spacing.x, desc.voxel_spacing.y, desc.voxel_spacing.z);
    uniform_data.env_radiance = desc.shading.env_radiance;
    uniform_data.roughness = desc.shading.roughness;
    uniform_data.dir_radiance = desc.shading.dir_radiance;
    uniform_data.F0 = F0;
    uniform_data.dvr_enabled = desc.dvr.enabled ? 1 : 0;
    uniform_data.iso_enabled = desc.iso.enabled ? 1 : 0;
    uniform_data.temporal_enabled = desc.temporal.enabled ? 1 : 0;

    memcpy(m_uniform_mapped_ptr, &uniform_data, sizeof(UniformData));
}

void VulkanVolumeRenderer::update_descriptor_set(const VulkanRenderDesc& desc) {
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = m_descriptor_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &m_descriptor_set_layout;

    if (vkAllocateDescriptorSets(m_device, &alloc_info, &m_descriptor_set) != VK_SUCCESS) {
        MD_LOG_ERROR("Failed to allocate descriptor set");
        return;
    }

    // Update descriptor set with current textures and buffer
    VkWriteDescriptorSet descriptor_writes[7] = {};

    // Output image
    VkDescriptorImageInfo image_info = {};
    image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_info.imageView = desc.render_target.color_view;

    descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[0].dstSet = m_descriptor_set;
    descriptor_writes[0].dstBinding = 0;
    descriptor_writes[0].descriptorCount = 1;
    descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptor_writes[0].pImageInfo = &image_info;

    // Set up other texture bindings similarly...
    // This is simplified for brevity
    
    vkUpdateDescriptorSets(m_device, 7, descriptor_writes, 0, nullptr);
}

void VulkanVolumeRenderer::render_volume(VkCommandBuffer command_buffer, const VulkanRenderDesc& desc) {
    if (!m_initialized) {
        MD_LOG_ERROR("VulkanVolumeRenderer not initialized");
        return;
    }

    // Update uniform data
    update_uniform_data(desc);
    
    // Update descriptor set with current textures
    update_descriptor_set(desc);

    // Prepare isosurface data for push constants
    IsovalueData iso_data = {};
    if (desc.iso.enabled && desc.iso.count > 0) {
        size_t count = desc.iso.count < 8 ? desc.iso.count : 8;
        for (size_t i = 0; i < count; ++i) {
            iso_data.values[i] = desc.iso.values[i];
            iso_data.colors[i] = desc.iso.colors[i];
        }
        iso_data.count = (uint32_t)count;
    }

    // Bind compute pipeline
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute_pipeline);
    
    // Bind descriptor set
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline_layout, 0, 1, &m_descriptor_set, 0, nullptr);
    
    // Push constants for isovalue data
    vkCmdPushConstants(command_buffer, m_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(IsovalueData), &iso_data);

    // Dispatch compute shader
    uint32_t group_count_x = (desc.render_target.width + 7) / 8;
    uint32_t group_count_y = (desc.render_target.height + 7) / 8;
    vkCmdDispatch(command_buffer, group_count_x, group_count_y, 1);

    // Memory barrier to ensure compute shader writes are visible
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void VulkanVolumeRenderer::cleanup_vulkan_resources() {
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);

        if (m_uniform_buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_allocator, m_uniform_buffer, m_uniform_allocation);
        }

        if (m_compute_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_compute_pipeline, nullptr);
        }

        if (m_entry_exit_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_entry_exit_pipeline, nullptr);
        }

        if (m_pipeline_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_device, m_pipeline_layout, nullptr);
        }

        if (m_entry_exit_pipeline_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_device, m_entry_exit_pipeline_layout, nullptr);
        }

        if (m_descriptor_set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_device, m_descriptor_set_layout, nullptr);
        }

        if (m_entry_exit_render_pass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(m_device, m_entry_exit_render_pass, nullptr);
        }

        if (m_compute_shader != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_device, m_compute_shader, nullptr);
        }

        if (m_entry_exit_vertex_shader != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_device, m_entry_exit_vertex_shader, nullptr);
        }

        if (m_entry_exit_fragment_shader != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_device, m_entry_exit_fragment_shader, nullptr);
        }
    }

    // Reset all handles
    m_device = VK_NULL_HANDLE;
    m_allocator = VK_NULL_HANDLE;
    m_pipeline_layout = VK_NULL_HANDLE;
    m_compute_pipeline = VK_NULL_HANDLE;
    m_compute_shader = VK_NULL_HANDLE;
    m_descriptor_set_layout = VK_NULL_HANDLE;
    m_descriptor_pool = VK_NULL_HANDLE;
    m_descriptor_set = VK_NULL_HANDLE;
    m_uniform_buffer = VK_NULL_HANDLE;
    m_uniform_allocation = VK_NULL_HANDLE;
    m_uniform_mapped_ptr = nullptr;
    m_entry_exit_render_pass = VK_NULL_HANDLE;
    m_entry_exit_pipeline_layout = VK_NULL_HANDLE;
    m_entry_exit_pipeline = VK_NULL_HANDLE;
    m_entry_exit_vertex_shader = VK_NULL_HANDLE;
    m_entry_exit_fragment_shader = VK_NULL_HANDLE;
}

// Texture management implementation
namespace texture {

bool create_volume_texture(VkDevice device, VmaAllocator allocator, 
                          uint32_t width, uint32_t height, uint32_t depth,
                          VkFormat format, const void* data, size_t data_size,
                          VulkanTexture3D& out_texture) {
    // Create 3D image
    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_3D;
    image_info.extent.width = width;
    image_info.extent.height = height;
    image_info.extent.depth = depth;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = format;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &image_info, &alloc_info, &out_texture.image, &out_texture.allocation, nullptr) != VK_SUCCESS) {
        MD_LOG_ERROR("Failed to create 3D volume texture");
        return false;
    }

    // Create image view
    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = out_texture.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_3D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &view_info, nullptr, &out_texture.image_view) != VK_SUCCESS) {
        MD_LOG_ERROR("Failed to create 3D texture image view");
        vmaDestroyImage(allocator, out_texture.image, out_texture.allocation);
        return false;
    }

    // Create sampler
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

    if (vkCreateSampler(device, &sampler_info, nullptr, &out_texture.sampler) != VK_SUCCESS) {
        MD_LOG_ERROR("Failed to create 3D texture sampler");
        vkDestroyImageView(device, out_texture.image_view, nullptr);
        vmaDestroyImage(allocator, out_texture.image, out_texture.allocation);
        return false;
    }

    out_texture.width = width;
    out_texture.height = height;
    out_texture.depth = depth;
    out_texture.format = format;

    // TODO: Upload data using staging buffer
    // This would require creating a staging buffer, copying data, and transitioning image layouts

    return true;
}

bool create_transfer_function_texture(VkDevice device, VmaAllocator allocator,
                                     uint32_t width, VkFormat format,
                                     const void* data, size_t data_size,
                                     VulkanTexture2D& out_texture) {
    // Similar implementation to 3D texture but for 2D transfer function
    // Implementation details would follow the same pattern as create_volume_texture
    return true; // Placeholder
}

bool create_entry_exit_textures(VkDevice device, VmaAllocator allocator,
                               uint32_t width, uint32_t height,
                               VulkanTexture2D& out_entry, VulkanTexture2D& out_exit) {
    // Create entry and exit point textures for volume rendering
    // Implementation would create render targets for entry/exit point generation
    return true; // Placeholder
}

void destroy_texture_3d(VkDevice device, VmaAllocator allocator, VulkanTexture3D& texture) {
    if (texture.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, texture.sampler, nullptr);
        texture.sampler = VK_NULL_HANDLE;
    }
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

void destroy_texture_2d(VkDevice device, VmaAllocator allocator, VulkanTexture2D& texture) {
    if (texture.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, texture.sampler, nullptr);
        texture.sampler = VK_NULL_HANDLE;
    }
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

} // namespace volume