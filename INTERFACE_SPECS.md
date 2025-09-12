# Vulkan Migration Interface Specifications

## Overview
This document defines the exact interfaces that agents must implement to ensure seamless integration during the Vulkan migration. Each interface includes header files, function signatures, and usage examples.

## Core Infrastructure Interfaces (Agent A)

### VulkanContext (src/gfx/vk_context.h)
```cpp
#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <string>

class VulkanContext {
public:
    struct InitInfo {
        const char* applicationName = "ViaMD";
        uint32_t applicationVersion = VK_MAKE_VERSION(0, 1, 15);
        const char* engineName = "ViaMD Engine";
        uint32_t engineVersion = VK_MAKE_VERSION(1, 0, 0);
        bool enableValidationLayers = true;
        std::vector<const char*> requiredExtensions;
    };

    // Core initialization
    bool initialize(const InitInfo& info);
    void shutdown();
    
    // Getters - CRITICAL: These exact signatures must be implemented
    VkInstance getInstance() const { return m_instance; }
    VkDevice getDevice() const { return m_device; }
    VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    
    // Queue management - CRITICAL for other agents
    VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
    VkQueue getComputeQueue() const { return m_computeQueue; }
    VkQueue getTransferQueue() const { return m_transferQueue; }
    uint32_t getGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
    uint32_t getComputeQueueFamily() const { return m_computeQueueFamily; }
    uint32_t getTransferQueueFamily() const { return m_transferQueueFamily; }
    
    // Memory allocator - CRITICAL for resource management
    VmaAllocator getAllocator() const { return m_allocator; }
    
    // Utility functions
    VkCommandPool getCommandPool(uint32_t queueFamily) const;
    bool isExtensionSupported(const char* extension) const;
    
private:
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_computeQueue = VK_NULL_HANDLE;
    VkQueue m_transferQueue = VK_NULL_HANDLE;
    uint32_t m_graphicsQueueFamily = UINT32_MAX;
    uint32_t m_computeQueueFamily = UINT32_MAX;
    uint32_t m_transferQueueFamily = UINT32_MAX;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    std::vector<VkCommandPool> m_commandPools;
};

// Global access - MUST be available for other agents
extern VulkanContext* g_vulkanContext;
```

### Buffer Management (src/gfx/vk_buffer.h)
```cpp
#pragma once
#include "vk_context.h"

enum class BufferUsage {
    Vertex,
    Index,
    Uniform,
    Storage,
    Staging
};

class Buffer {
public:
    struct CreateInfo {
        size_t size;
        BufferUsage usage;
        bool hostVisible = false;
        const void* initialData = nullptr;
    };
    
    // CRITICAL: Exact signatures for Agent B/C integration
    bool create(const CreateInfo& info);
    void destroy();
    
    // Memory management - REQUIRED by rendering agents
    void* map();
    void unmap();
    void copyFrom(const void* data, size_t size, size_t offset = 0);
    void copyTo(Buffer* dst, size_t size, size_t srcOffset = 0, size_t dstOffset = 0);
    
    // Accessors - CRITICAL for binding
    VkBuffer getHandle() const { return m_buffer; }
    VkDeviceMemory getMemory() const { return m_allocation ? VK_NULL_HANDLE : m_deviceMemory; }
    VmaAllocation getAllocation() const { return m_allocation; }
    size_t getSize() const { return m_size; }
    
private:
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_deviceMemory = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    size_t m_size = 0;
    void* m_mappedData = nullptr;
    bool m_hostVisible = false;
};

// Staging helper - REQUIRED for efficient transfers
class StagingManager {
public:
    void initialize(VulkanContext* context);
    void shutdown();
    
    // CRITICAL: Used by all agents for data upload
    void uploadToBuffer(Buffer* dst, const void* data, size_t size, size_t offset = 0);
    void uploadToTexture(class Texture* dst, const void* data, uint32_t width, uint32_t height);
    
    void flush(); // Submit all pending transfers
    
private:
    VulkanContext* m_context = nullptr;
    std::vector<Buffer> m_stagingBuffers;
    VkCommandBuffer m_transferCmd = VK_NULL_HANDLE;
};

extern StagingManager* g_stagingManager;
```

