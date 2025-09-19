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
#include <cmath>
#endif

namespace openmm {

#ifdef VIAMD_ENABLE_OPENMM
enum class ForceFieldType {
    AMBER14,
    UFF,
    GAFF2,
    OPLS_AA
};

static const char* force_field_names[] = {
    "AMBER14",
    "UFF",
    "GAFF-2", 
    "OPLS-AA"
};

// Atom type detection and assignment system
struct AtomTypeInfo {
    std::string openmm_type;
    std::string description;
    int atomic_number;
    int formal_charge;
    int hybridization; // 1=sp, 2=sp2, 3=sp3, 4=aromatic
};

// Basic atom type detection based on element and bonding
class AtomTypeDetector {
public:
    static std::string detect_atom_type(const ApplicationState& state, size_t atom_idx, ForceFieldType ff_type) {
        if (atom_idx >= state.mold.mol.atom.count) {
            return "UNK";
        }
        
        md_element_t element = state.mold.mol.atom.element ? state.mold.mol.atom.element[atom_idx] : 0;
        md_flags_t flags = state.mold.mol.atom.flags ? state.mold.mol.atom.flags[atom_idx] : 0;
        
        switch (ff_type) {
            case ForceFieldType::AMBER14:
                return detect_amber_type(state, atom_idx, element, flags);
            case ForceFieldType::UFF:
                return detect_uff_type(state, atom_idx, element, flags);
            case ForceFieldType::GAFF2:
                return detect_gaff2_type(state, atom_idx, element, flags);
            case ForceFieldType::OPLS_AA:
                return detect_opls_type(state, atom_idx, element, flags);
            default:
                return "UNK";
        }
    }
    
private:
    static std::string detect_amber_type(const ApplicationState& state, size_t atom_idx, md_element_t element, md_flags_t flags) {
        // Basic AMBER14 atom type detection
        switch (element) {
            case 1:  return "H";   // Hydrogen
            case 6:  // Carbon
                if (flags & MD_FLAG_AROMATIC) return "CA";
                if (flags & MD_FLAG_SP2) return "C2"; 
                if (flags & MD_FLAG_SP3) return "CT";
                return "C";
            case 7:  // Nitrogen
                if (flags & MD_FLAG_AROMATIC) return "NA";
                if (flags & MD_FLAG_SP2) return "N2";
                if (flags & MD_FLAG_SP3) return "N3";
                return "N";
            case 8:  // Oxygen
                if (flags & MD_FLAG_SP2) return "O2";
                return "OH";
            case 15: return "P";   // Phosphorus
            case 16: return "S";   // Sulfur
            default: return "DU";  // Dummy
        }
    }
    
    static std::string detect_uff_type(const ApplicationState& state, size_t atom_idx, md_element_t element, md_flags_t flags) {
        // UFF atom type detection - element + hybridization
        switch (element) {
            case 1:  return "H_";
            case 6:  // Carbon
                if (flags & MD_FLAG_AROMATIC) return "C_R";
                if (flags & MD_FLAG_SP) return "C_1";
                if (flags & MD_FLAG_SP2) return "C_2";
                if (flags & MD_FLAG_SP3) return "C_3";
                return "C_3";
            case 7:  // Nitrogen
                if (flags & MD_FLAG_AROMATIC) return "N_R";
                if (flags & MD_FLAG_SP) return "N_1";
                if (flags & MD_FLAG_SP2) return "N_2";
                if (flags & MD_FLAG_SP3) return "N_3";
                return "N_3";
            case 8:  // Oxygen
                if (flags & MD_FLAG_SP2) return "O_2";
                return "O_3";
            case 15: return "P_3"; // Phosphorus
            case 16: return "S_3"; // Sulfur
            default: return "UNK";
        }
    }
    
    static std::string detect_gaff2_type(const ApplicationState& state, size_t atom_idx, md_element_t element, md_flags_t flags) {
        // GAFF-2 atom type detection for organic molecules
        switch (element) {
            case 1:  return "h1";  // Hydrogen
            case 6:  // Carbon
                if (flags & MD_FLAG_AROMATIC) return "ca";
                if (flags & MD_FLAG_SP2) return "c2";
                if (flags & MD_FLAG_SP3) return "c3";
                return "c3";
            case 7:  // Nitrogen
                if (flags & MD_FLAG_AROMATIC) return "na";
                if (flags & MD_FLAG_SP2) return "n2";
                if (flags & MD_FLAG_SP3) return "n3";
                return "n3";
            case 8:  // Oxygen
                if (flags & MD_FLAG_SP2) return "o";
                return "oh";
            case 15: return "p3";  // Phosphorus
            case 16: return "s4";  // Sulfur
            default: return "du";  // Dummy
        }
    }
    
