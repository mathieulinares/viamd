#define IMGUI_DEFINE_MATH_OPERATORS

#include <event.h>
#include <viamd.h>
#include <task_system.h>
#include <color_utils.h>

#include <core/md_vec_math.h>
#include <core/md_log.h>
#include <core/md_arena_allocator.h>

#include <imgui_internal.h>
#include <imgui_widgets.h>
#include <implot_widgets.h>

#include <app/IconsFontAwesome6.h>

struct OpenMM : viamd::EventHandler {
    OpenMM() { viamd::event_system_register_handler(*this); }

    // OpenMM component state
    md_allocator_i* arena = nullptr;
    
    struct {
        bool show_window = false;
    } simulation;
    
    struct {
        bool show_window = false;
    } forces;
    
    struct {
        bool show_window = false;
    } integrator;

    void draw_simulation_window(ApplicationState& state) {
        if (!simulation.show_window) return;
        
        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("OpenMM Simulation", &simulation.show_window)) {
            ImGui::Text("OpenMM Simulation Control");
            ImGui::Separator();
            
            if (ImGui::Button("Start Simulation")) {
                MD_LOG_INFO("OpenMM: Starting simulation...");
            }
            ImGui::SameLine();
            if (ImGui::Button("Stop Simulation")) {
                MD_LOG_INFO("OpenMM: Stopping simulation...");
            }
            
            ImGui::Spacing();
            if (ImGui::Button("Reset System")) {
                MD_LOG_INFO("OpenMM: Resetting system...");
            }
            
            ImGui::Separator();
            ImGui::Text("Simulation Status: Ready");
            ImGui::Text("Current Step: 0");
            ImGui::Text("Total Energy: 0.0 kJ/mol");
        }
        ImGui::End();
    }
    
    void draw_forces_window(ApplicationState& state) {
        if (!forces.show_window) return;
        
        ImGui::SetNextWindowSize(ImVec2(350, 250), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("OpenMM Forces", &forces.show_window)) {
            ImGui::Text("Force Field Configuration");
            ImGui::Separator();
            
            ImGui::Text("Available Force Fields:");
            if (ImGui::Button("AMBER14")) {
                MD_LOG_INFO("OpenMM: Selected AMBER14 force field");
            }
            if (ImGui::Button("CHARMM36")) {
                MD_LOG_INFO("OpenMM: Selected CHARMM36 force field");
            }
            if (ImGui::Button("OPLS-AA")) {
                MD_LOG_INFO("OpenMM: Selected OPLS-AA force field");
            }
            
            ImGui::Separator();
            ImGui::Text("Current Force Field: None");
        }
        ImGui::End();
    }
    
    void draw_integrator_window(ApplicationState& state) {
        if (!integrator.show_window) return;
        
        ImGui::SetNextWindowSize(ImVec2(320, 200), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("OpenMM Integrator", &integrator.show_window)) {
            ImGui::Text("Integration Settings");
            ImGui::Separator();
            
            static int integrator_type = 0;
            ImGui::Combo("Integrator", &integrator_type, "Verlet\0Langevin\0Brownian\0");
            
            static float timestep = 0.002f;
            ImGui::SliderFloat("Time Step (ps)", &timestep, 0.001f, 0.005f, "%.3f");
            
            static float temperature = 300.0f;
            ImGui::SliderFloat("Temperature (K)", &temperature, 250.0f, 400.0f, "%.1f");
            
            static float friction = 1.0f;
            ImGui::SliderFloat("Friction (1/ps)", &friction, 0.1f, 10.0f, "%.1f");
        }
        ImGui::End();
    }

    void process_events(const viamd::Event* events, size_t num_events) final {
        for (size_t event_idx = 0; event_idx < num_events; ++event_idx) {
            const viamd::Event& e = events[event_idx];

            switch (e.type) {
            case viamd::EventType_ViamdInitialize: {
                ASSERT(e.payload_type == viamd::EventPayloadType_ApplicationState);
                ApplicationState& state = *(ApplicationState*)e.payload;
                arena = md_arena_allocator_create(state.allocator.persistent, MEGABYTES(1));
                MD_LOG_INFO("OpenMM component initialized");
                break;
            }
            case viamd::EventType_ViamdShutdown:
                if (arena) {
                    md_arena_allocator_destroy(arena);
                    arena = nullptr;
                }
                MD_LOG_INFO("OpenMM component shutdown");
                break;
            case viamd::EventType_ViamdFrameTick: {
                ASSERT(e.payload_type == viamd::EventPayloadType_ApplicationState);
                ApplicationState& state = *(ApplicationState*)e.payload;
                
                // Draw OpenMM windows
                draw_simulation_window(state);
                draw_forces_window(state);
                draw_integrator_window(state);
                break;
            }
            case viamd::EventType_ViamdWindowDrawMenu:
                // Add OpenMM submenu to the Windows menu
                if (ImGui::BeginMenu("OpenMM")) {
                    ImGui::Checkbox("Simulation", &simulation.show_window);
                    ImGui::Checkbox("Forces", &forces.show_window);
                    ImGui::Checkbox("Integrator", &integrator.show_window);
                    ImGui::EndMenu();
                }
                break;
            case viamd::EventType_ViamdTopologyInit: {
                ASSERT(e.payload_type == viamd::EventPayloadType_ApplicationState);
                ApplicationState& state = *(ApplicationState*)e.payload;
                MD_LOG_INFO("OpenMM: Topology initialized with %zu atoms", state.mold.mol.atom.count);
                break;
            }
            case viamd::EventType_ViamdTopologyFree:
                MD_LOG_INFO("OpenMM: Topology freed");
                break;
            case viamd::EventType_ViamdTrajectoryInit:
                MD_LOG_INFO("OpenMM: Trajectory initialized");
                break;
            case viamd::EventType_ViamdTrajectoryFree:
                MD_LOG_INFO("OpenMM: Trajectory freed");
                break;
            }
        }
    }
};

// Global instance - this automatically registers the component
static OpenMM g_openmm_component;