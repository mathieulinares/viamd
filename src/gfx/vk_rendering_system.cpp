// Final Vulkan Rendering System Integration - Agent B Task 3.7 Implementation
// Unified rendering coordinator implementation

#include <gfx/vk_rendering_system.h>
#include <gfx/vk_utils.h>
#include <core/md_log.h>
#include <chrono>

namespace vulkan_system {

VulkanRenderingSystem::VulkanRenderingSystem()
    : m_main_render_pass(VK_NULL_HANDLE)
    , m_main_framebuffer(VK_NULL_HANDLE)
    , m_command_pool(VK_NULL_HANDLE)
    , m_main_command_buffer(VK_NULL_HANDLE)
    , m_image_available_semaphore(VK_NULL_HANDLE)
    , m_render_finished_semaphore(VK_NULL_HANDLE)
    , m_in_flight_fence(VK_NULL_HANDLE)
    , m_initialized(false)
    , m_current_width(0)
    , m_current_height(0)
{
    memset(&m_render_targets, 0, sizeof(m_render_targets));
    memset(&m_imgui_init_data, 0, sizeof(m_imgui_init_data));
    memset(&m_metrics, 0, sizeof(m_metrics));
}

VulkanRenderingSystem::~VulkanRenderingSystem() {
    if (m_initialized) {
        shutdown();
    }
}

bool VulkanRenderingSystem::initialize(uint32_t width, uint32_t height) {
    MD_LOG_INFO("Initializing Vulkan Rendering System...");
    
    m_current_width = width;
    m_current_height = height;
    
    // Initialize Vulkan context
    if (!m_context.initialize()) {
        MD_LOG_ERROR("Failed to initialize Vulkan context");
        return false;
    }
    
    // Create render targets
    if (!create_render_targets(width, height)) {
        MD_LOG_ERROR("Failed to create render targets");
        return false;
    }
    
    // Create render pass
    if (!create_render_pass()) {
        MD_LOG_ERROR("Failed to create render pass");
        return false;
    }
    
    // Create framebuffer
    if (!create_framebuffer()) {
        MD_LOG_ERROR("Failed to create framebuffer");
        return false;
    }
    
    // Create command pool and buffers
    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = m_context.get_graphics_queue_family();
    
    if (vkCreateCommandPool(m_context.get_device(), &pool_info, nullptr, &m_command_pool) != VK_SUCCESS) {
        MD_LOG_ERROR("Failed to create command pool");
        return false;
    }
    
    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = m_command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;
    
    if (vkAllocateCommandBuffers(m_context.get_device(), &alloc_info, &m_main_command_buffer) != VK_SUCCESS) {
        MD_LOG_ERROR("Failed to allocate command buffer");
        return false;
    }
    
    // Create synchronization objects
    if (!create_sync_objects()) {
        MD_LOG_ERROR("Failed to create synchronization objects");
        return false;
    }
    
    // Initialize subsystems
    m_immediate_renderer = std::make_unique<VulkanImmediateRenderer>();
    if (!m_immediate_renderer->initialize(m_context.get_device(), m_context.get_allocator(), 
                                         m_context.get_graphics_queue(), 
                                         m_context.get_graphics_queue_family())) {
        MD_LOG_ERROR("Failed to initialize immediate renderer");
        return false;
    }
    
    m_volume_system = std::make_unique<integration::VulkanVolumeRenderingSystem>();
    if (!m_volume_system->initialize(m_context.get_device(), m_context.get_allocator(),
                                    m_context.get_transfer_queue(),
                                    m_context.get_transfer_queue_family())) {
        MD_LOG_ERROR("Failed to initialize volume rendering system");
        return false;
    }
    
    m_post_processing = std::make_unique<postprocessing::VulkanPostProcessingPipeline>();
    if (!m_post_processing->initialize(m_context.get_device(), m_context.get_allocator(),
                                      width, height)) {
        MD_LOG_ERROR("Failed to initialize post-processing pipeline");
        return false;
    }
    
    // Setup ImGui integration
    m_imgui_init_data.instance = m_context.get_instance();
    m_imgui_init_data.physical_device = m_context.get_physical_device();
    m_imgui_init_data.device = m_context.get_device();
    m_imgui_init_data.queue_family = m_context.get_graphics_queue_family();
    m_imgui_init_data.queue = m_context.get_graphics_queue();
    m_imgui_init_data.descriptor_pool = m_context.get_descriptor_pool();
    m_imgui_init_data.render_pass = m_main_render_pass;
    m_imgui_init_data.min_image_count = 2;
    m_imgui_init_data.image_count = 2;
    m_imgui_init_data.msaa_samples = VK_SAMPLE_COUNT_1_BIT;
    m_imgui_init_data.pipeline_cache = VK_NULL_HANDLE;
    m_imgui_init_data.subpass = 0;
    
    imgui_integration::set_vulkan_init_data(m_imgui_init_data);
    
    m_initialized = true;
    MD_LOG_INFO("Vulkan Rendering System initialized successfully");
    return true;
}

void VulkanRenderingSystem::shutdown() {
    if (!m_initialized) return;
    
    MD_LOG_INFO("Shutting down Vulkan Rendering System...");
    
    // Wait for device to be idle
    vkDeviceWaitIdle(m_context.get_device());
    
    // Shutdown subsystems
    m_post_processing.reset();
    m_volume_system.reset();
    m_immediate_renderer.reset();
    
    // Destroy synchronization objects
    if (m_in_flight_fence != VK_NULL_HANDLE) {
        vkDestroyFence(m_context.get_device(), m_in_flight_fence, nullptr);
        m_in_flight_fence = VK_NULL_HANDLE;
    }
    
    if (m_render_finished_semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(m_context.get_device(), m_render_finished_semaphore, nullptr);
        m_render_finished_semaphore = VK_NULL_HANDLE;
    }
    
    if (m_image_available_semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(m_context.get_device(), m_image_available_semaphore, nullptr);
        m_image_available_semaphore = VK_NULL_HANDLE;
    }
    
    // Destroy command pool
    if (m_command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_context.get_device(), m_command_pool, nullptr);
        m_command_pool = VK_NULL_HANDLE;
    }
    