### Texture Management (src/gfx/vk_texture.h)
```cpp
#pragma once
#include "vk_context.h"

enum class TextureFormat {
    R8,
    RG8,
    RGB8,
    RGBA8,
    R16F,
    RG16F,
    RGB16F,
    RGBA16F,
    R32F,
    RG32F,
    RGB32F,
    RGBA32F,
    Depth24Stencil8,
    Depth32F
};

enum class TextureType {
    Texture2D,
    Texture3D,
    TextureCube,
    Texture2DArray
};

class Texture {
public:
    struct CreateInfo {
        uint32_t width;
        uint32_t height;
        uint32_t depth = 1;
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        TextureFormat format;
        TextureType type = TextureType::Texture2D;
        bool renderTarget = false;
        bool depthBuffer = false;
        const void* initialData = nullptr;
    };
    
    // CRITICAL: Required by all rendering agents
    bool create(const CreateInfo& info);
    void destroy();
    
    // Upload operations - REQUIRED for data loading
    void uploadData(const void* data, uint32_t mipLevel = 0, uint32_t arrayLayer = 0);
    void generateMipmaps();
    
    // Accessors - CRITICAL for binding and render passes
    VkImage getImage() const { return m_image; }
    VkImageView getImageView() const { return m_imageView; }
    VkSampler getSampler() const { return m_sampler; }
    VkFormat getVkFormat() const { return m_format; }
    uint32_t getWidth() const { return m_width; }
    uint32_t getHeight() const { return m_height; }
    uint32_t getDepth() const { return m_depth; }
    uint32_t getMipLevels() const { return m_mipLevels; }
    
    // Render target support - REQUIRED for Agent C
    VkFramebuffer createFramebuffer(VkRenderPass renderPass, uint32_t width, uint32_t height);
    
private:
    VkImage m_image = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    uint32_t m_width = 0, m_height = 0, m_depth = 0;
    uint32_t m_mipLevels = 1;
    uint32_t m_arrayLayers = 1;
};
```

## Pipeline Management Interface (Agent B)

### Pipeline Management (src/gfx/vk_pipeline.h)
```cpp
#pragma once
#include "vk_context.h"
#include <unordered_map>
#include <string>

enum class ShaderStage {
    Vertex,
    Fragment,
    Compute,
    Geometry,
    TessControl,
    TessEvaluation
};

struct ShaderModule {
    VkShaderModule module = VK_NULL_HANDLE;
    ShaderStage stage;
    std::string entryPoint = "main";
};

class PipelineManager {
public:
    void initialize(VulkanContext* context);
    void shutdown();
    
    // Shader management - CRITICAL for all rendering
    ShaderModule loadShader(const std::string& filepath, ShaderStage stage);
    ShaderModule loadShaderFromSource(const std::string& source, ShaderStage stage, const std::string& filename = "");
    void destroyShader(ShaderModule& shader);
    
    // Graphics pipeline creation - REQUIRED by Agent B & C
    struct GraphicsPipelineInfo {
        std::vector<ShaderModule> shaders;
        VkRenderPass renderPass;
        uint32_t subpass = 0;
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        bool depthTest = true;
        bool depthWrite = true;
        VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
        bool blendEnable = false;
        VkBlendFactor srcBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        VkBlendFactor dstBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
        std::vector<VkVertexInputBindingDescription> vertexBindings;
        std::vector<VkVertexInputAttributeDescription> vertexAttributes;
    };
    
    VkPipeline createGraphicsPipeline(const GraphicsPipelineInfo& info, VkPipelineLayout layout);
    
    // Compute pipeline creation - REQUIRED by Agent B
    VkPipeline createComputePipeline(const ShaderModule& computeShader, VkPipelineLayout layout);
    
    // Pipeline binding - REQUIRED for rendering
    void bindGraphicsPipeline(VkCommandBuffer cmd, VkPipeline pipeline);
    void bindComputePipeline(VkCommandBuffer cmd, VkPipeline pipeline);
    
    // Pipeline cache for performance
    void savePipelineCache(const std::string& filepath);
    void loadPipelineCache(const std::string& filepath);
    
private:
    VulkanContext* m_context = nullptr;
    VkPipelineCache m_pipelineCache = VK_NULL_HANDLE;
    std::unordered_map<std::string, VkPipeline> m_pipelineCache_map;
};

extern PipelineManager* g_pipelineManager;
```

## Descriptor Management Interface (Agent A → All)

