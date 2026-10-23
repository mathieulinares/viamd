#include "lavery_core.h"

#include <core/md_allocator.h>
#include <core/md_array.h>
#include <core/md_bitset.h>
#include <core/md_log.h>
#include <core/md_hash.h>
#include <core/md_os.h>
#include <core/md_str.h>
#include <md_system.h>
#include <md_util.h>

#include <viamd_event.h>
#include <viamd.h>
#include <event.h>
#include <task_system.h>
#include <serialization_utils.h>
#include <gfx/immediate_draw_utils.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
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
    bool show_axis = true;
    bool signed_overlay = true;
    bool overlay_halo = true;
    bool reset_layout = false;
    int hovered_outlier = -1;
    std::array<std::vector<int>, 2> comparison_members;
    std::array<DistributionComparison, Metric_Count> distributions;
    std::vector<uint8_t> pair_contacts;
    std::vector<uint8_t> pair_uses_hydrogens;
    std::unique_ptr<PairingValidator> display_validator;
    std::vector<BaseFrame> display_bases;
    std::vector<uint16_t> display_pair_quality;
    AxisPath display_axis;
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
    std::atomic<bool> cancel_requested = false;
    std::atomic<size_t> frames_done = 0;
    task_system::ID calculation = task_system::INVALID_ID;
    bool calculating = false;
    bool computed = false;
    std::string run;
    std::string analysis_error;

    Lavery() { viamd::event_system_register_handler(*this); }

    void wait_for_attribute_readers() {
        if (app->script.eval) md_script_eval_interrupt(app->script.eval);
        task_system::task_wait_for(app->tasks.evaluate);
        task_system::task_wait_for(app->script.vis_task);
    }

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
        cancel_requested = true;
        if (calculation != task_system::INVALID_ID) task_system::task_interrupt_and_wait_for(calculation);
        calculation = task_system::INVALID_ID;
        clear_highlight();
        if (app && !run.empty()) {
            wait_for_attribute_readers();
            const std::string prefix = run + "/lavery";
            if (md_attributes_remove_prefix(&app->mold.sys.attributes, str_from_cstr(prefix.c_str()))) app->script.compile_ir = true;
        }
        bases.clear();
        pairs.clear();
        steps.clear();
        for (auto& v : values) v.clear();
        for (auto& s : statistics) s = {};
        pair_labels.clear();
        step_labels.clear();
        reset_heatmap_view = true;
        focus_heatmap_selection = false;
        heatmap_limits = {};
        heatmap_frame = -1.0;
        heatmap_pressed = heatmap_dragged = false;
        heatmap_member_count = 0;
        outlier_counts.clear();
        pair_quality_flags.clear();
        pair_contacts.clear();
        pair_uses_hydrogens.clear();
        display_validator.reset();
        display_bases.clear();
        display_pair_quality.clear();
        display_axis = {};
        for (auto& d : distributions) d = {};
        for (auto& c : comparison_members) c.clear();
        hovered_outlier = -1;
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
        frames_done = 0;
        analysis_error.clear();
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
                reset();
                app = nullptr;
                break;
            case viamd::EventType_ViamdRenderOpaque:
                if (app && show_window && computed && calculation_ok.load()) {
                    prepare_display_geometry();
                    draw_axis();
                }
                break;
            case viamd::EventType_ViamdRenderTransparent:
                if (app && show_window && computed && calculation_ok.load()) draw_signed_overlay();
                break;
            case viamd::EventType_ViamdRenderKey: {
                auto* key = (RenderKey*)e.payload;
                const bool active = show_window && computed && calculation_ok.load();
                key->key = md_hash64(&active, sizeof(active), key->key);
                if (active) {
                    for (const bool flag : {show_axis, signed_overlay, overlay_halo, sync_viewport, aggregate_outliers}) {
                        key->key = md_hash64(&flag, sizeof(flag), key->key);
                    }
                    key->key = md_hash64(&hovered_outlier, sizeof(hovered_outlier), key->key);
                    key->key = md_hash64(&metric, sizeof(metric), key->key);
                    key->key = md_hash64(&selected_parameters, sizeof(selected_parameters), key->key);
                    key->key = md_hash64(&outlier_sigma, sizeof(outlier_sigma), key->key);
                }
                break;
            }
            case viamd::EventType_ViamdSerialize: {
                auto& state = *(viamd::serialization_state_t*)e.payload;
                viamd::write_section_header(state, STR_LIT("Lavery"));
                viamd::write_bool(state, STR_LIT("ShowWindow"), show_window);
                viamd::write_bool(state, STR_LIT("ShowHeatmap"), show_heatmap);
                viamd::write_bool(state, STR_LIT("SyncViewport"), sync_viewport);
                viamd::write_bool(state, STR_LIT("CompareAll"), compare_all);
                viamd::write_bool(state, STR_LIT("ShowAxis"), show_axis);
                viamd::write_bool(state, STR_LIT("SignedOverlay"), signed_overlay);
                viamd::write_bool(state, STR_LIT("OverlayHalo"), overlay_halo);
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
                show_axis = signed_overlay = overlay_halo = true;
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
                    else if (str_eq(key, STR_LIT("ShowAxis"))) viamd::extract_bool(show_axis, arg);
                    else if (str_eq(key, STR_LIT("SignedOverlay"))) viamd::extract_bool(signed_overlay, arg);
                    else if (str_eq(key, STR_LIT("OverlayHalo"))) viamd::extract_bool(overlay_halo, arg);
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

    bool analysis_cancelled() const {
        return cancel_requested.load() || task_system::task_is_interrupted(calculation);
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
        PairingValidator first_validator;
        std::vector<uint16_t> chemistry_quality;
        std::vector<uint8_t> contacts;
        if (!first_validator.initialize(*sys, bases, pairs) ||
            !first_validator.evaluate(chemistry_quality, contacts, first_state)) {
            md_system_state_free(&first_state);
            md_temp_end(temp);
            analysis_error = "Could not validate first-frame canonical pairing. See the application log.";
            computed = true;
            return;
        }
        size_t retained = 0;
        for (size_t p = 0; p < pairs.size(); ++p) {
            if (!(chemistry_quality[p] & Quality_NoncanonicalPair)) {
                pair_uses_hydrogens.push_back(first_validator.chemistry()[p].uses_hydrogens ? 1 : 0);
                pairs[retained++] = pairs[p];
            }
        }
        if (retained != pairs.size()) MD_LOG_INFO("Lavery: excluded %zu unsupported first-frame pair identities", pairs.size() - retained);
        pairs.resize(retained);
        md_system_state_free(&first_state);
        md_temp_end(temp);
        if (pairs.empty()) {
            computed = true;
            MD_LOG_INFO("Lavery: no geometrically paired bases found in frame 0");
            return;
        }
        display_validator = std::make_unique<PairingValidator>();
        if (!display_validator->initialize(*sys, bases, pairs)) {
            analysis_error = "Could not prepare displayed-frame pairing validation. See the application log.";
            computed = true;
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
        pair_contacts.assign(frame_count * bp_count, 0);
        step_quality_flags.assign(frame_count * step_count, Quality_AdjacentPair);
        columns.fill(0);
        for (int m = 0; m < (int)Metric_Count; ++m) {
            columns[m] = metric_is_step((Metric)m) ? step_count : bp_count;
            values[m].assign(frame_count * columns[m], std::numeric_limits<float>::quiet_NaN());
        }

        calculation_ready = false;
        calculation_ok = false;
        cancel_requested = false;
        frames_done = 0;
        analysis_error.clear();
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
            PairingValidator validator;
            FrameAnalysis result;
            bool ok = validator.initialize(*sys, bases, pairs);
            for (size_t frame = 0; frame < frame_count; ++frame) {
                if (!ok || analysis_cancelled()) { ok = false; break; }
                if (!md_system_extract_frame(ex, (int64_t)frame, &state)) {
                    ok = false;
                    break;
                }
                if (!analyze_frame(&result, *sys, state, bases, pairs, steps, validator)) {
                    ok = false;
                    break;
                }
                std::copy(result.pair_quality.begin(), result.pair_quality.end(), pair_quality_flags.begin() + frame * pairs.size());
                std::copy(result.step_quality.begin(), result.step_quality.end(), step_quality_flags.begin() + frame * steps.size());
                std::copy(result.contacts.begin(), result.contacts.end(), pair_contacts.begin() + frame * pairs.size());
                for (int m = 0; m < (int)Metric_Count; ++m) {
                    const auto& samples = metric_is_step((Metric)m) ? result.step_values : result.pair_values;
                    for (size_t i = 0; i < samples.size(); ++i) values[m][frame * columns[m] + i] = samples[i].metric[m];
                }
                frames_done = frame + 1;
            }
            md_system_extract_end(ex);
            md_system_state_free(&state);
            md_temp_end(temp_scope);
            if (ok && !analysis_cancelled()) {
                for (int m = 0; m < (int)Metric_Count; ++m) {
                    if (analysis_cancelled()) { ok = false; break; }
                    if (!compute_statistics(&statistics[m], values[m], columns[m])) {
                        ok = false;
                        break;
                    }
                }
            } else ok = false;
            calculation_ok = ok && !analysis_cancelled();
            calculation_ready = true;
        });
        task_system::enqueue_task(calculation);
    }

    void complete_if_ready() {
        if (!calculating) return;
        if (!calculation_ready.load()) {
            if (task_system::task_is_running(calculation)) return;
            analysis_error = "Analysis was interrupted before it could finish. Use Retry analysis.";
            MD_LOG_ERROR("Lavery: trajectory task was interrupted");
        }
        task_system::task_wait_for(calculation);
        calculation = task_system::INVALID_ID;
        calculating = false;
        if (!calculation_ok.load()) {
            analysis_error = cancel_requested.load() ? "Analysis cancelled. Use Retry analysis to restart."
                : "Trajectory analysis failed or was interrupted. See the application log; use Retry analysis.";
            MD_LOG_ERROR("Lavery: %s", analysis_error.c_str());
            computed = true;
            return;
        }
        if (!publish_attributes()) {
            calculation_ok = false;
            analysis_error = "Could not publish analysis attributes. See the application log; use Retry analysis.";
        }
        computed = true;
    }

    bool publish_attributes() {
        if (!app || run.empty()) {
            MD_LOG_ERROR("Lavery: no dataset for attribute publication");
            return false;
        }
        md_attributes_t* table = &app->mold.sys.attributes;
        wait_for_attribute_readers();
        const std::string prefix_string = run + "/lavery";
        const str_t prefix = str_from_cstr(prefix_string.c_str());
        md_attributes_remove_prefix(table, prefix);
        app->script.compile_ir = true;
        for (int m = 0; m < (int)Metric_Count; ++m) {
            const Metric metric_id = (Metric)m;
            const size_t n = columns[m];
            if (n == 0) continue;
            if (!publish_series(table, run, metric_path(metric_id), metric_name(metric_id),
                MD_ATTRIBUTE_TYPE_F32, run_num_frames(app), n, values[m].data(), values[m].size() * sizeof(float),
                metric_is_angular(metric_id) ? md_unit_degree() : md_unit_angstrom())) {
                md_attributes_remove_prefix(table, prefix);
                return false;
            }
        }
        const size_t frames = run_num_frames(app);
        if (!publish_series(table, run, "bp/quality", "Pair quality flags", MD_ATTRIBUTE_TYPE_U16,
                frames, pairs.size(), pair_quality_flags.data(), pair_quality_flags.size() * sizeof(uint16_t), md_unit_none()) ||
            !publish_series(table, run, "bp/contact_count", "Canonical contacts", MD_ATTRIBUTE_TYPE_U8,
                frames, pairs.size(), pair_contacts.data(), pair_contacts.size(), md_unit_none()) ||
            (!steps.empty() && !publish_series(table, run, "step/quality", "Step quality flags", MD_ATTRIBUTE_TYPE_U16,
                frames, steps.size(), step_quality_flags.data(), step_quality_flags.size() * sizeof(uint16_t), md_unit_none()))) {
            md_attributes_remove_prefix(table, prefix);
            return false;
        }
        std::vector<uint8_t> evidence(pair_contacts.size());
        for (size_t f = 0; f < frames; ++f) std::copy(pair_uses_hydrogens.begin(), pair_uses_hydrogens.end(), evidence.begin() + f * pairs.size());
        if (!publish_series(table, run, "bp/hydrogen_evidence", "Explicit-hydrogen evidence (1) vs heavy-atom proxy (0)",
            MD_ATTRIBUTE_TYPE_U8, frames, pairs.size(), evidence.data(), evidence.size(), md_unit_none())) {
            md_attributes_remove_prefix(table, prefix);
            return false;
        }
        return true;
    }

    void draw() {
        if (!ImGui::GetIO().KeyCtrl || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) scrubbing_plot = 0;
        hovered_outlier = -1;
        const ImVec2 work_size = ImGui::GetMainViewport()->WorkSize;
        ImGui::SetNextWindowSize({work_size.x * 0.9f, work_size.y * 0.9f}, ImGuiCond_FirstUseEver);
        const bool expanded = ImGui::Begin("Lavery", &show_window,
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const ImGuiID dock = ImGui::GetID("LaveryWorkspace");
        auto finish_status = [&]() {
            if (!ImGui::DockBuilderGetNode(dock)) reset_layout = true;
            ImGui::DockSpace(dock, ImVec2(0, 0), ImGuiDockNodeFlags_KeepAliveOnly);
            ImGui::End();
        };
        if (!expanded) {
            clear_highlight();
            finish_status();
            return;
        }

        if (!app || app->mold.sys.nucleic_backbone.segment.count == 0) {
            ImGui::TextDisabled("Load a structure with nucleic-acid residues to analyze Lavery parameters.");
            finish_status();
            return;
        }
        if (app->mold.run[0] == '\0') {
            ImGui::TextDisabled("Load a trajectory to compute per-frame Lavery parameters.");
            finish_status();
            return;
        }
        if (calculating) {
            ImGui::Text("Analyzing trajectory: %zu / %zu frames", frames_done.load(), run_num_frames(app));
            if (cancel_requested.load()) ImGui::TextDisabled("Cancelling...");
            else if (ImGui::Button("Cancel analysis")) cancel_requested = true;
            finish_status();
            return;
        }
        if (!computed) {
            ImGui::TextDisabled("Preparing trajectory analysis...");
            finish_status();
            return;
        }
        if (!calculation_ok.load()) {
            ImGui::TextWrapped("%s", analysis_error.empty()
                ? "No supported canonical pairs found, or analysis could not start. See the application log."
                : analysis_error.c_str());
            if (ImGui::Button("Retry analysis")) reset();
            clear_highlight();
            finish_status();
            return;
        }

        draw_shared_controls();
        const int count = (int)columns[metric];
        const size_t frame_count = run_num_frames(app);
        if (count <= 0 || !frame_count) {
            ImGui::TextDisabled("No inter-base-pair links found. Select Intra-base pair.");
            finish_status();
            clear_highlight();
            return;
        }
        const ImVec2 size = ImGui::GetContentRegionAvail();
        if (reset_layout || !ImGui::DockBuilderGetNode(dock)) {
            ImGui::DockBuilderRemoveNode(dock);
            ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dock, size);
            ImGuiID top = dock, bottom = 0, left = 0, right = 0;
            ImGui::DockBuilderSplitNode(top, ImGuiDir_Down, 0.24f, &bottom, &top);
            ImGui::DockBuilderSplitNode(top, ImGuiDir_Right, 0.38f, &right, &left);
            ImGui::DockBuilderDockWindow("Lavery - Overview", left);
            ImGui::DockBuilderDockWindow("Lavery - Inspector", right);
            ImGui::DockBuilderDockWindow("Lavery - Events & quality", bottom);
            ImGui::DockBuilderFinish(dock);
            reset_layout = false;
        }
        ImGui::DockSpace(dock);
        ImGui::End();
        const ImPlotInputMap input = ImPlot::GetInputMap();
        if (ImGui::GetIO().KeyCtrl) ImPlot::GetInputMap().Pan = ImGuiMouseButton_Middle;
        update_counts(count, frame_count);
        if (ImGui::Begin("Lavery - Overview", nullptr, ImGuiWindowFlags_NoFocusOnAppearing)) {
            draw_outlier_timeline(count, frame_count);
            if (show_heatmap) draw_heatmap(count, frame_count, statistics[metric].heatmap, &member);
        }
        ImGui::End();
        if (ImGui::Begin("Lavery - Inspector", nullptr, ImGuiWindowFlags_NoFocusOnAppearing)) draw_inspector(count, frame_count);
        ImGui::End();
        if (ImGui::Begin("Lavery - Events & quality", nullptr, ImGuiWindowFlags_NoFocusOnAppearing)) draw_events_quality(count, frame_count);
        ImGui::End();
        ImPlot::GetInputMap() = input;
        if (sync_viewport || hovered_outlier >= 0) update_outlier_highlight(count, frame_count, hovered_outlier);
        else clear_highlight();
    }

    void draw_shared_controls() {
        static const char* bp_items[] = {"Buckle", "Propeller Twist", "Opening", "Shear", "Stretch", "Stagger"};
        static const char* step_items[] = {"Tilt", "Roll", "Twist", "Shift", "Slide", "Rise"};
        int group = metric_is_step((Metric)metric) ? 1 : 0;
        ImGui::SetNextItemWidth(170);
        if (ImGui::Combo("Class", &group, "Intra-base pair\0Inter-base pair\0")) {
            metric = group * 6 + metric % 6;
            focus_heatmap_selection = true;
            reset_heatmap_view = true;
        }
        const bool step_metric = group != 0;
        const int offset = step_metric ? (int)Tilt : (int)Buckle;
        int local_metric = std::clamp(metric - offset, 0, 5);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(155);
        ImGui::Combo("Parameter", &local_metric, step_metric ? step_items : bp_items, 6);
        metric = offset + local_metric;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        ImGui::SliderFloat("Tolerance", &outlier_sigma, 1.5f, 4.0f, "%.1f sigma");
        ImGui::SameLine();
        if (ImGui::Button("Settings / Export")) ImGui::OpenPopup("Lavery settings");
        if (ImGui::BeginPopup("Lavery settings")) {
            ImGui::Checkbox("Heatmap", &show_heatmap);
            ImGui::Checkbox("Highlight all outliers", &sync_viewport);
            ImGui::Checkbox("Signed outlier overlay", &signed_overlay);
            ImGui::Checkbox("Soft overlay halos", &overlay_halo);
            ImGui::Checkbox("Smoothed midpoint axis", &show_axis);
            ImGui::TextDisabled("Axis is not the Curves+ screw axis; halos are not emissive bloom.");
            ImGui::Separator();
            if (ImGui::Button("Export parameter CSV")) export_csv(run_num_frames(app));
            if (ImGui::Button("Export displayed axis CSV")) export_axis();
            if (ImGui::Button("Reset panel layout")) reset_layout = true;
            if (ImGui::Button("Recompute analysis")) {
                reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::TextDisabled("Canonical A/G/C/T/U; fixed partners identified in frame 0.");
            ImGui::TextDisabled("Script: attr(\"lavery/step/twist\")");
            ImGui::EndPopup();
        }
        const int count = (int)columns[metric];
        if (count <= 0) {
            ImGui::TextDisabled("No consecutive inter-base-pair links were found.");
            clear_highlight();
            return;
        }
        member = std::clamp(member, 0, count - 1);
        ImGui::SetNextItemWidth(100);
        int member_number = member + 1;
        if (ImGui::InputInt(step_metric ? "Inter-base pair" : "Intra-base pair", &member_number)) {
            member = std::clamp(member_number, 1, count) - 1;
            focus_heatmap_selection = true;
        }
        ImGui::SameLine();
        int frame_number = (int)std::llround(app->animation.frame);
        ImGui::SetNextItemWidth(110);
        if (ImGui::InputInt("Frame", &frame_number)) {
            app->animation.frame = (double)std::clamp(frame_number, 0, (int)run_num_frames(app) - 1);
            app->animation.mode = PlaybackMode::Stopped;
            focus_heatmap_selection = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", metric_unit((Metric)metric));
        const size_t frame = nearest_frame(app->animation.frame, nullptr, run_num_frames(app));
        size_t valid_pairs = 0, valid_steps = 0;
        for (size_t p = 0; p < pairs.size(); ++p) if (!pair_quality_flags[frame * pairs.size() + p]) ++valid_pairs;
        for (size_t s = 0; s < steps.size(); ++s) if (!step_quality_flags[frame * steps.size() + s]) ++valid_steps;
        ImGui::Text("Intra-base pairs: %zu/%zu valid | Inter-base pairs: %zu/%zu valid | frame %zu",
            valid_pairs, pairs.size(), valid_steps, steps.size(), frame);
        ImGui::SetItemTooltip("Denominators are fixed tracked identities/links. A missing internal pair invalidates BOTH neighboring links; gaps are never bridged.");
        const auto& labels = step_metric ? step_labels : pair_labels;
        ImGui::TextDisabled("%s", labels[member].c_str());
        ImGui::Checkbox("Outlier in any selected parameter", &aggregate_outliers);
        ImGui::SameLine();
        if (aggregate_outliers) {
            if (ImGui::Button("Choose parameters")) ImGui::OpenPopup("Outlier parameters");
            if (ImGui::BeginPopup("Outlier parameters")) {
                for (int k = 0; k < 6; ++k) {
                    bool selected = (selected_parameters & (1u << k)) != 0;
                    if (ImGui::Checkbox(metric_name((Metric)(offset + k)), &selected)) {
                        if (selected) selected_parameters |= 1u << k;
                        else selected_parameters &= ~(1u << k);
                    }
                }
                ImGui::EndPopup();
            }
            if (!selected_parameters) ImGui::TextDisabled("Select at least one parameter to detect outliers.");
        }

    }

    void draw_inspector(int count, size_t frame_count) {
        const bool step_metric = metric_is_step((Metric)metric);
        const auto& labels = step_metric ? step_labels : pair_labels;
        const std::vector<float>& data = values[metric];
        if (data.size() < frame_count * (size_t)count) return;
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
        const float* times = timeline_times(frame_count);
        char time_label[64];
        timeline_label(time_label, sizeof(time_label), times);
        ImGui::TextDisabled("Ctrl-click / Ctrl-drag: jump / scrub time. Drag / wheel: pan / zoom.");
        if (ImGui::CollapsingHeader("Selected-member timeline", ImGuiTreeNodeFlags_DefaultOpen)) {
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

        const auto& member_stats = stats.members[member];
        if (ImGui::CollapsingHeader("Distribution comparison", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& pinned = comparison_members[step_metric ? 1 : 0];
            if (ImGui::BeginCombo("Compare with", "Select additional members")) {
                for (int m = 0; m < count; ++m) {
                    auto found = std::find(pinned.begin(), pinned.end(), m);
                    bool selected = found != pinned.end();
                    if (ImGui::Selectable(labels[m].c_str(), selected, ImGuiSelectableFlags_DontClosePopups)) {
                        if (selected) pinned.erase(found);
                        else pinned.push_back(m);
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("Clear comparison")) pinned.clear();
            ImGui::Text("Mean %.3f  SD %.3f  valid %zu/%zu",
                member_stats.mean, member_stats.sd, member_stats.count, frame_count);
            auto& histogram = distributions[metric];
            if (histogram.valid.empty() && !compute_distributions(&histogram, data, (size_t)count)) {
                ImGui::TextUnformatted("Could not calculate distributions. See the application log.");
                return;
            }
            if (ImPlot::BeginPlot("Distribution", ImVec2(-1, 180))) {
                ImPlot::SetupAxes(metric_unit((Metric)metric), "Probability density");
                if (!histogram.centers.empty()) {
                    auto plot = [&](int m) {
                        if (!histogram.valid[m]) return;
                        ImPlot::PlotLine(labels[m].c_str(), histogram.centers.data(),
                            histogram.density.data() + (size_t)m * histogram.centers.size(), (int)histogram.centers.size());
                    };
                    for (int m : pinned) if (m != member) plot(m);
                    plot(member);
                }
                const size_t frame = (size_t)std::clamp((int64_t)std::llround(app->animation.frame),
                    (int64_t)0, (int64_t)frame_count - 1);
                const float value = data[frame * count + member];
                if (std::isfinite(value)) ImPlot::PlotInfLines("Current value", &value, 1);
                ImPlot::EndPlot();
            }
        } else if (!member_stats.count) {
            ImGui::TextDisabled("No finite values for this member; other members remain available below.");
        }
        hovered_outlier = draw_current_outliers(count, frame_count);
        ImGui::TextColored(ImVec4(1.0f, 0.255f, 0.373f, 1), "Positive deviation");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.157f, 0.843f, 1.0f, 1), "Negative deviation");
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
        for (uint16_t bit = 1; bit <= Quality_Last; bit <<= 1) {
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

    void update_counts(int count, size_t frame_count) {
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
    }

    void draw_outlier_timeline(int count, size_t frame_count) {
        const float* times = timeline_times(frame_count);
        char x_label[64];
        timeline_label(x_label, sizeof(x_label), times);
        auto x_at = [&](size_t f) { return times ? (double)times[f] : (double)f; };
        ImGui::Text("Outlier count: %s, %.1f sigma (all %s)", aggregate_outliers ? "any selected parameter" : metric_name((Metric)metric),
            outlier_sigma, metric_is_step((Metric)metric) ? "inter-base pairs" : "intra-base pairs");
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

    void draw_events_quality(int count, size_t frame_count) {
        const size_t frame = (size_t)std::clamp((int64_t)std::llround(app->animation.frame), (int64_t)0, (int64_t)frame_count - 1);
        const auto& labels = metric_is_step((Metric)metric) ? step_labels : pair_labels;
        if (!ImGui::BeginTabBar("EventsQuality")) return;
        if (ImGui::BeginTabItem("Outlier events")) {
            const float* times = timeline_times(frame_count);
            char unit[64];
            timeline_label(unit, sizeof(unit), times);
            ImGui::Text("%zu events | duration in %s | click a row to jump to its peak", outlier_events.size(), unit);
            if (ImGui::BeginTable("Events", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
                for (const char* title : {"Frames", "Duration", "Peak frame", "Peak sigma", "Max outliers"}) ImGui::TableSetupColumn(title);
                ImGui::TableHeadersRow();
                for (const auto& event : outlier_events) {
                    ImGui::PushID((int)event.begin);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    char label[64];
                    snprintf(label, sizeof(label), "%zu - %zu", event.begin, event.end);
                    if (ImGui::Selectable(label, false, ImGuiSelectableFlags_SpanAllColumns)) {
                        app->animation.frame = (double)event.peak;
                        app->animation.mode = PlaybackMode::Stopped;
                        focus_heatmap_selection = true;
                    }
                    ImGui::TableNextColumn();
                    ImGui::Text("%.4g", times ? (double)times[event.end] - times[event.begin] : (double)(event.end - event.begin));
                    ImGui::TableNextColumn(); ImGui::Text("%zu", event.peak);
                    ImGui::TableNextColumn(); ImGui::Text("%.2f", event.severity);
                    ImGui::TableNextColumn(); ImGui::Text("%zu", event.peak_count);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Pairing & coverage")) {
            ImGui::Text("Frame %zu | %zu / %d members have data", frame, outlier_counts[frame].valid, count);
            ImGui::TextDisabled("Explicit H: >=1 canonical H-bond (D-A <=3.5 A, H-D-A <=30 deg). No H: >=2 heavy contacts <=3.5 A.");
            if (ImGui::BeginTable("Quality", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Member");
                ImGui::TableSetupColumn("Contacts / evidence");
                ImGui::TableSetupColumn("Validity");
                ImGui::TableHeadersRow();
                for (int m = 0; m < count; ++m) {
                    ImGui::PushID(m);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    if (ImGui::Selectable(labels[m].c_str(), member == m, ImGuiSelectableFlags_SpanAllColumns)) {
                        member = m;
                        focus_heatmap_selection = true;
                    }
                    ImGui::TableNextColumn();
                    auto contact = [&](size_t p) {
                        ImGui::Text("%u %s", (unsigned)pair_contacts[frame * pairs.size() + p],
                            pair_uses_hydrogens[p] ? "H-bonds" : "heavy-atom proxy");
                    };
                    if (metric_is_step((Metric)metric)) {
                        contact(steps[m].a);
                        ImGui::SameLine();
                        contact(steps[m].b);
                    } else contact((size_t)m);
                    ImGui::TableNextColumn();
                    draw_quality(quality_at(frame, (size_t)m));
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
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
        if (heatmap_member_count != count) {
            heatmap_member_count = count;
            reset_heatmap_view = true;
        }
        const bool child_visible = ImGui::BeginChild("##heatmap_area", ImVec2(0, std::max(150.0f, ImGui::GetContentRegionAvail().y)),
            ImGuiChildFlags_Borders,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImPlotColormap colormap = ImPlot::GetColormapIndex("Lavery signed z-score");
        if (colormap < 0) {
            const ImU32 colors[] = {IM_COL32(40, 215, 255, 255), IM_COL32(240, 240, 240, 255), IM_COL32(255, 65, 95, 255)};
            colormap = ImPlot::AddColormap("Lavery signed z-score", colors, 3, false);
        }
        ImPlot::PushColormap(colormap);
        if (child_visible && ImPlot::BeginPlot("Frames × sequence", ImVec2(-1, -1), ImPlotFlags_NoLegend)) {
            ImPlot::SetupAxes("Frame", metric_is_step((Metric)metric) ? "Inter-base pair" : "Intra-base pair");
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

    void prepare_display_geometry() {
        display_axis = {};
        display_bases.clear();
        display_pair_quality.clear();
        if ((!show_axis && !signed_overlay) || !display_validator || !app->mold.state.xyz) return;
        // The displayed coordinates may be turned relative to the cell. Fit and
        // unwrap in the lattice frame, then turn the resulting geometry back.
        auto state = app->mold.state;
        std::vector<vec3_t> coordinates(state.num_atoms);
        const mat4_t inverse = mat4_inverse(app->operations.state_rotation);
        for (size_t i = 0; i < state.num_atoms; ++i) coordinates[i] = mat4_mul_vec3(inverse, md_state_coord(&state, i), 1);
        state.xyz = coordinates.data();
        FrameAnalysis current;
        if (!analyze_frame(&current, app->mold.sys, state, bases, pairs, steps, *display_validator) ||
            !extract_base_frames(display_bases, app->mold.sys, state, bases)) {
            MD_LOG_ERROR("Lavery: could not prepare displayed-frame geometry");
            display_bases.clear();
            return;
        }
        const size_t frame = nearest_frame(app->animation.frame, nullptr, run_num_frames(app));
        display_pair_quality = current.pair_quality;
        for (size_t p = 0; p < pairs.size(); ++p) {
            display_pair_quality[p] |= pair_quality_flags[frame * pairs.size() + p];
            if (display_pair_quality[p]) current.pair_frames[p] = {};
        }
        if (show_axis) {
            display_axis = compute_axis(current.pair_frames, steps, &state.unitcell);
            for (auto& point : display_axis.points) point = mat4_mul_vec3(app->operations.state_rotation, point, 1);
            for (auto& tangent : display_axis.tangents) tangent = mat4_mul_vec3(app->operations.state_rotation, tangent, 0);
        }
        for (auto& frame_base : display_bases) {
            frame_base.center = mat4_mul_vec3(app->operations.state_rotation, frame_base.center, 1);
            frame_base.x = mat4_mul_vec3(app->operations.state_rotation, frame_base.x, 0);
            frame_base.y = mat4_mul_vec3(app->operations.state_rotation, frame_base.y, 0);
        }
    }

    void draw_axis() {
        if (!show_axis || display_axis.points.empty()) return;
        immediate::Scope scope(app->gfx.world, "Lavery midpoint axis");
        for (size_t segment = 1; segment < display_axis.offsets.size(); ++segment) {
            for (size_t i = display_axis.offsets[segment - 1] + 1; i < display_axis.offsets[segment]; ++i) {
                if (vec3_length(vec3_sub(display_axis.points[i], display_axis.points[i - 1])) < 1.0e-6f) continue;
                immediate::cylinder(scope, display_axis.points[i - 1], display_axis.points[i], 0.25f,
                    IM_COL32(245, 205, 55, 255), 0xFFFFFFFF, 8);
            }
        }
    }

    void draw_signed_overlay() {
        if (!signed_overlay || display_bases.empty() || (!sync_viewport && hovered_outlier < 0)) return;
        const size_t frame = nearest_frame(app->animation.frame, nullptr, run_num_frames(app));
        const bool step = metric_is_step((Metric)metric);
        std::vector<float> severity(bases.size());
        auto add_pair = [&](size_t p, float z) {
            if (display_pair_quality[p]) return;
            for (uint32_t b : {pairs[p].a, pairs[p].b}) {
                if (std::fabs(z) > std::fabs(severity[b])) severity[b] = z;
            }
        };
        const int first = hovered_outlier >= 0 ? hovered_outlier : 0;
        const int end = hovered_outlier >= 0 ? first + 1 : (int)columns[metric];
        for (int m = first; m < end; ++m) {
            const auto result = assess(frame, (size_t)m);
            if (!result.contributors) continue;
            if (step) {
                add_pair(steps[m].a, result.peak_z);
                add_pair(steps[m].b, result.peak_z);
            } else add_pair((size_t)m, result.peak_z);
        }
        immediate::Scope scope(app->gfx.overlay, "Lavery signed outliers");
        for (size_t b = 0; b < bases.size(); ++b) {
            if (severity[b] == 0) continue;
            const auto& base = display_bases[b];
            const bool positive = severity[b] > 0;
            auto color = [&](int alpha) { return positive ? IM_COL32(255, 65, 95, alpha) : IM_COL32(40, 215, 255, alpha); };
            // Layered, unlit transparency is a visual halo, not HDR emission.
            const int layers = overlay_halo ? 3 : 1;
            for (int layer = layers - 1; layer >= 0; --layer) {
                const float radius = 2.0f + layer * 0.22f;
                vec3_t previous = vec3_add(base.center, vec3_mul1(base.x, radius));
                for (int s = 1; s <= 24; ++s) {
                    const float angle = (float)s * (6.28318530718f / 24);
                    const vec3_t point = vec3_add(base.center, vec3_mul1(vec3_add(
                        vec3_mul1(base.x, std::cos(angle)), vec3_mul1(base.y, std::sin(angle))), radius));
                    immediate::cylinder(scope, previous, point, 0.08f + layer * 0.12f,
                        color(layer ? 25 : 220), 0xFFFFFFFF, 6);
                    previous = point;
                }
            }
        }
    }

    void export_axis() {
        prepare_display_geometry();
        if (display_axis.points.empty()) {
            MD_LOG_ERROR("Lavery: no displayed midpoint axis to export; enable the axis and check pair validity");
            return;
        }
        char path[4096] = {};
        if (!application::file_dialog(path, sizeof(path), application::FileDialogFlag_Save, STR_LIT("csv"))) return;
        md_file_t file = {};
        if (!md_file_open(&file, str_from_cstr(path), MD_FILE_WRITE | MD_FILE_CREATE | MD_FILE_TRUNCATE)) {
            MD_LOG_ERROR("Lavery: could not open displayed-axis CSV '%s'", path);
            return;
        }
        const std::string output = csv_axis(run, app->animation.frame, display_axis);
        bool ok = !output.empty() && md_file_write(file, output.data(), output.size()) == output.size();
        if (!md_file_close(&file)) ok = false;
        if (ok) MD_LOG_INFO("Lavery: exported displayed midpoint axis to '%s'", path);
        else MD_LOG_ERROR("Lavery: displayed-axis CSV write failed; '%s' may be incomplete", path);
    }
};

Lavery instance;

} // namespace
