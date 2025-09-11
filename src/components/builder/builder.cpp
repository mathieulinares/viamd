#include <event.h>

#ifdef VIAMD_ENABLE_BUILDER

// Include lightweight molecule builder
#include "lightweight_mol_builder.h"

#include <viamd.h>

#include <core/md_common.h>
#include <core/md_allocator.h>
#include <core/md_arena_allocator.h>
#include <core/md_log.h>
#include <core/md_vec_math.h>
#include <core/md_array.h>
#include <core/md_bitfield.h>
#include <md_molecule.h>
#include <md_util.h>
#include <md_gl.h>

#include <imgui_widgets.h>
#include <imgui.h>
#include <loader.h>

#ifdef VIAMD_ENABLE_OPENMM
#include "../openmm/openmm_interface.h"
#endif

#include <memory>
#include <string>
#include <vector>
#include <cstring>
#include <map>
#include <cmath>

namespace builder {

struct MoleculeBuilder : viamd::EventHandler {
    bool show_window = false;
    bool molecule_builder_available = true; // Always available with lightweight implementation
    
    char smiles_input[256] = "CCO";  // Default to ethanol
    char error_message[512] = "";
    char info_message[512] = "";
    
    ApplicationState* app_state = nullptr;
    md_allocator_i* arena = nullptr;
    
    // Lightweight molecule builder instance
    lightweight_mol::MoleculeBuilder mol_builder;
    
    // Built molecule data
    struct BuiltMolecule {
        md_molecule_t mol = {};
        bool valid = false;
        int num_atoms = 0;
        int num_bonds = 0;
        std::string formula;
    } built_molecule;

    MoleculeBuilder() { 
        viamd::event_system_register_handler(*this);
        MD_LOG_INFO("Lightweight molecule builder enabled");
    }

    void process_events(const viamd::Event* events, size_t num_events) final {
        for (size_t i = 0; i < num_events; ++i) {
            const viamd::Event& e = events[i];

            switch (e.type) {
            case viamd::EventType_ViamdInitialize: {
                app_state = (ApplicationState*)e.payload;
                arena = md_arena_allocator_create(app_state->allocator.persistent, MEGABYTES(1));
                MD_LOG_INFO("Molecule Builder component initialized");
                break;
            }
            case viamd::EventType_ViamdShutdown:
                if (arena) {
                    cleanup_built_molecule();
                    md_arena_allocator_destroy(arena);
                }
                break;
            case viamd::EventType_ViamdFrameTick:
                draw_window();
                break;
            case viamd::EventType_ViamdWindowDrawMenu: {
                if (ImGui::BeginMenu("Builder")) {
                    ImGui::Checkbox("Molecule Builder", &show_window);
                    ImGui::EndMenu();
                }
                break;
            }
            default:
                break;
            }
        }
    }

    void cleanup_built_molecule() {
        if (built_molecule.valid && app_state && app_state->mold.mol_alloc) {
            md_molecule_free(&built_molecule.mol, app_state->mold.mol_alloc);
            built_molecule = {};
        }
    }

    void clear_molecule_from_viamd() {
        if (!app_state) {
            strcpy(error_message, "Application state not available");
            return;
        }

        // Complete scene clearing
        // Reset the arena allocator to free existing molecule data
        md_arena_allocator_reset(app_state->mold.mol_alloc);
        
        // Zero out the existing molecule structure completely
        memset(&app_state->mold.mol, 0, sizeof(app_state->mold.mol));
        
        // Destroy existing GPU resources
        if (app_state->mold.gl_mol.id != 0) {
            md_gl_mol_destroy(app_state->mold.gl_mol);
            app_state->mold.gl_mol = {0};
        }
        
        // Clear selection and highlight masks
        md_bitfield_clear(&app_state->selection.selection_mask);
        md_bitfield_clear(&app_state->selection.highlight_mask);
        
        // Update app state flags
        app_state->mold.dirty_buffers = MolBit_DirtyPosition | MolBit_DirtyRadius | MolBit_DirtyBonds;
        
        // Clear the builder flag
        app_state->mold.from_builder = false;
        
        // Reset animation
        app_state->animation.frame = 0.0;
        
        // Clear trajectory - check if exists first
        if (app_state->mold.traj) {
            ::load::traj::close(app_state->mold.traj);
            app_state->mold.traj = nullptr;
        }

        snprintf(info_message, sizeof(info_message), "Molecule cleared from VIAMD");
        
        MD_LOG_INFO("Molecule builder: cleared molecule from VIAMD");
        
        // First clear representations, then broadcast topology initialization event to recreate GL resources
        viamd::event_system_broadcast_event(viamd::EventType_ViamdRepresentationsClear, viamd::EventPayloadType_ApplicationState, app_state);
        viamd::event_system_broadcast_event(viamd::EventType_ViamdTopologyInit, viamd::EventPayloadType_ApplicationState, app_state);
    }

