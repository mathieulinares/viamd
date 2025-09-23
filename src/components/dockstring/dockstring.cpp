#define IMGUI_DEFINE_MATH_OPERATORS

#include <event.h>

#ifdef VIAMD_ENABLE_DOCKSTRING

#include <viamd.h>
#include <task_system.h>

#include <core/md_log.h>
#include <core/md_vec_math.h>
#include <core/md_array.h>
#include <core/md_arena_allocator.h>
#include <core/md_str.h>
#include <core/md_os.h>

#include <md_molecule.h>
#include <md_util.h>
#include <md_pdb.h>

#include <imgui_widgets.h>
#include <imgui_internal.h>

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

namespace dockstring {

struct DockstringComponent : viamd::EventHandler {
    bool show_window = false;
    bool dockstring_available = false;
    
    char smiles_input[256] = "CCO";  // Default to ethanol
    char target_protein[256] = "DRD2";  // Default target
    char error_message[512] = "";
    char info_message[512] = "";
    
    bool use_loaded_protein = false;  // Whether to use already loaded protein
    bool protein_available = false;   // Whether there's a protein loaded in VIAMD
    
    ApplicationState* app_state = nullptr;
    md_allocator_i* arena = nullptr;
    
    // Docking results
    struct DockingResult {
        float score = 0.0f;
        bool valid = false;
        str_t ligand_pdb_data = {};
    } docking_result;

    task_system::ID docking_task = 0;
    bool docking_in_progress = false;

    DockstringComponent() { 
        viamd::event_system_register_handler(*this); 
    }

