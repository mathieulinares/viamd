#include "vk_command.h"

#ifdef VIAMD_ENABLE_VULKAN

#include "vk_context.h"
#include "vk_utils.h"
#include <iostream>
#include <stdexcept>

namespace viamd {
namespace gfx {

CommandBufferManager::CommandBufferManager() = default;

CommandBufferManager::~CommandBufferManager() {
    // Note: cleanup should be called explicitly with device
}

CommandBufferManager::CommandBufferManager(CommandBufferManager&& other) noexcept
    : command_buffers_(std::move(other.command_buffers_))
    , command_pool_(other.command_pool_)
{
    other.command_pool_ = VK_NULL_HANDLE;
}

CommandBufferManager& CommandBufferManager::operator=(CommandBufferManager&& other) noexcept {
    if (this != &other) {
        command_buffers_ = std::move(other.command_buffers_);
        command_pool_ = other.command_pool_;
        other.command_pool_ = VK_NULL_HANDLE;
    }
    return *this;
}

bool CommandBufferManager::initialize(VkDevice device, const CommandBufferCreateInfo& create_info) {
    command_pool_ = create_info.command_pool;
    
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = create_info.command_pool;
    alloc_info.level = create_info.level;
    alloc_info.commandBufferCount = create_info.count;

    command_buffers_.resize(create_info.count);
    
    VkResult result = vkAllocateCommandBuffers(device, &alloc_info, command_buffers_.data());
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to allocate command buffers: " << result << std::endl;
        return false;
    }

    return true;
}

void CommandBufferManager::cleanup(VkDevice device) {
    if (!command_buffers_.empty() && command_pool_ != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device, command_pool_, 
                           static_cast<uint32_t>(command_buffers_.size()), 
                           command_buffers_.data());
        command_buffers_.clear();
    }
    command_pool_ = VK_NULL_HANDLE;
}

bool CommandBufferManager::begin_recording(size_t index, VkCommandBufferUsageFlags usage_flags) {
    if (index >= command_buffers_.size()) {
        std::cerr << "Command buffer index out of range" << std::endl;
        return false;
    }

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = usage_flags;

    VkResult result = vkBeginCommandBuffer(command_buffers_[index], &begin_info);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to begin recording command buffer: " << result << std::endl;
        return false;
    }

    return true;
}

bool CommandBufferManager::end_recording(size_t index) {
    if (index >= command_buffers_.size()) {
        std::cerr << "Command buffer index out of range" << std::endl;
        return false;
    }

    VkResult result = vkEndCommandBuffer(command_buffers_[index]);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to end recording command buffer: " << result << std::endl;
        return false;
    }

    return true;
}

bool CommandBufferManager::submit_and_wait(VkQueue queue, size_t index) {
    if (index >= command_buffers_.size()) {
        std::cerr << "Command buffer index out of range" << std::endl;
        return false;
    }

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffers_[index];

    VkResult result = vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to submit command buffer: " << result << std::endl;
        return false;
    }

    result = vkQueueWaitIdle(queue);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to wait for queue idle: " << result << std::endl;
        return false;
    }

    return true;
}

bool CommandBufferManager::submit_async(VkQueue queue, size_t index,
                                       VkSemaphore wait_semaphore,
                                       VkSemaphore signal_semaphore,
                                       VkFence fence) {
    if (index >= command_buffers_.size()) {
        std::cerr << "Command buffer index out of range" << std::endl;
        return false;
    }

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    
    if (wait_semaphore != VK_NULL_HANDLE) {
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &wait_semaphore;
        submit_info.pWaitDstStageMask = wait_stages;
    }

    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffers_[index];

    if (signal_semaphore != VK_NULL_HANDLE) {
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &signal_semaphore;
    }

    VkResult result = vkQueueSubmit(queue, 1, &submit_info, fence);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to submit command buffer async: " << result << std::endl;
        return false;
    }

    return true;
}

