#include <event.h>

#ifdef VIAMD_ENABLE_OPENMM

#include <viamd.h>

#include <core/md_common.h>
#include <core/md_allocator.h>
#include <core/md_arena_allocator.h>
#include <core/md_log.h>
#include <core/md_vec_math.h>
#include <core/md_array.h>
#include <core/md_bitfield.h>
#include <md_molecule.h>

#include <imgui_widgets.h>
#include <imgui.h>

#include <OpenMM.h>

#include <memory>
#include <string>
#include <vector>
#include <cstring>
#include <map>
#include <tuple>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace openmm {

// Force field enumeration
enum class ForceFieldType {
    AMBER,
    UFF
};

// AMBER force field parameter structures
struct AmberAtomType {
    std::string name;
    double mass;
    double sigma;    // LJ sigma parameter (nm)
    double epsilon;  // LJ epsilon parameter (kJ/mol)
    double charge;   // Partial charge (default, can be overridden)
};

struct AmberBondType {
    double k;        // Force constant (kJ/mol/nm^2)
    double r0;       // Equilibrium length (nm)
};

struct AmberAngleType {
    double k;        // Force constant (kJ/mol/rad^2)
    double theta0;   // Equilibrium angle (radians)
};

// UFF (Universal Force Field) parameter structures
struct UFFAtomType {
    std::string name;
    double mass;
    double radius;   // VdW radius (Angstroms)
    double depth;    // Well depth (kcal/mol)
    double charge;   // Partial charge (default, can be overridden)
    int hybridization;  // SP, SP2, SP3, etc.
};

struct UFFBondType {
    double k;        // Force constant (kcal/mol/Å^2)
    double r0;       // Equilibrium length (Angstroms)
};

struct UFFAngleType {
    double k;        // Force constant (kcal/mol/rad^2)
    double theta0;   // Equilibrium angle (radians)
};

struct SimulationContext {
    std::unique_ptr<OpenMM::System> system;
    std::unique_ptr<OpenMM::Context> context;
    std::unique_ptr<OpenMM::Integrator> integrator;
    
    // Force field selection
    ForceFieldType force_field_type = ForceFieldType::AMBER;
    std::string force_field_name = "AMBER14";
};

class OpenMMComponent : public viamd::EventHandler {
private:
    SimulationContext sim_context;
    md_allocator_i* allocator = nullptr;

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
            case viamd::EventType_ViamdWindowDrawMenu: {
                draw_menu();
                break;
            }
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
        // Note: cleanup_simulation needs an ApplicationState, so we can't clean up here
        // Cleanup will happen when topology is freed
        MD_LOG_INFO("OpenMM component shutdown");
    }

    void update(ApplicationState& state) {
#ifdef VIAMD_ENABLE_OPENMM
        if (state.simulation.running && !state.simulation.paused && state.simulation.initialized) {
            // Run simulation steps
            run_simulation_step(state);
        }
#endif
    }

    void draw_ui(ApplicationState& state) {
#ifdef VIAMD_ENABLE_OPENMM
        if (state.simulation.show_window) {
            draw_simulation_window(state);
        }
#endif
    }

    void draw_menu() {
#ifdef VIAMD_ENABLE_OPENMM
        if (ImGui::BeginMenu("Simulation")) {
            if (ImGui::MenuItem("OpenMM Simulation", nullptr, false)) {
                // This will be set via the ApplicationState
            }
            ImGui::EndMenu();
        }
#endif
    }

    void on_topology_init(ApplicationState& state) {
#ifdef VIAMD_ENABLE_OPENMM
        if (state.mold.mol.atom.count > 0) {
            MD_LOG_INFO("Topology loaded, OpenMM system can be initialized");
            // Reset simulation state when new topology is loaded
            cleanup_simulation(state);
        }
#endif
    }

    void on_topology_free() {
        // Note: We can't call cleanup_simulation here without ApplicationState
        // The ApplicationState will handle clearing the simulation flags
        MD_LOG_INFO("Topology freed, OpenMM simulation stopped");
    }

