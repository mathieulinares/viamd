#define IMGUI_DEFINE_MATH_OPERATORS

#include "openmm_dynamics.h"
#include <viamd.h>
#include <event.h>

#include <imgui.h>
#include <imgui_widgets.h>
#include <implot_widgets.h>
#include <app/IconsFontAwesome6.h>

#include <core/md_log.h>
#include <core/md_str.h>

#include <cstring>
#include <algorithm>

// Include Python integration if available
#ifdef VIAMD_ENABLE_PYTHON
#include <Python.h>
#include <pybind11/embed.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
namespace py = pybind11;

// Global interpreter guard to prevent multiple initialization
static py::scoped_interpreter* g_python_interpreter = nullptr;
static bool g_python_initialized = false;
#endif

namespace OpenMMDynamics {

static const char* force_field_names[(int)ForceField::Count] = {
    "AMBER14",
    "AMBER99SB", 
    "CHARMM36",
    "OPLS-AA"
};

static const char* water_model_names[(int)WaterModel::Count] = {
    "None",
    "TIP3P",
    "TIP4P",
    "SPC",
    "SPC/E"
};

static const char* protocol_names[(int)ProtocolType::Count] = {
    "Energy Minimization",
    "NVT Equilibration",
    "NPT Equilibration", 
    "Production NVT",
    "Production NPT",
    "Heating Protocol",
    "Cooling Protocol"
};

static const char* simulation_state_names[] = {
    "Not Initialized",
    "Ready",
    "Running",
    "Paused", 
    "Completed",
    "Error"
};

const char* get_force_field_name(ForceField ff) {
    return force_field_names[(int)ff];
}

const char* get_water_model_name(WaterModel wm) {
    return water_model_names[(int)wm];
}

const char* get_protocol_name(ProtocolType pt) {
    return protocol_names[(int)pt];
}

const char* get_simulation_state_name(SimulationState state) {
    return simulation_state_names[(int)state];
}

// OpenMMDynamicsInterface Implementation
OpenMMDynamicsInterface::OpenMMDynamicsInterface() 
    : state_(SimulationState::NotInitialized)
    , current_force_field_(ForceField::AMBER14)
    , current_water_model_(WaterModel::TIP3P)
    , python_runner_(nullptr)
    , python_module_(nullptr) {
}

OpenMMDynamicsInterface::~OpenMMDynamicsInterface() {
    cleanup_python();
}

bool OpenMMDynamicsInterface::initialize(const ApplicationState& state) {
    if (!state.mold.mol.atom.count) {
        progress_.has_error = true;
        progress_.error_message = "No molecular system loaded";
        return false;
    }

#ifdef VIAMD_ENABLE_PYTHON
    if (!initialize_python()) {
        return false;
    }
    
    if (!create_dynamics_runner()) {
        return false;
    }
    
    state_ = SimulationState::Ready;
    progress_.status_message = "System initialized successfully";
    return true;
#else
    progress_.has_error = true;
    progress_.error_message = "Python bindings not available. Build with VIAMD_ENABLE_PYTHON=ON";
    return false;
#endif
}

bool OpenMMDynamicsInterface::initialize_python() {
#ifdef VIAMD_ENABLE_PYTHON
    try {
        // Check if Python interpreter is already initialized globally
        if (!g_python_initialized) {
            progress_.has_error = true;
            progress_.error_message = "Python interpreter not initialized. This should be done at application startup.";
            return false;
        }
        
        // Import pyviamd dynamics module
        py::module_ pyviamd = py::module_::import("pyviamd.dynamics");
        python_module_ = new py::module_(pyviamd);
        return true;
    } catch (const std::exception& e) {
        progress_.has_error = true;
        progress_.error_message = "Failed to import pyviamd.dynamics: " + std::string(e.what());
        return false;
    }
#else
    return false;
#endif
}

bool OpenMMDynamicsInterface::create_dynamics_runner() {
#ifdef VIAMD_ENABLE_PYTHON
    try {
        py::module_& mod = *static_cast<py::module_*>(python_module_);
        py::object DynamicsRunner = mod.attr("DynamicsRunner");
        
        // Create DynamicsRunner with current molecular system
        // For now, we'll assume the system is already loaded in VIAMD
        py::object runner = DynamicsRunner();
        python_runner_ = new py::object(runner);
        
        return true;
    } catch (const std::exception& e) {
        progress_.has_error = true;
        progress_.error_message = "Failed to create DynamicsRunner: " + std::string(e.what());
        return false;
    }
#else
    return false;
#endif
}

void OpenMMDynamicsInterface::cleanup_python() {
#ifdef VIAMD_ENABLE_PYTHON
    if (python_runner_) {
        delete static_cast<py::object*>(python_runner_);
        python_runner_ = nullptr;
    }
    if (python_module_) {
        delete static_cast<py::module_*>(python_module_);
        python_module_ = nullptr;
    }
#endif
}

bool OpenMMDynamicsInterface::setup_system(ForceField force_field, WaterModel water_model) {
    current_force_field_ = force_field;
    current_water_model_ = water_model;
    
#ifdef VIAMD_ENABLE_PYTHON
    try {
        py::object& runner = *static_cast<py::object*>(python_runner_);
        
        // Setup OpenMM system with selected force field and water model
        std::string ff_str = force_field_to_string(force_field);
        std::string wm_str = water_model_to_string(water_model);
        
        if (wm_str != "None") {
            runner.attr("setup_system")(ff_str, py::arg("water_model") = wm_str);
        } else {
            runner.attr("setup_system")(ff_str);
        }
        
        progress_.status_message = "System setup complete with " + ff_str;
        if (wm_str != "None") {
            progress_.status_message += " and " + wm_str;
        }
        
        return true;
    } catch (const std::exception& e) {
        progress_.has_error = true;
        progress_.error_message = "System setup failed: " + std::string(e.what());
        return false;
    }
#else
    return false;
#endif
}

bool OpenMMDynamicsInterface::run_protocol(ProtocolType protocol, const ProtocolParameters& params) {
    current_params_ = params;
    
#ifdef VIAMD_ENABLE_PYTHON
    try {
        py::object& runner = *static_cast<py::object*>(python_runner_);
        py::module_& mod = *static_cast<py::module_*>(python_module_);
        
        // Create ProtocolParameters object
        py::object ProtocolParameters = mod.attr("ProtocolParameters");
        // Create pressure parameter - handle NPT vs NVT protocols
        py::object pressure_param;
        if (protocol == ProtocolType::NPT_Equilibration || protocol == ProtocolType::Production_NPT) {
            pressure_param = py::cast(params.pressure);
        } else {
            pressure_param = py::none();
        }
        
        py::object py_params = ProtocolParameters(
            py::arg("temperature") = params.temperature,
            py::arg("pressure") = pressure_param,
            py::arg("time_step") = params.time_step,
            py::arg("n_steps") = params.n_steps,
            py::arg("report_interval") = params.report_interval,
            py::arg("friction_coefficient") = params.friction_coefficient,
            py::arg("minimize_tolerance") = params.minimize_tolerance,
            py::arg("minimize_max_iterations") = params.minimize_max_iterations,
            py::arg("use_pbc") = params.use_pbc,
            py::arg("nonbonded_cutoff") = params.nonbonded_cutoff,
            py::arg("constraints") = params.constraints,
            py::arg("nonbonded_method") = params.nonbonded_method
        );
        
        // Get SimulationProtocol enum
        py::object SimulationProtocol = mod.attr("SimulationProtocol");
        py::object protocol_enum;
        
        switch (protocol) {
            case ProtocolType::Minimization:
                protocol_enum = SimulationProtocol.attr("MINIMIZATION");
                break;
            case ProtocolType::NVT_Equilibration:
                protocol_enum = SimulationProtocol.attr("NVT_EQUILIBRATION");
                break;
            case ProtocolType::NPT_Equilibration:
                protocol_enum = SimulationProtocol.attr("NPT_EQUILIBRATION");
                break;
            case ProtocolType::Production_NVT:
                protocol_enum = SimulationProtocol.attr("PRODUCTION_NVT");
                break;
            case ProtocolType::Production_NPT:
                protocol_enum = SimulationProtocol.attr("PRODUCTION_NPT");
                break;
            case ProtocolType::Heating:
                protocol_enum = SimulationProtocol.attr("HEATING");
                break;
            case ProtocolType::Cooling:
                protocol_enum = SimulationProtocol.attr("COOLING");
                break;
            default:
                protocol_enum = SimulationProtocol.attr("MINIMIZATION");
        }
        
        // Run protocol
        state_ = SimulationState::Running;
        progress_.current_step = 0;
        progress_.total_steps = params.n_steps;
        progress_.status_message = "Running " + std::string(get_protocol_name(protocol));
        
        // This would be run in a separate thread in a real implementation
        py::object result = runner.attr("run_protocol")(protocol_enum, py_params);
        
        state_ = SimulationState::Completed;
        progress_.current_step = params.n_steps;
        progress_.status_message = "Protocol completed successfully";
        
        return true;
    } catch (const std::exception& e) {
        state_ = SimulationState::Error;
        progress_.has_error = true;
        progress_.error_message = "Protocol execution failed: " + std::string(e.what());
        return false;
    }
#else
    return false;
#endif
}

bool OpenMMDynamicsInterface::start_simulation() {
    state_ = SimulationState::Running;
    progress_.status_message = "Simulation started";
    return true;
}

bool OpenMMDynamicsInterface::pause_simulation() {
    if (state_ == SimulationState::Running) {
        state_ = SimulationState::Paused;
        progress_.status_message = "Simulation paused";
        return true;
    }
    return false;
}

bool OpenMMDynamicsInterface::stop_simulation() {
    if (state_ == SimulationState::Running || state_ == SimulationState::Paused) {
        state_ = SimulationState::Completed;
        progress_.status_message = "Simulation stopped";
        return true;
    }
    return false;
}

bool OpenMMDynamicsInterface::reset_simulation() {
    state_ = SimulationState::Ready;
    progress_ = SimulationProgress{}; // Reset progress
    progress_.status_message = "Simulation reset";
    return true;
}

bool OpenMMDynamicsInterface::export_trajectory(const std::string& filename, const std::string& format) {
#ifdef VIAMD_ENABLE_PYTHON
    try {
        py::object& runner = *static_cast<py::object*>(python_runner_);
        runner.attr("export_results")(filename, format);
        return true;
    } catch (const std::exception& e) {
        progress_.has_error = true;
        progress_.error_message = "Export failed: " + std::string(e.what());
        return false;
    }
#else
    return false;
#endif
}

std::string OpenMMDynamicsInterface::force_field_to_string(ForceField ff) const {
    switch (ff) {
        case ForceField::AMBER14: return "amber14";
        case ForceField::AMBER99SB: return "amber99sb";
        case ForceField::CHARMM36: return "charmm36";
        case ForceField::OPLS_AA: return "opls_aa";
        default: return "amber14";
    }
}

std::string OpenMMDynamicsInterface::water_model_to_string(WaterModel wm) const {
    switch (wm) {
        case WaterModel::None: return "None";
        case WaterModel::TIP3P: return "tip3p";
        case WaterModel::TIP4P: return "tip4p";
        case WaterModel::SPC: return "spc";
        case WaterModel::SPCE: return "spce";
        default: return "None";
    }
}

// GUI Implementation
void draw_openmm_dynamics_window(ApplicationState& state, OpenMMDynamicsInterface& interface, GUIState& gui_state) {
    if (!gui_state.show_window) return;

    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin(ICON_FA_FLASK " OpenMM Dynamics", &gui_state.show_window)) {
        
        // Status bar
        SimulationProgress progress = interface.get_progress();
        SimulationState sim_state = interface.get_state();
        
        if (progress.has_error) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            ImGui::Text(ICON_FA_TRIANGLE_EXCLAMATION " Error: %s", progress.error_message.c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::Text("Status: %s", get_simulation_state_name(sim_state));
            if (!progress.status_message.empty()) {
                ImGui::SameLine();
                ImGui::Text("- %s", progress.status_message.c_str());
            }
        }
        
        ImGui::Separator();
        
        if (ImGui::BeginTabBar("OpenMM_Tabs")) {
            
            // Setup Tab
            if (ImGui::BeginTabItem(ICON_FA_GEAR " Setup")) {
                
                ImGui::Text("System Configuration");
                ImGui::Separator();
                
                // Force field selection
                ImGui::Text("Force Field:");
                ImGui::SameLine();
                int ff_idx = (int)gui_state.selected_force_field;
                if (ImGui::Combo("##ForceField", &ff_idx, force_field_names, (int)ForceField::Count)) {
                    gui_state.selected_force_field = (ForceField)ff_idx;
                }
                
                // Water model selection
                ImGui::Text("Water Model:");
                ImGui::SameLine();
                int wm_idx = (int)gui_state.selected_water_model;
                if (ImGui::Combo("##WaterModel", &wm_idx, water_model_names, (int)WaterModel::Count)) {
                    gui_state.selected_water_model = (WaterModel)wm_idx;
                }
                
                ImGui::Spacing();
                
                // Initialize/Setup buttons
                if (sim_state == SimulationState::NotInitialized) {
                    if (ImGui::Button(ICON_FA_CIRCLE_PLAY " Initialize System")) {
                        interface.initialize(state);
                    }
                } else if (sim_state == SimulationState::Ready || sim_state == SimulationState::Completed) {
                    if (ImGui::Button(ICON_FA_GEAR " Setup System")) {
                        interface.setup_system(gui_state.selected_force_field, gui_state.selected_water_model);
                    }
                }
                
                ImGui::EndTabItem();
            }
            
            // Protocol Tab
            if (ImGui::BeginTabItem(ICON_FA_FLASK " Protocol")) {
                
                ImGui::Text("Simulation Protocol");
                ImGui::Separator();
                
                // Protocol selection
                int protocol_idx = (int)gui_state.selected_protocol;
                if (ImGui::Combo("Protocol Type", &protocol_idx, protocol_names, (int)ProtocolType::Count)) {
                    gui_state.selected_protocol = (ProtocolType)protocol_idx;
                }
                
                ImGui::Spacing();
                
                // Protocol parameters
                draw_protocol_parameters(gui_state.params, gui_state.selected_protocol);
                
                ImGui::Spacing();
                
                // Simulation controls
                draw_simulation_controls(interface, gui_state);
                
                ImGui::EndTabItem();
            }
            
            // Monitoring Tab
            if (ImGui::BeginTabItem(ICON_FA_CHART_LINE " Monitor")) {
                
                draw_progress_monitoring(progress, gui_state);
                
                ImGui::EndTabItem();
            }
            
            // Visualization Tab
            if (ImGui::BeginTabItem(ICON_FA_EYE " Visualization")) {
                
                draw_visualization_controls(state, gui_state);
                
                ImGui::EndTabItem();
            }
            
            // Export Tab
            if (ImGui::BeginTabItem(ICON_FA_DOWNLOAD " Export")) {
                
                draw_export_options(interface, gui_state);
                
                ImGui::EndTabItem();
            }
            
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void draw_protocol_parameters(ProtocolParameters& params, ProtocolType protocol_type) {
    ImGui::Text("Parameters");
    ImGui::Separator();
    
    // Common parameters
    ImGui::InputInt("Steps", &params.n_steps);
    if (params.n_steps < 1) params.n_steps = 1;
    
    if (protocol_type != ProtocolType::Minimization) {
        ImGui::InputFloat("Temperature (K)", &params.temperature, 1.0f, 10.0f, "%.1f");
        if (params.temperature < 0) params.temperature = 0;
        
        ImGui::InputFloat("Time Step (ps)", &params.time_step, 0.001f, 0.01f, "%.3f");
        if (params.time_step < 0.001f) params.time_step = 0.001f;
        
        ImGui::InputFloat("Friction (1/ps)", &params.friction_coefficient, 0.1f, 1.0f, "%.1f");
        if (params.friction_coefficient < 0.1f) params.friction_coefficient = 0.1f;
    }
    
    if (protocol_type == ProtocolType::NPT_Equilibration || protocol_type == ProtocolType::Production_NPT) {
        ImGui::InputFloat("Pressure (bar)", &params.pressure, 0.1f, 1.0f, "%.1f");
        if (params.pressure < 0) params.pressure = 0;
    }
    
    ImGui::InputInt("Report Interval", &params.report_interval);
    if (params.report_interval < 1) params.report_interval = 1;
    
    // Minimization specific
    if (protocol_type == ProtocolType::Minimization) {
        ImGui::InputFloat("Tolerance", &params.minimize_tolerance, 1.0f, 10.0f, "%.1f");
        ImGui::InputInt("Max Iterations", &params.minimize_max_iterations);
    }
    
    // Advanced options
    if (ImGui::CollapsingHeader("Advanced Options")) {
        ImGui::Checkbox("Use PBC", &params.use_pbc);
        ImGui::InputFloat("Nonbonded Cutoff (nm)", &params.nonbonded_cutoff, 0.1f, 0.5f, "%.1f");
        
        // Constraints combo
        const char* constraint_options[] = {"None", "HBonds", "AllBonds"};
        int constraint_idx = 0;
        if (params.constraints == "HBonds") constraint_idx = 1;
        else if (params.constraints == "AllBonds") constraint_idx = 2;
        
        if (ImGui::Combo("Constraints", &constraint_idx, constraint_options, 3)) {
            switch (constraint_idx) {
                case 0: params.constraints = "None"; break;
                case 1: params.constraints = "HBonds"; break; 
                case 2: params.constraints = "AllBonds"; break;
            }
        }
        
        // Nonbonded method combo
        const char* nb_methods[] = {"PME", "Ewald", "CutoffPeriodic", "CutoffNonPeriodic"};
        int nb_idx = 0;
        if (params.nonbonded_method == "Ewald") nb_idx = 1;
        else if (params.nonbonded_method == "CutoffPeriodic") nb_idx = 2;
        else if (params.nonbonded_method == "CutoffNonPeriodic") nb_idx = 3;
        
        if (ImGui::Combo("Nonbonded Method", &nb_idx, nb_methods, 4)) {
            switch (nb_idx) {
                case 0: params.nonbonded_method = "PME"; break;
                case 1: params.nonbonded_method = "Ewald"; break;
                case 2: params.nonbonded_method = "CutoffPeriodic"; break;
                case 3: params.nonbonded_method = "CutoffNonPeriodic"; break;
            }
        }
    }
}

void draw_simulation_controls(OpenMMDynamicsInterface& interface, GUIState& gui_state) {
    ImGui::Text("Simulation Control");
    ImGui::Separator();
    
    SimulationState state = interface.get_state();
    
    if (state == SimulationState::Ready || state == SimulationState::Completed) {
        if (ImGui::Button(ICON_FA_CIRCLE_PLAY " Run Protocol", ImVec2(120, 0))) {
            interface.run_protocol(gui_state.selected_protocol, gui_state.params);
        }
    }
    
    if (state == SimulationState::Running) {
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CIRCLE_PAUSE " Pause", ImVec2(80, 0))) {
            interface.pause_simulation();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CIRCLE_STOP " Stop", ImVec2(80, 0))) {
            interface.stop_simulation();
        }
    }
    
