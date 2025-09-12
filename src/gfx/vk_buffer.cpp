#include "vk_buffer.h"
#include "vk_context.h"
#include "vk_command.h"
#include "vk_utils.h"

#ifdef VIAMD_ENABLE_VULKAN

#include <algorithm>
#include <cassert>

namespace viamd {
namespace gfx {

// VulkanBuffer implementation
VulkanBuffer::VulkanBuffer() = default;

VulkanBuffer::~VulkanBuffer() {
    // Ensure cleanup was called
    assert(buffer_ == VK_NULL_HANDLE && "VulkanBuffer not properly cleaned up");
}

VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept
    : buffer_(other.buffer_)
    , allocation_(other.allocation_)
    , size_(other.size_)
    , mapped_data_(other.mapped_data_)
    , persistent_mapped_(other.persistent_mapped_) {
    
    other.buffer_ = VK_NULL_HANDLE;
    other.allocation_ = VK_NULL_HANDLE;
    other.size_ = 0;
    other.mapped_data_ = nullptr;
    other.persistent_mapped_ = false;
}

VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept {
    if (this != &other) {
        buffer_ = other.buffer_;
        allocation_ = other.allocation_;
        size_ = other.size_;
        mapped_data_ = other.mapped_data_;
        persistent_mapped_ = other.persistent_mapped_;
        
        other.buffer_ = VK_NULL_HANDLE;
        other.allocation_ = VK_NULL_HANDLE;
        other.size_ = 0;
        other.mapped_data_ = nullptr;
        other.persistent_mapped_ = false;
    }
    return *this;
}

bool VulkanBuffer::create(VmaAllocator allocator, const BufferCreateInfo& create_info) {
    VkBufferCreateInfo buffer_create_info{};
    buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_create_info.size = create_info.size;
    buffer_create_info.usage = create_info.usage;
    buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocation_create_info{};
    allocation_create_info.usage = create_info.memory_usage;
    allocation_create_info.flags = create_info.allocation_flags;
    
    if (create_info.persistent_mapped) {
        allocation_create_info.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    VmaAllocationInfo allocation_info;
    VkResult result = vmaCreateBuffer(allocator, &buffer_create_info, &allocation_create_info,
                                     &buffer_, &allocation_, &allocation_info);

    if (result != VK_SUCCESS) {
        return false;
    }

    size_ = create_info.size;
    persistent_mapped_ = create_info.persistent_mapped;
    
    if (persistent_mapped_) {
        mapped_data_ = allocation_info.pMappedData;
    }

    return true;
}

void VulkanBuffer::cleanup(VmaAllocator allocator) {
    if (buffer_ != VK_NULL_HANDLE) {
        if (mapped_data_ && !persistent_mapped_) {
            vmaUnmapMemory(allocator, allocation_);
        }
        
        vmaDestroyBuffer(allocator, buffer_, allocation_);
        buffer_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
        size_ = 0;
        mapped_data_ = nullptr;
        persistent_mapped_ = false;
    }
}

bool VulkanBuffer::upload_data(VmaAllocator allocator, const void* data, VkDeviceSize size, VkDeviceSize offset) {
    if (offset + size > size_) {
        return false;
    }

    void* mapped_memory = nullptr;
    if (persistent_mapped_) {
        mapped_memory = mapped_data_;
    } else {
        VkResult result = vmaMapMemory(allocator, allocation_, &mapped_memory);
        if (result != VK_SUCCESS) {
            return false;
        }
    }

    std::memcpy(static_cast<char*>(mapped_memory) + offset, data, size);

    if (!persistent_mapped_) {
        vmaUnmapMemory(allocator, allocation_);
    } else {
        // Flush for coherent memory
        VmaAllocationInfo allocation_info;
        vmaGetAllocationInfo(allocator, allocation_, &allocation_info);
        
        VkMappedMemoryRange range{};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = allocation_info.deviceMemory;
        range.offset = allocation_info.offset + offset;
        range.size = size;
        
        // Only flush if memory is not coherent
        VkMemoryPropertyFlags memory_flags;
        vmaGetMemoryTypeProperties(allocator, allocation_info.memoryType, &memory_flags);
        if (!(memory_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            vmaFlushAllocation(allocator, allocation_, offset, size);
        }
    }

    return true;
}

bool VulkanBuffer::download_data(VmaAllocator allocator, void* data, VkDeviceSize size, VkDeviceSize offset) {
    if (offset + size > size_) {
        return false;
    }

    void* mapped_memory = nullptr;
    if (persistent_mapped_) {
        mapped_memory = mapped_data_;
        
        // Invalidate for non-coherent memory
        VmaAllocationInfo allocation_info;
        vmaGetAllocationInfo(allocator, allocation_, &allocation_info);
        
        VkMemoryPropertyFlags memory_flags;
        vmaGetMemoryTypeProperties(allocator, allocation_info.memoryType, &memory_flags);
        if (!(memory_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            vmaInvalidateAllocation(allocator, allocation_, offset, size);
        }
    } else {
        VkResult result = vmaMapMemory(allocator, allocation_, &mapped_memory);
        if (result != VK_SUCCESS) {
            return false;
        }
    }

    std::memcpy(data, static_cast<const char*>(mapped_memory) + offset, size);

    if (!persistent_mapped_) {
        vmaUnmapMemory(allocator, allocation_);
    }

    return true;
}

void* VulkanBuffer::map(VmaAllocator allocator) {
    if (persistent_mapped_) {
        return mapped_data_;
    }

    if (mapped_data_ == nullptr) {
        VkResult result = vmaMapMemory(allocator, allocation_, &mapped_data_);
        if (result != VK_SUCCESS) {
            return nullptr;
        }
    }

    return mapped_data_;
}

void VulkanBuffer::unmap(VmaAllocator allocator) {
    if (!persistent_mapped_ && mapped_data_ != nullptr) {
        vmaUnmapMemory(allocator, allocation_);
        mapped_data_ = nullptr;
    }
}

void VulkanBuffer::flush(VmaAllocator allocator, VkDeviceSize offset, VkDeviceSize size) {
    if (size == VK_WHOLE_SIZE) {
        size = size_;
    }
    vmaFlushAllocation(allocator, allocation_, offset, size);
}

// StagingBuffer implementation
StagingBuffer::StagingBuffer() = default;

StagingBuffer::~StagingBuffer() = default;

bool StagingBuffer::initialize(VmaAllocator allocator, VkDeviceSize size) {
    BufferCreateInfo create_info{};
    create_info.size = size;
    create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    create_info.memory_usage = VMA_MEMORY_USAGE_CPU_ONLY;
    create_info.persistent_mapped = true;

    if (!staging_buffer_.create(allocator, create_info)) {
        return false;
    }

    max_size_ = size;
    return true;
}

void StagingBuffer::cleanup(VmaAllocator allocator) {
    staging_buffer_.cleanup(allocator);
    max_size_ = 0;
}

bool StagingBuffer::upload_to_buffer(VmaAllocator allocator, VkDevice device, VkQueue queue,
                                     VkCommandPool command_pool, VulkanBuffer& dst_buffer,
                                     const void* data, VkDeviceSize size, VkDeviceSize dst_offset) {
    if (size > max_size_) {
        return false;
    }

    // Upload data to staging buffer
    if (!staging_buffer_.upload_data(allocator, data, size)) {
        return false;
    }

    // Copy staging buffer to destination buffer
    SingleTimeCommandBuffer cmd_buffer(device, queue, command_pool);
    cmd_utils::copy_buffer(cmd_buffer.get(), staging_buffer_.get_buffer(), 
                          dst_buffer.get_buffer(), size, 0, dst_offset);

    return true;
}

bool StagingBuffer::upload_to_image(VmaAllocator allocator, VkDevice device, VkQueue queue,
                                    VkCommandPool command_pool, VkImage dst_image,
                                    const void* data, uint32_t width, uint32_t height,
                                    VkFormat format, uint32_t layer_count) {
    // Calculate image size based on format
    VkDeviceSize image_size = width * height * layer_count;
    
    // Simple size calculation - should be extended for different formats
    switch (format) {
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            image_size *= 4;
            break;
        case VK_FORMAT_R8G8B8_UNORM:
        case VK_FORMAT_R8G8B8_SRGB:
            image_size *= 3;
            break;
        case VK_FORMAT_R8_UNORM:
            image_size *= 1;
            break;
        default:
            return false; // Unsupported format
    }

    if (image_size > max_size_) {
        return false;
    }

    // Upload data to staging buffer
    if (!staging_buffer_.upload_data(allocator, data, image_size)) {
        return false;
    }

    // Copy staging buffer to image
    SingleTimeCommandBuffer cmd_buffer(device, queue, command_pool);
    cmd_utils::copy_buffer_to_image(cmd_buffer.get(), staging_buffer_.get_buffer(),
                                   dst_image, width, height, layer_count);

    return true;
}

// DynamicBufferPool implementation
DynamicBufferPool::DynamicBufferPool() = default;

DynamicBufferPool::~DynamicBufferPool() = default;

bool DynamicBufferPool::initialize(VmaAllocator allocator, VkDeviceSize buffer_size,
                                  VkBufferUsageFlags usage, uint32_t buffer_count) {
    frame_buffers_.resize(buffer_count);
    buffer_size_ = buffer_size;

    for (auto& frame_buffer : frame_buffers_) {
        BufferCreateInfo create_info{};
        create_info.size = buffer_size;
        create_info.usage = usage;
        create_info.memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        create_info.persistent_mapped = true;

        if (!frame_buffer.buffer.create(allocator, create_info)) {
            return false;
        }
    }

    return true;
}

void DynamicBufferPool::cleanup(VmaAllocator allocator) {
    for (auto& frame_buffer : frame_buffers_) {
        frame_buffer.buffer.cleanup(allocator);
    }
    frame_buffers_.clear();
    buffer_size_ = 0;
}

VulkanBuffer* DynamicBufferPool::get_current_buffer(uint32_t frame_index) {
    if (frame_index >= frame_buffers_.size()) {
        return nullptr;
    }
    return &frame_buffers_[frame_index].buffer;
}

VkDeviceSize DynamicBufferPool::allocate(uint32_t frame_index, VkDeviceSize size, VkDeviceSize alignment) {
    if (frame_index >= frame_buffers_.size()) {
        return VK_WHOLE_SIZE;
    }

    auto& frame_buffer = frame_buffers_[frame_index];
    
    // Align offset
    VkDeviceSize aligned_offset = (frame_buffer.current_offset + alignment - 1) & ~(alignment - 1);
    
    if (aligned_offset + size > buffer_size_) {
        return VK_WHOLE_SIZE; // Not enough space
    }

    frame_buffer.current_offset = aligned_offset + size;
    return aligned_offset;
}

void DynamicBufferPool::reset_frame(uint32_t frame_index) {
    if (frame_index < frame_buffers_.size()) {
        frame_buffers_[frame_index].current_offset = 0;
    }
}

// Utility functions
namespace buffer_utils {

VulkanBuffer create_vertex_buffer(VmaAllocator allocator, VkDevice device, VkQueue queue,
                                 VkCommandPool command_pool, const void* vertices, VkDeviceSize size) {
    // Create staging buffer
    StagingBuffer staging;
    if (!staging.initialize(allocator, size)) {
        return VulkanBuffer{};
    }

    // Create vertex buffer
    VulkanBuffer vertex_buffer;
    BufferCreateInfo create_info{};
    create_info.size = size;
    create_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    create_info.memory_usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (!vertex_buffer.create(allocator, create_info)) {
        staging.cleanup(allocator);
        return VulkanBuffer{};
    }

    // Upload data
    if (!staging.upload_to_buffer(allocator, device, queue, command_pool, vertex_buffer, vertices, size)) {
        vertex_buffer.cleanup(allocator);
        staging.cleanup(allocator);
        return VulkanBuffer{};
    }

    staging.cleanup(allocator);
    return vertex_buffer;
}

VulkanBuffer create_index_buffer(VmaAllocator allocator, VkDevice device, VkQueue queue,
                                VkCommandPool command_pool, const void* indices, VkDeviceSize size) {
    // Create staging buffer
    StagingBuffer staging;
    if (!staging.initialize(allocator, size)) {
        return VulkanBuffer{};
    }

    // Create index buffer
    VulkanBuffer index_buffer;
    BufferCreateInfo create_info{};
    create_info.size = size;
    create_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    create_info.memory_usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (!index_buffer.create(allocator, create_info)) {
        staging.cleanup(allocator);
        return VulkanBuffer{};
    }

    // Upload data
    if (!staging.upload_to_buffer(allocator, device, queue, command_pool, index_buffer, indices, size)) {
        index_buffer.cleanup(allocator);
        staging.cleanup(allocator);
        return VulkanBuffer{};
    }

    staging.cleanup(allocator);
    return index_buffer;
}

VulkanBuffer create_uniform_buffer(VmaAllocator allocator, VkDeviceSize size) {
    VulkanBuffer uniform_buffer;
    BufferCreateInfo create_info{};
    create_info.size = size;
    create_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    create_info.memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    create_info.persistent_mapped = true;

    if (!uniform_buffer.create(allocator, create_info)) {
        return VulkanBuffer{};
    }

    return uniform_buffer;
}

VulkanBuffer create_storage_buffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags additional_usage) {
    VulkanBuffer storage_buffer;
    BufferCreateInfo create_info{};
    create_info.size = size;
    create_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | additional_usage;
    create_info.memory_usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (!storage_buffer.create(allocator, create_info)) {
        return VulkanBuffer{};
    }

    return storage_buffer;
}

} // namespace buffer_utils

} // namespace gfx
} // namespace viamd

#endif // VIAMD_ENABLE_VULKAN