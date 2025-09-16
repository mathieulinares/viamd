#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

// Forward declarations
struct ApplicationState;

namespace OpenMMDynamics {

    // Force field options
    enum class ForceField {
        AMBER14,
        AMBER99SB,
        CHARMM36,
        OPLS_AA,
        Count
    };

    // Water model options  
    enum class WaterModel {
        None,
        TIP3P,
        TIP4P,
        SPC,
        SPCE,
        Count
    };

    // Simulation protocol options
    enum class ProtocolType {
        Minimization,
        NVT_Equilibration,
        NPT_Equilibration,
        Production_NVT,
        Production_NPT,
        Heating,
        Cooling,
        Count
    };

    // Simulation state
    enum class SimulationState {
        NotInitialized,
        Ready,
        Running,
        Paused,
        Completed,
        Error
    };

    // Protocol parameters structure
    struct ProtocolParameters {
        int n_steps = 10000;
        float temperature = 300.0f;          // Kelvin
        float pressure = 1.0f;               // bar (for NPT)
        float time_step = 0.002f;            // ps
        float friction_coefficient = 1.0f;   // 1/ps
        int report_interval = 1000;
        float minimize_tolerance = 10.0f;
        int minimize_max_iterations = 1000;
        bool use_pbc = true;
        float nonbonded_cutoff = 1.0f;       // nm
        std::string constraints = "HBonds";
        std::string nonbonded_method = "PME";
    };

    // Simulation progress info
    struct SimulationProgress {
        int current_step = 0;
        int total_steps = 0;
        float current_temperature = 0.0f;
        float current_pressure = 0.0f;
        float potential_energy = 0.0f;
        float kinetic_energy = 0.0f;
        float total_energy = 0.0f;
        float ns_per_day = 0.0f;
        std::string status_message = "";
        bool has_error = false;
        std::string error_message = "";
    };

    // Main OpenMM dynamics interface
    class OpenMMDynamicsInterface {
    public:
        OpenMMDynamicsInterface();
        ~OpenMMDynamicsInterface();

        // Initialize with current molecular system
        bool initialize(const ApplicationState& state);
        
        // Setup simulation system
        bool setup_system(ForceField force_field, WaterModel water_model = WaterModel::None);
        
        // Run simulation protocols
        bool run_protocol(ProtocolType protocol, const ProtocolParameters& params);
        
        // Control simulation
        bool start_simulation();
        bool pause_simulation();
        bool stop_simulation();
        bool reset_simulation();
        
        // Get current state
        SimulationState get_state() const { return state_; }
        SimulationProgress get_progress() const { return progress_; }
        
        // Update coordinates in VIAMD from simulation
        bool update_viamd_coordinates(ApplicationState& state);
        
        // Export results
        bool export_trajectory(const std::string& filename, const std::string& format = "xyz");
        
        // Callbacks for real-time updates
        void set_progress_callback(std::function<void(const SimulationProgress&)> callback);
        void set_coordinate_callback(std::function<void(const std::vector<float>&)> callback);

    private:
        SimulationState state_;
        SimulationProgress progress_;
        ForceField current_force_field_;
        WaterModel current_water_model_;
        ProtocolParameters current_params_;
        
        // Python interface handles
        void* python_runner_;  // DynamicsRunner instance
        void* python_module_;  // Python module reference
        
        // Callbacks
        std::function<void(const SimulationProgress&)> progress_callback_;
        std::function<void(const std::vector<float>&)> coordinate_callback_;
        
        // Internal methods
        bool initialize_python();
        bool create_dynamics_runner();
        void cleanup_python();
        void update_progress();
        
        // Utility methods
        std::string force_field_to_string(ForceField ff) const;
        std::string water_model_to_string(WaterModel wm) const;
        std::string protocol_to_string(ProtocolType pt) const;
    };

    // GUI State
    struct GUIState {
        bool show_window = false;
        bool show_advanced_options = false;
        bool auto_update_visualization = true;
        
        // Current selections
        ForceField selected_force_field = ForceField::AMBER14;
        WaterModel selected_water_model = WaterModel::TIP3P;
        ProtocolType selected_protocol = ProtocolType::Minimization;
        
        // Protocol parameters
        ProtocolParameters params;
        
        // Visualization
        bool show_progress_plot = true;
        bool show_energy_plot = true;
        float plot_history_length = 100.0f;  // seconds
        
        // Export options
        char export_filename[256] = "trajectory";
        int export_format = 0;  // 0=XYZ, 1=PDB, 2=JSON
        
        // Status
        std::string last_error = "";
        float last_update_time = 0.0f;
        
        // Performance
        std::vector<float> temperature_history;
        std::vector<float> energy_history;
        std::vector<float> time_history;
        int max_history_points = 1000;
    };

    // Main GUI functions
    void draw_openmm_dynamics_window(ApplicationState& state, OpenMMDynamicsInterface& interface, GUIState& gui_state);
    void draw_protocol_parameters(ProtocolParameters& params, ProtocolType protocol_type);
    void draw_simulation_controls(OpenMMDynamicsInterface& interface, GUIState& gui_state);
    void draw_progress_monitoring(const SimulationProgress& progress, GUIState& gui_state);
    void draw_visualization_controls(ApplicationState& state, GUIState& gui_state);
    void draw_export_options(OpenMMDynamicsInterface& interface, GUIState& gui_state);

    // Utility functions
    const char* get_force_field_name(ForceField ff);
    const char* get_water_model_name(WaterModel wm);
    const char* get_protocol_name(ProtocolType pt);
    const char* get_simulation_state_name(SimulationState state);

    // Python interpreter management
    void initialize_global_python_interpreter();
    void cleanup_global_python_interpreter();
    bool is_python_initialized();

} // namespace OpenMMDynamics