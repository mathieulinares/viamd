#define IMGUI_DEFINE_MATH_OPERATORS

#include <event.h>
#include <viamd.h>
#include <task_system.h>
#include <color_utils.h>

// md_molden.h is now available in mdlib molden branch
#if MD_MOLDEN
#include <md_molden.h>
#endif
#include <md_util.h>
#include <core/md_vec_math.h>
#include <core/md_log.h>
#include <core/md_arena_allocator.h>

#include <gfx/gl_utils.h>

#include <imgui_internal.h>
#include <imgui_widgets.h>

#include <algorithm>
#include <app/IconsFontAwesome6.h>

// Constants (following VeloxChem pattern)
#define ANGSTROM_TO_BOHR 1.8897261246257702
#define BOHR_TO_ANGSTROM 0.529177210903

/**
 * Molden Component for VIAMD
 * 
 * This component handles loading and visualization of Molden format files.
 * It follows the same architectural pattern as the VeloxChem component but
 * is simplified to handle only molecular geometry (Phase 3 requirement).
 * 
 * Key differences from VeloxChem:
 * - No orbital/NTO/RSP/VIB windows (Phase 3 focuses on geometry only)
 * - Simpler data structure (atoms + coordinates + bonds)
 * - No quantum chemistry calculations
 * 
 * API functions expected from md_molden (implemented in mdlib):
 * - md_molden_t* md_molden_create(md_allocator_i* alloc)
 * - bool md_molden_parse_file(md_molden_t* mol, str_t filename)
 * - void md_molden_destroy(md_molden_t* mol)
 * - void md_molden_reset(md_molden_t* mol)
 * - size_t md_molden_number_of_atoms(const md_molden_t* mol)
 * - const dvec3_t* md_molden_atom_coordinates(const md_molden_t* mol)
 * - const uint8_t* md_molden_atomic_numbers(const md_molden_t* mol)
 * - bool md_molden_system_init(md_system_t* sys, const md_molden_t* mol, md_allocator_i* alloc)
 */

struct Molden : viamd::EventHandler {
    Molden() { viamd::event_system_register_handler(*this); }
    
    // Molden data structure
#if MD_MOLDEN
    md_molden_t* molden = nullptr;
#else
    void* molden = nullptr;  // Placeholder when Molden not enabled
#endif
    
    // GL representation for molecule rendering
    md_gl_rep_t gl_rep = {};
    
    // Arena for persistent allocations tied to Molden object lifetime
    md_allocator_i* arena = nullptr;
    
    /**
     * Process events from the VIAMD event system
     * Mirrors the VeloxChem event handling pattern
     */
    void process_events(const viamd::Event* events, size_t num_events) final {
        for (size_t event_idx = 0; event_idx < num_events; ++event_idx) {
            const viamd::Event& e = events[event_idx];
            
            switch (e.type) {
            case viamd::EventType_ViamdInitialize: {
                ASSERT(e.payload_type == viamd::EventPayloadType_ApplicationState);
                ApplicationState& state = *(ApplicationState*)e.payload;
                
                // Initialize arena allocator for Molden data
                arena = md_arena_allocator_create(state.allocator.persistent, MEGABYTES(4));
                break;
            }
            
            case viamd::EventType_ViamdShutdown:
                // Clean up arena allocator
                md_arena_allocator_destroy(arena);
                break;
                
            case viamd::EventType_ViamdFrameTick: {
                ASSERT(e.payload_type == viamd::EventPayloadType_ApplicationState);
                ApplicationState& state = *(ApplicationState*)e.payload;
                
                // @TODO: Add any per-frame update logic if needed
                // Currently Molden is just static geometry display
                break;
            }
            
            case viamd::EventType_ViamdWindowDrawMenu:
                // Menu integration (similar to VeloxChem)
                if (molden) {
                    if (ImGui::BeginMenu("Molden")) {
                        ImGui::Text("Molden file loaded");
                        // @TODO: Add any Molden-specific menu items
                        ImGui::EndMenu();
                    }
                }
                break;
                
            case viamd::EventType_ViamdTopologyInit: {
                ASSERT(e.payload_type == viamd::EventPayloadType_ApplicationState);
                ApplicationState& state = *(ApplicationState*)e.payload;
                
                // Load Molden file if path is provided
                init_from_file(str_from_cstr(state.files.molecule), state);
                break;
            }
            
            case viamd::EventType_ViamdTopologyFree:
                // Clean up Molden data
                reset_data();
                break;
                
            default:
                break;
            }
        }
    }
    
