// Haddock restraints: the distance restraints of a CNS table (the "assign" lines of a HADDOCK .tbl file), drawn between
// their two atoms. A restraint is satisfied when the distance lies in [d - dminus, d + dplus]. Each atom is matched to
// the system by segid (chain), resid and name, when the table is loaded and when a system is initialized. A restraint
// whose atoms are not in the system is listed but not drawn.

#include <core/md_log.h>
#include <core/md_common.h>
#include <core/md_str.h>
#include <core/md_file.h>
#include <core/md_path.h>
#include <core/md_arena_allocator.h>

#include <md_system.h>
#include <md_pdb.h>
#include <md_haddock.h>
#include <md_map_fit.h>
#include <md_clash.h>

#include <chrono>

#include <viamd_event.h>
#include <window_menu.h>
#include <viamd.h>
#include <serialization_utils.h>
#include <representation.h>
#include <event.h>
#include <gfx/immediate_draw_utils.h>
#include <components/haddock/haddock.h>

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

// An atom of the system shown (part -1, idx is its system atom) or of a file of the model folder (part is the file's index)
struct AtomRef {
    int part = -1;
    int32_t idx = -1;          // -1 when there is no such atom
};

static double ref_key(AtomRef r) { return (double)r.part * 1e9 + (double)r.idx; }

// A PDB file of the model files, read for the analysis only. The system shown is not changed by it.
struct FolderPart {
    std::string name;           // the folder and file name, to tell files with the same name apart
    std::string path;           // the file the part was read from
    md_system_t sys = {};       // owns the arena that holds the system and its state
    md_system_state_t state = {};
    md_haddock_energy_t energy = {}; // the HADDOCK energies from the PDB REMARK block, if present
    bool has_energy = false;
};

static void free_folder_part(FolderPart& p) {
    md_system_state_free(&p.state);
    md_system_free(&p.sys);
}

struct Restraint {
    md_haddock_distance_restraint_t r;
    AtomRef atom[2];            // per side: in the model files when some are loaded, else in the system shown
};

static int find_atom(const md_system_t& sys, const md_haddock_atom_sel_t& sel) {
    const md_str_t segid = md_str_t{sel.segid, strlen(sel.segid)};
    const md_str_t name  = md_str_t{sel.name, strlen(sel.name)};
    const md_str_t alt   = md_str_t{sel.alt_name, strlen(sel.alt_name)};
    for (size_t i = 0; i < sys.atom.count; ++i) {
        const md_str_t atom_name = md_atom_name(&sys.atom, i);
        if (md_str_eq(atom_name, name) == false && (md_str_len(alt) == 0 || md_str_eq(atom_name, alt) == false)) continue;
        const md_residue_idx_t res = md_system_residue_find_by_atom_idx(&sys, i);
        if (res < 0 || md_system_residue_seq_id(&sys, (size_t)res) != sel.resid) continue;
        const md_chain_idx_t chain = md_system_chain_find_by_atom_idx(&sys, i);
        if (chain < 0 || md_str_eq(md_system_chain_id(&sys, (size_t)chain), segid) == false) continue;
        return (int)i;
    }
    return -1;
}

// The C-alpha atoms of chain A, the atoms that matchmaker superposes on
static std::vector<size_t> chain_a_calpha(const md_system_t& sys) {
    std::vector<size_t> out;
    const md_str_t ca = md_str_from_cstr("CA");
    const md_str_t chain_a = md_str_from_cstr("A");
    for (size_t i = 0; i < sys.atom.count; ++i) {
        if (md_str_eq(md_atom_name(&sys.atom, i), ca) == false) continue;
        const md_chain_idx_t chain = md_system_chain_find_by_atom_idx(&sys, i);
        if (chain < 0 || md_str_eq(md_system_chain_id(&sys, (size_t)chain), chain_a) == false) continue;
        out.push_back(i);
    }
    return out;
}

// The chain ids of a list separated by spaces or commas
static std::vector<std::string> parse_chains(const std::string& txt) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : txt) {
        if (c == ' ' || c == ',' || c == '\t') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static bool input_text(const char* label, std::string& txt) {
    char buf[2048];
    snprintf(buf, sizeof(buf), "%s", txt.c_str());
    if (!ImGui::InputText(label, buf, sizeof(buf))) return false;
    txt = buf;
    return true;
}

static bool in_chains(const md_system_t& sys, size_t i, const std::vector<std::string>& chains) {
    const md_chain_idx_t chain = md_system_chain_find_by_atom_idx(&sys, i);
    if (chain < 0) return false;
    const md_str_t id = md_system_chain_id(&sys, (size_t)chain);
    for (const std::string& c : chains) {
        if (c.size() == id.len && memcmp(c.data(), id.ptr, id.len) == 0) return true;
    }
    return false;
}

// The atoms whose chain is in the list, or outside it when invert is set (so an empty list with invert is every atom)
static std::vector<uint32_t> atoms_in_chains(const md_system_t& sys, const std::vector<std::string>& chains, bool invert) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < sys.atom.count; ++i) {
        if (in_chains(sys, i, chains) != invert) out.push_back((uint32_t)i);
    }
    return out;
}

// Chain, residue number and atom name, as in ChimeraX (A 12 CA)
static std::string atom_label(const md_system_t& sys, size_t i) {
    std::string out;
    const md_chain_idx_t chain = md_system_chain_find_by_atom_idx(&sys, i);
    if (chain >= 0) {
        const md_str_t id = md_system_chain_id(&sys, (size_t)chain);
        out.assign(id.ptr, id.len);
    }
    const md_residue_idx_t res = md_system_residue_find_by_atom_idx(&sys, i);
    if (res >= 0) out += " " + std::to_string(md_system_residue_seq_id(&sys, (size_t)res));
    const md_str_t name = md_atom_name(&sys.atom, i);
    out += " ";
    out.append(name.ptr, name.len);
    return out;
}

static md_float3_t transform_point(const float m[12], md_float3_t p) {
    md_float3_t r = {};
    r.x = m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3];
    r.y = m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7];
    r.z = m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11];
    return r;
}

// The label of an atom: the file's folder for a model file atom, then chain, residue number and atom name
static std::string ref_label(const ApplicationState& state, const std::vector<FolderPart>& files, AtomRef r) {
    if (r.idx < 0) return "-";
    if (r.part < 0) return atom_label(state.mold.sys, (size_t)r.idx);
    return files[r.part].name + ": " + atom_label(files[r.part].sys, (size_t)r.idx);
}

// The position of an atom: the system shown is given by its coordinates (sys_xyz, NULL when there are none)
static bool ref_position(const md_float3_t* sys_xyz, size_t sys_count, const std::vector<FolderPart>& files, AtomRef ref, md_float3_t* out) {
    if (ref.idx < 0) return false;
    if (ref.part < 0) {
        if (!sys_xyz || (size_t)ref.idx >= sys_count) return false;
        *out = sys_xyz[ref.idx];
        return true;
    }
    if ((size_t)ref.part >= files.size() || (size_t)ref.idx >= files[ref.part].state.num_atoms) return false;
    *out = files[ref.part].state.xyz[ref.idx];
    return true;
}

static bool atom_position(const ApplicationState& state, const std::vector<FolderPart>& files, AtomRef ref, md_float3_t* out) {
    return ref_position(state.mold.state.xyz, state.mold.state.num_atoms, files, ref, out);
}

static bool restraint_distance(const md_float3_t* sys_xyz, size_t sys_count, const std::vector<FolderPart>& files, const Restraint& rs, float* out) {
    md_float3_t a, b;
    if (!ref_position(sys_xyz, sys_count, files, rs.atom[0], &a) || !ref_position(sys_xyz, sys_count, files, rs.atom[1], &b)) return false;
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float dz = b.z - a.z;
    *out = std::sqrt(dx * dx + dy * dy + dz * dz);
    return true;
}

static bool restraint_satisfied(const Restraint& rs, float dist) {
    return dist >= rs.r.dist - rs.r.dist_minus && dist <= rs.r.dist + rs.r.dist_plus;
}

// The map fit of one model: its simulated-map score, and the local fit applied to it
struct ModelFit {
    bool scored = false;
    double score = 0.0;
    size_t points = 0;
    bool fitted = false;
    double fit_mean = 0.0;      // the mean map value at the atoms after the local fit
    double fit_shift = 0.0;     // the shift of the centroid by the local fit (Å)
    float transform[12] = {};   // the local fit, row-major 3x4: moved = R * original + t
    bool aligned = false;       // the superposition onto the first model is known
    double rmsd = 0.0;          // the chain A C-alpha RMSD to the first model after the superposition (A)
    float superpose[12] = {};   // the superposition onto the first model, row-major 3x4
};

// The rotation axis and angle (degrees) of a row-major 3x4 rigid transform
static void rotation_axis_angle(const float m[12], float axis[3], float* angle_deg) {
    const float tr = m[0] + m[5] + m[10];
    const float c = MD_CLAMP((tr - 1.0f) * 0.5f, -1.0f, 1.0f);
    const float angle = std::acos(c);
    const float s2 = 2.0f * std::sin(angle);
    axis[0] = 0.0f;
    axis[1] = 0.0f;
    axis[2] = 1.0f;
    if (std::fabs(s2) > 1e-6f) {
        axis[0] = (m[9] - m[6]) / s2;
        axis[1] = (m[2] - m[8]) / s2;
        axis[2] = (m[4] - m[1]) / s2;
    }
    *angle_deg = angle * 180.0f / 3.14159265358979f;
}

// An atom taking part in the clash search: its reference, position and element
struct Member {
    AtomRef ref;
    md_float3_t p;
    md_atomic_number_t z;
};

struct Clash {
    AtomRef atom[2];    // the first in the fixed chains and the second in the moving chains
    float overlap;      // van der Waals overlap (Å), after the hydrogen bond allowance
};

// The DisVis report of a results folder: the complex counts per number of satisfied restraints, the violation matrix and
// the z-scores, as DisVis writes them (accessible_complexes.out, violations.out, z-score.out)
struct DisvisReport {
    std::string dir;
    bool loaded = false;
    std::string error;
    std::vector<std::vector<double>> complexes;   // restraints, complexes, relative, cumulative, cumulative relative
    std::vector<int> violation_cols;
    std::vector<int> violation_rows;
    std::vector<std::vector<double>> violations;  // one row per violation_rows entry
    std::vector<std::vector<double>> zscore;
};

static std::vector<std::vector<double>> read_numeric_rows(const std::string& file, bool& ok) {
    std::vector<std::vector<double>> rows;
    std::ifstream in(file);
    ok = in.good();
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::vector<double> row;
        double v;
        while (ss >> v) row.push_back(v);
        if (!row.empty()) rows.push_back(row);
    }
    return rows;
}

static void load_disvis(DisvisReport& rep) {
    rep.loaded = false;
    rep.error.clear();
    const std::string base = rep.dir.empty() ? std::string() : rep.dir + "/";
    bool ok_c = false, ok_v = false, ok_z = false;
    rep.complexes = read_numeric_rows(base + "accessible_complexes.out", ok_c);
    const auto viol = read_numeric_rows(base + "violations.out", ok_v);
    rep.zscore = read_numeric_rows(base + "z-score.out", ok_z);
    if (!ok_c || !ok_v || !ok_z) {
        rep.error = "Missing DisVis output in " + rep.dir;
        return;
    }
    // violations.out: the first row holds the column labels, each further row a label then its values
    rep.violation_cols.clear();
    rep.violation_rows.clear();
    rep.violations.clear();
    for (size_t i = 0; i < viol.size(); ++i) {
        if (i == 0) {
            for (double c : viol[i]) rep.violation_cols.push_back((int)c);
        } else {
            rep.violation_rows.push_back((int)viol[i][0]);
            rep.violations.push_back(std::vector<double>(viol[i].begin() + 1, viol[i].end()));
        }
    }
    rep.loaded = true;
}

// The PowerFit solutions of a results folder (solutions.out): rank, cross-correlation, Fisher z-score and relative z-score
struct PowerfitReport {
    std::string dir;
    bool loaded = false;
    std::string error;
    std::vector<std::vector<double>> rows;   // rank, cc, Fisher z, relative z
};

// Reads the first columns (rank, cc, Fish-z, rel-z) of a PowerFit solutions.out (a comma separated file with a header)
static void load_powerfit(PowerfitReport& rep) {
    rep.loaded = false;
    rep.error.clear();
    rep.rows.clear();
    const std::string path = rep.dir.empty() ? std::string() : rep.dir + "/solutions.out";
    std::ifstream in(path);
    if (!in.good()) {
        rep.error = "No solutions.out in " + rep.dir;
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || (line[0] < '0' || line[0] > '9')) continue;   // the header starts with "rank"
        std::vector<double> row;
        std::stringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, ',') && row.size() < 7) row.push_back(atof(cell.c_str()));
        if (row.size() >= 4) rep.rows.push_back(row);   // rank, cc, Fisher z, rel z, then x, y, z when present
    }
    if (rep.rows.empty()) {
        rep.error = "No solutions found in " + path;
        return;
    }
    rep.loaded = true;
}

struct Haddock : viamd::EventHandler {
    ApplicationState* app_state = nullptr;
    std::vector<Restraint> restraints;
    std::string path;
    // The model files (PDB) loaded from the Molecules section. When any are loaded, the clashes, restraints and scans use
    // them, and the system shown is not used for analysis.
    std::vector<FolderPart> files;
    std::string files_input;    // the path text typed in the Model files box
    std::string files_text;     // the path text the files were loaded from
    std::string files_status;   // why the last loading of the files failed
    DisvisReport disvis;
    PowerfitReport powerfit;
    // Buffered workspace inputs, applied once the files are loaded (ViamdDeserializeEnd)
    bool ws_pending = false;
    std::string ws_files, ws_restraints, ws_cryo, ws_disvis, ws_powerfit, ws_fixed, ws_moving;
    std::string ws_folder;      // the folder of the workspace being loaded, to resolve relative paths against
    float ws_resolution = 9.0f;

    // Splits a ';'-separated path list, trimming each entry
    static std::vector<std::string> split_paths(const std::string& text) {
        std::vector<std::string> out;
        std::stringstream ss(text);
        std::string item;
        while (std::getline(ss, item, ';')) {
            const size_t b = item.find_first_not_of(" \t");
            if (b != std::string::npos) out.push_back(item.substr(b, item.find_last_not_of(" \t") - b + 1));
        }
        return out;
    }