void CommandBufferManager::execute_immediate(VkDevice device, VkQueue queue, VkCommandPool command_pool,
                                           std::function<void(VkCommandBuffer)> commands) {
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = command_pool;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer;
    vkAllocateCommandBuffers(device, &alloc_info, &command_buffer);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(command_buffer, &begin_info);
    commands(command_buffer);
    vkEndCommandBuffer(command_buffer);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
}

VkCommandBuffer CommandBufferManager::get_command_buffer(size_t index) const {
    if (index >= command_buffers_.size()) {
        return VK_NULL_HANDLE;
    }
    return command_buffers_[index];
}

// SingleTimeCommandBuffer implementation
SingleTimeCommandBuffer::SingleTimeCommandBuffer(VkDevice device, VkQueue queue, VkCommandPool command_pool)
    : device_(device), queue_(queue), command_pool_(command_pool) {
    
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = command_pool;
    alloc_info.commandBufferCount = 1;

    vkAllocateCommandBuffers(device, &alloc_info, &command_buffer_);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(command_buffer_, &begin_info);
}

SingleTimeCommandBuffer::~SingleTimeCommandBuffer() {
    vkEndCommandBuffer(command_buffer_);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer_;

    vkQueueSubmit(queue_, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);

    vkFreeCommandBuffers(device_, command_pool_, 1, &command_buffer_);
}

// Command buffer utilities
namespace cmd_utils {

void begin_render_pass(VkCommandBuffer cmd_buffer, VkRenderPass render_pass,
                      VkFramebuffer framebuffer, VkExtent2D extent,
                      const std::vector<VkClearValue>& clear_values) {
    VkRenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = render_pass;
    render_pass_info.framebuffer = framebuffer;
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = extent;
    render_pass_info.clearValueCount = static_cast<uint32_t>(clear_values.size());
    render_pass_info.pClearValues = clear_values.data();

    vkCmdBeginRenderPass(cmd_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
}

void end_render_pass(VkCommandBuffer cmd_buffer) {
    vkCmdEndRenderPass(cmd_buffer);
}

void bind_graphics_pipeline(VkCommandBuffer cmd_buffer, VkPipeline pipeline) {
    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
}

void bind_compute_pipeline(VkCommandBuffer cmd_buffer, VkPipeline pipeline) {
    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
}

void copy_buffer(VkCommandBuffer cmd_buffer, VkBuffer src, VkBuffer dst, VkDeviceSize size,
                VkDeviceSize src_offset, VkDeviceSize dst_offset) {
    VkBufferCopy copy_region{};
    copy_region.srcOffset = src_offset;
    copy_region.dstOffset = dst_offset;
    copy_region.size = size;

    vkCmdCopyBuffer(cmd_buffer, src, dst, 1, &copy_region);
}

void copy_buffer_to_image(VkCommandBuffer cmd_buffer, VkBuffer buffer, VkImage image,
                         uint32_t width, uint32_t height, uint32_t layer_count) {
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = layer_count;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(cmd_buffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void transition_image_layout(VkCommandBuffer cmd_buffer, VkImage image, VkFormat format,
                           VkImageLayout old_layout, VkImageLayout new_layout,
                           uint32_t layer_count, uint32_t mip_levels) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mip_levels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = layer_count;

    // Handle depth/stencil formats
    if (format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D32_SFLOAT_S8_UINT || 
        format == VK_FORMAT_D24_UNORM_S8_UINT) {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (vk_utils::has_stencil_component(format)) {
            barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    }

    VkPipelineStageFlags source_stage;
    VkPipelineStageFlags destination_stage;

    // Determine access masks and pipeline stages based on layouts
    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else {
        // Generic transition
        barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        source_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        destination_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(cmd_buffer, source_stage, destination_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void insert_memory_barrier(VkCommandBuffer cmd_buffer,
                          VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage,
                          VkAccessFlags src_access, VkAccessFlags dst_access) {
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = dst_access;

    vkCmdPipelineBarrier(cmd_buffer, src_stage, dst_stage, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

} // namespace cmd_utils

} // namespace gfx
} // namespace viamd

#endif // VIAMD_ENABLE_VULKAN