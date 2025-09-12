#include "vk_utils.h"

#ifdef VIAMD_ENABLE_VULKAN

#include "vk_context.h"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cstdlib>
#include <cstdio>

// For now, we'll create placeholder VMA functions until we integrate the actual VMA
// This allows the interface to be defined while Agent A completes foundation work

namespace viamd {
namespace gfx {
namespace vk_utils {

VkCommandBuffer begin_single_time_commands(VkDevice device, VkCommandPool command_pool) {
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = command_pool;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer;
    vkAllocateCommandBuffers(device, &alloc_info, &command_buffer);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(command_buffer, &begin_info);

    return command_buffer;
}

void end_single_time_commands(VkDevice device, VkQueue queue, VkCommandPool command_pool, VkCommandBuffer command_buffer) {
    vkEndCommandBuffer(command_buffer);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
}

uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);

    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type!");
}

VkFormat find_supported_format(VkPhysicalDevice physical_device, const std::vector<VkFormat>& candidates, 
                              VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physical_device, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    throw std::runtime_error("Failed to find supported format!");
}

VkFormat find_depth_format(VkPhysicalDevice physical_device) {
    return find_supported_format(
        physical_device,
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

bool has_stencil_component(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

std::vector<uint32_t> compile_glsl_to_spirv(const std::string& glsl_source, VkShaderStageFlagBits stage) {
    // For production use, this should use shaderc library for runtime compilation
    // For now, we'll implement a simple file-based compilation approach using external tools
    
    // Determine shader stage suffix for temporary file
    std::string stage_suffix;
    switch (stage) {
        case VK_SHADER_STAGE_VERTEX_BIT:
            stage_suffix = ".vert";
            break;
        case VK_SHADER_STAGE_FRAGMENT_BIT:
            stage_suffix = ".frag";
            break;
        case VK_SHADER_STAGE_COMPUTE_BIT:
            stage_suffix = ".comp";
            break;
        case VK_SHADER_STAGE_GEOMETRY_BIT:
            stage_suffix = ".geom";
            break;
        case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
            stage_suffix = ".tesc";
            break;
        case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
            stage_suffix = ".tese";
            break;
        default:
            std::cerr << "Unsupported shader stage for compilation" << std::endl;
            return {};
    }
    
    // Create temporary files for compilation
    std::string temp_glsl = "/tmp/shader_temp" + stage_suffix;
    std::string temp_spirv = "/tmp/shader_temp.spv";
    
    // Write GLSL source to temporary file
    std::ofstream glsl_file(temp_glsl);
    if (!glsl_file.is_open()) {
        std::cerr << "Failed to create temporary GLSL file: " << temp_glsl << std::endl;
        return {};
    }
    glsl_file << glsl_source;
    glsl_file.close();
    
    // Try to compile using glslc (from Vulkan SDK)
    std::string compile_cmd = "glslc " + temp_glsl + " -o " + temp_spirv + " 2>/dev/null";
    int result = system(compile_cmd.c_str());
    
    if (result != 0) {
        // If glslc is not available, try glslangValidator
        compile_cmd = "glslangValidator -V " + temp_glsl + " -o " + temp_spirv + " 2>/dev/null";
        result = system(compile_cmd.c_str());
        
        if (result != 0) {
            std::cerr << "SPIR-V compilation failed. Please install Vulkan SDK with glslc or glslangValidator." << std::endl;
            // Clean up temporary files
            remove(temp_glsl.c_str());
            return {};
        }
    }
    
    // Read compiled SPIR-V
    std::vector<uint32_t> spirv_code;
    try {
        spirv_code = load_spirv_from_file(temp_spirv);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load compiled SPIR-V: " << e.what() << std::endl;
    }
    
    // Clean up temporary files
    remove(temp_glsl.c_str());
    remove(temp_spirv.c_str());
    
    return spirv_code;
}

std::vector<uint32_t> load_spirv_from_file(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filename);
    }

    size_t file_size = (size_t) file.tellg();
    std::vector<uint32_t> buffer(file_size / sizeof(uint32_t));

    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), file_size);
    file.close();

    return buffer;
}

VkShaderModule create_shader_module(VkDevice device, const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = code.size() * sizeof(uint32_t);
    create_info.pCode = code.data();

    VkShaderModule shader_module;
    if (vkCreateShaderModule(device, &create_info, nullptr, &shader_module) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module!");
    }

    return shader_module;
}

