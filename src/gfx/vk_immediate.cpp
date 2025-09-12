#include "vk_immediate.h"
#include "vk_context.h"
#include "vk_pipeline.h"
#include "vk_buffer.h"
#include "vk_utils.h"
#include <color_utils.h>

#ifdef VIAMD_ENABLE_VULKAN

#include <cassert>
#include <algorithm>

namespace viamd {
namespace gfx {

// Vertex shader source (GLSL that will be compiled to SPIR-V)
static const char* vertex_shader_source = R"(
#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in uint in_color;

layout(binding = 0) uniform UniformBufferObject {
    mat4 mvp_matrix;
    mat4 normal_matrix;
    float point_size;
} ubo;

layout(location = 0) out vec4 frag_color;

// Convert packed color to vec4
vec4 unpack_color(uint packed_color) {
    float r = float((packed_color >>  0) & 0xFFu) / 255.0;
    float g = float((packed_color >>  8) & 0xFFu) / 255.0;
    float b = float((packed_color >> 16) & 0xFFu) / 255.0;
    float a = float((packed_color >> 24) & 0xFFu) / 255.0;
    return vec4(r, g, b, a);
}

void main() {
    gl_Position = ubo.mvp_matrix * vec4(in_position, 1.0);
    gl_PointSize = max(ubo.point_size, 200.0 / gl_Position.w);
    frag_color = unpack_color(in_color);
}
)";

// Fragment shader source
static const char* fragment_shader_source = R"(
#version 450

layout(location = 0) in vec4 frag_color;

layout(location = 0) out vec4 out_color_alpha;
layout(location = 1) out vec4 out_normal;

vec4 encode_normal(vec3 n) {
    float p = sqrt(n.z * 8.0 + 8.0);
    return vec4(n.xy / p + 0.5, 0.0, 0.0);
}

void main() {
    out_color_alpha = frag_color;
    out_normal = encode_normal(vec3(0.0, 0.0, 1.0));
}
)";

VulkanImmediateRenderer::VulkanImmediateRenderer() = default;

VulkanImmediateRenderer::~VulkanImmediateRenderer() {
    // Ensure cleanup was called
    assert(context_ == nullptr && "VulkanImmediateRenderer not properly cleaned up");
}

bool VulkanImmediateRenderer::initialize(VulkanContext* context, VkRenderPass render_pass, uint32_t max_frames_in_flight) {
    context_ = context;
    max_frames_in_flight_ = max_frames_in_flight;
    
    if (!create_shaders()) {
        return false;
    }
    
    if (!create_pipeline(render_pass)) {
        return false;
    }
    
    if (!create_buffers()) {
        return false;
    }
    
    if (!create_descriptor_sets()) {
        return false;
    }
    
    // Reserve space for vertex data
    vertices_.reserve(MAX_VERTICES);
    indices_.reserve(MAX_INDICES);
    draw_commands_.reserve(1000);
    matrix_stack_.reserve(100);
    
    return true;
}

void VulkanImmediateRenderer::cleanup() {
    if (context_) {
        VkDevice device = context_->get_device();
        
        // Wait for device to be idle
        vkDeviceWaitIdle(device);
        
        // Cleanup frame data
        for (auto& frame : frame_data_) {
            if (frame.vertex_buffer) {
                frame.vertex_buffer.reset();
            }
            if (frame.index_buffer) {
                frame.index_buffer.reset();
            }
            if (frame.uniform_buffer) {
                frame.uniform_buffer.reset();
            }
        }
        frame_data_.clear();
        
        // Cleanup descriptor pool
        if (descriptor_pool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, descriptor_pool_, nullptr);
            descriptor_pool_ = VK_NULL_HANDLE;
        }
        
        // Cleanup descriptor set layout
        if (descriptor_set_layout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, descriptor_set_layout_, nullptr);
            descriptor_set_layout_ = VK_NULL_HANDLE;
        }
        
        // Cleanup pipeline
        if (pipeline_) {
            pipeline_->cleanup(device);
            pipeline_.reset();
        }
        
        context_ = nullptr;
    }
}

