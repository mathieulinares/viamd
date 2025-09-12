#include "vk_volume_shaders.h"

namespace volume_shaders {

// Placeholder SPIR-V data - In a real implementation, these would be generated
// from the GLSL shaders using glslc or the shaderc library

// For now, we'll use minimal placeholder data
// In practice, you would compile the shaders like this:
// glslc src/shaders/volume/raycaster.comp -o raycaster.comp.spv
// Then include the binary data here

// Minimal SPIR-V header for validation
const uint32_t raycaster_comp_spv[] = {
    0x07230203, 0x00010000, 0x00080001, 0x00000001, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    // ... rest of SPIR-V bytecode would go here
};
const size_t raycaster_comp_spv_size = sizeof(raycaster_comp_spv);

const uint32_t entry_exit_vert_spv[] = {
    0x07230203, 0x00010000, 0x00080001, 0x00000001, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    // ... rest of SPIR-V bytecode would go here
};
const size_t entry_exit_vert_spv_size = sizeof(entry_exit_vert_spv);

const uint32_t entry_exit_frag_spv[] = {
    0x07230203, 0x00010000, 0x00080001, 0x00000001, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    // ... rest of SPIR-V bytecode would go here
};
const size_t entry_exit_frag_spv_size = sizeof(entry_exit_frag_spv);

}