    static std::string detect_opls_type(const ApplicationState& state, size_t atom_idx, md_element_t element, md_flags_t flags) {
        // OPLS-AA atom type detection
        switch (element) {
            case 1:  return "opls_140"; // Hydrogen (alkane)
            case 6:  // Carbon
                if (flags & MD_FLAG_AROMATIC) return "opls_145"; // Aromatic carbon
                if (flags & MD_FLAG_SP2) return "opls_142";      // sp2 carbon
                if (flags & MD_FLAG_SP3) return "opls_135";      // sp3 carbon
                return "opls_135";
            case 7:  // Nitrogen
                if (flags & MD_FLAG_AROMATIC) return "opls_500"; // Aromatic nitrogen
                if (flags & MD_FLAG_SP2) return "opls_238";      // sp2 nitrogen
                if (flags & MD_FLAG_SP3) return "opls_237";      // sp3 nitrogen
                return "opls_237";
            case 8:  // Oxygen
                if (flags & MD_FLAG_SP2) return "opls_236";      // sp2 oxygen
                return "opls_154";                               // sp3 oxygen
            case 15: return "opls_393"; // Phosphorus
            case 16: return "opls_200"; // Sulfur
            default: return "opls_999"; // Unknown
        }
    }
};

struct SimulationContext {
    std::unique_ptr<OpenMM::System> system;
    std::unique_ptr<OpenMM::Context> context;
    std::unique_ptr<OpenMM::Integrator> integrator;
    
    // Force field configuration
    ForceFieldType force_field_type = ForceFieldType::AMBER14;
    std::string force_field_name = "AMBER14";
    
    // Atom type mapping
    std::vector<std::string> atom_types;  // Assigned atom types for each atom
    std::vector<AtomTypeInfo> type_info;  // Detailed type information
    
    // System information
    bool system_initialized = false;
    size_t num_atoms = 0;
    double last_energy = 0.0;
    
    // Simulation state
    bool simulation_running = false;
    bool simulation_paused = false;
    int simulation_frame = 0;
    double simulation_time = 0.0; // ps
    double timestep = 0.002; // ps, conservative default
    
    // Configurable simulation parameters
    double temperature = 300.0; // K
    double friction = 1.0; // ps^-1
    int steps_per_update = 10;
    
    // Minimization parameters
    double minimization_tolerance = 1e-6; // kJ/mol
    int minimization_max_iterations = 5000;
    
    // Trajectory storage
    std::vector<std::vector<OpenMM::Vec3>> trajectory_frames;
    std::vector<double> trajectory_energies;
    std::vector<double> trajectory_times;
    int max_trajectory_frames = 1000;
    
    void clear() {
        system.reset();
        context.reset();
        integrator.reset();
        atom_types.clear();
        type_info.clear();
        system_initialized = false;
        num_atoms = 0;
        last_energy = 0.0;
        simulation_running = false;
        simulation_paused = false;
        simulation_frame = 0;
        simulation_time = 0.0;
        trajectory_frames.clear();
        trajectory_energies.clear();
        trajectory_times.clear();
    }
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
        if (sim_context.simulation_running && !sim_context.simulation_paused && sim_context.system_initialized) {
            run_simulation_step(state);
        }
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
        sim_context.clear();
#endif
    }

#ifdef VIAMD_ENABLE_OPENMM
public:
    void setup_system(ApplicationState& state) {
        if (state.mold.mol.atom.count == 0) {
            MD_LOG_ERROR("No molecule loaded for OpenMM system setup");
            return;
        }
        
        try {
            MD_LOG_INFO("Setting up OpenMM system with %zu atoms using %s force field", 
                       state.mold.mol.atom.count, sim_context.force_field_name.c_str());
            
            // Clear previous system
            sim_context.clear();
            sim_context.num_atoms = state.mold.mol.atom.count;
            
            // Detect and assign atom types
            assign_atom_types(state);
            
            // Create OpenMM system
            create_openmm_system(state);
            
            // Set up integrator (Langevin dynamics)
            setup_integrator();
            
            // Create context
            sim_context.context = std::make_unique<OpenMM::Context>(*sim_context.system, *sim_context.integrator);
            
            // Set initial positions
            set_positions(state);
            
            sim_context.system_initialized = true;
            MD_LOG_INFO("OpenMM system initialized successfully");
            
        } catch (const std::exception& e) {
            MD_LOG_ERROR("Failed to setup OpenMM system: %s", e.what());
            sim_context.clear();
        }
    }
    
private:
    void assign_atom_types(ApplicationState& state) {
        sim_context.atom_types.clear();
        sim_context.atom_types.reserve(state.mold.mol.atom.count);
        
        MD_LOG_INFO("Assigning atom types for %s force field", sim_context.force_field_name.c_str());
        
        for (size_t i = 0; i < state.mold.mol.atom.count; ++i) {
            std::string atom_type = AtomTypeDetector::detect_atom_type(state, i, sim_context.force_field_type);
            sim_context.atom_types.push_back(atom_type);
            
            // Log first 10 assignments for debugging
            if (i < 10) {
                md_element_t element = state.mold.mol.atom.element ? state.mold.mol.atom.element[i] : 0;
                MD_LOG_INFO("Atom %zu: element=%d, type=%s", i, element, atom_type.c_str());
            }
        }
        
        MD_LOG_INFO("Assigned %zu atom types", sim_context.atom_types.size());
    }
    