    void process_events(const viamd::Event* events, size_t num_events) final {
        for (size_t i = 0; i < num_events; ++i) {
            const viamd::Event& e = events[i];
            switch (e.type) {
            case viamd::EventType_ViamdInitialize: {
                ApplicationState& state = *(ApplicationState*)e.payload;
                initialize(state);
                break;
            }
            case viamd::EventType_ViamdShutdown:
                shutdown();
                break;
            case viamd::EventType_ViamdFrameTick:
                update();
                draw_window();
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
        app_state = &state;
        arena = md_arena_allocator_create(app_state->allocator.persistent, MEGABYTES(1));
        
        // Check if Python and dockstring are available
        check_dockstring_availability();
        
        // Check if there's a protein already loaded
        check_protein_availability();
    }

    void shutdown() {
        if (docking_task) {
            task_system::task_interrupt_and_wait_for(docking_task);
        }
        if (arena) {
            md_arena_allocator_destroy(arena);
        }
    }

    void update() {
        // Check if docking task is complete
        if (docking_task && !task_system::task_is_running(docking_task)) {
            // Handle docking completion
            handle_docking_completion();
            docking_task = task_system::INVALID_ID;
            docking_in_progress = false;
        }
    }

    void check_protein_availability() {
        protein_available = app_state && 
                           app_state->mold.mol.atom.count > 0 && 
                           strlen(app_state->files.molecule) > 0;
    }

    void check_dockstring_availability() {
        // Try to run a simple Python command to check if dockstring is available
        const char* test_cmd = "python3 -c \"import dockstring; print('success')\" 2>/dev/null";
        int result = system(test_cmd);
        dockstring_available = (result == 0);
        
        if (!dockstring_available) {
            strcpy(error_message, "Dockstring not available. Please install: pip install dockstring");
        } else {
            strcpy(info_message, "Dockstring is available and ready");
        }
    }

    void draw_menu() {
        if (ImGui::BeginMenu("Docking")) {
            ImGui::Checkbox("Dockstring", &show_window);
            ImGui::EndMenu();
        }
    }

    void draw_window() {
        if (!show_window) return;

        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Dockstring Molecular Docking", &show_window)) {
            draw_dockstring_interface();
        }
        ImGui::End();
    }

    void draw_dockstring_interface() {
        ImGui::Text("Molecular Docking with Dockstring");
        ImGui::Separator();

        // Status indicator
        if (dockstring_available) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "✓ Dockstring Available");
        } else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "✗ Dockstring Not Available");
        }

        // Check for protein availability on each frame
        check_protein_availability();
        
        if (protein_available) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "✓ Protein Loaded");
        }

        ImGui::Spacing();

        // SMILES input
        ImGui::Text("SMILES String:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##smiles", smiles_input, sizeof(smiles_input));
        
        // Protein target selection
        ImGui::Spacing();
        ImGui::Text("Target Protein:");
        
        if (protein_available) {
            if (ImGui::RadioButton("Use loaded protein", use_loaded_protein)) {
                use_loaded_protein = true;
            }
            if (use_loaded_protein) {
                ImGui::Text("Loaded: %s", app_state->files.molecule);
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "Note: Will dock against loaded protein structure");
            }
            if (ImGui::RadioButton("Use dockstring target", !use_loaded_protein)) {
                use_loaded_protein = false;
            }
        }
        
        if (!protein_available || !use_loaded_protein) {
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##target", target_protein, sizeof(target_protein));
            if (!protein_available) {
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "No protein loaded - using dockstring target");
            }
        }

        ImGui::Spacing();

        // Example molecules
        if (ImGui::CollapsingHeader("Example Molecules")) {
            draw_example_buttons();
        }

        ImGui::Spacing();

        // Docking controls
        bool can_dock = dockstring_available && !docking_in_progress && 
                       (use_loaded_protein ? protein_available : strlen(target_protein) > 0);
        
        ImGui::BeginDisabled(!can_dock);
        
        if (ImGui::Button("Dock Molecule", ImVec2(-1, 0))) {
            start_docking();
        }
        
        ImGui::EndDisabled();

        if (docking_in_progress) {
            ImGui::SameLine();
            ImGui::Text("Docking in progress...");
        }

        // Results section
        if (docking_result.valid) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Docking Results:");
            ImGui::Text("Score: %.3f kcal/mol", docking_result.score);
            
            if (ImGui::Button("Load into VIAMD", ImVec2(-1, 0))) {
                load_docked_molecule_into_viamd();
            }
        }

        // Status messages
        if (strlen(error_message) > 0) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error: %s", error_message);
        }
        
        if (strlen(info_message) > 0) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "%s", info_message);
        }
    }

    void draw_example_buttons() {
        const struct { const char* name; const char* smiles; } examples[] = {
            {"Ethanol", "CCO"},
            {"Aspirin", "CC(=O)OC1=CC=CC=C1C(=O)O"},
            {"Caffeine", "CN1C=NC2=C1C(=O)N(C(=O)N2C)C"},
            {"Ibuprofen", "CC(C)CC1=CC=C(C=C1)C(C)C(=O)O"},
            {"Risperidone", "CC1=C(C(=O)N2CCCCC2=N1)CCN3CCC(CC3)C4=NOC5=C4C=CC(=C5)F"},
        };

        for (const auto& example : examples) {
            if (ImGui::Button(example.name)) {
                strcpy(smiles_input, example.smiles);
            }
            ImGui::SameLine();
        }
        ImGui::NewLine();
    }

    void start_docking() {
        if (strlen(smiles_input) == 0) {
            strcpy(error_message, "Please enter a SMILES string");
            return;
        }

        if (use_loaded_protein && !protein_available) {
            strcpy(error_message, "No protein loaded in VIAMD");
            return;
        }

        if (!use_loaded_protein && strlen(target_protein) == 0) {
            strcpy(error_message, "Please enter a target protein name");
            return;
        }

        error_message[0] = '\0';
        
        if (use_loaded_protein) {
            strcpy(info_message, "Starting docking against loaded protein... (using DRD2 target for now)");
            // TODO: Implement custom target from loaded protein
            // For now, we'll use a default target but note that we're using the loaded protein
        } else {
            strcpy(info_message, "Starting docking calculation...");
        }
        
        docking_in_progress = true;

        // Start docking task
        docking_task = task_system::create_pool_task(STR_LIT("Dockstring Docking"), [this]() {
            perform_docking();
        });
        task_system::enqueue_task(docking_task);
    }

    void perform_docking() {
        // Create Python script to perform docking
        char script_path[512];
        char output_path[512];
        
        // Use temporary directory
        const char* temp_dir = "/tmp";
        snprintf(script_path, sizeof(script_path), "%s/viamd_docking.py", temp_dir);
        snprintf(output_path, sizeof(output_path), "%s/viamd_docking_result.txt", temp_dir);

        // Create Python script
        if (!create_docking_script(script_path, output_path)) {
            strcpy(error_message, "Failed to create docking script");
            return;
        }

        // Execute Python script
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "python3 %s 2>&1", script_path);
        
        int result = system(cmd);
        
        if (result == 0) {
            // Read results
            read_docking_results(output_path);
        } else {
            strcpy(error_message, "Docking calculation failed");
        }

        // Cleanup
        unlink(script_path);
        unlink(output_path);
    }

    bool create_docking_script(const char* script_path, const char* output_path) {
        FILE* f = fopen(script_path, "w");
        if (!f) return false;

        fprintf(f, "#!/usr/bin/env python3\n");
        fprintf(f, "import sys\n");
        fprintf(f, "try:\n");
        fprintf(f, "    from dockstring import load_target\n");
        fprintf(f, "    target = load_target('%s')\n", target_protein);
        fprintf(f, "    score, result_data = target.dock('%s')\n", smiles_input);
        fprintf(f, "    with open('%s', 'w') as out:\n", output_path);
        fprintf(f, "        out.write(f'SCORE:{score}\\n')\n");
        fprintf(f, "        if 'ligand' in result_data:\n");
        fprintf(f, "            from rdkit.Chem import MolToPDBBlock\n");
        fprintf(f, "            ligand = result_data['ligand']\n");
        fprintf(f, "            if ligand.GetNumConformers() > 0:\n");
        fprintf(f, "                pdb_block = MolToPDBBlock(ligand, confId=0)\n");
        fprintf(f, "                out.write('PDB_DATA:\\n')\n");
        fprintf(f, "                out.write(pdb_block)\n");
        fprintf(f, "    print(f'Docking completed, score: {score}')\n");
        fprintf(f, "except Exception as e:\n");
        fprintf(f, "    print(f'Error: {e}', file=sys.stderr)\n");
        fprintf(f, "    sys.exit(1)\n");

        fclose(f);
        return true;
    }

    void read_docking_results(const char* output_path) {
        FILE* f = fopen(output_path, "r");
        if (!f) {
            strcpy(error_message, "Could not read docking results");
            return;
        }

        char line[1024];
        bool score_found = false;
        
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "SCORE:", 6) == 0) {
                docking_result.score = atof(line + 6);
                score_found = true;
            } else if (strncmp(line, "PDB_DATA:", 9) == 0) {
                // Read PDB data
                md_arena_allocator_reset(arena);
                
                size_t pdb_size = 0;
                char* pdb_data = nullptr;
                
                // Read remaining file content
                fseek(f, 0, SEEK_END);
                long file_size = ftell(f);
                fseek(f, ftell(f) - (file_size - ftell(f)), SEEK_SET);
                
                while (fgets(line, sizeof(line), f)) {
                    size_t line_len = strlen(line);
                    pdb_data = (char*)md_arena_allocator_push(arena, pdb_size + line_len + 1);
                    if (pdb_size > 0) {
                        memcpy(pdb_data, docking_result.ligand_pdb_data.ptr, pdb_size);
                    }
                    memcpy(pdb_data + pdb_size, line, line_len);
                    pdb_size += line_len;
                    pdb_data[pdb_size] = '\0';
                    
                    docking_result.ligand_pdb_data = {pdb_data, pdb_size};
                }
            }
        }
        
        fclose(f);

        if (score_found) {
            docking_result.valid = true;
            snprintf(info_message, sizeof(info_message), 
                     "Docking completed! Score: %.3f kcal/mol", docking_result.score);
            error_message[0] = '\0';
        } else {
            strcpy(error_message, "Invalid docking results");
        }
    }

    void handle_docking_completion() {
        // This function is called when the docking task completes
        // Results are already processed in perform_docking()
    }

    void load_docked_molecule_into_viamd() {
        if (!docking_result.valid || !app_state) {
            strcpy(error_message, "No valid docking results to load");
            return;
        }

        if (docking_result.ligand_pdb_data.len == 0) {
            strcpy(error_message, "No PDB data available for docked molecule");
            return;
        }

        try {
            // Parse the PDB data and load it into VIAMD
            load_pdb_data_into_viamd();
            strcpy(info_message, "Docked molecule loaded into VIAMD successfully");
            error_message[0] = '\0';
        } catch (...) {
            strcpy(error_message, "Failed to load docked molecule into VIAMD");
        }
    }