    // A path written to the workspace, relative to the workspace file so the saved session is portable
    static std::string ws_relative(md_str_t ws_file, const std::string& p) {
        if (p.empty()) return p;
        md_temp_scope_t temp = md_temp_begin();
        MD_DEFER { md_temp_end(temp); };
        md_str_t rel = md_path_make_relative(ws_file, md_str_from_cstr(p.c_str()), md_temp_allocator(temp));
        return md_str_empty(rel) ? p : std::string(rel.ptr, rel.len);
    }

    // A path read from the workspace, resolved against the workspace folder (absolute paths are kept)
    std::string ws_resolve(const std::string& p) const {
        if (p.empty() || ws_folder.empty()) return p;
        const md_str_t ps = md_str_from_cstr(p.c_str());
        if (md_path_is_absolute(ps)) return p;
        md_temp_scope_t temp = md_temp_begin();
        MD_DEFER { md_temp_end(temp); };
        md_str_writer_t joined = md_str_writer_create(md_temp_allocator(temp));
        joined += md_str_from_cstr(ws_folder.c_str());
        joined += ps;
        const md_str_t canonical = md_path_make_canonical(md_str_writer_to_str(&joined), md_temp_allocator(temp));
        return md_str_empty(canonical) ? p : std::string(canonical.ptr, canonical.len);
    }
    bool show_setup = false;
    bool show_analysis = false;
    bool show_lines = true;
    int hovered = -1;           // restraint under the mouse in the table, -1 when none
    int hovered_clash = -1;     // clash under the mouse in the table, -1 when none
    int hovered_solution = -1;  // PowerFit solution under the mouse, -1 when none
    int pinned_rank = -1;       // the PowerFit solution pinned by a click, emphasized and centred (option A)
    int hovered_model = -1;     // the model highlighted in both the table and the scatter (set last frame)
    int pending_hover_table = -1;  // the model row under the mouse this frame
    int pending_hover_plot = -1;   // the model scatter point under the mouse this frame
    bool show_powerfit_coms = false; // overlay every PowerFit solution's centre of mass, coloured by cross-correlation
    bool highlighting = false;  // the shared highlight mask is set by this component

    // The two molecules of a system, by chain id. The moving chains are fitted and checked for clashes against the
    // fixed chains. With no moving chains, the fit moves the whole model and the clashes are every atom outside the fixed chains.
    std::string fixed_chains = "A";
    std::string moving_chains;
    std::vector<Clash> clashes;
    struct ModelScore {
        size_t clashes = 0;     // clashes between the fixed and the moving chains
        int satisfied = 0;      // restraints whose distance lies in their range
        bool done = false;      // the clashes and restraints were counted
        double score = 0.0;     // the map correlation score
        size_t points = 0;      // the atoms scored against the map
        bool scored = false;    // the map correlation was computed
    };
    std::vector<ModelScore> model_scores;  // one per model (model file, or frame), filled by Analyze all
    std::string scan_status;
    bool clashes_searched = false;
    int64_t clash_frame = -1;   // the frame the clashes were found on
    std::string clash_status;   // why the last search found no clashes to report
    std::string clash_key;      // the inputs (frame + chains) the clashes were computed for; recomputed when it changes
    bool show_clashes = true;
    int clash_revision = 0;     // the clash lines re-make the scene
    std::string fit_status;     // why the last local fit was not applied

    // Map fit. The map is read from its file, since the loaded copy is kept transposed for display.
    md_mrc_t fit_map = {};
    std::string cryo_em_map_path;               // the cryo-EM map the fit reads, owned by Haddock
    std::string fit_map_path;                   // the file the cached copy was read from
    std::string map_status;                     // why the cryo-EM map could not be read
    float fit_resolution = 9.0f;
    int fit_revision = 0;                       // the moved coordinates re-make the scene
    std::vector<ModelFit> models;               // one per frame (a single model is one frame)
    int64_t fit_frame = -1;                     // the frame that the local fit was applied to
    bool fit_applied = false;                   // the local fit is on the model shown
    std::vector<md_float3_t> fit_original;      // the coordinates before the local fit, restored by Reset
    std::vector<std::string> ensemble_names;    // the name of each model of the ensemble at ensemble_names_path
    std::string ensemble_names_path;
    std::string score_status;                   // why the last scoring left a model unscored
    std::string superpose_status;               // why the last superposition left a model unaligned
    std::vector<ModelFit> file_fits;            // one per model file: the superposition onto the first file
    int shown_file = -1;                        // the model file shown in the viewport, opened by Show
    int pending_show = -1;                      // the model file Show has queued, until its system is loaded
    bool style_pending = false;                 // the system Show loaded gets the model style
    bool restore_view_pending = false;          // the camera is kept across a Show, so switching models does not jump
    bool pending_keepview = false;              // a queued load (e.g. a PowerFit fit) should keep the camera
    Camera saved_camera = {};                   // the camera when Show was clicked
    ViewTransform saved_target = {};            // the view target when Show was clicked
    // The models are shown as one multi-frame ensemble (models, or PowerFit fits), so switching is a frame change (smooth)
    enum EnsembleKind { Ensemble_None, Ensemble_Models, Ensemble_Powerfit };
    EnsembleKind ensemble_kind = Ensemble_None;
    EnsembleKind pending_ensemble_kind = Ensemble_None;
    bool ensemble_shown = false;
    bool pending_ensemble = false;              // the ensemble is queued, until its trajectory loads
    int pending_frame = -1;                     // the frame to show once the ensemble has loaded
    std::string ensemble_tmp_path;
    std::vector<std::string> powerfit_fits;     // the fit_N.pdb of the PowerFit folder, in rank order
    // The representations on screen when Show was clicked, recreated on the model Show loads
    struct StyleRep {
        RepresentationType type;
        ColorMapping color_mapping;
        md_float4_t base_color;
        std::string filter;
        bool enabled;
    };
    std::vector<StyleRep> style_template;

    Haddock() { viamd::event_system_register_handler(*this); }

    ~Haddock() {
        if (fit_map.val) md_mrc_free(&fit_map, md_get_heap_allocator());
        for (FolderPart& p : files) free_folder_part(p);
    }

    // Loads the model files named in the text: PDB files or folders of PDB files, separated by semicolons.
    // An empty text removes the files, so the system shown is analysed again.
    void load_files(const std::string& text) {
        for (FolderPart& p : files) free_folder_part(p);
        files.clear();
        file_fits.clear();
        model_scores.clear();
        shown_file = -1;
        ensemble_shown = false;
        files_text = text;
        files_status.clear();
        // A new model set invalidates the status of the analyses that ran on the previous one, so a stale message
        // (e.g. "Load at least two model files to superpose" from a single-file set) does not linger after a reload.
        superpose_status.clear();
        ensemble_status.clear();
        std::vector<std::string> paths;
        std::stringstream ss(text);
        std::string item;
        while (std::getline(ss, item, ';')) {
            const size_t b = item.find_first_not_of(" \t");
            if (b == std::string::npos) continue;
            paths.push_back(item.substr(b, item.find_last_not_of(" \t") - b + 1));
        }
        std::vector<std::string> pdbs;
        for (const std::string& p : paths) {
            std::error_code ec;
            if (std::filesystem::is_directory(p, ec)) {
                std::vector<std::string> in_dir;
                for (const auto& e : std::filesystem::directory_iterator(p, ec)) {
                    if (e.is_regular_file(ec) && e.path().extension() == ".pdb") in_dir.push_back(e.path().string());
                }
                std::sort(in_dir.begin(), in_dir.end());
                pdbs.insert(pdbs.end(), in_dir.begin(), in_dir.end());
            } else {
                pdbs.push_back(p);
            }
        }
        for (const std::string& p : pdbs) add_model_file(p);
        if (files.empty() && !paths.empty() && files_status.empty()) files_status = "No PDB files in " + text;
        clear_clashes();
        clear_scores();
    }

    // Reads one model file into files, inferring its system. False when it cannot be read.
    bool add_model_file(const std::string& p) {
        const std::filesystem::path fp(p);
        FolderPart part;
        part.name = fp.parent_path().filename().string() + "/" + fp.filename().string();
        part.path = p;
        md_allocator_i* alloc = md_arena_allocator_create(md_get_heap_allocator(), MD_MEGABYTES(16));
        part.sys = {.alloc = alloc};
        part.state = {.alloc = alloc};
        if (!md_pdb_system_init_from_file(&part.sys, &part.state, md_str_from_cstr(p.c_str()), NULL) ||
            !md_system_infer(&part.sys, &part.state, NULL)) {
            files_status = "Could not read " + p;
            MD_LOG_ERROR("Haddock: %s", files_status.c_str());
            free_folder_part(part);
            return false;
        }
        // The HADDOCK score, when the PDB carries a REMARK energy block (HADDOCK output models)
        md_temp_scope_t temp = md_temp_begin();
        const md_str_t pdb_text = md_load_textfile(md_str_from_cstr(p.c_str()), md_temp_allocator(temp));
        if (pdb_text.len) part.has_energy = md_haddock_energy_deserialize(&part.energy, pdb_text);
        md_temp_end(temp);
        files.push_back(part);
        return true;
    }

    // Queues a model file the way a dropped file is opened, so it becomes the system shown in the viewport
    void show_in_viewport(size_t i) {
        if (!app_state || i >= files.size() || file_queue_full(&app_state->file_queue)) return;
        pending_show = (int)i;
        // The camera is kept so switching from one model to another does not jump
        saved_camera = app_state->view.camera;
        saved_target = app_state->view.target;
        capture_style_template();
        file_queue_push(&app_state->file_queue, md_str_from_cstr(files[i].path.c_str()), FileFlags_None);
    }

    // Shows model i. The loaded files are shown as one multi-frame ensemble when they share topology, so switching is a
    // frame change (smooth, no reload, no camera jump). Mixed-topology files fall back to reloading each file.
    void show_model(size_t i) {
        if (!app_state || i >= files.size()) return;
        if (ensemble_shown && ensemble_kind == Ensemble_Models && run_num_frames(app_state) == files.size()) {
            select_frame(*app_state, (int64_t)i);
            shown_file = (int)i;
            return;
        }
        std::vector<std::string> paths;
        for (const FolderPart& f : files) paths.push_back(f.path);
        if (paths.size() >= 2 && build_ensemble(paths, (int)i, Ensemble_Models)) return;
        show_in_viewport(i);
    }

    // Pins a PowerFit solution (option A): the current scene is kept, the solution's centre of mass is emphasized and the
    // camera is centred on it. Loading the lone fitted molecule in the map is left for later (option B).
    void pin_powerfit_solution(size_t row) {
        if (row >= powerfit.rows.size() || powerfit.rows[row].size() < 7) return;
        pinned_rank = (int)powerfit.rows[row][0];
        const md_float3_t com = { (float)powerfit.rows[row][4], (float)powerfit.rows[row][5], (float)powerfit.rows[row][6] };
        focus_on(com);
    }

    // Centres the camera on a world point, keeping the current orientation and distance (like a double-click focus)
    void focus_on(md_float3_t p) {
        if (!app_state) return;
        ViewTransform t = app_state->view.target;
        t.orientation = app_state->view.camera.orientation;
        t.distance = app_state->view.camera.distance;
        t.position = p + t.orientation * md_float3_set(0, 0, t.distance);
        app_state->view.target = t;
    }

    // Writes the given PDBs as a temp ensemble and queues it as a trajectory; the frame to show is kept until it loads
    bool build_ensemble(const std::vector<std::string>& paths, int frame, EnsembleKind kind) {
        if (!app_state || paths.size() < 2 || file_queue_full(&app_state->file_queue)) return false;
        static uint64_t counter = 0;
        const std::string name = "haddock_ensemble_" +
            std::to_string((uint64_t)std::chrono::steady_clock::now().time_since_epoch().count()) + "_" + std::to_string(counter++) + ".pdb";
        const std::string out = (std::filesystem::temp_directory_path() / name).string();
        md_temp_scope_t temp = md_temp_begin();
        md_str_t* in = md_temp_alloc_array(temp, md_str_t, paths.size());
        for (size_t k = 0; k < paths.size(); ++k) in[k] = md_str_from_cstr(paths[k].c_str());
        const size_t n = md_haddock_ensemble_write(md_str_from_cstr(out.c_str()), in, paths.size(), md_get_heap_allocator());
        md_temp_end(temp);
        if (n != paths.size()) {
            std::error_code ec;
            std::filesystem::remove(out, ec);
            return false;   // mixed topology: the caller falls back to a single-file load
        }
        ensemble_tmp_path = out;
        pending_ensemble = true;
        pending_ensemble_kind = kind;
        pending_frame = frame;
        // Keep the camera and the current representations when a structure is already shown, so building the ensemble
        // does not reorient the view or reset the representation. The first load (from the placeholder) still frames it.
        saved_camera = app_state->view.camera;
        saved_target = app_state->view.target;
        pending_keepview = !app_state->system_is_default;
        capture_style_template();
        file_queue_push(&app_state->file_queue, md_str_from_cstr(out.c_str()), (FileFlags)(FileFlags_Temporary | FileFlags_DisableCacheWrite));
        return true;
    }

    // Loads a PDB into the viewport (e.g. a single PowerFit fit) keeping the camera and the current representations
    void show_pdb_keepview(const std::string& p) {
        if (!app_state || p.empty() || file_queue_full(&app_state->file_queue)) return;
        saved_camera = app_state->view.camera;
        saved_target = app_state->view.target;
        pending_keepview = true;
        capture_style_template();
        file_queue_push(&app_state->file_queue, md_str_from_cstr(p.c_str()), FileFlags_None);
    }

    // Captures the molecular representations on screen, so a following load can recreate them (not the map representation)
    void capture_style_template() {
        style_template.clear();
        if (app_state->system_is_default) return;
        for (size_t r = 0; r < md_array_size(app_state->representation.reps); ++r) {
            const Representation& rep = app_state->representation.reps[r];
            if (rep.type == RepresentationType::ElectronicStructure) continue;
            style_template.push_back({rep.type, rep.color_mapping, rep.base_color, rep.filter.text, rep.enabled});
        }
    }

    // Puts the camera back where it was before Show, so switching between models does not jump. Runs after the loader's
    // reset_view (on the frame tick), so the restored view is the one that stays.
    void restore_view() {
        if (!app_state || !restore_view_pending) return;
        restore_view_pending = false;
        app_state->view.camera = saved_camera;
        app_state->view.target = saved_target;
    }