bool VulkanImmediateRenderer::create_shaders() {
    // Shaders will be compiled during pipeline creation
    return true;
}

bool VulkanImmediateRenderer::create_pipeline(VkRenderPass render_pass) {
    pipeline_ = std::make_unique<VulkanPipeline>();
    
    // Create descriptor set layout
    VkDescriptorSetLayoutBinding ubo_binding{};
    ubo_binding.binding = 0;
    ubo_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo_binding.descriptorCount = 1;
    ubo_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    
    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &ubo_binding;
    
    VkDevice device = context_->get_device();
    if (vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &descriptor_set_layout_) != VK_SUCCESS) {
        return false;
    }
    
    // Create graphics pipeline
    GraphicsPipelineCreateInfo create_info{};
    
    // Shader stages
    PipelineShaderStage vertex_stage{};
    vertex_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertex_stage.source_code = vertex_shader_source;
    
    PipelineShaderStage fragment_stage{};
    fragment_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragment_stage.source_code = fragment_shader_source;
    
    create_info.shader_stages = {vertex_stage, fragment_stage};
    
    // Vertex input
    VkVertexInputBindingDescription binding_description{};
    binding_description.binding = 0;
    binding_description.stride = sizeof(ImmediateVertex);
    binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    std::vector<VkVertexInputAttributeDescription> attribute_descriptions(2);
    
    // Position attribute
    attribute_descriptions[0].binding = 0;
    attribute_descriptions[0].location = 0;
    attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_descriptions[0].offset = offsetof(ImmediateVertex, position);
    
    // Color attribute
    attribute_descriptions[1].binding = 0;
    attribute_descriptions[1].location = 1;
    attribute_descriptions[1].format = VK_FORMAT_R8G8B8A8_UNORM;
    attribute_descriptions[1].offset = offsetof(ImmediateVertex, color);
    
    create_info.vertex_binding_descriptions = {binding_description};
    create_info.vertex_attribute_descriptions = attribute_descriptions;
    
    // Input assembly - support multiple topologies
    create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    
    // Viewport - will be set dynamically
    create_info.viewport_extent = {1920, 1080}; // Default, will be overridden
    
    // Rasterization
    create_info.polygon_mode = VK_POLYGON_MODE_FILL;
    create_info.cull_mode = VK_CULL_MODE_NONE; // Immediate drawing typically doesn't cull
    create_info.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    create_info.line_width = 1.0f;
    
    // Multisampling
    create_info.msaa_samples = VK_SAMPLE_COUNT_1_BIT;
    
    // Depth testing
    create_info.depth_test_enable = VK_TRUE;
    create_info.depth_write_enable = VK_TRUE;
    create_info.depth_compare_op = VK_COMPARE_OP_LESS;
    
    // Color blending
    create_info.blend_enable = VK_FALSE; // Could be enabled for transparency
    
    // Dynamic state
    create_info.dynamic_states = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY_EXT // If supported
    };
    
    // Render pass
    create_info.render_pass = render_pass;
    create_info.subpass = 0;
    
    return pipeline_->create_graphics_pipeline(device, create_info);
}

