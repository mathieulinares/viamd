# Agent Quick Start Templates

## Agent A - Infrastructure & Foundation

### Your First Day Checklist:
1. **Read the interface specs**: Review `INTERFACE_SPECS.md` - your code becomes the foundation for everyone else
2. **Set up development environment**: Ensure Vulkan SDK is installed and accessible
3. **Start with Task 1.1**: CMakeLists.txt changes are straightforward and unblock shader compilation

### Week 1 Implementation Plan:

#### Task 1.1: CMakeLists.txt Vulkan Integration
```cmake
# Add to CMakeLists.txt after existing OpenGL setup
find_package(Vulkan REQUIRED)

# Vulkan options
option(VIAMD_USE_VULKAN "Use Vulkan instead of OpenGL" ON)

if(VIAMD_USE_VULKAN)
    add_definitions(-DVIAMD_USE_VULKAN)
    
    # Add Vulkan Memory Allocator
    set(VMA_STATIC_VULKAN_FUNCTIONS OFF CACHE BOOL "" FORCE)
    set(VMA_DYNAMIC_VULKAN_FUNCTIONS ON CACHE BOOL "" FORCE)
    add_subdirectory(ext/VulkanMemoryAllocator)
    
    # Link Vulkan
    target_link_libraries(viamd PRIVATE 
        Vulkan::Vulkan
        VulkanMemoryAllocator
    )
else()
    # Existing OpenGL setup remains
endif()
```

#### Task 1.2: Basic VulkanContext Implementation Template
```cpp
// src/gfx/vk_context.cpp - Start with this minimal implementation
#include "vk_context.h"
#include <iostream>
#include <vector>

VulkanContext* g_vulkanContext = nullptr;

bool VulkanContext::initialize(const InitInfo& info) {
    // 1. Create instance
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = info.applicationName;
    appInfo.applicationVersion = info.applicationVersion;
    appInfo.pEngineName = info.engineName;
    appInfo.engineVersion = info.engineVersion;
    appInfo.apiVersion = VK_API_VERSION_1_2;
    
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    
    // Add required extensions
    std::vector<const char*> extensions = info.requiredExtensions;
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    
    if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) {
        std::cerr << "Failed to create Vulkan instance!" << std::endl;
        return false;
    }
    
    // 2. Set up debug messenger if validation enabled
    // 3. Pick physical device
    // 4. Create logical device and queues
    // 5. Create VMA allocator
    
    return true;
}
```

### Critical Success Factors for Agent A:
- **Get the interfaces exactly right** - other agents depend on exact function signatures
- **Test thoroughly** - create simple test programs to verify each component works
- **Document memory management patterns** - VMA usage must be consistent
- **Coordinate with Agent D** - they need validation hooks from day 1

---

## Agent B - Rendering Pipeline & Shaders

### Your First Day Checklist:
1. ✅ **Agent A Foundation Complete** - All infrastructure ready!
2. **Study existing OpenGL code**: Understand current immediate drawing and volume rendering
3. **Set up development environment**: Vulkan validation layers, debugging tools
4. **Review foundation APIs**: Study `vk_pipeline.h`, `vk_command.h`, `vk_utils.h`

### Week 7 Implementation Plan (Ready to Start):

#### Task 3.1: Immediate Drawing Migration Strategy
```cpp
// Migration approach: Implement Vulkan backend while keeping OpenGL API
// src/gfx/immediate_draw_utils.cpp

#ifdef VIAMD_USE_VULKAN
    #include "vk_pipeline.h"
    #include "vk_command.h"
    
    // New Vulkan implementation using Agent A's infrastructure
    void ImmediateDrawContext::begin(VkCommandBuffer cmd, const float* projMatrix, const float* viewMatrix) {
        m_currentCmd = cmd;
        m_vertices.clear();
        m_indices.clear();
        
        // Create graphics pipeline if not exists
        if (m_pipeline.get_pipeline() == VK_NULL_HANDLE) {
            createImmediateDrawPipeline();
        }
        
        // Update uniform buffer with matrices
        struct Uniforms {
            float projMatrix[16];
            float viewMatrix[16];
        } uniforms;
        memcpy(uniforms.projMatrix, projMatrix, sizeof(float) * 16);
        memcpy(uniforms.viewMatrix, viewMatrix, sizeof(float) * 16);
        
        // Upload to uniform buffer using VMA
        vk_utils::VmaBuffer& uniformBuffer = getUniformBuffer();
        memcpy(uniformBuffer.mapped_data, &uniforms, sizeof(uniforms));
    }
#else
    // Keep existing OpenGL implementation as fallback
#endif
```

