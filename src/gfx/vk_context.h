#pragma once

#ifdef VIAMD_ENABLE_VULKAN

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <optional>

namespace viamd {
namespace gfx {

struct QueueFamilyIndices {
    std::optional<uint32_t> graphics_family;
    std::optional<uint32_t> present_family;
    std::optional<uint32_t> compute_family;

    bool is_complete() const {
        return graphics_family.has_value() && present_family.has_value() && compute_family.has_value();
    }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> present_modes;
};

class VulkanContext {
public:
    VulkanContext();
    ~VulkanContext();

    bool initialize(void* window_handle);
    void cleanup();

    VkInstance get_instance() const { return instance_; }
    VkDevice get_device() const { return device_; }
    VkPhysicalDevice get_physical_device() const { return physical_device_; }
    VkSurfaceKHR get_surface() const { return surface_; }
    VkQueue get_graphics_queue() const { return graphics_queue_; }
    VkQueue get_present_queue() const { return present_queue_; }
    VkQueue get_compute_queue() const { return compute_queue_; }
    QueueFamilyIndices get_queue_family_indices() const { return queue_family_indices_; }

    SwapChainSupportDetails query_swapchain_support(VkPhysicalDevice device);

private:
    bool create_instance();
    bool setup_debug_messenger();
    bool create_surface(void* window_handle);
    bool pick_physical_device();
    bool create_logical_device();
    bool initialize_vma();
    
    QueueFamilyIndices find_queue_families(VkPhysicalDevice device);
    bool is_device_suitable(VkPhysicalDevice device);
    bool check_device_extension_support(VkPhysicalDevice device);
    std::vector<const char*> get_required_extensions();
    bool check_validation_layer_support();

    static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
        VkDebugUtilsMessageTypeFlagsEXT message_type,
        const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
        void* user_data);

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;
    VkQueue compute_queue_ = VK_NULL_HANDLE;
    
    QueueFamilyIndices queue_family_indices_;

    const std::vector<const char*> validation_layers_ = {
        "VK_LAYER_KHRONOS_validation"
    };

    const std::vector<const char*> device_extensions_ = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

#ifdef NDEBUG
    const bool enable_validation_layers_ = false;
#else
    const bool enable_validation_layers_ = true;
#endif
};

} // namespace gfx
} // namespace viamd

#endif // VIAMD_ENABLE_VULKAN