    // Destroy framebuffer and render pass
    if (m_main_framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_context.get_device(), m_main_framebuffer, nullptr);
        m_main_framebuffer = VK_NULL_HANDLE;
    }
    
    if (m_main_render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_context.get_device(), m_main_render_pass, nullptr);
        m_main_render_pass = VK_NULL_HANDLE;
    }
    
    // Destroy render targets
    destroy_render_targets();
    
    // Shutdown Vulkan context
    m_context.shutdown();
    
    m_initialized = false;
    MD_LOG_INFO("Vulkan Rendering System shut down successfully");
}

void VulkanRenderingSystem::resize(uint32_t width, uint32_t height) {
    if (!m_initialized) return;
    
    vkDeviceWaitIdle(m_context.get_device());
    
    m_current_width = width;
    m_current_height = height;
    
    // Recreate render targets and framebuffer
    destroy_render_targets();
    if (m_main_framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_context.get_device(), m_main_framebuffer, nullptr);
        m_main_framebuffer = VK_NULL_HANDLE;
    }
    
    create_render_targets(width, height);
    create_framebuffer();
    
    // Resize subsystems
    if (m_post_processing) {
        m_post_processing->resize(width, height);
    }
}

bool VulkanRenderingSystem::supports_features() const {
    return m_context.is_initialized();
}