#### Task 3.3: Volume Rendering Compute Approach
```cpp
// Convert from fragment shader ray-casting to compute shader
// Benefits: Better performance, easier to optimize, no geometry required

// Compute shader approach (new file: src/shaders/volume/volume_raycast.comp)
// #version 450
// layout(local_size_x = 8, local_size_y = 8) in;
// layout(binding = 0, rgba8) uniform writeonly image2D outputImage;
// layout(binding = 1) uniform sampler3D volumeTexture;
// layout(binding = 2) uniform sampler1D transferTexture;
// layout(binding = 3) uniform VolumeParams { ... } params;
// 
// void main() {
//     ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
//     vec2 uv = (coord + 0.5) / vec2(imageSize(outputImage));
//     
//     // Ray generation from screen coordinate
//     // Volume ray-casting algorithm
//     // Write result to output image
// }
```

### Critical Success Factors for Agent B:
- **Maintain API compatibility** - existing application code should not need changes
- **Profile early and often** - Vulkan performance characteristics differ from OpenGL
- **Coordinate with Agent C** - you share pipeline and descriptor management
- **Batch shader conversions** - don't convert shaders one by one

---

## Agent C - Post-Processing & UI Integration

### Your First Day Checklist:
1. **Study ImGui Vulkan backend** - understand the differences from OpenGL3 backend
2. **Analyze post-processing chain** - understand current render pass dependencies
3. **Coordinate with Agent B** - you'll work in parallel starting Week 10

### Week 10 Implementation Plan:

#### Task 3.5: Post-Processing Framework Design
```cpp
// Key insight: Vulkan requires explicit render pass management
// src/gfx/postprocessing_utils.cpp

class PostProcessingChain {
private:
    struct EffectPass {
        VkRenderPass renderPass;
        VkFramebuffer framebuffer;
        VkPipeline pipeline;
        Texture* inputTexture;
        Texture* outputTexture;
    };
    
    std::vector<EffectPass> m_passes;
    
    void setupRenderPasses() {
        // Create render passes for each effect
        // Handle dependencies between effects
        // Optimize for GPU efficiency
    }
};
```

#### Task 4.1: ImGui Backend Migration
```cpp
// Key changes from OpenGL3 to Vulkan backend
// main.cpp - ImGui setup changes

#ifdef VIAMD_USE_VULKAN
    // Setup Vulkan backend
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = g_vulkanContext->getInstance();
    init_info.PhysicalDevice = g_vulkanContext->getPhysicalDevice();
    init_info.Device = g_vulkanContext->getDevice();
    init_info.QueueFamily = g_vulkanContext->getGraphicsQueueFamily();
    init_info.Queue = g_vulkanContext->getGraphicsQueue();
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = descriptorPool; // Need to create
    init_info.Subpass = 0;
    init_info.MinImageCount = 2;
    init_info.ImageCount = 2;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&init_info, renderPass);
#else
    // Keep existing OpenGL3 backend
    ImGui_ImplOpenGL3_Init("#version 150");
#endif
```

### Critical Success Factors for Agent C:
- **Understand render pass dependencies** - post-processing effects have complex dependencies
- **Test UI early** - ImGui backend changes can break the entire interface
- **Coordinate descriptor usage** - don't conflict with other agents' descriptor sets
- **Handle multi-viewport carefully** - this is a new feature with potential complexity

