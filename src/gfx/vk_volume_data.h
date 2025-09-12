#pragma once

#include <core/md_vec_math.h>
#include <gfx/vk_utils.h>
#include <vulkan/vulkan.h>

#include <memory>
#include <vector>

// Forward declarations
struct VmaAllocator_T;
typedef VmaAllocator_T* VmaAllocator;

namespace volume {

// Enhanced volume data management for Agent B Task 3.4
// Focuses on optimized data upload, streaming, and memory management

enum class CompressionType {
    NONE,
    LZ4,
    ZSTD,
    BLOCK_COMPRESSION
};

enum class StreamingStrategy {
    IMMEDIATE,      // Load all data immediately
    LOD_PYRAMID,    // Level-of-detail based streaming
    TEMPORAL,       // Time-based streaming for animations
    ADAPTIVE        // Adaptive based on performance
};

struct VolumeDataDesc {
    uint32_t width, height, depth;
    VkFormat format;
    uint32_t bytes_per_voxel;
    uint32_t time_steps;
    
    // Data source
    const void* data;
    size_t data_size;
    
    // Optimization settings
    CompressionType compression = CompressionType::NONE;
    StreamingStrategy streaming = StreamingStrategy::IMMEDIATE;
    
    // LOD settings
    uint32_t max_lod_levels = 4;
    float lod_bias = 0.0f;
    
    // Memory budget (in bytes)
    size_t memory_budget = 512 * 1024 * 1024; // 512MB default
};

struct VolumeDataMetrics {
    size_t total_memory_used;
    size_t gpu_memory_used;
    size_t compressed_size;
    float compression_ratio;
    uint32_t active_lod_level;
    uint32_t loaded_time_steps;
    float last_upload_time_ms;
};

class VulkanVolumeDataManager {
public:
    VulkanVolumeDataManager();
    ~VulkanVolumeDataManager();

    // Initialization
    bool initialize(VkDevice device, VmaAllocator allocator, VkQueue transfer_queue, uint32_t transfer_queue_family);
    void shutdown();

    // Volume data management
    bool load_volume(const VolumeDataDesc& desc, uint32_t& out_volume_id);
    bool update_volume(uint32_t volume_id, const void* data, size_t data_size);
    void unload_volume(uint32_t volume_id);

    // LOD management
    void set_lod_level(uint32_t volume_id, uint32_t lod_level);
    void set_adaptive_lod(uint32_t volume_id, bool enabled, float target_frame_time_ms = 16.67f);

    // Temporal volume management
    bool load_temporal_volume(const VolumeDataDesc& desc, const std::vector<const void*>& time_step_data, uint32_t& out_volume_id);
    void set_active_time_step(uint32_t volume_id, uint32_t time_step);
    void preload_time_steps(uint32_t volume_id, uint32_t start_step, uint32_t count);

    // Memory management
    void set_memory_budget(size_t budget_bytes);
    void garbage_collect();
    VolumeDataMetrics get_metrics(uint32_t volume_id) const;

    // Transfer function management
    bool upload_transfer_function(const vec4_t* colors, uint32_t count, uint32_t& out_tf_id);
    void bind_transfer_function(uint32_t volume_id, uint32_t tf_id);

    // Access textures for rendering
    VkImage get_volume_texture(uint32_t volume_id) const;
    VkImageView get_volume_texture_view(uint32_t volume_id) const;
    VkSampler get_volume_sampler(uint32_t volume_id) const;
    
    VkImage get_transfer_function_texture(uint32_t tf_id) const;
    VkImageView get_transfer_function_texture_view(uint32_t tf_id) const;
    VkSampler get_transfer_function_sampler(uint32_t tf_id) const;

private:
    struct VolumeData {
        uint32_t id;
        VolumeDataDesc desc;
        
        // Main texture
        VkImage texture;
        VkImageView texture_view;
        VkSampler sampler;
        VmaAllocation allocation;
        
        // LOD pyramid
        std::vector<VkImage> lod_textures;
        std::vector<VkImageView> lod_texture_views;
        std::vector<VmaAllocation> lod_allocations;
        uint32_t current_lod_level;
        bool adaptive_lod_enabled;
        float target_frame_time;
        
        // Temporal data
        std::vector<VkImage> temporal_textures;
        std::vector<VkImageView> temporal_texture_views;
        std::vector<VmaAllocation> temporal_allocations;
        uint32_t current_time_step;
        
        // Compressed data cache
        std::vector<uint8_t> compressed_data;
        
        // Metrics
        VolumeDataMetrics metrics;
        
        // State
        bool is_loaded;
        float last_access_time;
    };

    struct TransferFunction {
        uint32_t id;
        VkImage texture;
        VkImageView texture_view;
        VkSampler sampler;
        VmaAllocation allocation;
        uint32_t size;
        bool is_loaded;
    };

    VkDevice m_device;
    VmaAllocator m_allocator;
    VkQueue m_transfer_queue;
    uint32_t m_transfer_queue_family;
    
    // Command buffer for transfers
    VkCommandPool m_transfer_command_pool;
    VkCommandBuffer m_transfer_command_buffer;
    
    // Volume storage
    std::vector<std::unique_ptr<VolumeData>> m_volumes;
    std::vector<std::unique_ptr<TransferFunction>> m_transfer_functions;
    uint32_t m_next_volume_id;
    uint32_t m_next_tf_id;
    
    // Memory management
    size_t m_memory_budget;
    size_t m_current_memory_usage;
    
    // Staging resources
    VkBuffer m_staging_buffer;
    VmaAllocation m_staging_allocation;
    void* m_staging_mapped_ptr;
    size_t m_staging_buffer_size;
    
    bool m_initialized;

    // Private methods
    bool create_staging_buffer(size_t size);
    bool create_volume_texture(VolumeData& volume);
    bool create_lod_pyramid(VolumeData& volume);
    bool upload_volume_data(VolumeData& volume, const void* data, size_t data_size);
    bool upload_volume_data_async(VolumeData& volume, const void* data, size_t data_size);
    
    bool create_transfer_function_texture(TransferFunction& tf, const vec4_t* colors, uint32_t count);
    
    void* compress_data(const void* data, size_t data_size, CompressionType type, size_t& out_compressed_size);
    bool decompress_data(const void* compressed_data, size_t compressed_size, CompressionType type, void* out_data, size_t out_data_size);
    
    void update_lod_level(VolumeData& volume, float frame_time_ms);
    void cleanup_volume_data(VolumeData& volume);
    void cleanup_transfer_function(TransferFunction& tf);
    
    VolumeData* find_volume(uint32_t volume_id);
    TransferFunction* find_transfer_function(uint32_t tf_id);
    
    void update_memory_usage();
    void evict_least_recently_used();
};

// Utility functions for data compression and optimization
namespace compression {
    
bool compress_volume_data(const void* input_data, size_t input_size, 
                         CompressionType type, std::vector<uint8_t>& output_data);

bool decompress_volume_data(const std::vector<uint8_t>& compressed_data,
                           CompressionType type, void* output_data, size_t output_size);

size_t estimate_compressed_size(size_t input_size, CompressionType type);

} // namespace compression

// LOD generation utilities
namespace lod {

bool generate_lod_pyramid(const void* source_data, uint32_t width, uint32_t height, uint32_t depth,
                         VkFormat format, uint32_t max_levels, std::vector<std::vector<uint8_t>>& out_lod_data);

uint32_t calculate_optimal_lod_level(uint32_t screen_coverage_pixels, uint32_t volume_resolution, float quality_bias);

} // namespace lod

} // namespace volume