# VIAMD Vulkan Features and Developer Guide

## Overview

This guide explains how to use the new Vulkan rendering features in VIAMD, including volume rendering, post-processing effects, and the unified backend system.

## Unified Backend System

### Backend Selection

The application now supports seamless switching between OpenGL and Vulkan backends:

```cpp
#include <gfx/vk_rendering_system.h>

// Get the backend manager instance
auto& backend_manager = vulkan_system::backend_switching::BackendManager::instance();

// Automatic backend selection (recommended)
backend_manager.initialize_backend(
    vulkan_system::backend_switching::BackendType::Auto, 
    window_width, window_height
);

// Check which backend is active
if (backend_manager.is_vulkan_active()) {
    // Vulkan-specific optimizations
    auto* vulkan_system = backend_manager.get_vulkan_system();
    auto metrics = vulkan_system->get_performance_metrics();
    printf("Vulkan memory usage: %zu MB\n", metrics.memory_usage_bytes / (1024*1024));
}
```

### Runtime Backend Switching

```cpp
// Switch to Vulkan (if available)
backend_manager.switch_backend(vulkan_system::backend_switching::BackendType::Vulkan);

// Switch back to OpenGL
backend_manager.switch_backend(vulkan_system::backend_switching::BackendType::OpenGL);
```

## Enhanced Volume Rendering

### Basic Volume Rendering

```cpp
#include <gfx/vk_integration.h>

// Create the volume rendering system
integration::VulkanVolumeRenderingSystem volume_system;
volume_system.initialize(device, allocator, transfer_queue, transfer_queue_family);

// Load volume dataset
volume::VolumeDataDesc desc = {};
desc.width = 512;
desc.height = 512; 
desc.depth = 512;
desc.format = VK_FORMAT_R32_SFLOAT;
desc.enable_compression = true;  // Enable LZ4/ZSTD compression

uint32_t volume_id;
volume_system.load_volume_dataset("data/volume.raw", desc, &volume_id);

// Configure adaptive quality
volume_system.set_adaptive_quality(true, 60.0f);  // Target 60 FPS
volume_system.set_memory_budget(4ULL * 1024 * 1024 * 1024);  // 4GB limit
```

### Advanced Volume Rendering with Post-Processing

```cpp
// Setup post-processing effect chain
integration::PostProcessingChainBuilder chain_builder;
auto effect_chain = chain_builder
    .add_ssao(0.5f, 0.025f, 16)  // radius, bias, samples
    .add_dof(10.0f, 2.8f)        // focus_distance, aperture
    .add_fxaa(0.0312f, 0.063f)   // contrast_threshold, relative_threshold
    .create_volume_preset();     // Optimized for volume rendering

// Build the effect chain
postprocessing::EffectType effects[16];
size_t effect_count;
postprocessing::VulkanPostProcessDesc post_desc;
effect_chain.build(effects, effect_count, post_desc);

// Render volume with post-processing
volume::VulkanRenderDesc render_desc = {};
render_desc.view_matrix = view_matrix;
render_desc.proj_matrix = proj_matrix;
render_desc.model_matrix = volume_transform;
render_desc.viewport_width = width;
render_desc.viewport_height = height;

volume_system.render_volume_with_post_processing(
    command_buffer, volume_id, render_desc, post_desc, effects, effect_count
);
```

## Post-Processing Pipeline

### Individual Effects

```cpp
#include <gfx/vk_postprocess_effects.h>

postprocessing::VulkanPostProcessingEffects effects;
effects.initialize(device, allocator, width, height);

// Configure SSAO
postprocessing::SSAOParams ssao = {};
ssao.radius = 0.5f;
ssao.bias = 0.025f;
ssao.samples = 16;
ssao.power = 2.0f;

// Configure Depth of Field
postprocessing::DOFParams dof = {};
dof.focus_distance = 10.0f;
dof.aperture = 2.8f;
dof.focal_length = 50.0f;
dof.sensor_size = 36.0f;

// Configure FXAA
postprocessing::FXAAParams fxaa = {};
fxaa.contrast_threshold = 0.0312f;
fxaa.relative_threshold = 0.063f;
fxaa.subpixel_blending = 0.75f;

// Apply effects in sequence
effects.apply_ssao(command_buffer, input_textures, ssao);
effects.apply_dof(command_buffer, input_textures, dof);
effects.apply_fxaa(command_buffer, input_textures, fxaa);
```