bool VulkanImmediateRenderer::create_buffers() {
    frame_data_.resize(max_frames_in_flight_);
    
    VmaAllocator allocator = vk_utils::get_vma_allocator(); // From vk_utils.h
    
    for (size_t i = 0; i < max_frames_in_flight_; ++i) {
        auto& frame = frame_data_[i];
        
        // Create vertex buffer
        frame.vertex_buffer = std::make_unique<VulkanBuffer>();
        BufferCreateInfo vertex_create_info{};
        vertex_create_info.size = MAX_VERTICES * sizeof(ImmediateVertex);
        vertex_create_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        vertex_create_info.memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        vertex_create_info.persistent_mapped = true;
        
        if (!frame.vertex_buffer->create(allocator, vertex_create_info)) {
            return false;
        }
        
        // Create index buffer
        frame.index_buffer = std::make_unique<VulkanBuffer>();
        BufferCreateInfo index_create_info{};
        index_create_info.size = MAX_INDICES * sizeof(uint32_t);
        index_create_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        index_create_info.memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        index_create_info.persistent_mapped = true;
        
        if (!frame.index_buffer->create(allocator, index_create_info)) {
            return false;
        }
        
        // Create uniform buffer
        frame.uniform_buffer = std::make_unique<VulkanBuffer>();
        BufferCreateInfo uniform_create_info{};
        uniform_create_info.size = sizeof(ImmediateUniforms);
        uniform_create_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        uniform_create_info.memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        uniform_create_info.persistent_mapped = true;
        
        if (!frame.uniform_buffer->create(allocator, uniform_create_info)) {
            return false;
        }
    }
    
    return true;
}

bool VulkanImmediateRenderer::create_descriptor_sets() {
    VkDevice device = context_->get_device();
    
    // Create descriptor pool
    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_size.descriptorCount = max_frames_in_flight_;
    
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    pool_info.maxSets = max_frames_in_flight_;
    
    if (vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool_) != VK_SUCCESS) {
        return false;
    }
    
    // Allocate descriptor sets
    std::vector<VkDescriptorSetLayout> layouts(max_frames_in_flight_, descriptor_set_layout_);
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = descriptor_pool_;
    alloc_info.descriptorSetCount = max_frames_in_flight_;
    alloc_info.pSetLayouts = layouts.data();
    
    std::vector<VkDescriptorSet> descriptor_sets(max_frames_in_flight_);
    if (vkAllocateDescriptorSets(device, &alloc_info, descriptor_sets.data()) != VK_SUCCESS) {
        return false;
    }
    
    // Update descriptor sets
    for (size_t i = 0; i < max_frames_in_flight_; ++i) {
        frame_data_[i].descriptor_set = descriptor_sets[i];
        
        VkDescriptorBufferInfo buffer_info{};
        buffer_info.buffer = frame_data_[i].uniform_buffer->get_buffer();
        buffer_info.offset = 0;
        buffer_info.range = sizeof(ImmediateUniforms);
        
        VkWriteDescriptorSet descriptor_write{};
        descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_write.dstSet = descriptor_sets[i];
        descriptor_write.dstBinding = 0;
        descriptor_write.dstArrayElement = 0;
        descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptor_write.descriptorCount = 1;
        descriptor_write.pBufferInfo = &buffer_info;
        
        vkUpdateDescriptorSets(device, 1, &descriptor_write, 0, nullptr);
    }
    
    return true;
}

void VulkanImmediateRenderer::set_model_view_matrix(const mat4_t& model_view_matrix) {
    current_view_matrix_idx_ = static_cast<uint32_t>(matrix_stack_.size());
    matrix_stack_.push_back(model_view_matrix);
}

void VulkanImmediateRenderer::set_proj_matrix(const mat4_t& proj_matrix) {
    current_proj_matrix_idx_ = static_cast<uint32_t>(matrix_stack_.size());
    matrix_stack_.push_back(proj_matrix);
}

void VulkanImmediateRenderer::add_vertex(const ImmediateVertex& vertex, vec4_t color_mult) {
    ImmediateVertex modified_vertex = vertex;
    
    if (color_mult.x != 1.0f || color_mult.y != 1.0f || color_mult.z != 1.0f || color_mult.w != 1.0f) {
        // Apply color multiplication
        vec4_t original_color = convert_color(vertex.color);
        vec4_t final_color = {
            original_color.x * color_mult.x,
            original_color.y * color_mult.y,
            original_color.z * color_mult.z,
            original_color.w * color_mult.w
        };
        modified_vertex.color = convert_color(final_color);
    }
    
    vertices_.push_back(modified_vertex);
}