    void create_openmm_system(ApplicationState& state) {
        sim_context.system = std::make_unique<OpenMM::System>();
        
        // Add particles
        for (size_t i = 0; i < state.mold.mol.atom.count; ++i) {
            double mass = 1.0; // Default mass
            if (state.mold.mol.atom.mass) {
                mass = state.mold.mol.atom.mass[i];
            } else if (state.mold.mol.atom.element) {
                // Use standard atomic masses based on element
                mass = get_standard_mass(state.mold.mol.atom.element[i]);
            }
            sim_context.system->addParticle(mass);
        }
        
        // Add force field specific forces
        setup_force_field_forces(state);
        
        MD_LOG_INFO("Created OpenMM system with %d particles", sim_context.system->getNumParticles());
    }
    
    double get_standard_mass(md_element_t element) {
        // Standard atomic masses in amu
        switch (element) {
            case 1:  return 1.008;   // Hydrogen
            case 6:  return 12.011;  // Carbon
            case 7:  return 14.007;  // Nitrogen
            case 8:  return 15.999;  // Oxygen
            case 15: return 30.974;  // Phosphorus
            case 16: return 32.065;  // Sulfur
            default: return 1.0;
        }
    }
    
    void setup_force_field_forces(ApplicationState& state) {
        switch (sim_context.force_field_type) {
            case ForceFieldType::AMBER14:
                setup_amber_forces(state);
                break;
            case ForceFieldType::UFF:
                setup_uff_forces(state);
                break;
            case ForceFieldType::GAFF2:
                setup_gaff2_forces(state);
                break;
            case ForceFieldType::OPLS_AA:
                setup_opls_forces(state);
                break;
        }
    }
    
    void setup_amber_forces(ApplicationState& state) {
        // Basic AMBER-style forces
        setup_nonbonded_forces_amber(state);
        setup_bonded_forces(state);
    }
    
    void setup_uff_forces(ApplicationState& state) {
        // UFF uses simple harmonic and Lennard-Jones potentials
        setup_nonbonded_forces_uff(state);
        setup_bonded_forces(state);
    }
    
    void setup_gaff2_forces(ApplicationState& state) {
        // GAFF-2 for organic molecules
        setup_nonbonded_forces_gaff2(state);
        setup_bonded_forces(state);
    }
    
    void setup_opls_forces(ApplicationState& state) {
        // OPLS-AA all-atom force field
        setup_nonbonded_forces_opls(state);
        setup_bonded_forces(state);
    }
    
    void setup_nonbonded_forces_amber(ApplicationState& state) {
        auto* nb_force = new OpenMM::NonbondedForce();
        
        for (size_t i = 0; i < state.mold.mol.atom.count; ++i) {
            // AMBER-style parameters (simplified)
            double charge = 0.0;  // Would need proper charge calculation
            double sigma = 0.35;  // nm, typical for carbon
            double epsilon = 0.4; // kJ/mol, typical for carbon
            
            // Adjust based on element
            if (state.mold.mol.atom.element) {
                md_element_t element = state.mold.mol.atom.element[i];
                switch (element) {
                    case 1:  sigma = 0.25; epsilon = 0.1; break;  // Hydrogen
                    case 7:  sigma = 0.32; epsilon = 0.7; break;  // Nitrogen  
                    case 8:  sigma = 0.30; epsilon = 0.9; break;  // Oxygen
                }
            }
            
            nb_force->addParticle(charge, sigma, epsilon);
        }
        
        sim_context.system->addForce(nb_force);
        MD_LOG_INFO("Added AMBER nonbonded forces");
    }
    
    void setup_nonbonded_forces_uff(ApplicationState& state) {
        auto* nb_force = new OpenMM::NonbondedForce();
        
        for (size_t i = 0; i < state.mold.mol.atom.count; ++i) {
            // UFF parameters
            double charge = 0.0;
            double sigma = 0.35;
            double epsilon = 0.4;
            
            // UFF-specific parameters based on atom type
            const std::string& atom_type = sim_context.atom_types[i];
            if (atom_type.substr(0, 2) == "C_") {
                sigma = 0.343; epsilon = 0.5;
            } else if (atom_type.substr(0, 2) == "N_") {
                sigma = 0.325; epsilon = 0.7;
            } else if (atom_type.substr(0, 2) == "O_") {
                sigma = 0.303; epsilon = 0.9;
            }
            
            nb_force->addParticle(charge, sigma, epsilon);
        }
        
        sim_context.system->addForce(nb_force);
        MD_LOG_INFO("Added UFF nonbonded forces");
    }
    
