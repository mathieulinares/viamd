#include "vk_pipeline.h"

#ifdef VIAMD_ENABLE_VULKAN

#include "vk_utils.h"
#include <iostream>
#include <stdexcept>

namespace viamd {
namespace gfx {

VulkanPipeline::VulkanPipeline() = default;

VulkanPipeline::~VulkanPipeline() {
    // Note: cleanup should be called explicitly with device
}

VulkanPipeline::VulkanPipeline(VulkanPipeline&& other) noexcept
    : pipeline_(other.pipeline_)
    , pipeline_layout_(other.pipeline_layout_)
    , shader_modules_(std::move(other.shader_modules_))
{
    other.pipeline_ = VK_NULL_HANDLE;
    other.pipeline_layout_ = VK_NULL_HANDLE;
}

VulkanPipeline& VulkanPipeline::operator=(VulkanPipeline&& other) noexcept {
    if (this != &other) {
        pipeline_ = other.pipeline_;
        pipeline_layout_ = other.pipeline_layout_;
        shader_modules_ = std::move(other.shader_modules_);
        
        other.pipeline_ = VK_NULL_HANDLE;
        other.pipeline_layout_ = VK_NULL_HANDLE;
    }
    return *this;
}

bool VulkanPipeline::create_graphics_pipeline(VkDevice device, const GraphicsPipelineCreateInfo& create_info) {
    // Clean up any existing pipeline
    cleanup(device);

    // Compile shaders
    if (!compile_shaders(device, create_info.shader_stages)) {
        std::cerr << "Failed to compile shaders for graphics pipeline" << std::endl;
        return false;
    }

    // Create shader stage info
    std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
    for (size_t i = 0; i < create_info.shader_stages.size(); ++i) {
        VkPipelineShaderStageCreateInfo stage_info{};
        stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage_info.stage = create_info.shader_stages[i].stage;
        stage_info.module = shader_modules_[i];
        stage_info.pName = create_info.shader_stages[i].entry_point.c_str();
        shader_stages.push_back(stage_info);
    }

    // Vertex input state
    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = static_cast<uint32_t>(create_info.vertex_binding_descriptions.size());
    vertex_input_info.pVertexBindingDescriptions = create_info.vertex_binding_descriptions.data();
    vertex_input_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(create_info.vertex_attribute_descriptions.size());
    vertex_input_info.pVertexAttributeDescriptions = create_info.vertex_attribute_descriptions.data();

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = create_info.topology;
    input_assembly.primitiveRestartEnable = create_info.primitive_restart_enable;

    // Viewport state
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(create_info.viewport_extent.width);
    viewport.height = static_cast<float>(create_info.viewport_extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = create_info.viewport_extent;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    // Rasterization state
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = create_info.polygon_mode;
    rasterizer.lineWidth = create_info.line_width;
    rasterizer.cullMode = create_info.cull_mode;
    rasterizer.frontFace = create_info.front_face;
    rasterizer.depthBiasEnable = create_info.depth_bias_enable;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = create_info.sample_shading_enable;
    multisampling.rasterizationSamples = create_info.msaa_samples;

    // Depth and stencil testing
    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = create_info.depth_test_enable;
    depth_stencil.depthWriteEnable = create_info.depth_write_enable;
    depth_stencil.depthCompareOp = create_info.depth_compare_op;
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.stencilTestEnable = VK_FALSE;

    // Color blending
    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask = create_info.color_write_mask;
    color_blend_attachment.blendEnable = create_info.blend_enable;
    color_blend_attachment.srcColorBlendFactor = create_info.src_color_blend_factor;
    color_blend_attachment.dstColorBlendFactor = create_info.dst_color_blend_factor;
    color_blend_attachment.colorBlendOp = create_info.color_blend_op;
    color_blend_attachment.srcAlphaBlendFactor = create_info.src_alpha_blend_factor;
    color_blend_attachment.dstAlphaBlendFactor = create_info.dst_alpha_blend_factor;
    color_blend_attachment.alphaBlendOp = create_info.alpha_blend_op;

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.logicOp = VK_LOGIC_OP_COPY;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;
    color_blending.blendConstants[0] = 0.0f;
    color_blending.blendConstants[1] = 0.0f;
    color_blending.blendConstants[2] = 0.0f;
    color_blending.blendConstants[3] = 0.0f;

    // Dynamic state
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = static_cast<uint32_t>(create_info.dynamic_states.size());
    dynamic_state.pDynamicStates = create_info.dynamic_states.data();

    // Pipeline layout (empty for now - will be enhanced by Agent B/C)
    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 0;
    pipeline_layout_info.pushConstantRangeCount = 0;

    if (vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &pipeline_layout_) != VK_SUCCESS) {
        std::cerr << "Failed to create pipeline layout" << std::endl;
        cleanup_shader_modules(device);
        return false;
    }

    // Graphics pipeline
    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = static_cast<uint32_t>(shader_stages.size());
    pipeline_info.pStages = shader_stages.data();
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = create_info.dynamic_states.empty() ? nullptr : &dynamic_state;
    pipeline_info.layout = pipeline_layout_;
    pipeline_info.renderPass = create_info.render_pass;
    pipeline_info.subpass = create_info.subpass;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_) != VK_SUCCESS) {
        std::cerr << "Failed to create graphics pipeline" << std::endl;
        cleanup(device);
        return false;
    }