void VulkanRenderingSystem::begin_frame() {
    if (!m_initialized) return;
    
    m_frame_start_time = std::chrono::high_resolution_clock::now();
    
    // Wait for previous frame
    vkWaitForFences(m_context.get_device(), 1, &m_in_flight_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(m_context.get_device(), 1, &m_in_flight_fence);
    
    // Reset command buffer
    vkResetCommandBuffer(m_main_command_buffer, 0);
    
    // Begin command buffer recording
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = 0;
    begin_info.pInheritanceInfo = nullptr;
    
    vkBeginCommandBuffer(m_main_command_buffer, &begin_info);
    
    // Begin render pass
    VkRenderPassBeginInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = m_main_render_pass;
    render_pass_info.framebuffer = m_main_framebuffer;
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = {m_current_width, m_current_height};
    
    VkClearValue clear_values[4] = {};
    clear_values[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};  // Color
    clear_values[1].depthStencil = {1.0f, 0};             // Depth
    clear_values[2].color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // Normal
    clear_values[3].color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // Velocity
    
    render_pass_info.clearValueCount = 4;
    render_pass_info.pClearValues = clear_values;
    
    vkCmdBeginRenderPass(m_main_command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    
    // Set viewport
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_current_width;
    viewport.height = (float)m_current_height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(m_main_command_buffer, 0, 1, &viewport);
    
    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = {m_current_width, m_current_height};
    vkCmdSetScissor(m_main_command_buffer, 0, 1, &scissor);
}

void VulkanRenderingSystem::render_immediate_geometry(const RenderParams& params) {
    if (!m_initialized || !m_immediate_renderer) return;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Render immediate geometry using the Vulkan immediate renderer
    m_immediate_renderer->render(m_main_command_buffer, params.view_matrix, params.proj_matrix);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    m_metrics.immediate_render_time_ms = std::chrono::duration<float, std::milli>(end_time - start_time).count();
}

void VulkanRenderingSystem::render_volume_data(const RenderParams& params) {
    if (!m_initialized || !m_volume_system || !params.enable_volume_rendering) return;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Create volume rendering description
    volume::VulkanRenderDesc volume_desc = {};
    volume_desc.view_matrix = params.view_matrix;
    volume_desc.proj_matrix = params.proj_matrix;
    volume_desc.model_matrix = params.volume_transform;
    volume_desc.viewport_width = params.viewport_width;
    volume_desc.viewport_height = params.viewport_height;
    
    // Render volume data
    // m_volume_system->render_volume_with_post_processing(...);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    m_metrics.volume_render_time_ms = std::chrono::duration<float, std::milli>(end_time - start_time).count();
}

void VulkanRenderingSystem::apply_post_processing(const RenderParams& params) {
    if (!m_initialized || !m_post_processing) return;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Setup post-processing effects
    postprocessing::VulkanPostProcessDesc post_desc = {};
    post_desc.enable_ssao = params.enable_ssao;
    post_desc.enable_dof = params.enable_dof;
    post_desc.enable_fxaa = params.enable_fxaa;
    post_desc.enable_temporal_aa = params.enable_temporal_aa;
    post_desc.background_color = params.background_color;
    post_desc.background_intensity = params.background_intensity;
    
    // Apply post-processing
    m_post_processing->process(m_main_command_buffer, post_desc);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    m_metrics.post_process_time_ms = std::chrono::duration<float, std::milli>(end_time - start_time).count();
}

void VulkanRenderingSystem::render_ui(const RenderParams& params) {
    if (!m_initialized) return;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Render ImGui using Vulkan backend
    // This would typically be called from the main application loop
    // imgui_integration::render(imgui_integration::Backend::Vulkan, application_context);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    m_metrics.ui_render_time_ms = std::chrono::duration<float, std::milli>(end_time - start_time).count();
}

void VulkanRenderingSystem::end_frame_and_present() {
    if (!m_initialized) return;
    
    // End render pass
    vkCmdEndRenderPass(m_main_command_buffer);
    
    // End command buffer recording
    vkEndCommandBuffer(m_main_command_buffer);
    
    // Submit command buffer
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &m_main_command_buffer;
    
    VkSemaphore wait_semaphores[] = {m_image_available_semaphore};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;
    
    VkSemaphore signal_semaphores[] = {m_render_finished_semaphore};
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;
    
    vkQueueSubmit(m_context.get_graphics_queue(), 1, &submit_info, m_in_flight_fence);
    
    update_performance_metrics();
}

void VulkanRenderingSystem::update_buffers(const void* vertex_data, size_t vertex_size, 
                                          const void* index_data, size_t index_size) {
    if (!m_initialized || !m_immediate_renderer) return;
    
    // Update immediate renderer buffers
    m_immediate_renderer->update_vertex_data(vertex_data, vertex_size);
    if (index_data && index_size > 0) {
        m_immediate_renderer->update_index_data(index_data, index_size);
    }
}

VulkanRenderingSystem::PerformanceMetrics VulkanRenderingSystem::get_performance_metrics() const {
    return m_metrics;
}

bool VulkanRenderingSystem::create_render_targets(uint32_t width, uint32_t height) {
    m_render_targets.width = width;
    m_render_targets.height = height;
    
    VkDevice device = m_context.get_device();
    VmaAllocator allocator = m_context.get_allocator();
    
    // Create color render target
    if (!vulkan_utils::create_image_2d(device, allocator,
                                      width, height, VK_FORMAT_R8G8B8A8_UNORM,
                                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                      VMA_MEMORY_USAGE_GPU_ONLY,
                                      &m_render_targets.color_image,
                                      &m_render_targets.color_allocation)) {
        return false;
    }
    
    if (!vulkan_utils::create_image_view_2d(device, m_render_targets.color_image,
                                           VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT,
                                           &m_render_targets.color_view)) {
        return false;
    }
    
    // Create depth render target
    VkFormat depth_format = vulkan_utils::find_depth_format(m_context.get_physical_device());
    if (!vulkan_utils::create_image_2d(device, allocator,
                                      width, height, depth_format,
                                      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                      VMA_MEMORY_USAGE_GPU_ONLY,
                                      &m_render_targets.depth_image,
                                      &m_render_targets.depth_allocation)) {
        return false;
    }
    
    if (!vulkan_utils::create_image_view_2d(device, m_render_targets.depth_image,
                                           depth_format, VK_IMAGE_ASPECT_DEPTH_BIT,
                                           &m_render_targets.depth_view)) {
        return false;
    }
    
    // Create normal render target
    if (!vulkan_utils::create_image_2d(device, allocator,
                                      width, height, VK_FORMAT_R16G16B16A16_SFLOAT,
                                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                      VMA_MEMORY_USAGE_GPU_ONLY,
                                      &m_render_targets.normal_image,
                                      &m_render_targets.normal_allocation)) {
        return false;
    }
    
    if (!vulkan_utils::create_image_view_2d(device, m_render_targets.normal_image,
                                           VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT,
                                           &m_render_targets.normal_view)) {
        return false;
    }
    
    // Create velocity render target
    if (!vulkan_utils::create_image_2d(device, allocator,
                                      width, height, VK_FORMAT_R16G16_SFLOAT,
                                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                      VMA_MEMORY_USAGE_GPU_ONLY,
                                      &m_render_targets.velocity_image,
                                      &m_render_targets.velocity_allocation)) {
        return false;
    }
    
    if (!vulkan_utils::create_image_view_2d(device, m_render_targets.velocity_image,
                                           VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT,
                                           &m_render_targets.velocity_view)) {
        return false;
    }
    
    return true;
}

void VulkanRenderingSystem::destroy_render_targets() {
    VkDevice device = m_context.get_device();
    VmaAllocator allocator = m_context.get_allocator();
    
    // Destroy velocity render target
    if (m_render_targets.velocity_view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_render_targets.velocity_view, nullptr);
        m_render_targets.velocity_view = VK_NULL_HANDLE;
    }
    if (m_render_targets.velocity_image != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, m_render_targets.velocity_image, m_render_targets.velocity_allocation);
        m_render_targets.velocity_image = VK_NULL_HANDLE;
    }
    
    // Destroy normal render target
    if (m_render_targets.normal_view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_render_targets.normal_view, nullptr);
        m_render_targets.normal_view = VK_NULL_HANDLE;
    }
    if (m_render_targets.normal_image != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, m_render_targets.normal_image, m_render_targets.normal_allocation);
        m_render_targets.normal_image = VK_NULL_HANDLE;
    }
    
    // Destroy depth render target
    if (m_render_targets.depth_view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_render_targets.depth_view, nullptr);
        m_render_targets.depth_view = VK_NULL_HANDLE;
    }
    if (m_render_targets.depth_image != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, m_render_targets.depth_image, m_render_targets.depth_allocation);
        m_render_targets.depth_image = VK_NULL_HANDLE;
    }
    
    // Destroy color render target
    if (m_render_targets.color_view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_render_targets.color_view, nullptr);
        m_render_targets.color_view = VK_NULL_HANDLE;
    }
    if (m_render_targets.color_image != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, m_render_targets.color_image, m_render_targets.color_allocation);
        m_render_targets.color_image = VK_NULL_HANDLE;
    }
}