---

## Agent D - Testing & Validation

### Your First Day Checklist:
1. **Start immediately** - you can begin framework setup while others work on implementation
2. **Set up automated testing** - visual regression and performance benchmarking
3. **Create validation hooks** - integration points for each phase

### Week 1 Implementation Plan:

#### Framework Setup (Task D.1)
```python
# Create automated testing framework
# tools/validation/visual_regression.py

import subprocess
import numpy as np
from PIL import Image
import argparse

class VisualRegressionTester:
    def __init__(self, opengl_reference_dir, vulkan_output_dir):
        self.opengl_ref = opengl_reference_dir
        self.vulkan_out = vulkan_output_dir
        
    def capture_opengl_reference(self, test_cases):
        """Capture OpenGL baseline images"""
        for test_case in test_cases:
            # Run ViaMD with OpenGL backend
            # Capture screenshots
            # Save as reference
            pass
    
    def test_vulkan_output(self, test_cases):
        """Compare Vulkan output against OpenGL reference"""
        results = []
        for test_case in test_cases:
            # Run ViaMD with Vulkan backend
            # Capture screenshot
            # Compare with reference
            # Calculate difference percentage
            results.append(self.compare_images(ref_img, vulkan_img))
        return results
    
    def compare_images(self, img1, img2):
        """Calculate pixel-level difference between images"""
        # Convert to numpy arrays
        # Calculate MSE and percentage difference
        # Return difference metrics
        pass
```

#### Performance Monitoring (Task D.2)
```cpp
// Create performance monitoring hooks
// src/gfx/performance_monitor.h

class PerformanceMonitor {
public:
    void beginFrame();
    void endFrame();
    
    void beginRenderPass(const std::string& name);
    void endRenderPass(const std::string& name);
    
    struct FrameStats {
        float frameTime;
        float gpuTime;
        size_t memoryUsed;
        uint32_t drawCalls;
        uint32_t triangles;
    };
    
    FrameStats getFrameStats() const;
    void exportStats(const std::string& filepath);
    
private:
    std::chrono::high_resolution_clock::time_point m_frameStart;
    std::vector<float> m_frameTimes;
    VkQueryPool m_queryPool;
};
```

### Critical Success Factors for Agent D:
- **Start validation early** - don't wait for complete implementation
- **Automate everything** - manual testing won't scale
- **Focus on visual fidelity first** - performance optimization comes later
- **Create clear failure reports** - help other agents debug issues quickly

---

## Daily Workflow for All Agents

### Morning Standup (5 minutes):
```
Agent X Daily Update:
✅ Yesterday: [specific accomplishments]
🔄 Today: [specific plans]
⏸️ Blockers: [dependencies or issues]
🔗 Interfaces: [any changes affecting others]
📅 Status: [on track / at risk / blocked]
```

### Integration Testing Schedule:
- **Week 3**: Agent A foundation testing
- **Week 6**: Agent A + resource management integration
- **Week 9**: Agent A + Agent B integration
- **Week 12**: All agents integration
- **Week 15**: Complete system validation
- **Week 18**: Performance optimization validation
- **Week 20**: Final release validation

### Communication Channels:
1. **Daily updates**: Post in shared document or chat
2. **Interface changes**: Immediate notification to affected agents
3. **Blockers**: Escalate within 4 hours if no resolution path
4. **Integration issues**: All-hands debugging session if needed

### Success Metrics:
- **Daily commits**: Each agent should commit working code daily
- **Weekly integrations**: Successful integration at each checkpoint
- **No breaking changes**: Interface stability between agents
- **Performance targets**: 10-30% improvement over OpenGL baseline
- **Visual fidelity**: < 1% pixel difference from OpenGL reference

Remember: This is a complex migration project. The key to success is **frequent communication**, **incremental progress**, and **early integration testing**. Don't wait until the end to see if everything works together!