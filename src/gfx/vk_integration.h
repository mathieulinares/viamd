#pragma once

// Agent B Task 3.4 & Agent C Task 3.5 Integration
// Integration utilities for enhanced volume data management and post-processing

#include <gfx/vk_volume_data.h>
#include <gfx/vk_postprocess.h>
#include <core/md_vec_math.h>

namespace integration {

// Enhanced volume rendering integration
class VulkanVolumeRenderingSystem {
public:
    VulkanVolumeRenderingSystem();
    ~VulkanVolumeRenderingSystem();

    bool initialize(VkDevice device, VmaAllocator allocator, VkQueue transfer_queue, uint32_t transfer_queue_family);
    void shutdown();

    // High-level volume rendering with optimized data management
    bool load_volume_dataset(const char* file_path, const volume::VolumeDataDesc& desc, uint32_t& out_volume_id);
    void render_volume_with_post_processing(VkCommandBuffer cmd, uint32_t volume_id, 
                                          const volume::VulkanRenderDesc& volume_desc,
                                          const postprocessing::VulkanPostProcessDesc& post_desc,
                                          const postprocessing::EffectType* effects, size_t effect_count);

    // Performance optimization controls
    void set_adaptive_quality(bool enabled, float target_fps = 60.0f);
    void set_memory_budget(size_t memory_bytes);
    
    // Metrics and monitoring
    struct RenderMetrics {
        float volume_render_time_ms;
        float post_process_time_ms;
        float total_frame_time_ms;
        size_t memory_usage_bytes;
        uint32_t active_lod_level;
    };
    
    RenderMetrics get_render_metrics() const;

private:
    volume::VulkanVolumeDataManager* m_volume_data_manager;
    postprocessing::VulkanPostProcessingPipeline* m_post_processing;
    
    // Performance monitoring
    mutable RenderMetrics m_metrics;
    bool m_adaptive_quality_enabled;
    float m_target_fps;
    
    bool m_initialized;
};

// Post-processing effect chain builder
class PostProcessingChainBuilder {
public:
    PostProcessingChainBuilder();
    
    // Chainable effect configuration
    PostProcessingChainBuilder& add_ssao(float radius = 0.5f, float bias = 0.025f, int samples = 16);
    PostProcessingChainBuilder& add_dof(float focus_distance = 10.0f, float aperture = 2.8f);
    PostProcessingChainBuilder& add_fxaa(float contrast_threshold = 0.0312f, float relative_threshold = 0.063f);
    PostProcessingChainBuilder& add_tone_mapping();
    
    // Build final effect chain
    void build(postprocessing::EffectType* out_effects, size_t& out_count, 
              postprocessing::VulkanPostProcessDesc& out_desc) const;
    
    // Preset configurations
    static PostProcessingChainBuilder create_quality_preset();
    static PostProcessingChainBuilder create_performance_preset();
    static PostProcessingChainBuilder create_volume_preset();

private:
    struct EffectConfig {
        postprocessing::EffectType type;
        postprocessing::VulkanPostProcessDesc::params_union params;
    };
    
    std::vector<EffectConfig> m_effects;
    postprocessing::VulkanPostProcessDesc m_base_desc;
};

// Utility functions for seamless integration
namespace utils {

// Automatic format conversion for volume data
VkFormat determine_optimal_volume_format(const void* data, uint32_t bytes_per_voxel, bool is_signed = false);

// Memory budget calculation based on system resources
size_t calculate_optimal_memory_budget();

// Performance profiling helpers
class PerformanceProfiler {
public:
    void begin_frame();
    void begin_section(const char* name);
    void end_section();
    void end_frame();
    
    struct ProfileData {
        const char* name;
        float time_ms;
        float percentage;
    };
    
    const std::vector<ProfileData>& get_frame_data() const;
    float get_average_frame_time() const;

private:
    // Implementation details
    struct SectionData {
        const char* name;
        std::chrono::high_resolution_clock::time_point start_time;
        float accumulated_time;
    };
    
    std::vector<SectionData> m_sections;
    std::vector<ProfileData> m_frame_data;
    std::chrono::high_resolution_clock::time_point m_frame_start;
    float m_average_frame_time;
    size_t m_frame_count;
};

} // namespace utils

} // namespace integration