    void setup_nonbonded_forces_gaff2(ApplicationState& state) {
        auto* nb_force = new OpenMM::NonbondedForce();
        
        for (size_t i = 0; i < state.mold.mol.atom.count; ++i) {
            // GAFF-2 parameters for organic molecules
            double charge = 0.0;
            double sigma = 0.35;
            double epsilon = 0.4;
            
            // GAFF-2 specific parameters
            const std::string& atom_type = sim_context.atom_types[i];
            if (atom_type == "ca") {        // aromatic carbon
                sigma = 0.340; epsilon = 0.36;
            } else if (atom_type == "c3") { // sp3 carbon
                sigma = 0.340; epsilon = 0.46;
            } else if (atom_type == "n3") { // sp3 nitrogen
                sigma = 0.325; epsilon = 0.71;
            } else if (atom_type == "oh") { // hydroxyl oxygen
                sigma = 0.306; epsilon = 0.88;
            }
            
            nb_force->addParticle(charge, sigma, epsilon);
        }
        
        sim_context.system->addForce(nb_force);
        MD_LOG_INFO("Added GAFF-2 nonbonded forces");
    }
    
    void setup_nonbonded_forces_opls(ApplicationState& state) {
        auto* nb_force = new OpenMM::NonbondedForce();
        
        for (size_t i = 0; i < state.mold.mol.atom.count; ++i) {
            // OPLS-AA parameters
            double charge = 0.0;
            double sigma = 0.35;
            double epsilon = 0.4;
            
            // OPLS-AA specific parameters based on atom type
            const std::string& atom_type = sim_context.atom_types[i];
            if (atom_type == "opls_145") {      // aromatic carbon
                sigma = 0.355; epsilon = 0.293;
            } else if (atom_type == "opls_135") { // sp3 carbon
                sigma = 0.350; epsilon = 0.276;
            } else if (atom_type == "opls_237") { // sp3 nitrogen
                sigma = 0.325; epsilon = 0.711;
            } else if (atom_type == "opls_154") { // sp3 oxygen
                sigma = 0.312; epsilon = 0.711;
            }
            
            nb_force->addParticle(charge, sigma, epsilon);
        }
        
        sim_context.system->addForce(nb_force);
        MD_LOG_INFO("Added OPLS-AA nonbonded forces");
    }
    
    void setup_bonded_forces(ApplicationState& state) {
        // Add harmonic bond forces if bond information is available
        if (state.mold.mol.bond.count > 0) {
            auto* bond_force = new OpenMM::HarmonicBondForce();
            
            for (size_t i = 0; i < state.mold.mol.bond.count; ++i) {
                uint32_t atom1 = state.mold.mol.bond.pairs[i].idx[0];
                uint32_t atom2 = state.mold.mol.bond.pairs[i].idx[1];
                
                // Default bond parameters
                double length = 0.15; // nm, typical C-C bond
                double k = 250000.0;   // kJ/(mol*nm^2), typical spring constant
                
                bond_force->addBond(atom1, atom2, length, k);
            }
            
            sim_context.system->addForce(bond_force);
            MD_LOG_INFO("Added %zu harmonic bonds", state.mold.mol.bond.count);
        }
    }
    
    void setup_integrator() {
        // Use configurable parameters from context
        sim_context.integrator = std::make_unique<OpenMM::LangevinIntegrator>(
            sim_context.temperature, sim_context.friction, sim_context.timestep);
        MD_LOG_INFO("Set up Langevin integrator: T=%.1f K, friction=%.1f ps^-1, dt=%.3f ps", 
                   sim_context.temperature, sim_context.friction, sim_context.timestep);
    }
    
    void set_positions(ApplicationState& state) {
        std::vector<OpenMM::Vec3> positions;
        positions.reserve(state.mold.mol.atom.count);
        
        for (size_t i = 0; i < state.mold.mol.atom.count; ++i) {
            // Convert from Angstrom to nm
            double x = state.mold.mol.atom.x[i] * 0.1;
            double y = state.mold.mol.atom.y[i] * 0.1;
            double z = state.mold.mol.atom.z[i] * 0.1;
            positions.emplace_back(x, y, z);
        }
        
        sim_context.context->setPositions(positions);
        MD_LOG_INFO("Set positions for %zu atoms", positions.size());
    }
    