private:
    void setup_system(ApplicationState& state) {
#ifdef VIAMD_ENABLE_OPENMM
        try {
            if (state.mold.mol.atom.count == 0) {
                MD_LOG_ERROR("No atoms available for simulation setup");
                return;
            }

            // Create OpenMM system
            sim_context.system = std::make_unique<OpenMM::System>();
            
            // Add particles (atoms) to the system with AMBER masses
            std::vector<std::string> amber_types(state.mold.mol.atom.count);
            for (size_t i = 0; i < state.mold.mol.atom.count; ++i) {
                uint8_t atomic_number = get_atomic_number_from_atom_type(state.mold.mol.atom.type[i]);
                
                // Create a temporary connectivity for atom type assignment
                std::vector<uint32_t> temp_connectivity;
                amber_types[i] = map_to_amber_type(state.mold.mol.atom.type[i], atomic_number, temp_connectivity, state);
                
                // Get AMBER mass for this atom type
                AmberAtomType atom_params = get_amber_atom_params(amber_types[i]);
                double mass = atom_params.mass;
                
                sim_context.system->addParticle(mass);
                
                MD_LOG_DEBUG("Atom %zu: type=%s, AMBER_type=%s, mass=%.3f amu", 
                           i, state.mold.mol.atom.type[i].buf, amber_types[i].c_str(), mass);
            }
            
            // Set up basic force field (simplified)
            setup_force_field(state);
            
            // Create integrator
            sim_context.integrator = std::make_unique<OpenMM::LangevinIntegrator>(
                state.simulation.temperature, state.simulation.friction, state.simulation.timestep);
            
            // Create context
            sim_context.context = std::make_unique<OpenMM::Context>(
                *sim_context.system, *sim_context.integrator);
            
            // Set initial positions
            set_positions(state);
            
            // Perform energy minimization to prevent simulation explosion
            minimize_energy(state);
            
            state.simulation.initialized = true;
            state.simulation.current_frame = 0;
            state.simulation.simulation_time = 0.0;
            
            MD_LOG_INFO("OpenMM simulation system initialized with %zu atoms", 
                       state.mold.mol.atom.count);
                       
        } catch (const std::exception& e) {
            MD_LOG_ERROR("Failed to initialize OpenMM system: %s", e.what());
            cleanup_simulation(state);
        }
#endif
    }

    void setup_force_field(ApplicationState& state) {
        if (sim_context.force_field_type == ForceFieldType::AMBER) {
            setup_amber_force_field(state);
        } else if (sim_context.force_field_type == ForceFieldType::UFF) {
            setup_uff_force_field(state);
        }
    }

    void setup_amber_force_field(ApplicationState& state) {
        MD_LOG_INFO("Setting up AMBER force field...");
        
        // Step 1: Build connectivity information and assign AMBER atom types
        std::vector<std::string> amber_types(state.mold.mol.atom.count);
        std::vector<std::vector<uint32_t>> connectivity(state.mold.mol.atom.count);
        
        // Build connectivity matrix
        for (size_t i = 0; i < state.mold.mol.bond.count; ++i) {
            const auto& bond = state.mold.mol.bond.pairs[i];
            uint32_t atom1 = bond.idx[0];
            uint32_t atom2 = bond.idx[1];
            connectivity[atom1].push_back(atom2);
            connectivity[atom2].push_back(atom1);
        }
        
        // Assign AMBER atom types
        for (size_t i = 0; i < state.mold.mol.atom.count; ++i) {
            uint8_t atomic_number = get_atomic_number_from_atom_type(state.mold.mol.atom.type[i]);
            amber_types[i] = map_to_amber_type(state.mold.mol.atom.type[i], atomic_number, connectivity[i], state);
        }
        
        // Step 2: Set up bond forces with AMBER parameters
        if (state.mold.mol.bond.count > 0) {
            auto* bondForce = new OpenMM::HarmonicBondForce();
            
            for (size_t i = 0; i < state.mold.mol.bond.count; ++i) {
                const auto& bond = state.mold.mol.bond.pairs[i];
                uint32_t atom1 = bond.idx[0];
                uint32_t atom2 = bond.idx[1];
                
                // Get AMBER bond parameters
                AmberBondType bond_params = get_amber_bond_params(amber_types[atom1], amber_types[atom2]);
                
                // Calculate current bond length as starting point
                double dx = state.mold.mol.atom.x[atom1] - state.mold.mol.atom.x[atom2];
                double dy = state.mold.mol.atom.y[atom1] - state.mold.mol.atom.y[atom2];
                double dz = state.mold.mol.atom.z[atom1] - state.mold.mol.atom.z[atom2];
                double current_length = sqrt(dx*dx + dy*dy + dz*dz) * 0.1; // Convert to nm
                
                // Use AMBER equilibrium length, but if current length is reasonable, use it
                double equilibrium_length = bond_params.r0;
                if (current_length > 0.05 && current_length < 0.30) {  // Reasonable bond length range
                    equilibrium_length = current_length;
                }
                
                bondForce->addBond(atom1, atom2, equilibrium_length, bond_params.k);
                
                MD_LOG_DEBUG("Bond %zu-%zu: %s-%s, k=%.1f, r0=%.4f nm", 
                           atom1, atom2, amber_types[atom1].c_str(), amber_types[atom2].c_str(),
                           bond_params.k, equilibrium_length);
            }
            
            sim_context.system->addForce(bondForce);
            MD_LOG_INFO("Added %zu bond forces", state.mold.mol.bond.count);
        }
        
        // Step 3: Set up angle forces (missing from original implementation)
        auto* angleForce = new OpenMM::HarmonicAngleForce();
        size_t angle_count = 0;
        
        for (size_t center = 0; center < state.mold.mol.atom.count; ++center) {
            const auto& bonded = connectivity[center];
            
            // For each pair of atoms bonded to the center atom, create an angle
            for (size_t i = 0; i < bonded.size(); ++i) {
                for (size_t j = i + 1; j < bonded.size(); ++j) {
                    uint32_t atom1 = bonded[i];
                    uint32_t atom2 = static_cast<uint32_t>(center);
                    uint32_t atom3 = bonded[j];
                    
                    // Get AMBER angle parameters
                    AmberAngleType angle_params = get_amber_angle_params(
                        amber_types[atom1], amber_types[atom2], amber_types[atom3]);
                    
                    angleForce->addAngle(atom1, atom2, atom3, angle_params.theta0, angle_params.k);
                    angle_count++;
                    
                    MD_LOG_DEBUG("Angle %u-%u-%u: %s-%s-%s, k=%.1f, theta0=%.3f rad", 
                               atom1, atom2, atom3, 
                               amber_types[atom1].c_str(), amber_types[atom2].c_str(), amber_types[atom3].c_str(),
                               angle_params.k, angle_params.theta0);
                }
            }
        }
        
        if (angle_count > 0) {
            sim_context.system->addForce(angleForce);
            MD_LOG_INFO("Added %zu angle forces", angle_count);
        } else {
            delete angleForce;
        }
        
        // Step 4: Set up non-bonded forces with AMBER parameters
        auto* nonbondedForce = new OpenMM::NonbondedForce();
        
        for (size_t i = 0; i < state.mold.mol.atom.count; ++i) {
            AmberAtomType atom_params = get_amber_atom_params(amber_types[i]);
            
            // Use AMBER parameters for charge, sigma, epsilon
            double charge = atom_params.charge;
            double sigma = atom_params.sigma;
            double epsilon = atom_params.epsilon;
            
            // For initial stability, reduce charges by factor of 0.5
            // This prevents electrostatic explosions while still having realistic interactions
            charge *= 0.5;
            
            nonbondedForce->addParticle(charge, sigma, epsilon);
            
            MD_LOG_DEBUG("Atom %zu (%s): charge=%.3f, sigma=%.3f nm, epsilon=%.3f kJ/mol", 
                       i, amber_types[i].c_str(), charge, sigma, epsilon);
        }
        
        nonbondedForce->setNonbondedMethod(OpenMM::NonbondedForce::CutoffNonPeriodic);
        nonbondedForce->setCutoffDistance(1.0); // nm - standard cutoff for non-periodic
        
        sim_context.system->addForce(nonbondedForce);
        MD_LOG_INFO("Added non-bonded forces with AMBER parameters");
        
        MD_LOG_INFO("AMBER force field setup complete: %zu atoms, %zu bonds, %zu angles", 
                   state.mold.mol.atom.count, state.mold.mol.bond.count, angle_count);
    }

    void setup_uff_force_field(ApplicationState& state) {
        MD_LOG_INFO("Setting up UFF force field...");
        
        // Step 1: Build connectivity information and assign atomic numbers
        std::vector<uint8_t> atomic_numbers(state.mold.mol.atom.count);
        std::vector<std::vector<uint32_t>> connectivity(state.mold.mol.atom.count);
        
        // Build connectivity matrix
        for (size_t i = 0; i < state.mold.mol.bond.count; ++i) {
            const auto& bond = state.mold.mol.bond.pairs[i];
            uint32_t atom1 = bond.idx[0];
            uint32_t atom2 = bond.idx[1];
            connectivity[atom1].push_back(atom2);
            connectivity[atom2].push_back(atom1);
        }
        
        // Get atomic numbers for all atoms
        for (size_t i = 0; i < state.mold.mol.atom.count; ++i) {
            atomic_numbers[i] = get_atomic_number_from_atom_type(state.mold.mol.atom.type[i]);
        }
        
        // Step 2: Set up bond forces with UFF parameters
        if (state.mold.mol.bond.count > 0) {
            auto* bondForce = new OpenMM::HarmonicBondForce();
            
            for (size_t i = 0; i < state.mold.mol.bond.count; ++i) {
                const auto& bond = state.mold.mol.bond.pairs[i];
                uint32_t atom1 = bond.idx[0];
                uint32_t atom2 = bond.idx[1];
                
                // Get UFF bond parameters
                UFFBondType bond_params = get_uff_bond_params(atomic_numbers[atom1], atomic_numbers[atom2]);
                
                // Calculate current bond length as starting point
                double dx = state.mold.mol.atom.x[atom1] - state.mold.mol.atom.x[atom2];
                double dy = state.mold.mol.atom.y[atom1] - state.mold.mol.atom.y[atom2];
                double dz = state.mold.mol.atom.z[atom1] - state.mold.mol.atom.z[atom2];
                double current_length = sqrt(dx*dx + dy*dy + dz*dz) * 0.1; // Convert to nm
                
                // Use UFF equilibrium length, but if current length is reasonable, use it
                double equilibrium_length = bond_params.r0;
                if (current_length > 0.05 && current_length < 0.30) {  // Reasonable bond length range
                    equilibrium_length = current_length;
                }
                
                bondForce->addBond(atom1, atom2, equilibrium_length, bond_params.k);
                
                MD_LOG_DEBUG("UFF Bond %zu-%zu: elements %u-%u, k=%.1f, r0=%.4f nm", 
                           atom1, atom2, atomic_numbers[atom1], atomic_numbers[atom2],
                           bond_params.k, equilibrium_length);
            }
            
            sim_context.system->addForce(bondForce);
            MD_LOG_INFO("Added %zu UFF bond forces", state.mold.mol.bond.count);
        }
        
        // Step 3: Set up angle forces
        auto* angleForce = new OpenMM::HarmonicAngleForce();
        size_t angle_count = 0;
        
        for (size_t center = 0; center < state.mold.mol.atom.count; ++center) {
            const auto& bonded = connectivity[center];
            
            // For each pair of atoms bonded to the center atom, create an angle
            for (size_t i = 0; i < bonded.size(); ++i) {
                for (size_t j = i + 1; j < bonded.size(); ++j) {
                    uint32_t atom1 = bonded[i];
                    uint32_t atom2 = static_cast<uint32_t>(center);
                    uint32_t atom3 = bonded[j];
                    
                    // Get UFF angle parameters
                    UFFAngleType angle_params = get_uff_angle_params(
                        atomic_numbers[atom1], atomic_numbers[atom2], atomic_numbers[atom3]);
                    
                    angleForce->addAngle(atom1, atom2, atom3, angle_params.theta0, angle_params.k);
                    angle_count++;
                    
                    MD_LOG_DEBUG("UFF Angle %u-%u-%u: elements %u-%u-%u, k=%.1f, theta0=%.3f rad", 
                               atom1, atom2, atom3, 
                               atomic_numbers[atom1], atomic_numbers[atom2], atomic_numbers[atom3],
                               angle_params.k, angle_params.theta0);
                }
            }
        }
        
        if (angle_count > 0) {
            sim_context.system->addForce(angleForce);
            MD_LOG_INFO("Added %zu UFF angle forces", angle_count);
        } else {
            delete angleForce;
        }
        
        // Step 4: Set up non-bonded forces with UFF parameters
        auto* nonbondedForce = new OpenMM::NonbondedForce();
        
        for (size_t i = 0; i < state.mold.mol.atom.count; ++i) {
            UFFAtomType atom_params = get_uff_atom_params(atomic_numbers[i]);
            
            // Convert UFF parameters to OpenMM format
            double charge = atom_params.charge; // Usually zero for UFF
            
            // Convert UFF radius and depth to OpenMM sigma and epsilon
            // UFF uses radius in Angstroms, OpenMM needs sigma in nm
            double sigma = atom_params.radius * 0.1; // Convert Å to nm
            
            // Convert UFF depth from kcal/mol to kJ/mol
            double epsilon = atom_params.depth * 4.184; // kcal/mol to kJ/mol
            
            nonbondedForce->addParticle(charge, sigma, epsilon);
            
            MD_LOG_DEBUG("UFF Atom %zu (element %u): charge=%.3f, sigma=%.3f nm, epsilon=%.3f kJ/mol", 
                       i, atomic_numbers[i], charge, sigma, epsilon);
        }
        
        nonbondedForce->setNonbondedMethod(OpenMM::NonbondedForce::CutoffNonPeriodic);
        nonbondedForce->setCutoffDistance(1.0); // nm - standard cutoff for non-periodic
        
        sim_context.system->addForce(nonbondedForce);
        MD_LOG_INFO("Added non-bonded forces with UFF parameters");
        
        MD_LOG_INFO("UFF force field setup complete: %zu atoms, %zu bonds, %zu angles", 
                   state.mold.mol.atom.count, state.mold.mol.bond.count, angle_count);
    }

    void set_positions(ApplicationState& state) {
        std::vector<OpenMM::Vec3> positions;
        positions.reserve(state.mold.mol.atom.count);
        
        for (size_t i = 0; i < state.mold.mol.atom.count; ++i) {
            // Convert from Angstroms to nanometers
            double x = state.mold.mol.atom.x[i] * 0.1;
            double y = state.mold.mol.atom.y[i] * 0.1;
            double z = state.mold.mol.atom.z[i] * 0.1;
            positions.emplace_back(x, y, z);
        }
        
        sim_context.context->setPositions(positions);
    }

    void minimize_energy(ApplicationState& state) {
#ifdef VIAMD_ENABLE_OPENMM
        if (!sim_context.context) {
            return;
        }
        
        try {
            MD_LOG_INFO("Performing energy minimization to stabilize system...");
            
            // Perform local energy minimization to relax bad contacts
            // This is crucial to prevent simulation explosion
            OpenMM::LocalEnergyMinimizer::minimize(*sim_context.context, 1e-4, 1000);
            
            // Get minimized positions and update VIAMD coordinates
            OpenMM::State minimizedState = sim_context.context->getState(OpenMM::State::Positions | OpenMM::State::Energy);
            const std::vector<OpenMM::Vec3>& positions = minimizedState.getPositions();
            
            // Update VIAMD atom positions with minimized coordinates
            for (size_t i = 0; i < state.mold.mol.atom.count && i < positions.size(); ++i) {
                state.mold.mol.atom.x[i] = static_cast<float>(positions[i][0] * 10.0);
                state.mold.mol.atom.y[i] = static_cast<float>(positions[i][1] * 10.0);
                state.mold.mol.atom.z[i] = static_cast<float>(positions[i][2] * 10.0);
            }
            
            // Mark buffers as dirty for visualization update
            state.mold.dirty_buffers |= MolBit_DirtyPosition;
            
            double energy = minimizedState.getPotentialEnergy();
            MD_LOG_INFO("Energy minimization completed. Final energy: %.3f kJ/mol", energy);
            
        } catch (const std::exception& e) {
            MD_LOG_ERROR("Energy minimization failed: %s. Proceeding without minimization.", e.what());
        }
#endif
    }

    void run_simulation_step(ApplicationState& state) {
#ifdef VIAMD_ENABLE_OPENMM
        if (!state.simulation.initialized || !sim_context.context) {
            return;
        }
        
        try {
            // Run simulation steps
            sim_context.integrator->step(state.simulation.steps_per_update);
            
            // Get updated positions and check for explosion
            OpenMM::State openmmState = sim_context.context->getState(OpenMM::State::Positions | OpenMM::State::Energy);
            const std::vector<OpenMM::Vec3>& positions = openmmState.getPositions();
            
            // Check for simulation explosion (coordinates too large)
            bool explosion_detected = false;
            double max_coord = 0.0;
            
            for (size_t i = 0; i < positions.size(); ++i) {
                double x = positions[i][0] * 10.0; // Convert to Angstroms
                double y = positions[i][1] * 10.0;
                double z = positions[i][2] * 10.0;
                
                double coord_magnitude = sqrt(x*x + y*y + z*z);
                max_coord = std::max(max_coord, coord_magnitude);
                
                // Check for explosion: coordinates > 100 Angstroms from origin
                if (coord_magnitude > 100.0) {
                    explosion_detected = true;
                    break;
                }
            }
            
            // Check for NaN or infinite values in energy
            double energy = openmmState.getPotentialEnergy();
            if (std::isnan(energy) || std::isinf(energy)) {
                explosion_detected = true;
            }
            
            if (explosion_detected) {
                MD_LOG_ERROR("Simulation explosion detected! Max coordinate: %.3f Å, Energy: %.3f kJ/mol", 
                            max_coord, energy);
                MD_LOG_ERROR("Stopping simulation to prevent further instability");
                state.simulation.running = false;
                state.simulation.paused = false;
                return;
            }
            
            // Update VIAMD atom positions (convert from nm to Angstroms)
            for (size_t i = 0; i < state.mold.mol.atom.count && i < positions.size(); ++i) {
                state.mold.mol.atom.x[i] = static_cast<float>(positions[i][0] * 10.0);
                state.mold.mol.atom.y[i] = static_cast<float>(positions[i][1] * 10.0);
                state.mold.mol.atom.z[i] = static_cast<float>(positions[i][2] * 10.0);
            }
            
            // Mark buffers as dirty for visualization update
            state.mold.dirty_buffers |= MolBit_DirtyPosition;
            
            // Update simulation state
            state.simulation.current_frame++;
            state.simulation.simulation_time += state.simulation.timestep * state.simulation.steps_per_update;
            
        } catch (const std::exception& e) {
            MD_LOG_ERROR("Simulation step failed: %s", e.what());
            state.simulation.running = false;
        }
#endif
    }

    void draw_simulation_window(ApplicationState& state) {
#ifdef VIAMD_ENABLE_OPENMM
        if (!ImGui::Begin("OpenMM Simulation", &state.simulation.show_window)) {
            ImGui::End();
            return;
        }

        ImGui::Text("OpenMM Molecular Dynamics Simulation");
        ImGui::Text("Force Field: %s", sim_context.force_field_name.c_str());
        ImGui::Separator();

        // System information
        if (state.mold.mol.atom.count > 0) {
            ImGui::Text("System: %zu atoms, %zu bonds", 
                       state.mold.mol.atom.count, state.mold.mol.bond.count);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No molecular system loaded");
        }

        ImGui::Text("Status: %s", state.simulation.initialized ? "Initialized" : "Not initialized");
        
        if (state.simulation.initialized) {
            ImGui::Text("Frame: %d", state.simulation.current_frame);
            ImGui::Text("Time: %.3f ps", state.simulation.simulation_time);
        }

        ImGui::Separator();

        // Simulation parameters
        if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
            // Force field selection (only allow change when not initialized)
            if (!state.simulation.initialized) {
                ImGui::Text("Force Field:");
                ImGui::SameLine();
                
                static const char* force_field_items[] = { "AMBER14", "UFF" };
                static int force_field_current = (sim_context.force_field_type == ForceFieldType::AMBER) ? 0 : 1;
                
                if (ImGui::Combo("##force_field", &force_field_current, force_field_items, IM_ARRAYSIZE(force_field_items))) {
                    if (force_field_current == 0) {
                        sim_context.force_field_type = ForceFieldType::AMBER;
                        sim_context.force_field_name = "AMBER14";
                    } else {
                        sim_context.force_field_type = ForceFieldType::UFF;
                        sim_context.force_field_name = "UFF";
                    }
                    MD_LOG_INFO("Force field changed to: %s", sim_context.force_field_name.c_str());
                }
                
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("AMBER: Biomolecular force field (proteins, nucleic acids)\nUFF: Universal force field (general purpose)");
                }
                
                ImGui::Separator();
            }
            
            float temp = static_cast<float>(state.simulation.temperature);
            if (ImGui::SliderFloat("Temperature (K)", &temp, 250.0f, 400.0f)) {
                // Bounds checking for temperature
                temp = std::max(250.0f, std::min(400.0f, temp));
                state.simulation.temperature = temp;
            }
            
            float timestep = static_cast<float>(state.simulation.timestep);
            if (ImGui::SliderFloat("Timestep (ps)", &timestep, 0.0005f, 0.002f)) {
                // Much smaller timestep range to prevent instability
                // Reduced max from 0.005 to 0.002 ps
                timestep = std::max(0.0005f, std::min(0.002f, timestep));
                state.simulation.timestep = timestep;
            }
            
            float friction = static_cast<float>(state.simulation.friction);
            if (ImGui::SliderFloat("Friction (ps^-1)", &friction, 0.5f, 5.0f)) {
                // Constrain friction to reasonable range
                friction = std::max(0.5f, std::min(5.0f, friction));
                state.simulation.friction = friction;
            }
            
            ImGui::SliderInt("Steps per update", &state.simulation.steps_per_update, 1, 50);
            
            // Add helpful tooltips for safety
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Lower values = smoother animation, higher values = faster simulation");
            }
        }

        ImGui::Separator();

        // Control buttons
        if (!state.simulation.initialized) {
            if (ImGui::Button("Initialize System")) {
                if (state.mold.mol.atom.count > 0) {
                    setup_system(state);
                } else {
                    MD_LOG_ERROR("Load a molecular structure first");
                }
            }
        } else {
            if (!state.simulation.running) {
                if (ImGui::Button("Start Simulation")) {
                    state.simulation.running = true;
                    state.simulation.paused = false;
                }
            } else {
                if (!state.simulation.paused) {
                    if (ImGui::Button("Pause")) {
                        state.simulation.paused = true;
                    }
                } else {
                    if (ImGui::Button("Resume")) {
                        state.simulation.paused = false;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Stop")) {
                    state.simulation.running = false;
                    state.simulation.paused = false;
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Reset")) {
                reset_simulation(state);
            }
        }

        ImGui::End();
#endif
    }

    void reset_simulation(ApplicationState& state) {
#ifdef VIAMD_ENABLE_OPENMM
        if (state.simulation.initialized) {
            state.simulation.running = false;
            state.simulation.paused = false;
            state.simulation.current_frame = 0;
            state.simulation.simulation_time = 0.0;
            
            // Reset positions to initial state
            set_positions(state);
            
            MD_LOG_INFO("Simulation reset");
        }
#endif
    }

    void cleanup_simulation(ApplicationState& state) {
#ifdef VIAMD_ENABLE_OPENMM
        state.simulation.running = false;
        state.simulation.paused = false;
        state.simulation.initialized = false;
        
        sim_context.context.reset();
        sim_context.integrator.reset();
        sim_context.system.reset();
#endif
    }

    uint8_t get_atomic_number_from_atom_type(const md_label_t& atom_type) {
        // Convert atom type label to atomic number
        const char* type_str = atom_type.buf;
        
        // Simple mapping based on common element symbols
        if (strncmp(type_str, "H", 1) == 0) return 1;
        if (strncmp(type_str, "C", 1) == 0) return 6;
        if (strncmp(type_str, "N", 1) == 0) return 7;
        if (strncmp(type_str, "O", 1) == 0) return 8;
        if (strncmp(type_str, "P", 1) == 0) return 15;
        if (strncmp(type_str, "S", 1) == 0) return 16;
        if (strncmp(type_str, "Cl", 2) == 0) return 17;
        if (strncmp(type_str, "Na", 2) == 0) return 11;
        if (strncmp(type_str, "Mg", 2) == 0) return 12;
        if (strncmp(type_str, "Ca", 2) == 0) return 20;
        if (strncmp(type_str, "Fe", 2) == 0) return 26;
        if (strncmp(type_str, "Zn", 2) == 0) return 30;
        
        // Default to carbon if unknown
        return 6;
    }

    double get_atomic_mass(uint8_t atomic_number) {
        // Simplified atomic masses in amu
        static const double masses[] = {
            0.0,    // 0 (dummy)
            1.008,  // H
            4.003,  // He
            6.941,  // Li
            9.012,  // Be
            10.811, // B
            12.011, // C
            14.007, // N
            15.999, // O
            18.998, // F
            20.180, // Ne
            22.990, // Na
            24.305, // Mg
            26.982, // Al
            28.086, // Si
            30.974, // P
            32.065, // S
            35.453, // Cl
            39.948, // Ar
        };
        
        if (atomic_number < sizeof(masses) / sizeof(masses[0])) {
            return masses[atomic_number];
        }
        return 12.011; // Default to carbon mass
    }

    std::string map_to_amber_type(const md_label_t& atom_type, uint8_t atomic_number, const std::vector<uint32_t>& bonded_atoms, ApplicationState& state) {
        // Convert VIAMD atom type to AMBER atom type based on chemical environment
        const char* type_str = atom_type.buf;
        
        // First try direct mapping for common AMBER types
        std::string type_upper = type_str;
        std::transform(type_upper.begin(), type_upper.end(), type_upper.begin(), ::toupper);
        
        // Direct AMBER type mapping - check if this is already a known AMBER type
        AmberAtomType test_params = get_amber_atom_params(type_str);
        if (test_params.name == type_str) {  // Found exact match
            return type_str;
        }
        
        // Element-based mapping with chemical environment consideration
        switch (atomic_number) {
        case 1: // Hydrogen
            return "H";  // Generic hydrogen, could be refined based on bonding
        case 6: // Carbon
            if (bonded_atoms.size() == 4) return "CA";  // sp3 carbon (approximation)
            if (bonded_atoms.size() == 3) return "C";   // sp2 carbon (approximation)
            return "C*";  // Generic carbon
        case 7: // Nitrogen
            return "N";   // Generic nitrogen
        case 8: // Oxygen
            if (bonded_atoms.size() == 1) return "O";   // Carbonyl oxygen
            if (bonded_atoms.size() == 2) return "OH";  // Hydroxyl oxygen
            return "O*";  // Generic oxygen
        case 16: // Sulfur
            return "SH";  // Sulfur (thiol)
        default:
            // Create generic type based on element
            switch (atomic_number) {
            case 1: return "H*";
            case 6: return "C*";
            case 7: return "N*";
            case 8: return "O*";
            default: return "C*";  // Fallback to carbon
            }
        }
    }

    AmberAtomType get_amber_atom_params(const std::string& amber_type) {
        // AMBER14 force field parameters (selected subset)
        static const std::map<std::string, AmberAtomType> amber_atom_types = {
            // Protein backbone
            {"C",   {"C", 12.011, 0.339967, 0.359824, 0.5973}},    // Carbonyl carbon
            {"CA",  {"CA", 12.011, 0.339967, 0.359824, 0.0337}},    // Alpha carbon  
            {"CB",  {"CB", 12.011, 0.339967, 0.359824, -0.0875}},   // Beta carbon
            {"N",   {"N", 14.007, 0.325000, 0.711280, -0.4157}},   // Backbone nitrogen
            {"O",   {"O", 15.999, 0.296000, 0.878640, -0.5679}},   // Carbonyl oxygen
            {"H",   {"H", 1.008,  0.247135, 0.065270, 0.2719}},    // Backbone hydrogen
            {"HA",  {"HA", 1.008,  0.264953, 0.065270, 0.0337}},    // Alpha hydrogen
            {"HB",  {"HB", 1.008,  0.264953, 0.065270, 0.0295}},    // Beta hydrogen
            
            // Common side chains
            {"OH",  {"OH", 15.999, 0.306647, 0.880314, -0.6546}},   // Hydroxyl oxygen
            {"HO",  {"HO", 1.008,  0.000000, 0.000000, 0.4275}},    // Hydroxyl hydrogen
            {"SH",  {"SH", 32.065, 0.356359, 1.046000, -0.3119}},   // Sulfur
            {"HS",  {"HS", 1.008,  0.106908, 0.065270, 0.1933}},    // Sulfur hydrogen
            
            // Generic fallbacks
            {"C*",  {"C*", 12.011, 0.339967, 0.359824, 0.0000}},    // Generic carbon
            {"N*",  {"N*", 14.007, 0.325000, 0.711280, 0.0000}},    // Generic nitrogen  
            {"O*",  {"O*", 15.999, 0.296000, 0.878640, 0.0000}},    // Generic oxygen
            {"H*",  {"H*", 1.008,  0.247135, 0.065270, 0.0000}},    // Generic hydrogen
        };
        
        auto it = amber_atom_types.find(amber_type);
        if (it != amber_atom_types.end()) {
            return it->second;
        }
        
        // Fallback to generic parameters based on first character
        if (amber_type[0] == 'H') return amber_atom_types.at("H*");
        if (amber_type[0] == 'C') return amber_atom_types.at("C*");
        if (amber_type[0] == 'N') return amber_atom_types.at("N*");
        if (amber_type[0] == 'O') return amber_atom_types.at("O*");
        
        return amber_atom_types.at("C*");  // Ultimate fallback
    }

    AmberBondType get_amber_bond_params(const std::string& type1, const std::string& type2) {
        // AMBER bond parameters (kJ/mol/nm^2, nm)
        static const std::map<std::pair<std::string, std::string>, AmberBondType> amber_bond_types = {
            {{"C", "O"},   {502080.0, 0.1229}},   // C=O bond
            {{"C", "N"},   {351456.0, 0.1335}},   // C-N amide bond  
            {{"C", "CA"},  {317984.0, 0.1522}},   // C-C bond
            {{"CA", "N"},  {337648.0, 0.1449}},   // CA-N bond
            {{"CA", "HA"}, {307105.6, 0.1090}},   // C-H bond
            {{"CA", "CB"}, {317984.0, 0.1526}},   // CA-CB bond
            {{"N", "H"},   {363171.2, 0.1010}},   // N-H bond
            {{"OH", "HO"},  {462750.4, 0.0974}},   // O-H bond
            {{"SH", "HS"}, {274887.2, 0.1336}},   // S-H bond
            
            // Generic fallback bonds
            {{"C*", "C*"}, {317984.0, 0.1540}},   // Generic C-C
            {{"C*", "H*"}, {307105.6, 0.1090}},   // Generic C-H
            {{"N*", "H*"}, {363171.2, 0.1010}},   // Generic N-H
            {{"O*", "H*"}, {462750.4, 0.0960}},   // Generic O-H
        };
        
        // Try direct lookup
        auto key1 = std::make_pair(type1, type2);
        auto key2 = std::make_pair(type2, type1);  // Try reverse order
        
        auto it = amber_bond_types.find(key1);
        if (it != amber_bond_types.end()) {
            return it->second;
        }
        
        it = amber_bond_types.find(key2);
        if (it != amber_bond_types.end()) {
            return it->second;
        }
        
        // Fallback to generic types
        std::string gen1 = std::string(1, type1[0]) + "*";
        std::string gen2 = std::string(1, type2[0]) + "*";
        
        key1 = std::make_pair(gen1, gen2);
        key2 = std::make_pair(gen2, gen1);
        
        it = amber_bond_types.find(key1);
        if (it != amber_bond_types.end()) {
            return it->second;
        }
        
        it = amber_bond_types.find(key2);
        if (it != amber_bond_types.end()) {
            return it->second;
        }
        
        // Ultimate fallback
        return {317984.0, 0.1540};  // Generic C-C bond
    }

    AmberAngleType get_amber_angle_params(const std::string& type1, const std::string& type2, const std::string& type3) {
        // AMBER angle parameters (kJ/mol/rad^2, radians)  
        static const std::map<std::tuple<std::string, std::string, std::string>, AmberAngleType> amber_angle_types = {
            {std::make_tuple("N", "CA", "C"),   {527.184, 1.9373}},   // 111.0 degrees
            {std::make_tuple("CA", "C", "O"),   {568.518, 2.0944}},   // 120.0 degrees
            {std::make_tuple("CA", "C", "N"),   {585.760, 2.0246}},   // 116.0 degrees
            {std::make_tuple("C", "N", "CA"),   {418.400, 2.0246}},   // 116.0 degrees
            {std::make_tuple("H", "N", "CA"),   {418.400, 2.0595}},   // 118.0 degrees
            {std::make_tuple("HA", "CA", "N"),  {418.400, 1.9373}},   // 111.0 degrees
            {std::make_tuple("HA", "CA", "C"),  {418.400, 1.9373}},   // 111.0 degrees
            
            // Generic fallback angles
            {std::make_tuple("C*", "C*", "C*"), {527.184, 1.9373}},   // Generic C-C-C
            {std::make_tuple("C*", "C*", "H*"), {418.400, 1.9373}},   // Generic C-C-H
            {std::make_tuple("H*", "C*", "H*"), {276.144, 1.8762}},   // Generic H-C-H
        };
        
        auto key = std::make_tuple(type1, type2, type3);
        auto it = amber_angle_types.find(key);
        if (it != amber_angle_types.end()) {
            return it->second;
        }
        
        // Try reverse order
        key = std::make_tuple(type3, type2, type1);
        it = amber_angle_types.find(key);
        if (it != amber_angle_types.end()) {
            return it->second;
        }
        
        // Fallback to generic types
        std::string gen1 = std::string(1, type1[0]) + "*";
        std::string gen2 = std::string(1, type2[0]) + "*";
        std::string gen3 = std::string(1, type3[0]) + "*";
        
        key = std::make_tuple(gen1, gen2, gen3);
        it = amber_angle_types.find(key);
        if (it != amber_angle_types.end()) {
            return it->second;
        }
        
        key = std::make_tuple(gen3, gen2, gen1);
        it = amber_angle_types.find(key);
        if (it != amber_angle_types.end()) {
            return it->second;
        }
        
        // Ultimate fallback
        return {527.184, 1.9373};  // Generic tetrahedral angle
    }

    // UFF (Universal Force Field) parameter functions
    UFFAtomType get_uff_atom_params(uint8_t atomic_number) {
        // UFF atom parameters based on atomic number and hybridization
        // Reference: Rappé et al., J. Am. Chem. Soc. 1992, 114, 10024-10035
        static const std::map<uint8_t, UFFAtomType> uff_atom_types = {
            // Element symbol, mass, radius(Å), depth(kcal/mol), charge, hybridization
            {1,  {"H_", 1.008,  2.886, 0.044, 0.0, 0}},      // Hydrogen
            {6,  {"C_3", 12.011, 3.851, 0.105, 0.0, 3}},     // Carbon sp3
            {7,  {"N_3", 14.007, 3.660, 0.069, 0.0, 3}},     // Nitrogen sp3  
            {8,  {"O_3", 15.999, 3.500, 0.060, 0.0, 3}},     // Oxygen sp3
            {9,  {"F_", 18.998,  3.364, 0.050, 0.0, 0}},     // Fluorine
            {15, {"P_3", 30.974, 4.147, 0.305, 0.0, 3}},     // Phosphorus sp3
            {16, {"S_3", 32.065, 4.035, 0.274, 0.0, 3}},     // Sulfur sp3
            {17, {"Cl", 35.453,  3.947, 0.227, 0.0, 0}},     // Chlorine
            {35, {"Br", 79.904,  4.189, 0.251, 0.0, 0}},     // Bromine
            {53, {"I_", 126.904, 4.463, 0.339, 0.0, 0}},     // Iodine
        };
        
        auto it = uff_atom_types.find(atomic_number);
        if (it != uff_atom_types.end()) {
            return it->second;
        }
        
        // Fallback based on element type
        if (atomic_number == 1) return uff_atom_types.at(1);  // H
        if (atomic_number >= 3 && atomic_number <= 10) return uff_atom_types.at(6);  // C for period 2
        if (atomic_number >= 11 && atomic_number <= 18) return uff_atom_types.at(6); // C for period 3
        
        return uff_atom_types.at(6);  // Default to carbon
    }
    
    UFFBondType get_uff_bond_params(uint8_t atom1_number, uint8_t atom2_number) {
        // UFF bond parameters calculated using combining rules
        UFFAtomType atom1_params = get_uff_atom_params(atom1_number);
        UFFAtomType atom2_params = get_uff_atom_params(atom2_number);
        
        // Bond order (assuming single bonds for simplicity)
        double bond_order = 1.0;
        
        // UFF bond length: r_ij = r_i + r_j - 0.1332(r_i + r_j)ln(n)
        double r_sum = atom1_params.radius + atom2_params.radius;
        double r0 = r_sum - 0.1332 * r_sum * std::log(bond_order);
        
        // Convert from Angstroms to nm for OpenMM
        r0 *= 0.1;
        
        // UFF force constant: k = 664.12 * Z_i * Z_eff_j / r_ij^3
        // Simplified approximation
        double k = 700.0 * 4184.0; // Convert kcal/mol/Å² to kJ/mol/nm²
        
        return {k, r0};
    }
    
    UFFAngleType get_uff_angle_params(uint8_t atom1_number, uint8_t atom2_number, uint8_t atom3_number) {
        // UFF angle parameters based on center atom hybridization
        UFFAtomType center_params = get_uff_atom_params(atom2_number);
        
        double theta0, k;
        
        // Determine angle based on hybridization of center atom
        switch (center_params.hybridization) {
        case 3: // sp3 - tetrahedral
            theta0 = 109.47 * M_PI / 180.0; // radians
            k = 100.0 * 4184.0; // kcal/mol -> kJ/mol
            break;
        case 2: // sp2 - trigonal planar
            theta0 = 120.0 * M_PI / 180.0;
            k = 100.0 * 4184.0;
            break;
        case 1: // sp - linear
            theta0 = 180.0 * M_PI / 180.0;
            k = 100.0 * 4184.0;
            break;
        default: // Default to tetrahedral
            theta0 = 109.47 * M_PI / 180.0;
            k = 100.0 * 4184.0;
            break;
        }
        
        return {k, theta0};
    }
    
    std::string map_to_uff_type(const md_label_t& atom_type, uint8_t atomic_number) {
        // Map to UFF atom type based on atomic number
        // For now, use simple mapping - could be enhanced with connectivity analysis
        switch (atomic_number) {
        case 1: return "H_";
        case 6: return "C_3";  // Assume sp3 carbon
        case 7: return "N_3";  // Assume sp3 nitrogen
        case 8: return "O_3";  // Assume sp3 oxygen
        case 9: return "F_";
        case 15: return "P_3";
        case 16: return "S_3";
        case 17: return "Cl";
        case 35: return "Br";
        case 53: return "I_";
        default: return "C_3"; // Default to sp3 carbon
        }
    }
};

static OpenMMComponent g_openmm_component;

} // namespace openmm

#endif // VIAMD_ENABLE_OPENMM