// ViaIMD ImGui Vulkan Integration Implementation

#include "imgui_integration.h"
#include <app/application.h>
#include <app/imgui_impl_glfw.h>
#include <app/imgui_impl_opengl3.h>
#include <app/imgui_impl_vulkan.h>
#include <core/md_log.h>
#include <GLFW/glfw3.h>

namespace imgui_integration {

static Backend current_backend = Backend::OpenGL;
static VulkanInitData vulkan_data = {};
static bool vulkan_data_set = false;

void set_vulkan_init_data(const VulkanInitData& init_data) {
    vulkan_data = init_data;
    vulkan_data_set = true;
}

bool initialize(Backend backend, application::Context* ctx, const char* glsl_version) {
    current_backend = backend;
    GLFWwindow* window = (GLFWwindow*)ctx->window.ptr;
    
    switch (backend) {
        case Backend::OpenGL: {
            if (!ImGui_ImplGlfw_InitForOpenGL(window, false) ||
                !ImGui_ImplOpenGL3_Init(glsl_version ? glsl_version : "#version 150"))
            {
                MD_LOG_ERROR("Failed to initialize ImGui OpenGL backend");
                return false;
            }
            MD_LOG_INFO("ImGui OpenGL backend initialized");
            return true;
        }
        
        case Backend::Vulkan: {
            if (!vulkan_data_set) {
                MD_LOG_ERROR("Vulkan initialization data not set. Call set_vulkan_init_data() first");
                return false;
            }
            
            if (!ImGui_ImplGlfw_InitForVulkan(window, false)) {
                MD_LOG_ERROR("Failed to initialize ImGui GLFW for Vulkan");
                return false;
            }
            
            ImGui_ImplVulkan_InitInfo init_info = {};
            init_info.Instance = vulkan_data.instance;
            init_info.PhysicalDevice = vulkan_data.physical_device;
            init_info.Device = vulkan_data.device;
            init_info.QueueFamily = vulkan_data.queue_family;
            init_info.Queue = vulkan_data.queue;
            init_info.DescriptorPool = vulkan_data.descriptor_pool;
            init_info.RenderPass = vulkan_data.render_pass;
            init_info.MinImageCount = vulkan_data.min_image_count;
            init_info.ImageCount = vulkan_data.image_count;
            init_info.MSAASamples = vulkan_data.msaa_samples;
            init_info.PipelineCache = vulkan_data.pipeline_cache;
            init_info.Subpass = vulkan_data.subpass;
            init_info.CheckVkResultFn = [](VkResult err) {
                if (err != VK_SUCCESS) {
                    MD_LOG_ERROR("Vulkan error: %d", err);
                }
            };
            
            if (!ImGui_ImplVulkan_Init(&init_info)) {
                MD_LOG_ERROR("Failed to initialize ImGui Vulkan backend");
                ImGui_ImplGlfw_Shutdown();
                return false;
            }
            
            // Create fonts texture
            if (!ImGui_ImplVulkan_CreateFontsTexture()) {
                MD_LOG_ERROR("Failed to create ImGui Vulkan fonts texture");
                ImGui_ImplVulkan_Shutdown();
                ImGui_ImplGlfw_Shutdown();
                return false;
            }
            
            MD_LOG_INFO("ImGui Vulkan backend initialized");
            return true;
        }
    }
    
    return false;
}

void shutdown(Backend backend, application::Context* ctx) {
    switch (backend) {
        case Backend::OpenGL:
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            break;
            
        case Backend::Vulkan:
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            break;
    }
}

void new_frame(Backend backend) {
    switch (backend) {
        case Backend::OpenGL:
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            break;
            
        case Backend::Vulkan:
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            break;
    }
}

void render(Backend backend, application::Context* ctx) {
    GLFWwindow* window = (GLFWwindow*)ctx->window.ptr;
    
    switch (backend) {
        case Backend::OpenGL:
            glfwMakeContextCurrent(window);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            break;
            
        case Backend::Vulkan:
            // Note: For Vulkan, rendering is handled externally through command buffers
            // This function would typically be called with a command buffer parameter
            // For now, we'll leave this as a placeholder
            MD_LOG_DEBUG("Vulkan rendering handled externally");
            break;
    }
}

void render_platform_windows(Backend backend) {
    // Update and Render additional Platform Windows
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        
        switch (backend) {
            case Backend::OpenGL:
                ImGui::RenderPlatformWindowsDefault();
                break;
                
            case Backend::Vulkan:
                ImGui::RenderPlatformWindowsDefault();
                break;
        }
    }
}

} // namespace imgui_integration