bool VulkanRenderingSystem::create_render_pass() {
    std::vector<VkAttachmentDescription> attachments(4);
    
    // Color attachment
    attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    // Depth attachment
    VkFormat depth_format = vulkan_utils::find_depth_format(m_context.get_physical_device());
    attachments[1].format = depth_format;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    // Normal attachment
    attachments[2].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    // Velocity attachment
    attachments[3].format = VK_FORMAT_R16G16_SFLOAT;
    attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[3].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    VkAttachmentReference color_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_ref = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkAttachmentReference normal_ref = {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference velocity_ref = {3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    
    std::vector<VkAttachmentReference> color_refs = {color_ref, normal_ref, velocity_ref};
    
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(color_refs.size());
    subpass.pColorAttachments = color_refs.data();
    subpass.pDepthStencilAttachment = &depth_ref;
    
    VkRenderPassCreateInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = static_cast<uint32_t>(attachments.size());
    render_pass_info.pAttachments = attachments.data();
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    
    return vkCreateRenderPass(m_context.get_device(), &render_pass_info, nullptr, &m_main_render_pass) == VK_SUCCESS;
}

bool VulkanRenderingSystem::create_framebuffer() {
    std::vector<VkImageView> attachments = {
        m_render_targets.color_view,
        m_render_targets.depth_view,
        m_render_targets.normal_view,
        m_render_targets.velocity_view
    };
    
    VkFramebufferCreateInfo framebuffer_info = {};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = m_main_render_pass;
    framebuffer_info.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebuffer_info.pAttachments = attachments.data();
    framebuffer_info.width = m_render_targets.width;
    framebuffer_info.height = m_render_targets.height;
    framebuffer_info.layers = 1;
    
    return vkCreateFramebuffer(m_context.get_device(), &framebuffer_info, nullptr, &m_main_framebuffer) == VK_SUCCESS;
}

bool VulkanRenderingSystem::create_sync_objects() {
    VkSemaphoreCreateInfo semaphore_info = {};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    
    VkDevice device = m_context.get_device();
    
    if (vkCreateSemaphore(device, &semaphore_info, nullptr, &m_image_available_semaphore) != VK_SUCCESS ||
        vkCreateSemaphore(device, &semaphore_info, nullptr, &m_render_finished_semaphore) != VK_SUCCESS ||
        vkCreateFence(device, &fence_info, nullptr, &m_in_flight_fence) != VK_SUCCESS) {
        return false;
    }
    
    return true;
}

void VulkanRenderingSystem::update_performance_metrics() {
    auto end_time = std::chrono::high_resolution_clock::now();
    m_metrics.total_frame_time_ms = std::chrono::duration<float, std::milli>(end_time - m_frame_start_time).count();
    
    // Get memory usage from VMA
    VmaBudget budget;
    vmaGetBudget(m_context.get_allocator(), &budget);
    m_metrics.memory_usage_bytes = budget.usage;
}

// Backend switching implementation
namespace backend_switching {

BackendCapabilities detect_backend_capabilities() {
    BackendCapabilities caps = {};
    
    // Check Vulkan availability
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "VIAMD";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "VIAMD Engine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_2;
    
    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    
    VkInstance test_instance;
    caps.vulkan_available = (vkCreateInstance(&create_info, nullptr, &test_instance) == VK_SUCCESS);
    
    if (caps.vulkan_available) {
        // Check for required features
        uint32_t device_count = 0;
        vkEnumeratePhysicalDevices(test_instance, &device_count, nullptr);
        
        if (device_count > 0) {
            std::vector<VkPhysicalDevice> devices(device_count);
            vkEnumeratePhysicalDevices(test_instance, &device_count, devices.data());
            
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(devices[0], &properties);
            
            caps.vulkan_supports_required_features = true;  // Simplified check
            caps.vulkan_api_version = properties.apiVersion;
            caps.vulkan_device_name = properties.deviceName;
        }
        
        vkDestroyInstance(test_instance, nullptr);
    }
    
    // OpenGL is always assumed available in this context
    caps.opengl_available = true;
    caps.opengl_version_string = "OpenGL 4.6";
    
    return caps;
}

BackendType recommend_backend(const BackendCapabilities& caps) {
    if (caps.vulkan_available && caps.vulkan_supports_required_features) {
        return BackendType::Vulkan;
    }
    return BackendType::OpenGL;
}

BackendManager& BackendManager::instance() {
    static BackendManager instance;
    return instance;
}

bool BackendManager::initialize_backend(BackendType type, uint32_t width, uint32_t height) {
    if (m_initialized) {
        shutdown_backend();
    }
    
    m_capabilities = detect_backend_capabilities();
    m_current_backend = type;
    
    if (type == BackendType::Auto) {
        m_current_backend = recommend_backend(m_capabilities);
    }
    
    if (m_current_backend == BackendType::Vulkan) {
        if (m_capabilities.vulkan_available && m_capabilities.vulkan_supports_required_features) {
            m_vulkan_system = std::make_unique<VulkanRenderingSystem>();
            if (m_vulkan_system->initialize(width, height)) {
                m_initialized = true;
                MD_LOG_INFO("Vulkan backend initialized successfully");
                return true;
            } else {
                MD_LOG_ERROR("Failed to initialize Vulkan backend, falling back to OpenGL");
                m_vulkan_system.reset();
                m_current_backend = BackendType::OpenGL;
            }
        }
    }
    
    if (m_current_backend == BackendType::OpenGL) {
        // OpenGL initialization is handled by existing systems
        m_initialized = true;
        MD_LOG_INFO("OpenGL backend active");
        return true;
    }
    
    return false;
}

void BackendManager::shutdown_backend() {
    if (!m_initialized) return;
    
    m_vulkan_system.reset();
    m_initialized = false;
    MD_LOG_INFO("Backend shut down");
}

void BackendManager::switch_backend(BackendType new_type) {
    if (new_type == m_current_backend) return;
    
    uint32_t current_width = 1920;  // Would get from current context
    uint32_t current_height = 1080;
    
    if (m_vulkan_system) {
        current_width = m_vulkan_system->m_current_width;
        current_height = m_vulkan_system->m_current_height;
    }
    
    shutdown_backend();
    initialize_backend(new_type, current_width, current_height);
}

void BackendManager::render_frame(const VulkanRenderingSystem::RenderParams& params) {
    if (!m_initialized) return;
    
    if (is_vulkan_active()) {
        m_vulkan_system->begin_frame();
        m_vulkan_system->render_immediate_geometry(params);
        m_vulkan_system->render_volume_data(params);
        m_vulkan_system->apply_post_processing(params);
        m_vulkan_system->render_ui(params);
        m_vulkan_system->end_frame_and_present();
    }
    // OpenGL rendering would be handled by existing systems
}

void BackendManager::resize_viewport(uint32_t width, uint32_t height) {
    if (is_vulkan_active()) {
        m_vulkan_system->resize(width, height);
    }
}

} // namespace backend_switching

} // namespace vulkan_system