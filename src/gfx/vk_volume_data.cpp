#include "vk_volume_data.h"
#include "vk_utils.h"
#include <core/md_log.h>

#include <algorithm>
#include <cstring>
#include <chrono>

namespace volume {

VulkanVolumeDataManager::VulkanVolumeDataManager() 
    : m_device(VK_NULL_HANDLE)
    , m_allocator(VK_NULL_HANDLE)
    , m_transfer_queue(VK_NULL_HANDLE)
    , m_transfer_queue_family(0)
    , m_transfer_command_pool(VK_NULL_HANDLE)
    , m_transfer_command_buffer(VK_NULL_HANDLE)
    , m_next_volume_id(1)
    , m_next_tf_id(1)
    , m_memory_budget(512 * 1024 * 1024) // 512MB default
    , m_current_memory_usage(0)
    , m_staging_buffer(VK_NULL_HANDLE)
    , m_staging_allocation(VK_NULL_HANDLE)
    , m_staging_mapped_ptr(nullptr)
    , m_staging_buffer_size(64 * 1024 * 1024) // 64MB staging buffer
    , m_initialized(false)
{
}

VulkanVolumeDataManager::~VulkanVolumeDataManager() {
    shutdown();
}

bool VulkanVolumeDataManager::initialize(VkDevice device, VmaAllocator allocator, VkQueue transfer_queue, uint32_t transfer_queue_family) {
    if (m_initialized) {
        return true;
    }

    m_device = device;
    m_allocator = allocator;
    m_transfer_queue = transfer_queue;
    m_transfer_queue_family = transfer_queue_family;

    // Create command pool for transfers
    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = m_transfer_queue_family;

    if (vkCreateCommandPool(m_device, &pool_info, nullptr, &m_transfer_command_pool) != VK_SUCCESS) {
        md_logf(MD_LOG_TYPE_ERROR, "Failed to create transfer command pool");
        return false;
    }

    // Allocate command buffer
    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = m_transfer_command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(m_device, &alloc_info, &m_transfer_command_buffer) != VK_SUCCESS) {
        md_logf(MD_LOG_TYPE_ERROR, "Failed to allocate transfer command buffer");
        return false;
    }

    // Create staging buffer
    if (!create_staging_buffer(m_staging_buffer_size)) {
        md_logf(MD_LOG_TYPE_ERROR, "Failed to create staging buffer");
        return false;
    }

    m_initialized = true;
    md_logf(MD_LOG_TYPE_INFO, "VulkanVolumeDataManager initialized successfully");
    return true;
}

void VulkanVolumeDataManager::shutdown() {
    if (!m_initialized) {
        return;
    }

    // Clean up all volumes
    for (auto& volume : m_volumes) {
        if (volume) {
            cleanup_volume_data(*volume);
        }
    }
    m_volumes.clear();

    // Clean up all transfer functions
    for (auto& tf : m_transfer_functions) {
        if (tf) {
            cleanup_transfer_function(*tf);
        }
    }
    m_transfer_functions.clear();

    // Clean up staging buffer
    if (m_staging_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_staging_buffer, m_staging_allocation);
        m_staging_buffer = VK_NULL_HANDLE;
        m_staging_allocation = VK_NULL_HANDLE;
        m_staging_mapped_ptr = nullptr;
    }

    // Clean up command pool
    if (m_transfer_command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_transfer_command_pool, nullptr);
        m_transfer_command_pool = VK_NULL_HANDLE;
    }

    m_initialized = false;
    md_logf(MD_LOG_TYPE_INFO, "VulkanVolumeDataManager shutdown complete");
}

bool VulkanVolumeDataManager::load_volume(const VolumeDataDesc& desc, uint32_t& out_volume_id) {
    if (!m_initialized) {
        md_logf(MD_LOG_TYPE_ERROR, "VulkanVolumeDataManager not initialized");
        return false;
    }

    auto volume = std::make_unique<VolumeData>();
    volume->id = m_next_volume_id++;
    volume->desc = desc;
    volume->current_lod_level = 0;
    volume->adaptive_lod_enabled = false;
    volume->target_frame_time = 16.67f;
    volume->current_time_step = 0;
    volume->is_loaded = false;
    volume->last_access_time = static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());

    // Create volume texture
    if (!create_volume_texture(*volume)) {
        md_logf(MD_LOG_TYPE_ERROR, "Failed to create volume texture for volume %u", volume->id);
        return false;
    }

    // Upload volume data
    if (desc.data && desc.data_size > 0) {
        if (!upload_volume_data(*volume, desc.data, desc.data_size)) {
            md_logf(MD_LOG_TYPE_ERROR, "Failed to upload volume data for volume %u", volume->id);
            cleanup_volume_data(*volume);
            return false;
        }
    }

    // Create LOD pyramid if requested
    if (desc.streaming == StreamingStrategy::LOD_PYRAMID && desc.max_lod_levels > 1) {
        if (!create_lod_pyramid(*volume)) {
            md_logf(MD_LOG_TYPE_WARNING, "Failed to create LOD pyramid for volume %u", volume->id);
            // Continue without LOD pyramid
        }
    }

    volume->is_loaded = true;
    out_volume_id = volume->id;
    
    // Update metrics
    update_memory_usage();
    
    m_volumes.push_back(std::move(volume));
    
    md_logf(MD_LOG_TYPE_INFO, "Successfully loaded volume %u (%ux%ux%u)", 
           out_volume_id, desc.width, desc.height, desc.depth);
    
    return true;
}