    bool build_molecule_from_smiles(const char* smiles) {
        if (!smiles || strlen(smiles) == 0) {
            strcpy(error_message, "Please enter a SMILES string");
            return false;
        }

        try {
            // Use lightweight molecule builder
            lightweight_mol::Molecule lightweight_mol;
            
            if (!mol_builder.build_from_smiles(std::string(smiles), lightweight_mol)) {
                snprintf(error_message, sizeof(error_message), "Error: %s", mol_builder.get_error().c_str());
                return false;
            }

            // Convert lightweight molecule to VIAMD format
            return convert_lightweight_to_viamd(lightweight_mol);

        } catch (const std::exception& e) {
            snprintf(error_message, sizeof(error_message), "Error: %s", e.what());
            return false;
        } catch (...) {
            strcpy(error_message, "Unknown error in molecule processing");
            return false;
        }
    }

    bool convert_lightweight_to_viamd(const lightweight_mol::Molecule& lightweight_mol) {
        cleanup_built_molecule();

        if (!app_state || !app_state->mold.mol_alloc) {
            strcpy(error_message, "Application state not available");
            return false;
        }

        // Initialize VIAMD molecule structure - zero initialize
        built_molecule.mol = {};

        unsigned int num_atoms = lightweight_mol.atoms.size();
        unsigned int num_bonds = lightweight_mol.bonds.size();

        // Use the same allocator that will manage the molecule in VIAMD
        md_allocator_i* mol_alloc = app_state->mold.mol_alloc;

        // Allocate arrays for atoms
        md_array_resize(built_molecule.mol.atom.x, num_atoms, mol_alloc);
        md_array_resize(built_molecule.mol.atom.y, num_atoms, mol_alloc);
        md_array_resize(built_molecule.mol.atom.z, num_atoms, mol_alloc);
        md_array_resize(built_molecule.mol.atom.element, num_atoms, mol_alloc);
        md_array_resize(built_molecule.mol.atom.type, num_atoms, mol_alloc);
        md_array_resize(built_molecule.mol.atom.radius, num_atoms, mol_alloc);
        md_array_resize(built_molecule.mol.atom.mass, num_atoms, mol_alloc);
        md_array_resize(built_molecule.mol.atom.flags, num_atoms, mol_alloc);

        // Convert atoms
        for (unsigned int i = 0; i < num_atoms; ++i) {
            const auto& atom = lightweight_mol.atoms[i];

            // Position (convert from Angstrom to nanometers)
            built_molecule.mol.atom.x[i] = atom.x * 0.1f;
            built_molecule.mol.atom.y[i] = atom.y * 0.1f;
            built_molecule.mol.atom.z[i] = atom.z * 0.1f;

            // Element
            built_molecule.mol.atom.element[i] = (uint8_t)atom.element;

            // Atom type (use element symbol)
            str_t element_name = md_util_element_symbol(atom.element);
            // Convert str_t to md_label_t by copying the string data
            size_t copy_len = MIN(element_name.len, sizeof(built_molecule.mol.atom.type[i].buf) - 1);
            memcpy(built_molecule.mol.atom.type[i].buf, element_name.ptr, copy_len);
            built_molecule.mol.atom.type[i].buf[copy_len] = '\0';

            // Properties
            built_molecule.mol.atom.radius[i] = md_util_element_vdw_radius(atom.element);
            built_molecule.mol.atom.mass[i] = (float)md_util_element_atomic_mass(atom.element);
            built_molecule.mol.atom.flags[i] = 0;
        }

        built_molecule.mol.atom.count = num_atoms;

        // Convert bonds
        if (num_bonds > 0) {
            md_array_resize(built_molecule.mol.bond.pairs, num_bonds, mol_alloc);
            md_array_resize(built_molecule.mol.bond.order, num_bonds, mol_alloc);

            for (unsigned int i = 0; i < num_bonds; ++i) {
                const auto& bond = lightweight_mol.bonds[i];
                
                built_molecule.mol.bond.pairs[i].idx[0] = bond.atom1;
                built_molecule.mol.bond.pairs[i].idx[1] = bond.atom2;
                
                // Convert bond order
                built_molecule.mol.bond.order[i] = bond.order;
                if (bond.aromatic) {
                    built_molecule.mol.bond.order[i] |= MD_BOND_FLAG_AROMATIC;
                }
            }
            built_molecule.mol.bond.count = num_bonds;
        }

        // Set molecule info
        built_molecule.num_atoms = num_atoms;
        built_molecule.num_bonds = num_bonds;
        built_molecule.formula = lightweight_mol.formula;
        built_molecule.valid = true;

        snprintf(info_message, sizeof(info_message), 
                 "Built molecule: %s (%d atoms, %d bonds)", 
                 built_molecule.formula.c_str(), num_atoms, num_bonds);

        error_message[0] = '\0';  // Clear any previous errors
        return true;
    }

