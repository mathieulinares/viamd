#pragma once

#ifdef VIAMD_ENABLE_VULKAN

#include <vulkan/vulkan.h>
#include <core/md_vec_math.h>
#include <vector>
#include <memory>

namespace viamd {
namespace gfx {

// Forward declarations
class VulkanContext;
class VulkanPipeline;
class VulkanBuffer;
class DynamicBufferPool;

// Vertex format for immediate drawing
struct ImmediateVertex {
    vec3_t position;
    uint32_t color;
};

// Draw command for batching
struct ImmediateDrawCommand {
    uint32_t vertex_offset = 0;
    uint32_t index_offset = 0;
    uint32_t index_count = 0;
    uint32_t view_matrix_idx = 0;
    uint32_t proj_matrix_idx = 0;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
};

// Uniform buffer data
struct ImmediateUniforms {
    mat4_t mvp_matrix;
    mat4_t normal_matrix;  // Actually mat3 but padded to mat4 for alignment
    float point_size = 1.0f;
    float _padding[3];  // Explicit padding for alignment
};

class VulkanImmediateRenderer {
public:
    VulkanImmediateRenderer();
    ~VulkanImmediateRenderer();

    // Non-copyable
    VulkanImmediateRenderer(const VulkanImmediateRenderer&) = delete;
    VulkanImmediateRenderer& operator=(const VulkanImmediateRenderer&) = delete;

    bool initialize(VulkanContext* context, VkRenderPass render_pass, uint32_t max_frames_in_flight = 3);
    void cleanup();

    // Matrix management (same interface as OpenGL version)
    void set_model_view_matrix(const mat4_t& model_view_matrix);
    void set_proj_matrix(const mat4_t& proj_matrix);

    // Primitive drawing functions (same interface as OpenGL version)
    void draw_point(vec3_t pos, uint32_t color);
    void draw_line(vec3_t from, vec3_t to, uint32_t color);
    void draw_triangle(vec3_t p0, vec3_t p1, vec3_t p2, uint32_t color);

    // Batch drawing functions
    void draw_points_v(const ImmediateVertex vertices[], size_t count, vec4_t color_mult = {1,1,1,1});
    void draw_lines_v(const ImmediateVertex vertices[], size_t count, vec4_t color_mult = {1,1,1,1});
    void draw_triangles_v(const ImmediateVertex vertices[], size_t count, vec4_t color_mult = {1,1,1,1});

    // Render all accumulated commands
    void render(VkCommandBuffer command_buffer, uint32_t frame_index);

    // Clear frame data
    void reset_frame(uint32_t frame_index);

private:
    bool create_shaders();
    bool create_pipeline(VkRenderPass render_pass);
    bool create_buffers();
    bool create_descriptor_sets();
    
    void add_vertex(const ImmediateVertex& vertex, vec4_t color_mult = {1,1,1,1});
    void add_draw_command(uint32_t vertex_count, VkPrimitiveTopology topology);
    
    bool update_buffers(uint32_t frame_index);
    void bind_and_draw(VkCommandBuffer command_buffer, uint32_t frame_index);

    VulkanContext* context_ = nullptr;
    std::unique_ptr<VulkanPipeline> pipeline_;
    
    // Vertex data
    std::vector<ImmediateVertex> vertices_;
    std::vector<uint32_t> indices_;
    std::vector<ImmediateDrawCommand> draw_commands_;
    std::vector<mat4_t> matrix_stack_;
    
    // Per-frame resources
    struct FrameData {
        std::unique_ptr<VulkanBuffer> vertex_buffer;
        std::unique_ptr<VulkanBuffer> index_buffer;
        std::unique_ptr<VulkanBuffer> uniform_buffer;
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    };
    std::vector<FrameData> frame_data_;
    
    // Pipeline resources
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    
    // Current state
    uint32_t current_view_matrix_idx_ = 0;
    uint32_t current_proj_matrix_idx_ = 0;
    uint32_t max_frames_in_flight_ = 3;
    
    // Buffer sizes
    static constexpr VkDeviceSize MAX_VERTICES = 100000;
    static constexpr VkDeviceSize MAX_INDICES = 300000; // For triangles mostly
};

} // namespace gfx
} // namespace viamd

#endif // VIAMD_ENABLE_VULKAN