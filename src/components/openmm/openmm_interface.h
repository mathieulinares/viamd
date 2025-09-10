#pragma once

#include <viamd.h>

namespace openmm_interface {
    // Public interface for other components to trigger energy minimization
    void minimize_energy_if_available(ApplicationState& state);
    
    // Simulation frame management functions
    size_t get_simulation_frame_count(const ApplicationState& state);
    bool load_simulation_frame(ApplicationState& state, size_t frame_idx);
    
    // Export trajectory to viamd
    bool export_trajectory_to_viamd(ApplicationState& state);
}