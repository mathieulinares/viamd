#pragma once

#include <cstdint>
#include <cstddef>

namespace volume_shaders {

// SPIR-V shader data
// These would be generated from the GLSL shaders using glslc or similar tools

// Compute shader for volume ray-casting
extern const uint32_t raycaster_comp_spv[];
extern const size_t raycaster_comp_spv_size;

// Entry/exit point shaders
extern const uint32_t entry_exit_vert_spv[];
extern const size_t entry_exit_vert_spv_size;

extern const uint32_t entry_exit_frag_spv[];
extern const size_t entry_exit_frag_spv_size;

}