    if (state != SimulationState::NotInitialized) {
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ROTATE " Reset", ImVec2(80, 0))) {
            interface.reset_simulation();
        }
    }
}

void draw_progress_monitoring(const SimulationProgress& progress, GUIState& gui_state) {
    ImGui::Text("Simulation Progress");
    ImGui::Separator();
    
    // Progress bar
    if (progress.total_steps > 0) {
        float progress_fraction = (float)progress.current_step / (float)progress.total_steps;
        ImGui::ProgressBar(progress_fraction, ImVec2(-1, 0), nullptr);
        ImGui::Text("Step %d / %d (%.1f%%)", 
                   progress.current_step, progress.total_steps, progress_fraction * 100.0f);
    }
    
    // Current values
    if (progress.current_temperature > 0) {
        ImGui::Text("Temperature: %.1f K", progress.current_temperature);
    }
    if (progress.current_pressure > 0) {
        ImGui::Text("Pressure: %.1f bar", progress.current_pressure);
    }
    if (progress.potential_energy != 0) {
        ImGui::Text("Potential Energy: %.2f kJ/mol", progress.potential_energy);
    }
    if (progress.kinetic_energy != 0) {
        ImGui::Text("Kinetic Energy: %.2f kJ/mol", progress.kinetic_energy);
    }
    if (progress.ns_per_day > 0) {
        ImGui::Text("Performance: %.2f ns/day", progress.ns_per_day);
    }
    
    // Energy plots
    if (gui_state.show_energy_plot && !gui_state.energy_history.empty()) {
        if (ImPlot::BeginPlot("Energy vs Time", ImVec2(-1, 200))) {
            if (!gui_state.time_history.empty() && gui_state.time_history.size() == gui_state.energy_history.size()) {
                ImPlot::PlotLine("Energy", gui_state.time_history.data(), gui_state.energy_history.data(), 
                               gui_state.energy_history.size());
            }
            ImPlot::EndPlot();
        }
    }
}