### Descriptor Management (src/gfx/vk_descriptor.h)
```cpp
#pragma once
#include "vk_context.h"
#include "vk_buffer.h"
#include "vk_texture.h"
#include <vector>

enum class DescriptorType {
    UniformBuffer,
    StorageBuffer,
    CombinedImageSampler,
    StorageImage,
    InputAttachment
};

struct DescriptorBinding {
    uint32_t binding;
    DescriptorType type;
    uint32_t count = 1;
    VkShaderStageFlags stageFlags;
};

class DescriptorSetLayout {
public:
    bool create(const std::vector<DescriptorBinding>& bindings);
    void destroy();
    
    VkDescriptorSetLayout getHandle() const { return m_layout; }
    
private:
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
};

class DescriptorSet {
public:
    // CRITICAL: Required by all rendering agents
    void bindBuffer(uint32_t binding, Buffer* buffer, size_t offset = 0, size_t range = VK_WHOLE_SIZE);
    void bindTexture(uint32_t binding, Texture* texture);
    void bindTextures(uint32_t binding, const std::vector<Texture*>& textures);
    
    // Command buffer binding - REQUIRED for rendering
    void bind(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t setIndex = 0);
    
    VkDescriptorSet getHandle() const { return m_set; }
    
private:
    friend class DescriptorManager;
    VkDescriptorSet m_set = VK_NULL_HANDLE;
    std::vector<VkWriteDescriptorSet> m_writes;
    std::vector<VkDescriptorBufferInfo> m_bufferInfos;
    std::vector<VkDescriptorImageInfo> m_imageInfos;
};

class DescriptorManager {
public:
    void initialize(VulkanContext* context);
    void shutdown();
    
    // CRITICAL: All agents need descriptor sets
    DescriptorSet* allocateDescriptorSet(DescriptorSetLayout* layout);
    void freeDescriptorSet(DescriptorSet* set);
    
    // Bulk operations for performance
    void updateDescriptorSets();
    void resetPools();
    
private:
    VulkanContext* m_context = nullptr;
    std::vector<VkDescriptorPool> m_pools;
    std::vector<DescriptorSet> m_allocatedSets;
};

extern DescriptorManager* g_descriptorManager;
```

## Rendering Integration Interfaces (Agents B & C)

### Immediate Drawing Interface (Agent B)
```cpp
// src/gfx/immediate_draw_utils.h - MIGRATION TARGET
#pragma once
#include "vk_context.h"
#include "vk_buffer.h"
#include "vk_pipeline.h"

struct ImmediateVertex {
    float position[3];
    float color[4];
    float texCoord[2];
};

class ImmediateDrawContext {
public:
    void initialize(VulkanContext* context);
    void shutdown();
    
    // CRITICAL: Maintain exact API compatibility with OpenGL version
    void begin(VkCommandBuffer cmd, const float* projectionMatrix, const float* viewMatrix);
    void end();
    
    // Drawing commands - MUST maintain OpenGL compatibility
    void drawLine(const float* start, const float* end, const float* color);
    void drawTriangle(const float* v0, const float* v1, const float* v2, const float* color);
    void drawQuad(const float* v0, const float* v1, const float* v2, const float* v3, const float* color);
    void drawSphere(const float* center, float radius, const float* color, int segments = 16);
    void drawCylinder(const float* start, const float* end, float radius, const float* color, int segments = 16);
    
    // Textured drawing
    void setTexture(Texture* texture);
    void drawTexturedQuad(const float* v0, const float* v1, const float* v2, const float* v3, 
                         const float* uv0, const float* uv1, const float* uv2, const float* uv3);
    
private:
    VulkanContext* m_context = nullptr;
    Buffer m_vertexBuffer;
    Buffer m_indexBuffer;
    std::vector<ImmediateVertex> m_vertices;
    std::vector<uint32_t> m_indices;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    DescriptorSet* m_descriptorSet = nullptr;
    VkCommandBuffer m_currentCmd = VK_NULL_HANDLE;
};
```

### Volume Rendering Interface (Agent B)
```cpp
// src/gfx/volumerender_utils.h - MIGRATION TARGET
#pragma once
#include "vk_context.h"
#include "vk_texture.h"
#include "vk_pipeline.h"

struct VolumeRenderParams {
    float stepSize = 0.01f;
    float densityScale = 1.0f;
    float brightness = 1.0f;
    float transferOffset = 0.0f;
    float transferScale = 1.0f;
    float clipNear = 0.0f;
    float clipFar = 1.0f;
    float viewMatrix[16];
    float projMatrix[16];
    float invViewMatrix[16];
    float invProjMatrix[16];
};

class VolumeRenderer {
public:
    void initialize(VulkanContext* context);
    void shutdown();
    
    // CRITICAL: Maintain exact API compatibility
    void setVolumeData(Texture* volumeTexture);
    void setTransferFunction(Texture* transferTexture);
    void render(VkCommandBuffer cmd, const VolumeRenderParams& params, 
               Texture* outputTexture, uint32_t width, uint32_t height);
    
    // Performance optimization - new in Vulkan version
    void renderCompute(VkCommandBuffer cmd, const VolumeRenderParams& params, 
                      Texture* outputTexture, uint32_t width, uint32_t height);
    
private:
    VulkanContext* m_context = nullptr;
    VkPipeline m_raycastPipeline = VK_NULL_HANDLE;
    VkPipeline m_computePipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    Buffer m_uniformBuffer;
    DescriptorSet* m_descriptorSet = nullptr;
};
```

