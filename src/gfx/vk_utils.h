#pragma once

#ifdef VIAMD_ENABLE_VULKAN

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <string>
#include <vector>

namespace viamd {
namespace gfx {

// Forward declaration
class VulkanContext;

namespace vk_utils {

// Global VMA allocator - will be initialized by VulkanContext
extern VmaAllocator g_vma_allocator;

// Vulkan utility functions
VkCommandBuffer begin_single_time_commands(VkDevice device, VkCommandPool command_pool);
void end_single_time_commands(VkDevice device, VkQueue queue, VkCommandPool command_pool, VkCommandBuffer command_buffer);

uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties);

VkFormat find_supported_format(VkPhysicalDevice physical_device, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
VkFormat find_depth_format(VkPhysicalDevice physical_device);

bool has_stencil_component(VkFormat format);

// SPIR-V compilation utilities
std::vector<uint32_t> compile_glsl_to_spirv(const std::string& glsl_source, VkShaderStageFlagBits stage);
std::vector<uint32_t> load_spirv_from_file(const std::string& filename);

VkShaderModule create_shader_module(VkDevice device, const std::vector<uint32_t>& code);

// VMA buffer and image management
struct VmaBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mapped_data = nullptr;
};

struct VmaImage {
    VkImage image = VK_NULL_HANDLE;
    VkImageView image_view = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
};

// Memory management
bool create_buffer(size_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VmaBuffer& buffer);
void destroy_buffer(VmaBuffer& buffer);

bool create_image(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, 
                  VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VmaImage& image);
void destroy_image(VmaImage& image);

VkImageView create_image_view(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspect_flags);

// Synchronization helpers
struct SyncObjects {
    std::vector<VkSemaphore> image_available_semaphores;
    std::vector<VkSemaphore> render_finished_semaphores;
    std::vector<VkFence> in_flight_fences;
};

bool create_sync_objects(VkDevice device, size_t max_frames_in_flight, SyncObjects& sync);
void destroy_sync_objects(VkDevice device, SyncObjects& sync);

// Debug utilities
void set_debug_name(VkDevice device, VkObjectType object_type, uint64_t object_handle, const char* name);

} // namespace vk_utils
} // namespace gfx
} // namespace viamd

#endif // VIAMD_ENABLE_VULKAN