    // The model shown by Show gets the representations that were on screen when Show was clicked. Without any, it is a Cartoon coloured by chain ID.
    void apply_model_style() {
        if (!app_state || !style_pending) return;
        style_pending = false;
        if (app_state->system_is_default || app_state->mold.sys.atom.count == 0) return;

        remove_system_representations(app_state);
        // The inferred defaults of the system are not wanted over the model style
        app_state->mold.inference.defaults_pending = false;
        if (style_template.empty()) {
            create_representation(app_state, RepresentationType::Cartoon, ColorMapping::ChainId);
        } else {
            for (const StyleRep& s : style_template) {
                Representation* rep = create_representation(app_state, s.type, s.color_mapping, md_str_from_cstr(s.filter.c_str()));
                rep->base_color = s.base_color;
                rep->enabled = s.enabled;
            }
        }
        recompute_atom_visibility_mask(app_state);
    }


    // The DisVis accessible-space map of the loaded results folder, if any
    std::string disvis_map_path() const {
        if (disvis.dir.empty()) return std::string();
        return disvis.dir + "/accessible_interaction_space.mrc";
    }

    // Opens a map file in the viewport, the way a dropped map is opened
    void show_map(const std::string& path) {
        if (!app_state || path.empty() || file_queue_full(&app_state->file_queue)) return;
        file_queue_push(&app_state->file_queue, md_str_from_cstr(path.c_str()), FileFlags_None);
    }

    // Adopts a map dropped into the viewport as the cryo-EM map, unless it is the DisVis map
    void adopt_dropped_map(const ApplicationState& state) {
        if (!cryo_em_map_path.empty()) return;
        const std::string disvis_map = disvis_map_path();
        for (const MapVolume& m : state.maps) {
            if (!disvis_map.empty() && std::filesystem::path(m.path).lexically_normal() == std::filesystem::path(disvis_map).lexically_normal()) continue;
            cryo_em_map_path = m.path;
            return;
        }
    }

    // Reads the cryo-EM map from its file into fit_map. The map is read directly, so it works whatever is shown.
    bool load_fit_map() {
        if (cryo_em_map_path.empty()) {
            if (fit_map.val) md_mrc_free(&fit_map, md_get_heap_allocator());
            fit_map = {};
            fit_map_path.clear();
            return false;
        }
        if (fit_map.val && fit_map_path == cryo_em_map_path) return true;
        if (fit_map.val) md_mrc_free(&fit_map, md_get_heap_allocator());
        fit_map = {};
        fit_map_path.clear();
        clear_scores();
        if (!md_mrc_file_load(&fit_map, md_str_from_cstr(cryo_em_map_path.c_str()), md_get_heap_allocator())) {
            map_status = "Could not read the cryo-EM map " + cryo_em_map_path;
            MD_LOG_ERROR("Haddock: %s", map_status.c_str());
            return false;
        }
        map_status.clear();
        fit_map_path = cryo_em_map_path;
        return true;
    }

    // The simulated-map score of the heavy atoms (hydrogens are left out, as in ChimeraX fitmap)
    bool score_coords(const md_system_t& sys, const md_float3_t* src, size_t count, double* score, size_t* points) {
        std::vector<md_float3_t> xyz;
        std::vector<float> weight;
        for (size_t i = 0; i < count; ++i) {
            const md_atomic_number_t z = md_atom_atomic_number(&sys.atom, i);
            if (z == 1) continue;
            xyz.push_back(src[i]);
            weight.push_back((float)z);
        }
        if (xyz.empty()) return false;
        *score = md_map_fit_score_sim(&fit_map, (const float*)xyz.data(), weight.data(), xyz.size(), fit_resolution,
                                      MD_MAP_FIT_ENVELOPE_LEVEL, points, md_get_heap_allocator());
        return true;
    }

    int64_t current_frame(const ApplicationState& state) const {
        const int64_t n = (int64_t)run_num_frames(&state);
        if (n <= 1) return 0;
        return MD_CLAMP((int64_t)(state.animation.frame + 0.5), (int64_t)0, n - 1);
    }

    void sync_models(const ApplicationState& state) {
        const size_t n = std::max<size_t>(run_num_frames(&state), 1);
        if (models.size() != n) models.assign(n, ModelFit{});
    }

    // Sizes model_scores to the models: one per model file, or one per frame of the system shown
    void ensure_model_scores(const ApplicationState& state) {
        const size_t n = files.empty() ? std::max<size_t>(run_num_frames(&state), 1) : files.size();
        if (model_scores.size() != n) model_scores.assign(n, ModelScore{});
    }

    void clear_scores() {
        for (ModelFit& m : models) {
            m.scored = false;
            m.score = 0.0;
            m.points = 0;
        }
        for (ModelScore& s : model_scores) {
            s.scored = false;
            s.score = 0.0;
            s.points = 0;
        }
    }

    std::string model_name(const ApplicationState& state, size_t i) const {
        if (!ensemble_names_path.empty() && ensemble_names_path == state.ensemble_path && i < ensemble_names.size()) {
            return ensemble_names[i];
        }
        return "Frame " + std::to_string(i + 1);
    }

    // The coordinates of a frame as they are in the file. A single model has no trajectory, so it is the model on screen.
    bool frame_coords(ApplicationState& state, int64_t frame, std::vector<md_float3_t>& out) {
        if (run_num_frames(&state) <= 1) {
            out.assign(state.mold.state.xyz, state.mold.state.xyz + state.mold.state.num_atoms);
            return true;
        }
        // The positions are only read into a state that already holds them
        md_system_state_t fs = { .alloc = md_get_heap_allocator() };
        if (!md_system_state_init(&fs, state.mold.state.num_atoms)) return false;
        const bool ok = extract_frame(&state, frame, &fs);
        if (ok) out.assign(fs.xyz, fs.xyz + fs.num_atoms);
        md_system_state_free(&fs);
        return ok;
    }

    // Scores a model without moving it
    void score_frame(ApplicationState& state, int64_t frame) {
        ModelFit& m = models[frame];
        std::vector<md_float3_t> xyz;
        if (!frame_coords(state, frame, xyz)) {
            score_status = "Could not read the coordinates of " + model_name(state, frame);
            MD_LOG_ERROR("Haddock: %s", score_status.c_str());
            return;
        }
        m.scored = score_coords(state.mold.sys, xyz.data(), xyz.size(), &m.score, &m.points);
        if (!m.scored) {
            score_status = "No heavy atoms to score in " + model_name(state, frame);
            MD_LOG_ERROR("Haddock: %s", score_status.c_str());
        }
        if ((size_t)frame < model_scores.size()) {
            model_scores[frame].scored = m.scored;
            model_scores[frame].score = m.score;
            model_scores[frame].points = m.points;
        }
    }

    // Scores every model against the cryo-EM map: each model file on its own atoms, or every frame of the system shown
    void score_all(ApplicationState& state) {
        score_status.clear();
        if (!load_fit_map()) {
            score_status = cryo_em_map_path.empty() ? "Load a cryo-EM map to score the models." : map_status;
            return;
        }
        ensure_model_scores(state);
        if (!files.empty()) {
            for (size_t f = 0; f < files.size(); ++f) {
                ModelScore& s = model_scores[f];
                s.scored = score_coords(files[f].sys, files[f].state.xyz, files[f].sys.atom.count, &s.score, &s.points);
                if (!s.scored) {
                    score_status = "No heavy atoms to score in " + files[f].name;
                    MD_LOG_ERROR("Haddock: %s", score_status.c_str());
                }
            }
            return;
        }
        sync_models(state);
        for (int64_t f = 0; f < (int64_t)models.size(); ++f) score_frame(state, f);
    }

    // Runs the whole analysis of every model: clashes, satisfied restraints, map correlation and the RMSD superposition
    void analyze_all(ApplicationState& state) {
        scan_models(state);
        score_all(state);
        superpose_all(state);
    }

    // Writes the system shown as it is on screen. After Apply best fit the fitted chains are in the map frame. The map is not written.
    void save_fitted_pdb(const ApplicationState& state) {
        char path_buf[2048] = {};
        if (!application::file_dialog(path_buf, sizeof(path_buf), application::FileDialogFlag_Save, MD_STR_LIT("pdb"))) return;
        const md_str_t path = md_str_from_cstrn(path_buf, sizeof(path_buf));
        md_file_t file = {};
        if (!md_file_open(&file, path, MD_FILE_WRITE | MD_FILE_CREATE | MD_FILE_TRUNCATE)) {
            MD_LOG_ERROR("Haddock: could not open '" MD_STR_FMT "' for writing", MD_STR_ARG(path));
            return;
        }
        MD_DEFER { md_file_close(&file); };

        const size_t n = state.mold.state.num_atoms;
        std::vector<int32_t> indices(n);
        for (size_t i = 0; i < n; ++i) indices[i] = (int32_t)i;
        md_pdb_system_write_state_to_file(file, &state.mold.sys, &state.mold.state, indices.data(), n, 0);
        MD_LOG_INFO("Haddock: saved %zu atoms to '" MD_STR_FMT "'", n, MD_STR_ARG(path));
    }

    void apply_fit(ApplicationState& state, int64_t frame) {
        const size_t n = state.mold.state.num_atoms;
        fit_status.clear();
        if (n == 0 || frame < 0 || (size_t)frame >= models.size()) return;

        // The cryo-EM map must be read before fitting. Without it the fit would move nothing.
        if (!load_fit_map()) {
            fit_status = cryo_em_map_path.empty() ? "Load a cryo-EM map before fitting." : map_status;
            MD_LOG_ERROR("Haddock: %s", fit_status.c_str());
            return;
        }

        // Only the moving atoms are fitted; the other chains keep their place. Fits of different bodies accumulate, so
        // you can fit one molecule, then switch the moving chains and fit the other, and both stay (Reset restores all).
        const std::vector<uint32_t> group = moving_atoms(state.mold.sys, true);
        if (group.empty()) {
            fit_status = "No atoms in the moving chains";
            MD_LOG_ERROR("Haddock: %s", fit_status.c_str());
            return;
        }
        const size_t ng = group.size();

        // Capture the pre-fit coordinates once (the first fit), so Reset restores everything and later fits stack on top
        if (!fit_applied || fit_original.size() != n) fit_original.assign(state.mold.state.xyz, state.mold.state.xyz + n);
        std::vector<md_float3_t> sub(ng), moved(ng);
        for (size_t k = 0; k < ng; ++k) sub[k] = state.mold.state.xyz[group[k]];
        ModelFit& m = models[frame];
        m.fit_mean = md_map_fit_atoms_in_map(&fit_map, (const float*)sub.data(), (float*)moved.data(), ng);
        if (!md_map_fit_rigid_transform((const float*)sub.data(), (const float*)moved.data(), ng, m.transform)) {
            if (!fit_applied) fit_original.clear();
            return;
        }

        double c0[3] = {0, 0, 0}, c1[3] = {0, 0, 0};
        for (size_t i = 0; i < ng; ++i) {
            const float* a = &sub[i].x;
            const float* b = &moved[i].x;
            for (int k = 0; k < 3; ++k) {
                c0[k] += a[k];
                c1[k] += b[k];
            }
        }
        double d2 = 0.0;
        for (int k = 0; k < 3; ++k) {
            const double dk = (c1[k] - c0[k]) / (double)ng;
            d2 += dk * dk;
        }
        m.fit_shift = std::sqrt(d2);
        m.fitted = true;

        for (size_t k = 0; k < ng; ++k) state.mold.state.xyz[group[k]] = moved[k];
        clear_clashes();
        fit_frame = frame;
        fit_applied = true;
        ++fit_revision;
        state.mold.dirty_gpu_buffers |= MolBit_DirtyPosition | MolBit_ClearVelocity;
    }

    void reset_fit(ApplicationState& state) {
        if (fit_applied && fit_original.size() == state.mold.state.num_atoms) {
            std::copy(fit_original.begin(), fit_original.end(), state.mold.state.xyz);
            ++fit_revision;
            state.mold.dirty_gpu_buffers |= MolBit_DirtyPosition | MolBit_ClearVelocity;
        }
        fit_original.clear();
        fit_applied = false;
        clear_clashes();
    }

    // The atoms of the moving chains. With none given, the fit uses the whole model and the clashes every atom outside the fixed chains.
    std::vector<uint32_t> moving_atoms(const md_system_t& sys, bool fit) const {
        const std::vector<std::string> mov = parse_chains(moving_chains);
        if (!mov.empty()) return atoms_in_chains(sys, mov, false);
        if (fit) return atoms_in_chains(sys, {}, true);
        return atoms_in_chains(sys, parse_chains(fixed_chains), true);
    }

    void clear_clashes() {
        clashes.clear();
        clashes_searched = false;
        clash_key.clear();
        ++clash_revision;
    }

        // The atoms of the fixed chains, or of the moving chains (every atom outside the fixed chains when none are given),
    // from the model files when any are loaded, else from the system shown on the given coordinates
    std::vector<Member> gather_group(const ApplicationState& state, const md_float3_t* sys_xyz, bool fixed_group, int only_part = -1, bool force_system = false) const {
        const std::vector<std::string> fixed = parse_chains(fixed_chains);
        const std::vector<std::string> mov = parse_chains(moving_chains);
        const bool use_fixed = fixed_group || mov.empty();
        const std::vector<std::string>& chains = use_fixed ? fixed : mov;
        const bool outside = !fixed_group && mov.empty();
        std::vector<Member> out;
        // The shown system is used when there are no model files, or when the caller asks for it (the shown ensemble frame)
        const bool sys_part = files.empty() || force_system;
        const size_t num_parts = sys_part ? 1 : files.size();
        const size_t p_begin = only_part >= 0 ? (size_t)only_part : 0;
        const size_t p_end = only_part >= 0 ? (size_t)only_part + 1 : num_parts;
        for (size_t p = p_begin; p < p_end && p < num_parts; ++p) {
            const md_system_t& sys = sys_part ? state.mold.sys : files[p].sys;
            const md_float3_t* xyz = sys_part ? sys_xyz : files[p].state.xyz;
            if (!xyz) continue;
            const size_t n = sys_part ? state.mold.sys.atom.count : files[p].sys.atom.count;
            for (size_t i = 0; i < n; ++i) {
                if (in_chains(sys, i, chains) == outside) continue;
                out.push_back({{sys_part ? -1 : (int)p, (int32_t)i}, xyz[i], md_atom_atomic_number(&sys.atom, i)});
            }
        }
        return out;
    }