bool VulkanVolumeDataManager::update_volume(uint32_t volume_id, const void* data, size_t data_size) {
    VolumeData* volume = find_volume(volume_id);
    if (!volume) {
        md_logf(MD_LOG_TYPE_ERROR, "Volume %u not found", volume_id);
        return false;
    }

    volume->last_access_time = static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());

    return upload_volume_data(*volume, data, data_size);
}

void VulkanVolumeDataManager::unload_volume(uint32_t volume_id) {
    auto it = std::find_if(m_volumes.begin(), m_volumes.end(),
        [volume_id](const std::unique_ptr<VolumeData>& vol) {
            return vol->id == volume_id;
        });

    if (it != m_volumes.end()) {
        cleanup_volume_data(**it);
        m_volumes.erase(it);
        update_memory_usage();
        md_logf(MD_LOG_TYPE_INFO, "Unloaded volume %u", volume_id);
    }
}

void VulkanVolumeDataManager::set_lod_level(uint32_t volume_id, uint32_t lod_level) {
    VolumeData* volume = find_volume(volume_id);
    if (!volume) {
        return;
    }

    lod_level = std::min(lod_level, static_cast<uint32_t>(volume->lod_textures.size()));
    volume->current_lod_level = lod_level;
    volume->adaptive_lod_enabled = false;
}

void VulkanVolumeDataManager::set_adaptive_lod(uint32_t volume_id, bool enabled, float target_frame_time_ms) {
    VolumeData* volume = find_volume(volume_id);
    if (!volume) {
        return;
    }

    volume->adaptive_lod_enabled = enabled;
    volume->target_frame_time = target_frame_time_ms;
}

VkImage VulkanVolumeDataManager::get_volume_texture(uint32_t volume_id) const {
    for (const auto& volume : m_volumes) {
        if (volume->id == volume_id) {
            if (!volume->lod_textures.empty() && volume->current_lod_level < volume->lod_textures.size()) {
                return volume->lod_textures[volume->current_lod_level];
            }
            return volume->texture;
        }
    }
    return VK_NULL_HANDLE;
}

VkImageView VulkanVolumeDataManager::get_volume_texture_view(uint32_t volume_id) const {
    for (const auto& volume : m_volumes) {
        if (volume->id == volume_id) {
            if (!volume->lod_texture_views.empty() && volume->current_lod_level < volume->lod_texture_views.size()) {
                return volume->lod_texture_views[volume->current_lod_level];
            }
            return volume->texture_view;
        }
    }
    return VK_NULL_HANDLE;
}

VkSampler VulkanVolumeDataManager::get_volume_sampler(uint32_t volume_id) const {
    for (const auto& volume : m_volumes) {
        if (volume->id == volume_id) {
            return volume->sampler;
        }
    }
    return VK_NULL_HANDLE;
}

// Private methods implementation

bool VulkanVolumeDataManager::create_staging_buffer(size_t size) {
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocation_info;
    if (vmaCreateBuffer(m_allocator, &buffer_info, &alloc_info, &m_staging_buffer, &m_staging_allocation, &allocation_info) != VK_SUCCESS) {
        return false;
    }

    m_staging_mapped_ptr = allocation_info.pMappedData;
    return true;
}