void draw_visualization_controls(ApplicationState& state, GUIState& gui_state) {
    ImGui::Text("Visualization Options");
    ImGui::Separator();
    
    ImGui::Checkbox("Auto-update visualization", &gui_state.auto_update_visualization);
    
    if (!gui_state.auto_update_visualization) {
        if (ImGui::Button("Update Now")) {
            // Trigger manual update
        }
    }
    
    ImGui::Spacing();
    ImGui::Text("Real-time Display");
    ImGui::Checkbox("Show progress plots", &gui_state.show_progress_plot);
    ImGui::Checkbox("Show energy plots", &gui_state.show_energy_plot);
    
    ImGui::SliderFloat("Plot history (s)", &gui_state.plot_history_length, 10.0f, 1000.0f, "%.0f");
}

void draw_export_options(OpenMMDynamicsInterface& interface, GUIState& gui_state) {
    ImGui::Text("Export Trajectory");
    ImGui::Separator();
    
    ImGui::InputText("Filename", gui_state.export_filename, sizeof(gui_state.export_filename));
    
    const char* formats[] = {"XYZ", "PDB", "JSON"};
    ImGui::Combo("Format", &gui_state.export_format, formats, 3);
    
    if (ImGui::Button("Export Trajectory")) {
        std::string filename = gui_state.export_filename;
        std::string format = formats[gui_state.export_format];
        std::transform(format.begin(), format.end(), format.begin(), ::tolower);
        
        interface.export_trajectory(filename, format);
    }
}

// Add missing method implementations
void OpenMMDynamicsInterface::set_progress_callback(std::function<void(const SimulationProgress&)> callback) {
    progress_callback_ = callback;
}

void OpenMMDynamicsInterface::set_coordinate_callback(std::function<void(const std::vector<float>&)> callback) {
    coordinate_callback_ = callback;
}

bool OpenMMDynamicsInterface::update_viamd_coordinates(ApplicationState& state) {
    // This would extract coordinates from OpenMM and update VIAMD's molecular system
    // For now, this is a placeholder
    return true;
}

} // namespace OpenMMDynamics

// Global Python initialization functions
#ifdef VIAMD_ENABLE_PYTHON
bool initialize_global_python_interpreter() {
    if (!g_python_initialized) {
        try {
            g_python_interpreter = new py::scoped_interpreter();
            g_python_initialized = true;
            return true;
        } catch (const std::exception& e) {
            return false;
        }
    }
    return true;
}

void cleanup_global_python_interpreter() {
    if (g_python_initialized && g_python_interpreter) {
        delete g_python_interpreter;
        g_python_interpreter = nullptr;
        g_python_initialized = false;
    }
}
#endif