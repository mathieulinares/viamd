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
    
    // Setup OpenMM system with current molecule
    static bool setup_system();
    
    // Get available force field types
    static const char** get_force_field_names(int* count);
    
    // Minimize energy of current molecule (can be called by other components)
    static bool minimize_energy_if_available();
    
    // Get current simulation status
    static bool is_simulation_running();
    
    // Get current energy if available
    static double get_current_energy();
    
    // Get atom type assignments
    static const char* get_atom_type(size_t atom_index);
    
    // Force field selection
    static void set_force_field(int force_field_index);
    static int get_current_force_field();
};

} // namespace openmm

#endif // VIAMD_ENABLE_OPENMM