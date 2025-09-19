#pragma once

#ifdef VIAMD_ENABLE_OPENMM

namespace openmm {

// Public interface for OpenMM integration that can be accessed by other components
class OpenMMInterface {
public:
    // Check if OpenMM is available and initialized
    static bool is_available();
    
    // Check if a simulation system is initialized
    static bool is_system_initialized();
    
    // Minimize energy of current molecule (can be called by other components)
    static bool minimize_energy_if_available();
    
    // Get current simulation status
    static bool is_simulation_running();
    
    // Get current energy if available
    static double get_current_energy();
};

} // namespace openmm

#endif // VIAMD_ENABLE_OPENMM