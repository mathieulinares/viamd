// Haddock restraints: the distance restraints of a CNS table (the "assign" lines of a HADDOCK .tbl file), drawn between
// their two atoms. A restraint is satisfied when the distance lies in [d - dminus, d + dplus]. Each atom is matched to
// the system by segid (chain), resid and name, when the table is loaded and when a system is initialized. A restraint
// whose atoms are not in the system is listed but not drawn.

#include <core/md_log.h>
#include <core/md_str.h>
#include <core/md_file.h>
#include <core/md_arena_allocator.h>

#include <md_system.h>
#include <md_haddock.h>

#include <viamd_event.h>
#include <window_menu.h>
#include <viamd.h>
#include <event.h>
#include <gfx/immediate_draw_utils.h>
#include <components/haddock/haddock.h>

#include <imgui.h>

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

struct Haddock : viamd::EventHandler {
    ApplicationState* app_state = nullptr;
    std::vector<Restraint> restraints;
    std::string path;
    bool show_window = false;
    bool show_lines = true;
    int hovered = -1;           // restraint under the mouse in the table, -1 when none
    bool highlighting = false;  // the shared highlight mask is set by this component

    Haddock() { viamd::event_system_register_handler(*this); }

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
                workspace_register_window("Haddock Restraints", &show_window);
                break;
            case viamd::EventType_ViamdSystemInit:
                if (app_state) resolve(app_state->mold.sys);
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
                const int32_t state_key[2] = {hovered, show_lines};
                rk->key = md_hash64(state_key, sizeof(state_key), rk->key);
                break;
            }
            case viamd::EventType_ViamdWindowMenuRequest:
                viamd::window_menu_add(e, "Analysis/Haddock Restraints", &show_window, {.order = 40});
                break;
            default:
                break;
            }
        }
    }

    void draw_lines(const ApplicationState& state) {
        immediate::Scope scope(state.gfx.overlay, "haddock_restraints");
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

    void draw_window() {
        hovered = -1;
        if (!show_window) {
            update_highlight();
            return;
        }
        if (ImGui::Begin("Haddock Restraints", &show_window, ImGuiWindowFlags_NoFocusOnAppearing)) {
            ImGui::Checkbox("Show lines", &show_lines);
            if (restraints.empty()) {
                ImGui::TextDisabled("Drop a CNS restraint table (.tbl) to show its distance restraints.");
            } else {
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
