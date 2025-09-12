// Final Vulkan Rendering System Integration - Agent B Task 3.7
// Unified rendering coordinator that brings together all Vulkan components

#pragma once

#include <gfx/vk_context.h>
#include <gfx/vk_immediate.h>
#include <gfx/vk_volume.h>
#include <gfx/vk_postprocess.h>
#include <gfx/vk_integration.h>
#include <app/imgui_integration.h>
#include <core/md_vec_math.h>

namespace vulkan_system {

// Unified Vulkan rendering system that coordinates all components
class VulkanRenderingSystem {
public:
    VulkanRenderingSystem();
    ~VulkanRenderingSystem();

    // System initialization and cleanup
    bool initialize(uint32_t width, uint32_t height);
    void shutdown();
    void resize(uint32_t width, uint32_t height);

    // Backend switching support
    bool is_initialized() const { return m_initialized; }
    bool supports_features() const;

    // Main rendering pipeline
    struct RenderParams {
        // Camera and view matrices
        mat4_t view_matrix;
        mat4_t proj_matrix;
        mat4_t prev_view_matrix;
        mat4_t prev_proj_matrix;
        
        // Viewport information
        uint32_t viewport_width;
        uint32_t viewport_height;
        vec2_t jitter_offset;
        
        // Background and environment
        vec4_t background_color;
        float background_intensity;
        
        // Post-processing settings
        bool enable_ssao;
        bool enable_dof;
        bool enable_fxaa;
        bool enable_temporal_aa;
        
        // Volume rendering settings
        bool enable_volume_rendering;
        uint32_t volume_texture_id;
        mat4_t volume_transform;
    };

    // Core rendering methods
    void begin_frame();
    void render_immediate_geometry(const RenderParams& params);
    void render_volume_data(const RenderParams& params);
    void apply_post_processing(const RenderParams& params);
    void render_ui(const RenderParams& params);
    void end_frame_and_present();

    // Resource management
    void update_buffers(const void* vertex_data, size_t vertex_size, 
                       const void* index_data, size_t index_size);
    
    // Performance monitoring
    struct PerformanceMetrics {
        float immediate_render_time_ms;
        float volume_render_time_ms;
        float post_process_time_ms;
        float ui_render_time_ms;
        float total_frame_time_ms;
        size_t memory_usage_bytes;
    };
    
    PerformanceMetrics get_performance_metrics() const;

    // Integration with existing systems
    VulkanContext* get_context() { return &m_context; }
    VulkanImmediateRenderer* get_immediate_renderer() { return m_immediate_renderer.get(); }
    integration::VulkanVolumeRenderingSystem* get_volume_system() { return m_volume_system.get(); }

private:
    // Core Vulkan systems
    VulkanContext m_context;
    std::unique_ptr<VulkanImmediateRenderer> m_immediate_renderer;
    std::unique_ptr<integration::VulkanVolumeRenderingSystem> m_volume_system;
    std::unique_ptr<postprocessing::VulkanPostProcessingPipeline> m_post_processing;
    
    // Render targets and framebuffers
    VkRenderPass m_main_render_pass;
    VkFramebuffer m_main_framebuffer;
    
    struct RenderTargets {
        VkImage color_image;
        VkImageView color_view;
        VmaAllocation color_allocation;
        
        VkImage depth_image;
        VkImageView depth_view;
        VmaAllocation depth_allocation;
        
        VkImage normal_image;
        VkImageView normal_view;
        VmaAllocation normal_allocation;
        
        VkImage velocity_image;
        VkImageView velocity_view;
        VmaAllocation velocity_allocation;
        
        uint32_t width, height;
    } m_render_targets;
    
    // Command buffer management
    VkCommandPool m_command_pool;
    VkCommandBuffer m_main_command_buffer;
    
    // Synchronization
    VkSemaphore m_image_available_semaphore;
    VkSemaphore m_render_finished_semaphore;
    VkFence m_in_flight_fence;
    
    // ImGui integration
    imgui_integration::VulkanInitData m_imgui_init_data;
    
    // Performance tracking
    mutable PerformanceMetrics m_metrics;
    std::chrono::high_resolution_clock::time_point m_frame_start_time;
    
    // State tracking
    bool m_initialized;
    uint32_t m_current_width, m_current_height;
    
    // Internal methods
    bool create_render_targets(uint32_t width, uint32_t height);
    void destroy_render_targets();
    bool create_render_pass();
    bool create_framebuffer();
    bool create_sync_objects();
    void update_performance_metrics();
};

// Backend selection and switching utilities
namespace backend_switching {

enum class BackendType {
    OpenGL,
    Vulkan,
    Auto  // Automatically choose best available
};

// Backend capability detection
struct BackendCapabilities {
    bool vulkan_available;
    bool vulkan_supports_required_features;
    bool opengl_available;
    uint32_t vulkan_api_version;
    const char* vulkan_device_name;
    const char* opengl_version_string;
};

BackendCapabilities detect_backend_capabilities();
BackendType recommend_backend(const BackendCapabilities& caps);

// Global backend state management
class BackendManager {
public:
    static BackendManager& instance();
    
    bool initialize_backend(BackendType type, uint32_t width, uint32_t height);
    void shutdown_backend();
    void switch_backend(BackendType new_type);
    
    BackendType get_current_backend() const { return m_current_backend; }
    bool is_vulkan_active() const { return m_current_backend == BackendType::Vulkan && m_vulkan_system; }
    bool is_opengl_active() const { return m_current_backend == BackendType::OpenGL; }
    
    VulkanRenderingSystem* get_vulkan_system() { return m_vulkan_system.get(); }
    
    // Unified rendering interface
    void render_frame(const VulkanRenderingSystem::RenderParams& params);
    void resize_viewport(uint32_t width, uint32_t height);

private:
    BackendManager() = default;
    ~BackendManager() = default;
    
    BackendType m_current_backend = BackendType::OpenGL;
    std::unique_ptr<VulkanRenderingSystem> m_vulkan_system;
    BackendCapabilities m_capabilities;
    bool m_initialized = false;
};

} // namespace backend_switching

} // namespace vulkan_system