    // The clash pairs between the fixed and the moving atoms (indices into each list)
    static std::vector<md_clash_pair_t> clash_pairs(const std::vector<Member>& fixed, const std::vector<Member>& mov) {
        std::vector<float> xa(3 * fixed.size()), xb(3 * mov.size());
        std::vector<md_atomic_number_t> za(fixed.size()), zb(mov.size());
        for (size_t k = 0; k < fixed.size(); ++k) {
            xa[3 * k + 0] = fixed[k].p.x;
            xa[3 * k + 1] = fixed[k].p.y;
            xa[3 * k + 2] = fixed[k].p.z;
            za[k] = fixed[k].z;
        }
        for (size_t k = 0; k < mov.size(); ++k) {
            xb[3 * k + 0] = mov[k].p.x;
            xb[3 * k + 1] = mov[k].p.y;
            xb[3 * k + 2] = mov[k].p.z;
            zb[k] = mov[k].z;
        }

        // ChimeraX clashes: an overlap of 0.6 Å, with 0.4 Å allowed for hydrogen bonds
        std::vector<float> ra(fixed.size()), rb(mov.size());
        md_clash_radii(xa.data(), za.data(), fixed.size(), ra.data());
        md_clash_radii(xb.data(), zb.data(), mov.size(), rb.data());
        const size_t num = md_clash_find(xa.data(), za.data(), ra.data(), fixed.size(), xb.data(), zb.data(), rb.data(), mov.size(), 0.6f, 0.4f, nullptr, 0);
        std::vector<md_clash_pair_t> pairs(num);
        md_clash_find(xa.data(), za.data(), ra.data(), fixed.size(), xb.data(), zb.data(), rb.data(), mov.size(), 0.6f, 0.4f, pairs.data(), num);
        return pairs;
    }

    // Counts the clashes and the satisfied restraints of every model, without moving them. With model files loaded,
    // each file is one model, scored on its own atoms.
    void scan_models(ApplicationState& state) {
        scan_status.clear();
        ensure_model_scores(state);
        if (!files.empty()) {
            for (size_t f = 0; f < files.size(); ++f) {
                const FolderPart& part = files[f];
                ModelScore& s = model_scores[f];
                s.clashes = 0;
                s.satisfied = 0;
                s.done = false;
                const std::vector<Member> fixed = gather_group(state, nullptr, true, (int)f);
                const std::vector<Member> mov = gather_group(state, nullptr, false, (int)f);
                if (fixed.empty() || mov.empty()) {
                    scan_status = fixed.empty() ? "No atoms in the fixed chains" : "No atoms in the moving chains";
                    continue;
                }
                s.clashes = clash_pairs(fixed, mov).size();
                for (const Restraint& rs : restraints) {
                    const int ia = find_atom(part.sys, rs.r.atom[0]);
                    const int ib = find_atom(part.sys, rs.r.atom[1]);
                    if (ia < 0 || ib < 0) continue;
                    const md_float3_t a = part.state.xyz[ia];
                    const md_float3_t b = part.state.xyz[ib];
                    const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
                    if (restraint_satisfied(rs, std::sqrt(dx * dx + dy * dy + dz * dz))) ++s.satisfied;
                }
                s.done = true;
            }
            return;
        }

        sync_models(state);
        for (int64_t f = 0; f < (int64_t)models.size(); ++f) {
            ModelScore& s = model_scores[f];
            s.clashes = 0;
            s.satisfied = 0;
            s.done = false;
            std::vector<md_float3_t> xyz;
            if (!frame_coords(state, f, xyz)) {
                scan_status = "Could not read the coordinates of " + model_name(state, f);
                continue;
            }
            const md_float3_t* sys_xyz = xyz.data();
            const std::vector<Member> fixed = gather_group(state, sys_xyz, true);
            const std::vector<Member> mov = gather_group(state, sys_xyz, false);
            if (fixed.empty() || mov.empty()) {
                scan_status = fixed.empty() ? "No atoms in the fixed chains" : "No atoms in the moving chains";
                continue;
            }
            s.clashes = clash_pairs(fixed, mov).size();
            for (const Restraint& rs : restraints) {
                float dist = 0.0f;
                if (restraint_distance(sys_xyz, state.mold.state.num_atoms, files, rs, &dist) && restraint_satisfied(rs, dist)) ++s.satisfied;
            }
            s.done = true;
        }
    }


    // Finds the clashes between the fixed and the moving chains on the coordinates shown. With an ensemble shown, that is
    // the current frame (the shown model); with model files but no ensemble, the files combined.
    void find_clashes(ApplicationState& state) {
        clear_clashes();
        clash_status.clear();
        const std::vector<Member> fixed = gather_group(state, state.mold.state.xyz, true, -1, ensemble_shown);
        const std::vector<Member> mov = gather_group(state, state.mold.state.xyz, false, -1, ensemble_shown);
        if (fixed.empty() || mov.empty()) {
            // An expected configuration (e.g. a single-chain system, or a transient while a workspace loads), not a
            // hard error: shown in the Clashes tab via clash_status, and logged only at debug so it is not a toast card.
            clash_status = fixed.empty() ? "No atoms in the fixed chains" : "No atoms in the moving chains";
            MD_LOG_DEBUG("Haddock: %s", clash_status.c_str());
            return;
        }

        const std::vector<md_clash_pair_t> pairs = clash_pairs(fixed, mov);
        clashes.reserve(pairs.size());
        for (const md_clash_pair_t& p : pairs) clashes.push_back({{fixed[p.a].ref, mov[p.b].ref}, p.overlap});
        std::sort(clashes.begin(), clashes.end(), [](const Clash& x, const Clash& y) { return x.overlap > y.overlap; });
        clash_frame = current_frame(state);
        clashes_searched = true;
        ++clash_revision;
    }

    // Keeps the clashes current for the shown model: recomputes them whenever the frame or the chains change, so they are
    // always shown without a manual search.
    void ensure_clashes(ApplicationState& state) {
        if (state.mold.state.num_atoms == 0) {
            if (clashes_searched || !clashes.empty()) clear_clashes();
            clash_key.clear();
            return;
        }
        const std::string key = std::to_string(current_frame(state)) + "|" + fixed_chains + "|" + moving_chains;
        if (key == clash_key) return;
        find_clashes(state);   // clears clash_key via clear_clashes, so set it afterwards
        clash_key = key;
    }

    // Superposes every model file onto the first one on the chain A C-alpha atoms. The files are read directly, so
    // any of them can be superposed while another is shown. Only the RMSD is stored, the models are not moved until one is aligned.
    void superpose_files() {
        file_fits.assign(files.size(), ModelFit{});
        if (files.size() < 2) {
            superpose_status = "Load at least two model files to superpose";
            return;
        }
        const std::vector<size_t> ref_idx = chain_a_calpha(files[0].sys);
        if (ref_idx.size() < 3) {
            superpose_status = "Need at least 3 C-alpha atoms in chain A of " + files[0].name;
            MD_LOG_ERROR("Haddock: %s", superpose_status.c_str());
            return;
        }
        std::vector<md_float3_t> ref(ref_idx.size());
        for (size_t k = 0; k < ref_idx.size(); ++k) ref[k] = files[0].state.xyz[ref_idx[k]];

        for (size_t p = 0; p < files.size(); ++p) {
            const std::vector<size_t> ca = chain_a_calpha(files[p].sys);
            if (ca.size() != ref_idx.size()) {
                superpose_status = files[p].name + " has " + std::to_string(ca.size()) + " chain A C-alpha atoms, " + std::to_string(ref_idx.size()) + " in " + files[0].name;
                MD_LOG_ERROR("Haddock: %s", superpose_status.c_str());
                continue;
            }
            std::vector<md_float3_t> mov(ca.size());
            for (size_t k = 0; k < ca.size(); ++k) mov[k] = files[p].state.xyz[ca[k]];

            ModelFit& m = file_fits[p];
            if (!md_map_fit_rigid_transform((const float*)mov.data(), (const float*)ref.data(), ca.size(), m.superpose)) continue;

            double sum = 0.0;
            for (size_t k = 0; k < ca.size(); ++k) {
                const md_float3_t q = transform_point(m.superpose, mov[k]);
                const double dx = q.x - ref[k].x, dy = q.y - ref[k].y, dz = q.z - ref[k].z;
                sum += dx * dx + dy * dy + dz * dz;
            }
            m.rmsd = std::sqrt(sum / (double)ca.size());
            m.aligned = true;
        }
    }

    // Superposes every model onto the first one on the chain A C-alpha atoms (matchmaker #2 to #1/A). Only the
    // RMSD is stored, the models are not moved until one is aligned.
    void superpose_all(ApplicationState& state) {
        if (fit_applied) reset_fit(state);
        superpose_status.clear();
        if (!files.empty()) {
            superpose_files();
            return;
        }
        sync_models(state);
        for (ModelFit& m : models) m.aligned = false;

        const std::vector<size_t> ca = chain_a_calpha(state.mold.sys);
        if (ca.size() < 3) {
            superpose_status = "Need at least 3 C-alpha atoms in chain A";
            MD_LOG_ERROR("Haddock: %s", superpose_status.c_str());
            return;
        }

        std::vector<md_float3_t> ref_xyz;
        if (!frame_coords(state, 0, ref_xyz)) {
            superpose_status = "Could not read the coordinates of " + model_name(state, 0);
            MD_LOG_ERROR("Haddock: %s", superpose_status.c_str());
            return;
        }
        std::vector<md_float3_t> ref(ca.size());
        for (size_t k = 0; k < ca.size(); ++k) ref[k] = ref_xyz[ca[k]];

        for (size_t f = 0; f < models.size(); ++f) {
            std::vector<md_float3_t> xyz;
            if (!frame_coords(state, (int64_t)f, xyz)) {
                superpose_status = "Could not read the coordinates of " + model_name(state, f);
                MD_LOG_ERROR("Haddock: %s", superpose_status.c_str());
                continue;
            }
            std::vector<md_float3_t> mov(ca.size());
            for (size_t k = 0; k < ca.size(); ++k) mov[k] = xyz[ca[k]];

            ModelFit& m = models[f];
            if (!md_map_fit_rigid_transform((const float*)mov.data(), (const float*)ref.data(), ca.size(), m.superpose)) continue;

            double sum = 0.0;
            for (size_t k = 0; k < ca.size(); ++k) {
                const md_float3_t p = transform_point(m.superpose, mov[k]);
                const double dx = p.x - ref[k].x, dy = p.y - ref[k].y, dz = p.z - ref[k].z;
                sum += dx * dx + dy * dy + dz * dz;
            }
            m.rmsd = std::sqrt(sum / (double)ca.size());
            m.aligned = true;
        }
    }

    // The superposition that Align applies: the model file shown (opened by Show), else the frame on screen
    const ModelFit* align_target(const ApplicationState& state) const {
        if (!files.empty()) {
            if (shown_file < 0 || (size_t)shown_file >= file_fits.size() || !file_fits[shown_file].aligned) return nullptr;
            return &file_fits[shown_file];
        }
        const int64_t frame = current_frame(state);
        if (frame < 0 || (size_t)frame >= models.size() || !models[frame].aligned) return nullptr;
        return &models[frame];
    }

    // Moves the model shown onto the first model by the superposition of the model shown
    void align_model(ApplicationState& state) {
        const ModelFit* target = align_target(state);
        if (!target) return;
        float transform[12];
        memcpy(transform, target->superpose, sizeof(transform));
        const int64_t frame = current_frame(state);
        reset_fit(state);
        const size_t n = state.mold.state.num_atoms;
        fit_original.assign(state.mold.state.xyz, state.mold.state.xyz + n);
        for (size_t i = 0; i < n; ++i) state.mold.state.xyz[i] = transform_point(transform, fit_original[i]);
        clear_clashes();
        fit_frame = frame;
        fit_applied = true;
        ++fit_revision;
        state.mold.dirty_gpu_buffers |= MolBit_DirtyPosition | MolBit_ClearVelocity;
    }


    void drop_fit_on_frame_change(int64_t frame) {
        if (fit_applied && frame != fit_frame) {
            fit_original.clear();
            fit_applied = false;
        }
    }

    // Shows a model of the ensemble. Playback stops, so the model stays where it is picked.
    void select_frame(ApplicationState& state, int64_t frame) {
        state.animation.mode = PlaybackMode::Stopped;
        state.animation.frame = (double)frame;
    }

    // The atom in the model files when any are loaded (the first file holding it), else in the system shown
    // The atom a restraint refers to. When the models are shown as an ensemble the shown frame is the system, so resolve
    // against it (the distances then track the shown model). Otherwise, with model files loaded, resolve to the files.
    AtomRef find_ref(const ApplicationState& state, const md_haddock_atom_sel_t& sel) const {
        if (files.empty() || ensemble_shown) return {-1, find_atom(state.mold.sys, sel)};
        for (size_t p = 0; p < files.size(); ++p) {
            const int idx = find_atom(files[p].sys, sel);
            if (idx >= 0) return {(int)p, idx};
        }
        return {};
    }

    void resolve(const ApplicationState& state) {
        for (Restraint& rs : restraints) {
            for (int s = 0; s < 2; ++s) rs.atom[s] = find_ref(state, rs.r.atom[s]);
        }
    }

    void unresolve() {
        for (Restraint& rs : restraints) {
            rs.atom[0] = rs.atom[1] = AtomRef{};
        }
    }

    bool rdist(const Restraint& rs, float* out) const {
        return restraint_distance(app_state->mold.state.xyz, app_state->mold.state.num_atoms, files, rs, out);
    }