void VulkanImmediateRenderer::add_draw_command(uint32_t vertex_count, VkPrimitiveTopology topology) {
    // For now, just use a simple approach - matrices must be set before drawing
    if (matrix_stack_.empty()) {
        return; // No matrices set, can't draw
    }
    
    // Check if we can batch with the previous command
    if (!draw_commands_.empty()) {
        auto& last_cmd = draw_commands_.back();
        if (last_cmd.topology == topology &&
            last_cmd.view_matrix_idx == current_view_matrix_idx_ &&
            last_cmd.proj_matrix_idx == current_proj_matrix_idx_) {
            
            // Extend the previous command
            last_cmd.index_count += vertex_count;
            return;
        }
    }
    
    // Create new draw command
    ImmediateDrawCommand cmd{};
    cmd.vertex_offset = static_cast<uint32_t>(vertices_.size()) - vertex_count;
    cmd.index_offset = static_cast<uint32_t>(indices_.size()) - vertex_count;
    cmd.index_count = vertex_count;
    cmd.view_matrix_idx = current_view_matrix_idx_;
    cmd.proj_matrix_idx = current_proj_matrix_idx_;
    cmd.topology = topology;
    
    draw_commands_.push_back(cmd);
}

void VulkanImmediateRenderer::draw_point(vec3_t pos, uint32_t color) {
    ImmediateVertex vertex{pos, color};
    add_vertex(vertex);
    
    uint32_t index = static_cast<uint32_t>(vertices_.size()) - 1;
    indices_.push_back(index);
    
    add_draw_command(1, VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
}

void VulkanImmediateRenderer::draw_line(vec3_t from, vec3_t to, uint32_t color) {
    ImmediateVertex vertices[2] = {
        {from, color},
        {to, color}
    };
    
    add_vertex(vertices[0]);
    add_vertex(vertices[1]);
    
    uint32_t base_index = static_cast<uint32_t>(vertices_.size()) - 2;
    indices_.push_back(base_index);
    indices_.push_back(base_index + 1);
    
    add_draw_command(2, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
}

void VulkanImmediateRenderer::draw_triangle(vec3_t p0, vec3_t p1, vec3_t p2, uint32_t color) {
    ImmediateVertex vertices[3] = {
        {p0, color},
        {p1, color},
        {p2, color}
    };
    
    add_vertex(vertices[0]);
    add_vertex(vertices[1]);
    add_vertex(vertices[2]);
    
    uint32_t base_index = static_cast<uint32_t>(vertices_.size()) - 3;
    indices_.push_back(base_index);
    indices_.push_back(base_index + 1);
    indices_.push_back(base_index + 2);
    
    add_draw_command(3, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
}

void VulkanImmediateRenderer::draw_points_v(const ImmediateVertex vertices[], size_t count, vec4_t color_mult) {
    for (size_t i = 0; i < count; ++i) {
        add_vertex(vertices[i], color_mult);
        uint32_t index = static_cast<uint32_t>(vertices_.size()) - 1;
        indices_.push_back(index);
    }
    
    add_draw_command(static_cast<uint32_t>(count), VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
}

void VulkanImmediateRenderer::draw_lines_v(const ImmediateVertex vertices[], size_t count, vec4_t color_mult) {
    // Ensure even number of vertices
    count = count - (count & 1);
    
    for (size_t i = 0; i < count; ++i) {
        add_vertex(vertices[i], color_mult);
        uint32_t index = static_cast<uint32_t>(vertices_.size()) - 1;
        indices_.push_back(index);
    }
    
    add_draw_command(static_cast<uint32_t>(count), VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
}

void VulkanImmediateRenderer::draw_triangles_v(const ImmediateVertex vertices[], size_t count, vec4_t color_mult) {
    // Ensure count is divisible by 3
    count = count - (count % 3);
    
    for (size_t i = 0; i < count; ++i) {
        add_vertex(vertices[i], color_mult);
        uint32_t index = static_cast<uint32_t>(vertices_.size()) - 1;
        indices_.push_back(index);
    }
    
    add_draw_command(static_cast<uint32_t>(count), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
}

bool VulkanImmediateRenderer::update_buffers(uint32_t frame_index) {
    if (frame_index >= frame_data_.size()) {
        return false;
    }
    
    auto& frame = frame_data_[frame_index];
    VmaAllocator allocator = vk_utils::get_vma_allocator();
    
    // Update vertex buffer
    if (!vertices_.empty()) {
        VkDeviceSize vertex_size = vertices_.size() * sizeof(ImmediateVertex);
        if (!frame.vertex_buffer->upload_data(allocator, vertices_.data(), vertex_size)) {
            return false;
        }
    }
    
    // Update index buffer
    if (!indices_.empty()) {
        VkDeviceSize index_size = indices_.size() * sizeof(uint32_t);
        if (!frame.index_buffer->upload_data(allocator, indices_.data(), index_size)) {
            return false;
        }
    }
    
    // Update uniform buffer for each draw command with different matrices
    // For now, we'll use the last set matrices
    if (!matrix_stack_.empty() && current_view_matrix_idx_ < matrix_stack_.size() && current_proj_matrix_idx_ < matrix_stack_.size()) {
        ImmediateUniforms uniforms{};
        uniforms.mvp_matrix = mat4_mul(matrix_stack_[current_proj_matrix_idx_], 
                                      matrix_stack_[current_view_matrix_idx_]);
        
        // Create normal matrix (transpose of inverse of view matrix)
        mat3_t normal_mat3 = mat3_from_mat4(mat4_transpose(mat4_inverse(matrix_stack_[current_view_matrix_idx_])));
        // Convert to mat4 for uniform buffer (with padding)
        uniforms.normal_matrix = mat4_ident();
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                uniforms.normal_matrix.elem[i][j] = normal_mat3.elem[i][j];
            }
        }
        
        uniforms.point_size = 1.0f;
        
        if (!frame.uniform_buffer->upload_data(allocator, &uniforms, sizeof(uniforms))) {
            return false;
        }
    }
    
    return true;
}

void VulkanImmediateRenderer::render(VkCommandBuffer command_buffer, uint32_t frame_index) {
    if (draw_commands_.empty() || frame_index >= frame_data_.size()) {
        return;
    }
    
    // Update buffers with current frame data
    if (!update_buffers(frame_index)) {
        return;
    }
    
    bind_and_draw(command_buffer, frame_index);
}

void VulkanImmediateRenderer::bind_and_draw(VkCommandBuffer command_buffer, uint32_t frame_index) {
    auto& frame = frame_data_[frame_index];
    
    // Bind pipeline
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->get_pipeline());
    
    // Bind descriptor sets
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           pipeline_->get_layout(), 0, 1, &frame.descriptor_set, 0, nullptr);
    
    // Bind vertex buffer
    VkBuffer vertex_buffers[] = {frame.vertex_buffer->get_buffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, offsets);
    
    // Bind index buffer
    vkCmdBindIndexBuffer(command_buffer, frame.index_buffer->get_buffer(), 0, VK_INDEX_TYPE_UINT32);
    
    // Draw commands
    for (const auto& cmd : draw_commands_) {
        // Set topology if it's different (requires dynamic state extension)
        // For now, we'll handle different topologies in separate render passes
        
        vkCmdDrawIndexed(command_buffer, cmd.index_count, 1, cmd.index_offset, cmd.vertex_offset, 0);
    }
}

void VulkanImmediateRenderer::reset_frame(uint32_t frame_index) {
    vertices_.clear();
    indices_.clear();
    draw_commands_.clear();
    matrix_stack_.clear();
    current_view_matrix_idx_ = 0;
    current_proj_matrix_idx_ = 0;
}

} // namespace gfx
} // namespace viamd

#endif // VIAMD_ENABLE_VULKAN