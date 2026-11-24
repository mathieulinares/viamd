// Haddock restraints: the distance restraints of a CNS table (the "assign" lines of a HADDOCK .tbl file), drawn between
// their two atoms. A restraint is satisfied when the distance lies in [d - dminus, d + dplus]. Each atom is matched to
// the system by segid (chain), resid and name, when the table is loaded and when a system is initialized. A restraint
// whose atoms are not in the system is listed but not drawn.

#include <core/md_log.h>
#include <core/md_common.h>
#include <core/md_str.h>
#include <core/md_file.h>
#include <core/md_arena_allocator.h>

#include <md_system.h>
#include <md_haddock.h>
#include <md_map_fit.h>
#include <md_clash.h>

#include <viamd_event.h>
#include <window_menu.h>
#include <viamd.h>
#include <event.h>
#include <gfx/immediate_draw_utils.h>
#include <components/haddock/haddock.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Restraint {
    md_haddock_distance_restraint_t r;
    int atom[2] = {-1, -1};     // system atom per side, -1 when the system has no such atom
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
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", txt.c_str());
    if (!ImGui::InputText(label, buf, sizeof(buf))) return false;
    txt = buf;
    return true;
}

// The atoms whose chain is in the list, or outside it when invert is set (so an empty list with invert is every atom)
static std::vector<uint32_t> atoms_in_chains(const md_system_t& sys, const std::vector<std::string>& chains, bool invert) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < sys.atom.count; ++i) {
        bool in = false;
        const md_chain_idx_t chain = md_system_chain_find_by_atom_idx(&sys, i);
        if (chain >= 0) {
            const md_str_t id = md_system_chain_id(&sys, (size_t)chain);
            for (const std::string& c : chains) {
                if (c.size() == id.len && memcmp(c.data(), id.ptr, id.len) == 0) {
                    in = true;
                    break;
                }
            }
        }
        if (in != invert) out.push_back((uint32_t)i);
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

static bool atom_position(const ApplicationState& state, int idx, md_float3_t* out) {
    const md_system_state_t& st = state.mold.state;
    if (idx < 0 || !st.xyz || (size_t)idx >= st.num_atoms) return false;
    *out = st.xyz[idx];
    return true;
}

static bool restraint_distance(const ApplicationState& state, const Restraint& rs, float* out) {
    md_float3_t a, b;
    if (!atom_position(state, rs.atom[0], &a) || !atom_position(state, rs.atom[1], &b)) return false;
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

struct Clash {
    uint32_t atom[2];   // system atoms, the first in the fixed chains and the second in the moving chains
    float overlap;      // van der Waals overlap (Å), after the hydrogen bond allowance
};

struct Haddock : viamd::EventHandler {
    ApplicationState* app_state = nullptr;
    std::vector<Restraint> restraints;
    std::string path;
    bool show_window = false;
    bool show_lines = true;
    int hovered = -1;           // restraint under the mouse in the table, -1 when none
    bool highlighting = false;  // the shared highlight mask is set by this component

    // The two molecules of a system, by chain id. The moving chains are fitted and checked for clashes against the
    // fixed chains. With no moving chains, the fit moves the whole model and the clashes are every atom outside the fixed chains.
    std::string fixed_chains = "A";
    std::string moving_chains;
    std::vector<Clash> clashes;
    bool clashes_searched = false;
    int64_t clash_frame = -1;   // the frame the clashes were found on
    std::string clash_status;   // why the last search found no clashes to report
    bool show_clashes = true;
    int clash_revision = 0;     // the clash lines re-make the scene
    std::string fit_status;     // why the last local fit was not applied

    // Map fit. The map is read from its file, since the loaded copy is kept transposed for display.
    md_mrc_t fit_map = {};
    int fit_map_idx = -1;
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

    Haddock() { viamd::event_system_register_handler(*this); }

    ~Haddock() {
        if (fit_map.val) md_mrc_free(&fit_map, md_get_heap_allocator());
    }

    bool load_fit_map(const ApplicationState& state, int idx) {
        if (idx == fit_map_idx && fit_map.val) return true;
        if (fit_map.val) md_mrc_free(&fit_map, md_get_heap_allocator());
        fit_map = {};
        fit_map_idx = -1;
        if (idx < 0 || (size_t)idx >= state.maps.size()) return false;
        if (!md_mrc_file_load(&fit_map, md_str_from_cstr(state.maps[idx].path.c_str()), md_get_heap_allocator())) {
            MD_LOG_ERROR("Haddock: could not read map for fitting");
            return false;
        }
        fit_map_idx = idx;
        return true;
    }

    // The simulated-map score of the heavy atoms (hydrogens are left out, as in ChimeraX fitmap)
    bool score_coords(const ApplicationState& state, const md_float3_t* src, size_t count, double* score, size_t* points) {
        std::vector<md_float3_t> xyz;
        std::vector<float> weight;
        for (size_t i = 0; i < count; ++i) {
            const md_atomic_number_t z = md_atom_atomic_number(&state.mold.sys.atom, i);
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

    void clear_scores() {
        for (ModelFit& m : models) {
            m.scored = false;
            m.score = 0.0;
            m.points = 0;
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
        m.scored = score_coords(state, xyz.data(), xyz.size(), &m.score, &m.points);
        if (!m.scored) {
            score_status = "No heavy atoms to score in " + model_name(state, frame);
            MD_LOG_ERROR("Haddock: %s", score_status.c_str());
        }
    }

    void score_all(ApplicationState& state) {
        sync_models(state);
        score_status.clear();
        for (int64_t f = 0; f < (int64_t)models.size(); ++f) score_frame(state, f);
    }

    // Moves the model shown into the map by a local rigid fit of all its atoms, as ChimeraX fitmap without resolution
    void apply_fit(ApplicationState& state, int64_t frame) {
        if (fit_applied) reset_fit(state);
        const size_t n = state.mold.state.num_atoms;
        fit_status.clear();
        if (n == 0 || frame < 0 || (size_t)frame >= models.size()) return;

        // Only the moving atoms are fitted, so the fixed chains stay where they are
        const std::vector<uint32_t> group = moving_atoms(state.mold.sys, true);
        if (group.empty()) {
            fit_status = "No atoms in the moving chains";
            MD_LOG_ERROR("Haddock: %s", fit_status.c_str());
            return;
        }
        const size_t ng = group.size();

        fit_original.assign(state.mold.state.xyz, state.mold.state.xyz + n);
        std::vector<md_float3_t> sub(ng), moved(ng);
        for (size_t k = 0; k < ng; ++k) sub[k] = fit_original[group[k]];
        ModelFit& m = models[frame];
        m.fit_mean = md_map_fit_atoms_in_map(&fit_map, (const float*)sub.data(), (float*)moved.data(), ng);
        if (!md_map_fit_rigid_transform((const float*)sub.data(), (const float*)moved.data(), ng, m.transform)) {
            fit_original.clear();
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
        ++clash_revision;
    }

    // Finds the clashes between the fixed and the moving chains on the coordinates shown
    void find_clashes(ApplicationState& state) {
        clear_clashes();
        clash_status.clear();
        const md_system_t& sys = state.mold.sys;
        const std::vector<uint32_t> fixed = atoms_in_chains(sys, parse_chains(fixed_chains), false);
        const std::vector<uint32_t> mov = moving_atoms(sys, false);
        if (fixed.empty() || mov.empty()) {
            clash_status = fixed.empty() ? "No atoms in the fixed chains" : "No atoms in the moving chains";
            MD_LOG_ERROR("Haddock: %s", clash_status.c_str());
            return;
        }

        std::vector<float> xa(3 * fixed.size()), xb(3 * mov.size());
        std::vector<md_atomic_number_t> za(fixed.size()), zb(mov.size());
        for (size_t k = 0; k < fixed.size(); ++k) {
            const md_float3_t p = state.mold.state.xyz[fixed[k]];
            xa[3 * k + 0] = p.x;
            xa[3 * k + 1] = p.y;
            xa[3 * k + 2] = p.z;
            za[k] = md_atom_atomic_number(&sys.atom, fixed[k]);
        }
        for (size_t k = 0; k < mov.size(); ++k) {
            const md_float3_t p = state.mold.state.xyz[mov[k]];
            xb[3 * k + 0] = p.x;
            xb[3 * k + 1] = p.y;
            xb[3 * k + 2] = p.z;
            zb[k] = md_atom_atomic_number(&sys.atom, mov[k]);
        }

        // ChimeraX clashes: an overlap of 0.6 Å, with 0.4 Å allowed for hydrogen bonds
        std::vector<float> ra(fixed.size()), rb(mov.size());
        md_clash_radii(xa.data(), za.data(), fixed.size(), ra.data());
        md_clash_radii(xb.data(), zb.data(), mov.size(), rb.data());
        const size_t num = md_clash_find(xa.data(), za.data(), ra.data(), fixed.size(), xb.data(), zb.data(), rb.data(), mov.size(), 0.6f, 0.4f, nullptr, 0);
        std::vector<md_clash_pair_t> pairs(num);
        md_clash_find(xa.data(), za.data(), ra.data(), fixed.size(), xb.data(), zb.data(), rb.data(), mov.size(), 0.6f, 0.4f, pairs.data(), num);
        clashes.reserve(num);
        for (const md_clash_pair_t& p : pairs) clashes.push_back({{fixed[p.a], mov[p.b]}, p.overlap});
        clash_frame = current_frame(state);
        clashes_searched = true;
        ++clash_revision;
    }

    // Superposes every model onto the first one on the chain A C-alpha atoms (matchmaker #2 to #1/A). Only the
    // RMSD is stored, the models are not moved until one is aligned.
    void superpose_all(ApplicationState& state) {
        if (fit_applied) reset_fit(state);
        sync_models(state);
        superpose_status.clear();
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

    // Moves the model shown onto the first model by the superposition of that model
    void align_model(ApplicationState& state, int64_t frame) {
        if (frame < 0 || (size_t)frame >= models.size() || !models[frame].aligned) return;
        reset_fit(state);
        const size_t n = state.mold.state.num_atoms;
        fit_original.assign(state.mold.state.xyz, state.mold.state.xyz + n);
        for (size_t i = 0; i < n; ++i) state.mold.state.xyz[i] = transform_point(models[frame].superpose, fit_original[i]);
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

    void resolve(const md_system_t& sys) {
        for (Restraint& rs : restraints) {
            for (int s = 0; s < 2; ++s) {
                rs.atom[s] = find_atom(sys, rs.r.atom[s]);
            }
        }
    }

    void unresolve() {
        for (Restraint& rs : restraints) {
            rs.atom[0] = rs.atom[1] = -1;
        }
    }

    void process_events(const viamd::Event* events, size_t num_events) final {
        for (size_t i = 0; i < num_events; ++i) {
            const viamd::Event& e = events[i];
            switch (e.type) {
            case viamd::EventType_ViamdInitialize:
                app_state = (ApplicationState*)e.payload;
                workspace_register_window("Haddock", &show_window);
                break;
            case viamd::EventType_ViamdSystemInit:
                if (app_state) resolve(app_state->mold.sys);
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
            case viamd::EventType_ViamdFrameTick:
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
                const int32_t state_key[5] = {hovered, show_lines, fit_revision, show_clashes, clash_revision};
                rk->key = md_hash64(state_key, sizeof(state_key), rk->key);
                break;
            }
            case viamd::EventType_ViamdWindowMenuRequest:
                viamd::window_menu_add(e, "Analysis/Haddock", &show_window, {.order = 40});
                break;
            default:
                break;
            }
        }
    }

    void draw_lines(const ApplicationState& state) {
        immediate::Scope scope(state.gfx.overlay, "haddock_restraints");
        if (show_clashes) {
            for (const Clash& c : clashes) {
                md_float3_t a, b;
                if (!atom_position(state, (int)c.atom[0], &a) || !atom_position(state, (int)c.atom[1], &b)) continue;
                immediate::line(scope, a, b, immediate::COLOR_MAGENTA);
            }
        }
        for (int i = 0; i < (int)restraints.size(); ++i) {
            const Restraint& rs = restraints[i];
            md_float3_t a, b;
            if (!atom_position(state, rs.atom[0], &a) || !atom_position(state, rs.atom[1], &b)) continue;
            float dist = 0.0f;
            restraint_distance(state, rs, &dist);
            const uint32_t color = restraint_satisfied(rs, dist) ? immediate::COLOR_GREEN : immediate::COLOR_RED;
            if (i == hovered) {
                immediate::cylinder(scope, a, b, 0.25f, color);
            } else if (show_lines) {
                immediate::line(scope, a, b, color);
            }
        }
    }

    void update_highlight() {
        const bool has_hover = hovered >= 0 && (size_t)hovered < restraints.size();
        if (!has_hover) {
            if (highlighting) md_bitmask_clear(&app_state->selection.highlight_mask);
            highlighting = false;
            return;
        }
        const Restraint& rs = restraints[hovered];
        md_bitmask_clear(&app_state->selection.highlight_mask);
        for (int s = 0; s < 2; ++s) {
            if (rs.atom[s] >= 0) md_bitmask_set_bit(&app_state->selection.highlight_mask, (uint32_t)rs.atom[s]);
        }
        highlighting = true;
    }

    void draw_restraints() {
        ImGui::Checkbox("Show lines", &show_lines);
        if (restraints.empty()) {
            ImGui::TextDisabled("Drop a CNS restraint table (.tbl) to show its distance restraints.");
            return;
        }
        size_t num_violated = 0;
        size_t num_missing = 0;
        for (const Restraint& rs : restraints) {
            float dist = 0.0f;
            if (!restraint_distance(*app_state, rs, &dist)) ++num_missing;
            else if (!restraint_satisfied(rs, dist)) ++num_violated;
        }
        ImGui::Text("%s: %zu restraints, %zu violated, %zu not in system", path.c_str(), restraints.size(),
                    num_violated, num_missing);
        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("##restraints", 6, flags)) {
            ImGui::TableSetupColumn("Atom 1");
            ImGui::TableSetupColumn("Atom 2");
            ImGui::TableSetupColumn("Distance");
            ImGui::TableSetupColumn("Target");
            ImGui::TableSetupColumn("Range");
            ImGui::TableSetupColumn("Status");
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < restraints.size(); ++i) {
                const Restraint& rs = restraints[i];
                float dist = 0.0f;
                const bool has_dist = restraint_distance(*app_state, rs, &dist);
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

    void draw_molecules() {
        input_text("Fixed chains##molecules", fixed_chains);
        input_text("Moving chains##molecules", moving_chains);
        ImGui::TextDisabled("Chain ids separated by spaces. Empty moving chains: the fit moves the whole model, and the clashes are every atom outside the fixed chains.");
        if (ImGui::Button("Find clashes")) find_clashes(*app_state);
        ImGui::SameLine();
        ImGui::Checkbox("Show clashes", &show_clashes);
        if (!clash_status.empty()) ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "%s", clash_status.c_str());
        if (!clashes_searched) {
            ImGui::TextDisabled("Find the clashes between the fixed and the moving chains (ChimeraX defaults: overlap 0.6 A, hydrogen bond allowance 0.4 A).");
            return;
        }
        ImGui::Text("%zu clashes between the fixed and the moving chains", clashes.size());
        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
        if (!clashes.empty() && ImGui::BeginTable("##clashes", 3, flags, ImVec2(0, 240))) {
            ImGui::TableSetupColumn("Fixed atom");
            ImGui::TableSetupColumn("Moving atom");
            ImGui::TableSetupColumn("Overlap (A)");
            ImGui::TableHeadersRow();
            for (const Clash& c : clashes) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(atom_label(app_state->mold.sys, c.atom[0]).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(atom_label(app_state->mold.sys, c.atom[1]).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", c.overlap);
            }
            ImGui::EndTable();
        }
    }

    void draw_map_fit() {
        if (app_state->maps.empty()) {
            ImGui::TextDisabled("Drop a map (.mrc) to fit the models into it.");
            return;
        }
        const int64_t frame = current_frame(*app_state);
        drop_fit_on_frame_change(frame);
        sync_models(*app_state);

        int sel = fit_map_idx >= 0 ? fit_map_idx : 0;
        std::vector<const char*> names;
        for (const MapVolume& m : app_state->maps) names.push_back(m.name.c_str());
        if (ImGui::Combo("Map##fit", &sel, names.data(), (int)names.size())) clear_scores();
        if (ImGui::InputFloat("Resolution (A)##fit", &fit_resolution, 0.5f, 1.0f, "%.1f")) {
            if (fit_resolution < 1.0f) fit_resolution = 1.0f;
            clear_scores();
        }
        const bool map_ok = load_fit_map(*app_state, sel);

        ImGui::BeginDisabled(!map_ok);
        if (ImGui::Button("Score all models")) score_all(*app_state);
        ImGui::SameLine();
        if (ImGui::Button("Apply best fit")) apply_fit(*app_state, frame);
        ImGui::SameLine();
        ImGui::BeginDisabled(!fit_applied);
        if (ImGui::Button("Reset")) reset_fit(*app_state);
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        if (ImGui::Button("Superpose on chain A")) superpose_all(*app_state);
        ImGui::SameLine();
        ImGui::BeginDisabled(!(frame < (int64_t)models.size() && models[frame].aligned));
        if (ImGui::Button("Align")) align_model(*app_state, frame);
        ImGui::EndDisabled();
        if (!superpose_status.empty()) ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "%s", superpose_status.c_str());

        size_t num_scored = 0;
        for (const ModelFit& m : models) num_scored += m.scored;
        ImGui::Text("%zu models, %zu scored. Click a row to show that model.", models.size(), num_scored);
        if (!score_status.empty()) ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "%s", score_status.c_str());
        if (!fit_status.empty()) ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "%s", fit_status.c_str());
        ImGui::TextDisabled("Apply best fit moves: %s", moving_chains.empty() ? "the whole model" : ("chains " + moving_chains).c_str());

        // Ranked by correlation, the unscored models last
        std::vector<size_t> order(models.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [this](size_t a, size_t b) {
            if (models[a].scored != models[b].scored) return models[a].scored;
            return models[a].scored && models[a].score > models[b].score;
        });

        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("##models", 6, flags, ImVec2(0, 200))) {
            ImGui::TableSetupColumn("Model");
            ImGui::TableSetupColumn("Rank");
            ImGui::TableSetupColumn("Correlation");
            ImGui::TableSetupColumn("Points");
            ImGui::TableSetupColumn("Local fit mean");
            ImGui::TableSetupColumn("RMSD chain A (A)");
            ImGui::TableHeadersRow();
            for (size_t k = 0; k < order.size(); ++k) {
                const size_t i = order[k];
                const ModelFit& m = models[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const std::string label = model_name(*app_state, i) + "##" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), (int64_t)i == frame, ImGuiSelectableFlags_SpanAllColumns)) {
                    select_frame(*app_state, (int64_t)i);
                }
                ImGui::TableNextColumn();
                if (m.scored) ImGui::Text("%zu", k + 1);
                else ImGui::TextDisabled("-");
                ImGui::TableNextColumn();
                if (m.scored) ImGui::Text("%.4f", m.score);
                else ImGui::TextDisabled("-");
                ImGui::TableNextColumn();
                if (m.scored) ImGui::Text("%zu", m.points);
                else ImGui::TextDisabled("-");
                ImGui::TableNextColumn();
                if (m.fitted) ImGui::Text("%.5f", m.fit_mean);
                else ImGui::TextDisabled("-");
                ImGui::TableNextColumn();
                if (m.aligned) ImGui::Text("%.3f", m.rmsd);
                else ImGui::TextDisabled("-");
            }
            ImGui::EndTable();
        }

        ImGui::Separator();
        const ModelFit& cur = models[frame];
        ImGui::Text("Model: %s", model_name(*app_state, frame).c_str());
        if (cur.scored) ImGui::Text("Correlation %.4f over %zu points", cur.score, cur.points);
        else ImGui::TextDisabled("Not scored");
        if (cur.aligned) ImGui::Text("Chain A C-alpha RMSD to the first model %.3f A", cur.rmsd);
        else ImGui::TextDisabled("Not superposed");
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

    void draw_window() {
        hovered = -1;
        if (clashes_searched && current_frame(*app_state) != clash_frame) clear_clashes();
        if (!show_window) {
            update_highlight();
            return;
        }
        if (ImGui::Begin("Haddock", &show_window, ImGuiWindowFlags_NoFocusOnAppearing)) {
            if (ImGui::BeginTabBar("##haddock")) {
                if (ImGui::BeginTabItem("Restraints")) {
                    draw_restraints();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Molecules")) {
                    draw_molecules();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Map fit")) {
                    draw_map_fit();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
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
    instance.resolve(state->mold.sys);
    instance.show_window = true;

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