    void minimize_energy(ApplicationState& state) {
        if (!sim_context.system_initialized || !sim_context.context) {
            MD_LOG_ERROR("Cannot minimize energy: system not initialized");
            return;
        }
        
        try {
            MD_LOG_INFO("Starting energy minimization...");
            
            // Use configurable parameters
            double tolerance = sim_context.minimization_tolerance;
            int max_iterations = sim_context.minimization_max_iterations;
            
            sim_context.integrator->step(0); // Initialize context
            
            // Get initial energy
            OpenMM::State initial_state = sim_context.context->getState(OpenMM::State::Energy);
            double initial_energy = initial_state.getPotentialEnergy();
            
            MD_LOG_INFO("Initial energy: %.3f kJ/mol", initial_energy);
            
            // Perform minimization
            OpenMM::LocalEnergyMinimizer::minimize(*sim_context.context, tolerance, max_iterations);
            
            // Get final energy
            OpenMM::State final_state = sim_context.context->getState(OpenMM::State::Energy | OpenMM::State::Positions);
            double final_energy = final_state.getPotentialEnergy();
            sim_context.last_energy = final_energy;
            
            MD_LOG_INFO("Energy minimization completed: %.3f → %.3f kJ/mol (Δ = %.3f kJ/mol)", 
                       initial_energy, final_energy, final_energy - initial_energy);
            
            // Update positions in VIAMD
            update_viamd_positions(state, final_state.getPositions());
            
        } catch (const std::exception& e) {
            MD_LOG_ERROR("Energy minimization failed: %s", e.what());
        }
    }
    
    void update_viamd_positions(ApplicationState& state, const std::vector<OpenMM::Vec3>& positions) {
        if (positions.size() != state.mold.mol.atom.count) {
            MD_LOG_ERROR("Position count mismatch: OpenMM=%zu, VIAMD=%zu", positions.size(), state.mold.mol.atom.count);
            return;
        }
        
        // Update VIAMD molecule coordinates (convert nm to Angstrom)
        for (size_t i = 0; i < state.mold.mol.atom.count; ++i) {
            state.mold.mol.atom.x[i] = positions[i][0] * 10.0f; // nm to Å
            state.mold.mol.atom.y[i] = positions[i][1] * 10.0f;
            state.mold.mol.atom.z[i] = positions[i][2] * 10.0f;
        }
        
        // Mark buffers dirty for re-rendering
        state.mold.dirty_buffers |= MolBit_DirtyPosition;
        
        MD_LOG_INFO("Updated VIAMD positions from OpenMM");
    }
    
    void run_simulation_step(ApplicationState& state) {
        if (!sim_context.system_initialized || !sim_context.context) {
            return;
        }
        
        try {
            // Use configurable steps per update
            sim_context.integrator->step(sim_context.steps_per_update);
            
            // Get updated positions and energy for stability check
            OpenMM::State current_state = sim_context.context->getState(OpenMM::State::Positions | OpenMM::State::Energy);
            
            double current_energy = current_state.getPotentialEnergy();
            const std::vector<OpenMM::Vec3>& positions = current_state.getPositions();
            
            // Check for simulation explosion
            if (check_simulation_stability(positions, current_energy)) {
                sim_context.last_energy = current_energy;
                
                // Update VIAMD positions for real-time visualization
                update_viamd_positions(state, positions);
                
                // Update simulation state
                sim_context.simulation_frame++;
                sim_context.simulation_time += sim_context.timestep * sim_context.steps_per_update;
                
                // Capture trajectory frame if enabled
                capture_trajectory_frame(positions, current_energy, sim_context.simulation_time);
                
            } else {
                MD_LOG_ERROR("Simulation instability detected, stopping simulation");
                sim_context.simulation_running = false;
            }
            
        } catch (const std::exception& e) {
            MD_LOG_ERROR("Simulation step failed: %s", e.what());
            sim_context.simulation_running = false;
        }
    }
    
    void capture_trajectory_frame(const std::vector<OpenMM::Vec3>& positions, double energy, double time) {
        // Only capture if we haven't exceeded the maximum frames
        if (sim_context.trajectory_frames.size() >= sim_context.max_trajectory_frames) {
            // Remove oldest frame (circular buffer behavior)
            sim_context.trajectory_frames.erase(sim_context.trajectory_frames.begin());
            sim_context.trajectory_energies.erase(sim_context.trajectory_energies.begin());
            sim_context.trajectory_times.erase(sim_context.trajectory_times.begin());
        }
        
        // Store the frame
        sim_context.trajectory_frames.push_back(positions);
        sim_context.trajectory_energies.push_back(energy);
        sim_context.trajectory_times.push_back(time);
    }
    
    bool check_simulation_stability(const std::vector<OpenMM::Vec3>& positions, double energy) {
        // Check for NaN or infinite energy
        if (!std::isfinite(energy)) {
            MD_LOG_ERROR("Non-finite energy detected: %f", energy);
            return false;
        }
        
        // Check for excessive energy (likely explosion)
        const double energy_threshold = 1e6; // kJ/mol
        if (energy > energy_threshold) {
            MD_LOG_ERROR("Energy explosion detected: %f kJ/mol > %f kJ/mol", energy, energy_threshold);
            return false;
        }
        
        // Check for excessive coordinates (simulation box explosion)
        const double coord_threshold = 100.0; // nm (very large but reasonable)
        for (const auto& pos : positions) {
            for (int i = 0; i < 3; ++i) {
                if (!std::isfinite(pos[i]) || std::abs(pos[i]) > coord_threshold) {
                    MD_LOG_ERROR("Coordinate explosion detected: coordinate = %f nm", pos[i]);
                    return false;
                }
            }
        }
        
        return true;
    }
    