    void load_molecule_into_viamd() {
        if (!built_molecule.valid || !app_state) {
            strcpy(error_message, "No valid molecule to load");
            return;
        }

        // Complete scene clearing before loading new molecule
        // This prevents molecules from being added on top of each other
        
        // Reset the arena allocator to free existing molecule data
        md_arena_allocator_reset(app_state->mold.mol_alloc);
        
        // Zero out the existing molecule structure completely
        memset(&app_state->mold.mol, 0, sizeof(app_state->mold.mol));
        
        // Destroy existing GPU resources
        if (app_state->mold.gl_mol.id != 0) {
            md_gl_mol_destroy(app_state->mold.gl_mol);
            app_state->mold.gl_mol = {0};
        }
        
        // Clear selection and highlight masks
        md_bitfield_clear(&app_state->selection.selection_mask);
        md_bitfield_clear(&app_state->selection.highlight_mask);
        
        // Copy the built molecule to the app state
        app_state->mold.mol = built_molecule.mol;
        
        // Transfer ownership by clearing our copy without freeing
        built_molecule.mol = {};
        built_molecule.valid = false;

        // Mark that this molecule came from the builder
        app_state->mold.from_builder = true;

        // Update app state flags
        app_state->mold.dirty_buffers = MolBit_DirtyPosition | MolBit_DirtyRadius | MolBit_DirtyBonds;
        
        // Reset animation
        app_state->animation.frame = 0.0;
        
        // Clear trajectory - check if exists first
        if (app_state->mold.traj) {
            ::load::traj::close(app_state->mold.traj);
            app_state->mold.traj = nullptr;
        }

        snprintf(info_message, sizeof(info_message), 
                 "Molecule loaded successfully: %s", built_molecule.formula.c_str());
        
        MD_LOG_INFO("Molecule builder: loaded molecule into VIAMD");
        
        // Broadcast topology initialization event to recreate GL resources
        viamd::event_system_broadcast_event(viamd::EventType_ViamdTopologyInit, viamd::EventPayloadType_ApplicationState, app_state);
        
        // Perform energy minimization with UFF after loading
#ifdef VIAMD_ENABLE_OPENMM
        openmm_interface::minimize_energy_if_available(*app_state);
#endif
    }

    void draw_example_buttons() {
        const char* examples[][2] = {
            {"Water", "O"},
            {"Methane", "C"},
            {"Ethanol", "CCO"},
            {"Benzene", "c1ccccc1"},
            {"Caffeine", "CN1C=NC2=C1C(=O)N(C(=O)N2C)C"},
            {"Aspirin", "CC(=O)OC1=CC=CC=C1C(=O)O"},
            {"Glucose", "C([C@@H]1[C@H]([C@@H]([C@H]([C@H](O1)O)O)O)O)O"},
        };

        ImGui::Text("Quick Examples:");
        
        for (size_t i = 0; i < sizeof(examples) / sizeof(examples[0]); ++i) {
            if (i > 0 && i % 2 == 0) ImGui::NewLine();
            if (i % 2 == 1) ImGui::SameLine();
            
            if (ImGui::Button(examples[i][0])) {
                strcpy(smiles_input, examples[i][1]);
            }
        }
    }

    void draw_window() {
        if (!show_window) return;

        ImGui::SetNextWindowSize({400, 500}, ImGuiCond_FirstUseEver);
        
        if (ImGui::Begin("Molecule Builder", &show_window, ImGuiWindowFlags_NoFocusOnAppearing)) {
            // SMILES input
            ImGui::Text("SMILES String:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##smiles", smiles_input, sizeof(smiles_input));
            
            ImGui::Separator();
            
            // Example buttons
            draw_example_buttons();
            
            ImGui::Separator();
            
            // Build button
            if (ImGui::Button("Build Molecule", ImVec2(-1, 0))) {
                build_molecule_from_smiles(smiles_input);
            }
            
            // Load button (only enabled if we have a valid molecule)
            ImGui::BeginDisabled(!built_molecule.valid);
            if (ImGui::Button("Load into VIAMD", ImVec2(-1, 0))) {
                load_molecule_into_viamd();
            }
            ImGui::EndDisabled();
            
            // Clear button - clear any loaded molecule from VIAMD
            if (ImGui::Button("Clear Molecule", ImVec2(-1, 0))) {
                clear_molecule_from_viamd();
            }
            
            ImGui::Separator();
            
            // Status messages
            if (strlen(error_message) > 0) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                ImGui::TextWrapped("Error: %s", error_message);
                ImGui::PopStyleColor();
            }
            
            if (strlen(info_message) > 0) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                ImGui::TextWrapped("Info: %s", info_message);
                ImGui::PopStyleColor();
            }
            
            // Molecule info
            if (built_molecule.valid) {
                ImGui::Separator();
                ImGui::Text("Built Molecule:");
                ImGui::BulletText("Formula: %s", built_molecule.formula.c_str());
                ImGui::BulletText("Atoms: %d", built_molecule.num_atoms);
                ImGui::BulletText("Bonds: %d", built_molecule.num_bonds);
            }
        }
        ImGui::End();
    }
};

// Global instance
static MoleculeBuilder component;

} // namespace builder

#endif // VIAMD_ENABLE_BUILDER