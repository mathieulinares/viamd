// VMA implementation file - this file must define VMA_IMPLEMENTATION exactly once
#ifdef VIAMD_ENABLE_VULKAN

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

namespace viamd {
namespace gfx {
namespace vk_utils {

// Global VMA allocator instance
VmaAllocator g_vma_allocator = VK_NULL_HANDLE;

} // namespace vk_utils
} // namespace gfx
} // namespace viamd

#endif // VIAMD_ENABLE_VULKAN