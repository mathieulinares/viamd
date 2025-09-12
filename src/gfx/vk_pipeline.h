#pragma once

#ifdef VIAMD_ENABLE_VULKAN

#include <vulkan/vulkan.h>
#include <vector>
#include <string>

namespace viamd {
namespace gfx {

struct PipelineShaderStage {
    VkShaderStageFlagBits stage;
    std::string source_code;
    std::string entry_point = "main";
    std::vector<uint32_t> spirv_code;  // Compiled SPIR-V code
};

struct GraphicsPipelineCreateInfo {
    std::vector<PipelineShaderStage> shader_stages;
    
    // Vertex input state
    std::vector<VkVertexInputBindingDescription> vertex_binding_descriptions;
    std::vector<VkVertexInputAttributeDescription> vertex_attribute_descriptions;
    
    // Input assembly
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkBool32 primitive_restart_enable = VK_FALSE;
    
    // Viewport state
    VkExtent2D viewport_extent;
    
    // Rasterization state
    VkPolygonMode polygon_mode = VK_POLYGON_MODE_FILL;
    VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkBool32 depth_bias_enable = VK_FALSE;
    float line_width = 1.0f;
    
    // Multisampling
    VkSampleCountFlagBits msaa_samples = VK_SAMPLE_COUNT_1_BIT;
    VkBool32 sample_shading_enable = VK_FALSE;
    
    // Depth and stencil testing
    VkBool32 depth_test_enable = VK_TRUE;
    VkBool32 depth_write_enable = VK_TRUE;
    VkCompareOp depth_compare_op = VK_COMPARE_OP_LESS;
    
    // Color blending
    VkBool32 blend_enable = VK_FALSE;
    VkBlendFactor src_color_blend_factor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dst_color_blend_factor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp color_blend_op = VK_BLEND_OP_ADD;
    VkBlendFactor src_alpha_blend_factor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dst_alpha_blend_factor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp alpha_blend_op = VK_BLEND_OP_ADD;
    VkColorComponentFlags color_write_mask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    
    // Dynamic state
    std::vector<VkDynamicState> dynamic_states;
    
    // Render pass compatibility
    VkRenderPass render_pass = VK_NULL_HANDLE;
    uint32_t subpass = 0;
};

class VulkanPipeline {
public:
    VulkanPipeline();
    ~VulkanPipeline();

    // Non-copyable
    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;

    // Move constructible
    VulkanPipeline(VulkanPipeline&& other) noexcept;
    VulkanPipeline& operator=(VulkanPipeline&& other) noexcept;

    bool create_graphics_pipeline(VkDevice device, const GraphicsPipelineCreateInfo& create_info);
    bool create_compute_pipeline(VkDevice device, const PipelineShaderStage& compute_shader);
    
    void cleanup(VkDevice device);

    VkPipeline get_pipeline() const { return pipeline_; }
    VkPipelineLayout get_layout() const { return pipeline_layout_; }

private:
    bool compile_shaders(VkDevice device, const std::vector<PipelineShaderStage>& stages);
    void cleanup_shader_modules(VkDevice device);

    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    std::vector<VkShaderModule> shader_modules_;
};

// Render pass utilities for Agent B
struct RenderPassCreateInfo {
    std::vector<VkFormat> color_formats;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    
    // Load/store operations
    VkAttachmentLoadOp color_load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
    VkAttachmentStoreOp color_store_op = VK_ATTACHMENT_STORE_OP_STORE;
    VkAttachmentLoadOp depth_load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
    VkAttachmentStoreOp depth_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
};

class RenderPassManager {
public:
    static VkRenderPass create_render_pass(VkDevice device, const RenderPassCreateInfo& create_info);
    static void destroy_render_pass(VkDevice device, VkRenderPass render_pass);
};

} // namespace gfx
} // namespace viamd

#endif // VIAMD_ENABLE_VULKAN