    void process_events(const viamd::Event* events, size_t num_events) final {
        for (size_t i = 0; i < num_events; ++i) {
            const viamd::Event& e = events[i];
            switch (e.type) {
            case viamd::EventType_ViamdInitialize:
                app_state = (ApplicationState*)e.payload;
                workspace_register_window("Haddock Setup", &show_setup);
                workspace_register_window("Haddock Analysis", &show_analysis);
                break;
            case viamd::EventType_ViamdSystemInit:
                shown_file = pending_show;
                style_pending = (pending_show >= 0) || pending_keepview;
                restore_view_pending = (pending_show >= 0) || pending_keepview;
                pending_keepview = false;
                pending_show = -1;
                if (pending_ensemble) {
                    // The ensemble just loaded as a trajectory: keep it as the shown set and jump to the chosen model
                    ensemble_shown = true;
                    ensemble_kind = pending_ensemble_kind;
                    style_pending = true;
                    shown_file = (pending_ensemble_kind == Ensemble_Models) ? pending_frame : -1;
                    if (app_state && pending_frame >= 0) {
                        app_state->animation.mode = PlaybackMode::Stopped;
                        app_state->animation.frame = (double)pending_frame;
                    }
                    pending_ensemble = false;
                    pending_ensemble_kind = Ensemble_None;
                    pending_frame = -1;
                } else {
                    ensemble_shown = false;
                    ensemble_kind = Ensemble_None;
                }
                // After the ensemble flags are set, so restraints resolve against the shown model, not the model files
                if (app_state) resolve(*app_state);
                models.clear();
                fit_applied = false;
                fit_original.clear();
                fit_frame = -1;
                fit_status.clear();
                clear_clashes();
                break;
            case viamd::EventType_ViamdSystemFree:
                unresolve();
                break;
            case viamd::EventType_ViamdSerialize: {
                viamd::serialization_state_t& s = *(viamd::serialization_state_t*)e.payload;
                viamd::write_section_header(s, MD_STR_LIT("Haddock"));
                viamd::write_str(s, MD_STR_LIT("FixedChains"), md_str_from_cstr(fixed_chains.c_str()));
                viamd::write_str(s, MD_STR_LIT("MovingChains"), md_str_from_cstr(moving_chains.c_str()));
                if (!files_text.empty()) {
                    // The model files are written relative to the workspace, so a saved session is portable
                    std::string rel;
                    for (const std::string& p : split_paths(files_text)) { if (!rel.empty()) rel += ";"; rel += ws_relative(s.filename, p); }
                    viamd::write_str(s, MD_STR_LIT("ModelFiles"), md_str_from_cstr(rel.c_str()));
                }
                if (!path.empty()) viamd::write_str(s, MD_STR_LIT("Restraints"), md_str_from_cstr(ws_relative(s.filename, path).c_str()));
                if (!cryo_em_map_path.empty()) viamd::write_str(s, MD_STR_LIT("CryoEMMap"), md_str_from_cstr(ws_relative(s.filename, cryo_em_map_path).c_str()));
                if (!disvis.dir.empty()) viamd::write_str(s, MD_STR_LIT("DisvisDir"), md_str_from_cstr(ws_relative(s.filename, disvis.dir).c_str()));
                if (!powerfit.dir.empty()) viamd::write_str(s, MD_STR_LIT("PowerfitDir"), md_str_from_cstr(ws_relative(s.filename, powerfit.dir).c_str()));
                viamd::write_flt(s, MD_STR_LIT("Resolution"), fit_resolution);
                break;
            }
            case viamd::EventType_ViamdDeserializeBegin:
                // A missing section means the defaults, so reset the kept inputs
                ws_pending = false;
                ws_files.clear(); ws_restraints.clear(); ws_cryo.clear(); ws_disvis.clear(); ws_powerfit.clear();
                ws_fixed = "A"; ws_moving.clear(); ws_resolution = 9.0f; ws_folder.clear();
                // A new workspace starts with a clean slate of transient analysis messages, so none lingers from the
                // checkpoint loaded before it (e.g. a clash or map message from a different system)
                superpose_status.clear(); ensemble_status.clear(); files_status.clear();
                clash_status.clear(); scan_status.clear(); score_status.clear(); fit_status.clear(); map_status.clear();
                break;
            case viamd::EventType_ViamdDeserialize: {
                viamd::deserialization_state_t& s = *(viamd::deserialization_state_t*)e.payload;
                if (md_str_eq(viamd::section_header(s), MD_STR_LIT("Haddock"))) {
                    ws_pending = true;
                    const md_str_t folder = md_path_folder(s.filename);
                    ws_folder.assign(folder.ptr, folder.len);
                    md_str_t ident, arg, str;
                    while (viamd::next_entry(ident, arg, s)) {
                        if (md_str_eq(ident, MD_STR_LIT("FixedChains")) && viamd::extract_str(str, arg)) ws_fixed.assign(str.ptr, str.len);
                        else if (md_str_eq(ident, MD_STR_LIT("MovingChains")) && viamd::extract_str(str, arg)) ws_moving.assign(str.ptr, str.len);
                        else if (md_str_eq(ident, MD_STR_LIT("ModelFiles")) && viamd::extract_str(str, arg)) ws_files.assign(str.ptr, str.len);
                        else if (md_str_eq(ident, MD_STR_LIT("Restraints")) && viamd::extract_str(str, arg)) ws_restraints.assign(str.ptr, str.len);
                        else if (md_str_eq(ident, MD_STR_LIT("CryoEMMap")) && viamd::extract_str(str, arg)) ws_cryo.assign(str.ptr, str.len);
                        else if (md_str_eq(ident, MD_STR_LIT("DisvisDir")) && viamd::extract_str(str, arg)) ws_disvis.assign(str.ptr, str.len);
                        else if (md_str_eq(ident, MD_STR_LIT("PowerfitDir")) && viamd::extract_str(str, arg)) ws_powerfit.assign(str.ptr, str.len);
                        else if (md_str_eq(ident, MD_STR_LIT("Resolution"))) viamd::extract_flt(ws_resolution, arg);
                    }
                }
                break;
            }
            case viamd::EventType_ViamdDeserializeEnd:
                if (ws_pending && app_state) {
                    ws_pending = false;
                    fixed_chains = ws_fixed;
                    moving_chains = ws_moving;
                    fit_resolution = ws_resolution;
                    cryo_em_map_path = ws_resolve(ws_cryo);
                    // Resolve each model file relative to the workspace
                    std::string files_abs;
                    for (const std::string& p : split_paths(ws_files)) { if (!files_abs.empty()) files_abs += ";"; files_abs += ws_resolve(p); }
                    files_input = files_abs;
                    if (!files_abs.empty()) load_files(files_abs);
                    disvis.dir = ws_resolve(ws_disvis);
                    if (!disvis.dir.empty()) load_disvis(disvis);
                    powerfit.dir = ws_resolve(ws_powerfit);
                    if (!powerfit.dir.empty()) { load_powerfit(powerfit); scan_powerfit_fits(); }
                    resolve(*app_state);
                    const std::string restraints_abs = ws_resolve(ws_restraints);
                    if (!restraints_abs.empty()) haddock_restraints_load(app_state, md_str_from_cstr(restraints_abs.c_str()));
                }
                break;
            case viamd::EventType_ViamdFrameTick:
                restore_view();
                apply_model_style();
                draw_window();
                break;
            case viamd::EventType_ViamdRenderTransparent: {
                MD_ASSERT(e.payload_type == viamd::EventPayloadType_ApplicationState);
                draw_lines(*(ApplicationState*)e.payload);
                break;
            }
            case viamd::EventType_ViamdRenderKey: {
                // The overlay (RenderTransparent) is drawn with the scene: a hovered restraint must re-make it
                MD_ASSERT(e.payload_type == viamd::EventPayloadType_RenderKey);
                RenderKey* rk = (RenderKey*)e.payload;
                const int32_t state_key[9] = {hovered, hovered_clash, show_lines, fit_revision, show_clashes, clash_revision, hovered_solution, (int)show_powerfit_coms, pinned_rank};
                rk->key = md_hash64(state_key, sizeof(state_key), rk->key);
                break;
            }
            case viamd::EventType_ViamdWindowMenuRequest:
                viamd::window_menu_add(e, "Analysis/Haddock/Setup", &show_setup, {.order = 40});
                viamd::window_menu_add(e, "Analysis/Haddock/Analysis", &show_analysis, {.order = 41});
                break;
            default:
                break;
            }
        }
    }

    // A clash colour on an amber (mild) to deep-orange (severe) ramp. t and alpha are clamped to [0,1] and [0,255].
    static uint32_t clash_color(float t, int alpha) {
        t = MD_CLAMP(t, 0.0f, 1.0f);
        const int r = 255;
        const int g = (int)(180.0f + (60.0f - 180.0f) * t);
        const int b = (int)(40.0f + (0.0f - 40.0f) * t);
        const int a = MD_CLAMP(alpha, 0, 255);
        return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
    }

    // A PowerFit colour on a blue (low cross-correlation) to green (high) ramp. t and alpha are clamped.
    static uint32_t powerfit_color(float t, int alpha) {
        t = MD_CLAMP(t, 0.0f, 1.0f);
        const int r = (int)(80.0f + (60.0f - 80.0f) * t);
        const int g = (int)(120.0f + (230.0f - 120.0f) * t);
        const int b = (int)(255.0f + (90.0f - 255.0f) * t);
        const int a = MD_CLAMP(alpha, 0, 255);
        return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
    }

    void draw_lines(const ApplicationState& state) {
        immediate::Scope scope(state.gfx.overlay, "haddock_restraints");
        if (show_clashes && !clashes.empty()) {
            float max_overlap = 0.0f;
            for (const Clash& c : clashes) max_overlap = std::max(max_overlap, c.overlap);
            if (max_overlap <= 0.0f) max_overlap = 1.0f;
            const bool hovering = hovered_clash >= 0;
            for (int i = 0; i < (int)clashes.size(); ++i) {
                const Clash& c = clashes[i];
                md_float3_t a, b;
                if (!atom_position(state, files, c.atom[0], &a) || !atom_position(state, files, c.atom[1], &b)) continue;
                const float t = c.overlap / max_overlap;
                const bool hv = (i == hovered_clash);
                const int alpha = (!hovering || hv) ? 255 : 55;
                const uint32_t col = clash_color(t, alpha);
                if (hv) {
                    immediate::cylinder(scope, a, b, 0.14f, col);
                    immediate::sphere(scope, a, 0.32f, col);
                    immediate::sphere(scope, b, 0.32f, col);
                } else {
                    immediate::cylinder(scope, a, b, 0.06f, col, 0xFFFFFFFF, 8);
                }
            }
        }
        for (int i = 0; i < (int)restraints.size(); ++i) {
            const Restraint& rs = restraints[i];
            md_float3_t a, b;
            if (!atom_position(state, files, rs.atom[0], &a) || !atom_position(state, files, rs.atom[1], &b)) continue;
            float dist = 0.0f;
            restraint_distance(state.mold.state.xyz, state.mold.state.num_atoms, files, rs, &dist);
            const uint32_t color = restraint_satisfied(rs, dist) ? immediate::COLOR_GREEN : immediate::COLOR_RED;
            if (i == hovered) {
                immediate::cylinder(scope, a, b, 0.25f, color);
            } else if (show_lines) {
                immediate::line(scope, a, b, color);
            }
        }

        // PowerFit solution centres of mass, coloured by cross-correlation. This couples the PowerFit table (and, when the
        // DisVis map is shown, the accessible interaction space) to the 3D scene. The positions are the solutions.out x,y,z,
        // which are in the same frame as the loaded structures (world Angstrom).
        if (powerfit.loaded && (show_powerfit_coms || hovered_solution >= 0 || pinned_rank >= 0)) {
            double cc_min = 1e30, cc_max = -1e30;
            for (const auto& r : powerfit.rows) { cc_min = std::min(cc_min, r[1]); cc_max = std::max(cc_max, r[1]); }
            const double span = (cc_max > cc_min) ? (cc_max - cc_min) : 1.0;
            for (int i = 0; i < (int)powerfit.rows.size(); ++i) {
                const std::vector<double>& r = powerfit.rows[i];
                if (r.size() < 7) continue;
                const bool hv = (i == hovered_solution);
                const bool pin = ((int)r[0] == pinned_rank);
                if (!show_powerfit_coms && !hv && !pin) continue;
                const md_float3_t c = { (float)r[4], (float)r[5], (float)r[6] };
                const uint32_t col = powerfit_color((float)((r[1] - cc_min) / span), (hv || pin) ? 255 : 150);
                immediate::sphere(scope, c, (hv || pin) ? 5.0f : 3.0f, col);
            }
        }
    }

    // The atoms of the hovered restraint or clash are highlighted in the scene (only atoms of the system shown can be)
    void update_highlight() {
        int32_t atoms[2] = {-1, -1};
        auto in_system = [](AtomRef r) -> int32_t { return r.part < 0 ? r.idx : -1; };
        if (hovered >= 0 && (size_t)hovered < restraints.size()) {
            atoms[0] = in_system(restraints[hovered].atom[0]);
            atoms[1] = in_system(restraints[hovered].atom[1]);
        } else if (show_clashes && hovered_clash >= 0 && (size_t)hovered_clash < clashes.size()) {
            atoms[0] = in_system(clashes[hovered_clash].atom[0]);
            atoms[1] = in_system(clashes[hovered_clash].atom[1]);
        }
        if (atoms[0] < 0 && atoms[1] < 0) {
            if (highlighting) md_bitmask_clear(&app_state->selection.highlight_mask);
            highlighting = false;
            return;
        }
        md_bitmask_clear(&app_state->selection.highlight_mask);
        for (int s = 0; s < 2; ++s) {
            if (atoms[s] >= 0) md_bitmask_set_bit(&app_state->selection.highlight_mask, (uint32_t)atoms[s]);
        }
        highlighting = true;
    }

    // --- Input preparation helpers ---

    std::string convert_status;
    std::string ensemble_status;
    std::string cfg_status;
    std::string combine_status;
    std::vector<std::string> combine_list;   // PDBs staged to be merged into one complex (added across folders)

    // Adds PDBs (one folder at a time) to the list staged for combining into a complex
    void add_to_combine() {
        char buf[8192] = {};
        if (!application::file_dialog_multiple(buf, sizeof(buf), MD_STR_LIT("pdb"))) return;
        std::stringstream ss(buf);
        std::string item;
        while (std::getline(ss, item, ';')) {
            const size_t b = item.find_first_not_of(" \t");
            if (b != std::string::npos) combine_list.push_back(item.substr(b, item.find_last_not_of(" \t") - b + 1));
        }
    }

    // Combines the staged PDBs (e.g. two PowerFit fits, each a single body) into one complex and loads it as the system shown.
    // Chain IDs that collide between files are reassigned so every body keeps a distinct chain.
    void combine_pdbs_into_complex() {
        if (!app_state) return;
        const std::vector<std::string>& paths = combine_list;
        if (paths.size() < 2) { combine_status = "Add at least two PDB files (Browse again to add from another folder)"; return; }

        // Write the complex to a persistent, user-chosen file so a workspace saved afterwards can reload it
        char out_buf[2048] = {};
        if (!application::file_dialog(out_buf, sizeof(out_buf), application::FileDialogFlag_Save, MD_STR_LIT("pdb"))) return;
        const std::string out = out_buf;
        std::ofstream o(out);
        if (!o.good()) { combine_status = "Could not write the combined PDB"; return; }

        const std::string pool = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        std::string used;
        std::string summary;
        for (const std::string& p : paths) {
            std::ifstream in(p);
            if (!in.good()) { combine_status = "Could not read " + p; return; }
            std::vector<std::string> lines;
            std::string line, file_chains;
            while (std::getline(in, line)) {
                lines.push_back(line);
                if ((line.rfind("ATOM", 0) == 0 || line.rfind("HETATM", 0) == 0) && line.size() > 21) {
                    const char c = line[21];
                    if (file_chains.find(c) == std::string::npos) file_chains.push_back(c);
                }
            }
            // Remap chains of this file that are already used by an earlier file
            std::map<char, char> remap;
            for (char c : file_chains) {
                char nc = c;
                if (used.find(nc) != std::string::npos) {
                    for (char cand : pool) { if (used.find(cand) == std::string::npos) { nc = cand; break; } }
                }
                remap[c] = nc;
                used.push_back(nc);
                summary.push_back(nc);
            }
            for (std::string l : lines) {
                if ((l.rfind("ATOM", 0) == 0 || l.rfind("HETATM", 0) == 0) && l.size() > 21) {
                    l[21] = remap[l[21]];
                    o << l << "\n";
                }
            }
            o << "TER\n";
        }
        o << "END\n";
        o.close();

        if (!file_queue_full(&app_state->file_queue)) {
            file_queue_push(&app_state->file_queue, md_str_from_cstr(out.c_str()), FileFlags_None);
            combine_status = "Combined " + std::to_string(paths.size()) + " files into a complex (chains " + summary + "): " + out;
            combine_list.clear();
        }
    }