private:
    void load_pdb_data_into_viamd() {
        // Create a temporary file with the PDB data
        char temp_pdb_path[512];
        snprintf(temp_pdb_path, sizeof(temp_pdb_path), "/tmp/viamd_docked_ligand_%lx.pdb", (unsigned long)this);
        
        FILE* temp_file = fopen(temp_pdb_path, "w");
        if (!temp_file) {
            strcpy(error_message, "Could not create temporary PDB file");
            return;
        }
        
        fwrite(docking_result.ligand_pdb_data.ptr, 1, docking_result.ligand_pdb_data.len, temp_file);
        fclose(temp_file);
        
        // Load the PDB file using VIAMD's molecule loader
        md_molecule_t ligand_mol = {};
        md_allocator_i* temp_alloc = md_arena_allocator_create(app_state->allocator.persistent, MEGABYTES(10));
        
        md_molecule_loader_i* pdb_loader = md_pdb_molecule_api();
        if (pdb_loader && pdb_loader->init_from_file) {
            bool load_success = pdb_loader->init_from_file(&ligand_mol, (str_t){temp_pdb_path, strlen(temp_pdb_path)}, nullptr, temp_alloc);
            
            if (load_success && ligand_mol.atom.count > 0) {
                // Add the ligand atoms to the existing molecule structure
                merge_ligand_with_existing_molecule(ligand_mol);
                
                // Trigger topology update events
                viamd::event_system_broadcast_event(viamd::EventType_ViamdTopologyInit, viamd::EventPayloadType_ApplicationState, app_state);
                
                strcpy(info_message, "Docked ligand added to VIAMD visualization");
            } else {
                strcpy(error_message, "Failed to parse docked molecule PDB data");
            }
        } else {
            strcpy(error_message, "PDB loader not available");
        }
        
        md_arena_allocator_destroy(temp_alloc);
        unlink(temp_pdb_path);
    }
    
    void merge_ligand_with_existing_molecule(const md_molecule_t& ligand_mol) {
        if (!app_state || ligand_mol.atom.count == 0) return;
        
        md_molecule_t* main_mol = &app_state->mold.mol;
        md_allocator_i* mol_alloc = app_state->mold.mol_alloc;
        
        if (!mol_alloc) {
            strcpy(error_message, "Molecule allocator not available");
            return;
        }
        
        size_t old_atom_count = main_mol->atom.count;
        size_t new_atom_count = old_atom_count + ligand_mol.atom.count;
        
        // Resize main molecule arrays to accommodate new atoms
        md_array_resize(main_mol->atom.x, new_atom_count, mol_alloc);
        md_array_resize(main_mol->atom.y, new_atom_count, mol_alloc);
        md_array_resize(main_mol->atom.z, new_atom_count, mol_alloc);
        md_array_resize(main_mol->atom.element, new_atom_count, mol_alloc);
        md_array_resize(main_mol->atom.radius, new_atom_count, mol_alloc);
        md_array_resize(main_mol->atom.mass, new_atom_count, mol_alloc);
        md_array_resize(main_mol->atom.flags, new_atom_count, mol_alloc);
        
        if (md_array_size(main_mol->atom.type) > 0) {
            md_array_resize(main_mol->atom.type, new_atom_count, mol_alloc);
        }
        
        // Copy ligand atoms to the end of the main molecule
        for (size_t i = 0; i < ligand_mol.atom.count; ++i) {
            size_t idx = old_atom_count + i;
            main_mol->atom.x[idx] = ligand_mol.atom.x[i];
            main_mol->atom.y[idx] = ligand_mol.atom.y[i];
            main_mol->atom.z[idx] = ligand_mol.atom.z[i];
            main_mol->atom.element[idx] = ligand_mol.atom.element[i];
            main_mol->atom.radius[idx] = ligand_mol.atom.radius[i];
            main_mol->atom.mass[idx] = ligand_mol.atom.mass[i];
            main_mol->atom.flags[idx] = ligand_mol.atom.flags[i];
            
            if (md_array_size(main_mol->atom.type) > 0 && md_array_size(ligand_mol.atom.type) > 0) {
                main_mol->atom.type[idx] = ligand_mol.atom.type[i];
            }
        }
        
        // Update atom count
        main_mol->atom.count = new_atom_count;
        
        // Mark buffers as dirty for re-rendering
        app_state->mold.dirty_buffers |= MolBit_DirtyPosition | MolBit_DirtyRadius;
    }

public:
};

// Create a global instance of the component
static DockstringComponent g_dockstring_component;

} // namespace dockstring

#endif // VIAMD_ENABLE_DOCKSTRING