### Effect Chain Builder

```cpp
// Create preset configurations
auto quality_preset = integration::PostProcessingChainBuilder::create_quality_preset();
auto performance_preset = integration::PostProcessingChainBuilder::create_performance_preset();
auto volume_preset = integration::PostProcessingChainBuilder::create_volume_preset();

// Custom effect chains
auto custom_chain = integration::PostProcessingChainBuilder()
    .add_ssao(0.8f, 0.02f, 32)      // High-quality SSAO
    .add_dof(15.0f, 1.4f)           // Shallow depth of field
    .add_tone_mapping()             // HDR tone mapping
    .add_fxaa();                    // Anti-aliasing
```

## ImGui Vulkan Backend

### Setup and Usage

```cpp
#include <app/imgui_integration.h>

// Initialize ImGui with Vulkan backend
imgui_integration::VulkanInitData vulkan_init = {};
vulkan_init.instance = vulkan_instance;
vulkan_init.physical_device = physical_device;
vulkan_init.device = device;
vulkan_init.queue_family = graphics_queue_family;
vulkan_init.queue = graphics_queue;
vulkan_init.descriptor_pool = descriptor_pool;
vulkan_init.render_pass = render_pass;
vulkan_init.min_image_count = 2;
vulkan_init.image_count = 2;

imgui_integration::set_vulkan_init_data(vulkan_init);
imgui_integration::initialize(imgui_integration::Backend::Vulkan, app_context);

// Render loop
while (!window_should_close) {
    imgui_integration::new_frame(imgui_integration::Backend::Vulkan);
    
    // Your ImGui code here
    ImGui::Begin("Vulkan Demo");
    ImGui::Text("Running on Vulkan backend");
    ImGui::End();
    
    imgui_integration::render(imgui_integration::Backend::Vulkan, app_context);
    imgui_integration::render_platform_windows(imgui_integration::Backend::Vulkan);
}

imgui_integration::shutdown(imgui_integration::Backend::Vulkan, app_context);
```

### Multi-Viewport Support

```cpp
// Enable multi-viewport support
ImGuiIO& io = ImGui::GetIO();
io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

// Create dockable windows
ImGui::Begin("Main Viewport");
if (ImGui::Begin("Volume Rendering")) {
    // Volume rendering controls
}
ImGui::End();

if (ImGui::Begin("Post-Processing")) {
    // Post-processing controls
}
ImGui::End();
ImGui::End();
```

## Performance Monitoring

### Real-time Metrics

```cpp
// Get performance metrics from Vulkan system
auto metrics = vulkan_system->get_performance_metrics();

printf("Frame Breakdown:\n");
printf("  Immediate rendering: %.2f ms\n", metrics.immediate_render_time_ms);
printf("  Volume rendering: %.2f ms\n", metrics.volume_render_time_ms);
printf("  Post-processing: %.2f ms\n", metrics.post_process_time_ms);
printf("  UI rendering: %.2f ms\n", metrics.ui_render_time_ms);
printf("  Total frame time: %.2f ms\n", metrics.total_frame_time_ms);
printf("  Memory usage: %zu MB\n", metrics.memory_usage_bytes / (1024*1024));
```

### Performance Profiler

```cpp
#include <gfx/vk_integration.h>

integration::utils::PerformanceProfiler profiler;

// Begin profiling
profiler.begin_frame();

profiler.begin_section("Volume Rendering");
// ... volume rendering code ...
profiler.end_section();

profiler.begin_section("Post-Processing");
// ... post-processing code ...
profiler.end_section();

profiler.end_frame();

// Get profiling results
auto frame_data = profiler.get_frame_data();
float avg_frame_time = profiler.get_average_frame_time();

for (const auto& section : frame_data) {
    printf("%s: %.2f ms (%.1f%%)\n", section.name, section.time_ms, section.percentage);
}
```

## Integration with Existing Systems

### Immediate Mode Rendering

```cpp
// The system automatically uses the appropriate backend
immediate::set_model_view_matrix(view_matrix);
immediate::set_proj_matrix(proj_matrix);

// Draw wireframe box (works with both OpenGL and Vulkan)
immediate::draw_box_wireframe({-1,-1,-1}, {1,1,1}, transform_matrix, color);

// Render (backend-agnostic)
immediate::render();
```

### Volume Data Integration