    // to a CNS/HADDOCK .tbl, as the tutorial's awk one-liner does: assign (sel1) (sel2) max max min
    bool convert_xlinks_to_tbl(const std::string& in_path, const std::string& out_path) {
        std::ifstream in(in_path);
        if (!in.good()) { convert_status = "Could not read " + in_path; return false; }
        std::ofstream out(out_path);
        if (!out.good()) { convert_status = "Could not write " + out_path; return false; }
        std::string line;
        size_t n = 0;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::stringstream ss(line);
            std::vector<std::string> f;
            std::string t;
            while (ss >> t) f.push_back(t);
            if (f.size() != 8) continue;
            out << "assign (segid " << f[0] << " and resid " << f[1] << " and name " << f[2] << ") "
                << "(segid " << f[3] << " and resid " << f[4] << " and name " << f[5] << ") "
                << f[7] << " " << f[7] << " " << f[6] << "\n";
            ++n;
        }
        convert_status = n ? ("Wrote " + std::to_string(n) + " restraints to " + out_path) : "No 8-column restraint lines found in " + in_path;
        return n > 0;
    }

    // Writes the loaded model files as one multi-model ensemble PDB (HADDOCK starting structures). The files must hold the same atoms.
    void save_ensemble() {
        if (files.size() < 2) { ensemble_status = "Load at least two model files to write an ensemble"; return; }
        char buf[2048] = {};
        if (!application::file_dialog(buf, sizeof(buf), application::FileDialogFlag_Save, MD_STR_LIT("pdb"))) return;
        md_temp_scope_t temp = md_temp_begin();
        md_str_t* paths = md_temp_alloc_array(temp, md_str_t, files.size());
        for (size_t i = 0; i < files.size(); ++i) paths[i] = md_str_from_cstr(files[i].path.c_str());
        const size_t n = md_haddock_ensemble_write(md_str_from_cstr(buf), paths, files.size(), md_get_heap_allocator());
        md_temp_end(temp);
        ensemble_status = n ? ("Wrote " + std::to_string(n) + " models to " + std::string(buf))
                            : "Could not write the ensemble: the files must hold the same atoms";
    }

    // Writes a minimal HADDOCK3 configuration (.cfg): the loaded model files as molecules, and the loaded restraint table
    void write_haddock_cfg() {
        char buf[2048] = {};
        if (!application::file_dialog(buf, sizeof(buf), application::FileDialogFlag_Save, MD_STR_LIT("cfg"))) return;
        std::ofstream out(buf);
        if (!out.good()) { cfg_status = "Could not write " + std::string(buf); return; }
        out << "# HADDOCK3 configuration generated by VIAMD. Review before running.\n";
        out << "run_dir = \"run1\"\n";
        out << "mode = \"local\"\n\n";
        out << "molecules = [\n";
        if (files.empty()) {
            out << "    # Add your input PDB files here\n";
        } else {
            for (const FolderPart& f : files) out << "    \"" << f.path << "\",\n";
        }
        out << "]\n\n";
        out << "[topoaa]\n\n";
        out << "[rigidbody]\n";
        if (!path.empty()) out << "ambig_fname = \"" << path << "\"\n";
        out << "\n[flexref]\n";
        if (!path.empty()) out << "ambig_fname = \"" << path << "\"\n";
        out << "\n[emref]\n";
        if (!path.empty()) out << "ambig_fname = \"" << path << "\"\n";
        cfg_status = "Wrote " + std::string(buf);
    }

    // The restraints whose two atoms were both found in the loaded structure (resolve assigns them)
    int restraints_matched() const {
        int n = 0;
        for (const Restraint& rs : restraints) if (rs.atom[0].idx >= 0 && rs.atom[1].idx >= 0) ++n;
        return n;
    }

    // Per-cluster statistics of the loaded model files, grouped by the name before the last '_' (clusterN_M.pdb -> clusterN)
    struct ClusterStat { std::string name; int count = 0; double mean = 0.0; double sd = 0.0; double best = 0.0; };
    std::vector<ClusterStat> cluster_stats() const {
        std::vector<std::string> order;
        std::map<std::string, std::vector<double>> groups;
        for (const FolderPart& f : files) {
            if (!f.has_energy) continue;
            const std::string fn = std::filesystem::path(f.path).filename().string();
            const size_t us = fn.find_last_of('_');
            const std::string key = us == std::string::npos ? fn : fn.substr(0, us);
            if (groups.find(key) == groups.end()) order.push_back(key);
            groups[key].push_back(f.energy.total);
        }
        std::vector<ClusterStat> out;
        for (const std::string& key : order) {
            const std::vector<double>& v = groups[key];
            ClusterStat s;
            s.name = key;
            s.count = (int)v.size();
            double sum = 0.0;
            s.best = v[0];
            for (double x : v) { sum += x; if (x < s.best) s.best = x; }
            s.mean = sum / (double)v.size();
            double var = 0.0;
            for (double x : v) var += (x - s.mean) * (x - s.mean);
            s.sd = v.size() > 1 ? std::sqrt(var / (double)(v.size() - 1)) : 0.0;
            out.push_back(s);
        }
        std::stable_sort(out.begin(), out.end(), [](const ClusterStat& a, const ClusterStat& b) { return a.mean < b.mean; });
        return out;
    }

    void draw_restraint_input() {
        if (ImGui::Button("Browse restraints##restraints")) {
            char path_buf[2048] = {};
            if (app_state && application::file_dialog(path_buf, sizeof(path_buf), application::FileDialogFlag_Open, MD_STR_LIT("tbl"))) {
                haddock_restraints_load(app_state, md_str_from_cstr(path_buf));
            }
        }
        ImGui::SetItemTooltip("CNS restraint table (.tbl) with the distance restraints. Dropping a .tbl file in the viewport loads it too.");
        ImGui::SameLine();
        ImGui::TextUnformatted(restraints.empty() ? "No restraint table loaded" : path.c_str());
        if (!restraints.empty()) {
            const int matched = restraints_matched();
            const int total = (int)restraints.size();
            const ImVec4 col = matched == total ? ImVec4(0.5f, 0.9f, 0.5f, 1.0f) : ImVec4(0.95f, 0.6f, 0.35f, 1.0f);
            ImGui::TextColored(col, "%d of %d restraints match the loaded structure", matched, total);
            if (matched < total) ImGui::SetItemTooltip("Unmatched restraints name atoms (segid/resid/name) absent from the loaded model files or the system shown. Check chain IDs and residue numbering.");
        }

        if (ImGui::Button("Convert DisVis .txt to .tbl")) {
            char in_buf[2048] = {};
            if (application::file_dialog(in_buf, sizeof(in_buf), application::FileDialogFlag_Open, MD_STR_LIT("txt"))) {
                char out_buf[2048] = {};
                if (application::file_dialog(out_buf, sizeof(out_buf), application::FileDialogFlag_Save, MD_STR_LIT("tbl"))) {
                    convert_xlinks_to_tbl(in_buf, out_buf);
                }
            }
        }
        ImGui::SetItemTooltip("Converts a DisVis cross-link file (columns: segid resid name  segid resid name  min max) into a HADDOCK .tbl.");
        if (!convert_status.empty()) ImGui::TextDisabled("%s", convert_status.c_str());
    }

    // Buttons that prepare HADDOCK input from the loaded model files
    void draw_input_prep() {
        ImGui::BeginDisabled(files.size() < 2);
        if (ImGui::Button("Save ensemble PDB")) save_ensemble();
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Writes the loaded model files as one multi-model ensemble PDB (HADDOCK starting structures). The files must hold the same atoms.");
        ImGui::SameLine();
        if (ImGui::Button("Write HADDOCK3 .cfg")) write_haddock_cfg();
        ImGui::SetItemTooltip("Writes a minimal HADDOCK3 configuration with the loaded model files and restraint table. Review it before running.");

        // Combine several PDBs (e.g. two PowerFit fits from different folders) into one complex. Staged across folders.
        if (ImGui::Button("Add PDB(s) to complex")) add_to_combine();
        ImGui::SetItemTooltip("Select PDBs in one folder; Browse again to add files from another folder (e.g. the two PowerFit fits).");
        ImGui::SameLine();
        ImGui::Text("%zu staged", combine_list.size());
        if (!combine_list.empty()) {
            ImGui::SameLine();
            ImGui::BeginDisabled(combine_list.size() < 2);
            if (ImGui::Button("Combine into complex")) combine_pdbs_into_complex();
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Clear##combine")) combine_list.clear();
            for (const std::string& p : combine_list) ImGui::BulletText("%s", std::filesystem::path(p).filename().string().c_str());
        }
        if (!ensemble_status.empty()) ImGui::TextDisabled("%s", ensemble_status.c_str());
        if (!cfg_status.empty()) ImGui::TextDisabled("%s", cfg_status.c_str());
        if (!combine_status.empty()) ImGui::TextDisabled("%s", combine_status.c_str());
    }

    void draw_restraints() {
        ImGui::Checkbox("Show lines", &show_lines);
        if (restraints.empty()) {
            ImGui::TextDisabled("Load a CNS restraint table (.tbl) in Inputs to show its distance restraints.");
            return;
        }
        size_t num_violated = 0;
        size_t num_missing = 0;
        for (const Restraint& rs : restraints) {
            float dist = 0.0f;
            if (!rdist(rs, &dist)) ++num_missing;
            else if (!restraint_satisfied(rs, dist)) ++num_violated;
        }
        ImGui::Text("%s: %zu restraints, %zu violated, %zu not in system", path.c_str(), restraints.size(),
                    num_violated, num_missing);
        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable;
        if (ImGui::BeginTable("##restraints", 6, flags)) {
            ImGui::TableSetupColumn("Atom 1");
            ImGui::TableSetupColumn("Atom 2");
            ImGui::TableSetupColumn("Distance");
            ImGui::TableSetupColumn("Target");
            ImGui::TableSetupColumn("Range");
            ImGui::TableSetupColumn("Status");
            ImGui::TableHeadersRow();
            if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsCount > 0) {
                const int col = specs->Specs[0].ColumnIndex;
                const bool desc = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
                // Status order: violated, satisfied, not in system
                auto key = [&](const Restraint& rs) -> double {
                    float dist = 0.0f;
                    const bool has_dist = rdist(rs, &dist);
                    switch (col) {
                    case 0: return ref_key(rs.atom[0]);
                    case 1: return ref_key(rs.atom[1]);
                    case 2: return has_dist ? dist : 1e30;
                    case 3: return rs.r.dist;
                    case 4: return rs.r.dist - rs.r.dist_minus;
                    default:
                        if (!has_dist) return 2.0;
                        return restraint_satisfied(rs, dist) ? 1.0 : 0.0;
                    }
                };
                std::stable_sort(restraints.begin(), restraints.end(), [&](const Restraint& x, const Restraint& y) {
                    const double kx = key(x), ky = key(y);
                    return desc ? kx > ky : kx < ky;
                });
            }
            for (size_t i = 0; i < restraints.size(); ++i) {
                const Restraint& rs = restraints[i];
                float dist = 0.0f;
                const bool has_dist = rdist(rs, &dist);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char label[128];
                snprintf(label, sizeof(label), "%s %d %s##%zu", rs.r.atom[0].segid, rs.r.atom[0].resid, rs.r.atom[0].name, i);
                ImGui::Selectable(label, false, ImGuiSelectableFlags_SpanAllColumns);
                if (ImGui::IsItemHovered() && has_dist) hovered = (int)i;
                ImGui::TableNextColumn();
                ImGui::Text("%s %d %s", rs.r.atom[1].segid, rs.r.atom[1].resid, rs.r.atom[1].name);
                ImGui::TableNextColumn();
                if (has_dist) ImGui::Text("%.2f", dist);
                else ImGui::TextDisabled("-");
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", rs.r.dist);
                ImGui::TableNextColumn();
                ImGui::Text("[%.2f, %.2f]", rs.r.dist - rs.r.dist_minus, rs.r.dist + rs.r.dist_plus);
                ImGui::TableNextColumn();
                if (!has_dist) ImGui::TextDisabled("not in system");
                else if (restraint_satisfied(rs, dist)) ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "satisfied");
                else ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "violated");
            }
            ImGui::EndTable();
        }
    }

    void draw_disvis_inputs() {
        input_text("DisVis folder##disvis", disvis.dir);
        ImGui::SetItemTooltip("The DisVis results folder, e.g. disvis/results-core-C82-C34-wHTH3 (holds accessible_complexes.out)");
        ImGui::SameLine();
        if (ImGui::Button("Browse##disvis")) {
            char path_buf[2048] = {};
            if (application::file_dialog(path_buf, sizeof(path_buf), application::FileDialogFlag_Dir)) {
                disvis.dir = path_buf;
                load_disvis(disvis);
            }
        }
        if (ImGui::Button("Load##disvis")) load_disvis(disvis);
        if (!disvis.error.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", disvis.error.c_str());

        // The accessible interaction space of the DisVis run, as a map
        const std::string map_path = disvis_map_path();
        if (!map_path.empty() && std::filesystem::exists(map_path)) {
            ImGui::SameLine();
            if (ImGui::Button("Show DisVis map##disvis")) show_map(map_path);
            ImGui::SetItemTooltip("Opens the DisVis accessible interaction space in the viewport. The cryo-EM map used for fitting is unaffected.");
            ImGui::TextDisabled("The map stores the number of satisfied restraints per voxel: raise the density isosurface level to see the space consistent with more restraints (N), lower it for fewer.");
        }
    }

    // Sorts numeric rows by the active sort column of a sortable table
    static void sort_numeric_rows(std::vector<std::vector<double>>& rows) {
        ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
        if (!specs || specs->SpecsCount == 0) return;
        const int col = specs->Specs[0].ColumnIndex;
        const bool desc = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
        std::stable_sort(rows.begin(), rows.end(), [&](const std::vector<double>& a, const std::vector<double>& b) {
            const double va = col < (int)a.size() ? a[col] : 0.0;
            const double vb = col < (int)b.size() ? b[col] : 0.0;
            return desc ? va > vb : va < vb;
        });
    }

    // Highlights the loaded cross-link that a DisVis restraint number refers to (same order as the restraint table)
    void hover_disvis_restraint(int disvis_num) {
        const int idx = disvis_num - 1;
        if (idx >= 0 && (size_t)idx < restraints.size()) hovered = idx;
    }

    void draw_disvis_results() {
        if (!disvis.loaded) return;

        ImGui::TextUnformatted("Complexes by number of satisfied restraints");
        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;
        const ImGuiTableFlags sortable = flags | ImGuiTableFlags_Sortable;
        if (ImGui::BeginTable("##disvis_complexes", 5, sortable)) {
            ImGui::TableSetupColumn("Restraints");
            ImGui::TableSetupColumn("Complexes");
            ImGui::TableSetupColumn("Relative");
            ImGui::TableSetupColumn("Cumulative");
            ImGui::TableSetupColumn("Cumulative relative");
            ImGui::TableHeadersRow();
            sort_numeric_rows(disvis.complexes);
            for (const auto& row : disvis.complexes) {
                ImGui::TableNextRow();
                for (size_t c = 0; c < row.size() && c < 5; ++c) {
                    ImGui::TableSetColumnIndex((int)c);
                    if (c == 0 || c == 1 || c == 3) ImGui::Text("%.0f", row[c]);
                    else ImGui::Text("%.6f", row[c]);
                }
            }
            ImGui::EndTable();
        }

        ImGui::TextUnformatted("Violations: row = consistent restraints, column = restraint violated");
        if (!disvis.violation_cols.empty() && ImGui::BeginTable("##disvis_violations", (int)disvis.violation_cols.size() + 1, flags)) {
            ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Consistent");
            for (size_t c = 0; c < disvis.violation_cols.size(); ++c) {
                ImGui::TableSetColumnIndex((int)c + 1);
                ImGui::Text("%d", disvis.violation_cols[c]);
            }
            for (size_t r = 0; r < disvis.violations.size(); ++r) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%d", disvis.violation_rows[r]);
                for (size_t c = 0; c < disvis.violations[r].size(); ++c) {
                    ImGui::TableSetColumnIndex((int)c + 1);
                    char lbl[48];
                    snprintf(lbl, sizeof(lbl), "%.4f##vi%zu_%zu", disvis.violations[r][c], r, c);
                    ImGui::Selectable(lbl);
                    if (ImGui::IsItemHovered() && c < disvis.violation_cols.size()) hover_disvis_restraint(disvis.violation_cols[c]);
                }
            }
            ImGui::EndTable();
        }

        ImGui::TextUnformatted("z-scores: a z-score above 1 (red) is a likely false-positive restraint");
        if (!disvis.zscore.empty() && ImGui::BeginTable("##disvis_zscore", 4, sortable)) {
            ImGui::TableSetupColumn("Restraint #");
            ImGui::TableSetupColumn("Average violated fraction");
            ImGui::TableSetupColumn("Standard deviation");
            ImGui::TableSetupColumn("z-score");
            ImGui::TableHeadersRow();
            sort_numeric_rows(disvis.zscore);
            for (size_t i = 0; i < disvis.zscore.size(); ++i) {
                const std::vector<double>& row = disvis.zscore[i];
                if (row.empty()) continue;
                const bool flagged = row.back() > 1.0;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (flagged) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
                char lbl[32];
                snprintf(lbl, sizeof(lbl), "%.0f##zs%zu", row[0], i);
                ImGui::Selectable(lbl, false, ImGuiSelectableFlags_SpanAllColumns);
                if (ImGui::IsItemHovered()) hover_disvis_restraint((int)row[0]);
                for (size_t c = 1; c < row.size() && c < 4; ++c) {
                    ImGui::TableSetColumnIndex((int)c);
                    ImGui::Text("%.3f", row[c]);
                }
                if (flagged) ImGui::PopStyleColor();
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Hover a z-score or violation to highlight that cross-link on the structure.");
    }

    // PowerFit results: the folder with solutions.out and the table of fits (cross-correlation, Fisher z-score, relative z-score)
    // The fit_N.pdb present in the PowerFit folder, in rank order (fit_1, fit_2, ...), for the ensemble view
    void scan_powerfit_fits() {
        powerfit_fits.clear();
        if (powerfit.dir.empty()) return;
        for (int n = 1; ; ++n) {
            const std::string p = powerfit.dir + "/fit_" + std::to_string(n) + ".pdb";
            if (!std::filesystem::exists(p)) break;
            powerfit_fits.push_back(p);
        }
    }

    // PowerFit inputs (folder + load), mirroring draw_disvis_inputs. Shown in the Setup window.
    void draw_powerfit_inputs() {
        input_text("PowerFit folder##powerfit", powerfit.dir);
        ImGui::SetItemTooltip("A PowerFit results folder holding solutions.out, e.g. cryo-EM/powerfit-PolIII-core");
        ImGui::SameLine();
        if (ImGui::Button("Browse##powerfit")) {
            char path_buf[2048] = {};
            if (application::file_dialog(path_buf, sizeof(path_buf), application::FileDialogFlag_Dir)) {
                powerfit.dir = path_buf;
                load_powerfit(powerfit);
                scan_powerfit_fits();
            }
        }
        if (ImGui::Button("Load##powerfit")) { load_powerfit(powerfit); scan_powerfit_fits(); }
        if (!powerfit.error.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", powerfit.error.c_str());
    }

    // PowerFit results table, mirroring draw_disvis_results. Shown in the Analysis window.
    void draw_powerfit_results() {
        if (!powerfit.loaded) {
            ImGui::TextDisabled("PowerFit's own cross-correlation, Fisher z-score and relative z-score. Load a PowerFit folder in Setup.");
            ImGui::TextDisabled("These values are read from PowerFit, not computed by VIAMD.");
            return;
        }
        ImGui::Checkbox("Overlay solution centres of mass on the map", &show_powerfit_coms);
        ImGui::SetItemTooltip("Draws every PowerFit solution's centre of mass, coloured blue (low) to green (high) by cross-correlation. With the DisVis map shown, you see which fits fall in the cross-link-consistent region.");
        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable;
        if (ImGui::BeginTable("##powerfit", 4, flags, ImVec2(0, 240))) {
            ImGui::TableSetupColumn("Rank");
            ImGui::TableSetupColumn("Cross-correlation");
            ImGui::TableSetupColumn("Fisher z-score");
            ImGui::TableSetupColumn("Relative z-score");
            ImGui::TableHeadersRow();
            sort_numeric_rows(powerfit.rows);
            for (int i = 0; i < (int)powerfit.rows.size(); ++i) {
                const std::vector<double>& row = powerfit.rows[i];
                const int rank = (int)row[0];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                char label[32];
                snprintf(label, sizeof(label), "%d##pf%d", rank, i);
                const bool sel = (rank == pinned_rank);
                if (ImGui::Selectable(label, sel, ImGuiSelectableFlags_SpanAllColumns)) pin_powerfit_solution((size_t)i);
                if (ImGui::IsItemHovered()) hovered_solution = i;
                ImGui::TableSetColumnIndex(1); ImGui::Text("%.4f", row[1]);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f", row[2]);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%.3f", row[3]);
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Hover a row to mark its centre of mass; click to pin it and centre the view on it (the scene is kept).");
    }

    // Per-cluster HADDOCK score statistics of the loaded model files
    void draw_clusters() {
        const std::vector<ClusterStat> stats = cluster_stats();
        if (stats.empty()) {
            ImGui::TextDisabled("Load the cluster model files (e.g. clusterX_1.pdb ... clusterX_4.pdb). The HADDOCK score is read from each PDB's REMARK block.");
            return;
        }
        ImGui::TextDisabled("Grouped by the name before the last '_', ranked by mean HADDOCK score.");
        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("##clusters", 5, flags, ImVec2(0, 200))) {
            ImGui::TableSetupColumn("Cluster");
            ImGui::TableSetupColumn("Models");
            ImGui::TableSetupColumn("Mean HADDOCK score");
            ImGui::TableSetupColumn("Std. dev.");
            ImGui::TableSetupColumn("Best");
            ImGui::TableHeadersRow();
            for (const ClusterStat& s : stats) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(s.name.c_str());
                ImGui::TableSetColumnIndex(1); ImGui::Text("%d", s.count);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.1f", s.mean);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%.1f", s.sd);
                ImGui::TableSetColumnIndex(4); ImGui::Text("%.1f", s.best);
            }
            ImGui::EndTable();
        }
    }

    void draw_model_files() {
        input_text("Model files##molecules", files_input);
        ImGui::SetItemTooltip("PDB files or folders separated by ';'. Loaded as separate models, so clashes can be found between different files.");
        ImGui::SameLine();
        if (ImGui::Button("Browse##molecules")) {
            char path_buf[8192] = {};
            if (application::file_dialog_multiple(path_buf, sizeof(path_buf), MD_STR_LIT("pdb"))) {
                if (!files_input.empty()) files_input += ";";
                files_input += path_buf;
                load_files(files_input);
                if (app_state) resolve(*app_state);
                clear_clashes();
            }
        }
        ImGui::SetItemTooltip("Select one or more PDB files in the same folder. Browse again to add files from another folder.");
        if (ImGui::Button("Load files##molecules")) {
            load_files(files_input);
            if (app_state) resolve(*app_state);
            clear_clashes();
        }
        ImGui::SameLine();
        if (files.empty()) ImGui::TextDisabled("No model files loaded: the model shown is used.");
        else ImGui::Text("%zu model files loaded", files.size());
        if (!files_status.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", files_status.c_str());
        for (size_t i = 0; i < files.size(); ++i) {
            ImGui::PushID((int)i);
            if (ImGui::Button("Show")) show_model(i);
            ImGui::PopID();
            ImGui::SetItemTooltip("Shows this model in the viewport. When the models share topology they are shown as one ensemble, so switching is smooth.");
            ImGui::SameLine();
            ImGui::TextUnformatted(files[i].name.c_str());
            if (i < file_fits.size() && file_fits[i].aligned) {
                ImGui::SameLine();
                if (i == 0) ImGui::TextDisabled("reference");
                else ImGui::TextDisabled("RMSD %.2f A to %s", file_fits[i].rmsd, files[0].name.c_str());
            }
        }
        draw_superpose_buttons();
        ImGui::Separator();
    }

    void draw_molecule_inputs() {
        input_text("Fixed chains##molecules", fixed_chains);
        input_text("Moving chains##molecules", moving_chains);
        ImGui::TextDisabled("Chain ids separated by spaces. Empty moving chains: the fit moves the whole model, and the clashes are every atom outside the fixed chains.");
    }

    void draw_clashes() {
        ImGui::Checkbox("Show clashes", &show_clashes);
        ImGui::SameLine();
        ImGui::TextDisabled("ChimeraX defaults: overlap 0.6 A, hydrogen bond allowance 0.4 A. Updated automatically for the shown model.");
        if (!clash_status.empty()) ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "%s", clash_status.c_str());
        if (!clashes_searched) {
            ImGui::TextDisabled("Set the fixed and moving chains and show a model to see its clashes.");
            return;
        }
        ImGui::Text("%zu clashes between the fixed and the moving chains", clashes.size());
        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable;
        if (!clashes.empty() && ImGui::BeginTable("##clashes", 3, flags, ImVec2(0, 240))) {
            ImGui::TableSetupColumn("Fixed atom");
            ImGui::TableSetupColumn("Moving atom");
            ImGui::TableSetupColumn("Overlap (A)", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending);
            ImGui::TableHeadersRow();
            if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsCount > 0) {
                const int col = specs->Specs[0].ColumnIndex;
                const bool desc = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
                std::stable_sort(clashes.begin(), clashes.end(), [&](const Clash& x, const Clash& y) {
                    if (col == 0) return desc ? ref_key(x.atom[0]) > ref_key(y.atom[0]) : ref_key(x.atom[0]) < ref_key(y.atom[0]);
                    if (col == 1) return desc ? ref_key(x.atom[1]) > ref_key(y.atom[1]) : ref_key(x.atom[1]) < ref_key(y.atom[1]);
                    return desc ? x.overlap > y.overlap : x.overlap < y.overlap;
                });
            }
            for (size_t i = 0; i < clashes.size(); ++i) {
                const Clash& c = clashes[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID((int)i);
                ImGui::Selectable(ref_label(*app_state, files, c.atom[0]).c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
                if (ImGui::IsItemHovered()) hovered_clash = (int)i;
                ImGui::PopID();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(ref_label(*app_state, files, c.atom[1]).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", c.overlap);
            }
            ImGui::EndTable();
        }
    }

    // One point per model: the satisfied restraints (x) against the clashes (y)
    void draw_model_scores() {
        std::vector<int> done;
        for (int i = 0; i < (int)model_scores.size(); ++i) {
            if (model_scores[i].done) done.push_back(i);
        }
        if (done.empty()) {
            ImGui::TextDisabled("Satisfied restraints vs clashes.");
            ImGui::TextDisabled("Run Analyze all models to fill this plot.");
            return;
        }

        const bool file_mode = !files.empty();
        // Coincident models share the same integer (satisfied, clashes): fan them out a little so every model is a visible point
        struct Pt { double x, y; int idx; };
        std::map<std::pair<int, int>, int> seen;
        std::map<std::string, std::vector<Pt>> groups;
        std::vector<std::string> order;
        std::vector<Pt> all;
        for (int i : done) {
            const int sx = model_scores[i].satisfied;
            const int sy = (int)model_scores[i].clashes;
            const int n = seen[{sx, sy}]++;
            const double ang = 0.7 * n, rad = n ? 0.12 : 0.0;
            const Pt pt{ (double)sx + rad * std::cos(ang), (double)sy + rad * std::sin(ang), i };
            std::string key = "Models";
            if (file_mode) {
                std::string fn = files[i].name;
                const size_t sl = fn.find_last_of('/');
                if (sl != std::string::npos) fn = fn.substr(sl + 1);
                const size_t us = fn.find_last_of('_');
                key = us == std::string::npos ? fn : fn.substr(0, us);
            }
            if (groups.find(key) == groups.end()) order.push_back(key);
            groups[key].push_back(pt);
            all.push_back(pt);
        }
        if (ImPlot::BeginPlot("##model_scores", ImVec2(-1, -1))) {
            ImPlot::SetupAxes("Satisfied restraints", "Clashes");
            ImPlot::SetupLegend(ImPlotLocation_NorthEast);
            for (const std::string& k : order) {
                std::vector<double> xs, ys;
                for (const Pt& p : groups[k]) { xs.push_back(p.x); ys.push_back(p.y); }
                ImPlot::PlotScatter(k.c_str(), xs.data(), ys.data(), (int)xs.size());
            }
            // Emphasize the model highlighted by the table (or the scatter, from the previous frame)
            if (hovered_model >= 0) {
                for (const Pt& p : all) {
                    if (p.idx == hovered_model) {
                        double hx = p.x, hy = p.y;
                        ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 11.0f, ImVec4(1, 1, 1, 0.0f), 2.5f, ImVec4(1, 1, 1, 1));
                        ImPlot::PlotScatter("##hl", &hx, &hy, 1);
                        break;
                    }
                }
            }
            if (ImPlot::IsPlotHovered()) {
                const ImPlotPoint mouse = ImPlot::GetPlotMousePos();
                int best = -1;
                double best_d = 0.0;
                for (size_t k = 0; k < all.size(); ++k) {
                    const double dx = all[k].x - mouse.x, dy = all[k].y - mouse.y;
                    const double d = dx * dx + dy * dy;
                    if (best < 0 || d < best_d) { best = (int)k; best_d = d; }
                }
                if (best >= 0 && best_d < 4.0) {
                    const int mi = all[best].idx;
                    pending_hover_plot = mi;
                    const ModelScore& s = model_scores[mi];
                    ImGui::SetTooltip("%s\nSatisfied restraints: %d\nClashes: %zu", (files.empty() ? model_name(*app_state, mi) : files[mi].name).c_str(), s.satisfied, s.clashes);
                    // Click a point to show that model
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        if (!files.empty()) show_model((size_t)mi);
                        else select_frame(*app_state, (int64_t)mi);
                    }
                }
            }
            ImPlot::EndPlot();
        }
    }

    void draw_superpose_buttons() {
        ImGui::BeginDisabled(align_target(*app_state) == nullptr);
        if (ImGui::Button("Align shown model to reference")) align_model(*app_state);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Moves the model shown onto the first model file on chain A. Run Analyze all models first, then Show a model.");
        if (!superpose_status.empty()) ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "%s", superpose_status.c_str());
    }

    void draw_map_inputs() {
        adopt_dropped_map(*app_state);
        if (cryo_em_map_path.empty()) {
            ImGui::TextDisabled("No cryo-EM map. Browse for one, or drop a map (.mrc) in the viewport.");
        } else {
            ImGui::Text("Cryo-EM map: %s", std::filesystem::path(cryo_em_map_path).filename().string().c_str());
        }
        if (ImGui::Button("Browse cryo-EM map")) {
            char path_buf[2048] = {};
            if (application::file_dialog(path_buf, sizeof(path_buf), application::FileDialogFlag_Open, MD_STR_LIT("mrc,map,ccp4"))) {
                cryo_em_map_path = path_buf;
                clear_scores();
            }
        }
        if (!cryo_em_map_path.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Show cryo-EM map")) show_map(cryo_em_map_path);
            ImGui::SetItemTooltip("Opens the cryo-EM map in the viewport. The map used for fitting is read from its file, so switching maps does not change it.");
        }
        if (ImGui::InputFloat("Resolution (A)##fit", &fit_resolution, 0.5f, 1.0f, "%.1f")) {
            if (fit_resolution < 1.0f) fit_resolution = 1.0f;
            clear_scores();
        }
        if (!map_status.empty()) ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "%s", map_status.c_str());
    }

    // The Analyze-all button and its status lines
    void draw_analyze_bar() {
        ApplicationState& state = *app_state;
        sync_models(state);
        ensure_model_scores(state);
        const bool have_map = !cryo_em_map_path.empty();
        if (ImGui::Button("Analyze all models")) analyze_all(state);
        ImGui::SameLine();
        ImGui::TextDisabled("Clashes, satisfied restraints, map correlation and RMSD for every model.");
        if (restraints.empty()) ImGui::TextDisabled("Load a restraint table (.tbl) in Setup to count satisfied restraints.");
        if (!have_map) ImGui::TextDisabled("Load a cryo-EM map in Setup to compute the map correlation.");
        if (!scan_status.empty()) ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "%s", scan_status.c_str());
        if (!score_status.empty()) ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "%s", score_status.c_str());
        if (!superpose_status.empty()) ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "%s", superpose_status.c_str());
    }

    // One row per model: clashes, satisfied restraints, map correlation and the RMSD to the first model
    void draw_models_table() {
        ApplicationState& state = *app_state;
        const bool file_mode = !files.empty();
        const int64_t frame = current_frame(state);
        sync_models(state);
        ensure_model_scores(state);

        const size_t n = file_mode ? files.size() : models.size();
        auto rmsd_of = [&](size_t i, double* rmsd) -> bool {
            if (file_mode) { if (i < file_fits.size() && file_fits[i].aligned) { *rmsd = file_fits[i].rmsd; return true; } return false; }
            if (i < models.size() && models[i].aligned) { *rmsd = models[i].rmsd; return true; }
            return false;
        };

        // Ranked by map correlation, the unscored models last
        std::vector<size_t> order(n);
        for (size_t i = 0; i < n; ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [this](size_t a, size_t b) {
            const bool sa = a < model_scores.size() && model_scores[a].scored;
            const bool sb = b < model_scores.size() && model_scores[b].scored;
            if (sa != sb) return sa;
            return sa && model_scores[a].score > model_scores[b].score;
        });

        const int selected = !file_mode ? (int)frame
                           : (ensemble_shown && ensemble_kind == Ensemble_Models) ? (int)frame
                           : shown_file;
        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("##models", 7, flags, ImVec2(0, 0))) {
            ImGui::TableSetupColumn("Model");
            ImGui::TableSetupColumn("Rank");
            ImGui::TableSetupColumn("Map correlation");
            ImGui::TableSetupColumn("HADDOCK score");
            ImGui::TableSetupColumn("Clashes");
            ImGui::TableSetupColumn("Restraints satisfied");
            ImGui::TableSetupColumn("RMSD chain A (A)");
            ImGui::TableHeadersRow();
            for (size_t k = 0; k < order.size(); ++k) {
                const size_t i = order[k];
                const ModelScore& s = i < model_scores.size() ? model_scores[i] : ModelScore{};
                ImGui::TableNextRow();
                if ((int)i == hovered_model) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(80, 110, 170, 110));
                ImGui::TableNextColumn();
                const std::string name = file_mode ? files[i].name : model_name(state, i);
                const std::string label = name + "##row" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), (int)i == selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    if (file_mode) show_model(i);
                    else select_frame(state, (int64_t)i);
                }
                if (ImGui::IsItemHovered()) pending_hover_table = (int)i;
                ImGui::TableNextColumn();
                if (s.scored) ImGui::Text("%zu", k + 1); else ImGui::TextDisabled("-");
                ImGui::TableNextColumn();
                if (s.scored) ImGui::Text("%.4f", s.score); else ImGui::TextDisabled("-");
                ImGui::TableNextColumn();
                if (file_mode && files[i].has_energy) {
                    ImGui::Text("%.1f", files[i].energy.total);
                    ImGui::SetItemTooltip("HADDOCK total %.1f (vdw %.1f, elec %.1f)", files[i].energy.total, files[i].energy.vdw, files[i].energy.elec);
                } else {
                    ImGui::TextDisabled("-");
                }
                ImGui::TableNextColumn();
                if (s.done) ImGui::Text("%zu", s.clashes); else ImGui::TextDisabled("-");
                ImGui::TableNextColumn();
                if (s.done) ImGui::Text("%d", s.satisfied); else ImGui::TextDisabled("-");
                ImGui::TableNextColumn();
                double rmsd = 0.0;
                if (rmsd_of(i, &rmsd)) ImGui::Text("%.3f", rmsd); else ImGui::TextDisabled("-");
            }
            ImGui::EndTable();
        }
    }

    // The local fit of the model shown into the cryo-EM map, and its result
    void draw_fit_actions() {
        ApplicationState& state = *app_state;
        const int64_t frame = current_frame(state);
        drop_fit_on_frame_change(frame);
        sync_models(state);
        const bool have_map = !cryo_em_map_path.empty();

        ImGui::BeginDisabled(!have_map || state.mold.state.num_atoms == 0);
        if (ImGui::Button("Apply best fit")) apply_fit(state, frame);
        ImGui::SameLine();
        ImGui::BeginDisabled(!fit_applied);
        if (ImGui::Button("Reset")) reset_fit(state);
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(state.mold.state.num_atoms == 0);
        if (ImGui::Button("Save fitted PDB")) save_fitted_pdb(state);
        ImGui::EndDisabled();
        if (!fit_status.empty()) ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "%s", fit_status.c_str());
        ImGui::TextDisabled("Apply best fit moves: %s", moving_chains.empty() ? "the whole model" : ("chains " + moving_chains).c_str());
        ImGui::TextDisabled("To optimise both molecules, fit one, then change the moving chains and fit the other; both are kept. Reset restores all.");

        const ModelFit& cur = models[frame];
        if (cur.fitted) {
            ImGui::TextUnformatted(fit_applied ? "Local fit applied to the model shown:" : "Local fit result (not applied to the model shown):");
            ImGui::Text("Mean map value %.5f, centroid shift %.3f A", cur.fit_mean, cur.fit_shift);
            const float* t = cur.transform;
            ImGui::Text("Matrix  %8.4f %8.4f %8.4f %8.4f", t[0], t[1], t[2], t[3]);
            ImGui::Text("        %8.4f %8.4f %8.4f %8.4f", t[4], t[5], t[6], t[7]);
            ImGui::Text("        %8.4f %8.4f %8.4f %8.4f", t[8], t[9], t[10], t[11]);
            float axis[3], angle = 0.0f;
            rotation_axis_angle(t, axis, &angle);
            ImGui::Text("Axis %.4f %.4f %.4f", axis[0], axis[1], axis[2]);
            ImGui::Text("Rotation angle %.3f degrees", angle);
            ImGui::Text("Translation %.3f %.3f %.3f A", t[3], t[7], t[11]);
        }
    }

    void draw_setup_window() {
        if (!show_setup) return;
        if (ImGui::Begin("Haddock Setup", &show_setup, ImGuiWindowFlags_NoFocusOnAppearing)) {
            draw_model_files();
            draw_molecule_inputs();
            ImGui::Separator();
            draw_restraint_input();
            ImGui::Separator();
            draw_map_inputs();
            ImGui::Separator();
            draw_disvis_inputs();
            ImGui::Separator();
            draw_powerfit_inputs();
            ImGui::Separator();
            ImGui::TextUnformatted("Prepare HADDOCK input");
            draw_input_prep();
        }
        ImGui::End();
    }

    void draw_analysis_window() {
        if (!show_analysis) return;
        if (ImGui::Begin("Haddock Analysis", &show_analysis, ImGuiWindowFlags_NoFocusOnAppearing)) {
            const bool file_mode = !files.empty();
            draw_analyze_bar();
            // The models table and the satisfied-vs-clashes plot, side by side
            const float row_h = 260.0f;
            const float avail = ImGui::GetContentRegionAvail().x;
            ImGui::BeginChild("##models_panel", ImVec2(avail * 0.62f, row_h), ImGuiChildFlags_None);
            draw_models_table();
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("##plot_panel", ImVec2(0, row_h), ImGuiChildFlags_None);
            draw_model_scores();
            ImGui::EndChild();
            ImGui::TextDisabled(file_mode ? "Click a row to show that model in the viewport." : "Click a row to show that frame.");

            ImGui::Separator();
            draw_fit_actions();

            ImGui::Separator();
            if (ImGui::BeginTabBar("##haddock_details")) {
                if (ImGui::BeginTabItem("Clashes")) {
                    draw_clashes();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Restraints")) {
                    draw_restraints();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Clusters")) {
                    draw_clusters();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("DisVis")) {
                    draw_disvis_results();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("PowerFit")) {
                    draw_powerfit_results();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }

    void draw_window() {
        hovered = -1;
        hovered_clash = -1;
        hovered_solution = -1;
        pending_hover_table = -1;
        pending_hover_plot = -1;
        // Clashes are always shown for the model on screen: recomputed (fast, spatial hash) when the frame or chains change
        ensure_clashes(*app_state);
        draw_setup_window();
        draw_analysis_window();
        // The table and the scatter cross-highlight the same model (one frame of lag, which is imperceptible on hover)
        hovered_model = pending_hover_table >= 0 ? pending_hover_table : pending_hover_plot;
        update_highlight();
    }
};

static Haddock instance;

}  // namespace

bool haddock_restraints_load(ApplicationState* state, md_str_t path) {
    md_temp_scope_t temp = md_temp_begin();
    MD_DEFER { md_temp_end(temp); };
    md_allocator_i* temp_alloc = md_temp_allocator(temp);

    md_str_t txt = md_load_textfile(path, temp_alloc);
    if (txt.len == 0) {
        MD_LOG_ERROR("Haddock: could not read restraint table");
        return false;
    }

    const size_t count = md_haddock_distance_restraints_parse(nullptr, 0, txt);
    if (count == 0) {
        MD_LOG_ERROR("Haddock: no distance restraints found in table");
        return false;
    }
    md_haddock_distance_restraint_t* parsed = md_temp_alloc_array(temp, md_haddock_distance_restraint_t, count);
    md_haddock_distance_restraints_parse(parsed, count, txt);

    instance.restraints.clear();
    instance.restraints.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        Restraint rs;
        rs.r = parsed[i];
        instance.restraints.push_back(rs);
    }
    instance.path.assign(path.ptr, path.len);
    instance.resolve(*state);
    instance.show_setup = true;
    instance.show_analysis = true;

    MD_LOG_INFO("Haddock: loaded %zu distance restraints", count);
    return true;
}

void haddock_ensemble_names_set(md_str_t path, const md_str_t* names, size_t count) {
    instance.ensemble_names_path.assign(path.ptr, path.len);
    instance.ensemble_names.clear();
    for (size_t i = 0; i < count; ++i) {
        instance.ensemble_names.emplace_back(names[i].ptr, names[i].len);
    }
}