    return true;
}

bool VulkanPipeline::create_compute_pipeline(VkDevice device, const PipelineShaderStage& compute_shader) {
    // Clean up any existing pipeline
    cleanup(device);

    // Compile shader
    std::vector<PipelineShaderStage> stages = { compute_shader };
    if (!compile_shaders(device, stages)) {
        std::cerr << "Failed to compile compute shader" << std::endl;
        return false;
    }

    // Pipeline layout (empty for now)
    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 0;
    pipeline_layout_info.pushConstantRangeCount = 0;

    if (vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &pipeline_layout_) != VK_SUCCESS) {
        std::cerr << "Failed to create compute pipeline layout" << std::endl;
        cleanup_shader_modules(device);
        return false;
    }

    // Compute pipeline
    VkComputePipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline_info.stage.module = shader_modules_[0];
    pipeline_info.stage.pName = compute_shader.entry_point.c_str();
    pipeline_info.layout = pipeline_layout_;

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_) != VK_SUCCESS) {
        std::cerr << "Failed to create compute pipeline" << std::endl;
        cleanup(device);
        return false;
    }

    return true;
}

void VulkanPipeline::cleanup(VkDevice device) {
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }

    if (pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
        pipeline_layout_ = VK_NULL_HANDLE;
    }

    cleanup_shader_modules(device);
}

bool VulkanPipeline::compile_shaders(VkDevice device, const std::vector<PipelineShaderStage>& stages) {
    cleanup_shader_modules(device);
    shader_modules_.clear();
    shader_modules_.reserve(stages.size());

    for (const auto& stage : stages) {
        std::vector<uint32_t> spirv_code;
        
        // Use provided SPIR-V code if available, otherwise compile from source
        if (!stage.spirv_code.empty()) {
            spirv_code = stage.spirv_code;
        } else if (!stage.source_code.empty()) {
            spirv_code = vk_utils::compile_glsl_to_spirv(stage.source_code, stage.stage);
            if (spirv_code.empty()) {
                std::cerr << "Failed to compile shader stage" << std::endl;
                cleanup_shader_modules(device);
                return false;
            }
        } else {
            std::cerr << "No shader source or SPIR-V code provided" << std::endl;
            cleanup_shader_modules(device);
            return false;
        }

        VkShaderModule shader_module = vk_utils::create_shader_module(device, spirv_code);
        shader_modules_.push_back(shader_module);
    }

    return true;
}

void VulkanPipeline::cleanup_shader_modules(VkDevice device) {
    for (VkShaderModule module : shader_modules_) {
        vkDestroyShaderModule(device, module, nullptr);
    }
    shader_modules_.clear();
}

// Render pass utilities
VkRenderPass RenderPassManager::create_render_pass(VkDevice device, const RenderPassCreateInfo& create_info) {
    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkAttachmentReference> color_refs;
    VkAttachmentReference depth_ref{};

    // Color attachments
    for (size_t i = 0; i < create_info.color_formats.size(); ++i) {
        VkAttachmentDescription color_attachment{};
        color_attachment.format = create_info.color_formats[i];
        color_attachment.samples = create_info.samples;
        color_attachment.loadOp = create_info.color_load_op;
        color_attachment.storeOp = create_info.color_store_op;
        color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        attachments.push_back(color_attachment);

        VkAttachmentReference color_ref{};
        color_ref.attachment = static_cast<uint32_t>(i);
        color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_refs.push_back(color_ref);
    }

    // Depth attachment
    bool has_depth = (create_info.depth_format != VK_FORMAT_UNDEFINED);
    if (has_depth) {
        VkAttachmentDescription depth_attachment{};
        depth_attachment.format = create_info.depth_format;
        depth_attachment.samples = create_info.samples;
        depth_attachment.loadOp = create_info.depth_load_op;
        depth_attachment.storeOp = create_info.depth_store_op;
        depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        attachments.push_back(depth_attachment);

        depth_ref.attachment = static_cast<uint32_t>(attachments.size() - 1);
        depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    // Subpass
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(color_refs.size());
    subpass.pColorAttachments = color_refs.data();
    subpass.pDepthStencilAttachment = has_depth ? &depth_ref : nullptr;

    // Subpass dependencies
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    // Create render pass
    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = static_cast<uint32_t>(attachments.size());
    render_pass_info.pAttachments = attachments.data();
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;

    VkRenderPass render_pass;
    if (vkCreateRenderPass(device, &render_pass_info, nullptr, &render_pass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass!");
    }

    return render_pass;
}

void RenderPassManager::destroy_render_pass(VkDevice device, VkRenderPass render_pass) {
    if (render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, render_pass, nullptr);
    }
}

} // namespace gfx
} // namespace viamd

#endif // VIAMD_ENABLE_VULKAN