### Post-Processing Interface (Agent C)
```cpp
// src/gfx/postprocessing_utils.h - MIGRATION TARGET
#pragma once
#include "vk_context.h"
#include "vk_texture.h"
#include "vk_pipeline.h"

enum class PostProcessEffect {
    SSAO,
    DepthOfField,
    TemporalAA,
    FXAA,
    ToneMapping,
    LumaExtraction
};

struct PostProcessParams {
    // SSAO parameters
    float ssaoRadius = 0.5f;
    float ssaoBias = 0.025f;
    float ssaoIntensity = 1.0f;
    
    // DOF parameters
    float dofFocusDistance = 10.0f;
    float dofFocusRange = 5.0f;
    float dofBlurRadius = 5.0f;
    
    // Temporal AA parameters
    float taaBlendFactor = 0.1f;
    float taaJitterScale = 1.0f;
    
    // Tone mapping parameters
    float exposure = 1.0f;
    float gamma = 2.2f;
};

class PostProcessingChain {
public:
    void initialize(VulkanContext* context);
    void shutdown();
    
    // CRITICAL: Maintain exact API compatibility
    void addEffect(PostProcessEffect effect);
    void removeEffect(PostProcessEffect effect);
    void setParameters(const PostProcessParams& params);
    
    // Main processing function
    void process(VkCommandBuffer cmd, Texture* inputTexture, Texture* outputTexture, 
                uint32_t width, uint32_t height);
    
    // Individual effect processing - for fine control
    void processSSAO(VkCommandBuffer cmd, Texture* depthTexture, Texture* normalTexture, 
                    Texture* outputTexture, const PostProcessParams& params);
    void processDepthOfField(VkCommandBuffer cmd, Texture* colorTexture, Texture* depthTexture, 
                           Texture* outputTexture, const PostProcessParams& params);
    void processTemporalAA(VkCommandBuffer cmd, Texture* currentFrame, Texture* previousFrame, 
                          Texture* motionVectors, Texture* outputTexture, const PostProcessParams& params);
    
private:
    VulkanContext* m_context = nullptr;
    std::vector<PostProcessEffect> m_activeEffects;
    std::map<PostProcessEffect, VkPipeline> m_pipelines;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    Buffer m_uniformBuffer;
    std::vector<DescriptorSet*> m_descriptorSets;
    std::vector<Texture> m_intermediateTextures;
};
```

## Critical Integration Points

### Global Initialization Order (main.cpp)
```cpp
// REQUIRED: Exact initialization sequence for proper startup
void initializeVulkanBackend() {
    // 1. Core context - MUST be first
    g_vulkanContext = new VulkanContext();
    VulkanContext::InitInfo contextInfo{};
    if (!g_vulkanContext->initialize(contextInfo)) {
        // Handle error
        return;
    }
    
    // 2. Resource managers - MUST be after context
    g_stagingManager = new StagingManager();
    g_stagingManager->initialize(g_vulkanContext);
    
    g_descriptorManager = new DescriptorManager();
    g_descriptorManager->initialize(g_vulkanContext);
    
    g_pipelineManager = new PipelineManager();
    g_pipelineManager->initialize(g_vulkanContext);
    
    // 3. Rendering systems - MUST be after managers
    // Agent B and C initialize their systems here
}

void shutdownVulkanBackend() {
    // Reverse order shutdown
    // Rendering systems first
    // Then managers
    // Finally context
}
```

### Error Handling Protocol
```cpp
// REQUIRED: Consistent error handling across all agents
#define VK_CHECK(x) \
    do { \
        VkResult err = x; \
        if (err != VK_SUCCESS) { \
            fprintf(stderr, "Vulkan error: %d at %s:%d\n", err, __FILE__, __LINE__); \
            abort(); \
        } \
    } while (0)

// REQUIRED: Validation layer callback - Agent A implementation
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData);
```

## Interface Validation Checklist

Before integration, each agent must verify:

### Agent A Deliverables:
- [ ] VulkanContext creates instance, device, queues successfully
- [ ] Buffer creation, mapping, staging works
- [ ] Texture creation, upload, sampling works  
- [ ] Descriptor sets can be allocated and bound
- [ ] Command buffers can be allocated and recorded

### Agent B Dependencies on Agent A:
- [ ] Can get graphics queue and command pool
- [ ] Can create vertex/index buffers for immediate drawing
- [ ] Can create uniform buffers for pipeline parameters
- [ ] Can load and compile shaders to SPIR-V
- [ ] Can create graphics and compute pipelines

### Agent C Dependencies on Agent A:
- [ ] Can create render targets and framebuffers
- [ ] Can create descriptor sets for post-processing
- [ ] Can allocate textures for intermediate results
- [ ] Can bind textures as render targets and samplers

### Integration Test Requirements:
- [ ] All agents can initialize without conflicts
- [ ] Resource sharing works between agents
- [ ] Command buffer recording from multiple agents
- [ ] Proper synchronization between rendering phases

This interface specification ensures that all agents can work independently while maintaining perfect integration compatibility.