// VMA placeholder implementations - will be replaced when VMA is integrated
bool create_buffer(size_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VmaBuffer& buffer) {
    if (g_vma_allocator == VK_NULL_HANDLE) {
        std::cerr << "VMA allocator not initialized" << std::endl;
        return false;
    }

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_info.flags = (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT : 0;

    VkResult result = vmaCreateBuffer(g_vma_allocator, &buffer_info, &alloc_info, &buffer.buffer, &buffer.allocation, nullptr);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create buffer with VMA: " << result << std::endl;
        return false;
    }

    // Map memory if host visible
    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        result = vmaMapMemory(g_vma_allocator, buffer.allocation, &buffer.mapped_data);
        if (result != VK_SUCCESS) {
            std::cerr << "Failed to map buffer memory: " << result << std::endl;
            vmaDestroyBuffer(g_vma_allocator, buffer.buffer, buffer.allocation);
            buffer = {};
            return false;
        }
    }

    return true;
}

void destroy_buffer(VmaBuffer& buffer) {
    if (g_vma_allocator != VK_NULL_HANDLE && buffer.allocation != VK_NULL_HANDLE) {
        if (buffer.mapped_data != nullptr) {
            vmaUnmapMemory(g_vma_allocator, buffer.allocation);
        }
        vmaDestroyBuffer(g_vma_allocator, buffer.buffer, buffer.allocation);
    }
    buffer = {};
}

bool create_image(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, 
                  VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VmaImage& image) {
    if (g_vma_allocator == VK_NULL_HANDLE) {
        std::cerr << "VMA allocator not initialized" << std::endl;
        return false;
    }

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = width;
    image_info.extent.height = height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = format;
    image_info.tiling = tiling;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = usage;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;

    VkResult result = vmaCreateImage(g_vma_allocator, &image_info, &alloc_info, &image.image, &image.allocation, nullptr);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create image with VMA: " << result << std::endl;
        return false;
    }

    return true;
}

void destroy_image(VmaImage& image) {
    if (g_vma_allocator != VK_NULL_HANDLE && image.allocation != VK_NULL_HANDLE) {
        vmaDestroyImage(g_vma_allocator, image.image, image.allocation);
    }
    image = {};
}

VkImageView create_image_view(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspect_flags) {
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = aspect_flags;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    VkImageView image_view;
    if (vkCreateImageView(device, &view_info, nullptr, &image_view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture image view!");
    }

    return image_view;
}

bool create_sync_objects(VkDevice device, size_t max_frames_in_flight, SyncObjects& sync) {
    sync.image_available_semaphores.resize(max_frames_in_flight);
    sync.render_finished_semaphores.resize(max_frames_in_flight);
    sync.in_flight_fences.resize(max_frames_in_flight);

    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < max_frames_in_flight; i++) {
        if (vkCreateSemaphore(device, &semaphore_info, nullptr, &sync.image_available_semaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semaphore_info, nullptr, &sync.render_finished_semaphores[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fence_info, nullptr, &sync.in_flight_fences[i]) != VK_SUCCESS) {
            std::cerr << "Failed to create synchronization objects for frame " << i << std::endl;
            return false;
        }
    }

    return true;
}

void destroy_sync_objects(VkDevice device, SyncObjects& sync) {
    for (size_t i = 0; i < sync.image_available_semaphores.size(); i++) {
        vkDestroySemaphore(device, sync.image_available_semaphores[i], nullptr);
        vkDestroySemaphore(device, sync.render_finished_semaphores[i], nullptr);
        vkDestroyFence(device, sync.in_flight_fences[i], nullptr);
    }
    sync.image_available_semaphores.clear();
    sync.render_finished_semaphores.clear();
    sync.in_flight_fences.clear();
}

void set_debug_name(VkDevice device, VkObjectType object_type, uint64_t object_handle, const char* name) {
    VkDebugUtilsObjectNameInfoEXT name_info{};
    name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    name_info.objectType = object_type;
    name_info.objectHandle = object_handle;
    name_info.pObjectName = name;

    auto func = (PFN_vkSetDebugUtilsObjectNameEXT) vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT");
    if (func != nullptr) {
        func(device, &name_info);
    }
}

VmaAllocator get_vma_allocator() {
    return g_vma_allocator;
}

} // namespace vk_utils
} // namespace gfx
} // namespace viamd

#endif // VIAMD_ENABLE_VULKAN