```cpp
// Load volume data using existing utilities
auto volume_data = load_volume_from_file("data/protein.vol");

// Create volume texture (automatically uses Vulkan if available)
uint32_t volume_texture = volume::create_texture_3d(
    volume_data.width, volume_data.height, volume_data.depth,
    volume_data.format, volume_data.data
);

// Render using volume utilities (backend-agnostic)
volume::RenderDesc desc = {};
desc.texture.volume = volume_texture;
desc.matrix.model = volume_transform;
desc.matrix.view = view_matrix;
desc.matrix.proj = proj_matrix;

volume::render_volume(desc);
```

## Configuration and Customization

### CMake Configuration

```cmake
# Enable specific Vulkan features
set(VIAMD_ENABLE_VULKAN ON)
set(VIAMD_VULKAN_VALIDATION ON)  # Debug builds
set(VIAMD_VULKAN_COMPUTE_SHADERS ON)
set(VIAMD_VULKAN_VOLUME_RENDERING ON)
set(VIAMD_VULKAN_POST_PROCESSING ON)
set(VIAMD_VULKAN_MULTI_VIEWPORT ON)

# Performance tuning
set(VIAMD_VULKAN_MEMORY_BUDGET_MB "4096")
set(VIAMD_VULKAN_STAGING_BUFFER_SIZE_MB "256")
set(VIAMD_VULKAN_COMMAND_BUFFER_COUNT "3")
```

### Runtime Configuration

```cpp
// Set memory budget at runtime
if (backend_manager.is_vulkan_active()) {
    auto* vulkan_system = backend_manager.get_vulkan_system();
    auto* volume_system = vulkan_system->get_volume_system();
    
    // Set memory budget (4GB)
    volume_system->set_memory_budget(4ULL * 1024 * 1024 * 1024);
    
    // Enable adaptive quality targeting 60 FPS
    volume_system->set_adaptive_quality(true, 60.0f);
}
```

## Migration from OpenGL Code

### Immediate Rendering

**Before (OpenGL):**
```cpp
glBegin(GL_TRIANGLES);
glVertex3f(0.0f, 1.0f, 0.0f);
glVertex3f(-1.0f, -1.0f, 0.0f);
glVertex3f(1.0f, -1.0f, 0.0f);
glEnd();
```

**After (Backend-agnostic):**
```cpp
immediate::Vertex vertices[] = {
    {{0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}}
};
immediate::draw_triangles_v(vertices, 3, immediate::COLOR_WHITE);
immediate::render();
```

### Volume Rendering

**Before (OpenGL):**
```cpp
glBindTexture(GL_TEXTURE_3D, volume_texture);
glUseProgram(volume_shader);
// ... manual uniform setup ...
glDrawArrays(GL_TRIANGLES, 0, 36);
```

**After (Vulkan/OpenGL):**
```cpp
volume::RenderDesc desc = {};
desc.texture.volume = volume_texture;
desc.matrix.view = view_matrix;
desc.matrix.proj = proj_matrix;
volume::render_volume(desc);
```

## Best Practices

### Performance Optimization

1. **Use adaptive quality for volume rendering**:
   ```cpp
   volume_system->set_adaptive_quality(true, 60.0f);
   ```

2. **Batch immediate mode calls**:
   ```cpp
   immediate::begin_batch();
   for (const auto& object : objects) {
       immediate::draw_object(object);
   }
   immediate::end_batch();
   immediate::render();
   ```

3. **Monitor memory usage**:
   ```cpp
   auto metrics = vulkan_system->get_performance_metrics();
   if (metrics.memory_usage_bytes > memory_budget) {
       // Reduce quality or free unused resources
   }
   ```

### Error Handling

```cpp
// Check if Vulkan is available
auto caps = vulkan_system::backend_switching::detect_backend_capabilities();
if (!caps.vulkan_available) {
    // Fall back to OpenGL
    backend_manager.initialize_backend(
        vulkan_system::backend_switching::BackendType::OpenGL,
        width, height
    );
}

// Handle initialization failures
if (!backend_manager.initialize_backend(BackendType::Vulkan, width, height)) {
    MD_LOG_ERROR("Failed to initialize Vulkan, falling back to OpenGL");
    backend_manager.initialize_backend(BackendType::OpenGL, width, height);
}
```

This guide provides comprehensive coverage of the new Vulkan features while maintaining compatibility with existing OpenGL code paths.