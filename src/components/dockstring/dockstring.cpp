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

        ImGui::Spacing();

        // SMILES input
        ImGui::Text("SMILES String:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##smiles", smiles_input, sizeof(smiles_input));
        
        // Target protein selection
        ImGui::Text("Target Protein:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##target", target_protein, sizeof(target_protein));

        ImGui::Spacing();

        // Example molecules
        if (ImGui::CollapsingHeader("Example Molecules")) {
            draw_example_buttons();
        }

        ImGui::Spacing();

        // Docking controls
        ImGui::BeginDisabled(!dockstring_available || docking_in_progress);
        
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
            
            if (ImGui::Button("Load into VIA MD", ImVec2(-1, 0))) {
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

        error_message[0] = '\0';
        strcpy(info_message, "Starting docking calculation...");
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

        // For now, just show a message that the feature is implemented
        // In a full implementation, we would parse the PDB data and create a VIA MD molecule
        strcpy(info_message, "Docked molecule would be loaded into VIA MD visualization");
        
        // TODO: Parse PDB data and create md_molecule_t structure
        // TODO: Add to VIA MD's molecule rendering pipeline
        // TODO: Set up proper visualization alongside the protein target
    }
};

// Create a global instance of the component
static DockstringComponent g_dockstring_component;

} // namespace dockstring

#endif // VIAMD_ENABLE_DOCKSTRING