bool VulkanVolumeDataManager::create_volume_texture(VolumeData& volume) {
    const auto& desc = volume.desc;

    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_3D;
    image_info.extent.width = desc.width;
    image_info.extent.height = desc.height;
    image_info.extent.depth = desc.depth;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = desc.format;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(m_allocator, &image_info, &alloc_info, &volume.texture, &volume.allocation, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Create image view
    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = volume.texture;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_3D;
    view_info.format = desc.format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_device, &view_info, nullptr, &volume.texture_view) != VK_SUCCESS) {
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
    sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(m_device, &sampler_info, nullptr, &volume.sampler) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool VulkanVolumeDataManager::create_lod_pyramid(VolumeData& volume) {
    // TODO: Implement LOD pyramid generation
    // This would create multiple texture levels with reduced resolution
    return true;
}

bool VulkanVolumeDataManager::upload_volume_data(VolumeData& volume, const void* data, size_t data_size) {
    if (data_size > m_staging_buffer_size) {
        md_logf(MD_LOG_TYPE_ERROR, "Volume data size (%zu) exceeds staging buffer size (%zu)", data_size, m_staging_buffer_size);
        return false;
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    // Copy data to staging buffer
    memcpy(m_staging_mapped_ptr, data, data_size);

    // Begin command buffer
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(m_transfer_command_buffer, &begin_info);

    // Transition image to transfer destination layout
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = volume.texture;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(m_transfer_command_buffer,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Copy buffer to image
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {volume.desc.width, volume.desc.height, volume.desc.depth};

    vkCmdCopyBufferToImage(m_transfer_command_buffer, m_staging_buffer, volume.texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition image to shader read layout
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(m_transfer_command_buffer,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(m_transfer_command_buffer);

    // Submit and wait
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &m_transfer_command_buffer;

    vkQueueSubmit(m_transfer_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_transfer_queue);

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    volume.metrics.last_upload_time_ms = duration.count() / 1000.0f;

    md_logf(MD_LOG_TYPE_INFO, "Volume data upload completed in %.2f ms", volume.metrics.last_upload_time_ms);
    return true;
}

void VulkanVolumeDataManager::cleanup_volume_data(VolumeData& volume) {
    if (volume.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, volume.sampler, nullptr);
        volume.sampler = VK_NULL_HANDLE;
    }

    if (volume.texture_view != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, volume.texture_view, nullptr);
        volume.texture_view = VK_NULL_HANDLE;
    }

    if (volume.texture != VK_NULL_HANDLE) {
        vmaDestroyImage(m_allocator, volume.texture, volume.allocation);
        volume.texture = VK_NULL_HANDLE;
        volume.allocation = VK_NULL_HANDLE;
    }

    // Clean up LOD textures
    for (size_t i = 0; i < volume.lod_texture_views.size(); ++i) {
        if (volume.lod_texture_views[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device, volume.lod_texture_views[i], nullptr);
        }
    }
    volume.lod_texture_views.clear();

    for (size_t i = 0; i < volume.lod_textures.size(); ++i) {
        if (volume.lod_textures[i] != VK_NULL_HANDLE && i < volume.lod_allocations.size()) {
            vmaDestroyImage(m_allocator, volume.lod_textures[i], volume.lod_allocations[i]);
        }
    }
    volume.lod_textures.clear();
    volume.lod_allocations.clear();
}

VulkanVolumeDataManager::VolumeData* VulkanVolumeDataManager::find_volume(uint32_t volume_id) {
    for (auto& volume : m_volumes) {
        if (volume->id == volume_id) {
            return volume.get();
        }
    }
    return nullptr;
}

void VulkanVolumeDataManager::update_memory_usage() {
    m_current_memory_usage = 0;
    for (const auto& volume : m_volumes) {
        VmaAllocationInfo alloc_info;
        vmaGetAllocationInfo(m_allocator, volume->allocation, &alloc_info);
        volume->metrics.gpu_memory_used = alloc_info.size;
        m_current_memory_usage += alloc_info.size;
    }
}

VolumeDataMetrics VulkanVolumeDataManager::get_metrics(uint32_t volume_id) const {
    for (const auto& volume : m_volumes) {
        if (volume->id == volume_id) {
            return volume->metrics;
        }
    }
    return {};
}

void VulkanVolumeDataManager::set_memory_budget(size_t budget_bytes) {
    m_memory_budget = budget_bytes;
    
    // Trigger garbage collection if over budget
    if (m_current_memory_usage > m_memory_budget) {
        const_cast<VulkanVolumeDataManager*>(this)->garbage_collect();
    }
}

void VulkanVolumeDataManager::garbage_collect() {
    // Simple LRU eviction strategy
    if (m_current_memory_usage <= m_memory_budget) {
        return;
    }

    // Sort volumes by last access time
    std::vector<std::pair<float, uint32_t>> lru_list;
    for (const auto& volume : m_volumes) {
        lru_list.push_back({volume->last_access_time, volume->id});
    }

    std::sort(lru_list.begin(), lru_list.end());

    // Remove least recently used volumes until under budget
    for (const auto& entry : lru_list) {
        if (m_current_memory_usage <= m_memory_budget) {
            break;
        }
        unload_volume(entry.second);
    }

    md_logf(MD_LOG_TYPE_INFO, "Garbage collection completed. Memory usage: %zu / %zu bytes", 
           m_current_memory_usage, m_memory_budget);
}

// TODO: Implement remaining methods for transfer functions, temporal volumes, etc.

} // namespace volume