    void export_trajectory_to_viamd(ApplicationState& state) {
        if (sim_context.trajectory_frames.empty()) {
            MD_LOG_ERROR("No trajectory frames to export");
            return;
        }
        
        try {
            // Create a simple trajectory structure that VIAMD can understand
            // This creates multiple coordinate sets that can be played back
            
            size_t num_frames = sim_context.trajectory_frames.size();
            size_t num_atoms = state.mold.mol.atom.count;
            
            MD_LOG_INFO("Exporting %zu trajectory frames with %zu atoms each", num_frames, num_atoms);
            
            // Store current frame index for restoration
            int original_frame = 0; // Will be enhanced in later phases
            
            // Create a simple trajectory export by updating coordinates frame by frame
            // This is a basic implementation - more sophisticated trajectory handling 
            // would be added in Phase 5 with ApplicationState integration
            
            for (size_t frame = 0; frame < num_frames; ++frame) {
                const auto& positions = sim_context.trajectory_frames[frame];
                update_viamd_positions(state, positions);
                
                // Log progress for every 100th frame
                if (frame % 100 == 0 || frame == num_frames - 1) {
                    MD_LOG_INFO("Exported frame %zu/%zu (t=%.3f ps, E=%.3f kJ/mol)", 
                               frame + 1, num_frames, 
                               sim_context.trajectory_times[frame],
                               sim_context.trajectory_energies[frame]);
                }
            }
            
            MD_LOG_INFO("Trajectory export completed. Final frame displayed in VIAMD.");
            
        } catch (const std::exception& e) {
            MD_LOG_ERROR("Trajectory export failed: %s", e.what());
        }
    }
    
