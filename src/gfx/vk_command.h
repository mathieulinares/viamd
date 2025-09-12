#pragma once

#ifdef VIAMD_ENABLE_VULKAN

#include <vulkan/vulkan.h>
#include <vector>
#include <functional>

namespace viamd {
namespace gfx {

// Forward declarations
class VulkanContext;

struct CommandBufferCreateInfo {
    VkCommandPool command_pool;
    VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    uint32_t count = 1;
};

class CommandBufferManager {
public:
    CommandBufferManager();
    ~CommandBufferManager();

    // Non-copyable
    CommandBufferManager(const CommandBufferManager&) = delete;
    CommandBufferManager& operator=(const CommandBufferManager&) = delete;

    // Move constructible
    CommandBufferManager(CommandBufferManager&& other) noexcept;
    CommandBufferManager& operator=(CommandBufferManager&& other) noexcept;

    bool initialize(VkDevice device, const CommandBufferCreateInfo& create_info);
    void cleanup(VkDevice device);

    // Command buffer recording
    bool begin_recording(size_t index = 0, VkCommandBufferUsageFlags usage_flags = 0);
    bool end_recording(size_t index = 0);
    
    // Command buffer submission
    bool submit_and_wait(VkQueue queue, size_t index = 0);
    bool submit_async(VkQueue queue, size_t index = 0, 
                     VkSemaphore wait_semaphore = VK_NULL_HANDLE,
                     VkSemaphore signal_semaphore = VK_NULL_HANDLE,
                     VkFence fence = VK_NULL_HANDLE);

    // Immediate command execution
    void execute_immediate(VkDevice device, VkQueue queue, VkCommandPool command_pool,
                          std::function<void(VkCommandBuffer)> commands);

    // Accessors
    VkCommandBuffer get_command_buffer(size_t index = 0) const;
    size_t get_command_buffer_count() const { return command_buffers_.size(); }

private:
    std::vector<VkCommandBuffer> command_buffers_;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
};

// Helper class for single-use command buffers
class SingleTimeCommandBuffer {
public:
    SingleTimeCommandBuffer(VkDevice device, VkQueue queue, VkCommandPool command_pool);
    ~SingleTimeCommandBuffer();

    // Non-copyable and non-movable for simplicity
    SingleTimeCommandBuffer(const SingleTimeCommandBuffer&) = delete;
    SingleTimeCommandBuffer& operator=(const SingleTimeCommandBuffer&) = delete;
    SingleTimeCommandBuffer(SingleTimeCommandBuffer&&) = delete;
    SingleTimeCommandBuffer& operator=(SingleTimeCommandBuffer&&) = delete;

    VkCommandBuffer get() const { return command_buffer_; }

private:
    VkDevice device_;
    VkQueue queue_;
    VkCommandPool command_pool_;
    VkCommandBuffer command_buffer_;
};

// Synchronization helper for command buffer dependencies
struct CommandBufferSyncInfo {
    std::vector<VkSemaphore> wait_semaphores;
    std::vector<VkPipelineStageFlags> wait_stages;
    std::vector<VkSemaphore> signal_semaphores;
    VkFence fence = VK_NULL_HANDLE;
};

// Command buffer recording utilities for common operations
namespace cmd_utils {

// Render pass commands
void begin_render_pass(VkCommandBuffer cmd_buffer, VkRenderPass render_pass, 
                      VkFramebuffer framebuffer, VkExtent2D extent,
                      const std::vector<VkClearValue>& clear_values = {});
void end_render_pass(VkCommandBuffer cmd_buffer);

// Pipeline binding
void bind_graphics_pipeline(VkCommandBuffer cmd_buffer, VkPipeline pipeline);
void bind_compute_pipeline(VkCommandBuffer cmd_buffer, VkPipeline pipeline);

// Buffer and texture operations
void copy_buffer(VkCommandBuffer cmd_buffer, VkBuffer src, VkBuffer dst, VkDeviceSize size, 
                VkDeviceSize src_offset = 0, VkDeviceSize dst_offset = 0);
void copy_buffer_to_image(VkCommandBuffer cmd_buffer, VkBuffer buffer, VkImage image,
                         uint32_t width, uint32_t height, uint32_t layer_count = 1);

// Image layout transitions
void transition_image_layout(VkCommandBuffer cmd_buffer, VkImage image, VkFormat format,
                           VkImageLayout old_layout, VkImageLayout new_layout,
                           uint32_t layer_count = 1, uint32_t mip_levels = 1);

// Synchronization barriers
void insert_memory_barrier(VkCommandBuffer cmd_buffer, 
                          VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage,
                          VkAccessFlags src_access, VkAccessFlags dst_access);

} // namespace cmd_utils

} // namespace gfx
} // namespace viamd

#endif // VIAMD_ENABLE_VULKAN