#include <event.h>
#include <viamd.h>

#include <core/md_common.h>
#include <core/md_allocator.h>
#include <core/md_arena_allocator.h>
#include <core/md_log.h>
#include <core/md_vec_math.h>
#include <core/md_array.h>
#include <md_molecule.h>

#include <imgui_widgets.h>

#ifdef VIAMD_ENABLE_OPENMM
#include <OpenMM.h>
#include <memory>
#include <string>
#endif

namespace openmm {

#ifdef VIAMD_ENABLE_OPENMM
enum class ForceFieldType {
    AMBER14,
    UFF
};

struct SimulationContext {
    std::unique_ptr<OpenMM::System> system;
    std::unique_ptr<OpenMM::Context> context;
    std::unique_ptr<OpenMM::Integrator> integrator;
    
    // Force field configuration
    ForceFieldType force_field_type = ForceFieldType::AMBER14;
    std::string force_field_name = "AMBER14";
};
#endif

struct OpenMMComponent : viamd::EventHandler {
private:
    md_allocator_i* allocator = nullptr;
    bool show_window = false;

#ifdef VIAMD_ENABLE_OPENMM
    SimulationContext sim_context;
#endif

public:
    OpenMMComponent() { 
        viamd::event_system_register_handler(*this); 
    }

    void process_events(const viamd::Event* events, size_t num_events) final {
        for (size_t i = 0; i < num_events; ++i) {
            const viamd::Event e = events[i];
            switch (e.type) {
            case viamd::EventType_ViamdInitialize: {
                ApplicationState& state = *(ApplicationState*)e.payload;
                initialize(state);
                break;
            }
            case viamd::EventType_ViamdShutdown:
                shutdown();
                break;
            case viamd::EventType_ViamdFrameTick: {
                ApplicationState& state = *(ApplicationState*)e.payload;
                update(state);
                draw_ui(state);
                break;
            }
            case viamd::EventType_ViamdTopologyInit: {
                ApplicationState& state = *(ApplicationState*)e.payload;
                on_topology_init(state);
                break;
            }
            case viamd::EventType_ViamdTopologyFree:
                on_topology_free();
                break;
            case viamd::EventType_ViamdWindowDrawMenu:
                draw_menu();
                break;
            default:
                break;
            }
        }
    }

    void initialize(ApplicationState& state) {
        allocator = state.allocator.persistent;
        MD_LOG_INFO("OpenMM component initialized");
    }

    void shutdown() {
        MD_LOG_INFO("OpenMM component shutdown");
    }

    void update(ApplicationState& state) {
#ifdef VIAMD_ENABLE_OPENMM
        // Update simulation if running
        (void)state; // Avoid unused parameter warning
#else
        (void)state; // Avoid unused parameter warning
#endif
    }

    void draw_ui(ApplicationState& state) {
#ifdef VIAMD_ENABLE_OPENMM
        if (show_window) {
            draw_simulation_window(state);
        }
#else
        (void)state; // Avoid unused parameter warning
#endif
    }

    void draw_menu() {
#ifdef VIAMD_ENABLE_OPENMM
        ImGui::Checkbox("OpenMM Simulation", &show_window);
#else
        if (ImGui::BeginMenu("OpenMM Simulation")) {
            ImGui::TextDisabled("OpenMM not available");
            ImGui::TextDisabled("Rebuild with VIAMD_ENABLE_OPENMM=ON");
            ImGui::EndMenu();
        }
#endif
    }

    void on_topology_init(ApplicationState& state) {
#ifdef VIAMD_ENABLE_OPENMM
        MD_LOG_INFO("Topology initialized, OpenMM ready for simulation setup");
        (void)state; // Will be used for actual simulation setup
#else
        (void)state; // Avoid unused parameter warning
#endif
    }

    void on_topology_free() {
#ifdef VIAMD_ENABLE_OPENMM
        MD_LOG_INFO("Topology freed, OpenMM simulation stopped");
        // Clean up any running simulation
#endif
    }

#ifdef VIAMD_ENABLE_OPENMM
private:
    void draw_simulation_window(ApplicationState& state) {
        if (!ImGui::Begin("OpenMM Simulation", &show_window)) {
            ImGui::End();
            return;
        }

        ImGui::Text("OpenMM Molecular Dynamics Simulation");
        ImGui::Separator();
        
        // Placeholder UI - will be expanded in later phases
        ImGui::Text("Force Field: %s", sim_context.force_field_name.c_str());
        
        if (state.mold.mol.atom.count > 0) {
            ImGui::Text("Loaded molecule: %zu atoms", state.mold.mol.atom.count);
            
            if (ImGui::Button("Initialize System")) {
                MD_LOG_INFO("System initialization requested");
                // Placeholder - will implement in Phase 2
            }
        } else {
            ImGui::TextDisabled("Load a molecular structure first");
        }

        ImGui::End();
    }
#endif
};

static OpenMMComponent instance = {};

}  // namespace openmm