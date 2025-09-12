// ViaIMD ImGui Vulkan Integration
// This provides a simple interface to switch between OpenGL and Vulkan backends for ImGui

#pragma once

#include <vulkan/vulkan.h>

namespace application {
    struct Context;
}

namespace imgui_integration {

// Backend type selection
enum class Backend {
    OpenGL,
    Vulkan
};

// Initialize ImGui with specified backend
bool initialize(Backend backend, application::Context* ctx, const char* glsl_version = nullptr);

// Cleanup ImGui
void shutdown(Backend backend, application::Context* ctx);

// Start new frame
void new_frame(Backend backend);

// Render ImGui draw data
void render(Backend backend, application::Context* ctx);

// Update and render multi-viewport windows
void render_platform_windows(Backend backend);

// Vulkan-specific initialization data
struct VulkanInitData {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    uint32_t queue_family;
    VkQueue queue;
    VkDescriptorPool descriptor_pool;
    VkRenderPass render_pass;
    uint32_t min_image_count;
    uint32_t image_count;
    VkSampleCountFlagBits msaa_samples;
    VkPipelineCache pipeline_cache;
    uint32_t subpass;
};

// Set Vulkan initialization data (must be called before initialize with Vulkan backend)
void set_vulkan_init_data(const VulkanInitData& init_data);

} // namespace imgui_integration