    /**
     * Reset and clean up Molden data
     * Mirrors VeloxChem::reset_data()
     */
    void reset_data() {
        md_gl_rep_destroy(gl_rep);
        gl_rep = {};
        
#if MD_MOLDEN
        md_molden_destroy(molden);
#endif
        molden = nullptr;
        
        md_arena_allocator_reset(arena);
    }
    
    /**
     * Initialize and load Molden data from file
     * This is the core loading function that mirrors VeloxChem::init_from_file()
     * 
     * @param filename Path to the Molden file
     * @param state Application state for accessing allocators and molecule system
     */
    void init_from_file(str_t filename, ApplicationState& state) {
        str_t ext;
        if (extract_ext(&ext, filename)) {
            // Check for .molden extension (following VLX pattern which checks .out and .h5)
            if (str_eq_ignore_case(ext, STR_LIT("molden")) || str_eq_ignore_case(ext, STR_LIT("mold"))) {
                MD_LOG_INFO("Attempting to load Molden data from file '" STR_FMT "'", STR_ARG(filename));
                
#if MD_MOLDEN
                // Create or reset Molden context (following VeloxChem pattern)
                if (!molden) {
                    molden = md_molden_create(arena);
                } else {
                    md_molden_reset(molden);
                }
                
                if (md_molden_parse_file(molden, filename)) {
                    MD_LOG_INFO("Successfully loaded Molden data");
                    
                    // Extract molecular data from Molden file
                    // Following VeloxChem pattern lines 944-957
                    size_t num_atoms = md_molden_number_of_atoms(molden);
                    const dvec3_t* coords = md_molden_atom_coordinates(molden);
                    const uint8_t* atomic_numbers = md_molden_atomic_numbers(molden);
                    
                    if (!coords || !atomic_numbers || num_atoms == 0) {
                        MD_LOG_ERROR("Invalid Molden data - missing coordinates or atomic numbers");
                        reset_data();
                        return;
                    }
                    
                    // Prepare coordinate data for processing
                    // Following VeloxChem pattern lines 953-957
                    vec4_t* xyzw = (vec4_t*)md_vm_arena_push(state.allocator.frame, sizeof(vec4_t) * num_atoms);
                    for (size_t i = 0; i < num_atoms; ++i) {
                        xyzw[i] = vec4_set((float)coords[i].x, (float)coords[i].y, (float)coords[i].z, 1.0f);
                    }
                    
                    // Initialize molecule system from Molden data
                    // Following VeloxChem pattern lines 959-961
                    md_system_t mol = { 0 };
                    if (!md_molden_system_init(&mol, molden, state.allocator.frame)) {
                        MD_LOG_ERROR("Failed to initialize molecule system from Molden data");
                        reset_data();
                        return;
                    }
                    
                    // Postprocess molecule: infer bonds, assign stereochemistry, compute secondary structure
                    // This is a key step that matches VeloxChem workflow
                    md_util_molecule_postprocess(&mol, state.allocator.frame, 
                        MD_UTIL_POSTPROCESS_BOND_BIT | MD_UTIL_POSTPROCESS_STRUCTURE_BIT);
                    
                    // Create color array for CPK coloring
                    // Following VeloxChem pattern lines 964-965
                    uint32_t* colors = (uint32_t*)md_vm_arena_push(state.allocator.frame, 
                        mol.atom.count * sizeof(uint32_t));
                    color_atoms_cpk(colors, mol.atom.count, mol);
                    
                    // Create OpenGL representation
                    // Following VeloxChem pattern lines 967-968
                    gl_rep = md_gl_rep_create(state.mold.gl_mol);
                    md_gl_rep_set_color(gl_rep, 0, (uint32_t)mol.atom.count, colors, 0);
                    
                    MD_LOG_INFO("Molden molecule visualization initialized with %zu atoms", num_atoms);
                } else {
                    MD_LOG_ERROR("Failed to parse Molden file '" STR_FMT "'", STR_ARG(filename));
                    reset_data();
                }
#else
                MD_LOG_ERROR("Molden support not compiled in - rebuild with MD_ENABLE_MOLDEN=ON");
#endif
            }
        }
    }
};

// Create static instance (following VeloxChem pattern)
static Molden instance = {};
