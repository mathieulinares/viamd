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
#include <bitset>
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

constexpr PickingDomainID PickingDomain_Lavery = HASH_STR_LIT64("picking domain lavery outlier");
constexpr uint32_t kEventAllParameters = (1u << Metric_Count) - 1;

// A member preview requested by hovering a UI row; mask selects the class parameters assessed.
struct Target {
    bool step = false;
    uint32_t index = 0;
    uint32_t mask = 63;
};

// A member drawn in the viewport this frame.
struct Marker {
    bool step = false;
    uint32_t index = 0;
    uint32_t mask = 0;
    uint32_t contributors = 0;
    float z = 0;
    bool lost = false;
};

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
    bool reset_requested = false;
    uint64_t timeline_fit_key = 0;      // what the inspector timeline shows; refit on change
    uint64_t distribution_fit_key = 0;  // what the distribution plot shows; refit on change
    std::vector<Target> preview;
    std::vector<Marker> markers;
    bool markers_explicit = false;
    int baseline = Baseline_MeanSD;
    int statistics_baseline = Baseline_MeanSD;
    int calculation_baseline = Baseline_MeanSD;
    // Parameters used for event detection, independent of the viewed class: bit k is Metric k
    // (bits 0-5 intra-base pair, bits 6-11 inter-base pair).
    uint32_t event_parameters = kEventAllParameters;
    int event_min_frames = 3;
    size_t inspected_event = SIZE_MAX;
    double inspection_frame = -1;
    int event_max_gap = 0;
    int event_neighbourhood = 2;
    bool event_whole_structure = false;
    float event_hysteresis = 0.5f;
    bool event_pair_loss = true;
    bool event_hierarchy = true;
    float event_burst_extra = 1.0f;
    std::vector<int> event_order;      // table order of outlier_events
    bool event_order_dirty = true;
    std::vector<uint32_t> timeline_period_ids; // timeline periods made from events
    std::vector<float> expected_counts;        // chance outliers per frame for the count plot
    uint64_t counts_key = 0;
    uint64_t event_data_key = 0;
    std::vector<uint32_t> event_context_bases;
    std::vector<ImVec4> event_context_colors;
    std::vector<size_t> event_colors;
    PickingRange picking_range = {};
    std::vector<int32_t> atom_base;
    std::vector<int32_t> base_pair;
    std::vector<vec3_t> display_step_from, display_step_to;
    std::vector<uint8_t> display_step_valid;
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
        preview.clear();
        markers.clear();
        markers_explicit = false;
        step_quality_flags.clear();
        outlier_events.clear();
        inspected_event = SIZE_MAX;
        counts_key = 0;
        event_data_key = 0;
        event_context_bases.clear();
        event_context_colors.clear();
        event_colors.clear();
        picking_range = {};
        atom_base.clear();
        base_pair.clear();
        display_step_from.clear();
        display_step_to.clear();
        display_step_valid.clear();
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
                    for (const auto& marker : markers) {
                        const uint32_t fields[5] = {marker.step ? 1u : 0u, marker.index, marker.mask, marker.contributors, marker.lost ? 1u : 0u};
                        key->key = md_hash64(fields, sizeof(fields), key->key);
                        key->key = md_hash64(&marker.z, sizeof(marker.z), key->key);
                    }
                    key->key = md_hash64(&metric, sizeof(metric), key->key);
                    key->key = md_hash64(&selected_parameters, sizeof(selected_parameters), key->key);
                    key->key = md_hash64(&outlier_sigma, sizeof(outlier_sigma), key->key);
                    key->key = md_hash64(&counts_key, sizeof(counts_key), key->key);
                    key->key = md_hash64(&inspected_event, sizeof(inspected_event), key->key);
                    if (!event_context_bases.empty()) {
                        key->key = md_hash64(event_context_bases.data(), event_context_bases.size() * sizeof(uint32_t), key->key);
                        key->key = md_hash64(event_context_colors.data(), event_context_colors.size() * sizeof(ImVec4), key->key);
                    }
                    key->key = md_hash64(&picking_range, sizeof(picking_range), key->key);
                }
                break;
            }
            case viamd::EventType_ViamdPickingRangeReserve: {
                picking_range = {};
                if (overlay_active() && signed_overlay) {
                    picking_range_reserve(&picking_range, (PickingSpace*)e.payload, PickingDomain_Lavery, pairs.size() + steps.size());
                }
                break;
            }
            case viamd::EventType_ViamdPickingTooltipTextRequest: {
                auto* req = (PickingTooltipTextRequest*)e.payload;
                if (req && overlay_active()) fill_tooltip(req);
                break;
            }
            case viamd::EventType_ViamdInteractionSurface: {
                auto* event = (InteractionSurfaceEvent*)e.payload;
                if (event && overlay_active() && event->surface_id == interaction_surface_main &&
                    event->kind == InteractionSurfaceEventKind::Click &&
                    event->selection_mode == InteractionSelectionMode::None &&
                    event->hit.domain == PickingDomain_Lavery) {
                    bool step = false;
                    uint32_t index = 0;
                    if (decode_pick(event->hit.local_idx, &step, &index)) {
                        const size_t frame = current_frame();
                        const auto result = assess_member(frame, step, index, marker_mask(step, index));
                        select_target(step, index, result.peak_metric, frame);
                    }
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
                viamd::write_int(state, STR_LIT("Baseline"), baseline);
                viamd::write_int(state, STR_LIT("EventParameters"), (int)event_parameters);
                viamd::write_int(state, STR_LIT("EventMinFrames"), event_min_frames);
                viamd::write_int(state, STR_LIT("EventMaxGap"), event_max_gap);
                viamd::write_int(state, STR_LIT("EventNeighbourhood"), event_neighbourhood);
                viamd::write_bool(state, STR_LIT("EventWholeStructure"), event_whole_structure);
                viamd::write_flt(state, STR_LIT("EventHysteresis"), event_hysteresis);
                viamd::write_bool(state, STR_LIT("EventPairLoss"), event_pair_loss);
                viamd::write_bool(state, STR_LIT("EventHierarchy"), event_hierarchy);
                viamd::write_flt(state, STR_LIT("EventBurstExtra"), event_burst_extra);
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
                baseline = Baseline_MeanSD;
                event_parameters = kEventAllParameters;
                event_min_frames = 3;
                inspected_event = SIZE_MAX;
                event_max_gap = 0;
                event_neighbourhood = 2;
                event_whole_structure = false;
                event_hysteresis = 0.5f;
                event_pair_loss = true;
                event_hierarchy = true;
                event_burst_extra = 1.0f;
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
                    else if (str_eq(key, STR_LIT("Baseline"))) viamd::extract_enum(baseline, arg, Baseline_Count);
                    else if (str_eq(key, STR_LIT("EventParameters"))) {
                        int mask = (int)kEventAllParameters;
                        viamd::extract_int(mask, arg);
                        event_parameters = (uint32_t)mask & kEventAllParameters;
                    }
                    else if (str_eq(key, STR_LIT("EventMinFrames"))) {
                        viamd::extract_int(event_min_frames, arg);
                        event_min_frames = std::clamp(event_min_frames, 1, 100000);
                    }
                    else if (str_eq(key, STR_LIT("EventMaxGap"))) {
                        viamd::extract_int(event_max_gap, arg);
                        event_max_gap = std::clamp(event_max_gap, 0, 100000);
                    }
                    else if (str_eq(key, STR_LIT("EventNeighbourhood"))) {
                        viamd::extract_int(event_neighbourhood, arg);
                        event_neighbourhood = std::clamp(event_neighbourhood, 0, 1000);
                    }
                    else if (str_eq(key, STR_LIT("EventWholeStructure"))) viamd::extract_bool(event_whole_structure, arg);
                    else if (str_eq(key, STR_LIT("EventHysteresis"))) {
                        viamd::extract_flt(event_hysteresis, arg);
                        event_hysteresis = std::isfinite(event_hysteresis) ? std::clamp(event_hysteresis, 0.0f, 2.0f) : 0.5f;
                    }
                    else if (str_eq(key, STR_LIT("EventPairLoss"))) viamd::extract_bool(event_pair_loss, arg);
                    else if (str_eq(key, STR_LIT("EventHierarchy"))) viamd::extract_bool(event_hierarchy, arg);
                    else if (str_eq(key, STR_LIT("EventBurstExtra"))) {
                        viamd::extract_flt(event_burst_extra, arg);
                        if (!std::isfinite(event_burst_extra) || event_burst_extra < 0.25f || event_burst_extra > 4.0f) {
                            MD_LOG_ERROR("Lavery: EventBurstExtra must be between 0.25 and 4 sigma");
                            event_burst_extra = 1.0f;
                        }
                    }
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
        atom_base.assign(sys->atom.count, -1);
        for (size_t b = 0; b < bases.size(); ++b) {
            for (uint32_t atom : bases[b].atoms) if (atom < atom_base.size()) atom_base[atom] = (int32_t)b;
        }
        base_pair.assign(bases.size(), -1);
        for (size_t p = 0; p < pairs.size(); ++p) base_pair[pairs[p].a] = base_pair[pairs[p].b] = (int32_t)p;
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
        calculation_baseline = baseline;
        const Baseline method = (Baseline)baseline;
        calculation = task_system::create_pool_task(STR_LIT("Lavery trajectory analysis"), [this, sys, frame_count, method]() {
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
                    if (!compute_statistics(&statistics[m], values[m], columns[m], metric_is_angular((Metric)m), method)) {
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
        statistics_baseline = calculation_baseline;
        counts_key = 0;
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
        refresh_event_inspection();
        if (!ImGui::GetIO().KeyCtrl || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) scrubbing_plot = 0;
        preview.clear();
        markers.clear();
        event_context_bases.clear();
        markers_explicit = false;
        const ImVec2 work_size = ImGui::GetMainViewport()->WorkSize;
        ImGui::SetNextWindowSize({work_size.x * 0.9f, work_size.y * 0.9f}, ImGuiCond_FirstUseEver);
        const bool expanded = ImGui::Begin("Lavery", &show_window,
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking);
        auto finish_status = [&]() { ImGui::End(); };
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

        const ImVec2 size = ImGui::GetContentRegionAvail();
        const size_t frame_count = run_num_frames(app);
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float sidebar_width = std::min(size.x * 0.28f, ImGui::GetFontSize() * 20.0f);
        if (ImGui::BeginChild("Settings", ImVec2(sidebar_width, 0))) draw_shared_controls();
        ImGui::EndChild();
        if (reset_requested) {
            reset_requested = false;
            reset();
            ImGui::End();
            return;
        }
        ImGui::SameLine();
        ImGui::BeginGroup();
        if (statistics_baseline != baseline) recompute_statistics();
        const int count = (int)columns[metric];
        const bool ready = count > 0 && frame_count;
        if (ready) member = std::clamp(member, 0, count - 1);
        auto unavailable = [&]() {
            ImGui::TextDisabled("No inter-base-pair links found. Select Intra-base pair.");
        };
        const ImPlotInputMap input = ImPlot::GetInputMap();
        if (ImGui::GetIO().KeyCtrl) ImPlot::GetInputMap().Pan = ImGuiMouseButton_Middle;
        if (frame_count) update_counts(count, frame_count);
        const float events_height = std::max(ImGui::GetFrameHeight() * 4, size.y * 0.34f);
        if (ImGui::BeginChild("Events & quality", ImVec2(0, events_height))) {
            if (frame_count) draw_events_quality(count, frame_count); else unavailable();
        }
        ImGui::EndChild();
        const float overview_width = std::max(1.0f, (ImGui::GetContentRegionAvail().x - gap) * 0.52f);
        if (ImGui::BeginChild("Overview", ImVec2(overview_width, 0))) {
            if (ready) {
                draw_outlier_timeline(count, frame_count);
                if (show_heatmap) draw_heatmap(count, frame_count, statistics[metric].heatmap, &member);
            } else unavailable();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::BeginChild("Inspector", ImVec2(0, 0))) {
            if (ready) draw_inspector(count, frame_count); else unavailable();
        }
        ImGui::EndChild();
        ImGui::EndGroup();
        ImGui::End();
        ImPlot::GetInputMap() = input;
        if (!frame_count) {
            clear_highlight();
            return;
        }
        build_markers(current_frame());
        if (sync_viewport || markers_explicit || inspected_event != SIZE_MAX) update_outlier_highlight();
        else clear_highlight();
    }

    void draw_shared_controls() {
        static const char* bp_items[] = {"Buckle", "Propeller Twist", "Opening", "Shear", "Stretch", "Stagger"};
        static const char* step_items[] = {"Tilt", "Roll", "Twist", "Shift", "Slide", "Rise"};
        ImGui::PushItemWidth(-ImGui::GetFontSize() * 7.5f);
        ImGui::SeparatorText("Selection");
        int group = metric_is_step((Metric)metric) ? 1 : 0;
        if (ImGui::Combo("Class", &group, "Intra-base pair\0Inter-base pair\0")) {
            metric = group * 6 + metric % 6;
            focus_heatmap_selection = true;
            reset_heatmap_view = true;
        }
        const bool step_metric = group != 0;
        const int offset = step_metric ? (int)Tilt : (int)Buckle;
        int local_metric = std::clamp(metric - offset, 0, 5);
        ImGui::Combo("Parameter", &local_metric, step_metric ? step_items : bp_items, 6);
        metric = offset + local_metric;
        const int count = (int)columns[metric];
        if (count <= 0) {
            ImGui::TextDisabled("No consecutive inter-base-pair links were found.");
        } else {
            member = std::clamp(member, 0, count - 1);
            int member_number = member + 1;
            if (ImGui::InputInt(step_metric ? "Step" : "Base pair", &member_number)) {
                member = std::clamp(member_number, 1, count) - 1;
                focus_heatmap_selection = true;
            }
            const auto& labels = step_metric ? step_labels : pair_labels;
            ImGui::TextDisabled("%s (%s)", labels[member].c_str(), metric_unit((Metric)metric));
        }
        int frame_number = (int)std::llround(app->animation.frame);
        if (ImGui::InputInt("Frame", &frame_number)) {
            app->animation.frame = (double)std::clamp(frame_number, 0, (int)run_num_frames(app) - 1);
            app->animation.mode = PlaybackMode::Stopped;
            focus_heatmap_selection = true;
        }
        const size_t frame = nearest_frame(app->animation.frame, nullptr, run_num_frames(app));
        size_t valid_pairs = 0, valid_steps = 0;
        for (size_t p = 0; p < pairs.size(); ++p) if (!pair_quality_flags[frame * pairs.size() + p]) ++valid_pairs;
        for (size_t st = 0; st < steps.size(); ++st) if (!step_quality_flags[frame * steps.size() + st]) ++valid_steps;
        ImGui::TextDisabled("Valid: %zu/%zu pairs, %zu/%zu steps", valid_pairs, pairs.size(), valid_steps, steps.size());
        ImGui::SetItemTooltip("Denominators are fixed tracked identities/links. A missing internal pair invalidates BOTH neighboring links; gaps are never bridged.");

        ImGui::SeparatorText("Outliers");
        ImGui::SliderFloat("Tolerance", &outlier_sigma, 1.5f, 4.0f, "%.1f sigma");
        ImGui::Combo("Baseline", &baseline, "Mean / SD\0Median / MAD\0");
        ImGui::SetItemTooltip("Per-member reference for z-scores.\nMean / SD: classic; large excursions inflate their own SD.\n"
            "Median / MAD: robust; scale is 1.4826 x MAD (SD if MAD is 0).\nAngular parameters are measured on the circle (wrap at +/-180 deg).");
        ImGui::Checkbox("Any selected parameter", &aggregate_outliers);
        ImGui::SetItemTooltip("A member is outlying when any of the chosen parameters of its class is (count plot, heatmap, 3D).");
        if (aggregate_outliers) {
            if (ImGui::BeginTable("##aggregate", 2)) {
                for (int k = 0; k < 6; ++k) {
                    ImGui::TableNextColumn();
                    bool selected = (selected_parameters & (1u << k)) != 0;
                    if (ImGui::Checkbox(metric_name((Metric)(offset + k)), &selected)) {
                        if (selected) selected_parameters |= 1u << k;
                        else selected_parameters &= ~(1u << k);
                    }
                }
                ImGui::EndTable();
            }
            if (!selected_parameters) ImGui::TextDisabled("Select at least one parameter to detect outliers.");
        }

        ImGui::SeparatorText("Events");
        draw_event_settings();

        ImGui::SeparatorText("Display");
        ImGui::Checkbox("Heatmap", &show_heatmap);
        ImGui::Checkbox("Highlight event contributions", &sync_viewport);
        ImGui::SetItemTooltip("Show all active events: event-coloured context halos and current-frame contributors from both classes.");
        ImGui::Checkbox("Signed outlier overlay", &signed_overlay);
        ImGui::Checkbox("Soft overlay halos", &overlay_halo);
        ImGui::Checkbox("Smoothed midpoint axis", &show_axis);
        ImGui::SetItemTooltip("Axis is not the Curves+ screw axis; halos are not emissive bloom.");

        ImGui::SeparatorText("Export & tools");
        const float wide = -FLT_MIN;
        if (ImGui::Button("Export parameter CSV", ImVec2(wide, 0))) export_csv(run_num_frames(app));
        if (ImGui::Button("Export events CSV", ImVec2(wide, 0))) export_events_csv(run_num_frames(app));
        if (ImGui::Button("Export displayed axis CSV", ImVec2(wide, 0))) export_axis();
        if (ImGui::Button("Recompute analysis", ImVec2(wide, 0))) reset_requested = true;
        ImGui::TextDisabled("Canonical A/G/C/T/U; fixed partners identified in frame 0.");
        ImGui::TextDisabled("Script: attr(\"lavery/step/twist\")");
        ImGui::PopItemWidth();
    }

    std::string event_parameters_label() const {
        if (event_parameters == kEventAllParameters) return "all";
        if (event_parameters == 63u) return "intra";
        if (event_parameters == (63u << 6)) return "inter";
        std::string label;
        for (int k = 0; k < (int)Metric_Count; ++k) {
            if (!(event_parameters & (1u << k))) continue;
            if (!label.empty()) label += "+";
            label += metric_name((Metric)k);
        }
        return label.empty() ? "none" : label;
    }

    void draw_event_parameters() {
        ImGui::TextUnformatted("Detect on");
        ImGui::SetItemTooltip("Parameters used to build events. Independent of the class and parameter shown in the plots.\n"
            "With both classes, one event can list base pairs and steps together.");
        struct Preset { const char* name; uint32_t mask; const char* tip; };
        static const Preset presets[] = {
            {"Intra", 63u, "Intra-base pair only: Buckle, Propeller, Opening, Shear, Stretch, Stagger"},
            {"Inter", 63u << 6, "Inter-base pair only: Tilt, Roll, Twist, Shift, Slide, Rise"},
            {"Both", kEventAllParameters, "All 12 parameters"},
        };
        const float width = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
        for (int i = 0; i < 3; ++i) {
            if (i) ImGui::SameLine();
            const bool active = event_parameters == presets[i].mask;
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(presets[i].name, ImVec2(width, 0))) event_parameters = presets[i].mask;
            if (active) ImGui::PopStyleColor();
            ImGui::SetItemTooltip("%s", presets[i].tip);
        }
        if (ImGui::BeginTable("##event_parameters", 2, ImGuiTableFlags_SizingStretchSame)) {
            for (int group = 0; group < 2; ++group) {
                ImGui::TableNextColumn();
                const uint32_t group_mask = 63u << (group * 6);
                const uint32_t chosen = event_parameters & group_mask;
                bool all = chosen == group_mask;
                if (chosen && !all) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
                if (ImGui::Checkbox(group ? "Inter##ev" : "Intra##ev", &all)) {
                    event_parameters = all ? (event_parameters | group_mask) : (event_parameters & ~group_mask);
                }
                if (chosen && !all) ImGui::PopItemFlag();
            }
            for (int k = 0; k < 6; ++k) {
                for (int group = 0; group < 2; ++group) {
                    ImGui::TableNextColumn();
                    const int id = group * 6 + k;
                    bool on = (event_parameters & (1u << id)) != 0;
                    ImGui::PushID(id);
                    if (ImGui::Checkbox(metric_name((Metric)id), &on)) {
                        if (on) event_parameters |= 1u << id;
                        else event_parameters &= ~(1u << id);
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
        if (ImGui::Button("Use plotted parameters", ImVec2(-FLT_MIN, 0))) {
            event_parameters = outlier_parameter_mask() << (metric_is_step((Metric)metric) ? 6 : 0);
        }
        ImGui::SetItemTooltip("Copy the class and parameter(s) of the outlier-count plot.");
        if (!event_parameters) {
            ImGui::TextDisabled(event_pair_loss ? "No parameter: only pair losses form events." : "No parameter: no events.");
        }
    }

    void draw_event_settings() {
        draw_event_parameters();
        ImGui::BeginDisabled(event_whole_structure && !event_hierarchy);
        if (ImGui::InputInt("Neighbourhood", &event_neighbourhood)) event_neighbourhood = std::clamp(event_neighbourhood, 0, 1000);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("In base pairs. Outliers join one event only when they are at most this far apart along the helix\n"
            "(a step sits half-way between its two pairs). Separate duplexes never join.\n"
            "In Whole structure mode, this still sets the local reach of hierarchical child events.");
        ImGui::Checkbox("Whole structure", &event_whole_structure);
        ImGui::SetItemTooltip("Ignore position: every outlier at the same time belongs to one event (global changes such as unwinding).");
        ImGui::Checkbox("Hierarchical events", &event_hierarchy);
        ImGui::SetItemTooltip("Keep broad events and also detect stronger local children inside them.\n"
            "Two levels: parent context and local bursts. Children must involve fewer base pairs than their parent.");
        if (event_hierarchy) {
            ImGui::SliderFloat("Burst extra", &event_burst_extra, 0.25f, 4.0f, "+%.2f sigma");
            ImGui::SetItemTooltip("Child events start at %.2f sigma and continue above %.2f sigma.\n"
                "Uses the same Min frames, Bridge gaps and local Neighbourhood as parents.\n"
                "Pair losses also qualify. Raising this value isolates stronger local bursts.",
                outlier_sigma + event_burst_extra, std::max(0.5f, outlier_sigma + event_burst_extra - event_hysteresis));
        }
        ImGui::SliderFloat("Hysteresis", &event_hysteresis, 0.0f, 2.0f, "%.1f sigma");
        ImGui::SetItemTooltip("An event starts at the tolerance (%.1f sigma) and continues while |z| stays above %.1f sigma,\n"
            "so a value hovering around the threshold does not break one event into many.", outlier_sigma, event_end_sigma());
        ImGui::Checkbox("Pair loss", &event_pair_loss);
        ImGui::SetItemTooltip("Frames where a base pair breaks (no H-bonds, separated, misaligned) count as outlying\n"
            "instead of ending the event. Each lost frame scores the tolerance.");
        if (ImGui::InputInt("Min frames", &event_min_frames)) event_min_frames = std::clamp(event_min_frames, 1, 100000);
        ImGui::SetItemTooltip("Require this many contributing frames for parents and children (default 3).\n"
            "Bridged gaps do not count. Set 1 to retain single-frame spikes.\n"
            "The appropriate minimum depends on trajectory sampling frequency.");
        if (ImGui::InputInt("Bridge gaps", &event_max_gap)) event_max_gap = std::clamp(event_max_gap, 0, 100000);
        ImGui::SetItemTooltip("Merge outliers separated by at most this many frames without outliers or data.");
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
        uint64_t timeline_key = md_hash64(&metric, sizeof(metric), 0x71E1);
        timeline_key = md_hash64(&compare_all, sizeof(compare_all), timeline_key);
        timeline_key = md_hash64(&frame_count, sizeof(frame_count), timeline_key);
        timeline_key = md_hash64(&statistics_baseline, sizeof(statistics_baseline), timeline_key);
        if (!compare_all) timeline_key = md_hash64(&member, sizeof(member), timeline_key);
        if (timeline_key != timeline_fit_key) {
            timeline_fit_key = timeline_key;
            ImPlot::SetNextAxesToFit();
        }
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
            if (statistics_baseline == Baseline_MedianMAD) {
                ImGui::Text("Median %.3f  scaled MAD %.3f  valid %zu/%zu",
                    member_stats.mean, member_stats.sd, member_stats.count, frame_count);
            } else {
                ImGui::Text("Mean %.3f  SD %.3f  valid %zu/%zu",
                    member_stats.mean, member_stats.sd, member_stats.count, frame_count);
            }
            auto& histogram = distributions[metric];
            if (histogram.valid.empty() && !compute_distributions(&histogram, data, (size_t)count)) {
                ImGui::TextUnformatted("Could not calculate distributions. See the application log.");
                return;
            }
            uint64_t distribution_key = md_hash64(&metric, sizeof(metric), 0xD157);
            distribution_key = md_hash64(&member, sizeof(member), distribution_key);
            distribution_key = md_hash64(&frame_count, sizeof(frame_count), distribution_key);
            if (!pinned.empty()) distribution_key = md_hash64(pinned.data(), pinned.size() * sizeof(int), distribution_key);
            if (distribution_key != distribution_fit_key) {
                distribution_fit_key = distribution_key;
                ImPlot::SetNextAxesToFit();
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
        draw_current_outliers(count, frame_count);
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

    // sigma < 0 uses the tolerance. 'strong' receives the mask bits that also reach the tolerance.
    OutlierAssessment assess_member(size_t frame, bool step, size_t index, uint32_t mask, float* z = nullptr,
                                    float sigma = -1, uint32_t* strong = nullptr) const {
        const int offset = step ? (int)Tilt : 0;
        float sample[6];
        MemberStatistics baselines[6];
        if (strong) *strong = 0;
        for (int k = 0; k < 6; ++k) {
            sample[k] = values[offset + k][frame * columns[offset + k] + index];
            baselines[k] = statistics[offset + k].members[index];
            if (z) z[k] = baselines[k].zscore(sample[k]);
            if (strong && (mask & (1u << k)) && baselines[k].is_outlier(sample[k], outlier_sigma)) *strong |= 1u << k;
        }
        return assess_outliers(sample, baselines, 6, mask, sigma < 0 ? outlier_sigma : sigma);
    }

    float event_end_sigma() const { return std::max(0.5f, outlier_sigma - std::max(0.0f, event_hysteresis)); }

    bool pair_lost(size_t frame, size_t pair) const {
        constexpr uint16_t broken = Quality_Separation | Quality_Alignment | Quality_PlaneOffset | Quality_NoPairContacts;
        return statistics[0].members[pair].count && (pair_quality_flags[frame * pairs.size() + pair] & broken) &&
            !(pair_quality_flags[frame * pairs.size() + pair] & ~(broken | Quality_AdjacentPair));
    }

    OutlierAssessment assess(size_t frame, size_t index) const {
        return assess_member(frame, metric_is_step((Metric)metric), index, outlier_parameter_mask());
    }

    size_t current_frame() const {
        const size_t frames = app ? run_num_frames(app) : 0;
        return frames ? nearest_frame(app->animation.frame, nullptr, frames) : 0;
    }

    void refresh_event_inspection() {
        if (inspected_event != SIZE_MAX && (app->animation.mode == PlaybackMode::Playing ||
            app->animation.frame != inspection_frame || inspected_event >= outlier_events.size())) {
            inspected_event = SIZE_MAX;
        }
    }

    bool event_visible(size_t index, size_t frame) const {
        const auto& event = outlier_events[index];
        return (inspected_event == SIZE_MAX || inspected_event == index) &&
            frame >= event.begin && frame <= event.end;
    }

    void inspect_event(size_t index, size_t frame) {
        app->animation.frame = (double)frame;
        app->animation.mode = PlaybackMode::Stopped;
        inspected_event = index;
        inspection_frame = app->animation.frame;
        focus_heatmap_selection = true;
    }

    bool overlay_active() const {
        return app && show_window && computed && !calculating && calculation_ok.load() &&
            !pairs.empty() && statistics[0].members.size() == pairs.size();
    }

    bool decode_pick(uint32_t local, bool* step, uint32_t* index) const {
        if (local < pairs.size()) {
            *step = false;
            *index = local;
            return true;
        }
        if (local - pairs.size() < steps.size()) {
            *step = true;
            *index = local - (uint32_t)pairs.size();
            return true;
        }
        return false;
    }

    uint32_t marker_mask(bool step, uint32_t index) const {
        for (const auto& m : markers) if (m.step == step && m.index == index) return m.mask;
        return 0;
    }

    // Selects a member (switching class when needed), optionally a parameter (0-5), and jumps to a frame.
    void select_target(bool step, uint32_t index, int parameter, size_t frame) {
        const int offset = step ? (int)Tilt : 0;
        const bool class_changed = metric_is_step((Metric)metric) != step;
        metric = offset + (parameter >= 0 && parameter < 6 ? parameter : metric % 6);
        member = (int)index;
        if (class_changed) reset_heatmap_view = true;
        app->animation.frame = (double)frame;
        app->animation.mode = PlaybackMode::Stopped;
        focus_heatmap_selection = true;
        show_window = true;
    }

    void recompute_statistics() {
        for (int m = 0; m < (int)Metric_Count; ++m) {
            if (!compute_statistics(&statistics[m], values[m], columns[m], metric_is_angular((Metric)m), (Baseline)baseline)) {
                MD_LOG_ERROR("Lavery: could not recompute statistics for %s", metric_name((Metric)m));
            }
        }
        statistics_baseline = baseline;
        counts_key = 0;
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
        uint64_t key = md_hash64(&outlier_sigma, sizeof(outlier_sigma), 0x1A7E);
        key = md_hash64(&statistics_baseline, sizeof(statistics_baseline), key);
        key = md_hash64(&event_parameters, sizeof(event_parameters), key);
        key = md_hash64(&event_min_frames, sizeof(event_min_frames), key);
        key = md_hash64(&event_max_gap, sizeof(event_max_gap), key);
        key = md_hash64(&event_neighbourhood, sizeof(event_neighbourhood), key);
        key = md_hash64(&event_whole_structure, sizeof(event_whole_structure), key);
        key = md_hash64(&event_hysteresis, sizeof(event_hysteresis), key);
        key = md_hash64(&event_pair_loss, sizeof(event_pair_loss), key);
        key = md_hash64(&event_hierarchy, sizeof(event_hierarchy), key);
        key = md_hash64(&event_burst_extra, sizeof(event_burst_extra), key);
        key = md_hash64(&frame_count, sizeof(frame_count), key);
        if (event_data_key != key) inspected_event = SIZE_MAX;
        event_data_key = key;
        key = md_hash64(&metric, sizeof(metric), key);
        key = md_hash64(&mask, sizeof(mask), key);
        if (key == counts_key && outlier_counts.size() == frame_count) return;
        counts_key = key;
        const bool step = metric_is_step((Metric)metric);
        const float end_sigma = event_end_sigma();
        const double chance = chance_outlier_probability(outlier_sigma, std::max(1, (int)std::bitset<6>(mask).count()));
        outlier_counts.assign(frame_count, {});
        expected_counts.assign(frame_count, 0.0f);
        std::vector<std::vector<MemberOutlier>> frame_outliers(frame_count);
        float z[6];
        auto record = [&](size_t f, bool member_step, size_t m, uint32_t member_mask) {
            uint32_t strong = 0;
            const auto result = assess_member(f, member_step, m, member_mask, z, end_sigma, &strong);
            MemberOutlier outlier;
            outlier.member = (uint32_t)m;
            outlier.step = member_step;
            outlier.lost = !member_step && event_pair_loss && !result.valid && pair_lost(f, m);
            if (!result.contributors && !outlier.lost) return;
            outlier.contributors = result.contributors;
            outlier.strong = strong;
            std::copy(z, z + 6, outlier.z);
            frame_outliers[f].push_back(outlier);
        };
        for (size_t f = 0; f < frame_count; ++f) {
            for (int m = 0; m < count; ++m) {
                const auto result = assess_member(f, step, (size_t)m, mask);
                if (result.valid) ++outlier_counts[f].valid;
                if (result.contributors) ++outlier_counts[f].outliers;
            }
            expected_counts[f] = (float)(chance * (double)outlier_counts[f].valid);
            for (const bool member_step : {false, true}) {
                const uint32_t class_mask = (event_parameters >> (member_step ? 6 : 0)) & 63u;
                // A lost pair joins events even when no intra-base-pair parameter is tested.
                if (!class_mask && (member_step || !event_pair_loss)) continue;
                const size_t members = member_step ? steps.size() : pairs.size();
                for (size_t m = 0; m < members; ++m) {
                    if (class_mask || pair_lost(f, m)) record(f, member_step, m, class_mask);
                }
            }
        }
        EventOptions options;
        options.min_frames = (size_t)std::max(1, event_min_frames);
        options.max_gap = (size_t)std::max(0, event_max_gap);
        options.neighbourhood = event_whole_structure ? -1 : std::max(0, event_neighbourhood);
        options.lost_weight = outlier_sigma;
        options.burst_sigma = event_hierarchy ? outlier_sigma + event_burst_extra : 0;
        options.burst_hysteresis = event_hysteresis;
        options.burst_neighbourhood = event_neighbourhood;
        outlier_events = detect_events(frame_outliers, pairs.size(), steps, options);
        event_colors = event_color_slots(outlier_events);
        event_order_dirty = true;
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
        double observed = 0, expected = 0;
        for (size_t f = 0; f < frame_count; ++f) {
            observed += (double)outlier_counts[f].outliers;
            expected += expected_counts[f];
        }
        ImGui::Text("Over the trajectory: %.0f outlying member-frames, ~%.0f expected by chance", observed, expected);
        ImGui::SetItemTooltip("Chance level assumes Gaussian, independent parameters: each tested parameter exceeds %.1f sigma\n"
            "with probability %.2g. Observed counts near the dashed line are mostly noise; raise the tolerance\n"
            "or Min frames, or rely on events with a high score.", outlier_sigma, chance_outlier_probability(outlier_sigma, 1));
        ImGui::TextDisabled("Ctrl-click / Ctrl-drag: jump / scrub. Shaded bands: outlier events. Dashed: expected by chance.");
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
            draw_expected_line(x_at, frame_count);
            draw_event_bands(x_at);
            scrub_timeline("##outlier_scrub", times, frame_count);
            draw_time_cursor(times, frame_count);
            const bool hovered = ImPlot::IsPlotHovered();
            if (hovered) {
                const size_t f = nearest_frame(ImPlot::GetPlotMousePos().x, times, frame_count);
                const auto& sample = outlier_counts[f];
                ImGui::BeginTooltip();
                ImGui::Text("Frame %zu | %s: %.3f", f, x_label, x_at(f));
                ImGui::Text("%zu outliers / %zu valid (%d total); ~%.2f expected by chance", sample.outliers, sample.valid, count,
                    (double)expected_counts[f]);
                for (size_t e = 0; e < outlier_events.size(); ++e) {
                    const auto& event = outlier_events[e];
                    if (f < event.begin || f > event.end) continue;
                    const std::string kind = event_kind(event);
                    ImGui::Text("Event #%zu: %s, bp %u-%u, peak %.2f sigma", e + 1, kind.empty() ? "-" : kind.c_str(),
                        event.pair_first + 1, event.pair_last + 1, event.severity);
                }
                ImGui::EndTooltip();
            }
            ImPlot::EndPlot();
        }
        ImGui::PopID();
        ImGui::PopID();
    }

    template <typename X>
    void draw_expected_line(X&& x_at, size_t frame_count) const {
        if (expected_counts.size() != frame_count || frame_count < 2) return;
        ImDrawList* draw = ImPlot::GetPlotDrawList();
        const ImU32 color = ImGui::GetColorU32(ImVec4(0.75f, 0.75f, 0.75f, 0.8f));
        constexpr float dash = 6, gap = 4;
        float phase = 0;
        ImPlot::PushPlotClipRect();
        for (size_t f = 1; f < frame_count; ++f) {
            const ImVec2 a = ImPlot::PlotToPixels(x_at(f - 1), expected_counts[f - 1]);
            const ImVec2 b = ImPlot::PlotToPixels(x_at(f), expected_counts[f]);
            const float length = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
            if (!(length > 0)) continue;
            for (float t = 0; t < length;) {
                const float period = dash + gap;
                const float in = std::fmod(phase + t, period);
                const float step = std::min(length - t, in < dash ? dash - in : period - in);
                if (in < dash) {
                    draw->AddLine(ImVec2(a.x + (b.x - a.x) * t / length, a.y + (b.y - a.y) * t / length),
                        ImVec2(a.x + (b.x - a.x) * (t + step) / length, a.y + (b.y - a.y) * (t + step) / length), color, 1.0f);
                }
                t += std::max(step, 0.01f);
            }
            phase = std::fmod(phase + length, dash + gap);
        }
        ImPlot::PopPlotClipRect();
    }

    template <typename X>
    void draw_event_bands(X&& x_at) const {
        if (outlier_events.empty()) return;
        ImDrawList* draw = ImPlot::GetPlotDrawList();
        const ImPlotRect limits = ImPlot::GetPlotLimits();
        const size_t frame = current_frame();
        ImPlot::PushPlotClipRect();
        for (size_t e = 0; e < outlier_events.size(); ++e) {
            const auto& event = outlier_events[e];
            ImVec2 a = ImPlot::PlotToPixels(x_at(event.begin), limits.Y.Max);
            ImVec2 b = ImPlot::PlotToPixels(x_at(event.end), limits.Y.Min);
            if (b.x - a.x < 3) { const float c = 0.5f * (a.x + b.x); a.x = c - 1.5f; b.x = c + 1.5f; }
            const bool current = frame >= event.begin && frame <= event.end;
            ImVec4 tint = event_color(e);
            tint.w = current ? 0.28f : 0.14f;
            draw->AddRectFilled(a, b, ImGui::GetColorU32(tint));
        }
        ImPlot::PopPlotClipRect();
    }

    void draw_events_quality(int count, size_t frame_count) {
        const size_t frame = (size_t)std::clamp((int64_t)std::llround(app->animation.frame), (int64_t)0, (int64_t)frame_count - 1);
        const auto& labels = metric_is_step((Metric)metric) ? step_labels : pair_labels;
        if (!ImGui::BeginTabBar("EventsQuality")) return;
        if (ImGui::BeginTabItem("Outlier events")) {
            draw_events(frame, frame_count);
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

    static ImVec4 sign_color(float z, float alpha) {
        return z > 0 ? ImVec4(1.0f, 0.255f, 0.373f, alpha) : ImVec4(0.157f, 0.843f, 1.0f, alpha);
    }

    ImVec4 event_color(size_t event) const {
        static const ImVec4 palette[] = {
            {1.0f, 0.78f, 0.30f, 1}, {0.62f, 0.48f, 1.0f, 1},
            {0.30f, 0.85f, 0.48f, 1}, {1.0f, 0.48f, 0.72f, 1},
            {0.35f, 0.60f, 1.0f, 1}, {1.0f, 0.52f, 0.25f, 1},
            {0.30f, 0.85f, 0.78f, 1}, {0.83f, 0.83f, 0.35f, 1},
        };
        const size_t slot = event_colors[event];
        if (slot < sizeof(palette) / sizeof(palette[0])) return palette[slot];
        ImVec4 color(0, 0, 0, 1);
        ImGui::ColorConvertHSVtoRGB(std::fmod((float)slot * 0.61803398875f, 1.0f), 0.55f, 1.0f, color.x, color.y, color.z);
        return color;
    }

    void draw_events(size_t frame, size_t frame_count) {
        const float* times = timeline_times(frame_count);
        char unit[64];
        timeline_label(unit, sizeof(unit), times);
        if (ImGui::Button("All to timeline")) {
            for (int e : event_order) if (!add_event_period((size_t)e)) break;
        }
        ImGui::SetItemTooltip("Add every event, in table order, as a period on the main timeline (VIAMD holds up to %d periods).", TIMELINE_MAX_PERIODS);
        ImGui::SameLine();
        ImGui::BeginDisabled(timeline_period_ids.empty());
        if (ImGui::Button("Remove from timeline")) remove_event_periods();
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Remove the timeline periods added from Lavery events.");

        const size_t children = (size_t)std::count_if(outlier_events.begin(), outlier_events.end(),
            [](const OutlierEvent& event) { return event.parent != SIZE_MAX; });
        ImGui::Text("%zu parents + %zu local bursts | start %.1f sigma, continue %.1f sigma | %s | duration in %s",
            outlier_events.size() - children, children, outlier_sigma,
            event_end_sigma(), event_whole_structure ? "whole structure" : "local", unit);
        ImGui::TextDisabled("Click: inspect only this event at peak. Play or change frame: all active events. Hover: details.");
        if (!ImGui::BeginTable("Events", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Sortable)) return;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Event / member", ImGuiTableColumnFlags_WidthStretch, 1.8f);
        ImGui::TableSetupColumn("Region", ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn("Frames / span", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Peak frame", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableSetupColumn("Peak sigma", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_PreferSortDescending, 0.5f);
        ImGui::TableSetupColumn("Score", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort |
            ImGuiTableColumnFlags_PreferSortDescending, 0.5f);
        ImGui::TableSetupColumn("Actions / parameters (peak sigma)", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort, 2.6f);
        ImGui::TableHeadersRow();
        sort_events(ImGui::TableGetSortSpecs(), times);

        for (int e : event_order) {
            const auto& event = outlier_events[(size_t)e];
            ImGui::PushID(e);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const ImVec2 swatch = ImGui::GetCursorScreenPos();
            const float side = ImGui::GetFontSize() * 0.65f;
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(swatch.x, swatch.y + 3), ImVec2(swatch.x + side, swatch.y + 3 + side),
                ImGui::GetColorU32(event_color((size_t)e)), 2.0f);
            ImGui::Dummy(ImVec2(side, ImGui::GetTextLineHeight()));
            ImGui::SameLine();
            char label[64];
            if (event.parent == SIZE_MAX) snprintf(label, sizeof(label), "#%d  %zu - %zu", e + 1, event.begin, event.end);
            else snprintf(label, sizeof(label), "#%d  child of #%zu", e + 1, event.parent + 1);
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_OpenOnArrow |
                ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_AllowOverlap;
            if (event_visible((size_t)e, frame)) flags |= ImGuiTreeNodeFlags_Selected;
            if (event.members.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
            const bool open = ImGui::TreeNodeEx(label, flags);
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                inspect_event((size_t)e, event.peak);
            }
            const std::string kind = event_kind(event);
            if (ImGui::IsItemHovered()) {
                for (const auto& m : event.members) preview.push_back({m.step, m.member, m.parameters});
                char hierarchy[128] = {};
                if (event.parent != SIZE_MAX) snprintf(hierarchy, sizeof(hierarchy),
                    "\nLocal burst inside #%zu; start %.2f sigma, continue %.2f sigma",
                    event.parent + 1, event.burst_sigma, std::max(0.5f, event.burst_sigma - event_hysteresis));
                ImGui::SetTooltip("Event #%d: %s\nBase pairs %u-%u, frames %zu-%zu (%zu outlying)\nPeak %.2f sigma at frame %zu; "
                    "up to %zu members outlying at once\nScore %.1f (sum of |z| over member-frames)%s%s",
                    e + 1, kind.empty() ? "-" : kind.c_str(), event.pair_first + 1, event.pair_last + 1, event.begin, event.end,
                    event.outlier_frames, event.severity, event.peak, event.peak_count, event.score,
                    event.lost_samples ? "\nIncludes frames where a base pair was lost" : "", hierarchy);
            }
            size_t bp = 0, st = 0;
            for (const auto& m : event.members) (m.step ? st : bp)++;
            ImGui::TableNextColumn();
            if (event.pair_first == event.pair_last) ImGui::Text("bp %u", event.pair_first + 1);
            else ImGui::Text("bp %u-%u", event.pair_first + 1, event.pair_last + 1);
            ImGui::TableNextColumn();
            const int dominant = event_dominant_metric(event);
            if (dominant >= 0) ImGui::TextColored(sign_color(event.metric_signed[dominant], 1), "%s", kind.c_str());
            else ImGui::TextUnformatted(kind.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%zu contributing %s", event.outlier_frames, event.outlier_frames == 1 ? "frame" : "frames");
            if (times) ImGui::TextDisabled("%.4g %s span", (double)times[event.end] - times[event.begin], unit);
            else ImGui::TextDisabled("%zu-frame span", event.end - event.begin + 1);
            ImGui::SetItemTooltip("Contributing frames exclude bridged gaps.\n"
                "Frame span includes both endpoints; elapsed time is last time minus first time.\n"
                "A single sample is 1 frame, with zero elapsed time.");
            ImGui::TableNextColumn(); ImGui::Text("%zu", event.peak);
            ImGui::TableNextColumn(); ImGui::Text("%.2f", event.severity);
            ImGui::TableNextColumn(); ImGui::Text("%.1f", event.score);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Select")) select_event_residues(event);
            ImGui::SetItemTooltip("Replace the VIAMD selection with the nucleotides of this event's members.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Timeline")) add_event_period((size_t)e);
            ImGui::SetItemTooltip("Add this event as a period on the main timeline.");
            ImGui::SameLine();
            ImGui::TextDisabled("%zu bp, %zu steps", bp, st);
            if (open) {
                for (const auto& m : event.members) draw_event_member(m, (size_t)e);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    void sort_events(ImGuiTableSortSpecs* specs, const float* times) {
        const bool stale = event_order.size() != outlier_events.size();
        if (!stale && !event_order_dirty && !(specs && specs->SpecsDirty)) return;
        event_order.resize(outlier_events.size());
        for (size_t i = 0; i < event_order.size(); ++i) event_order[i] = (int)i;
        if (specs && specs->SpecsCount > 0) {
            const ImGuiTableColumnSortSpecs spec = specs->Specs[0];
            auto duration = [&](const OutlierEvent& e) {
                return (double)e.outlier_frames;
            };
            std::vector<std::string> kinds;
            if (spec.ColumnIndex == 2) for (const auto& e : outlier_events) kinds.push_back(event_kind(e));
            std::stable_sort(event_order.begin(), event_order.end(), [&](int ia, int ib) {
                const OutlierEvent& a = outlier_events[(size_t)ia];
                const OutlierEvent& b = outlier_events[(size_t)ib];
                int c = 0;
                switch (spec.ColumnIndex) {
                case 0: c = a.begin < b.begin ? -1 : a.begin > b.begin; break;
                case 1: c = a.pair_first < b.pair_first ? -1 : a.pair_first > b.pair_first; break;
                case 2: c = kinds[(size_t)ia].compare(kinds[(size_t)ib]); break;
                case 3: c = duration(a) < duration(b) ? -1 : duration(a) > duration(b); break;
                case 4: c = a.peak < b.peak ? -1 : a.peak > b.peak; break;
                case 5: c = a.severity < b.severity ? -1 : a.severity > b.severity; break;
                default: c = a.score < b.score ? -1 : a.score > b.score; break;
                }
                return spec.SortDirection == ImGuiSortDirection_Descending ? c > 0 : c < 0;
            });
        }
        if (specs) specs->SpecsDirty = false;
        event_order_dirty = false;
    }

    template <typename F>
    void for_each_event_base(const OutlierEvent& event, F&& f) const {
        for (const auto& m : event.members) {
            if (m.step) {
                if (m.member >= steps.size()) continue;
                for (uint32_t p : {steps[m.member].a, steps[m.member].b}) { f(pairs[p].a); f(pairs[p].b); }
            } else if (m.member < pairs.size()) {
                f(pairs[m.member].a);
                f(pairs[m.member].b);
            }
        }
    }

    void select_event_residues(const OutlierEvent& event) {
        auto& mask = app->selection.selection_mask;
        md_bitmask_clear(&mask);
        for_each_event_base(event, [&](uint32_t base) {
            if (base < bases.size()) for (uint32_t atom : bases[base].atoms) md_bitmask_set_bit(&mask, atom);
        });
    }

    bool add_event_period(size_t index) {
        if (!app || index >= outlier_events.size()) return false;
        const OutlierEvent& event = outlier_events[index];
        const std::string kind = event_kind(event);
        char name[TIMELINE_PERIOD_NAME_CAP];
        snprintf(name, sizeof(name), "Lavery #%zu %s bp %u-%u", index + 1, kind.c_str(), event.pair_first + 1, event.pair_last + 1);
        auto& periods = app->timeline.periods;
        for (int i = 0; i < periods.count; ++i) {
            const TimelinePeriod& r = periods.items[i];
            if (r.beg == (int32_t)event.begin && r.end == (int32_t)event.end && !strcmp(r.name, name)) return true;
        }
        TimelinePeriod* period = timeline_periods_add(&periods, name, (int32_t)event.begin, (int32_t)event.end, false);
        if (!period) {
            MD_LOG_ERROR("Lavery: the timeline holds at most %d periods; remove some to add more events", TIMELINE_MAX_PERIODS);
            return false;
        }
        std::string note;
        char line[256];
        snprintf(line, sizeof(line), "%s\nBase pairs %u-%u, frames %zu-%zu\nPeak %.2f sigma at frame %zu, score %.1f\n",
            kind.empty() ? "Lavery outlier event" : kind.c_str(), event.pair_first + 1, event.pair_last + 1, event.begin, event.end,
            event.severity, event.peak, event.score);
        note = line;
        if (event.parent != SIZE_MAX) {
            snprintf(line, sizeof(line), "Child of #%zu; burst threshold %.2f sigma\n", event.parent + 1, event.burst_sigma);
            note += line;
        }
        note += "Members:";
        size_t shown = 0;
        for (const auto& m : event.members) {
            const auto& labels = m.step ? step_labels : pair_labels;
            if (m.member >= labels.size()) continue;
            if (++shown > 6) { note += "\n..."; break; }
            note += "\n";
            note += labels[m.member];
        }
        snprintf(period->note, sizeof(period->note), "%s", note.c_str());
        const ImVec4 c = event_color(index);
        period->color = vec4_set(c.x, c.y, c.z, 1);
        timeline_period_ids.push_back(period->id);
        return true;
    }

    void remove_event_periods() {
        if (!app) return;
        for (uint32_t id : timeline_period_ids) {
            if (timeline_periods_find(&app->timeline.periods, id)) timeline_periods_remove(&app->timeline.periods, id);
        }
        timeline_period_ids.clear();
    }

    void draw_event_member(const EventMember& m, size_t event_index) {
        const auto& labels = m.step ? step_labels : pair_labels;
        if (m.member >= labels.size()) return;
        const int offset = m.step ? (int)Tilt : 0;
        ImGui::PushID((int)(m.member * 2 + (m.step ? 1 : 0)));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        char text[512];
        snprintf(text, sizeof(text), "%s %s", m.step ? "[inter]" : "[intra]", labels[m.member].c_str());
        const bool selected = member == (int)m.member && metric_is_step((Metric)metric) == m.step;
        ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
        if (ImGui::Selectable(text, selected)) {
            select_target(m.step, m.member, m.peak_parameter, m.peak);
            inspect_event(event_index, m.peak);
        }
        ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
        if (ImGui::IsItemHovered()) {
            preview.push_back({m.step, m.member, m.parameters});
            if (m.peak_parameter >= 0) {
                ImGui::SetTooltip("%s\nClick: select it, its strongest parameter (%s) and peak frame %zu.",
                    m.step ? "Inter-base pair (step)" : "Intra-base pair", metric_name((Metric)(offset + m.peak_parameter)), m.peak);
            } else {
                ImGui::SetTooltip("Intra-base pair, lost (broken) in %zu frames\nClick: select it at frame %zu.", m.lost_frames, m.peak);
            }
        }
        ImGui::TableNextColumn();
        ImGui::TableNextColumn();
        ImGui::TableNextColumn();
        if (m.lost_frames) ImGui::TextDisabled("%zu frames (%zu lost)", m.frames, m.lost_frames);
        else ImGui::TextDisabled("%zu frames", m.frames);
        ImGui::TableNextColumn(); ImGui::Text("%zu", m.peak);
        ImGui::TableNextColumn();
        if (m.peak_parameter >= 0) ImGui::TextColored(sign_color(m.peak_z, 1), "%+.2f", m.peak_z);
        else ImGui::TextDisabled("-");
        ImGui::TableNextColumn();
        ImGui::TableNextColumn();
        bool first = true;
        if (m.lost_frames) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.55f, 0.55f, 0.45f));
            if (ImGui::SmallButton("Pair lost##lost")) {
                select_target(m.step, m.member, m.peak_parameter, m.peak);
                inspect_event(event_index, m.peak);
            }
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("This base pair broke (no H-bonds, separated or misaligned) in %zu frames of the event.", m.lost_frames);
            first = false;
        }
        for (int k = 0; k < 6; ++k) {
            if (!(m.parameters & (1u << k))) continue;
            const int id = offset + k;
            if (!first) ImGui::SameLine();
            first = false;
            char chip[64];
            snprintf(chip, sizeof(chip), "%s %+.1f##p%d", metric_name((Metric)id), m.parameter_peak_z[k], k);
            ImGui::PushStyleColor(ImGuiCol_Button, sign_color(m.parameter_peak_z[k], 0.35f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, sign_color(m.parameter_peak_z[k], 0.6f));
            const bool active = selected && metric == id;
            if (active) ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            if (ImGui::SmallButton(chip)) {
                select_target(m.step, m.member, k, m.parameter_peak[k]);
                inspect_event(event_index, m.parameter_peak[k]);
            }
            if (active) ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) {
                preview.push_back({m.step, m.member, 1u << k});
                const size_t f = m.parameter_peak[k];
                const float v = values[id][f * columns[id] + m.member];
                ImGui::SetTooltip("%s at peak frame %zu: %.3f %s (%+.2f sigma)\nClick: show %s for this member at that frame.",
                    metric_name((Metric)id), f, v, metric_unit((Metric)id), m.parameter_peak_z[k], metric_name((Metric)id));
            }
        }
        ImGui::PopID();
    }

    void export_events_csv(size_t frames) {
        char path[4096] = {};
        if (!application::file_dialog(path, sizeof(path), application::FileDialogFlag_Save, STR_LIT("csv"))) return;
        md_file_t file = {};
        if (!md_file_open(&file, str_from_cstr(path), MD_FILE_WRITE | MD_FILE_CREATE | MD_FILE_TRUNCATE)) {
            MD_LOG_ERROR("Lavery: could not open events CSV export '%s'", path);
            return;
        }
        const float* times = timeline_times(frames);
        char unit[32] = "frame";
        if (times) md_unit_print(unit, sizeof(unit), app->timeline.time_unit);
        std::string buffer = csv_event_header();
        bool ok = true;
        for (size_t e = 0; e < outlier_events.size() && ok; ++e) {
            const auto& event = outlier_events[e];
            for (const auto& m : event.members) {
                const int offset = m.step ? (int)Tilt : 0;
                const auto& labels = m.step ? step_labels : pair_labels;
                for (int k = 0; k < 6; ++k) {
                    const int id = offset + k;
                    CsvEventRow row;
                    row.run = run;
                    row.event = e + 1;
                    row.begin = event.begin;
                    row.end = event.end;
                    row.begin_time = times ? (double)times[event.begin] : (double)event.begin;
                    row.end_time = times ? (double)times[event.end] : (double)event.end;
                    row.time_unit = unit;
                    row.peak = event.peak;
                    row.severity = event.severity;
                    row.score = event.score;
                    row.kind = event_kind(event);
                    row.pair_first = event.pair_first + 1;
                    row.pair_last = event.pair_last + 1;
                    row.sigma = event.parent == SIZE_MAX ? outlier_sigma : event.burst_sigma;
                    row.end_sigma = std::max(0.5f, row.sigma - event_hysteresis);
                    row.neighbourhood = event.parent == SIZE_MAX && event_whole_structure ? -1 : event_neighbourhood;
                    row.parent_event = event.parent == SIZE_MAX ? 0 : event.parent + 1;
                    row.burst_sigma = event.burst_sigma;
                    row.member_lost = m.lost_frames;
                    row.scope = event_parameters_label();
                    row.baseline = statistics_baseline == Baseline_MedianMAD ? "median_mad" : "mean_sd";
                    row.metric = (Metric)id;
                    row.member = m.member + 1;
                    row.label = labels[m.member];
                    row.member_frames = m.frames;
                    row.contributing = (m.parameters & (1u << k)) != 0;
                    row.parameter_peak = row.contributing ? m.parameter_peak[k] : m.peak;
                    row.value = values[id][row.parameter_peak * columns[id] + m.member];
                    row.parameter_z = row.contributing ? m.parameter_peak_z[k]
                        : statistics[id].members[m.member].zscore(row.value);
                    buffer += csv_event_row(row);
                }
            }
            if (buffer.size() >= 65536) {
                if (md_file_write(file, buffer.data(), buffer.size()) != buffer.size()) ok = false;
                buffer.clear();
            }
        }
        if (ok && !buffer.empty() && md_file_write(file, buffer.data(), buffer.size()) != buffer.size()) ok = false;
        if (!md_file_close(&file)) ok = false;
        if (!ok) MD_LOG_ERROR("Lavery: events CSV export failed; '%s' may be incomplete", path);
        else MD_LOG_INFO("Lavery: exported events CSV to '%s'", path);
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
                sample.baseline_method = (Baseline)statistics_baseline;
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

    void draw_current_outliers(int count, size_t frame_count) {
        const auto& labels = metric_is_step((Metric)metric) ? step_labels : pair_labels;
        const size_t frame = (size_t)std::clamp((int64_t)std::llround(app->animation.frame),
            (int64_t)0, (int64_t)frame_count - 1);
        if (ImGui::CollapsingHeader("Current-frame outliers (all members)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Hover: add a paused 3D preview (unless inspecting an event). Click: select.");
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
                    preview.push_back({metric_is_step((Metric)metric), (uint32_t)m, outlier_parameter_mask()});
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

    void build_markers(size_t frame) {
        refresh_event_inspection();
        markers.clear();
        event_context_bases.clear();
        if (sync_viewport || inspected_event != SIZE_MAX) {
            event_context_colors.assign(bases.size(), ImVec4(0, 0, 0, 0));
            for (size_t e = 0; e < outlier_events.size(); ++e) {
                const auto& event = outlier_events[e];
                if (!event_visible(e, frame)) continue;
                std::vector<uint32_t> region;
                for_each_event_base(event, [&](uint32_t base) { region.push_back(base); });
                std::sort(region.begin(), region.end());
                region.erase(std::unique(region.begin(), region.end()), region.end());
                const ImVec4 color = event_color(e);
                // Local children remain visible inside a broad parent's context halo.
                const float weight = event.parent == SIZE_MAX ? 1.0f : 3.0f;
                for (uint32_t base : region) {
                    event_context_bases.push_back(base);
                    auto& blended = event_context_colors[base];
                    blended.x += color.x * weight;
                    blended.y += color.y * weight;
                    blended.z += color.z * weight;
                    blended.w += weight;
                }
            }
            std::sort(event_context_bases.begin(), event_context_bases.end());
            event_context_bases.erase(std::unique(event_context_bases.begin(), event_context_bases.end()), event_context_bases.end());
            for (uint32_t base : event_context_bases) {
                auto& color = event_context_colors[base];
                color.x /= color.w;
                color.y /= color.w;
                color.z /= color.w;
                color.w = 1.0f;
            }
            for (const auto& contribution : event_contributions(outlier_events, frame, inspected_event)) {
                const auto result = assess_member(frame, contribution.step, contribution.member,
                    contribution.parameters, nullptr, event_end_sigma());
                markers.push_back({contribution.step, contribution.member, contribution.parameters, contribution.parameters,
                    std::isfinite(result.peak_z) ? result.peak_z : 0.0f, contribution.lost});
            }
        }
        markers_explicit = inspected_event == SIZE_MAX && !preview.empty() && app->animation.mode != PlaybackMode::Playing;
        if (markers_explicit) {
            for (const auto& target : preview) {
                const size_t members = target.step ? steps.size() : pairs.size();
                if (target.index >= members) continue;
                const bool duplicate = std::any_of(markers.begin(), markers.end(), [&](const Marker& m) {
                    return m.step == target.step && m.index == target.index;
                });
                if (duplicate) continue;
                const auto result = assess_member(frame, target.step, target.index, target.mask);
                markers.push_back({target.step, target.index, target.mask, result.contributors,
                    std::isfinite(result.peak_z) ? result.peak_z : 0.0f});
            }
        }
    }

    void update_outlier_highlight() {
        auto& mask = app->selection.highlight_mask;
        const uint64_t previous_hash = md_bitset_hash64(md_bitmask_view(&mask), 0);
        if (highlight_owned && previous_hash != highlight_hash) highlight_owned = false;
        // Explicit row hover takes precedence; automatic highlighting yields to other hover feedback.
        if (!markers_explicit && !highlight_owned && !md_bitmask_empty(&mask)) return;

        md_bitmask_clear(&mask);
        auto add_pair = [&](uint32_t index) {
            const Pair& pair = pairs[index];
            for (uint32_t base_index : {pair.a, pair.b}) {
                for (uint32_t atom : bases[base_index].atoms) md_bitmask_set_bit(&mask, atom);
            }
        };
        for (const auto& marker : markers) {
            if (marker.step) {
                add_pair(steps[marker.index].a);
                add_pair(steps[marker.index].b);
            } else {
                add_pair(marker.index);
            }
        }
        highlight_owned = !md_bitmask_empty(&mask);
        highlight_hash = md_bitset_hash64(md_bitmask_view(&mask), 0);
        if (highlight_hash != previous_hash) {
            viamd::event_system_broadcast_event(viamd::EventType_ViamdHighlightMaskChanged,
                viamd::EventPayloadType_ApplicationState, app);
        }
    }

    void tooltip_member(PickingTooltipTextRequest* req, bool step, uint32_t index, uint32_t mask) const {
        const auto& labels = step ? step_labels : pair_labels;
        if (index >= labels.size()) return;
        const size_t frame = current_frame();
        const int offset = step ? (int)Tilt : 0;
        float z[6];
        const auto result = assess_member(frame, step, index, mask, z);
        tooltip_title(req, "Lavery %s" TOOLTIP_SEP "%s", step ? "inter-base pair" : "intra-base pair", labels[index].c_str());
        uint32_t event_parameters_now = 0;
        for (size_t e = 0; e < outlier_events.size(); ++e) {
            const auto& event = outlier_events[e];
            if ((!sync_viewport && inspected_event == SIZE_MAX) || !event_visible(e, frame)) continue;
            bool context = false;
            const EventMember::Sample* contribution = nullptr;
            for (const auto& m : event.members) {
                if (m.step == step && m.member == index) contribution = event_member_sample(m, frame);
                if (!step) {
                    context |= m.step ? steps[m.member].a == index || steps[m.member].b == index : m.member == index;
                } else {
                    context |= m.step && m.member == index;
                }
            }
            if (context) {
                tooltip_row(req, STR_LIT("event"), "#%zu" TOOLTIP_SEP "%s" TOOLTIP_SEP "frames %zu-%zu",
                    e + 1, event_kind(event).c_str(), event.begin, event.end);
                if (event.parent != SIZE_MAX) tooltip_row(req, STR_LIT("hierarchy"),
                    "Local burst inside #%zu" TOOLTIP_SEP "%.2f sigma", event.parent + 1, event.burst_sigma);
                tooltip_row(req, STR_LIT("event detail"), "%s", !contribution ? "Context only in this frame" :
                    contribution->lost ? "Pair lost" : "Contributing in this frame");
                if (contribution) {
                    event_parameters_now |= contribution->parameters;
                    std::string names;
                    for (int k = 0; k < 6; ++k) {
                        if (!(contribution->parameters & (1u << k))) continue;
                        if (!names.empty()) names += ", ";
                        names += metric_name((Metric)(offset + k));
                    }
                    if (!names.empty()) tooltip_row(req, STR_LIT("event parameters"), "%s", names.c_str());
                }
            }
        }
        tooltip_row(req, STR_LIT("frame"), "%zu" TOOLTIP_SEP "%.1f sigma" TOOLTIP_SEP "%s", frame, outlier_sigma,
            baseline_name((Baseline)statistics_baseline));
        for (int k = 0; k < 6; ++k) {
            const int id = offset + k;
            const float v = values[id][frame * columns[id] + index];
            const str_t key = str_from_cstr(metric_name((Metric)id));
            if (!std::isfinite(v)) {
                tooltip_row(req, key, "no valid sample");
                continue;
            }
            const char* unit = metric_is_angular((Metric)id) ? "\xC2\xB0" : " " TOOLTIP_ANGSTROM;
            const bool contributes = (event_parameters_now & (1u << k)) != 0;
            const bool outlier = (result.contributors & (1u << k)) != 0;
            tooltip_row(req, key, "%.2f%s" TOOLTIP_SEP "%+.2f sigma%s", v, unit, z[k],
                contributes ? "  EVENT CONTRIBUTOR" :
                outlier ? (k == result.peak_metric ? "  OUTLIER (peak)" : "  OUTLIER") : "");
        }
        const uint16_t flags = step ? step_quality_flags[frame * steps.size() + index] : pair_quality_flags[frame * pairs.size() + index];
        for (uint16_t bit = 1; bit && bit <= Quality_Last; bit <<= 1) {
            if (flags & bit) tooltip_row(req, STR_LIT("quality"), "%s", quality_description(bit));
        }
    }

    void tooltip_pair(PickingTooltipTextRequest* req, uint32_t pair) const {
        bool described = false;
        for (const auto& marker : markers) {
            const bool involved = marker.step
                ? steps[marker.index].a == pair || steps[marker.index].b == pair : marker.index == pair;
            if (involved) {
                tooltip_member(req, marker.step, marker.index, marker.mask);
                described = true;
            }
        }
        if (!described) tooltip_member(req, false, pair, 0);
    }

    void fill_tooltip(PickingTooltipTextRequest* req) const {
        const PickingHit& hit = req->hit;
        if (hit.domain == PickingDomain_Lavery) {
            bool step = false;
            uint32_t index = 0;
            if (decode_pick(hit.local_idx, &step, &index)) {
                if (step) tooltip_member(req, true, index, marker_mask(true, index));
                else tooltip_pair(req, index);
            }
        } else if (hit.domain == PickingDomain_Atom && hit.local_idx < atom_base.size() &&
                   (!markers.empty() || !event_context_bases.empty())) {
            const int32_t base = atom_base[hit.local_idx];
            const int32_t pair = base >= 0 ? base_pair[base] : -1;
            if (pair < 0) return;
            const bool context = std::binary_search(event_context_bases.begin(), event_context_bases.end(), (uint32_t)base);
            const bool involved = std::any_of(markers.begin(), markers.end(), [&](const Marker& marker) {
                return marker.step ? steps[marker.index].a == (uint32_t)pair || steps[marker.index].b == (uint32_t)pair
                    : marker.index == (uint32_t)pair;
            });
            if (context || involved) {
                tooltip_pair(req, (uint32_t)pair);
            }
        }
    }

    void prepare_display_geometry() {
        display_axis = {};
        display_bases.clear();
        display_pair_quality.clear();
        display_step_from.clear();
        display_step_to.clear();
        display_step_valid.clear();
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
        // Steps are drawn between pair midpoints, joined through the nearest periodic image.
        display_step_from.resize(steps.size());
        display_step_to.resize(steps.size());
        display_step_valid.assign(steps.size(), 0);
        for (size_t s = 0; s < steps.size(); ++s) {
            const size_t a = steps[s].a, b = steps[s].b;
            if (display_pair_quality[a] || display_pair_quality[b]) continue;
            const vec3_t from = current.pair_frames[a].origin;
            const vec3_t to = vec3_add(from, min_image(vec3_sub(current.pair_frames[b].origin, from), state.unitcell));
            display_step_from[s] = mat4_mul_vec3(app->operations.state_rotation, from, 1);
            display_step_to[s] = mat4_mul_vec3(app->operations.state_rotation, to, 1);
            display_step_valid[s] = 1;
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

    void draw_event_context() {
        if (event_context_bases.empty()) return;
        immediate::Scope scope(app->gfx.overlay, "Lavery active event context");
        const bool pickable = picking_range.domain == PickingDomain_Lavery &&
            picking_range.end - picking_range.beg == pairs.size() + steps.size();
        if (pickable) immediate::set_picking_base_idx(scope, picking_range.beg);
        for (uint32_t base : event_context_bases) {
            if (base >= display_bases.size()) continue;
            const auto& center = display_bases[base].center;
            ImVec4 color = event_context_colors[base];
            const uint32_t pick = pickable && base < base_pair.size() && base_pair[base] >= 0
                ? (uint32_t)base_pair[base] : 0xFFFFFFFFu;
            for (int layer = 2; layer >= 0; --layer) {
                color.w = (layer == 0 ? 18.0f : layer == 1 ? 10.0f : 5.0f) / 255.0f;
                immediate::sphere(scope, center, 3.4f + layer * 1.0f,
                    ImGui::ColorConvertFloat4ToU32(color), pick, 12, 8);
            }
        }
    }

    void draw_signed_overlay() {
        if (!signed_overlay || display_bases.empty()) return;
        draw_event_context();
        if (markers.empty()) return;
        immediate::Scope scope(app->gfx.overlay, "Lavery signed outliers");
        const bool pickable = picking_range.domain == PickingDomain_Lavery &&
            picking_range.end - picking_range.beg == pairs.size() + steps.size();
        if (pickable) immediate::set_picking_base_idx(scope, picking_range.beg);
        const int layers = overlay_halo ? 3 : 1;
        for (const auto& marker : markers) {
            const bool outlier = marker.contributors != 0;
            const bool positive = marker.z > 0;
            // Previewed members that are not outlying in this frame are drawn neutral.
            auto color = [&](int alpha) {
                if (marker.lost) return IM_COL32(255, 185, 55, alpha);
                if (!outlier) return IM_COL32(235, 235, 235, alpha * 2 / 3);
                return positive ? IM_COL32(255, 65, 95, alpha) : IM_COL32(40, 215, 255, alpha);
            };
            const uint32_t pick = pickable ? (marker.step ? (uint32_t)pairs.size() + marker.index : marker.index) : 0xFFFFFFFFu;
            // Layered, unlit transparency is a visual halo, not HDR emission.
            if (!marker.step) {
                if (marker.index >= display_pair_quality.size() || (!marker.lost && display_pair_quality[marker.index])) continue;
                for (uint32_t b : {pairs[marker.index].a, pairs[marker.index].b}) {
                    const auto& base = display_bases[b];
                    for (int layer = layers - 1; layer >= 0; --layer) {
                        const float radius = 2.0f + layer * 0.22f;
                        vec3_t previous = vec3_add(base.center, vec3_mul1(base.x, radius));
                        for (int s = 1; s <= 24; ++s) {
                            const float angle = (float)s * (6.28318530718f / 24);
                            const vec3_t point = vec3_add(base.center, vec3_mul1(vec3_add(
                                vec3_mul1(base.x, std::cos(angle)), vec3_mul1(base.y, std::sin(angle))), radius));
                            immediate::cylinder(scope, previous, point, 0.08f + layer * 0.12f,
                                color(layer ? 25 : 220), pick, 6);
                            previous = point;
                        }
                    }
                }
            } else {
                // Inter-base-pair parameters relate two pairs: a bar between their midpoints.
                if (marker.index >= display_step_valid.size() || !display_step_valid[marker.index]) continue;
                const vec3_t from = display_step_from[marker.index];
                const vec3_t to = display_step_to[marker.index];
                if (vec3_length(vec3_sub(to, from)) < 1.0e-4f) continue;
                for (int layer = layers - 1; layer >= 0; --layer) {
                    immediate::cylinder(scope, from, to, 0.3f + layer * 0.18f, color(layer ? 25 : 220), pick, 12);
                }
                immediate::sphere(scope, from, 0.55f, color(220), pick, 8, 12);
                immediate::sphere(scope, to, 0.55f, color(220), pick, 8, 12);
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