    void load_trajectory_frame(ApplicationState& state, size_t frame_index) {
        if (frame_index >= sim_context.trajectory_frames.size()) {
            MD_LOG_ERROR("Invalid frame index: %zu (max: %zu)", frame_index, sim_context.trajectory_frames.size());
            return;
        }
        
        const auto& positions = sim_context.trajectory_frames[frame_index];
        update_viamd_positions(state, positions);
        
        MD_LOG_INFO("Loaded trajectory frame %zu (t=%.3f ps, E=%.3f kJ/mol)", 
                   frame_index, 
                   sim_context.trajectory_times[frame_index],
                   sim_context.trajectory_energies[frame_index]);
    }
#endif

#ifdef VIAMD_ENABLE_OPENMM
private:
    void draw_simulation_window(ApplicationState& state) {
        if (!ImGui::Begin("OpenMM Simulation", &show_window, ImGuiWindowFlags_MenuBar)) {
            ImGui::End();
            return;
        }

        // Menu bar for quick actions
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Actions")) {
                if (ImGui::MenuItem("Initialize System", nullptr, false, state.mold.mol.atom.count > 0 && !sim_context.system_initialized)) {
                    setup_system(state);
                }
                if (ImGui::MenuItem("Minimize Energy", nullptr, false, sim_context.system_initialized)) {
                    minimize_energy(state);
                }
                if (ImGui::MenuItem("Reset Simulation", nullptr, false, sim_context.system_initialized)) {
                    sim_context.clear();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        ImGui::Text("OpenMM Molecular Dynamics Simulation");
        ImGui::Separator();
        
        // Check if molecule is loaded
        if (state.mold.mol.atom.count == 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Load a molecular structure first");
            ImGui::TextDisabled("Use File → Open to load a PDB, XYZ, or other molecular file");
            ImGui::End();
            return;
        }

        ImGui::Text("Loaded molecule: %zu atoms", state.mold.mol.atom.count);
        ImGui::Separator();

        // =========================
        // PANEL 1: Force Field Configuration
        // =========================
        if (ImGui::CollapsingHeader("Force Field Configuration", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            
            // Force field selection
            const char* force_field_items[] = { "AMBER14", "UFF", "GAFF-2", "OPLS-AA" };
            int current_ff = static_cast<int>(sim_context.force_field_type);
            
            if (ImGui::Combo("Force Field", &current_ff, force_field_items, IM_ARRAYSIZE(force_field_items))) {
                sim_context.force_field_type = static_cast<ForceFieldType>(current_ff);
                sim_context.force_field_name = force_field_names[current_ff];
                
                // Clear system if it was already initialized with different force field
                if (sim_context.system_initialized) {
                    MD_LOG_INFO("Force field changed, system will need reinitialization");
                    sim_context.clear();
                }
            }
            
            ImGui::Text("Selected: %s", sim_context.force_field_name.c_str());
            
            // Show atom type detection info
            if (!sim_context.atom_types.empty()) {
                ImGui::Text("Atom types assigned: %zu/%zu", sim_context.atom_types.size(), state.mold.mol.atom.count);
                
                if (ImGui::TreeNode("Atom Type Details")) {
                    ImGui::BeginChild("AtomTypeScroll", ImVec2(0, 150), true);
                    for (size_t i = 0; i < std::min(size_t(50), sim_context.atom_types.size()); ++i) {
                        md_element_t element = state.mold.mol.atom.element ? state.mold.mol.atom.element[i] : 0;
                        ImGui::Text("Atom %zu: element=%d → %s", i, element, sim_context.atom_types[i].c_str());
                    }
                    if (sim_context.atom_types.size() > 50) {
                        ImGui::Text("... and %zu more", sim_context.atom_types.size() - 50);
                    }
                    ImGui::EndChild();
                    ImGui::TreePop();
                }
            }
            
            // System initialization
            if (!sim_context.system_initialized) {
                if (ImGui::Button("Initialize OpenMM System", ImVec2(-1, 0))) {
                    setup_system(state);
                }
                ImGui::TextWrapped("Initialize the system to set up force field parameters and prepare for simulation.");
            } else {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ System initialized with %zu atoms", sim_context.num_atoms);
                ImGui::SameLine();
                if (ImGui::SmallButton("Reinitialize")) {
                    setup_system(state);
                }
            }
            
            ImGui::Unindent();
        }

        ImGui::Separator();

        // =========================
        // PANEL 2: Simulation Parameters
        // =========================
        if (ImGui::CollapsingHeader("Simulation Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            
            // Temperature control
            ImGui::SliderFloat("Temperature (K)", &sim_context.temperature, 200.0f, 400.0f, "%.1f");
            ImGui::SameLine(); 
            if (ImGui::SmallButton("300K")) sim_context.temperature = 300.0f;
            
            // Timestep control
            ImGui::SliderFloat("Timestep (ps)", &sim_context.timestep, 0.0005f, 0.005f, "%.4f");
            ImGui::SameLine();
            if (ImGui::SmallButton("2fs")) sim_context.timestep = 0.002f;
            
            // Friction coefficient
            ImGui::SliderFloat("Friction (ps⁻¹)", &sim_context.friction, 0.1f, 10.0f, "%.2f");
            ImGui::SameLine();
            if (ImGui::SmallButton("1.0")) sim_context.friction = 1.0f;
            
            // Steps per update
            ImGui::SliderInt("Steps per update", &sim_context.steps_per_update, 1, 100);
            ImGui::SameLine();
            if (ImGui::SmallButton("10")) sim_context.steps_per_update = 10;
            
            // Update integrator if system is initialized and parameters changed
            if (sim_context.system_initialized) {
                ImGui::TextDisabled("Note: Parameter changes will take effect after reinitialization");
                if (ImGui::Button("Apply Parameter Changes", ImVec2(-1, 0))) {
                    setup_integrator();
                    MD_LOG_INFO("Applied new simulation parameters");
                }
            }
            
            ImGui::Unindent();
        }

        ImGui::Separator();

        // =========================
        // PANEL 3: Energy Minimization
        // =========================
        if (ImGui::CollapsingHeader("Energy Minimization", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            
            // Minimization parameters
            ImGui::Text("Minimization Parameters:");
            
            // Tolerance control
            float tolerance_log = std::log10(sim_context.minimization_tolerance);
            if (ImGui::SliderFloat("Tolerance (log₁₀)", &tolerance_log, -8.0f, -3.0f, "%.1f")) {
                sim_context.minimization_tolerance = std::pow(10.0, tolerance_log);
            }
            ImGui::Text("Current tolerance: %.1e kJ/mol", sim_context.minimization_tolerance);
            
            // Max iterations
            ImGui::SliderInt("Max iterations", &sim_context.minimization_max_iterations, 100, 10000);
            
            // Minimization button
            ImGui::Separator();
            if (sim_context.system_initialized) {
                if (ImGui::Button("Minimize Energy", ImVec2(-1, 0))) {
                    minimize_energy(state);
                }
                ImGui::TextWrapped("Perform L-BFGS energy minimization to optimize molecular structure.");
            } else {
                ImGui::BeginDisabled();
                ImGui::Button("Minimize Energy (Initialize system first)", ImVec2(-1, 0));
                ImGui::EndDisabled();
            }
            
            // Show last energy if available
            if (sim_context.last_energy != 0.0) {
                ImGui::Text("Last computed energy: %.3f kJ/mol", sim_context.last_energy);
            }
            
            ImGui::Unindent();
        }

        ImGui::Separator();

        // =========================
        // PANEL 4: Molecular Dynamics Simulation
        // =========================
        if (ImGui::CollapsingHeader("Molecular Dynamics Simulation", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            
            if (sim_context.system_initialized) {
                // Simulation controls
                if (!sim_context.simulation_running) {
                    if (ImGui::Button("Start Simulation", ImVec2(-1, 0))) {
                        sim_context.simulation_running = true;
                        sim_context.simulation_paused = false;
                        MD_LOG_INFO("Starting molecular dynamics simulation");
                    }
                } else {
                    // Running controls
                    ImGui::Columns(2, "SimControls", false);
                    
                    if (!sim_context.simulation_paused) {
                        if (ImGui::Button("Pause", ImVec2(-1, 0))) {
                            sim_context.simulation_paused = true;
                            MD_LOG_INFO("Simulation paused");
                        }
                    } else {
                        if (ImGui::Button("Resume", ImVec2(-1, 0))) {
                            sim_context.simulation_paused = false;
                            MD_LOG_INFO("Simulation resumed");
                        }
                    }
                    
                    ImGui::NextColumn();
                    if (ImGui::Button("Stop", ImVec2(-1, 0))) {
                        sim_context.simulation_running = false;
                        sim_context.simulation_paused = false;
                        MD_LOG_INFO("Simulation stopped");
                    }
                    
                    ImGui::Columns(1);
                }
                
                // Simulation status
                if (sim_context.simulation_running || sim_context.simulation_frame > 0) {
                    ImGui::Separator();
                    ImGui::Text("Simulation Status:");
                    
                    ImGui::Columns(2, "Status", false);
                    ImGui::Text("Frame: %d", sim_context.simulation_frame);
                    ImGui::Text("Time: %.3f ps", sim_context.simulation_time);
                    
                    ImGui::NextColumn();
                    ImGui::Text("Timestep: %.3f ps", sim_context.timestep);
                    if (sim_context.last_energy != 0.0) {
                        ImGui::Text("Energy: %.3f kJ/mol", sim_context.last_energy);
                    }
                    ImGui::Columns(1);
                    
                    // Status indicator
                    if (sim_context.simulation_running) {
                        if (sim_context.simulation_paused) {
                            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "● PAUSED");
                        } else {
                            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "● RUNNING");
                        }
                    } else {
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "● STOPPED");
                    }
                }
                
            } else {
                ImGui::BeginDisabled();
                ImGui::Button("Start Simulation (Initialize system first)", ImVec2(-1, 0));
                ImGui::EndDisabled();
                ImGui::TextWrapped("Initialize the OpenMM system first before running simulations.");
            }
            
            ImGui::Unindent();
        }

        ImGui::Separator();

        // =========================
        // PANEL 5: Trajectory Export and Analysis
        // =========================
        if (ImGui::CollapsingHeader("Trajectory Export and Analysis")) {
            ImGui::Indent();
            
            ImGui::Text("Trajectory Storage:");
            ImGui::Text("Captured frames: %zu / %d", sim_context.trajectory_frames.size(), sim_context.max_trajectory_frames);
            
            // Trajectory storage settings
            ImGui::SliderInt("Max frames to store", &sim_context.max_trajectory_frames, 100, 5000);
            
            if (!sim_context.trajectory_frames.empty()) {
                ImGui::Separator();
                ImGui::Text("Export Options:");
                
                // Export to VIAMD
                if (ImGui::Button("Export Trajectory to VIAMD", ImVec2(-1, 0))) {
                    export_trajectory_to_viamd(state);
                }
                ImGui::TextWrapped("Export captured trajectory frames for visualization in VIAMD.");
                
                // Trajectory playback controls
                ImGui::Separator();
                ImGui::Text("Trajectory Playback:");
                
                static int current_frame = 0;
                if (ImGui::SliderInt("Frame", &current_frame, 0, (int)sim_context.trajectory_frames.size() - 1)) {
                    load_trajectory_frame(state, current_frame);
                }
                
                ImGui::Columns(3, "PlaybackControls", false);
                if (ImGui::Button("First", ImVec2(-1, 0))) {
                    current_frame = 0;
                    load_trajectory_frame(state, current_frame);
                }
                ImGui::NextColumn();
                if (ImGui::Button("Previous", ImVec2(-1, 0)) && current_frame > 0) {
                    current_frame--;
                    load_trajectory_frame(state, current_frame);
                }
                ImGui::NextColumn();
                if (ImGui::Button("Next", ImVec2(-1, 0)) && current_frame < (int)sim_context.trajectory_frames.size() - 1) {
                    current_frame++;
                    load_trajectory_frame(state, current_frame);
                }
                ImGui::Columns(1);
                
                if (current_frame < sim_context.trajectory_times.size()) {
                    ImGui::Text("Frame %d: t=%.3f ps, E=%.3f kJ/mol", 
                               current_frame, 
                               sim_context.trajectory_times[current_frame],
                               sim_context.trajectory_energies[current_frame]);
                }
                
            } else {
                ImGui::TextDisabled("Run a simulation to capture trajectory frames.");
            }
            
            ImGui::Unindent();
        }

        ImGui::End();
    }
#endif
};

static OpenMMComponent instance = {};

}  // namespace openmm