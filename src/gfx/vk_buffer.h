#pragma once

#ifdef VIAMD_ENABLE_VULKAN

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <cstring>

namespace viamd {
namespace gfx {

// Forward declarations
class VulkanContext;

struct BufferCreateInfo {
    VkDeviceSize size;
    VkBufferUsageFlags usage;
    VmaMemoryUsage memory_usage;
    VmaAllocationCreateFlags allocation_flags = 0;
    bool persistent_mapped = false;  // Keep buffer mapped for frequent updates
};

class VulkanBuffer {
public:
    VulkanBuffer();
    ~VulkanBuffer();

    // Non-copyable
    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;

    // Move constructible
    VulkanBuffer(VulkanBuffer&& other) noexcept;
    VulkanBuffer& operator=(VulkanBuffer&& other) noexcept;

    bool create(VmaAllocator allocator, const BufferCreateInfo& create_info);
    void cleanup(VmaAllocator allocator);

    // Data operations
    bool upload_data(VmaAllocator allocator, const void* data, VkDeviceSize size, VkDeviceSize offset = 0);
    bool download_data(VmaAllocator allocator, void* data, VkDeviceSize size, VkDeviceSize offset = 0);
    
    // For persistent mapped buffers
    void* map(VmaAllocator allocator);
    void unmap(VmaAllocator allocator);
    void flush(VmaAllocator allocator, VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);

    // Accessors
    VkBuffer get_buffer() const { return buffer_; }
    VkDeviceSize get_size() const { return size_; }
    bool is_mapped() const { return mapped_data_ != nullptr; }
    void* get_mapped_data() const { return mapped_data_; }

private:
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    void* mapped_data_ = nullptr;
    bool persistent_mapped_ = false;
};

// Staging buffer helper for efficient data transfers
class StagingBuffer {
public:
    StagingBuffer();
    ~StagingBuffer();

    bool initialize(VmaAllocator allocator, VkDeviceSize size);
    void cleanup(VmaAllocator allocator);

    // Upload data to device buffer using staging
    bool upload_to_buffer(VmaAllocator allocator, VkDevice device, VkQueue queue, 
                         VkCommandPool command_pool, VulkanBuffer& dst_buffer,
                         const void* data, VkDeviceSize size, VkDeviceSize dst_offset = 0);

    // Upload data to image using staging
    bool upload_to_image(VmaAllocator allocator, VkDevice device, VkQueue queue,
                        VkCommandPool command_pool, VkImage dst_image,
                        const void* data, uint32_t width, uint32_t height,
                        VkFormat format, uint32_t layer_count = 1);

private:
    VulkanBuffer staging_buffer_;
    VkDeviceSize max_size_;
};

// Buffer pool for dynamic allocations (useful for immediate rendering)
class DynamicBufferPool {
public:
    DynamicBufferPool();
    ~DynamicBufferPool();

    bool initialize(VmaAllocator allocator, VkDeviceSize buffer_size, 
                   VkBufferUsageFlags usage, uint32_t buffer_count = 3);
    void cleanup(VmaAllocator allocator);

    // Get a buffer for the current frame
    VulkanBuffer* get_current_buffer(uint32_t frame_index);
    
    // Allocate data in current buffer (returns offset)
    VkDeviceSize allocate(uint32_t frame_index, VkDeviceSize size, VkDeviceSize alignment = 1);
    
    // Reset frame allocations
    void reset_frame(uint32_t frame_index);

private:
    struct FrameBuffer {
        VulkanBuffer buffer;
        VkDeviceSize current_offset = 0;
    };

    std::vector<FrameBuffer> frame_buffers_;
    VkDeviceSize buffer_size_;
};

// Utility functions for common buffer operations
namespace buffer_utils {

// Create vertex buffer
VulkanBuffer create_vertex_buffer(VmaAllocator allocator, VkDevice device, VkQueue queue,
                                 VkCommandPool command_pool, const void* vertices, 
                                 VkDeviceSize size);

// Create index buffer  
VulkanBuffer create_index_buffer(VmaAllocator allocator, VkDevice device, VkQueue queue,
                                VkCommandPool command_pool, const void* indices,
                                VkDeviceSize size);

// Create uniform buffer
VulkanBuffer create_uniform_buffer(VmaAllocator allocator, VkDeviceSize size);

// Create storage buffer
VulkanBuffer create_storage_buffer(VmaAllocator allocator, VkDeviceSize size, 
                                  VkBufferUsageFlags additional_usage = 0);

} // namespace buffer_utils

} // namespace gfx
} // namespace viamd

#endif // VIAMD_ENABLE_VULKAN