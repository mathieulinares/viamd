#include "lavery_core.h"

#include <core/md_allocator.h>
#include <core/md_array.h>
#include <core/md_bitset.h>
#include <core/md_log.h>
#include <core/md_os.h>
#include <core/md_str.h>
#include <md_system.h>
#include <md_util.h>

#include <viamd_event.h>
#include <viamd.h>
#include <event.h>
#include <task_system.h>
#include <serialization_utils.h>

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {

using namespace lavery;

static const char* metric_unit(Metric metric) {
    return metric_is_angular(metric) ? "deg" : "Å";
}

static vec3_t min_image(vec3_t delta, const md_unitcell_t& cell) {
    md_util_min_image_vec3(&delta, 1, &cell);
    return delta;
}

static bool make_extract_state(md_system_state_t* state, size_t atom_count, md_allocator_i* alloc) {
    *state = {};
    state->alloc = alloc;
    return md_system_state_init(state, atom_count);
}

struct Lavery : viamd::EventHandler {
    ApplicationState* app = nullptr;
    bool show_window = false;
    bool show_heatmap = true;
    bool sync_viewport = true;
    float outlier_sigma = 2.5f;
    int metric = (int)Twist;
    int member = 0;
    bool highlight_owned = false;
    uint64_t highlight_hash = 0;
    bool compare_all = false;
    bool aggregate_outliers = false;
    uint32_t selected_parameters = 63;
    uint32_t counts_parameters = 0;
    std::vector<uint16_t> pair_quality_flags;
    std::vector<uint16_t> step_quality_flags;
    std::vector<OutlierEvent> outlier_events;
    bool reset_heatmap_view = true;
    bool focus_heatmap_selection = false;
    ImPlotRect heatmap_limits = {};
    double heatmap_frame = -1.0;
    bool heatmap_pressed = false;
    bool heatmap_dragged = false;
    int heatmap_member_count = 0;
    std::vector<FrameOutliers> outlier_counts;
    int outlier_counts_metric = -1;
    float outlier_counts_sigma = -1;
    ImGuiID scrubbing_plot = 0;
    int distribution_metric = -1;
    int distribution_member = -1;
    std::vector<float> distribution;
    std::vector<std::string> pair_labels;
    std::vector<std::string> step_labels;

    std::vector<BaseTopology> bases;
    std::vector<Pair> pairs;
    std::vector<StepIndex> steps;
    std::array<std::vector<float>, Metric_Count> values;
    std::array<MetricStatistics, Metric_Count> statistics;
    std::array<size_t, Metric_Count> columns = {};
    std::atomic<bool> calculation_ready = false;
    std::atomic<bool> calculation_ok = false;
    task_system::ID calculation = task_system::INVALID_ID;
    bool calculating = false;
    bool computed = false;
    std::string run;

    Lavery() { viamd::event_system_register_handler(*this); }

    void clear_highlight() {
        if (highlight_owned && app) {
            const bool unchanged = md_bitset_hash64(md_bitmask_view(&app->selection.highlight_mask), 0) == highlight_hash;
            highlight_owned = false;
            if (unchanged) {
                md_bitmask_clear(&app->selection.highlight_mask);
                viamd::event_system_broadcast_event(viamd::EventType_ViamdHighlightMaskChanged,
                    viamd::EventPayloadType_ApplicationState, app);
            }
        }
    }

    void reset() {
        clear_highlight();
        bases.clear();
        pairs.clear();
        steps.clear();
        for (auto& v : values) v.clear();
        for (auto& s : statistics) s = {};
        pair_labels.clear();
        step_labels.clear();
        distribution.clear();
        distribution_metric = distribution_member = -1;
        reset_heatmap_view = true;
        focus_heatmap_selection = false;
        heatmap_limits = {};
        heatmap_frame = -1.0;
        heatmap_pressed = heatmap_dragged = false;
        heatmap_member_count = 0;
        outlier_counts.clear();
        pair_quality_flags.clear();
        step_quality_flags.clear();
        outlier_events.clear();
        counts_parameters = 0;
        outlier_counts_metric = -1;
        outlier_counts_sigma = -1;
        scrubbing_plot = 0;
        columns.fill(0);
        computed = false;
        calculating = false;
        calculation_ready = false;
        calculation_ok = false;
        run.clear();
    }

    void process_events(const viamd::Event* events, size_t count) final {
        for (size_t i = 0; i < count; ++i) {
            const viamd::Event& e = events[i];
            switch (e.type) {
            case viamd::EventType_ViamdInitialize:
                app = (ApplicationState*)e.payload;
                workspace_register_window("Lavery", &show_window);
                break;
            case viamd::EventType_ViamdWindowDrawMenu:
                ImGui::Checkbox("Lavery", &show_window);
                break;
            case viamd::EventType_ViamdFrameTick:
                if (app && run != app->mold.run) reset();
                complete_if_ready();
                if (show_window) {
                    maybe_start();
                    draw();
                } else {
                    clear_highlight();
                }
                break;
            case viamd::EventType_ViamdSystemInit:
            case viamd::EventType_ViamdSystemFree:
            case viamd::EventType_ViamdTrajectoryInit:
            case viamd::EventType_ViamdTrajectoryFree:
                reset();
                break;
            case viamd::EventType_ViamdShutdown:
                clear_highlight();
                break;
            case viamd::EventType_ViamdSerialize: {
                auto& state = *(viamd::serialization_state_t*)e.payload;
                viamd::write_section_header(state, STR_LIT("Lavery"));
                viamd::write_bool(state, STR_LIT("ShowWindow"), show_window);
                viamd::write_bool(state, STR_LIT("ShowHeatmap"), show_heatmap);
                viamd::write_bool(state, STR_LIT("SyncViewport"), sync_viewport);
                viamd::write_bool(state, STR_LIT("CompareAll"), compare_all);
                viamd::write_bool(state, STR_LIT("AggregateOutliers"), aggregate_outliers);
                viamd::write_int(state, STR_LIT("SelectedParameters"), (int)selected_parameters);
                viamd::write_flt(state, STR_LIT("OutlierSigma"), outlier_sigma);
                viamd::write_int(state, STR_LIT("Metric"), metric);
                viamd::write_int(state, STR_LIT("Member"), member);
                break;
            }
            case viamd::EventType_ViamdDeserializeBegin:
                show_heatmap = true;
                sync_viewport = true;
                outlier_sigma = 2.5f;
                metric = (int)Twist;
                member = 0;
                compare_all = false;
                aggregate_outliers = false;
                selected_parameters = 63;
                break;
            case viamd::EventType_ViamdDeserialize: {
                auto& state = *(viamd::deserialization_state_t*)e.payload;
                if (!str_eq(viamd::section_header(state), STR_LIT("Lavery"))) break;
                str_t key, arg;
                while (viamd::next_entry(key, arg, state)) {
                    if (str_eq(key, STR_LIT("ShowWindow"))) viamd::extract_bool(show_window, arg);
                    else if (str_eq(key, STR_LIT("ShowHeatmap"))) viamd::extract_bool(show_heatmap, arg);
                    else if (str_eq(key, STR_LIT("SyncViewport"))) viamd::extract_bool(sync_viewport, arg);
                    else if (str_eq(key, STR_LIT("CompareAll"))) viamd::extract_bool(compare_all, arg);
                    else if (str_eq(key, STR_LIT("AggregateOutliers"))) viamd::extract_bool(aggregate_outliers, arg);
                    else if (str_eq(key, STR_LIT("SelectedParameters"))) {
                        int mask = 63;
                        viamd::extract_int(mask, arg);
                        selected_parameters = (uint32_t)mask & 63;
                    }
                    else if (str_eq(key, STR_LIT("OutlierSigma"))) viamd::extract_flt(outlier_sigma, arg);
                    else if (str_eq(key, STR_LIT("Metric"))) viamd::extract_enum(metric, arg, Metric_Count);
                    else if (str_eq(key, STR_LIT("Member"))) viamd::extract_int(member, arg);
                }
                break;
            }
            default:
                break;
            }
        }
    }

    void maybe_start() {
        const std::string current_run = app ? app->mold.run : "";
        if (!run.empty() && run != current_run) reset();
        if (!app || calculating || computed || app->mold.run[0] == '\0') return;
        const size_t frame_count = run_num_frames(app);
        if (frame_count == 0 || app->mold.sys.nucleic_backbone.segment.count == 0) return;
        if (frame_count > UINT32_MAX) {
            MD_LOG_ERROR("Lavery: trajectory has too many frames for attribute indexing");
            computed = true;
            return;
        }
        start_calculation(frame_count);
    }

    void start_calculation(size_t frame_count) {
        const md_system_t* sys = &app->mold.sys;
        run = app->mold.run;
        bases = gather_bases(*sys);
        const size_t unsupported = (size_t)std::count_if(bases.begin(), bases.end(), [](const BaseTopology& base) {
            return base.reference < 0 || base.fit_atoms.empty();
        });
        if (unsupported) {
            MD_LOG_ERROR("Lavery: %zu nucleotides lack a supported standard-base fit and will not be analyzed", unsupported);
        }
        if (bases.size() < 2) {
            computed = true;
            MD_LOG_INFO("Lavery: no nucleobase ring frames found");
            return;
        }

        md_temp_scope_t temp = md_temp_begin();
        md_allocator_i* alloc = md_temp_allocator(temp);
        md_system_state_t first_state = {};
        if (!make_extract_state(&first_state, sys->atom.count, alloc)) {
            md_temp_end(temp);
            MD_LOG_ERROR("Lavery: could not allocate coordinates for the first frame");
            computed = true;
            return;
        }
        const str_t paths[] = {STR_LIT("atom/position"), STR_LIT("unitcell")};
        md_system_extract_t* ex = md_system_extract_begin(sys, str_from_cstr(run.c_str()), paths, ARRAY_SIZE(paths), md_get_heap_allocator());
        const bool extracted = ex && md_system_extract_frame(ex, 0, &first_state);
        if (ex) md_system_extract_end(ex);
        if (!extracted) {
            md_system_state_free(&first_state);
            md_temp_end(temp);
            MD_LOG_ERROR("Lavery: could not read frame 0 to identify base pairs");
            computed = true;
            return;
        }
        std::vector<BaseFrame> first_bases;
        if (!extract_base_frames(first_bases, *sys, first_state, bases)) {
            md_system_state_free(&first_state);
            md_temp_end(temp);
            MD_LOG_ERROR("Lavery: could not calculate nucleobase frames in the first trajectory frame");
            computed = true;
            return;
        }
        pairs.resize(bases.size());
        pairs.resize(detect_pairs(pairs.data(), pairs.size(), first_bases.data(), first_bases.size(), &first_state.unitcell));
        md_system_state_free(&first_state);
        md_temp_end(temp);
        if (pairs.empty()) {
            computed = true;
            MD_LOG_INFO("Lavery: no geometrically paired bases found in frame 0");
            return;
        }
        steps = gather_steps(*sys, bases, pairs.data(), pairs.size());
        auto base_label = [&](uint32_t index) {
            const auto& base = bases[index];
            const str_t name = md_component_name(&sys->component, base.component);
            char buf[128];
            snprintf(buf, sizeof(buf), "S%u:" STR_FMT "%d", base.strand + 1,
                STR_ARG(name), (int)md_system_component_seq_id(sys, base.component));
            return std::string(buf);
        };
        for (size_t p = 0; p < pairs.size(); ++p) {
            pair_labels.push_back(std::to_string(p + 1) + ": " +
                base_label(pairs[p].a) + " / " + base_label(pairs[p].b));
        }
        for (auto step : steps) {
            step_labels.push_back(std::to_string(step.a + 1) + "-" + std::to_string(step.b + 1) +
                ": " + base_label(pairs[step.a].a) + " -> " + base_label(pairs[step.b].a));
        }
        const size_t bp_count = pairs.size();
        const size_t step_count = steps.size();
        pair_quality_flags.assign(frame_count * bp_count, Quality_InvalidBase);
        step_quality_flags.assign(frame_count * step_count, Quality_AdjacentPair);
        columns.fill(0);
        for (int m = 0; m < (int)Metric_Count; ++m) {
            columns[m] = metric_is_step((Metric)m) ? step_count : bp_count;
            values[m].assign(frame_count * columns[m], std::numeric_limits<float>::quiet_NaN());
        }

        calculation_ready = false;
        calculation_ok = false;
        calculating = true;
        calculation = task_system::create_pool_task(STR_LIT("Lavery trajectory analysis"), [this, sys, frame_count]() {
            md_temp_scope_t temp_scope = md_temp_begin();
            md_allocator_i* alloc = md_temp_allocator(temp_scope);
            md_system_state_t state = {};
            if (!make_extract_state(&state, sys->atom.count, alloc)) {
                calculation_ok = false;
                calculation_ready = true;
                md_temp_end(temp_scope);
                return;
            }

            const str_t paths[] = {STR_LIT("atom/position"), STR_LIT("unitcell")};
            md_system_extract_t* ex = md_system_extract_begin(sys, str_from_cstr(run.c_str()), paths, ARRAY_SIZE(paths), md_get_heap_allocator());
            if (!ex) {
                md_system_state_free(&state);
                md_temp_end(temp_scope);
                calculation_ok = false;
                calculation_ready = true;
                return;
            }
            std::vector<BaseFrame> frame_bases;
            std::vector<uint16_t> base_quality;
            std::vector<BasePairFrame> pair_frames(pairs.size());
            std::vector<Values> bp_values(pairs.size());
            std::vector<Values> step_values(steps.size());
            bool ok = true;
            for (size_t frame = 0; frame < frame_count; ++frame) {
                if (!md_system_extract_frame(ex, (int64_t)frame, &state)) {
                    ok = false;
                    break;
                }
                if (!extract_base_frames(frame_bases, *sys, state, bases, &base_quality)) {
                    continue;
                }
                std::fill(pair_frames.begin(), pair_frames.end(), BasePairFrame{});
                for (size_t p = 0; p < pairs.size(); ++p) {
                    BaseFrame a = frame_bases[pairs[p].a];
                    BaseFrame b = frame_bases[pairs[p].b];
                    auto& quality = pair_quality_flags[frame * pairs.size() + p];
                    const bool present = compute_present_base_pair(&bp_values[p], &pair_frames[p], &quality,
                        a, b, &state.unitcell);
                    quality |= base_quality[pairs[p].a] | base_quality[pairs[p].b];
                    if (present) {
                        for (int m = 0; m < (int)Tilt; ++m) values[m][frame * columns[m] + p] = bp_values[p].metric[m];
                    }
                }
                for (size_t s = 0; s < steps.size(); ++s) {
                    auto& quality = step_quality_flags[frame * steps.size() + s];
                    quality = pair_quality_flags[frame * pairs.size() + steps[s].a] |
                        pair_quality_flags[frame * pairs.size() + steps[s].b];
                    if (quality != Quality_Good) {
                        quality |= Quality_AdjacentPair;
                        continue;
                    }
                    BasePairFrame a = pair_frames[steps[s].a];
                    BasePairFrame b = pair_frames[steps[s].b];
                    b.origin = vec3_add(a.origin, min_image(vec3_sub(b.origin, a.origin), state.unitcell));
                    if (compute_step(&step_values[s], a, b)) {
                        for (int m = (int)Tilt; m < (int)Metric_Count; ++m) values[m][frame * columns[m] + s] = step_values[s].metric[m];
                    } else quality |= Quality_AmbiguousTransform;
                }
            }
            md_system_extract_end(ex);
            md_system_state_free(&state);
            md_temp_end(temp_scope);
            if (ok) {
                for (int m = 0; m < (int)Metric_Count; ++m) {
                    if (!compute_statistics(&statistics[m], values[m], columns[m])) {
                        ok = false;
                        break;
                    }
                }
            }
            calculation_ok = ok;
            calculation_ready = true;
        });
        task_system::enqueue_task(calculation);
    }

    void complete_if_ready() {
        if (!calculating || !calculation_ready.load()) return;
        calculating = false;
        if (!calculation_ok.load()) {
            MD_LOG_ERROR("Lavery: trajectory analysis failed while extracting frames");
            computed = true;
            return;
        }
        publish_attributes();
        computed = true;
    }

    void publish_attributes() {
        if (!app || run.empty()) return;
        md_attributes_t* table = &app->mold.sys.attributes;
        char prefix_buf[256];
        const int prefix_len = snprintf(prefix_buf, sizeof(prefix_buf), "%s/lavery", run.c_str());
        if (prefix_len <= 0 || (size_t)prefix_len >= sizeof(prefix_buf)) {
            MD_LOG_ERROR("Lavery: attribute prefix is too long");
            return;
        }
        const str_t prefix = str_from_cstr(prefix_buf);
        md_attributes_remove_prefix(table, prefix);
        char path[256];
        for (int m = 0; m < (int)Metric_Count; ++m) {
            const Metric metric_id = (Metric)m;
            const size_t n = columns[m];
            if (n == 0) continue;
            const int len = snprintf(path, sizeof(path), "%s/lavery/%s", run.c_str(), metric_path(metric_id));
            if (len <= 0 || (size_t)len >= sizeof(path)) {
                MD_LOG_ERROR("Lavery: attribute path is too long");
                md_attributes_remove_prefix(table, prefix);
                return;
            }
            const md_attribute_desc_t desc = {
                .path = str_from_cstr(path),
                .format = {
                    .type = MD_ATTRIBUTE_TYPE_F32, .components = 1,
                    .rank = 2, .shape = {(uint32_t)run_num_frames(app), (uint32_t)n},
                },
                .flags = MD_ATTRIBUTE_FLAG_TEMPORAL,
                .unit = metric_is_angular(metric_id) ? md_unit_degree() : md_unit_angstrom(),
                .label = str_from_cstr(metric_name(metric_id)),
                .description = STR_INIT("Tsukuba fitted-base frame, Curves+ rotation-vector/mid-frame convention; Watson-Crick pairs fixed from frame 0"),
                .data = values[m].data(),
                .byte_size = values[m].size() * sizeof(float),
            };
            const md_attribute_id_t id = md_attributes_replace(table, &desc);
            if (id == MD_ATTRIBUTE_INVALID) {
                MD_LOG_ERROR("Lavery: failed to publish " STR_FMT, STR_ARG(desc.path));
                md_attributes_remove_prefix(table, prefix);
                return;
            }
            char alias[128];
            const int alias_len = snprintf(alias, sizeof(alias), "lavery/%s", metric_path(metric_id));
            if (alias_len <= 0 || (size_t)alias_len >= sizeof(alias) ||
                md_attributes_alias(table, id, str_from_cstr(alias), str_from_cstr(metric_name(metric_id)), desc.description) == MD_ATTRIBUTE_INVALID) {
                MD_LOG_ERROR("Lavery: failed to publish script alias for " STR_FMT, STR_ARG(desc.path));
                md_attributes_remove_prefix(table, prefix);
                return;
            }
        }
        app->script.compile_ir = true;
    }

    void draw() {
        if (!ImGui::GetIO().KeyCtrl || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) scrubbing_plot = 0;
        const ImVec2 work_size = ImGui::GetMainViewport()->WorkSize;
        ImGui::SetNextWindowSize({std::min(1000.0f, work_size.x), std::min(1100.0f, work_size.y)}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Lavery", &show_window, ImGuiWindowFlags_NoFocusOnAppearing)) {
            clear_highlight();
            ImGui::End();
            return;
        }

        if (!app || app->mold.sys.nucleic_backbone.segment.count == 0) {
            ImGui::TextDisabled("Load a structure with nucleic-acid residues to analyze Lavery parameters.");
            ImGui::End();
            return;
        }
        if (app->mold.run[0] == '\0') {
            ImGui::TextDisabled("Load a trajectory to compute per-frame Lavery parameters.");
            ImGui::End();
            return;
        }
        if (calculating) {
            ImGui::Text("Analyzing trajectory...");
            ImGui::End();
            return;
        }
        if (!computed) {
            ImGui::TextDisabled("Preparing trajectory analysis...");
            ImGui::End();
            return;
        }
        if (pairs.empty()) {
            ImGui::TextDisabled("No geometric base pairs were found in the first frame.");
            ImGui::End();
            return;
        }

        if (!calculation_ok.load()) {
            ImGui::TextUnformatted("Trajectory analysis failed. See the application log for details.");
            clear_highlight();
            ImGui::End();
            return;
        }

        ImGui::Text("%zu base pairs, %zu consecutive steps; fixed from frame 0", pairs.size(), steps.size());
        ImGui::TextDisabled("%zu nucleotides identified from VIAMD backbone ranges and bonded base rings", bases.size());
        ImGui::SetItemTooltip("The first-frame pair identities are retained throughout the trajectory. Unpaired or degenerate frames are NaN.");
        ImGui::TextDisabled("Fitted Tsukuba frames; Curves+ intra/step conventions (canonical A/G/C/T/U, Watson-Crick orientation).");
        ImGui::TextDisabled("Experimental: validated on the nucleosome reference; helical-axis analysis is not implemented.");
        ImGui::TextDisabled("Script: twist = attr(\"lavery/step/twist\");  rise = attr(\"lavery/step/rise\");");
        ImGui::Checkbox("Heatmap", &show_heatmap);
        ImGui::SameLine();
        ImGui::Checkbox("Highlight all outliers", &sync_viewport);
        ImGui::SetItemTooltip("Highlights every outlying pair or step for the active parameter at the current frame. Uses VIAMD's highlight style, not an emissive glow.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        ImGui::SliderFloat("Tolerance", &outlier_sigma, 1.5f, 4.0f, "%.1f sigma");

        if (ImGui::BeginTabBar("LaveryMetrics")) {
            if (ImGui::BeginTabItem("Intra-base pair")) {
                draw_metric_group(false);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Inter-base-pair step")) {
                draw_metric_group(true);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }

    void draw_metric_group(bool step_metric) {
        static const char* bp_items[] = {"Buckle", "Propeller Twist", "Opening", "Shear", "Stretch", "Stagger"};
        static const char* step_items[] = {"Tilt", "Roll", "Twist", "Shift", "Slide", "Rise"};
        const int offset = step_metric ? (int)Tilt : (int)Buckle;
        int local_metric = std::clamp(metric - offset, 0, 5);
        ImGui::Combo("Parameter", &local_metric, step_metric ? step_items : bp_items, 6);
        metric = offset + local_metric;
        const int count = (int)columns[metric];
        if (count <= 0) {
            ImGui::TextDisabled("No consecutive base-pair steps were found.");
            clear_highlight();
            return;
        }
        member = std::clamp(member, 0, count - 1);
        ImGui::SetNextItemWidth(160);
        int member_number = member + 1;
        if (ImGui::InputInt(step_metric ? "Step" : "Base pair", &member_number)) {
            member = std::clamp(member_number, 1, count) - 1;
            focus_heatmap_selection = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", metric_unit((Metric)metric));
        const auto& labels = step_metric ? step_labels : pair_labels;
        ImGui::TextUnformatted(labels[member].c_str());
        ImGui::Checkbox("Outlier in any selected parameter", &aggregate_outliers);
        if (aggregate_outliers) {
            for (int k = 0; k < 6; ++k) {
                bool selected = (selected_parameters & (1u << k)) != 0;
                if (k % 3) ImGui::SameLine();
                if (ImGui::Checkbox(metric_name((Metric)(offset + k)), &selected)) {
                    if (selected) selected_parameters |= 1u << k;
                    else selected_parameters &= ~(1u << k);
                }
            }
            if (!selected_parameters) ImGui::TextDisabled("Select at least one parameter to detect outliers.");
        }

        const size_t frame_count = run_num_frames(app);
        const std::vector<float>& data = values[metric];
        if (data.size() < frame_count * (size_t)count) return;
        const ImPlotInputMap saved_input_map = ImPlot::GetInputMap();
        if (ImGui::GetIO().KeyCtrl) ImPlot::GetInputMap().Pan = ImGuiMouseButton_Middle;
        const auto& stats = statistics[metric];
        const size_t current_frame = (size_t)std::clamp((int64_t)std::llround(app->animation.frame),
            (int64_t)0, (int64_t)frame_count - 1);
        const float current_value = data[current_frame * count + member];
        if (std::isfinite(current_value)) {
            ImGui::Text("Frame %zu: %.3f %s (%+.2f sigma)", current_frame, current_value,
                metric_unit((Metric)metric), stats.members[member].zscore(current_value));
        } else {
            ImGui::Text("Frame %zu: selected member has no valid sample", current_frame);
        }
        draw_outlier_timeline(count, frame_count);
        ImGui::BeginChild("##analysis_details", ImVec2(0, 0));
        if (show_heatmap) {
            ImGui::TextDisabled("All %s; colors = z-score (-%.1f to +%.1f).",
                step_metric ? "steps" : "base pairs", outlier_sigma, outlier_sigma);
            draw_heatmap(count, frame_count, stats.heatmap, &member);
        }
        const int hovered_outlier = draw_current_outliers(count, frame_count);
        if (sync_viewport || hovered_outlier >= 0) {
            update_outlier_highlight(count, frame_count, hovered_outlier);
        } else {
            clear_highlight();
        }

        const float* times = timeline_times(frame_count);
        char time_label[64];
        timeline_label(time_label, sizeof(time_label), times);
        ImGui::TextDisabled("Ctrl-click / Ctrl-drag: jump / scrub time. Drag / wheel: pan / zoom.");
        if (ImGui::CollapsingHeader("Selected-member timeline")) {
        ImGui::Checkbox("Compare all members in timeline", &compare_all);
        struct TimelineData {
            const float* values;
            const float* times;
            int stride;
        };
        ImGui::PushID(time_label);
        if (ImPlot::BeginPlot("Timeline", ImVec2(-1, 220))) {
            ImPlot::SetupAxes(time_label, metric_unit((Metric)metric));
            const double first_x = times ? times[0] : 0;
            const double last_x = times ? times[frame_count - 1] : (double)frame_count - 1;
            ImPlot::SetupAxisLimits(ImAxis_X1, first_x, std::max(first_x + 1.0, last_x), ImPlotCond_Once);
            const int first = compare_all ? 0 : member;
            const int end = compare_all ? count : member + 1;
            for (int m = first; m < end; ++m) {
                TimelineData timeline_data = {data.data() + (size_t)m, times, count};
                ImPlot::PlotLineG(labels[m].c_str(), [](int idx, void* user_data) {
                    const auto* payload = (const TimelineData*)user_data;
                    return ImPlotPoint(payload->times ? (double)payload->times[idx] : (double)idx,
                        payload->values[(size_t)idx * payload->stride]);
                }, &timeline_data, (int)frame_count);
            }
            scrub_timeline("##parameter_scrub", times, frame_count);
            draw_time_cursor(times, frame_count);
            ImPlot::EndPlot();
        }
        ImGui::PopID();
        }

        if (distribution_metric != metric || distribution_member != member) {
            distribution.clear();
            distribution.reserve(frame_count);
            for (size_t f = 0; f < frame_count; ++f) {
                const float v = data[f * count + member];
                if (std::isfinite(v)) distribution.push_back(v);
            }
            distribution_metric = metric;
            distribution_member = member;
        }
        const auto& member_stats = stats.members[member];
        if (ImGui::CollapsingHeader("Selected-member distribution") && member_stats.count) {
            ImGui::Text("Mean %.3f  SD %.3f  valid %zu/%zu",
                member_stats.mean, member_stats.sd, member_stats.count, frame_count);
            if (ImPlot::BeginPlot("Distribution", ImVec2(-1, 180))) {
                ImPlot::SetupAxes(metric_unit((Metric)metric), "Count");
                ImPlot::PlotHistogram("##distribution", distribution.data(), (int)distribution.size(), ImPlotBin_Sturges);
                const size_t frame = (size_t)std::clamp((int64_t)std::llround(app->animation.frame),
                    (int64_t)0, (int64_t)frame_count - 1);
                const float value = data[frame * count + member];
                if (std::isfinite(value)) ImPlot::PlotInfLines("Current value", &value, 1);
                ImPlot::EndPlot();
            }
        } else if (!member_stats.count) {
            ImGui::TextDisabled("No finite values for this member; other members remain available below.");
        }
        ImPlot::GetInputMap() = saved_input_map;
        ImGui::EndChild();
    }

    const float* timeline_times(size_t frame_count) const {
        return md_array_size(app->timeline.x_values) == frame_count && !md_unit_is_none(app->timeline.time_unit)
            ? app->timeline.x_values : nullptr;
    }

    uint32_t outlier_parameter_mask() const {
        return aggregate_outliers ? selected_parameters : 1u << (metric % 6);
    }

    OutlierAssessment assess(size_t frame, size_t index) const {
        const int offset = metric_is_step((Metric)metric) ? (int)Tilt : 0;
        float sample[6];
        MemberStatistics baselines[6];
        for (int k = 0; k < 6; ++k) {
            sample[k] = values[offset + k][frame * columns[offset + k] + index];
            baselines[k] = statistics[offset + k].members[index];
        }
        return assess_outliers(sample, baselines, 6, outlier_parameter_mask(), outlier_sigma);
    }

    uint16_t quality_at(size_t frame, size_t index) const {
        return metric_is_step((Metric)metric) ? step_quality_flags[frame * steps.size() + index]
            : pair_quality_flags[frame * pairs.size() + index];
    }

    void draw_quality(uint16_t flags) const {
        if (!flags) ImGui::TextUnformatted("Quality: good");
        for (uint16_t bit = 1; bit <= Quality_MissingAtoms; bit <<= 1) {
            if (flags & bit) ImGui::TextWrapped("%s", quality_description(bit));
        }
    }

    void timeline_label(char* buffer, size_t size, const float* times) const {
        if (times) {
            char unit[32] = {};
            md_unit_print(unit, sizeof(unit), app->timeline.time_unit);
            snprintf(buffer, size, "Time (%s)", unit);
        } else {
            snprintf(buffer, size, "Frame");
        }
    }

    bool scrub_timeline(const char* id, const float* times, size_t frame_count) {
        const ImGuiID plot = ImGui::GetID(id);
        const bool ctrl = ImGui::GetIO().KeyCtrl;
        if (!ctrl || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) scrubbing_plot = 0;
        if (ctrl && ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            scrubbing_plot = plot;
        }
        if (scrubbing_plot != plot) return false;
        app->animation.mode = PlaybackMode::Stopped;
        app->animation.frame = (double)nearest_frame(ImPlot::GetPlotMousePos().x, times, frame_count);
        return true;
    }

    void draw_time_cursor(const float* times, size_t frame_count) const {
        const double frame = std::clamp(app->animation.frame, 0.0, (double)frame_count - 1);
        const size_t f0 = (size_t)frame;
        const size_t f1 = std::min(f0 + 1, frame_count - 1);
        const double x = times ? times[f0] + ((double)times[f1] - times[f0]) * (frame - (double)f0) : frame;
        ImPlot::SetNextLineStyle(ImVec4(1, 0.8f, 0, 1), 2);
        ImPlot::PlotInfLines("Current frame", &x, 1);
    }

    void draw_outlier_timeline(int count, size_t frame_count) {
        const uint32_t mask = outlier_parameter_mask();
        if (outlier_counts_metric != metric || outlier_counts_sigma != outlier_sigma || counts_parameters != mask) {
            outlier_counts.assign(frame_count, {});
            std::vector<float> severities(frame_count);
            for (size_t f = 0; f < frame_count; ++f) {
                float severity = 0;
                for (int m = 0; m < count; ++m) {
                    const auto result = assess(f, (size_t)m);
                    if (result.valid) ++outlier_counts[f].valid;
                    if (result.contributors) {
                        ++outlier_counts[f].outliers;
                        severity = std::max(severity, std::fabs(result.peak_z));
                    }
                }
                severities[f] = severity;
            }
            outlier_events = collect_outlier_events(outlier_counts, severities);
            outlier_counts_metric = metric;
            outlier_counts_sigma = outlier_sigma;
            counts_parameters = mask;
        }
        const float* times = timeline_times(frame_count);
        char x_label[64];
        timeline_label(x_label, sizeof(x_label), times);
        auto x_at = [&](size_t f) { return times ? (double)times[f] : (double)f; };
        ImGui::Text("Outlier count: %s, %.1f sigma (all %s)", aggregate_outliers ? "any selected parameter" : metric_name((Metric)metric),
            outlier_sigma, metric_is_step((Metric)metric) ? "steps" : "base pairs");
        const size_t selected_frame = (size_t)std::clamp((int64_t)std::llround(app->animation.frame),
            (int64_t)0, (int64_t)frame_count - 1);
        const auto& selected_counts = outlier_counts[selected_frame];
        ImGui::Text("Frame %zu: %zu outliers / %zu valid", selected_frame,
            selected_counts.outliers, selected_counts.valid);
        size_t flagged = 0;
        for (int m = 0; m < count; ++m) if (quality_at(selected_frame, (size_t)m)) ++flagged;
        ImGui::Text("Coverage %.1f%%; %zu/%d unavailable due to pairing loss or invalid geometry",
            100.0 * selected_counts.valid / count, flagged, count);
        if (ImGui::Button("Previous outlier frame")) jump_outlier(selected_frame, false);
        ImGui::SameLine();
        if (ImGui::Button("Next outlier frame")) jump_outlier(selected_frame, true);
        ImGui::SameLine();
        if (ImGui::Button("Export CSV")) export_csv(frame_count);
        if (ImGui::CollapsingHeader("Outlier events")) {
            ImGui::BeginChild("##events", ImVec2(0, 130), ImGuiChildFlags_Borders);
            for (const auto& event : outlier_events) {
                ImGui::PushID((int)event.begin);
                char label[256];
                snprintf(label, sizeof(label), "Frames %zu-%zu (%zu frames), duration %.4g; peak %.2f sigma @ %zu; max %zu outliers",
                    event.begin, event.end, event.end - event.begin + 1,
                    x_at(event.end) - x_at(event.begin), event.severity, event.peak, event.peak_count);
                if (ImGui::Selectable(label)) {
                    app->animation.frame = (double)event.peak;
                    app->animation.mode = PlaybackMode::Stopped;
                    focus_heatmap_selection = true;
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::Text("%zu events; duration measured in %s", outlier_events.size(), x_label);
        }
        ImGui::TextDisabled("Ctrl-click / Ctrl-drag: jump / scrub. Hover for valid counts; missing samples excluded.");
        ImGui::PushID(metric);
        ImGui::PushID(x_label);
        if (ImPlot::BeginPlot("Outliers over time", ImVec2(-1, 180), ImPlotFlags_NoLegend)) {
            ImPlot::SetupAxes(x_label, "Outliers");
            ImPlot::SetupAxisLimits(ImAxis_X1, x_at(0), std::max(x_at(0) + 1.0, x_at(frame_count - 1)), ImPlotCond_Once);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, count, ImPlotCond_Always);
            struct Payload {
                const std::vector<FrameOutliers>* counts;
                const float* times;
            } payload = {&outlier_counts, times};
            ImPlot::PlotLineG("Outliers", [](int index, void* user) {
                const auto& p = *(const Payload*)user;
                const auto& sample = (*p.counts)[index];
                return ImPlotPoint(p.times ? (double)p.times[index] : (double)index,
                    sample.valid ? (double)sample.outliers : std::numeric_limits<double>::quiet_NaN());
            }, &payload, (int)frame_count);
            scrub_timeline("##outlier_scrub", times, frame_count);
            draw_time_cursor(times, frame_count);
            const bool hovered = ImPlot::IsPlotHovered();
            if (hovered) {
                const size_t f = nearest_frame(ImPlot::GetPlotMousePos().x, times, frame_count);
                const auto& sample = outlier_counts[f];
                ImGui::BeginTooltip();
                ImGui::Text("Frame %zu | %s: %.3f", f, x_label, x_at(f));
                ImGui::Text("%zu outliers / %zu valid (%d total)", sample.outliers, sample.valid, count);
                ImGui::EndTooltip();
            }
            ImPlot::EndPlot();
        }
        ImGui::PopID();
        ImGui::PopID();
    }

    void jump_outlier(size_t current, bool forward) {
        size_t target = current;
        if (forward) {
            for (size_t f = current + 1; f < outlier_counts.size(); ++f) {
                if (outlier_counts[f].outliers) { target = f; break; }
            }
        } else {
            for (size_t f = current; f > 0; --f) {
                if (outlier_counts[f - 1].outliers) { target = f - 1; break; }
            }
        }
        app->animation.frame = (double)target;
        app->animation.mode = PlaybackMode::Stopped;
        focus_heatmap_selection = true;
    }

    void export_csv(size_t frames) {
        char path[4096] = {};
        if (!application::file_dialog(path, sizeof(path), application::FileDialogFlag_Save, STR_LIT("csv"))) return;
        md_file_t file = {};
        if (!md_file_open(&file, str_from_cstr(path), MD_FILE_WRITE | MD_FILE_CREATE | MD_FILE_TRUNCATE)) {
            MD_LOG_ERROR("Lavery: could not open CSV export '%s'", path);
            return;
        }
        std::string buffer = csv_header();
        bool ok = true;
        auto flush = [&]() {
            if (buffer.empty()) return;
            if (md_file_write(file, buffer.data(), buffer.size()) != buffer.size()) ok = false;
            buffer.clear();
        };
        const bool step = metric_is_step((Metric)metric);
        const auto& labels = step ? step_labels : pair_labels;
        const int offset = step ? (int)Tilt : 0;
        const size_t count = columns[metric];
        const float* times = timeline_times(frames);
        char unit[32] = "frame";
        if (times) md_unit_print(unit, sizeof(unit), app->timeline.time_unit);
        for (size_t f = 0; f < frames && ok; ++f) {
            for (size_t m = 0; m < count && ok; ++m) {
                const auto assessment = assess(f, m);
                CsvSample sample;
                sample.run = run;
                sample.frame = f;
                sample.time = times ? (double)times[f] : (double)f;
                sample.time_unit = unit;
                sample.member = m + 1;
                sample.label = labels[m];
                sample.pair_a = step ? pair_labels[steps[m].a] : pair_labels[m];
                sample.pair_b = step ? pair_labels[steps[m].b] : "";
                sample.quality = quality_at(f, m);
                sample.sigma = outlier_sigma;
                sample.selected_mask = outlier_parameter_mask();
                sample.member_outlier = assessment.contributors != 0;
                for (int k = 0; k < 6; ++k) {
                    const int id = offset + k;
                    sample.metric = (Metric)id;
                    sample.value = values[id][f * count + m];
                    sample.baseline = statistics[id].members[m];
                    buffer += csv_sample(sample);
                }
                if (buffer.size() >= 65536) flush();
            }
        }
        if (ok) flush();
        if (!md_file_close(&file)) ok = false;
        if (!ok) MD_LOG_ERROR("Lavery: CSV export failed; '%s' may be incomplete", path);
        else MD_LOG_INFO("Lavery: exported CSV to '%s'", path);
    }

    int draw_current_outliers(int count, size_t frame_count) {
        int hovered_member = -1;
        const auto& labels = metric_is_step((Metric)metric) ? step_labels : pair_labels;
        const size_t frame = (size_t)std::clamp((int64_t)std::llround(app->animation.frame),
            (int64_t)0, (int64_t)frame_count - 1);
        if (ImGui::CollapsingHeader("Current-frame outliers (all members)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Hover to preview only this outlier in 3D; click to select it.");
            const int offset = metric_is_step((Metric)metric) ? (int)Tilt : 0;
            std::vector<std::pair<int, OutlierAssessment>> ranked;
            for (int m = 0; m < count; ++m) {
                const auto result = assess(frame, (size_t)m);
                if (result.contributors) ranked.push_back({m, result});
            }
            std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
                return std::fabs(a.second.peak_z) > std::fabs(b.second.peak_z);
            });
            size_t outliers = 0;
            ImGui::BeginChild("##outlier_rows", ImVec2(0, 160), ImGuiChildFlags_Borders);
            for (const auto& entry : ranked) {
                const int m = entry.first;
                const int peak = offset + entry.second.peak_metric;
                const float v = values[peak][frame * count + m];
                const float z = entry.second.peak_z;
                ++outliers;
                ImGui::PushID(m);
                char row[512];
                snprintf(row, sizeof(row), "%s    %s %.3f %s (%+.2f sigma)%s",
                    labels[m].c_str(), metric_name((Metric)peak), v, metric_unit((Metric)peak), z,
                    quality_at(frame, (size_t)m) ? " [quality flag]" : "");
                if (ImGui::Selectable(row, member == m)) {
                    member = m;
                    focus_heatmap_selection = true;
                }
                if (ImGui::IsItemHovered()) {
                    hovered_member = m;
                    ImGui::BeginTooltip();
                    for (int k = 0; k < 6; ++k) {
                        if (!(entry.second.contributors & (1u << k))) continue;
                        const int id = offset + k;
                        ImGui::Text("%s: %.3f %s (%+.2f sigma)", metric_name((Metric)id),
                            values[id][frame * count + m], metric_unit((Metric)id),
                            statistics[id].members[m].zscore(values[id][frame * count + m]));
                    }
                    draw_quality(quality_at(frame, (size_t)m));
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::Text("%zu / %d outliers in frame %zu", outliers, count, frame);
        }
        return hovered_member;
    }

    void draw_heatmap(int count, size_t frame_count, const std::vector<float>& heatmap_data, int* selected_member) {
        auto& labels = metric_is_step((Metric)metric) ? step_labels : pair_labels;
        int frame_number = std::clamp((int)std::llround(app->animation.frame), 0, (int)frame_count - 1);
        ImGui::SetNextItemWidth(160);
        if (ImGui::InputInt("Frame (0-based)", &frame_number, 1, 10)) {
            app->animation.frame = std::clamp(frame_number, 0, (int)frame_count - 1);
            focus_heatmap_selection = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset view")) reset_heatmap_view = true;
        ImGui::SameLine();
        if (ImGui::Button("Go to selection")) focus_heatmap_selection = true;
        ImGui::TextDisabled("Ctrl-click/drag: jump/scrub | Click: select member | Drag: pan | Wheel: zoom");
        ImGui::TextDisabled("Drag the bottom border to resize the heatmap vertically.");
        if (heatmap_member_count != count) {
            heatmap_member_count = count;
            reset_heatmap_view = true;
        }
        const bool child_visible = ImGui::BeginChild("##heatmap_area", ImVec2(0, 460),
            ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImPlot::PushColormap(ImPlotColormap_RdBu);
        if (child_visible && ImPlot::BeginPlot("Frames × sequence", ImVec2(-1, -1), ImPlotFlags_NoLegend)) {
            ImPlot::SetupAxes("Frame", metric_is_step((Metric)metric) ? "Step" : "Base pair");
            ImPlot::SetupAxesLimits(-0.5, (double)frame_count - 0.5, -0.5, count - 0.5,
                reset_heatmap_view ? ImPlotCond_Always : ImPlotCond_Once);
            if (focus_heatmap_selection && !reset_heatmap_view &&
                heatmap_limits.X.Size() > 0 && heatmap_limits.Y.Size() > 0) {
                const double width = std::min(heatmap_limits.X.Size(), (double)frame_count);
                const double height = std::min(heatmap_limits.Y.Size(), (double)count);
                const double xmin = std::clamp(app->animation.frame - width * 0.5, -0.5, (double)frame_count - 0.5 - width);
                const double ymin = std::clamp((double)*selected_member - height * 0.5, -0.5, count - 0.5 - height);
                ImPlot::SetupAxesLimits(xmin, xmin + width, ymin, ymin + height, ImPlotCond_Always);
            } else if (!reset_heatmap_view && heatmap_frame != app->animation.frame &&
                heatmap_limits.X.Size() > 0 && !heatmap_limits.X.Contains(app->animation.frame)) {
                const double width = std::min(heatmap_limits.X.Size(), (double)frame_count);
                const double xmin = std::clamp(app->animation.frame - width * 0.5, -0.5, (double)frame_count - 0.5 - width);
                ImPlot::SetupAxisLimits(ImAxis_X1, xmin, xmin + width, ImPlotCond_Always);
            }
            reset_heatmap_view = false;
            focus_heatmap_selection = false;
            ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, -0.5, (double)frame_count - 0.5);
            ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, -0.5, count - 0.5);
            ImPlot::SetupAxisZoomConstraints(ImAxis_X1, 1, (double)frame_count);
            ImPlot::SetupAxisZoomConstraints(ImAxis_Y1, 1, count);
            ImPlot::SetupAxisFormat(ImAxis_Y1, [](double value, char* buffer, int size, void* user_data) {
                const auto& self = *(const Lavery*)user_data;
                const int index = (int)std::llround(value);
                if (index < 0 || index >= (int)self.columns[self.metric] || std::fabs(value - index) > 0.01) {
                    return snprintf(buffer, size, " ");
                }
                if (metric_is_step((Metric)self.metric)) {
                    return snprintf(buffer, size, "%u-%u", self.steps[index].a + 1, self.steps[index].b + 1);
                }
                return snprintf(buffer, size, "%d", index + 1);
            }, this);
            ImPlot::PlotHeatmap("##lavery_heatmap", heatmap_data.data(), count, (int)frame_count,
                -outlier_sigma, outlier_sigma,
                nullptr, ImPlotPoint(-0.5, -0.5), ImPlotPoint((double)frame_count - 0.5, count - 0.5));
            scrub_timeline("##heatmap_scrub", nullptr, frame_count);
            draw_time_cursor(nullptr, frame_count);
            const double selected_row = *selected_member;
            ImPlot::PlotInfLines("Selected member", &selected_row, 1, ImPlotInfLinesFlags_Horizontal);
            if (ImPlot::IsPlotHovered()) {
                const ImPlotPoint pos = ImPlot::GetPlotMousePos();
                const int frame = (int)std::floor(pos.x + 0.5);
                const int col = (int)std::floor(pos.y + 0.5);
                if (frame >= 0 && frame < (int)frame_count && col >= 0 && col < count) {
                    const float value = values[metric][(size_t)frame * count + col];
                    const float z = statistics[metric].members[col].zscore(value);
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(labels[col].c_str());
                    if (metric_is_step((Metric)metric)) {
                        ImGui::TextUnformatted(pair_labels[steps[col].a].c_str());
                        ImGui::TextUnformatted(pair_labels[steps[col].b].c_str());
                    }
                    ImGui::Text("Frame %d: %.3f %s; z = %+.2f", frame, value, metric_unit((Metric)metric), z);
                    draw_quality(quality_at((size_t)frame, (size_t)col));
                    ImGui::EndTooltip();
                }
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                heatmap_pressed = ImPlot::IsPlotHovered() && !ImGui::GetIO().KeyCtrl;
                heatmap_dragged = false;
            }
            if (heatmap_pressed && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) heatmap_dragged = true;
            if (heatmap_pressed && !heatmap_dragged && ImPlot::IsPlotHovered() &&
                ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                const ImPlotPoint pos = ImPlot::GetPlotMousePos();
                const int frame = (int)std::floor(pos.x + 0.5);
                const int col = (int)std::floor(pos.y + 0.5);
                if (frame >= 0 && frame < (int)frame_count && col >= 0 && col < count) {
                    *selected_member = col;
                }
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) heatmap_pressed = false;
            heatmap_limits = ImPlot::GetPlotLimits();
            heatmap_frame = app->animation.frame;
            ImPlot::EndPlot();
        }
        ImPlot::PopColormap();
        ImGui::EndChild();
    }

    void update_outlier_highlight(int count, size_t frame_count, int hovered_member) {
        const int frame = std::clamp((int)std::llround(app->animation.frame), 0, (int)frame_count - 1);
        auto& mask = app->selection.highlight_mask;
        const uint64_t previous_hash = md_bitset_hash64(md_bitmask_view(&mask), 0);
        if (highlight_owned && previous_hash != highlight_hash) highlight_owned = false;
        // Explicit row hover takes precedence; automatic highlighting yields to other hover feedback.
        if (hovered_member < 0 && !highlight_owned && !md_bitmask_empty(&mask)) return;

        md_bitmask_clear(&mask);
        auto add_pair = [&](uint32_t index) {
            const Pair& pair = pairs[index];
            for (uint32_t base_index : {pair.a, pair.b}) {
                for (uint32_t atom : bases[base_index].atoms) md_bitmask_set_bit(&mask, atom);
            }
        };
        const bool step_metric = metric_is_step((Metric)metric);
        const int first = hovered_member >= 0 ? hovered_member : 0;
        const int end = hovered_member >= 0 ? hovered_member + 1 : count;
        for (int m = first; m < end; ++m) {
            if (!assess((size_t)frame, (size_t)m).contributors) continue;
            if (step_metric) {
                add_pair(steps[m].a);
                add_pair(steps[m].b);
            } else {
                add_pair((uint32_t)m);
            }
        }
        highlight_owned = !md_bitmask_empty(&mask);
        highlight_hash = md_bitset_hash64(md_bitmask_view(&mask), 0);
        if (highlight_hash != previous_hash) {
            viamd::event_system_broadcast_event(viamd::EventType_ViamdHighlightMaskChanged,
                viamd::EventPayloadType_ApplicationState, app);
        }
    }
};

Lavery instance;

} // namespace
