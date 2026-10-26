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
#include <md_script.h>

#include <viamd_event.h>
#include <viamd.h>
#include <event.h>
#include <task_system.h>
#include <timeline_periods_ui.h>
#include <serialization_utils.h>
#include <gfx/immediate_draw_utils.h>
#include <gfx/scene_label.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>
#include <implot_internal.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <climits>
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
    return metric_uses_degrees(metric) ? "deg" : "Å";
}

static vec3_t min_image(vec3_t delta, const md_unitcell_t& cell) {
    md_util_min_image_vec3(&delta, 1, &cell);
    return delta;
}

constexpr PickingDomainID PickingDomain_Lavery = HASH_STR_LIT64("picking domain lavery outlier");
constexpr uint32_t kEventAllParameters = (1u << Metric_Count) - 1;

// A member preview requested by hovering a UI row; mask selects the class parameters assessed.
struct Target {
    uint8_t cls = Class_Pair;
    uint32_t index = 0;
    uint32_t mask = 1023;
};

// A member drawn in the viewport this frame.
struct Marker {
    uint8_t cls = Class_Pair;
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
    bool measurement_text = false;
    float contour_thickness = 1.5f;
    enum EventVisualStyle { EventVisual_Contour, EventVisual_Glow, EventVisual_Both, EventVisual_Off, EventVisual_Count };
    int event_visual_style = EventVisual_Glow;
    float glow_strength = 0.5f;
    bool reset_requested = false;
    uint64_t timeline_fit_key = 0;      // what the inspector timeline shows; refit on change
    uint64_t distribution_fit_key = 0;  // what the distribution plot shows; refit on change
    uint64_t event_distribution_key = 0;
    EventDistribution event_distribution;
    std::vector<Target> preview;
    std::vector<Marker> markers;
    bool markers_explicit = false;
    int baseline = Baseline_MeanSD;
    int reference_scope = Reference_OwnHistory;
    int statistics_reference_scope = Reference_OwnHistory;
    std::array<uint64_t, 4> reference_distribution_keys = {};
    std::array<DistributionComparison, 4> reference_distributions;
    int statistics_baseline = Baseline_MeanSD;
    int calculation_baseline = Baseline_MeanSD;
    bool baseline_range_enabled = false;
    bool baseline_range_period = false;
    uint32_t baseline_period_id = 0;
    float baseline_range_begin_ps = 0, baseline_range_end_ps = 0;
    int baseline_range_first = 0, baseline_range_last = 0;
    uint64_t statistics_key = 0;
    size_t statistics_first_frame = 0, statistics_end_frame = 0;
    std::vector<uint8_t> pair_has_baseline;
    std::string baseline_range_error;
    // Parameters used for event detection, independent of the viewed class: bit k is Metric k
    // (bits 0-5 intra-base pair, 6-13 inter-base pair incl. grooves, 14-23 nucleotide).
    uint32_t event_parameters = kEventAllParameters;
    int event_min_frames = 3;
    float event_min_time_ps = -1, event_gap_time_ps = -1;
    bool event_range_enabled = false;
    bool event_range_period = false;
    uint32_t event_period_id = 0;
    size_t detection_first_frame = 0, detection_last_frame = 0;
    float event_range_begin_ps = 0, event_range_end_ps = 0;
    int event_range_first = 0, event_range_last = 0;
    bool event_time_ns = false;
    std::string event_detection_error;
    size_t inspected_event = SIZE_MAX;
    enum InspectionScope { Inspection_Selected, Inspection_Parent, Inspection_All };
    int inspection_scope = Inspection_Selected;
    double inspection_frame = -1;
    int event_max_gap = 0;
    int event_neighbourhood = 2;
    bool event_whole_structure = false;
    float event_hysteresis = 0.5f;
    bool event_pair_loss = true;
    bool event_hierarchy = true;
    float event_burst_extra = 1.0f;
    int event_burst_mode = 0; // 0: stronger than the parent threshold, 1: stands out from the parent's background
    float event_residual_sigma = 2.0f;
    bool events_active_only = false;
    float events_max_shown_time_ps = 0;
    int events_max_shown_frames = 0;
    bool events_follow_playback = true;
    int event_follow_target = -1;
    char event_filter[64] = {};
    int event_nav_target = -1; // event scrolled into view after keyboard navigation
    // Persistent layout: pane fractions of the window and plot heights in pixels.
    float layout_sidebar = 0.24f, layout_events = 0.34f, layout_overview = 0.52f;
    float count_plot_height = 180, timeline_plot_height = 220, distribution_plot_height = 180, outliers_list_height = 160;
    std::vector<int> event_order;      // table order of outlier_events
    std::vector<uint8_t> event_expanded;
    uint64_t footprint_key = 0;
    size_t footprint_event = SIZE_MAX, footprint_columns = 0;
    std::vector<uint8_t> footprint_cells;
    bool event_order_dirty = true;
    std::vector<uint32_t> timeline_period_ids; // timeline periods made from events
    std::vector<float> expected_counts;        // chance outliers per frame for the count plot
    std::vector<float> count_plot_values;
    PlotSummary count_summary, expected_summary;
    double total_observed = 0, total_expected = 0;
    int summary_metric = -1;
    std::vector<PlotSummary> timeline_summaries;
    uint64_t heatmap_summary_key = 0;
    std::vector<PlotSummary> heatmap_summaries;
    std::vector<float> heatmap_display;
    size_t heatmap_first = 0, heatmap_end = 0, heatmap_bin_width = 0;
    uint64_t counts_key = 0;
    uint64_t event_data_key = 0;
    bool counts_pending = false; // settings changed during an active edit; recomputed on release
    std::vector<uint32_t> event_context_bases;
    std::vector<uint8_t> event_context_groups;
    std::vector<uint8_t> contour_atom_groups;
    std::vector<size_t> event_colors;
    std::vector<std::string> event_kinds, event_search; // per event, cached after detection; search text is lowercase
    PickingRange picking_range = {};
    std::vector<int32_t> atom_base;
    std::vector<int32_t> base_pair;
    std::vector<vec3_t> display_step_from, display_step_to;
    std::vector<uint8_t> display_step_valid;
    std::vector<vec3_t> display_sugar, display_phosphate; // per nucleotide member; non-finite when missing
    struct GrooveSpan {
        std::array<int32_t, 2> atoms;
        std::array<vec3_t, 2> points;
    };
    std::array<std::vector<std::vector<GrooveSpan>>, 2> display_grooves;
    std::vector<BasePairFrame> display_pair_frames, display_step_frames;
    std::vector<vec3_t> display_coordinates;
    md_unitcell_t display_cell = {};
    Target detail_target;
    int detail_metric = -1;
    bool selected_detail_active = true;
    int hovered_metric = -1, hovered_frame = -1;
    Target hovered_target;
    float detail_value = 0;
    float detail_companion = 0;
    struct MeasurementLabel {
        vec3_t position;
        std::string text;
        ImVec2 offset;
    };
    std::vector<MeasurementLabel> measurement_labels;
    std::array<std::vector<int>, Class_Count> comparison_members;
    std::array<DistributionComparison, Metric_Count> distributions;
    std::vector<uint8_t> pair_contacts;
    std::vector<PairingState> pairing_states;
    std::vector<PairingRun> pairing_history;
    std::vector<PairingTransition> pairing_changes;
    size_t pairing_cached_member = SIZE_MAX;
    uint64_t pairing_changes_key = 0;
    float pairing_min_time_ps = 200;
    int pairing_min_frames = 3;
    int state_history_kind = 0;
    int state_history_cached_kind = -1;
    std::vector<StateSummary> state_history_summaries;
    std::vector<uint8_t> pair_uses_hydrogens;
    std::unique_ptr<PairingValidator> display_validator;
    std::vector<BaseFrame> display_bases;
    std::vector<uint16_t> display_pair_quality;
    AxisPath display_axis;
    bool aggregate_outliers = false;
    uint32_t selected_parameters = 1023; // class-local bits, masked by the viewed class
    std::vector<uint16_t> pair_quality_flags;
    std::vector<uint16_t> step_quality_flags;
    std::array<std::vector<uint16_t>, 10> nucleotide_quality_flags;
    std::vector<OutlierEvent> outlier_events;
    std::vector<StateSwitch> state_switches;
    size_t inspected_switch = SIZE_MAX;
    float state_min_time_ps = -1;
    int state_min_frames = 3;
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
    std::vector<std::string> nucleotide_labels;

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
        nucleotide_labels.clear();
        reset_heatmap_view = true;
        focus_heatmap_selection = false;
        heatmap_limits = {};
        heatmap_frame = -1.0;
        heatmap_pressed = heatmap_dragged = false;
        heatmap_member_count = 0;
        outlier_counts.clear();
        count_plot_values.clear();
        count_summary = {};
        expected_summary = {};
        total_observed = total_expected = 0;
        summary_metric = -1;
        timeline_summaries.clear();
        heatmap_summary_key = 0;
        heatmap_summaries.clear();
        heatmap_display.clear();
        heatmap_first = heatmap_end = heatmap_bin_width = 0;
        pair_quality_flags.clear();
        pair_has_baseline.clear();
        pair_contacts.clear();
        pairing_states.clear();
        pairing_history.clear();
        pairing_changes.clear();
        pairing_cached_member = SIZE_MAX;
        pairing_changes_key = 0;
        state_history_cached_kind = -1;
        state_history_summaries.clear();
        pair_uses_hydrogens.clear();
        display_validator.reset();
        display_bases.clear();
        display_pair_quality.clear();
        display_axis = {};
        for (auto& d : distributions) d = {};
        event_distribution_key = 0;
        event_distribution = {};
        for (auto& c : comparison_members) c.clear();
        preview.clear();
        markers.clear();
        markers_explicit = false;
        detail_metric = -1;
        selected_detail_active = true;
        hovered_metric = hovered_frame = -1;
        measurement_labels.clear();
        display_coordinates.clear();
        display_pair_frames.clear();
        display_step_frames.clear();
        for (auto& grooves : display_grooves) grooves.clear();
        step_quality_flags.clear();
        for (auto& flags : nucleotide_quality_flags) flags.clear();
        outlier_events.clear();
        event_expanded.clear();
        footprint_key = 0;
        footprint_event = SIZE_MAX;
        footprint_columns = 0;
        footprint_cells.clear();
        state_switches.clear();
        inspected_switch = SIZE_MAX;
        event_detection_error.clear();
        inspected_event = SIZE_MAX;
        event_follow_target = -1;
        counts_key = 0;
        statistics_key = 0;
        reference_distribution_keys = {};
        for (auto& reference : reference_distributions) reference = {};
        baseline_range_error.clear();
        event_data_key = 0;
        event_context_bases.clear();
        event_context_groups.clear();
        contour_atom_groups.clear();
        event_colors.clear();
        event_kinds.clear();
        event_search.clear();
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
                refresh_event_inspection();
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
            case viamd::EventType_ViamdDrawSceneLabels:
                if (overlay_active() && signed_overlay && measurement_text) draw_measurement_labels();
                break;
            case viamd::EventType_ViamdRepresentationContours:
                if (overlay_active() && signed_overlay && !event_context_bases.empty() &&
                    (event_visual_style == EventVisual_Contour || event_visual_style == EventVisual_Both))
                    representation_contours((RepresentationContours*)e.payload);
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
                        const uint32_t fields[5] = {marker.cls, marker.index, marker.mask, marker.contributors, marker.lost ? 1u : 0u};
                        key->key = md_hash64(fields, sizeof(fields), key->key);
                        key->key = md_hash64(&marker.z, sizeof(marker.z), key->key);
                    }
                    key->key = md_hash64(&metric, sizeof(metric), key->key);
                    const int detail[] = {detail_metric, detail_target.cls, (int)detail_target.index};
                    key->key = md_hash64(detail, sizeof(detail), key->key);
                    key->key = md_hash64(&selected_parameters, sizeof(selected_parameters), key->key);
                    key->key = md_hash64(&outlier_sigma, sizeof(outlier_sigma), key->key);
                    key->key = md_hash64(&counts_key, sizeof(counts_key), key->key);
                    key->key = md_hash64(&inspected_event, sizeof(inspected_event), key->key);
                    key->key = md_hash64(&inspection_scope, sizeof(inspection_scope), key->key);
                    key->key = md_hash64(&event_visual_style, sizeof(event_visual_style), key->key);
                    key->key = md_hash64(&glow_strength, sizeof(glow_strength), key->key);
                    if (event_visual_style == EventVisual_Glow || event_visual_style == EventVisual_Both) {
                        key->key = md_hash64(event_context_bases.data(), event_context_bases.size()*sizeof(uint32_t), key->key);
                        key->key = md_hash64(event_context_groups.data(), event_context_groups.size(), key->key);
                    }
                    key->key = md_hash64(&picking_range, sizeof(picking_range), key->key);
                }
                break;
            }
            case viamd::EventType_ViamdPickingRangeReserve: {
                picking_range = {};
                if (overlay_active() && signed_overlay) {
                    picking_range_reserve(&picking_range, (PickingSpace*)e.payload, PickingDomain_Lavery, picking_count());
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
                    event->kind == InteractionSurfaceEventKind::Hover) {
                    hovered_metric = -1;
                    hovered_frame = ImGui::GetFrameCount();
                    int cls = Class_Pair, parameter = -1;
                    uint32_t index = 0;
                    if (event->hit.domain == PickingDomain_Lavery && decode_pick(event->hit.local_idx, &cls, &index, &parameter)) {
                        if (parameter < 0) {
                            const auto result = assess_member(current_frame(), cls, index, marker_mask(cls, index));
                            if (result.peak_metric >= 0) parameter = class_first_metric(cls) + result.peak_metric;
                        }
                        if (parameter >= 0) {
                            hovered_metric = parameter;
                            hovered_target = {(uint8_t)cls, index, 1u << (parameter - class_first_metric(cls))};
                        }
                    }
                }
                if (event && overlay_active() && event->surface_id == interaction_surface_main &&
                    event->kind == InteractionSurfaceEventKind::Click &&
                    event->selection_mode == InteractionSelectionMode::None) {
                    int cls = Class_Pair;
                    int parameter = -1;
                    uint32_t index = 0;
                    bool target = event->hit.domain == PickingDomain_Lavery &&
                        decode_pick(event->hit.local_idx, &cls, &index, &parameter);
                    if (!target && signed_overlay) {
                        const int32_t base = picked_base(event->hit);
                        if (base >= 0 && base_pair[base] >= 0 &&
                            std::binary_search(event_context_bases.begin(), event_context_bases.end(), (uint32_t)base)) {
                            index = (uint32_t)base_pair[base];
                            if (inspected_switch < state_switches.size()) {
                                cls = Class_Nucleotide;
                                index = state_switches[inspected_switch].member;
                                parameter = state_switches[inspected_switch].metric;
                            }
                            target = true;
                        }
                    }
                    if (target) {
                        const size_t frame = current_frame();
                        const auto result = assess_member(frame, cls, index, marker_mask(cls, index));
                        select_target(cls, index, parameter >= 0 ? parameter - class_first_metric(cls) : result.peak_metric, frame);
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
                viamd::write_bool(state, STR_LIT("MeasurementText"), measurement_text);
                viamd::write_flt(state, STR_LIT("ContourThickness"), contour_thickness);
                viamd::write_int(state, STR_LIT("EventVisualStyle"), event_visual_style);
                viamd::write_flt(state, STR_LIT("GlowStrength"), glow_strength);
                viamd::write_flt(state, STR_LIT("StateMinTimePs"), state_min_time_ps);
                viamd::write_int(state, STR_LIT("StateMinFrames"), state_min_frames);
                viamd::write_flt(state, STR_LIT("PairingMinTimePs"), pairing_min_time_ps);
                viamd::write_int(state, STR_LIT("PairingMinFrames"), pairing_min_frames);
                viamd::write_int(state, STR_LIT("StateHistoryKind"), state_history_kind);
                viamd::write_bool(state, STR_LIT("AggregateOutliers"), aggregate_outliers);
                viamd::write_int(state, STR_LIT("SelectedParameters"), (int)selected_parameters);
                viamd::write_int(state, STR_LIT("Baseline"), baseline);
                viamd::write_int(state, STR_LIT("ReferenceScope"), reference_scope);
                viamd::write_bool(state, STR_LIT("BaselineRangeEnabled"), baseline_range_enabled);
                viamd::write_bool(state, STR_LIT("BaselineRangePeriod"), baseline_range_period);
                viamd::write_int(state, STR_LIT("BaselinePeriodId"), baseline_period_id);
                viamd::write_flt(state, STR_LIT("BaselineRangeBeginPs"), baseline_range_begin_ps);
                viamd::write_flt(state, STR_LIT("BaselineRangeEndPs"), baseline_range_end_ps);
                viamd::write_int(state, STR_LIT("BaselineRangeFirst"), baseline_range_first);
                viamd::write_int(state, STR_LIT("BaselineRangeLast"), baseline_range_last);
                viamd::write_int(state, STR_LIT("EventParameters"), (int)event_parameters);
                viamd::write_int(state, STR_LIT("EventMinFrames"), event_min_frames);
                viamd::write_flt(state, STR_LIT("EventMinTimePs"), event_min_time_ps);
                viamd::write_flt(state, STR_LIT("EventGapTimePs"), event_gap_time_ps);
                viamd::write_bool(state, STR_LIT("EventRangeEnabled"), event_range_enabled);
                viamd::write_bool(state, STR_LIT("EventRangePeriod"), event_range_period);
                viamd::write_int(state, STR_LIT("EventPeriodId"), event_period_id);
                viamd::write_flt(state, STR_LIT("EventRangeBeginPs"), event_range_begin_ps);
                viamd::write_flt(state, STR_LIT("EventRangeEndPs"), event_range_end_ps);
                viamd::write_int(state, STR_LIT("EventRangeFirst"), event_range_first);
                viamd::write_int(state, STR_LIT("EventRangeLast"), event_range_last);
                viamd::write_bool(state, STR_LIT("EventTimeNs"), event_time_ns);
                viamd::write_int(state, STR_LIT("EventMaxGap"), event_max_gap);
                viamd::write_int(state, STR_LIT("EventNeighbourhood"), event_neighbourhood);
                viamd::write_bool(state, STR_LIT("EventWholeStructure"), event_whole_structure);
                viamd::write_flt(state, STR_LIT("EventHysteresis"), event_hysteresis);
                viamd::write_bool(state, STR_LIT("EventPairLoss"), event_pair_loss);
                viamd::write_bool(state, STR_LIT("EventHierarchy"), event_hierarchy);
                viamd::write_flt(state, STR_LIT("EventBurstExtra"), event_burst_extra);
                viamd::write_int(state, STR_LIT("EventBurstMode"), event_burst_mode);
                viamd::write_flt(state, STR_LIT("EventResidualSigma"), event_residual_sigma);
                viamd::write_bool(state, STR_LIT("EventsActiveOnly"), events_active_only);
                viamd::write_flt(state, STR_LIT("EventsMaxShownTimePs"), events_max_shown_time_ps);
                viamd::write_int(state, STR_LIT("EventsMaxShownFrames"), events_max_shown_frames);
                viamd::write_bool(state, STR_LIT("EventsFollowPlayback"), events_follow_playback);
                viamd::write_flt(state, STR_LIT("LayoutSidebar"), layout_sidebar);
                viamd::write_flt(state, STR_LIT("LayoutEvents"), layout_events);
                viamd::write_flt(state, STR_LIT("LayoutOverview"), layout_overview);
                viamd::write_flt(state, STR_LIT("LayoutCountPlot"), count_plot_height);
                viamd::write_flt(state, STR_LIT("LayoutTimelinePlot"), timeline_plot_height);
                viamd::write_flt(state, STR_LIT("LayoutDistributionPlot"), distribution_plot_height);
                viamd::write_flt(state, STR_LIT("LayoutOutlierList"), outliers_list_height);
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
                measurement_text = false;
                contour_thickness = 1.5f;
                event_visual_style = EventVisual_Glow;
                glow_strength = 0.5f;
                state_min_time_ps = -1;
                state_min_frames = 3;
                pairing_min_time_ps = 200;
                pairing_min_frames = 3;
                state_history_kind = 0;
                aggregate_outliers = false;
                selected_parameters = 1023;
                baseline = Baseline_MeanSD;
                reference_scope = Reference_OwnHistory;
                baseline_range_enabled = false;
                baseline_range_period = false;
                baseline_period_id = 0;
                baseline_range_begin_ps = baseline_range_end_ps = 0;
                baseline_range_first = baseline_range_last = 0;
                event_parameters = kEventAllParameters;
                event_min_frames = 3;
                event_min_time_ps = event_gap_time_ps = -1;
                event_range_enabled = false;
                event_range_period = false;
                event_period_id = 0;
                event_range_begin_ps = event_range_end_ps = 0;
                event_range_first = event_range_last = 0;
                event_time_ns = false;
                inspected_event = SIZE_MAX;
                event_max_gap = 0;
                event_neighbourhood = 2;
                event_whole_structure = false;
                event_hysteresis = 0.5f;
                event_pair_loss = true;
                event_hierarchy = true;
                event_burst_extra = 1.0f;
                event_burst_mode = 0;
                event_residual_sigma = 2.0f;
                events_active_only = false;
                events_max_shown_time_ps = 0;
                events_max_shown_frames = 0;
                events_follow_playback = true;
                event_follow_target = -1;
                reset_layout();
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
                    else if (str_eq(key, STR_LIT("MeasurementText"))) viamd::extract_bool(measurement_text, arg);
                    else if (str_eq(key, STR_LIT("ContourThickness"))) {
                        float thickness = contour_thickness;
                        if (!viamd::extract_flt(thickness, arg) || !std::isfinite(thickness) || thickness < 1.0f || thickness > 4.0f)
                            MD_LOG_ERROR("Lavery: contour thickness must be finite and between 1 and 4");
                        else contour_thickness = thickness;
                    }
                    else if (str_eq(key, STR_LIT("EventVisualStyle"))) {
                        if (!viamd::extract_enum(event_visual_style, arg, EventVisual_Count))
                            MD_LOG_ERROR("Lavery: event visual style must be Contour (0), Glow (1), Both (2) or Off (3)");
                    }
                    else if (str_eq(key, STR_LIT("GlowStrength"))) {
                        float strength = glow_strength;
                        if (!viamd::extract_flt(strength, arg) || !std::isfinite(strength) || strength < 0 || strength > 1)
                            MD_LOG_ERROR("Lavery: glow strength must be finite and between 0 and 1");
                        else glow_strength = strength;
                    }
                    else if (str_eq(key, STR_LIT("StateMinTimePs"))) {
                        viamd::extract_flt(state_min_time_ps, arg);
                        if (!std::isfinite(state_min_time_ps) || state_min_time_ps < 0) state_min_time_ps = -1;
                    }
                    else if (str_eq(key, STR_LIT("StateHistoryKind"))) {
                        if (!viamd::extract_int(state_history_kind, arg) || state_history_kind < 0 || state_history_kind > 5) {
                            MD_LOG_ERROR("Lavery: invalid state history selection");
                            state_history_kind = 0;
                        }
                    }
                    else if (str_eq(key, STR_LIT("PairingMinTimePs"))) {
                        if (!viamd::extract_flt(pairing_min_time_ps, arg) || !std::isfinite(pairing_min_time_ps) || pairing_min_time_ps < 0) {
                            MD_LOG_ERROR("Lavery: PairingMinTimePs must be finite and nonnegative");
                            pairing_min_time_ps = 200;
                        }
                    }
                    else if (str_eq(key, STR_LIT("PairingMinFrames"))) {
                        if (!viamd::extract_int(pairing_min_frames, arg) || pairing_min_frames < 1) {
                            MD_LOG_ERROR("Lavery: PairingMinFrames must be positive");
                            pairing_min_frames = 3;
                        }
                    }
                    else if (str_eq(key, STR_LIT("StateMinFrames"))) {
                        viamd::extract_int(state_min_frames, arg);
                        state_min_frames = std::clamp(state_min_frames, 1, 100000);
                    }
                    else if (str_eq(key, STR_LIT("AggregateOutliers"))) viamd::extract_bool(aggregate_outliers, arg);
                    else if (str_eq(key, STR_LIT("SelectedParameters"))) {
                        int mask = 1023;
                        viamd::extract_int(mask, arg);
                        selected_parameters = (uint32_t)mask & 1023;
                    }
                    else if (str_eq(key, STR_LIT("OutlierSigma"))) viamd::extract_flt(outlier_sigma, arg);
                    else if (str_eq(key, STR_LIT("Baseline"))) viamd::extract_enum(baseline, arg, Baseline_Count);
                    else if (str_eq(key, STR_LIT("ReferenceScope"))) viamd::extract_enum(reference_scope, arg, Reference_Count);
                    else if (str_eq(key, STR_LIT("BaselineRangeEnabled"))) viamd::extract_bool(baseline_range_enabled, arg);
                    else if (str_eq(key, STR_LIT("BaselineRangePeriod"))) viamd::extract_bool(baseline_range_period, arg);
                    else if (str_eq(key, STR_LIT("BaselinePeriodId"))) extract_period_id(baseline_period_id, arg);
                    else if (str_eq(key, STR_LIT("BaselineRangeBeginPs"))) viamd::extract_flt(baseline_range_begin_ps, arg);
                    else if (str_eq(key, STR_LIT("BaselineRangeEndPs"))) viamd::extract_flt(baseline_range_end_ps, arg);
                    else if (str_eq(key, STR_LIT("BaselineRangeFirst"))) viamd::extract_int(baseline_range_first, arg);
                    else if (str_eq(key, STR_LIT("BaselineRangeLast"))) viamd::extract_int(baseline_range_last, arg);
                    else if (str_eq(key, STR_LIT("EventParameters"))) {
                        int mask = (int)kEventAllParameters;
                        viamd::extract_int(mask, arg);
                        // Workspaces from before grooves and nucleotides only stored bits 0-11: enable the new parameters.
                        if ((uint32_t)mask < (1u << MinorGroove)) mask |= (int)(kEventAllParameters & ~((1u << MinorGroove) - 1));
                        event_parameters = (uint32_t)mask & kEventAllParameters;
                    }
                    else if (str_eq(key, STR_LIT("EventMinFrames"))) {
                        viamd::extract_int(event_min_frames, arg);
                        event_min_frames = std::clamp(event_min_frames, 1, 100000);
                    }
                    else if (str_eq(key, STR_LIT("EventMinTimePs"))) viamd::extract_flt(event_min_time_ps, arg);
                    else if (str_eq(key, STR_LIT("EventGapTimePs"))) viamd::extract_flt(event_gap_time_ps, arg);
                    else if (str_eq(key, STR_LIT("EventRangeEnabled"))) viamd::extract_bool(event_range_enabled, arg);
                    else if (str_eq(key, STR_LIT("EventRangePeriod"))) viamd::extract_bool(event_range_period, arg);
                    else if (str_eq(key, STR_LIT("EventPeriodId"))) extract_period_id(event_period_id, arg);
                    else if (str_eq(key, STR_LIT("EventRangeBeginPs"))) viamd::extract_flt(event_range_begin_ps, arg);
                    else if (str_eq(key, STR_LIT("EventRangeEndPs"))) viamd::extract_flt(event_range_end_ps, arg);
                    else if (str_eq(key, STR_LIT("EventRangeFirst"))) viamd::extract_int(event_range_first, arg);
                    else if (str_eq(key, STR_LIT("EventRangeLast"))) viamd::extract_int(event_range_last, arg);
                    else if (str_eq(key, STR_LIT("EventTimeNs"))) viamd::extract_bool(event_time_ns, arg);
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
                    else if (str_eq(key, STR_LIT("EventBurstMode"))) {
                        viamd::extract_int(event_burst_mode, arg);
                        if (event_burst_mode < 0 || event_burst_mode > 1) {
                            MD_LOG_ERROR("Lavery: EventBurstMode must be 0 (stronger) or 1 (background)");
                            event_burst_mode = 0;
                        }
                    }
                    else if (str_eq(key, STR_LIT("EventResidualSigma"))) {
                        viamd::extract_flt(event_residual_sigma, arg);
                        if (!std::isfinite(event_residual_sigma) || event_residual_sigma < 0.5f || event_residual_sigma > 6.0f) {
                            MD_LOG_ERROR("Lavery: EventResidualSigma must be between 0.5 and 6 sigma");
                            event_residual_sigma = 2.0f;
                        }
                    }
                    else if (str_eq(key, STR_LIT("Metric"))) viamd::extract_enum(metric, arg, Metric_Count);
                    else if (str_eq(key, STR_LIT("Member"))) viamd::extract_int(member, arg);
                    else if (str_eq(key, STR_LIT("EventsActiveOnly"))) viamd::extract_bool(events_active_only, arg);
                    else if (str_eq(key, STR_LIT("EventsMaxShownTimePs"))) {
                        viamd::extract_flt(events_max_shown_time_ps, arg);
                        if (!std::isfinite(events_max_shown_time_ps) || events_max_shown_time_ps < 0) {
                            MD_LOG_ERROR("Lavery: EventsMaxShownTimePs must be finite and nonnegative; using Unlimited");
                            events_max_shown_time_ps = 0;
                        }
                    }
                    else if (str_eq(key, STR_LIT("EventsMaxShownFrames"))) {
                        viamd::extract_int(events_max_shown_frames, arg);
                        if (events_max_shown_frames < 0) {
                            MD_LOG_ERROR("Lavery: EventsMaxShownFrames must be nonnegative; using Unlimited");
                            events_max_shown_frames = 0;
                        }
                    }
                    else if (str_eq(key, STR_LIT("EventsFollowPlayback"))) viamd::extract_bool(events_follow_playback, arg);
                    else if (str_eq(key, STR_LIT("LayoutSidebar"))) viamd::extract_flt(layout_sidebar, arg);
                    else if (str_eq(key, STR_LIT("LayoutEvents"))) viamd::extract_flt(layout_events, arg);
                    else if (str_eq(key, STR_LIT("LayoutOverview"))) viamd::extract_flt(layout_overview, arg);
                    else if (str_eq(key, STR_LIT("LayoutCountPlot"))) viamd::extract_flt(count_plot_height, arg);
                    else if (str_eq(key, STR_LIT("LayoutTimelinePlot"))) viamd::extract_flt(timeline_plot_height, arg);
                    else if (str_eq(key, STR_LIT("LayoutDistributionPlot"))) viamd::extract_flt(distribution_plot_height, arg);
                    else if (str_eq(key, STR_LIT("LayoutOutlierList"))) viamd::extract_flt(outliers_list_height, arg);
                }
                sanitize_layout();
                break;
            }
            default:
                break;
            }
        }
    }

    void reset_layout() {
        layout_sidebar = 0.24f;
        layout_events = 0.34f;
        layout_overview = 0.52f;
        count_plot_height = 180;
        timeline_plot_height = 220;
        distribution_plot_height = 180;
        outliers_list_height = 160;
    }

    void sanitize_layout() {
        auto fraction = [](float& v, float lo, float hi, float fallback) {
            v = std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
        };
        auto pixels = [](float& v, float fallback) {
            v = std::isfinite(v) ? std::clamp(v, 60.0f, 2000.0f) : fallback;
        };
        fraction(layout_sidebar, 0.12f, 0.5f, 0.24f);
        fraction(layout_events, 0.1f, 0.8f, 0.34f);
        fraction(layout_overview, 0.2f, 0.8f, 0.52f);
        pixels(count_plot_height, 180);
        pixels(timeline_plot_height, 220);
        pixels(distribution_plot_height, 180);
        pixels(outliers_list_height, 160);
    }

    // Thin draggable divider. 'value' is in pixels; returns true while dragging.
    static bool splitter(const char* id, bool vertical, float* value, float lo, float hi) {
        const float thickness = std::max(4.0f, ImGui::GetStyle().ItemSpacing.y);
        const ImVec2 size = vertical ? ImVec2(thickness, std::max(1.0f, ImGui::GetContentRegionAvail().y))
                                     : ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), thickness);
        ImGui::InvisibleButton(id, size);
        const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
        if (hovered || active) ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
        if (active) {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            *value = std::clamp(*value + (vertical ? delta.x : delta.y), lo, std::max(lo, hi));
        }
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        const ImU32 color = ImGui::GetColorU32(active ? ImGuiCol_SeparatorActive : hovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator);
        if (vertical) ImGui::GetWindowDrawList()->AddLine(ImVec2(0.5f * (a.x + b.x), a.y), ImVec2(0.5f * (a.x + b.x), b.y), color, hovered || active ? 2.0f : 1.0f);
        else ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, 0.5f * (a.y + b.y)), ImVec2(b.x, 0.5f * (a.y + b.y)), color, hovered || active ? 2.0f : 1.0f);
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) *value = -1; // caller resets
        return active;
    }

    // Horizontal handle below a fixed-height element (plot or list); double-click restores 'fallback'.
    static void height_handle(const char* id, float* height, float fallback) {
        float h = *height;
        splitter(id, false, &h, 60.0f, 2000.0f);
        *height = h < 0 ? fallback : h;
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
            const auto range = md_component_atom_range(&sys->component, bases[b].component);
            for (uint32_t atom = range.beg; atom < range.end; ++atom) atom_base[atom] = (int32_t)b;
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
        for (size_t p = 0; p < pairs.size(); ++p) {
            for (uint32_t base : {pairs[p].a, pairs[p].b}) {
                nucleotide_labels.push_back(base_label(base) + " (bp " + std::to_string(p + 1) + ")");
            }
        }
        const size_t bp_count = pairs.size();
        const size_t step_count = steps.size();
        pair_quality_flags.assign(frame_count * bp_count, Quality_InvalidBase);
        pair_contacts.assign(frame_count * bp_count, 0);
        pairing_states.assign(frame_count * bp_count, Pairing_Unavailable);
        step_quality_flags.assign(frame_count * step_count, Quality_AdjacentPair);
        for (auto& flags : nucleotide_quality_flags) flags.assign(frame_count * 2 * bp_count, Quality_MissingAtoms);
        columns.fill(0);
        for (int m = 0; m < (int)Metric_Count; ++m) {
            columns[m] = class_members(metric_class((Metric)m));
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
                std::copy(result.pairing_states.begin(), result.pairing_states.end(), pairing_states.begin() + frame * pairs.size());
                for (int m = 0; m < (int)Metric_Count; ++m) {
                    const int cls = metric_class((Metric)m);
                    const auto& samples = cls == Class_Step ? result.step_values :
                        cls == Class_Nucleotide ? result.nucleotide_values : result.pair_values;
                    for (size_t i = 0; i < samples.size(); ++i) {
                        values[m][frame * columns[m] + i] = samples[i].metric[m];
                        if (cls == Class_Nucleotide) nucleotide_quality_flags[m-Alpha][frame * columns[m] + i] =
                            result.nucleotide_quality[i][m-Alpha];
                    }
                }
                frames_done = frame + 1;
            }
            md_system_extract_end(ex);
            md_system_state_free(&state);
            md_temp_end(temp_scope);
            if (ok && !analysis_cancelled()) {
                for (int m = 0; m < (int)Metric_Count; ++m) {
                    if (analysis_cancelled()) { ok = false; break; }
                    if (!compute_statistics(&statistics[m], values[m], columns[m], metric_is_angular((Metric)m), method,
                            metric_state_count((Metric)m) ? m : -1)) {
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
        statistics_reference_scope = Reference_OwnHistory;
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
                metric_uses_degrees(metric_id) ? md_unit_degree() : md_unit_angstrom())) {
                md_attributes_remove_prefix(table, prefix);
                return false;
            }
            if (metric_class(metric_id) == Class_Nucleotide) {
                const std::string path = std::string(metric_path(metric_id)) + "_quality";
                const std::string label = std::string(metric_name(metric_id)) + " quality flags";
                const auto& flags = nucleotide_quality_flags[m-Alpha];
                if (!publish_series(table, run, path.c_str(), label.c_str(), MD_ATTRIBUTE_TYPE_U16,
                    run_num_frames(app), n, flags.data(), flags.size()*sizeof(uint16_t), md_unit_none())) {
                    md_attributes_remove_prefix(table, prefix);
                    return false;
                }
            }
        }
        const size_t frames = run_num_frames(app);
        if (!publish_series(table, run, "bp/quality", "Pair quality flags", MD_ATTRIBUTE_TYPE_U16,
                frames, pairs.size(), pair_quality_flags.data(), pair_quality_flags.size() * sizeof(uint16_t), md_unit_none()) ||
            !publish_series(table, run, "bp/contact_count", "Canonical contacts", MD_ATTRIBUTE_TYPE_U8,
                frames, pairs.size(), pair_contacts.data(), pair_contacts.size(), md_unit_none()) ||
            !publish_series(table, run, "bp/pairing_state", "Pairing state: 0 unavailable, 1 WC, 2 AT/U Hoogsteen, 3 other, 4 unpaired", MD_ATTRIBUTE_TYPE_U8,
                frames, pairs.size(), pairing_states.data(), pairing_states.size(), md_unit_none()) ||
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
        sanitize_layout();
        const float handle = std::max(4.0f, ImGui::GetStyle().ItemSpacing.y);
        float sidebar_width = std::max(ImGui::GetFontSize() * 10, size.x * layout_sidebar);
        if (ImGui::BeginChild("Settings", ImVec2(sidebar_width, 0))) draw_shared_controls();
        ImGui::EndChild();
        ImGui::SameLine(0, 0);
        splitter("##split_sidebar", true, &sidebar_width, ImGui::GetFontSize() * 10, size.x * 0.5f);
        layout_sidebar = sidebar_width < 0 ? 0.24f : sidebar_width / std::max(1.0f, size.x);
        ImGui::SameLine(0, 0);
        if (reset_requested) {
            reset_requested = false;
            reset();
            ImGui::End();
            return;
        }
        ImGui::BeginGroup();
        update_statistics(frame_count);
        if (!baseline_range_error.empty()) ImGui::TextWrapped("%s", baseline_range_error.c_str());
        int count = (int)columns[metric];
        bool ready = count > 0 && frame_count;
        if (ready) member = std::clamp(member, 0, count - 1);
        auto unavailable = [&]() {
            ImGui::TextDisabled("No inter-base-pair links found. Select Intra-base pair.");
        };
        const ImPlotInputMap input = ImPlot::GetInputMap();
        if (ImGui::GetIO().KeyCtrl) ImPlot::GetInputMap().Pan = ImGuiMouseButton_Middle;
        if (frame_count) update_counts(count, frame_count);
        const ImVec2 right = ImGui::GetContentRegionAvail();
        float events_height = std::max(ImGui::GetFrameHeight() * 4, right.y * layout_events);
        if (ImGui::BeginChild("Events & quality", ImVec2(0, events_height))) {
            if (frame_count) draw_events_quality(count, frame_count); else unavailable();
        }
        ImGui::EndChild();
        splitter("##split_events", false, &events_height, ImGui::GetFrameHeight() * 4, right.y - ImGui::GetFrameHeight() * 6);
        layout_events = events_height < 0 ? 0.34f : events_height / std::max(1.0f, right.y);
        // Clicking an event member can switch between intra- and inter-base pairs above.
        count = (int)columns[metric];
        ready = count > 0 && frame_count;
        if (ready) member = std::clamp(member, 0, count - 1);
        const float bottom = ImGui::GetContentRegionAvail().x;
        float overview_width = std::max(1.0f, (bottom - handle) * layout_overview);
        if (ImGui::BeginChild("Overview", ImVec2(overview_width, 0))) {
            if (ready) {
                draw_outlier_timeline(count, frame_count);
                if (show_heatmap) draw_heatmap(count, frame_count, statistics[metric].heatmap, &member);
            } else unavailable();
        }
        ImGui::EndChild();
        ImGui::SameLine(0, 0);
        splitter("##split_overview", true, &overview_width, bottom * 0.2f, bottom * 0.8f);
        layout_overview = overview_width < 0 ? 0.52f : overview_width / std::max(1.0f, bottom - handle);
        ImGui::SameLine(0, 0);
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
        if (sync_viewport || markers_explicit || inspected_event != SIZE_MAX || inspected_switch != SIZE_MAX) update_outlier_highlight();
        else clear_highlight();
    }

    void draw_shared_controls() {
        ImGui::PushItemWidth(-ImGui::GetFontSize() * 7.5f);
        ImGui::SeparatorText("Selection");
        int group = metric_cls();
        if (ImGui::Combo("Class", &group, "Intra-base pair\0Inter-base pair\0Nucleotide\0")) {
            selected_detail_active = true;
            const int previous = metric_cls();
            const int local = metric - class_first_metric(previous);
            metric = class_first_metric(group) + std::min(local, class_parameter_count(group) - 1);
            // Keep the same helix position: pair p <-> step from p <-> nucleotide 2p.
            const size_t pair = previous == Class_Step && (size_t)member < steps.size() ? steps[member].a :
                previous == Class_Nucleotide ? (size_t)member / 2 : (size_t)member;
            if (group == Class_Nucleotide) member = (int)(2 * pair);
            else if (group == Class_Step) {
                for (size_t st = 0; st < steps.size(); ++st) if (steps[st].a == pair) { member = (int)st; break; }
            } else member = (int)pair;
            focus_heatmap_selection = true;
            reset_heatmap_view = true;
        }
        const int offset = class_first_metric(group);
        int local_metric = std::clamp(metric - offset, 0, class_parameter_count(group) - 1);
        if (ImGui::Combo("Parameter", &local_metric, [](void* data, int k) {
            return metric_name((Metric)(*(int*)data + k));
        }, (void*)&offset, class_parameter_count(group))) selected_detail_active = true;
        metric = offset + local_metric;
        if (metric_state_count((Metric)metric)) {
            ImGui::SetItemTooltip("Sigma measures fluctuations within the current conformational state.\n"
                "Persistent changes are listed separately in State switches, ranked by time rather than sigma.");
        } else if (metric == MinorGroove || metric == MajorGroove) {
            ImGui::SetItemTooltip("3DNA direct phosphorus-phosphorus groove width minus 5.8 A (phosphate diameter).\n"
                "Needs 2-3 pairs on each side of the step; undefined near helix ends.");
        }
        const int count = (int)columns[metric];
        if (count <= 0) {
            ImGui::TextDisabled("No consecutive inter-base-pair links were found.");
        } else {
            member = std::clamp(member, 0, count - 1);
            int member_number = member + 1;
            if (ImGui::InputInt(group == Class_Step ? "Step" : group == Class_Nucleotide ? "Nucleotide" : "Base pair", &member_number)) {
                selected_detail_active = true;
                member = std::clamp(member_number, 1, count) - 1;
                focus_heatmap_selection = true;
            }
            const auto& labels = class_labels(group);
            ImGui::TextDisabled("%s (%s)", labels[member].c_str(), metric_unit((Metric)metric));
            const size_t frame = nearest_frame(app->animation.frame, nullptr, run_num_frames(app));
            size_t valid_pairs = 0, valid_steps = 0;
            for (size_t p = 0; p < pairs.size(); ++p) if (!pair_quality_flags[frame * pairs.size() + p]) ++valid_pairs;
            for (size_t st = 0; st < steps.size(); ++st) if (!step_quality_flags[frame * steps.size() + st]) ++valid_steps;
            ImGui::SetItemTooltip("Frame %zu: %zu/%zu pairs and %zu/%zu steps valid.\n"
                "A missing internal pair invalidates both neighbouring steps; gaps are never bridged.",
                frame, valid_pairs, pairs.size(), valid_steps, steps.size());
        }
        int frame_number = (int)std::llround(app->animation.frame);
        if (ImGui::InputInt("Frame", &frame_number)) {
            app->animation.frame = (double)std::clamp(frame_number, 0, (int)run_num_frames(app) - 1);
            app->animation.mode = PlaybackMode::Stopped;
            focus_heatmap_selection = true;
        }

        ImGui::SeparatorText("Detection");
        ImGui::Combo("Compare against", &reference_scope,
            "Own pair/step over time\0All pairs/steps over time (same helix)\0");
        ImGui::SetItemTooltip("Own history: temporal deviations at this location.\n"
            "All: pooled distribution of the same parameter across this helix and the baseline time range,\n"
            "including this location. Nucleotide parameters pool nucleotides, not pairs or steps.\n"
            "Persistent distortions can stand out; sequence differences and terminal effects can also be normal.\n"
            "State-conditioned parameters still compare within the current conformational state.");
        ImGui::TextWrapped("%s", reference_scope == Reference_HelixHistory
            ? "Pooled reference: detects persistent local differences."
            : "Own history: detects temporal deviations.");
        ImGui::SliderFloat("Tolerance", &outlier_sigma, 1.5f, 4.0f, "%.1f sigma");
        ImGui::SetItemTooltip("Outliers and regions start above the tolerance, using the selected reference distribution.");
        draw_event_time_settings();

        const std::string parameters = event_parameters_label();
        char header[128];
        const int chosen = (int)std::bitset<Metric_Count>(event_parameters).count();
        if (parameters.size() <= 18) snprintf(header, sizeof(header), "Event parameters: %s###event_parameters", parameters.c_str());
        else snprintf(header, sizeof(header), "Event parameters: %d of %d###event_parameters", chosen, (int)Metric_Count);
        if (ImGui::CollapsingHeader(header)) draw_event_parameters();
        else if (ImGui::IsItemHovered()) ImGui::SetTooltip("Events use: %s", parameters.c_str());

        if (ImGui::CollapsingHeader("Advanced detection")) {
            ImGui::Combo("Baseline", &baseline, "Mean / SD\0Median / MAD\0");
            ImGui::SetItemTooltip("Center and scale of the selected reference distribution.\nMean / SD: classic; large excursions inflate their own SD.\n"
                "Median / MAD: robust; scale is 1.4826 x MAD (SD if MAD is 0).\nAngular parameters are measured on the circle (wrap at +/-180 deg).");
            draw_event_advanced();
            ImGui::Checkbox("Count any selected parameter", &aggregate_outliers);
            ImGui::SetItemTooltip("Count plot, heatmap and 3D: a member is outlying when any chosen parameter of its class is.\n"
                "Does not affect events (see Event parameters).");
            if (aggregate_outliers) {
                if (ImGui::BeginTable("##aggregate", 2)) {
                    for (int k = 0; k < class_parameter_count(group); ++k) {
                        ImGui::TableNextColumn();
                        bool selected = (selected_parameters & (1u << k)) != 0;
                        if (ImGui::Checkbox(metric_name((Metric)(offset + k)), &selected)) {
                            if (selected) selected_parameters |= 1u << k;
                            else selected_parameters &= ~(1u << k);
                        }
                    }
                    ImGui::EndTable();
                }
                if (!(selected_parameters & class_all_parameters(group))) ImGui::TextDisabled("Select at least one parameter.");
            }
        }

        if (ImGui::Button("Visual feedback...", ImVec2(-FLT_MIN, 0))) ImGui::OpenPopup("##visual_feedback");
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize()*29,0), ImGuiCond_Appearing);
        ImGui::SetNextWindowSizeConstraints(ImVec2(0,0),
            ImVec2(ImGui::GetMainViewport()->WorkSize.x*0.95f, ImGui::GetMainViewport()->WorkSize.y*0.85f));
        if (ImGui::BeginPopup("##visual_feedback", ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Visual feedback");
            ImGui::Separator();
            ImGui::PushItemWidth(ImGui::GetFontSize()*12);
            if (ImGui::BeginTabBar("##visual_tabs")) {
                if (ImGui::BeginTabItem("Event regions")) {
                    ImGui::Checkbox("Highlight event contributions", &sync_viewport);
                    ImGui::Checkbox("Signed outlier overlay", &signed_overlay);
                    ImGui::SetItemTooltip("Master switch for event regions, signed markers and measurement geometry.");
                    ImGui::Combo("Region style", &event_visual_style, "Contour\0Glow\0Both\0Off\0");
                    ImGui::SetItemTooltip("Event/state context only. Off leaves current-contribution markers and measurements available.");
                    if (event_visual_style == EventVisual_Contour || event_visual_style == EventVisual_Both) {
                        ImGui::SliderFloat("Contour thickness", &contour_thickness, 1, 4, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
                        ImGui::SetItemTooltip("Multiplier of native silhouette width. Does not change normal selection.");
                    }
                    if (event_visual_style == EventVisual_Glow || event_visual_style == EventVisual_Both)
                        ImGui::SliderFloat("Glow strength", &glow_strength, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                    ImGui::Checkbox("Soft marker halos", &overlay_halo);
                    ImGui::SetItemTooltip("Small signed-contribution markers only, independent of region style.");
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Measurements")) {
                    draw_measurement_style_controls(app);
                    ImGui::Checkbox("Measurement value labels", &measurement_text);
                    ImGui::SetItemTooltip("Lavery labels only; off does not hide geometry or hover details.");
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::Separator();
            ImGui::Checkbox("Heatmap", &show_heatmap);
            ImGui::Checkbox("Smoothed midpoint axis", &show_axis);
            if (ImGui::Button("Reset visual defaults")) {
                event_visual_style = EventVisual_Glow;
                contour_thickness = 1.5f;
                glow_strength = 0.5f;
                signed_overlay = sync_viewport = overlay_halo = show_heatmap = show_axis = true;
                measurement_text = false;
                reset_measurement_style(app);
            }
            ImGui::SetItemTooltip("Also restores shared VIAMD measurement colours, opacity and line width. Does not change detection or selection.");
            ImGui::SameLine();
            if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
            if (ImGui::Button("Reset layout")) reset_layout();
            ImGui::PopItemWidth();
            ImGui::EndPopup();
        }

        if (ImGui::CollapsingHeader("Export & tools")) {
            const float wide = -FLT_MIN;
            if (ImGui::Button("Export parameter CSV", ImVec2(wide, 0))) export_csv(run_num_frames(app));
            if (ImGui::Button("Export events CSV", ImVec2(wide, 0))) export_events_csv(run_num_frames(app));
            ImGui::SetItemTooltip("Export all detected events, including those hidden by table filters.");
            if (ImGui::Button("Export shown events CSV", ImVec2(wide, 0))) export_events_csv(run_num_frames(app), true);
            ImGui::SetItemTooltip("Respect maximum shown duration, text/region filter and Active now.\n"
                "Event and parent IDs are preserved; a hidden parent may not have an exported row.");
            if (ImGui::Button("Export state switches CSV", ImVec2(wide, 0))) export_state_switches_csv();
            if (ImGui::Button("Export displayed axis CSV", ImVec2(wide, 0))) export_axis();
            if (ImGui::Button("Recompute analysis", ImVec2(wide, 0))) reset_requested = true;
            ImGui::TextDisabled("Script: attr(\"lavery/step/twist\")");
            ImGui::SetItemTooltip("Canonical A/G/C/T/U; fixed partners identified in frame 0.");
        }
        ImGui::PopItemWidth();
    }

    std::string event_parameters_label() const {
        if (event_parameters == kEventAllParameters) return "all";
        std::string label;
        bool whole = true;
        for (int cls = 0; cls < Class_Count; ++cls) {
            const uint32_t mask = class_all_parameters(cls) << class_first_metric(cls);
            if ((event_parameters & mask) == 0) continue;
            if ((event_parameters & mask) != mask) { whole = false; break; }
            if (!label.empty()) label += "+";
            label += cls == Class_Pair ? "intra" : cls == Class_Step ? "inter" : "nucleotide";
        }
        if (whole && !label.empty()) return label;
        label.clear();
        for (int k = 0; k < (int)Metric_Count; ++k) {
            if (!(event_parameters & (1u << k))) continue;
            if (!label.empty()) label += "+";
            label += metric_name((Metric)k);
        }
        return label.empty() ? "none" : label;
    }

    void draw_event_parameters() {
        if (ImGui::BeginTable("##event_parameters", Class_Count, ImGuiTableFlags_SizingStretchSame)) {
            static const char* titles[] = {"Intra##ev", "Inter##ev", "Nucl.##ev"};
            for (int group = 0; group < Class_Count; ++group) {
                ImGui::TableNextColumn();
                const uint32_t group_mask = class_all_parameters(group) << class_first_metric(group);
                const uint32_t chosen = event_parameters & group_mask;
                bool all = chosen == group_mask;
                if (chosen && !all) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
                if (ImGui::Checkbox(titles[group], &all)) {
                    event_parameters = all ? (event_parameters | group_mask) : (event_parameters & ~group_mask);
                }
                if (chosen && !all) ImGui::PopItemFlag();
                ImGui::SetItemTooltip("Toggle all %s parameters. Events use these, independently of the plotted class.",
                    class_name(group));
            }
            for (int k = 0; k < MAX_CLASS_PARAMETERS; ++k) {
                for (int group = 0; group < Class_Count; ++group) {
                    ImGui::TableNextColumn();
                    if (group == Class_Pair && k == class_parameter_count(Class_Pair)) {
                        ImGui::Checkbox("Pair loss", &event_pair_loss);
                        ImGui::SetItemTooltip("Include broken pairs in events; invalid parameters remain excluded.\n"
                            "Independent of the six intra-base-pair parameter checkboxes.\n"
                            "Each lost frame scores the tolerance.");
                    }
                    if (k >= class_parameter_count(group)) continue;
                    const int id = class_first_metric(group) + k;
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
            event_parameters = outlier_parameter_mask() << class_first_metric(metric_cls());
        }
        ImGui::SetItemTooltip("Copy the class and parameter(s) of the outlier-count plot.");
        if (!event_parameters) {
            ImGui::TextDisabled(event_pair_loss ? "No parameter: only pair losses form events." : "No parameter: no events.");
        }
    }

    void draw_event_advanced() {
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
            ImGui::Combo("Sub-events", &event_burst_mode, "Stronger\0Stand out from background\0");
            ImGui::SetItemTooltip("Stronger: children are where |z| exceeds a higher threshold than the parent.\n"
                "Stand out from background: children are where z differs from the parent's background,\n"
                "the per-frame median z of all base pairs (or steps) in the parent region. A global shift\n"
                "cancels, so local activity no stronger than a broad event is still found.");
            if (event_burst_mode == 0) {
                ImGui::SliderFloat("Burst extra", &event_burst_extra, 0.25f, 4.0f, "+%.2f sigma");
                ImGui::SetItemTooltip("Child events start at %.2f sigma and continue above %.2f sigma.\n"
                    "Uses the same minimum duration, gap limit and local Neighbourhood as parents.\n"
                    "Pair losses also qualify. Raising this value isolates stronger local bursts.",
                    outlier_sigma + event_burst_extra, std::max(0.5f, outlier_sigma + event_burst_extra - event_hysteresis));
            } else {
                ImGui::SliderFloat("Residual", &event_residual_sigma, 0.5f, 6.0f, "%.2f sigma");
                ImGui::SetItemTooltip("Child events start where |z - background| reaches %.2f sigma and continue above %.2f sigma.\n"
                    "Only parameters already outlying in the parent count. Needs at least 5 valid members\n"
                    "in the parent region per frame; smaller regions produce no children.",
                    event_residual_sigma, std::max(0.5f, event_residual_sigma - event_hysteresis));
            }
        }
        ImGui::SliderFloat("Hysteresis", &event_hysteresis, 0.0f, 2.0f, "%.1f sigma");
        ImGui::SetItemTooltip("An event starts at the tolerance (%.1f sigma) and continues while |z| stays above %.1f sigma,\n"
            "so a value hovering around the threshold does not break one event into many.", outlier_sigma, event_end_sigma());
        const size_t frame_count = run_num_frames(app);
        if (timeline_times(frame_count)) {
            const float scale = event_time_ns ? 1000.0f : 1.0f;
            float value = event_gap_time_ps / scale;
            if (ImGui::InputFloat(event_time_ns ? "Bridge gaps (ns)" : "Bridge gaps (ps)", &value, 0, 0, "%.6g")) event_gap_time_ps = value * scale;
            ImGui::SetItemTooltip("Maximum quiet-gap time: time just before the new sample minus the previous contribution's time.\n"
                "Consecutive samples always connect with zero gap, including irregular sampling.");
            ImGui::Checkbox("Time in ns", &event_time_ns);
            ImGui::SetItemTooltip("Enter durations, gaps and ranges in ns instead of ps.");
        } else if (!md_unit_base_equal(app->timeline.time_unit, md_unit_second())) {
            if (ImGui::InputInt("Bridge gaps", &event_max_gap)) event_max_gap = std::clamp(event_max_gap, 0, 100000);
            ImGui::SetItemTooltip("Merge outliers separated by at most this many frames without outliers or data.");
        }
    }

    static void extract_period_id(uint32_t& id, str_t arg) {
        int64_t value = 0;
        if (!viamd::extract_int64(value, arg)) return;
        if (value < 0 || value > UINT32_MAX) {
            MD_LOG_ERROR("Lavery: invalid timeline period ID");
            return;
        }
        id = (uint32_t)value;
    }

    std::pair<int, int> period_frames(uint32_t id, size_t frame_count) const {
        const TimelinePeriod* period = timeline_periods_find(&app->timeline.periods, id);
        int first = 0, last = 0;
        if (!period || frame_count > INT_MAX ||
            !timeline_period_frames(&first, &last, *period, app->animation.frame, (int)frame_count)) return {-1, -1};
        return {first, last};
    }

    void draw_analysis_range(const char* label, bool& enabled, bool& use_period, uint32_t& period_id,
                             float& begin_ps, float& end_ps,
                             int& first, int& last, const float* times, size_t frame_count, const char* tooltip) {
        ImGui::PushID(label);
        int mode = enabled ? use_period ? 2 : 1 : 0;
        if (ImGui::Combo(label, &mode, "Full trajectory\0Custom\0Timeline period\0")) {
            enabled = mode != 0;
            use_period = mode == 2;
            if (mode == 1) {
                if (times) {
                    const double to_ps = md_unit_scale(app->timeline.time_unit) / md_unit_scale(md_unit_picosecond());
                    begin_ps = (float)(times[0] * to_ps);
                    end_ps = (float)(times[frame_count - 1] * to_ps);
                } else {
                    first = 0;
                    last = (int)frame_count - 1;
                }
            }
        }
        ImGui::SetItemTooltip("%s", tooltip);
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(TIMELINE_PERIOD_DND)) {
                if (payload->DataSize == sizeof(uint32_t)) {
                    period_id = *(const uint32_t*)payload->Data;
                    enabled = use_period = true;
                } else {
                    MD_LOG_ERROR("Lavery: invalid timeline period drag payload");
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (enabled && use_period) {
            const auto& periods = app->timeline.periods;
            const TimelinePeriod* selected = timeline_periods_find(&periods, period_id);
            if (ImGui::BeginCombo("Period", selected ? selected->name : "Select a period...")) {
                for (int i = 0; i < periods.count; ++i) {
                    const auto& period = periods.items[i];
                    ImGui::PushID((int)period.id);
                    if (ImGui::Selectable(period.name, period.id == period_id)) period_id = period.id;
                    if (ImGui::IsItemHovered()) timeline_periods_highlight(period.id);
                    ImGui::PopID();
                }
                if (!periods.count) ImGui::TextDisabled("Create a period in VIAMD's timeline.");
                ImGui::EndCombo();
            }
            selected = timeline_periods_find(&periods, period_id);
            if (selected && ImGui::IsItemHovered()) timeline_periods_highlight(selected->id);
            const auto [first_frame, last_frame] = period_frames(period_id, frame_count);
            if (first_frame < 0) {
                ImGui::TextWrapped("%s", !selected
                    ? "Selected period is missing. Select an existing period; Full trajectory is not substituted."
                    : "Selected period covers no trajectory samples.");
            } else {
                if (times) {
                    const double scale = md_unit_scale(app->timeline.time_unit) /
                        md_unit_scale(md_unit_picosecond()) / (event_time_ns ? 1000.0 : 1.0);
                    ImGui::TextDisabled("%.6g - %.6g %s", times[first_frame] * scale,
                        times[last_frame] * scale, event_time_ns ? "ns" : "ps");
                }
                ImGui::TextDisabled("Frames %d - %d%s", first_frame, last_frame,
                    selected->follow ? " (follows current frame)" : "");
            }
            ImGui::PopID();
            return;
        }
        if (times) {
            const float scale = event_time_ns ? 1000.0f : 1.0f;
            if (enabled) {
                float begin = begin_ps / scale, end = end_ps / scale;
                if (ImGui::InputFloat(event_time_ns ? "From (ns)" : "From (ps)", &begin, 0, 0, "%.6g")) begin_ps = begin * scale;
                if (ImGui::InputFloat(event_time_ns ? "To (ns)" : "To (ps)", &end, 0, 0, "%.6g")) end_ps = end * scale;
            } else {
                const double display_scale = md_unit_scale(app->timeline.time_unit) /
                    md_unit_scale(md_unit_picosecond()) / scale;
                ImGui::TextDisabled("%.6g - %.6g %s", times[0] * display_scale,
                    times[frame_count - 1] * display_scale, event_time_ns ? "ns" : "ps");
            }
        } else if (enabled) {
            ImGui::InputInt("First frame", &first);
            ImGui::InputInt("Last frame", &last);
        } else {
            ImGui::TextDisabled("Frames 0 - %zu", frame_count - 1);
        }
        ImGui::PopID();
    }

    // Always-visible duration and independent reference/search intervals.
    void draw_event_time_settings() {
        const size_t frame_count = run_num_frames(app);
        const float* times = timeline_times(frame_count);
        if (times) {
            initialize_event_times(times, frame_count);
            const float scale = event_time_ns ? 1000.0f : 1.0f;
            auto input_time = [&](const char* label, float& ps) {
                float value = ps / scale;
                if (ImGui::InputFloat(label, &value, 0, 0, "%.6g")) ps = value * scale;
            };
            input_time(event_time_ns ? "Min duration (ns)" : "Min duration (ps)", event_min_time_ps);
            ImGui::SetItemTooltip("Elapsed time from first to last contributing sample, including bridged gaps.\n"
                "0 retains single-frame spikes. Initial value: two median sampling intervals.");
        } else if (!md_unit_base_equal(app->timeline.time_unit, md_unit_second())) {
            if (ImGui::InputInt("Min frames", &event_min_frames)) event_min_frames = std::clamp(event_min_frames, 1, 100000);
            ImGui::SetItemTooltip("No time metadata: frame-based detection.\n"
                "Require contributing frames for parents and children (default 3).\n"
                "Bridged gaps do not count. Set 1 to retain single-frame spikes.");
        } else {
            ImGui::TextWrapped("Physical timestamps do not match the trajectory. Event detection is unavailable.");
        }
        if (frame_count && (times || !md_unit_base_equal(app->timeline.time_unit, md_unit_second()))) {
            draw_analysis_range("Baseline range", baseline_range_enabled, baseline_range_period, baseline_period_id,
                baseline_range_begin_ps, baseline_range_end_ps,
                baseline_range_first, baseline_range_last, times, frame_count,
                "Reference interval for center/scale and within-state occupancy, independent of detection.\n"
                "States absent from the reference have no baseline; no other state's statistics are borrowed.");
            draw_analysis_range("Detection range", event_range_enabled, event_range_period, event_period_id,
                event_range_begin_ps, event_range_end_ps,
                event_range_first, event_range_last, times, frame_count,
                "Search for outlier events and state switches within this inclusive interval.\n"
                "Does not change the baseline. Count plot and distributions still cover the full trajectory.");
        }
    }

    void draw_inspector(int count, size_t frame_count) {
        const auto& labels = class_labels(metric_cls());
        const std::vector<float>& data = values[metric];
        if (data.size() < frame_count * (size_t)count) return;
        const auto& stats = statistics[metric];
        ImGui::TextDisabled("%s z-score", statistics_reference_scope == Reference_HelixHistory ? "Pooled-helix" : "Temporal");
        const size_t current_frame = (size_t)std::clamp((int64_t)std::llround(app->animation.frame),
            (int64_t)0, (int64_t)frame_count - 1);
        const float current_value = data[current_frame * count + member];
        if (std::isfinite(current_value)) {
            const std::string state = state_text((Metric)metric, (size_t)member, current_value);
            ImGui::Text("Frame %zu: %.3f %s (%+.2f sigma)", current_frame, current_value,
                metric_unit((Metric)metric), stats.members[member].zscore(current_value));
            if (!state.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", state.c_str() + 1);
                ImGui::SetItemTooltip("Baseline uses the selected reference's samples in the current state, not the dominant state.\n"
                    "State changes are detected separately with a minimum residence time.");
            }
        } else {
            ImGui::Text("Frame %zu: selected member has no valid sample", current_frame);
        }
        ImGui::SetItemTooltip("In plots: Ctrl-click / Ctrl-drag to jump / scrub time; drag / wheel to pan / zoom.\n"
            "Drag the thin line under a plot to resize it; double-click it to restore the default.");
        const float* times = timeline_times(frame_count);
        char time_label[64];
        timeline_label(time_label, sizeof(time_label), times);
        if (inspected_switch < state_switches.size()) {
            const auto& change = state_switches[inspected_switch];
            ImGui::TextWrapped("%s / %s: %s -> %s (%+.1f deg)",
                nucleotide_labels[change.member].c_str(), metric_name(change.metric),
                metric_state_name(change.metric, change.from), metric_state_name(change.metric, change.to), change.angular_change);
            draw_switch_context(change, times, frame_count);
            draw_quality(parameter_quality(current_frame, change.member, change.metric),
                std::isfinite(values[change.metric][current_frame*columns[change.metric]+change.member]));
        } else if (inspected_event < outlier_events.size()) {
            const auto& event = outlier_events[inspected_event];
            ImGui::TextWrapped("%s #%zu / %s | bp %u-%u | %zu members",
                statistics_reference_scope == Reference_HelixHistory ? "Region" : "Event",
                inspected_event+1, event_kinds[inspected_event].c_str(), event.pair_first+1, event.pair_last+1, event.members.size());
            draw_run_span(statistics_reference_scope == Reference_HelixHistory ? "Region span" : "Event span",
                event.begin, event.end, times, false);
            ImGui::TextWrapped("Selected: %s / %s", labels[member].c_str(), metric_name((Metric)metric));
            draw_quality(quality_at(current_frame, member), std::isfinite(current_value));
        } else {
            draw_quality(quality_at(current_frame, member), std::isfinite(current_value));
        }
        if (ImGui::CollapsingHeader("Timeline", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("All members", &compare_all);
        ImGui::SetItemTooltip("Overlay every member instead of only the selected one.");
        struct TimelineData {
            const float* values;
            const float* times;
            const uint32_t* indices;
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
            timeline_follow.frame = app->animation.frame;
        } else {
            follow_cursor(timeline_follow, times, frame_count);
        }
        if (ImPlot::BeginPlot("##Timeline", ImVec2(-1, timeline_plot_height))) {
            ImPlot::SetupAxes(time_label, metric_unit((Metric)metric));
            const double first_x = times ? times[0] : 0;
            const double last_x = times ? times[frame_count - 1] : (double)frame_count - 1;
            ImPlot::SetupAxisLimits(ImAxis_X1, first_x, std::max(first_x + 1.0, last_x), ImPlotCond_Once);
            const int first = compare_all ? 0 : member;
            const int end = compare_all ? count : member + 1;
            if (summary_metric != metric) {
                timeline_summaries.clear();
                timeline_summaries.resize(count);
                summary_metric = metric;
            }
            for (int m = first; m < end; ++m) {
                auto& summary = timeline_summaries[m];
                if (summary.size() != frame_count && !summary.build(data.data() + m, frame_count, count)) continue;
                const auto indices = visible_plot_indices(summary, times);
                TimelineData timeline_data = {data.data() + (size_t)m, times, indices.data(), count};
                ImPlot::PlotLineG(labels[m].c_str(), [](int idx, void* user_data) {
                    const auto* payload = (const TimelineData*)user_data;
                    const size_t f = payload->indices[idx];
                    return ImPlotPoint(payload->times ? (double)payload->times[f] : (double)f,
                        payload->values[f * payload->stride]);
                }, &timeline_data, (int)indices.size());
            }
            if (inspected_switch < state_switches.size()) {
                const auto& change = state_switches[inspected_switch];
                const ImPlotRect limits = ImPlot::GetPlotLimits();
                const double begin = times ? times[change.begin] : (double)change.begin;
                const double end = times ? times[change.end] : (double)change.end;
                ImPlot::PushPlotClipRect();
                const double source_begin = times ? times[change.source_begin] : (double)change.source_begin;
                const double source_end = times ? times[change.source_end] : (double)change.source_end;
                ImPlot::GetPlotDrawList()->AddRectFilled(ImPlot::PlotToPixels(source_begin, limits.Y.Max),
                    ImPlot::PlotToPixels(source_end, limits.Y.Min), IM_COL32(70,170,210,35));
                if (change.intervening_samples()) ImPlot::GetPlotDrawList()->AddRectFilled(
                    ImPlot::PlotToPixels(source_end, limits.Y.Max), ImPlot::PlotToPixels(begin, limits.Y.Min),
                    IM_COL32(230,180,70,25));
                ImPlot::GetPlotDrawList()->AddRectFilled(ImPlot::PlotToPixels(begin, limits.Y.Max),
                    ImPlot::PlotToPixels(end, limits.Y.Min), IM_COL32(190,128,255,35));
                ImPlot::PopPlotClipRect();
            }
            scrub_timeline("##parameter_scrub", times, frame_count);
            draw_time_cursor(times, frame_count);
            store_follow(timeline_follow);
            ImPlot::EndPlot();
        }
        ImGui::PopID();
        height_handle("##timeline_height", &timeline_plot_height, 220);
        }

        const auto& member_stats = stats.members[member];
        const auto current_reference = member_stats.reference(current_value);
        if (ImGui::CollapsingHeader("Distribution", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SetItemTooltip("Full-trajectory density per member. Click an event to overlay its whole span.");
            auto& pinned = comparison_members[metric_cls()];
            char compare_preview[64];
            if (pinned.empty()) snprintf(compare_preview, sizeof(compare_preview), "none");
            else snprintf(compare_preview, sizeof(compare_preview), "%zu member%s", pinned.size(), pinned.size() == 1 ? "" : "s");
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9);
            if (ImGui::BeginCombo("Compare", compare_preview)) {
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
            if (!pinned.empty()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear")) pinned.clear();
            }
            ImGui::SameLine();
            if (!current_reference.count) {
                ImGui::TextColored(ImVec4(1,0.7f,0.3f,1), "No baseline for this sample/state");
            } else if (statistics_baseline == Baseline_MedianMAD) {
                ImGui::TextDisabled("median %.3f, scaled MAD %.3f, reference n %zu",
                    current_reference.mean, current_reference.sd, current_reference.count);
            } else {
                ImGui::TextDisabled("mean %.3f, SD %.3f, reference n %zu",
                    current_reference.mean, current_reference.sd, current_reference.count);
            }
            if (member_stats.state_metric >= 0) {
                ImGui::SetItemTooltip("Center/scale use the baseline range and current state; the density includes all frames/states.\n"
                    "A missing state has no reference. Singleton/constant references have zero spread, not a calibrated sigma.");
            }
            auto& histogram = distributions[metric];
            if (histogram.valid.empty() && !compute_distributions(&histogram, data, (size_t)count)) {
                ImGui::TextUnformatted("Could not calculate distributions. See the application log.");
                return;
            }
            uint64_t distribution_key = md_hash64(&metric, sizeof(metric), 0xD157);
            distribution_key = md_hash64(&statistics_key, sizeof(statistics_key), distribution_key);
            distribution_key = md_hash64(&member, sizeof(member), distribution_key);
            distribution_key = md_hash64(&frame_count, sizeof(frame_count), distribution_key);
            distribution_key = md_hash64(&inspected_event, sizeof(inspected_event), distribution_key);
            distribution_key = md_hash64(&inspected_switch, sizeof(inspected_switch), distribution_key);
            distribution_key = md_hash64(&counts_key, sizeof(counts_key), distribution_key);
            if (!pinned.empty()) distribution_key = md_hash64(pinned.data(), pinned.size() * sizeof(int), distribution_key);
            const int reference_state = metric_state((Metric)metric, current_value);
            const size_t reference_slot = (size_t)(reference_state + 1);
            auto& reference_distribution = reference_distributions[reference_slot];
            if (statistics_reference_scope == Reference_HelixHistory) {
                const auto groups = helix_member_groups(pairs.size(), steps, metric_cls());
                uint64_t reference_key = md_hash64(&metric, sizeof(metric), statistics_key);
                reference_key = md_hash64(&groups[member], sizeof(groups[member]), reference_key);
                if (reference_key != reference_distribution_keys[reference_slot]) {
                    std::vector<float> pooled;
                    for (size_t f = statistics_first_frame; f < statistics_end_frame; ++f) {
                        for (int m = 0; m < count; ++m) {
                            const float v = data[f * count + m];
                            if (groups[m] == groups[member] && (!metric_state_count((Metric)metric) ||
                                metric_state((Metric)metric, v) == reference_state)) pooled.push_back(v);
                        }
                    }
                    if (!compute_distributions(&reference_distribution, pooled, 1)) {
                        ImGui::TextUnformatted("Could not calculate pooled reference distribution. See the application log.");
                        return;
                    }
                    reference_distribution_keys[reference_slot] = reference_key;
                }
                ImGui::TextDisabled("Reference: same helix, baseline range%s",
                    metric_state_count((Metric)metric) ? ", current state" : "");
            }
            const bool switch_selected = inspected_switch < state_switches.size();
            const bool event_selected = inspected_event < outlier_events.size() || switch_selected;
            const ImVec4 span_color = switch_selected ? ImVec4(0.75f,0.5f,1,1) : event_selected ? event_color(inspected_event) : ImVec4(1,1,1,1);
            const size_t span_id = switch_selected ? inspected_switch : inspected_event;
            const char* span_kind = switch_selected ? "Switch" :
                statistics_reference_scope == Reference_HelixHistory ? "Region" : "Event";
            if (event_selected) {
                const size_t begin = switch_selected ? state_switches[inspected_switch].begin : outlier_events[inspected_event].begin;
                const size_t end = switch_selected ? state_switches[inspected_switch].end : outlier_events[inspected_event].end;
                if (event_distribution_key != distribution_key) {
                    if (!compute_event_distribution(&event_distribution, data, (size_t)count, (size_t)member,
                        begin, end, histogram)) {
                        ImGui::TextUnformatted("Could not calculate event distribution. See the application log.");
                        return;
                    }
                    event_distribution_key = distribution_key;
                }
                if (!event_distribution.samples.empty()) {
                    ImGui::TextColored(span_color, "%s #%zu: median %.3f, range %.3f to %.3f %s",
                        span_kind, span_id + 1, event_distribution.median, event_distribution.minimum, event_distribution.maximum,
                        metric_unit((Metric)metric));
                } else {
                    ImGui::TextColored(span_color, "%s #%zu: no valid samples (e.g. pairing loss)",
                        span_kind, span_id + 1);
                }
                ImGui::SetItemTooltip("Frames %zu-%zu: %zu valid, %zu missing.\n"
                    "All finite values of this member and parameter in the inclusive event span,\n"
                    "including bridged gaps and frames where this member is not a contributor.\n"
                    "Density uses the full trajectory's bins and is normalized independently.\n"
                    "Fewer than 10 valid samples: individual sample marks instead of a density curve.",
                    begin, end, event_distribution.samples.size(), event_distribution.missing);
            }
            if (distribution_key != distribution_fit_key) {
                distribution_fit_key = distribution_key;
                ImPlot::SetNextAxesToFit();
            }
            if (ImPlot::BeginPlot("##Distribution", ImVec2(-1, distribution_plot_height))) {
                ImPlot::SetupAxes(metric_unit((Metric)metric), "Probability density");
                if (statistics_reference_scope == Reference_HelixHistory && !reference_distribution.centers.empty()) {
                    ImPlot::SetNextLineStyle(ImVec4(1,0.8f,0.2f,1), 2.5f);
                    ImPlot::PlotLine("Pooled helix reference", reference_distribution.centers.data(),
                        reference_distribution.density.data(), (int)reference_distribution.centers.size());
                }
                if (!histogram.centers.empty()) {
                    auto plot = [&](int m) {
                        if (!histogram.valid[m]) return;
                        ImPlot::PlotLine(labels[m].c_str(), histogram.centers.data(),
                            histogram.density.data() + (size_t)m * histogram.centers.size(), (int)histogram.centers.size());
                    };
                    for (int m : pinned) if (m != member) plot(m);
                    plot(member);
                    if (event_selected && !event_distribution.samples.empty()) {
                        char label[64];
                        snprintf(label, sizeof(label), "%s #%zu (%zu samples)", span_kind, span_id + 1, event_distribution.samples.size());
                        const ImVec4 color = span_color;
                        if (event_distribution.samples.size() >= 10) {
                            ImPlot::SetNextLineStyle(color, 2.5f);
                            ImPlot::PlotLine(label, histogram.centers.data(), event_distribution.density.data(),
                                (int)histogram.centers.size());
                        } else {
                            std::vector<double> y(event_distribution.samples.size(), 0.0);
                            ImPlot::SetNextMarkerStyle(ImPlotMarker_Diamond, 5, color, 1, color);
                            ImPlot::PlotScatter(label, event_distribution.samples.data(), y.data(), (int)y.size());
                        }
                    }
                }
                const size_t frame = (size_t)std::clamp((int64_t)std::llround(app->animation.frame),
                    (int64_t)0, (int64_t)frame_count - 1);
                const float value = data[frame * count + member];
                if (std::isfinite(value)) ImPlot::PlotInfLines("Current value", &value, 1);
                ImPlot::EndPlot();
            }
            height_handle("##distribution_height", &distribution_plot_height, 180);
        } else if (!member_stats.count) {
            ImGui::TextDisabled("No finite values for this member; other members remain available below.");
        }
        draw_current_outliers(count, frame_count);
    }

    const float* timeline_times(size_t frame_count) const {
        return md_array_size(app->timeline.x_values) == frame_count &&
            md_unit_base_equal(app->timeline.time_unit, md_unit_second())
            ? app->timeline.x_values : nullptr;
    }

    void initialize_event_times(const float* times, size_t frame_count) {
        if (!times || (event_min_time_ps != -1 && event_gap_time_ps != -1)) return;
        const double to_ps = md_unit_scale(app->timeline.time_unit) / md_unit_scale(md_unit_picosecond());
        std::vector<double> intervals;
        for (size_t f = 1; f < frame_count; ++f) {
            const double dt = ((double)times[f] - times[f - 1]) * to_ps;
            if (std::isfinite(dt) && dt > 0) intervals.push_back(dt);
        }
        std::sort(intervals.begin(), intervals.end());
        const double dt = intervals.empty() ? 0 : intervals[intervals.size() / 2];
        if (event_min_time_ps == -1) event_min_time_ps = (float)(dt * (event_min_frames - 1));
        if (event_gap_time_ps == -1) event_gap_time_ps = (float)(dt * event_max_gap);
    }

    uint32_t outlier_parameter_mask() const {
        const int cls = metric_cls();
        return aggregate_outliers ? selected_parameters & class_all_parameters(cls) : 1u << (metric - class_first_metric(cls));
    }

    // sigma < 0 uses the tolerance. 'strong' receives the mask bits that also reach the tolerance.
    OutlierAssessment assess_member(size_t frame, int cls, size_t index, uint32_t mask, float* z = nullptr,
                                    float sigma = -1, uint32_t* strong = nullptr) const {
        const int offset = class_first_metric(cls);
        const int n = class_parameter_count(cls);
        float sample[MAX_CLASS_PARAMETERS];
        MemberStatistics baselines[MAX_CLASS_PARAMETERS];
        if (strong) *strong = 0;
        for (int k = 0; k < n; ++k) {
            if (!z && !(mask & (1u << k))) continue;
            sample[k] = values[offset + k][frame * columns[offset + k] + index];
            baselines[k] = statistics[offset + k].members[index];
            if (z) z[k] = baselines[k].zscore(sample[k]);
            if (strong && (mask & (1u << k)) && baselines[k].is_outlier(sample[k], outlier_sigma)) *strong |= 1u << k;
        }
        return assess_outliers(sample, baselines, n, mask, sigma < 0 ? outlier_sigma : sigma);
    }

    float event_end_sigma() const { return std::max(0.5f, outlier_sigma - std::max(0.0f, event_hysteresis)); }

    bool pair_lost(size_t frame, size_t pair) const {
        constexpr uint16_t broken = Quality_Separation | Quality_Alignment | Quality_PlaneOffset | Quality_NoPairContacts;
        return pair < pair_has_baseline.size() && pair_has_baseline[pair] &&
            (pair_quality_flags[frame * pairs.size() + pair] & broken) &&
            !(pair_quality_flags[frame * pairs.size() + pair] & ~(broken | Quality_AdjacentPair));
    }

    OutlierAssessment assess(size_t frame, size_t index) const {
        return assess_member(frame, metric_cls(), index, outlier_parameter_mask());
    }

    size_t current_frame() const {
        const size_t frames = app ? run_num_frames(app) : 0;
        return frames ? nearest_frame(app->animation.frame, nullptr, frames) : 0;
    }

    void refresh_event_inspection() {
        if (!app) return;
        if (app->animation.mode == PlaybackMode::Playing) {
            selected_detail_active = false;
            detail_metric = -1;
            hovered_metric = hovered_frame = -1;
            preview.clear();
        }
        if (inspected_switch != SIZE_MAX && (inspected_switch >= state_switches.size() ||
            app->animation.mode == PlaybackMode::Playing || app->animation.frame != inspection_frame)) inspected_switch = SIZE_MAX;
        if (inspected_event != SIZE_MAX && (app->animation.mode == PlaybackMode::Playing ||
            app->animation.frame != inspection_frame || inspected_event >= outlier_events.size())) {
            inspected_event = SIZE_MAX;
        }
    }

    size_t displayed_event() const {
        if (inspected_event >= outlier_events.size() || inspection_scope == Inspection_All) return SIZE_MAX;
        const size_t parent = outlier_events[inspected_event].parent;
        return inspection_scope == Inspection_Parent && parent != SIZE_MAX ? parent : inspected_event;
    }

    bool event_visible(size_t index, size_t frame) const {
        const auto& event = outlier_events[index];
        const size_t displayed = displayed_event();
        return (displayed == SIZE_MAX || displayed == index) &&
            frame >= event.begin && frame <= event.end;
    }

    void inspect_event(size_t index, size_t frame) {
        inspected_switch = SIZE_MAX;
        app->animation.frame = (double)frame;
        app->animation.mode = PlaybackMode::Stopped;
        inspected_event = index;
        inspection_scope = Inspection_Selected;
        inspection_frame = app->animation.frame;
        focus_heatmap_selection = true;
    }

    bool overlay_active() const {
        return app && show_window && computed && !calculating && calculation_ok.load() &&
            !pairs.empty() && statistics[0].members.size() == pairs.size();
    }

    // Pick ids: pairs, then steps, then nucleotides (2 per pair).
    uint32_t pick_id(int cls, uint32_t index) const {
        return cls == Class_Pair ? index : cls == Class_Step ? (uint32_t)pairs.size() + index
                                                             : (uint32_t)(pairs.size() + steps.size()) + index;
    }

    size_t picking_count() const { return 29 * pairs.size() + 9 * steps.size(); }

    uint32_t parameter_pick_id(int id, uint32_t index) const {
        uint32_t offset = (uint32_t)(3 * pairs.size() + steps.size());
        for (int k = 0; k < id; ++k) offset += (uint32_t)class_members(metric_class((Metric)k));
        return offset + index;
    }

    bool decode_pick(uint32_t local, int* cls, uint32_t* index, int* parameter = nullptr) const {
        if (parameter) *parameter = -1;
        const uint32_t base_count = (uint32_t)(3 * pairs.size() + steps.size());
        if (local >= base_count) {
            local -= base_count;
            for (int id = 0; id < (int)Metric_Count; ++id) {
                const int group = metric_class((Metric)id);
                const size_t count = class_members(group);
                if (local < count) {
                    *cls = group;
                    *index = local;
                    if (parameter) *parameter = id;
                    return true;
                }
                local -= (uint32_t)count;
            }
            return false;
        }
        if (local < pairs.size()) {
            *cls = Class_Pair;
            *index = local;
            return true;
        }
        local -= (uint32_t)pairs.size();
        if (local < steps.size()) {
            *cls = Class_Step;
            *index = local;
            return true;
        }
        local -= (uint32_t)steps.size();
        if (local < 2 * pairs.size()) {
            *cls = Class_Nucleotide;
            *index = local;
            return true;
        }
        return false;
    }

    int metric_cls() const { return metric_class((Metric)metric); }

    std::string state_text(Metric id, size_t index, float value) const {
        if (!metric_state_count(id) || index >= statistics[id].members.size()) return {};
        const auto& stats = statistics[id].members[index];
        const int now = metric_state(id, value);
        if (now < 0) return {};
        char buf[160];
        if (!stats.reference(value).count) snprintf(buf, sizeof(buf), "\nState: %s (no baseline)", metric_state_name(id, now));
        else if (now == stats.state) snprintf(buf, sizeof(buf), "\nState: %s (dominant, %.0f%% of baseline samples)",
            metric_state_name(id, now), 100.0f * stats.state_fraction);
        else snprintf(buf, sizeof(buf), "\nState: %s (baseline usually %s, %.0f%%)",
            metric_state_name(id, now), metric_state_name(id, stats.state), 100.0f * stats.state_fraction);
        return buf;
    }

    uint32_t nucleotide_base(uint32_t member) const {
        return member % 2 ? pairs[member / 2].b : pairs[member / 2].a;
    }

    const std::vector<std::string>& class_labels(int cls) const {
        return cls == Class_Step ? step_labels : cls == Class_Nucleotide ? nucleotide_labels : pair_labels;
    }

    size_t class_members(int cls) const {
        return cls == Class_Step ? steps.size() : cls == Class_Nucleotide ? 2 * pairs.size() : pairs.size();
    }

    uint32_t marker_mask(int cls, uint32_t index) const {
        for (const auto& m : markers) if (m.cls == cls && m.index == index) return m.mask;
        return 0;
    }

    // Selects a member (switching class when needed), optionally a class-local parameter, and jumps to a frame.
    void select_target(int cls, uint32_t index, int parameter, size_t frame) {
        const int offset = class_first_metric(cls);
        const int n = class_parameter_count(cls);
        const bool class_changed = metric_cls() != cls;
        const int current = std::min(metric - class_first_metric(metric_cls()), n - 1);
        metric = offset + (parameter >= 0 && parameter < n ? parameter : current);
        member = (int)index;
        selected_detail_active = true;
        if (class_changed) reset_heatmap_view = true;
        app->animation.frame = (double)frame;
        app->animation.mode = PlaybackMode::Stopped;
        focus_heatmap_selection = true;
        show_window = true;
    }

    void update_statistics(size_t frame_count) {
        uint64_t key = md_hash64(&baseline, sizeof(baseline), 0xBA5E);
        key = md_hash64(&reference_scope, sizeof(reference_scope), key);
        key = md_hash64(&baseline_range_enabled, sizeof(baseline_range_enabled), key);
        key = md_hash64(&baseline_range_period, sizeof(baseline_range_period), key);
        key = md_hash64(&baseline_period_id, sizeof(baseline_period_id), key);
        const auto baseline_period_frames = baseline_range_enabled && baseline_range_period
            ? period_frames(baseline_period_id, frame_count) : std::pair<int, int>{0, 0};
        key = md_hash64(&baseline_period_frames.first, sizeof(int), key);
        key = md_hash64(&baseline_period_frames.second, sizeof(int), key);
        key = md_hash64(&baseline_range_begin_ps, sizeof(baseline_range_begin_ps), key);
        key = md_hash64(&baseline_range_end_ps, sizeof(baseline_range_end_ps), key);
        key = md_hash64(&baseline_range_first, sizeof(baseline_range_first), key);
        key = md_hash64(&baseline_range_last, sizeof(baseline_range_last), key);
        key = md_hash64(&frame_count, sizeof(frame_count), key);
        const float* times = timeline_times(frame_count);
        const bool physical_time = md_unit_base_equal(app->timeline.time_unit, md_unit_second());
        key = md_hash64(&physical_time, sizeof(physical_time), key);
        if (times) {
            key = md_hash64(times, frame_count * sizeof(float), key);
            const double scale = md_unit_scale(app->timeline.time_unit);
            key = md_hash64(&scale, sizeof(scale), key);
        }
        if (statistics_key == key) return;
        if (statistics_key && ImGui::IsAnyItemActive()) return;
        baseline_range_error.clear();
        statistics_first_frame = 0;
        statistics_end_frame = frame_count;
        bool valid = true;
        if (baseline_range_enabled) {
            if (baseline_range_period) {
                valid = baseline_period_frames.first >= 0;
                if (valid) {
                    statistics_first_frame = (size_t)baseline_period_frames.first;
                    statistics_end_frame = (size_t)baseline_period_frames.second + 1;
                }
            } else if (times) {
                EventOptions range;
                const double to_ps = md_unit_scale(app->timeline.time_unit) / md_unit_scale(md_unit_picosecond());
                range.times.resize(frame_count);
                for (size_t f = 0; f < frame_count; ++f) range.times[f] = times[f] * to_ps;
                valid = event_time_range(&range, baseline_range_begin_ps, baseline_range_end_ps, "baseline");
                if (valid) {
                    statistics_first_frame = range.first_frame;
                    statistics_end_frame = range.last_frame + 1;
                }
            } else {
                valid = !physical_time && baseline_range_first >= 0 && baseline_range_last >= baseline_range_first &&
                    (size_t)baseline_range_last < frame_count;
                if (valid) {
                    statistics_first_frame = (size_t)baseline_range_first;
                    statistics_end_frame = (size_t)baseline_range_last + 1;
                }
            }
        }
        if (!valid) {
            baseline_range_error = baseline_range_period
                ? "Baseline period is missing or covers no samples. Select an existing period. Sigma analysis has no reference; state-switch detection is independent."
                : "Invalid baseline range: use ordered limits containing at least one sample. Sigma analysis has no reference; state-switch detection is independent.";
            MD_LOG_ERROR("Lavery: %s", baseline_range_error.c_str());
            statistics_first_frame = statistics_end_frame = 0;
        }
        std::array<std::vector<uint32_t>, Class_Count> groups;
        // Pair loss still requires this pair to have a finite reference sample, not a neighbour's.
        pair_has_baseline.assign(pairs.size(), 0);
        for (size_t p = 0; p < pairs.size(); ++p)
            for (size_t f = statistics_first_frame; f < statistics_end_frame; ++f)
                if (std::isfinite(values[Buckle][f * pairs.size() + p])) {
                    pair_has_baseline[p] = 1;
                    break;
                }
        if (reference_scope == Reference_HelixHistory)
            for (int cls = 0; cls < Class_Count; ++cls) groups[cls] = helix_member_groups(pairs.size(), steps, cls);
        for (int m = 0; m < (int)Metric_Count; ++m) {
            if (!compute_statistics(&statistics[m], values[m], columns[m], metric_is_angular((Metric)m), (Baseline)baseline,
                    metric_state_count((Metric)m) ? m : -1, statistics_first_frame, statistics_end_frame,
                    groups[metric_class((Metric)m)])) {
                MD_LOG_ERROR("Lavery: could not recompute statistics for %s", metric_name((Metric)m));
            }
        }
        statistics_key = key;
        statistics_baseline = baseline;
        statistics_reference_scope = reference_scope;
        counts_key = 0;
    }

    std::pair<double, double> baseline_bounds(const float* times) const {
        if (statistics_end_frame <= statistics_first_frame) {
            const double missing = std::numeric_limits<double>::quiet_NaN();
            return {missing, missing};
        }
        return {times ? (double)times[statistics_first_frame] : (double)statistics_first_frame,
                times ? (double)times[statistics_end_frame - 1] : (double)statistics_end_frame - 1};
    }

    uint16_t parameter_quality(size_t frame, size_t index, int id) const {
        const int cls = metric_class((Metric)id);
        if (cls == Class_Nucleotide) return nucleotide_quality_flags[id-Alpha][frame * columns[id] + index];
        return cls == Class_Step ? step_quality_flags[frame * steps.size() + index]
            : pair_quality_flags[frame * pairs.size() + index];
    }

    uint16_t quality_at(size_t frame, size_t index) const {
        return parameter_quality(frame, index, metric);
    }

    static const char* quality_status(uint16_t flags, bool available) {
        if (!available) return "Unavailable - excluded from analysis";
        if (flags & Quality_NucleotideWarnings) return "Warning - finite value retained";
        if (flags) return "Quality flags - finite value retained";
        return "Quality: good";
    }

    void draw_quality(uint16_t flags, bool available) const {
        if (!available) ImGui::TextColored(ImVec4(1,0.55f,0.4f,1), "Unavailable - excluded from analysis");
        else if (flags) ImGui::TextColored(ImVec4(1,0.8f,0.3f,1), "%s", quality_status(flags, true));
        else ImGui::TextUnformatted(quality_status(flags, true));
        for (uint16_t bit = 1; bit <= Quality_Last; bit <<= 1) {
            if (flags & bit) ImGui::TextWrapped("%s", quality_description(bit));
        }
    }

    void draw_run_span(const char* label, size_t begin, size_t end, const float* times, bool residence) const {
        if (times) {
            char unit[32];
            md_unit_print(unit, sizeof(unit), app->timeline.time_unit);
            ImGui::Text("%s: %.3f - %.3f %s (%.3f %s)", label, times[begin], times[end], unit,
                times[end]-times[begin], unit);
        } else ImGui::Text("%s: frames %zu - %zu (%zu samples)", label, begin, end, end-begin+1);
        if (residence) ImGui::SetItemTooltip("Observed residence only; boundary-clipped runs may continue outside the analysed interval.");
    }

    void draw_switch_context(const StateSwitch& change, const float* times, size_t frame_count) const {
        draw_run_span("Source", change.source_begin, change.source_end, times, true);
        draw_run_span("Destination", change.begin, change.end, times, true);
        if (change.intervening_samples()) {
            ImGui::TextWrapped("Separated stable states: %zu intervening samples; not an instantaneous switch.",
                change.intervening_samples());
            draw_run_span("Between runs", change.source_end+1, change.begin-1, times, false);
        } else ImGui::TextUnformatted("Adjacent sampled runs (direct at the available sampling resolution)");
        if (change.source_clipped) ImGui::TextDisabled("%s", change.source_begin == 0 ?
            "Source already present at trajectory start; onset unknown." : "Source clipped at detection-range start; onset unknown.");
        if (change.destination_clipped) ImGui::TextWrapped("%s", change.end == frame_count-1 ?
            "Ongoing at trajectory end: observed destination residence is a lower bound." :
            "Ongoing at detection-range end: observed destination residence is a lower bound.");
        ImGui::TextDisabled("Timeline: blue source, amber intervening samples, purple destination.");
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

    // Visible X range of a time plot, so it can pan to keep a jumped-to or playing frame in view.
    struct CursorFollow {
        double min = 0, max = 0, frame = -1;
    };
    CursorFollow count_follow, timeline_follow;

    double cursor_x(const float* times, size_t frame_count) const {
        const double frame = std::clamp(app->animation.frame, 0.0, (double)frame_count - 1);
        const size_t f0 = (size_t)frame;
        const size_t f1 = std::min(f0 + 1, frame_count - 1);
        return times ? times[f0] + ((double)times[f1] - times[f0]) * (frame - (double)f0) : frame;
    }

    // Before BeginPlot: when the frame changed and the cursor left the visible range, recentre without zooming.
    void follow_cursor(CursorFollow& follow, const float* times, size_t frame_count) const {
        const double x = cursor_x(times, frame_count);
        const bool moved = follow.frame != app->animation.frame;
        follow.frame = app->animation.frame;
        const double width = follow.max - follow.min;
        if (!moved || !(width > 0) || (x >= follow.min && x <= follow.max)) return;
        ImPlot::SetNextAxisLimits(ImAxis_X1, x - width * 0.5, x + width * 0.5, ImPlotCond_Always);
    }

    // Inside the plot, after setup: remember the visible range for the next frame.
    static void store_follow(CursorFollow& follow) {
        const ImPlotRect limits = ImPlot::GetPlotLimits();
        follow.min = limits.X.Min;
        follow.max = limits.X.Max;
    }

    void draw_time_cursor(const float* times, size_t frame_count) const {
        const double x = cursor_x(times, frame_count);
        ImPlot::SetNextLineStyle(ImVec4(1, 0.8f, 0, 1), 2);
        ImPlot::PlotInfLines("Current frame", &x, 1);
    }

    std::vector<uint32_t> visible_plot_indices(const PlotSummary& summary, const float* times) const {
        const size_t n = summary.size();
        if (!n) return {};
        const auto limits = ImPlot::GetPlotLimits().X;
        const size_t pixels = (size_t)std::max(1.0f, ImPlot::GetPlotSize().x);
        if (ImPlot::FitThisFrame()) return summary.indices(0, n, pixels);
        size_t first, end;
        if (times) {
            first = (size_t)(std::lower_bound(times, times + n, limits.Min) - times);
            end = (size_t)(std::upper_bound(times, times + n, limits.Max) - times);
        } else {
            first = (size_t)std::clamp(std::floor(limits.Min), 0.0, (double)n);
            end = (size_t)std::clamp(std::ceil(limits.Max) + 1, 0.0, (double)n);
        }
        // Keep neighbouring samples for line segments crossing the visible boundary.
        if (first) --first;
        if (end < n) ++end;
        return summary.indices(first, end, pixels);
    }

    void update_counts(int count, size_t frame_count) {
        initialize_event_times(timeline_times(frame_count), frame_count);
        const uint32_t mask = outlier_parameter_mask();
        uint64_t key = md_hash64(&outlier_sigma, sizeof(outlier_sigma), 0x1A7E);
        key = md_hash64(&statistics_key, sizeof(statistics_key), key);
        key = md_hash64(&statistics_baseline, sizeof(statistics_baseline), key);
        key = md_hash64(&event_parameters, sizeof(event_parameters), key);
        key = md_hash64(&event_min_frames, sizeof(event_min_frames), key);
        key = md_hash64(&event_max_gap, sizeof(event_max_gap), key);
        key = md_hash64(&event_min_time_ps, sizeof(event_min_time_ps), key);
        key = md_hash64(&event_gap_time_ps, sizeof(event_gap_time_ps), key);
        key = md_hash64(&state_min_time_ps, sizeof(state_min_time_ps), key);
        key = md_hash64(&state_min_frames, sizeof(state_min_frames), key);
        key = md_hash64(&event_range_enabled, sizeof(event_range_enabled), key);
        key = md_hash64(&event_range_period, sizeof(event_range_period), key);
        key = md_hash64(&event_period_id, sizeof(event_period_id), key);
        const auto event_period_frames = event_range_enabled && event_range_period
            ? period_frames(event_period_id, frame_count) : std::pair<int, int>{0, 0};
        key = md_hash64(&event_period_frames.first, sizeof(int), key);
        key = md_hash64(&event_period_frames.second, sizeof(int), key);
        key = md_hash64(&event_range_begin_ps, sizeof(event_range_begin_ps), key);
        key = md_hash64(&event_range_end_ps, sizeof(event_range_end_ps), key);
        key = md_hash64(&event_range_first, sizeof(event_range_first), key);
        key = md_hash64(&event_range_last, sizeof(event_range_last), key);
        const float* times = timeline_times(frame_count);
        const bool physical_time = md_unit_base_equal(app->timeline.time_unit, md_unit_second());
        key = md_hash64(&physical_time, sizeof(physical_time), key);
        if (times) {
            key = md_hash64(times, frame_count * sizeof(float), key);
            const double scale = md_unit_scale(app->timeline.time_unit);
            key = md_hash64(&scale, sizeof(scale), key);
        }
        key = md_hash64(&event_neighbourhood, sizeof(event_neighbourhood), key);
        key = md_hash64(&event_whole_structure, sizeof(event_whole_structure), key);
        key = md_hash64(&event_hysteresis, sizeof(event_hysteresis), key);
        key = md_hash64(&event_pair_loss, sizeof(event_pair_loss), key);
        key = md_hash64(&event_hierarchy, sizeof(event_hierarchy), key);
        key = md_hash64(&event_burst_extra, sizeof(event_burst_extra), key);
        key = md_hash64(&event_burst_mode, sizeof(event_burst_mode), key);
        key = md_hash64(&event_residual_sigma, sizeof(event_residual_sigma), key);
        key = md_hash64(&frame_count, sizeof(frame_count), key);
        const uint64_t event_key = key;
        key = md_hash64(&metric, sizeof(metric), key);
        key = md_hash64(&mask, sizeof(mask), key);
        counts_pending = false;
        if (key == counts_key && outlier_counts.size() == frame_count) return;
        // While a slider or field is being edited, keep the previous result and recompute on release.
        if (ImGui::IsAnyItemActive() && outlier_counts.size() == frame_count) {
            counts_pending = true;
            return;
        }
        const bool rebuild_events = event_data_key != event_key;
        if (rebuild_events) inspected_event = SIZE_MAX;
        event_data_key = event_key;
        counts_key = key;
        const int view_cls = metric_cls();
        const float end_sigma = event_end_sigma();
        const double chance = chance_outlier_probability(outlier_sigma, std::max(1, (int)std::bitset<32>(mask).count()));
        outlier_counts.assign(frame_count, {});
        expected_counts.assign(frame_count, 0.0f);
        std::vector<std::vector<MemberOutlier>> frame_outliers(rebuild_events ? frame_count : 0);
        float z[MAX_CLASS_PARAMETERS];
        auto record = [&](size_t f, int cls, size_t m, uint32_t member_mask) {
            uint32_t strong = 0;
            const auto result = assess_member(f, cls, m, member_mask, z, end_sigma, &strong);
            MemberOutlier outlier;
            outlier.member = (uint32_t)m;
            outlier.cls = (uint8_t)cls;
            outlier.lost = cls == Class_Pair && event_pair_loss && !result.valid && pair_lost(f, m);
            if (!result.contributors && !outlier.lost) return;
            outlier.contributors = result.contributors;
            outlier.strong = strong;
            std::copy(z, z + class_parameter_count(cls), outlier.z);
            frame_outliers[f].push_back(outlier);
        };
        for (size_t f = 0; f < frame_count; ++f) {
            for (int m = 0; m < count; ++m) {
                const auto result = assess_member(f, view_cls, (size_t)m, mask);
                if (result.valid) ++outlier_counts[f].valid;
                if (result.contributors) ++outlier_counts[f].outliers;
            }
            expected_counts[f] = (float)(chance * (double)outlier_counts[f].valid);
            if (!rebuild_events) continue;
            for (int cls = 0; cls < Class_Count; ++cls) {
                const uint32_t class_mask = (event_parameters >> class_first_metric(cls)) & class_all_parameters(cls);
                // A lost pair joins events even when no intra-base-pair parameter is tested.
                if (!class_mask && (cls != Class_Pair || !event_pair_loss)) continue;
                const size_t members = class_members(cls);
                for (size_t m = 0; m < members; ++m) {
                    if (class_mask || pair_lost(f, m)) record(f, cls, m, class_mask);
                }
            }
        }
        total_observed = total_expected = 0;
        count_plot_values.resize(frame_count);
        for (size_t f = 0; f < frame_count; ++f) {
            total_observed += outlier_counts[f].outliers;
            total_expected += expected_counts[f];
            count_plot_values[f] = outlier_counts[f].valid ? (float)outlier_counts[f].outliers
                : std::numeric_limits<float>::quiet_NaN();
        }
        if (!count_summary.build(count_plot_values.data(), frame_count) ||
            !expected_summary.build(expected_counts.data(), frame_count))
            MD_LOG_ERROR("Lavery: could not build count plot display summaries");
        if (!rebuild_events) return;
        EventOptions options;
        options.min_frames = (size_t)std::max(1, event_min_frames);
        options.max_gap = (size_t)std::max(0, event_max_gap);
        options.neighbourhood = event_whole_structure ? -1 : std::max(0, event_neighbourhood);
        options.lost_weight = outlier_sigma;
        options.burst_sigma = event_hierarchy ? outlier_sigma + event_burst_extra : 0;
        options.burst_hysteresis = event_hysteresis;
        options.burst_neighbourhood = event_neighbourhood;
        if (event_hierarchy && event_burst_mode == 1) {
            options.residual_sigma = event_residual_sigma;
            options.member_z = [this](size_t f, int cls, uint32_t m, int k) {
                const int id = class_first_metric(cls) + k;
                if (!(event_parameters & (1u << id))) return std::numeric_limits<float>::quiet_NaN();
                return statistics[id].members[m].zscore(values[id][f * columns[id] + m]);
            };
        }
        bool valid_options = !physical_time || times;
        if (times) {
            const double scale = md_unit_scale(app->timeline.time_unit) / md_unit_scale(md_unit_picosecond());
            options.times.resize(frame_count);
            for (size_t f = 0; f < frame_count; ++f) options.times[f] = times[f] * scale;
            options.min_frames = 1;
            options.min_duration = event_min_time_ps;
            options.max_gap_duration = event_gap_time_ps;
            valid_options = std::isfinite(event_min_time_ps) && event_min_time_ps >= 0 &&
                std::isfinite(event_gap_time_ps) && event_gap_time_ps >= 0;
            for (size_t f = 0; f < frame_count; ++f) {
                valid_options &= std::isfinite(options.times[f]) && (!f || options.times[f] > options.times[f - 1]);
            }
            if (event_range_enabled && !event_range_period) {
                valid_options = valid_options && event_time_range(&options, event_range_begin_ps, event_range_end_ps);
            }
        } else if (event_range_enabled && !event_range_period) {
            valid_options &= event_range_first >= 0 && event_range_last >= event_range_first &&
                (size_t)event_range_last < frame_count;
            options.first_frame = (size_t)std::max(0, event_range_first);
            options.last_frame = (size_t)std::max(0, event_range_last);
        }
        if (event_range_enabled && event_range_period) {
            valid_options &= event_period_frames.first >= 0;
            if (valid_options) {
                options.first_frame = (size_t)event_period_frames.first;
                options.last_frame = (size_t)event_period_frames.second;
            }
        }
        if (valid_options) {
            detection_first_frame = options.first_frame;
            detection_last_frame = std::min(frame_count - 1, options.last_frame);
            outlier_events = detect_events(frame_outliers, pairs.size(), steps, options);
            state_switches.clear();
            inspected_switch = SIZE_MAX;
            EventOptions state_options = options;
            state_options.min_duration = options.times.empty() ? 0 : state_min_time_ps < 0 ? event_min_time_ps : state_min_time_ps;
            state_options.min_frames = (size_t)state_min_frames;
            for (int id = (int)Alpha; id <= (int)EpsilonZeta; ++id) {
                if (!(event_parameters & (1u << id)) || !metric_state_count((Metric)id)) continue;
                std::vector<StateSwitch> changes;
                if (!detect_state_switches(&changes, values[id], columns[id], (Metric)id, state_options)) {
                    valid_options = false;
                    break;
                }
                state_switches.insert(state_switches.end(), changes.begin(), changes.end());
            }
            std::stable_sort(state_switches.begin(), state_switches.end(), [](const StateSwitch& a, const StateSwitch& b) {
                return a.begin < b.begin;
            });
            event_detection_error.clear();
        }
        if (!valid_options) {
            outlier_events.clear();
            state_switches.clear();
            event_detection_error = event_range_enabled && event_range_period && event_period_frames.first < 0
                ? "Detection period is missing or covers no samples. Select an existing period; detection is unavailable."
                : "Invalid timestamps, duration/gap or range. Times must increase, limits must be nonnegative, and the range must contain a sample.";
            MD_LOG_ERROR("Lavery: %s", event_detection_error.c_str());
        }
        event_colors = event_color_slots(outlier_events);
        event_expanded.assign(outlier_events.size(), 0);
        event_kinds.clear();
        event_search.clear();
        for (const auto& event : outlier_events) {
            std::string kind = event_kind(event), search = kind;
            for (const auto& m : event.members) {
                const int offset = class_first_metric(m.cls);
                for (int k = 0; k < class_parameter_count(m.cls); ++k) {
                    if (m.parameters & (1u << k)) { search += ' '; search += metric_name((Metric)(offset + k)); }
                }
                if (m.lost_frames) search += " loss lost";
            }
            for (char& c : search) c = (char)tolower((unsigned char)c);
            event_kinds.push_back(std::move(kind));
            event_search.push_back(std::move(search));
        }
        event_order_dirty = true;
        event_order.resize(outlier_events.size());
        for (size_t e = 0; e < event_order.size(); ++e) event_order[e] = (int)e;
        event_nav_target = -1;
        event_follow_target = -1;
    }

    void draw_outlier_timeline(int count, size_t frame_count) {
        const float* times = timeline_times(frame_count);
        char x_label[64];
        timeline_label(x_label, sizeof(x_label), times);
        auto x_at = [&](size_t f) { return times ? (double)times[f] : (double)f; };
        const size_t selected_frame = (size_t)std::clamp((int64_t)std::llround(app->animation.frame),
            (int64_t)0, (int64_t)frame_count - 1);
        const auto& selected_counts = outlier_counts[selected_frame];
        size_t flagged = 0;
        for (int m = 0; m < count; ++m) if (quality_at(selected_frame, (size_t)m)) ++flagged;
        const double observed = total_observed, expected = total_expected;
        if (ImGui::ArrowButton("##prev_outlier", ImGuiDir_Left)) jump_outlier(selected_frame, false);
        ImGui::SetItemTooltip("Previous outlier frame");
        ImGui::SameLine(0, 2);
        if (ImGui::ArrowButton("##next_outlier", ImGuiDir_Right)) jump_outlier(selected_frame, true);
        ImGui::SetItemTooltip("Next outlier frame");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Outliers: %zu / %zu valid", selected_counts.outliers, selected_counts.valid);
        ImGui::SetItemTooltip("Counting %s at %.1f sigma over all %s.\n"
            "Frame %zu: coverage %.1f%%; %zu/%d unavailable (invalid geometry or missing baseline).\n"
            "Selected parameter: %zu quality-flagged members (includes retained warnings).\n"
            "Whole trajectory: %.0f outlying member-frames, ~%.0f expected by chance.\n"
            "Chance level assumes Gaussian, independent parameters (p = %.2g per test);\n"
            "counts near the dashed line are mostly noise.\n"
            "Ctrl-click / Ctrl-drag in the plot: jump / scrub. Shaded bands: events.",
            aggregate_outliers ? "any selected parameter" : metric_name((Metric)metric), outlier_sigma,
            metric_cls() == Class_Step ? "inter-base pairs" : metric_cls() == Class_Nucleotide ? "nucleotides" : "intra-base pairs",
            selected_frame, 100.0 * selected_counts.valid / count, (size_t)count-selected_counts.valid, count, flagged,
            observed, expected, chance_outlier_probability(outlier_sigma, 1));
        if (observed > 0 && observed < 2 * expected) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "(near chance)");
            ImGui::SetItemTooltip("Total outliers (%.0f) are less than twice the chance level (%.0f).", observed, expected);
        }
        size_t grey = 0;
        for (size_t e = 0; e < outlier_events.size() && e < event_colors.size(); ++e) {
            const auto& event = outlier_events[e];
            grey += selected_frame >= event.begin && selected_frame <= event.end && event_colors[e] >= EVENT_PALETTE_SIZE;
        }
        if (grey) {
            ImGui::SameLine();
            ImGui::TextDisabled("+%zu grey", grey);
            ImGui::SetItemTooltip("%zu active events overlap more than %zu coloured ones and are drawn in grey.\n"
                "Colours go to the highest-scoring events first. Use the event filter or Active now to focus.",
                grey, EVENT_PALETTE_SIZE);
        }
        ImGui::PushID(metric);
        ImGui::PushID(x_label);
        follow_cursor(count_follow, times, frame_count);
        if (ImPlot::BeginPlot("##Outliers over time", ImVec2(-1, count_plot_height), ImPlotFlags_NoLegend)) {
            ImPlot::SetupAxes(x_label, "Outliers");
            ImPlot::SetupAxisLimits(ImAxis_X1, x_at(0), std::max(x_at(0) + 1.0, x_at(frame_count - 1)), ImPlotCond_Once);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, count, ImPlotCond_Always);
            struct Payload {
                const std::vector<FrameOutliers>* counts;
                const float* times;
                const uint32_t* indices;
            };
            const auto indices = visible_plot_indices(count_summary, times);
            Payload payload = {&outlier_counts, times, indices.data()};
            ImPlot::PlotLineG("Outliers", [](int index, void* user) {
                const auto& p = *(const Payload*)user;
                const size_t f = p.indices[index];
                const auto& sample = (*p.counts)[f];
                return ImPlotPoint(p.times ? (double)p.times[f] : (double)f,
                    sample.valid ? (double)sample.outliers : std::numeric_limits<double>::quiet_NaN());
            }, &payload, (int)indices.size());
            draw_expected_line(x_at, frame_count);
            draw_event_bands(x_at);
            scrub_timeline("##outlier_scrub", times, frame_count);
            draw_time_cursor(times, frame_count);
            store_follow(count_follow);
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
                    const std::string& kind = event_kinds[e];
                    ImGui::Text("Event #%zu: %s, bp %u-%u, peak %.2f sigma", e + 1, kind.empty() ? "-" : kind.c_str(),
                        event.pair_first + 1, event.pair_last + 1, event.severity);
                }
                ImGui::EndTooltip();
            }
            ImPlot::EndPlot();
        }
        ImGui::PopID();
        ImGui::PopID();
        height_handle("##count_plot_height", &count_plot_height, 180);
    }

    template <typename X>
    void draw_expected_line(X&& x_at, size_t frame_count) const {
        if (expected_counts.size() != frame_count || frame_count < 2) return;
        ImDrawList* draw = ImPlot::GetPlotDrawList();
        const ImU32 color = ImGui::GetColorU32(ImVec4(0.75f, 0.75f, 0.75f, 0.8f));
        constexpr float dash = 6, gap = 4;
        float phase = 0;
        ImPlot::PushPlotClipRect();
        const auto indices = visible_plot_indices(expected_summary, timeline_times(frame_count));
        for (size_t i = 1; i < indices.size(); ++i) {
            const size_t previous = indices[i-1], f = indices[i];
            const ImVec2 a = ImPlot::PlotToPixels(x_at(previous), expected_counts[previous]);
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
            const double begin = x_at(event.begin), end = x_at(event.end);
            if (end < limits.X.Min || begin > limits.X.Max) continue;
            ImVec2 a = ImPlot::PlotToPixels(begin, limits.Y.Max);
            ImVec2 b = ImPlot::PlotToPixels(end, limits.Y.Min);
            if (b.x - a.x < 3) { const float c = 0.5f * (a.x + b.x); a.x = c - 1.5f; b.x = c + 1.5f; }
            const bool current = frame >= event.begin && frame <= event.end;
            ImVec4 tint = event_color(e);
            tint.w = current ? 0.28f : 0.14f;
            draw->AddRectFilled(a, b, ImGui::GetColorU32(tint));
        }
        ImPlot::PopPlotClipRect();
    }

    void draw_state_switches(size_t frame_count) {
        const float* times = timeline_times(frame_count);
        if (times) {
            const float scale = event_time_ns ? 0.001f : 1.0f;
            float residence = (state_min_time_ps < 0 ? event_min_time_ps : state_min_time_ps) * scale;
            if (ImGui::InputFloat(event_time_ns ? "Min residence (ns)" : "Min residence (ps)", &residence)) {
                if (std::isfinite(residence) && residence >= 0) state_min_time_ps = residence / scale;
            }
        } else if (ImGui::InputInt("Min residence (frames)", &state_min_frames)) {
            state_min_frames = std::clamp(state_min_frames, 1, 100000);
        }
        ImGui::SetItemTooltip("Both source and destination must remain in one state for this long.\n"
            "Short excursions are ignored; missing samples reset the source.\n"
            "Uses Event parameters and Detection range, not sigma or gap bridging.");
        ImGui::TextDisabled("%zu switches; angular change and residence, not sigma severity", state_switches.size());
        if (ImGui::BeginTable("##state_switches", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
            for (const char* title : {"Member / parameter", "Transition", "Destination start", "Observed residence", "Change (deg)"}) ImGui::TableSetupColumn(title);
            ImGui::TableHeadersRow();
            const float row_height = 2 * ImGui::GetTextLineHeightWithSpacing() + 2 * ImGui::GetStyle().CellPadding.y;
            ImGuiListClipper clipper;
            clipper.Begin((int)state_switches.size(), row_height);
            while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const size_t i = (size_t)row;
                const auto& change = state_switches[i];
                ImGui::PushID((int)i);
                ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);
                ImGui::TableNextColumn();
                const std::string label = nucleotide_labels[change.member] + " / " + metric_name(change.metric);
                if (ImGui::Selectable(label.c_str(), inspected_switch == i, ImGuiSelectableFlags_SpanAllColumns)) {
                    inspected_event = SIZE_MAX;
                    select_target(Class_Nucleotide, change.member, change.metric - Alpha, change.begin);
                    inspected_switch = i;
                    inspection_frame = app->animation.frame;
                }
                if (ImGui::IsItemHovered()) {
                    preview.push_back({Class_Nucleotide, change.member, 1u << (change.metric - Alpha)});
                    ImGui::BeginTooltip();
                    draw_switch_context(change, times, frame_count);
                    ImGui::EndTooltip();
                }
                ImGui::TableNextColumn();
                ImGui::Text("%s -> %s", metric_state_name(change.metric, change.from), metric_state_name(change.metric, change.to));
                ImGui::TextDisabled("%s", change.intervening_samples() ? "separated runs" : "adjacent runs");
                ImGui::TableNextColumn();
                if (times) ImGui::Text("%.3f", times[change.begin]); else ImGui::Text("%zu", change.begin);
                ImGui::TableNextColumn();
                if (times) ImGui::Text("%.3f", times[change.end]-times[change.begin]);
                else ImGui::Text("%zu frames", change.end-change.begin+1);
                if (change.destination_clipped) ImGui::TextDisabled("%s", change.end == frame_count-1 ? "ongoing at end" : "range-clipped");
                ImGui::TableNextColumn();
                ImGui::Text("%+.1f", change.angular_change);
                ImGui::PopID();
            }
            }
            ImGui::EndTable();
        }
        if (times) {
            char unit[32];
            md_unit_print(unit, sizeof(unit), app->timeline.time_unit);
            ImGui::TextDisabled("Start / residence in %s. Click to inspect at destination onset; hover for source context.", unit);
        }
    }

    void export_state_switches_csv() {
        char path[4096] = {};
        if (!application::file_dialog(path, sizeof(path), application::FileDialogFlag_Save, STR_LIT("csv"))) return;
        md_file_t file = {};
        if (!md_file_open(&file, str_from_cstr(path), MD_FILE_WRITE | MD_FILE_CREATE | MD_FILE_TRUNCATE)) {
            MD_LOG_ERROR("Lavery: could not open state-switch CSV '%s'", path);
            return;
        }
        const float* times = timeline_times(run_num_frames(app));
        char unit[32] = "frame";
        double minimum = state_min_frames;
        if (times) {
            md_unit_print(unit, sizeof(unit), app->timeline.time_unit);
            minimum = (state_min_time_ps < 0 ? event_min_time_ps : state_min_time_ps) *
                md_unit_scale(md_unit_picosecond()) / md_unit_scale(app->timeline.time_unit);
        }
        std::string output = csv_state_switch_header();
        for (const auto& change : state_switches) output += csv_state_switch_row(run, nucleotide_labels[change.member],
            change, times ? times[change.begin] : (double)change.begin, times ? times[change.end] : (double)change.end, unit, minimum,
            times ? times[change.source_begin] : (double)change.source_begin, times ? times[change.source_end] : (double)change.source_end);
        bool ok = md_file_write(file, output.data(), output.size()) == output.size();
        if (!md_file_close(&file)) ok = false;
        if (!ok) MD_LOG_ERROR("Lavery: state-switch CSV write failed; '%s' may be incomplete", path);
        else MD_LOG_INFO("Lavery: exported state switches to '%s'", path);
    }

    void draw_state_history(size_t frame_count) {
        ImGui::Combo("States", &state_history_kind,
            "BI / BII\0anti / syn\0Sugar pucker (North / South)\0Alpha rotamers\0Gamma rotamers\0Pairing chemistry\0");
        const bool chemistry = state_history_kind == 5;
        const Metric kinds[] = {EpsilonZeta, Chi, Phase, Alpha, Gamma};
        const Metric id = chemistry ? Buckle : kinds[state_history_kind];
        const int cls = chemistry ? Class_Pair : Class_Nucleotide;
        const size_t members = class_members(cls);
        if (!members || !frame_count) return;
        const auto& labels = class_labels(cls);
        const auto code = [&](size_t f, size_t m) -> uint8_t {
            return chemistry ? (uint8_t)pairing_states[f*members+m] : (uint8_t)(metric_state(id, values[id][f*members+m])+1);
        };
        if (state_history_cached_kind != state_history_kind) {
            state_history_summaries.clear();
            state_history_summaries.resize(members);
            std::vector<uint8_t> codes(frame_count);
            for (size_t m = 0; m < members; ++m) {
                for (size_t f = 0; f < frame_count; ++f) codes[f] = code(f,m);
                if (!state_history_summaries[m].build(codes)) return;
            }
            state_history_cached_kind = state_history_kind;
        }
        const ImU32 colors[] = {IM_COL32(100,100,100,255), IM_COL32(70,170,220,255), IM_COL32(180,100,230,255),
            IM_COL32(230,175,65,255), IM_COL32(220,80,80,255)};
        const size_t states = chemistry ? Pairing_Count : metric_state_count(id)+1;
        for (size_t s = 0; s < states; ++s) {
            const char* label = chemistry ? pairing_state_name((PairingState)s) : s ? metric_state_name(id, (int)s-1) : "Unavailable";
            if (s) {
                ImGui::SameLine();
                if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(label).x+30) ImGui::NewLine();
            }
            ImGui::ColorButton(("##state_color"+std::to_string(s)).c_str(), ImGui::ColorConvertU32ToFloat4(colors[s]),
                ImGuiColorEditFlags_NoTooltip, ImVec2(10,10));
            ImGui::SameLine();
            ImGui::TextUnformatted(label);
        }
        ImGui::TextWrapped("Raw states for every member. White = multiple states within a display bin; zoom to resolve. Residence filters do not repaint this view.");
        ImGui::TextDisabled("Click: select member and time. Drag: pan. Wheel: zoom. Hover: exact raw sample.");
        const float* times = timeline_times(frame_count);
        char unit[32] = "Frame";
        if (times) md_unit_print(unit, sizeof(unit), app->timeline.time_unit);
        ImGui::PushID(state_history_kind);
        if (ImPlot::BeginPlot("##state_history", ImVec2(-1, -1), ImPlotFlags_NoLegend)) {
            ImPlot::SetupAxes(unit, chemistry ? "Pair" : "Nucleotide");
            const double first = (times ? times[0] : 0) - (times && frame_count > 1 ? (times[1]-times[0])*.5 : .5);
            const double last = (times ? times[frame_count-1] : (double)frame_count-1) +
                (times && frame_count > 1 ? (times[frame_count-1]-times[frame_count-2])*.5 : .5);
            ImPlot::SetupAxesLimits(first, std::max(first+1,last), -.5, members-.5, ImPlotCond_Once);
            ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, -.5, members-.5);
            ImPlot::SetupAxisFormat(ImAxis_Y1, [](double v, char* buf, int size, void* user) {
                const auto& self = *(const Lavery*)user;
                const auto& names = self.class_labels(self.state_history_kind == 5 ? Class_Pair : Class_Nucleotide);
                const int m = (int)std::llround(v);
                return m >= 0 && m < (int)names.size() && std::fabs(v-m) < .01 ? snprintf(buf,size,"%s",names[m].c_str()) : snprintf(buf,size," ");
            }, this);
            const auto limits = ImPlot::GetPlotLimits();
            if (ImPlot::FitThisFrame()) {
                ImPlot::FitPoint(ImPlotPoint(first,-.5));
                ImPlot::FitPoint(ImPlotPoint(last,members-.5));
            }
            const size_t pixels = (size_t)std::max(1.0f, ImPlot::GetPlotSize().x);
            struct Bin { double begin, end; size_t first, end_frame; };
            std::vector<Bin> bins;
            for (size_t x = 0; x < pixels; ++x) {
                const double a = std::max(first,limits.X.Min+limits.X.Size()*x/pixels);
                const double b = std::min(last,limits.X.Min+limits.X.Size()*(x+1)/pixels);
                if (b < a) continue;
                bins.push_back({a,b,nearest_frame(a,times,frame_count),nearest_frame(b,times,frame_count)+1});
            }
            const int low = (int)std::clamp(std::floor(limits.Y.Min+.5), 0.0, (double)members);
            const int high = (int)std::clamp(std::ceil(limits.Y.Max+.5), (double)low, (double)members);
            ImPlot::PushPlotClipRect();
            for (int m = low; m < high; ++m) {
                size_t start = 0;
                while (start < bins.size()) {
                    const uint32_t mask = state_history_summaries[m].mask(bins[start].first,bins[start].end_frame);
                    size_t stop = start+1;
                    while (stop < bins.size() && state_history_summaries[m].mask(bins[stop].first,bins[stop].end_frame) == mask) ++stop;
                    ImU32 color = IM_COL32(225,225,225,255);
                    if (mask && !(mask&(mask-1))) for (size_t s = 0; s < states; ++s) if (mask == (1u<<s)) color = colors[s];
                    ImPlot::GetPlotDrawList()->AddRectFilled(ImPlot::PlotToPixels(bins[start].begin,m+.45),
                        ImPlot::PlotToPixels(bins[stop-1].end,m-.45),color);
                    start = stop;
                }
            }
            ImPlot::PopPlotClipRect();
            draw_time_cursor(times,frame_count);
            if (metric_cls() == cls) {
                const double selected = member;
                ImPlot::PlotInfLines("Selected member", &selected, 1, ImPlotInfLinesFlags_Horizontal);
            }
            if (ImPlot::IsPlotHovered()) {
                const auto pos = ImPlot::GetPlotMousePos();
                const int m = (int)std::floor(pos.y+.5);
                if (m >= 0 && m < (int)members && pos.x >= first && pos.x <= last) {
                    const size_t f = nearest_frame(pos.x,times,frame_count);
                    const uint8_t s = code(f,m);
                    ImGui::SetTooltip("%s\nFrame %zu: %s",labels[m].c_str(),f,
                        chemistry ? pairing_state_name((PairingState)s) : s ? metric_state_name(id,s-1) : "Unavailable");
                    preview.push_back({(uint8_t)cls,(uint32_t)m,chemistry ? class_all_parameters(cls) : (1u<<(id-Alpha))});
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        inspected_event = inspected_switch = SIZE_MAX;
                        select_target(cls,(uint32_t)m,chemistry ? Buckle : id-Alpha,f);
                    }
                }
            }
            ImPlot::EndPlot();
        }
        ImGui::PopID();
    }

    void export_pairing_csv(size_t pair, const float* times) {
        char path[4096] = {};
        if (!application::file_dialog(path, sizeof(path), application::FileDialogFlag_Save, STR_LIT("csv"))) return;
        md_file_t file = {};
        if (!md_file_open(&file, str_from_cstr(path), MD_FILE_WRITE | MD_FILE_CREATE | MD_FILE_TRUNCATE)) {
            MD_LOG_ERROR("Lavery: could not open pairing-state CSV '%s'", path);
            return;
        }
        char unit[32] = "frame";
        if (times) md_unit_print(unit, sizeof(unit), app->timeline.time_unit);
        std::string output = csv_pairing_header();
        const double minimum = times ? pairing_min_time_ps * md_unit_scale(md_unit_picosecond()) /
            md_unit_scale(app->timeline.time_unit) : pairing_min_frames;
        for (const auto& span : pairing_history) {
            const double begin = times ? times[span.begin] : (double)span.begin;
            const double end = times ? times[span.end] : (double)span.end;
            output += csv_pairing_run(run, pair_labels[pair], pair, span, begin, end, unit, minimum, times != nullptr);
        }
        bool ok = md_file_write(file, output.data(), output.size()) == output.size();
        if (!md_file_close(&file)) ok = false;
        if (!ok) MD_LOG_ERROR("Lavery: pairing-state CSV write failed; '%s' may be incomplete", path);
        else MD_LOG_INFO("Lavery: exported pairing states to '%s'", path);
    }

    void draw_pairing_states(size_t frame_count) {
        if (pairs.empty() || pairing_states.size() != frame_count*pairs.size()) return;
        size_t p = metric_cls() == Class_Step ? steps[member].a :
            metric_cls() == Class_Nucleotide ? (size_t)member/2 : (size_t)member;
        if (ImGui::BeginCombo("Pair", pair_labels[p].c_str())) {
            for (size_t i = 0; i < pairs.size(); ++i) if (ImGui::Selectable(pair_labels[i].c_str(), i == p)) {
                p = i;
                inspected_event = inspected_switch = SIZE_MAX;
                select_target(Class_Pair, p, Buckle, app->animation.frame);
            }
            ImGui::EndCombo();
        }
        if (p != pairing_cached_member) {
            if (!pairing_runs(&pairing_history, pairing_states, pairs.size(), p)) return;
            pairing_cached_member = p;
            pairing_changes_key = 0;
        }
        const float* times = timeline_times(frame_count);
        const size_t frame = (size_t)std::clamp((int64_t)std::llround(app->animation.frame), (int64_t)0, (int64_t)frame_count-1);
        ImGui::Text("Current: %s", pairing_state_name(pairing_states[frame*pairs.size()+p]));
        ImGui::TextWrapped("Explicit-H annotation, independent of sigma and pair-loss validity. WC: A-T/U or G-C; Hoogsteen: A-T/U only. No H / missing evidence: unavailable.");
        ImGui::SetItemTooltip("Requires all expected directional H bonds and compatible base geometry.\n"
            "WC: both chi in [-180,-60] deg; Hoogsteen: adenine chi [0,120], partner anti.\n"
            "Other includes partial contacts or uncertain orientation. Unpaired requires disrupted geometry and no candidate contacts.\n"
            "G-C Hoogsteen and other noncanonical chemistries are not identified.");
        if (times) {
            const float scale = event_time_ns ? .001f : 1;
            float duration = pairing_min_time_ps*scale;
            if (ImGui::InputFloat(event_time_ns ? "Pairing min residence (ns)" : "Pairing min residence (ps)", &duration)) {
                if (std::isfinite(duration) && duration >= 0) pairing_min_time_ps = duration/scale;
                else MD_LOG_ERROR("Lavery: pairing residence must be finite and nonnegative");
            }
        } else if (ImGui::InputInt("Pairing min residence (frames)", &pairing_min_frames)) {
            if (pairing_min_frames < 1) {
                MD_LOG_ERROR("Lavery: pairing residence must be positive");
                pairing_min_frames = 1;
            }
        }
        ImGui::SetItemTooltip("Both source and destination must qualify. Short runs are skipped, not relabelled.\n"
            "Unavailable resets the source. Uses the whole run, independent of Event parameters and Detection range.");
        uint64_t key = md_hash64(&pairing_min_time_ps, sizeof(pairing_min_time_ps), p+1);
        key = md_hash64(&pairing_min_frames, sizeof(pairing_min_frames), key);
        key = md_hash64(&frame_count, sizeof(frame_count), key);
        if (key != pairing_changes_key) {
            EventOptions options;
            options.min_frames = (size_t)pairing_min_frames;
            if (times) {
                const double to_ps = md_unit_scale(app->timeline.time_unit)/md_unit_scale(md_unit_picosecond());
                options.times.resize(frame_count);
                for (size_t f = 0; f < frame_count; ++f) options.times[f] = times[f]*to_ps;
                options.min_duration = pairing_min_time_ps;
            }
            if (!pairing_transitions(&pairing_changes, pairing_history, options)) return;
            pairing_changes_key = key;
        }
        if (ImGui::Button("Export pairing CSV")) export_pairing_csv(p, times);
        ImGui::SameLine();
        ImGui::TextDisabled("%zu transitions", pairing_changes.size());
        if (ImPlot::BeginPlot("##pairing_timeline", ImVec2(-1, 130), ImPlotFlags_NoLegend)) {
            char unit[32] = "Frame";
            if (times) md_unit_print(unit, sizeof(unit), app->timeline.time_unit);
            ImPlot::SetupAxes(unit, nullptr, ImPlotAxisFlags_None, ImPlotAxisFlags_NoDecorations);
            const double first = (times ? times[0] : 0) - (times && frame_count > 1 ? (times[1]-times[0])*.5 : .5);
            const double last = (times ? times[frame_count-1] : (double)frame_count-1) +
                (times && frame_count > 1 ? (times[frame_count-1]-times[frame_count-2])*.5 : .5);
            ImPlot::SetupAxisLimits(ImAxis_X1, first, std::max(first+1, last), ImPlotCond_Once);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1, ImPlotCond_Always);
            const auto limits = ImPlot::GetPlotLimits();
            if (ImPlot::FitThisFrame()) {
                ImPlot::FitPoint(ImPlotPoint(first,0));
                ImPlot::FitPoint(ImPlotPoint(last,1));
            }
            const size_t pixels = (size_t)std::max(1.0f, ImPlot::GetPlotSize().x);
            const ImU32 colors[] = {IM_COL32(100,100,100,220), IM_COL32(70,170,220,220), IM_COL32(180,100,230,220),
                IM_COL32(230,175,65,220), IM_COL32(220,80,80,220)};
            auto run_at = [&](size_t f) {
                return (size_t)(std::upper_bound(pairing_history.begin(), pairing_history.end(), f,
                    [](size_t v, const PairingRun& r) { return v < r.begin; })-pairing_history.begin()-1);
            };
            ImPlot::PushPlotClipRect();
            for (size_t x = 0; x < pixels; ++x) {
                const double a = limits.X.Min+limits.X.Size()*x/pixels;
                const double b = limits.X.Min+limits.X.Size()*(x+1)/pixels;
                if (b < first || a > last) continue;
                const auto left = run_at(nearest_frame(std::max(first,a), times, frame_count));
                const auto right = run_at(nearest_frame(std::min(last,b), times, frame_count));
                const ImU32 color = left == right ? colors[pairing_history[left].state] : IM_COL32(200,200,200,220);
                ImPlot::GetPlotDrawList()->AddRectFilled(ImPlot::PlotToPixels(std::max(a,first), .85),
                    ImPlot::PlotToPixels(std::min(b,last), .15), color);
            }
            ImPlot::PopPlotClipRect();
            scrub_timeline("##pairing_scrub", times, frame_count);
            draw_time_cursor(times, frame_count);
            if (ImPlot::IsPlotHovered()) {
                const size_t f = nearest_frame(ImPlot::GetPlotMousePos().x, times, frame_count);
                ImGui::SetTooltip("Frame %zu: %s\nWhite bins contain multiple raw runs.", f,
                    pairing_state_name(pairing_states[f*pairs.size()+p]));
            }
            ImPlot::EndPlot();
        }
        ImGui::TextWrapped("Blue: WC | Purple: HG | Amber: other | Red: unpaired | Grey: unavailable | White: mixed");
        if (ImGui::BeginTable("##pairing_changes", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
            for (const char* label : {"Transition", "Destination start", "Residence", "Intervening samples"}) ImGui::TableSetupColumn(label);
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin((int)pairing_changes.size());
            while (clipper.Step()) for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto& change = pairing_changes[i];
                const auto& dest = change.destination;
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const std::string label = std::string(pairing_state_name(change.source.state))+" -> "+pairing_state_name(dest.state);
                if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                    inspected_event = inspected_switch = SIZE_MAX;
                    select_target(Class_Pair, p, Buckle, dest.begin);
                }
                if (ImGui::IsItemHovered()) preview.push_back({Class_Pair, (uint32_t)p, class_all_parameters(Class_Pair)});
                ImGui::TableNextColumn();
                if (times) ImGui::Text("%.3f", times[dest.begin]); else ImGui::Text("%zu", dest.begin);
                ImGui::TableNextColumn();
                if (times) ImGui::Text("%.3f", times[dest.end]-times[dest.begin]); else ImGui::Text("%zu", dest.end-dest.begin+1);
                ImGui::TableNextColumn();
                ImGui::Text("%zu", dest.begin-change.source.end-1);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    void draw_events_quality(int count, size_t frame_count) {
        const size_t frame = (size_t)std::clamp((int64_t)std::llround(app->animation.frame), (int64_t)0, (int64_t)frame_count - 1);
        const auto& labels = class_labels(metric_cls());
        if (!ImGui::BeginTabBar("EventsQuality")) return;
        if (ImGui::BeginTabItem(statistics_reference_scope == Reference_HelixHistory
                ? "Anomalous regions###outlier_events" : "Outlier events###outlier_events")) {
            draw_events(frame, frame_count);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("State switches")) {
            draw_state_switches(frame_count);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("State history")) {
            draw_state_history(frame_count);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Pairing states")) {
            draw_pairing_states(frame_count);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Pairing & coverage")) {
            ImGui::Text("Frame %zu | %zu / %d members have data", frame, outlier_counts[frame].valid, count);
            ImGui::TextDisabled("Explicit H: >=1 canonical H-bond (D-A <=3.5 A, H-D-A <=30 deg). No H: >=2 heavy contacts <=3.5 A.");
            if (metric_cls() == Class_Nucleotide)
                ImGui::TextDisabled("Quality is for %s. Warnings retain finite values; contacts describe the parent pair.",
                    metric_name((Metric)metric));
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
                        selected_detail_active = true;
                        focus_heatmap_selection = true;
                    }
                    ImGui::TableNextColumn();
                    auto contact = [&](size_t p) {
                        ImGui::Text("%u %s", (unsigned)pair_contacts[frame * pairs.size() + p],
                            pair_uses_hydrogens[p] ? "H-bonds" : "heavy-atom proxy");
                    };
                    if (metric_cls() == Class_Step) {
                        contact(steps[m].a);
                        ImGui::SameLine();
                        contact(steps[m].b);
                    } else contact(metric_cls() == Class_Nucleotide ? (size_t)m / 2 : (size_t)m);
                    ImGui::TableNextColumn();
                    draw_quality(quality_at(frame, (size_t)m), std::isfinite(values[metric][frame*columns[metric]+m]));
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

    static constexpr size_t EVENT_PALETTE_SIZE = EVENT_COLOR_COUNT;

    ImVec4 event_color(size_t event) const {
        const size_t slot = event < event_colors.size() ? event_colors[event] : SIZE_MAX;
        return slot < EVENT_PALETTE_SIZE
            ? ImPlot::SampleColormap((float)slot / (EVENT_PALETTE_SIZE - 1), ImPlotColormap_Viridis)
            : ImVec4(0.62f, 0.62f, 0.62f, 1);
    }

    void draw_events(size_t frame, size_t frame_count) {
        if (!event_detection_error.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.35f, 1), "%s", event_detection_error.c_str());
        const float* times = timeline_times(frame_count);
        char unit[64] = "frames";
        if (times) md_unit_print(unit, sizeof(unit), app->timeline.time_unit);

        const size_t children = (size_t)std::count_if(outlier_events.begin(), outlier_events.end(),
            [](const OutlierEvent& event) { return event.parent != SIZE_MAX; });
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%zu %s", outlier_events.size() - children,
            statistics_reference_scope == Reference_HelixHistory ? "anomalous regions" : "events");
        if (children) { ImGui::SameLine(); ImGui::TextDisabled("+ %zu local", children); }
        if (counts_pending) {
            ImGui::SameLine();
            ImGui::TextDisabled("(updating on release)");
        }
        ImGui::SetItemTooltip("Start %.1f sigma, continue %.1f sigma, %s grouping.\n"
            "Click a row: inspect that event at its peak. Play or change frame: all active events.\n"
            "Right-click a row: select atoms or add it to the main timeline. Hover: details.",
            outlier_sigma, event_end_sigma(), event_whole_structure ? "whole-structure" : "local");
        if (statistics_reference_scope == Reference_HelixHistory)
            ImGui::TextDisabled("Persistent regions need not represent a change during this recording.");
        ImGui::SameLine();
        ImGui::Checkbox("Active now", &events_active_only);
        ImGui::SetItemTooltip("Show only events spanning the current frame.");
        ImGui::SameLine();
        ImGui::Checkbox("Follow playback", &events_follow_playback);
        ImGui::SetItemTooltip("During playback, scroll to the most local active sub-event, or an active parent.\n"
            "All active rows remain highlighted. Filters and table sorting are respected.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        ImGui::InputTextWithHint("##event_filter", "filter", event_filter, sizeof(event_filter));
        ImGui::SetItemTooltip("Type text (e.g. roll, unwinding, loss) and/or base pairs:\n"
            "\"7\" keeps events touching bp 7, \"5-9\" events overlapping bp 5-9.\n"
            "Space-separated terms must all match. Up/Down in the table: previous/next event.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Timeline...")) ImGui::OpenPopup("##timeline_menu");
        if (ImGui::BeginPopup("##timeline_menu")) {
            if (ImGui::MenuItem("Add all shown events")) {
                for (int e : event_order) if (event_shown((size_t)e, frame) && !add_event_period((size_t)e)) break;
            }
            ImGui::SetItemTooltip("In table order, respecting filters; VIAMD holds up to %d periods.", TIMELINE_MAX_PERIODS);
            if (ImGui::MenuItem("Remove added events", nullptr, false, !timeline_period_ids.empty())) remove_event_periods();
            ImGui::EndPopup();
        }
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
        if (times) {
            const float scale = event_time_ns ? 0.001f : 1.0f;
            float maximum = events_max_shown_time_ps * scale;
            if (ImGui::InputFloat(event_time_ns ? "Max shown duration (ns)" : "Max shown duration (ps)",
                    &maximum, 0, 0, "%.6g")) {
                if (std::isfinite(maximum) && maximum >= 0 && std::isfinite(maximum / scale))
                    events_max_shown_time_ps = maximum / scale;
                else MD_LOG_ERROR("Lavery: maximum shown duration must be finite and nonnegative");
            }
        } else {
            int maximum = events_max_shown_frames;
            if (ImGui::InputInt("Max shown duration (frames)", &maximum, 0, 0)) {
                if (maximum >= 0) events_max_shown_frames = maximum;
                else MD_LOG_ERROR("Lavery: maximum shown duration must be nonnegative");
            }
        }
        ImGui::SetItemTooltip("0 = Unlimited. Show spans no longer than this limit, including bridged gaps.\n"
            "Short sub-events remain visible even when their parent is hidden.\n"
            "Viewing only: detection, count plots, event bands and viewport feedback are unchanged.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Unlimited##max_duration")) {
            events_max_shown_time_ps = 0;
            events_max_shown_frames = 0;
        }
        if (inspected_event < outlier_events.size()) {
            const size_t selected = inspected_event;
            const size_t parent = outlier_events[selected].parent;
            if (!event_shown(selected, frame))
                ImGui::TextDisabled("The inspected event is hidden by the table filters; inspection is retained.");
            if (parent != SIZE_MAX) ImGui::Text("Selected: sub-event #%zu | parent #%zu", selected+1, parent+1);
            else ImGui::Text("Selected: event #%zu", selected+1);
            ImGui::TextUnformatted("Viewport:");
            ImGui::SameLine();
            char selected_label[64];
            snprintf(selected_label, sizeof(selected_label), parent != SIZE_MAX ? "Sub-event #%zu" : "Event #%zu", selected+1);
            ImGui::RadioButton(selected_label, &inspection_scope, Inspection_Selected);
            if (parent != SIZE_MAX) {
                ImGui::SameLine();
                char parent_label[64];
                snprintf(parent_label, sizeof(parent_label), "Parent #%zu", parent+1);
                ImGui::RadioButton(parent_label, &inspection_scope, Inspection_Parent);
            }
            ImGui::SameLine();
            ImGui::RadioButton("All active", &inspection_scope, Inspection_All);
            ImGui::SetItemTooltip("Changes viewport scope without losing the selected event or its inspector data.\n"
                "Playback or changing frame ends inspection and restores automatic feedback.");
        }
        if (!ImGui::BeginTable("Events", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Sortable | ImGuiTableFlags_Hideable)) return;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Event", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide, 1.4f);
        ImGui::TableSetupColumn("Region", ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        char duration_header[80];
        snprintf(duration_header, sizeof(duration_header), "Duration (%s)###duration", unit);
        ImGui::TableSetupColumn(duration_header, ImGuiTableColumnFlags_WidthStretch, 0.7f);
        ImGui::TableSetupColumn("Peak frame", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultHide, 0.5f);
        ImGui::TableSetupColumn("Peak sigma", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_PreferSortDescending, 0.5f);
        ImGui::TableSetupColumn("Score", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort |
            ImGuiTableColumnFlags_PreferSortDescending, 0.5f);
        ImGui::TableHeadersRow();
        sort_events(ImGui::TableGetSortSpecs(), times);
        navigate_events(frame);

        int follow_target = -1;
        if (events_follow_playback && app->animation.mode == PlaybackMode::Playing) {
            for (int e : event_order) {
                const auto& candidate = outlier_events[(size_t)e];
                if (!event_shown((size_t)e, frame) || frame < candidate.begin || frame > candidate.end) continue;
                if (follow_target >= 0) {
                    const auto& previous = outlier_events[(size_t)follow_target];
                    const bool child = candidate.parent != SIZE_MAX, previous_child = previous.parent != SIZE_MAX;
                    if (child != previous_child) {
                        if (!child) continue;
                    } else {
                        const auto span = candidate.pair_last-candidate.pair_first;
                        const auto previous_span = previous.pair_last-previous.pair_first;
                        if (span > previous_span || (span == previous_span && candidate.score <= previous.score)) continue;
                    }
                }
                follow_target = e;
            }
        }
        const bool follow_changed = follow_target >= 0 && follow_target != event_follow_target;
        event_follow_target = follow_target;
        struct TableRow { int event; int member; };
        std::vector<TableRow> rows;
        int target_row = -1;
        for (int e : event_order) {
            if (!event_shown((size_t)e, frame)) continue;
            if (event_nav_target == e || (follow_changed && follow_target == e)) target_row = (int)rows.size();
            rows.push_back({e, -1});
            if (event_expanded[e])
                for (size_t m = 0; m < outlier_events[e].members.size(); ++m) rows.push_back({e, (int)m});
        }
        const size_t shown_events = (size_t)std::count_if(rows.begin(), rows.end(),
            [](const TableRow& row) { return row.member < 0; });
        if (event_nav_target >= 0 && ((size_t)event_nav_target >= outlier_events.size() ||
            !event_shown((size_t)event_nav_target, frame))) event_nav_target = -1;
        ImGuiListClipper clipper;
        clipper.Begin((int)rows.size(), ImGui::GetTextLineHeight() + 2 * ImGui::GetStyle().CellPadding.y);
        if (target_row >= 0) clipper.IncludeItemByIndex(target_row);
        while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const int e = rows[row].event;
            const auto& event = outlier_events[(size_t)e];
            ImGui::PushID(e);
            if (rows[row].member >= 0) {
                draw_event_member(event.members[(size_t)rows[row].member], (size_t)e);
                ImGui::PopID();
                continue;
            }
            ImGui::TableNextRow();
            if (event_nav_target == e) {
                ImGui::SetScrollHereY(0.5f);
                event_nav_target = -1;
            }
            ImGui::TableNextColumn();
            const ImVec2 swatch = ImGui::GetCursorScreenPos();
            const float side = ImGui::GetFontSize() * 0.65f;
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(swatch.x, swatch.y + 3), ImVec2(swatch.x + side, swatch.y + 3 + side),
                ImGui::GetColorU32(event_color((size_t)e)), 2.0f);
            ImGui::Dummy(ImVec2(side, ImGui::GetTextLineHeight()));
            ImGui::SameLine();
            char label[64];
            if (event.parent != SIZE_MAX) snprintf(label, sizeof(label), "#%d  in #%zu", e + 1, event.parent + 1);
            else if (times) snprintf(label, sizeof(label), "#%d  %.4g-%.4g", e + 1, (double)times[event.begin], (double)times[event.end]);
            else snprintf(label, sizeof(label), "#%d  %zu-%zu", e + 1, event.begin, event.end);
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_OpenOnArrow |
                ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (inspected_event != SIZE_MAX ? inspected_event == (size_t)e : event_visible((size_t)e, frame))
                flags |= ImGuiTreeNodeFlags_Selected;
            if (event.members.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
            ImGui::SetNextItemOpen(event_expanded[e] != 0, ImGuiCond_Always);
            const bool open = ImGui::TreeNodeEx(label, flags);
            event_expanded[e] = open;
            if (follow_changed && follow_target == e) ImGui::SetScrollHereY(0.5f);
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                inspect_event((size_t)e, event.peak);
            }
            const std::string& kind = event_kinds[(size_t)e];
            if (ImGui::IsItemHovered()) {
                for (const auto& m : event.members) preview.push_back({m.cls, m.member, m.parameters});
                char hierarchy[256] = {};
                if (event.parent != SIZE_MAX && event.residual) {
                    const auto& parent = outlier_events[event.parent];
                    const uint32_t region = parent.pair_last - parent.pair_first + 1;
                    snprintf(hierarchy, sizeof(hierarchy),
                        "\nLocal sub-event inside #%zu: stands out from its background by %.2f sigma (continue %.2f)%s",
                        event.parent + 1, event.burst_sigma, std::max(0.5f, event.burst_sigma - event_hysteresis),
                        region < 10 ? "\nParent region is small (<10 base pairs): the background median is noisy." : "");
                } else if (event.parent != SIZE_MAX) snprintf(hierarchy, sizeof(hierarchy),
                    "\nLocal burst inside #%zu; start %.2f sigma, continue %.2f sigma",
                    event.parent + 1, event.burst_sigma, std::max(0.5f, event.burst_sigma - event_hysteresis));
                ImGui::BeginTooltip();
                ImGui::Text("Event #%d: %s\nBase pairs %u-%u, frames %zu-%zu (%zu outlying)\nPeak %.2f sigma at frame %zu; "
                    "up to %zu members outlying at once\nScore %.1f (sum of |z| over member-frames)%s%s%s",
                    e + 1, kind.empty() ? "-" : kind.c_str(), event.pair_first + 1, event.pair_last + 1, event.begin, event.end,
                    event.outlier_frames, event.severity, event.peak, event.peak_count, event.score,
                    event.lost_samples ? "\nIncludes frames where a base pair was lost" : "", hierarchy,
                    event.truncated_begin || event.truncated_end ? "\nTouches detection boundary: potentially truncated." : "");
                draw_event_footprint(event, (size_t)e, frame);
                ImGui::EndTooltip();
            }
            if (ImGui::BeginPopupContextItem("##event_menu")) {
                size_t counts[Class_Count] = {};
                for (const auto& m : event.members) ++counts[std::min<int>(m.cls, Class_Count - 1)];
                ImGui::TextDisabled("Event #%d: %zu base pairs, %zu steps, %zu nucleotides", e + 1,
                    counts[Class_Pair], counts[Class_Step], counts[Class_Nucleotide]);
                if (ImGui::MenuItem("Inspect at peak")) inspect_event((size_t)e, event.peak);
                if (ImGui::MenuItem("Select atoms")) select_event_residues(event);
                ImGui::SetItemTooltip("Replace the VIAMD selection with the nucleotides of this event's members.");
                if (ImGui::MenuItem("Add to timeline")) add_event_period((size_t)e);
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            const bool clipped = event.truncated_begin || event.truncated_end;
            if (event.pair_first == event.pair_last) ImGui::Text("bp %u%s", event.pair_first + 1, clipped ? "*" : "");
            else ImGui::Text("bp %u-%u%s", event.pair_first + 1, event.pair_last + 1, clipped ? "*" : "");
            if (clipped) ImGui::SetItemTooltip("* Touches the detection boundary: potentially truncated.");
            ImGui::TableNextColumn();
            const int dominant = event_dominant_metric(event);
            if (dominant >= 0) ImGui::TextColored(sign_color(event.metric_signed[dominant], 1), "%s", kind.c_str());
            else ImGui::TextUnformatted(kind.c_str());
            ImGui::TableNextColumn();
            if (times) ImGui::Text("%.4g", (double)times[event.end] - times[event.begin]);
            else ImGui::Text("%zu", event.end - event.begin + 1);
            ImGui::SetItemTooltip("%zu contributing %s (bridged gaps excluded).\n"
                "Elapsed time is last time minus first time; a single sample has zero elapsed time.",
                event.outlier_frames, event.outlier_frames == 1 ? "frame" : "frames");
            ImGui::TableNextColumn(); ImGui::Text("%zu", event.peak);
            ImGui::TableNextColumn(); ImGui::Text("%.2f", event.severity);
            ImGui::TableNextColumn(); ImGui::Text("%.1f", event.score);
            ImGui::PopID();
        }
        }
        if (rows.empty() && !outlier_events.empty()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("No event matches the filter%s.", events_active_only ? " at this frame" : "");
        }
        ImGui::EndTable();
        ImGui::TextDisabled("%zu / %zu shown (including sub-events)", shown_events, outlier_events.size());
    }

    bool event_matches_filter(size_t index) const {
        const OutlierEvent& event = outlier_events[index];
        const char* p = event_filter;
        while (*p) {
            while (*p == ' ') ++p;
            const char* start = p;
            while (*p && *p != ' ') ++p;
            if (p == start) break;
            const std::string term(start, p);
            unsigned lo = 0, hi = 0;
            char tail = 0;
            const int n = sscanf(term.c_str(), "%u-%u%c", &lo, &hi, &tail);
            if (n == 2 || (n == 1 && sscanf(term.c_str(), "%u%c", &lo, &tail) == 1)) {
                if (n == 1) hi = lo;
                if (lo > hi) std::swap(lo, hi);
                if (hi < event.pair_first + 1 || lo > event.pair_last + 1) return false;
                continue;
            }
            std::string lower = term;
            for (char& c : lower) c = (char)tolower((unsigned char)c);
            if (event_search[index].find(lower) == std::string::npos) return false;
        }
        return true;
    }

    bool event_shown(size_t index, size_t frame) const {
        const auto& event = outlier_events[index];
        const size_t frames = run_num_frames(app);
        const float* times = timeline_times(frames);
        const double maximum = times ? events_max_shown_time_ps *
            (md_unit_scale(md_unit_picosecond()) / md_unit_scale(app->timeline.time_unit)) : events_max_shown_frames;
        if (maximum > 0 && !event_within_duration(event.begin, event.end, times, frames, maximum)) return false;
        if (events_active_only && (frame < event.begin || frame > event.end)) return false;
        return event_matches_filter(index);
    }

    // Up/Down while the events table has focus: inspect the previous/next shown event in table order.
    void navigate_events(size_t frame) {
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) || ImGui::GetIO().WantTextInput || event_order.empty()) return;
        const ImGuiID owner = ImGui::GetID("##event_keys");
        ImGui::SetKeyOwner(ImGuiKey_UpArrow, owner);
        ImGui::SetKeyOwner(ImGuiKey_DownArrow, owner);
        const bool up = ImGui::IsKeyPressed(ImGuiKey_UpArrow, ImGuiInputFlags_Repeat, owner);
        const bool down = ImGui::IsKeyPressed(ImGuiKey_DownArrow, ImGuiInputFlags_Repeat, owner);
        if (up == down) return;
        std::vector<int> shown;
        for (int e : event_order) if (event_shown((size_t)e, frame)) shown.push_back(e);
        if (shown.empty()) return;
        auto it = std::find(shown.begin(), shown.end(), (int)inspected_event);
        int next;
        if (it == shown.end()) next = down ? shown.front() : shown.back();
        else if (down) next = it + 1 == shown.end() ? *it : *(it + 1);
        else next = it == shown.begin() ? *it : *(it - 1);
        inspect_event((size_t)next, outlier_events[(size_t)next].peak);
        event_nav_target = next;
    }

    // Tooltip mini-map: members (rows, by base-pair position) against the event's frames.
    void draw_event_footprint(const OutlierEvent& event, size_t index, size_t frame) {
        if (event.members.empty()) return;
        const size_t span = event.end - event.begin + 1;
        const float width = ImGui::GetFontSize() * 16;
        const float row = std::clamp(ImGui::GetFontSize() * 6 / (float)event.members.size(), 2.0f, 8.0f);
        const float height = row * (float)event.members.size();
        ImGui::Spacing();
        ImGui::TextDisabled("Footprint: members (rows) x frames %zu-%zu", event.begin, event.end);
        const ImVec2 o = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(width, height));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(o, ImVec2(o.x + width, o.y + height), ImGui::GetColorU32(ImGuiCol_FrameBg));
        std::vector<size_t> rows(event.members.size());
        for (size_t i = 0; i < rows.size(); ++i) rows[i] = i;
        auto position = [&](const EventMember& m) {
            if (m.cls == Class_Step) return m.member < steps.size() ? steps[m.member].a + 0.5f : (float)m.member;
            return m.cls == Class_Nucleotide ? (float)(m.member / 2) + 0.25f * (float)(m.member % 2) : (float)m.member;
        };
        std::sort(rows.begin(), rows.end(), [&](size_t a, size_t b) { return position(event.members[a]) < position(event.members[b]); });
        const ImU32 color = ImGui::GetColorU32(event_color(index));
        const ImU32 lost = ImGui::GetColorU32(ImVec4(1.0f, 0.7f, 0.2f, 1));
        const size_t columns = std::min(span, (size_t)std::max(1.0f, std::ceil(width)));
        if (footprint_key != event_data_key || footprint_event != index || footprint_columns != columns) {
            footprint_cells.assign(event.members.size() * columns, 0);
            for (size_t m = 0; m < event.members.size(); ++m) {
                for (const auto& s : event.members[m].samples) {
                    if (s.frame < event.begin || s.frame > event.end) continue;
                    const size_t col = (s.frame - event.begin) * columns / span;
                    footprint_cells[m * columns + col] |= s.lost ? 2 : 1;
                }
            }
            footprint_key = event_data_key;
            footprint_event = index;
            footprint_columns = columns;
        }
        const float cell = width / (float)columns;
        for (size_t r = 0; r < rows.size(); ++r) {
            const float y = o.y + row * (float)r;
            for (size_t col = 0; col < columns; ++col) {
                const uint8_t flags = footprint_cells[rows[r] * columns + col];
                if (!flags) continue;
                const float x = o.x + cell * (float)col;
                draw->AddRectFilled(ImVec2(x, y), ImVec2(x + std::max(1.0f, cell), y + std::max(1.0f, row - 1)),
                    flags & 2 ? lost : color);
            }
        }
        if (frame >= event.begin && frame <= event.end) {
            const float x = o.x + width * ((float)(frame - event.begin) + 0.5f) / (float)span;
            draw->AddLine(ImVec2(x, o.y), ImVec2(x, o.y + height), ImGui::GetColorU32(ImGuiCol_Text), 1.0f);
        }
        if (event.peak >= event.begin && event.peak <= event.end) {
            const float x = o.x + width * ((float)(event.peak - event.begin) + 0.5f) / (float)span;
            draw->AddTriangleFilled(ImVec2(x - 4, o.y + height + 1), ImVec2(x + 4, o.y + height + 1), ImVec2(x, o.y + height - 5),
                ImGui::GetColorU32(ImGuiCol_Text));
        }
        ImGui::TextDisabled("Amber: pair lost. Line: current frame. Triangle: peak.");
    }

    void sort_events(ImGuiTableSortSpecs* specs, const float* times) {
        const bool stale = event_order.size() != outlier_events.size();
        if (!stale && !event_order_dirty && !(specs && specs->SpecsDirty)) return;
        event_order.resize(outlier_events.size());
        for (size_t i = 0; i < event_order.size(); ++i) event_order[i] = (int)i;
        if (specs && specs->SpecsCount > 0) {
            const ImGuiTableColumnSortSpecs spec = specs->Specs[0];
            auto duration = [&](const OutlierEvent& e) {
                return times ? (double)times[e.end] - times[e.begin] : (double)(e.end - e.begin + 1);
            };
            std::stable_sort(event_order.begin(), event_order.end(), [&](int ia, int ib) {
                const OutlierEvent& a = outlier_events[(size_t)ia];
                const OutlierEvent& b = outlier_events[(size_t)ib];
                int c = 0;
                switch (spec.ColumnIndex) {
                case 0: c = a.begin < b.begin ? -1 : a.begin > b.begin; break;
                case 1: c = a.pair_first < b.pair_first ? -1 : a.pair_first > b.pair_first; break;
                case 2: c = event_kinds[(size_t)ia].compare(event_kinds[(size_t)ib]); break;
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
            if (m.cls == Class_Step) {
                if (m.member >= steps.size()) continue;
                for (uint32_t p : {steps[m.member].a, steps[m.member].b}) { f(pairs[p].a); f(pairs[p].b); }
            } else if (m.cls == Class_Nucleotide) {
                if (m.member < 2 * pairs.size()) f(nucleotide_base(m.member));
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
        note += "Reference: ";
        note += reference_scope_name((ReferenceScope)statistics_reference_scope);
        note += "\n";
        if (event.truncated_begin || event.truncated_end) note += "Touches detection-range boundary; potentially truncated.\n";
        if (timeline_times(outlier_counts.size())) {
            snprintf(line, sizeof(line), "Min duration %.6g ps; gap limit %.6g ps\n", event_min_time_ps, event_gap_time_ps);
            note += line;
            if (event_range_enabled && !event_range_period) {
                snprintf(line, sizeof(line), "Detection range %.6g-%.6g ps\n", event_range_begin_ps, event_range_end_ps);
                note += line;
            }
        } else {
            snprintf(line, sizeof(line), "Min %d contributing frames; gap limit %d frames\n", event_min_frames, event_max_gap);
            note += line;
            if (event_range_enabled && !event_range_period) {
                snprintf(line, sizeof(line), "Detection frames %d-%d\n", event_range_first, event_range_last);
                note += line;
            }
        }
        if (event_range_enabled && event_range_period) {
            const TimelinePeriod* selected = timeline_periods_find(&app->timeline.periods, event_period_id);
            snprintf(line, sizeof(line), "Detection period: %s, frames %zu-%zu\n",
                selected ? selected->name : "missing", detection_first_frame, detection_last_frame);
            note += line;
        }
        if (baseline_range_enabled && baseline_range_period) {
            const TimelinePeriod* selected = timeline_periods_find(&app->timeline.periods, baseline_period_id);
            snprintf(line, sizeof(line), "Baseline period: %s\n", selected ? selected->name : "missing");
            note += line;
        }
        const float* times = timeline_times(outlier_counts.size());
        const auto [baseline_begin, baseline_end] = baseline_bounds(times);
        char unit[32] = "frame";
        if (times) md_unit_print(unit, sizeof(unit), app->timeline.time_unit);
        snprintf(line, sizeof(line), "Baseline samples %.6g-%.6g %s\n", baseline_begin, baseline_end, unit);
        note += line;
        if (event.parent != SIZE_MAX) {
            snprintf(line, sizeof(line), event.residual ? "Child of #%zu; stands out from parent background by %.2f sigma\n" :
                "Child of #%zu; burst threshold %.2f sigma\n", event.parent + 1, event.burst_sigma);
            note += line;
        }
        note += "Members:";
        size_t shown = 0;
        for (const auto& m : event.members) {
            const auto& labels = class_labels(m.cls);
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
        const auto& labels = class_labels(m.cls);
        if (m.member >= labels.size()) return;
        const int offset = class_first_metric(m.cls);
        static const char* tags[] = {"[intra]", "[inter]", "[nt]"};
        ImGui::PushID((int)(m.member * Class_Count + m.cls));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        char text[512];
        snprintf(text, sizeof(text), "%s %s", tags[std::min<int>(m.cls, Class_Count - 1)], labels[m.member].c_str());
        const bool selected = member == (int)m.member && metric_cls() == m.cls;
        ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
        if (ImGui::Selectable(text, selected)) {
            select_target(m.cls, m.member, m.peak_parameter, m.peak);
            inspect_event(event_index, m.peak);
        }
        ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
        if (ImGui::IsItemHovered()) {
            preview.push_back({m.cls, m.member, m.parameters});
            if (m.peak_parameter >= 0) {
                ImGui::SetTooltip("%s\nClick: select it, its strongest parameter (%s) and peak frame %zu.",
                    class_name(m.cls), metric_name((Metric)(offset + m.peak_parameter)), m.peak);
            } else {
                ImGui::SetTooltip("Intra-base pair, lost (broken) in %zu frames\nClick: select it at frame %zu.", m.lost_frames, m.peak);
            }
        }
        ImGui::TableNextColumn();
        ImGui::TableNextColumn();
        bool first = true;
        if (m.lost_frames) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.55f, 0.55f, 0.45f));
            if (ImGui::SmallButton("Pair lost##lost")) {
                select_target(m.cls, m.member, m.peak_parameter, m.peak);
                inspect_event(event_index, m.peak);
            }
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("This base pair broke (no H-bonds, separated or misaligned) in %zu frames of the event.", m.lost_frames);
            first = false;
        }
        for (int k = 0; k < class_parameter_count(m.cls); ++k) {
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
                select_target(m.cls, m.member, k, m.parameter_peak[k]);
                inspect_event(event_index, m.parameter_peak[k]);
            }
            if (active) ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) {
                preview.push_back({m.cls, m.member, 1u << k});
                const size_t f = m.parameter_peak[k];
                const float v = values[id][f * columns[id] + m.member];
                const std::string state = state_text((Metric)id, m.member, v);
                ImGui::SetTooltip("%s at peak frame %zu: %.3f %s (%+.2f sigma)%s\nClick: show %s for this member at that frame.",
                    metric_name((Metric)id), f, v, metric_unit((Metric)id), m.parameter_peak_z[k], state.c_str(), metric_name((Metric)id));
            }
        }
        ImGui::TableNextColumn();
        if (m.lost_frames) ImGui::TextDisabled("%zu fr (%zu lost)", m.frames, m.lost_frames);
        else ImGui::TextDisabled("%zu fr", m.frames);
        ImGui::SetItemTooltip("Frames in which this member was outlying.");
        ImGui::TableNextColumn(); ImGui::Text("%zu", m.peak);
        ImGui::TableNextColumn();
        if (m.peak_parameter >= 0) ImGui::TextColored(sign_color(m.peak_z, 1), "%+.2f", m.peak_z);
        else ImGui::TextDisabled("-");
        ImGui::TableNextColumn();
        ImGui::PopID();
    }

    void export_events_csv(size_t frames, bool shown_only = false) {
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
        const size_t frame = current_frame();
        for (size_t e = 0; e < outlier_events.size() && ok; ++e) {
            if (shown_only && !event_shown(e, frame)) continue;
            const auto& event = outlier_events[e];
            for (const auto& m : event.members) {
                const int offset = class_first_metric(m.cls);
                const auto& labels = class_labels(m.cls);
                for (int k = 0; k < class_parameter_count(m.cls); ++k) {
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
                    row.residual = event.residual;
                    row.truncated_begin = event.truncated_begin;
                    row.truncated_end = event.truncated_end;
                    const double ps_to_time = times ? md_unit_scale(md_unit_picosecond()) / md_unit_scale(app->timeline.time_unit) : 1;
                    row.min_duration = times ? event_min_time_ps * ps_to_time : event_min_frames;
                    row.max_gap_duration = times ? event_gap_time_ps * ps_to_time : event_max_gap;
                    row.detection_begin = event_range_enabled
                        ? (times ? event_range_begin_ps * ps_to_time : event_range_first) : (times ? times[0] : 0);
                    row.detection_end = event_range_enabled
                        ? (times ? event_range_end_ps * ps_to_time : event_range_last) : (times ? times[frames - 1] : (double)frames - 1);
                    if (event_range_enabled && event_range_period) {
                        row.detection_begin = times ? times[detection_first_frame] : (double)detection_first_frame;
                        row.detection_end = times ? times[detection_last_frame] : (double)detection_last_frame;
                    }
                    row.member_lost = m.lost_frames;
                    row.scope = event_parameters_label();
                    row.baseline = statistics_baseline == Baseline_MedianMAD ? "median_mad" : "mean_sd";
                    row.reference_scope = (ReferenceScope)statistics_reference_scope;
                    const auto bounds = baseline_bounds(times);
                    row.baseline_begin = bounds.first;
                    row.baseline_end = bounds.second;
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
        else MD_LOG_INFO("Lavery: exported %s events CSV to '%s'", shown_only ? "shown" : "all", path);
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
        const int cls = metric_cls();
        const bool step = cls == Class_Step;
        const auto& labels = class_labels(cls);
        const int offset = class_first_metric(cls);
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
                sample.pair_a = step ? pair_labels[steps[m].a] : pair_labels[cls == Class_Nucleotide ? m / 2 : m];
                sample.pair_b = step ? pair_labels[steps[m].b] : "";
                sample.sigma = outlier_sigma;
                sample.selected_mask = outlier_parameter_mask();
                sample.member_outlier = assessment.contributors != 0;
                sample.baseline_method = (Baseline)statistics_baseline;
                sample.reference_scope = (ReferenceScope)statistics_reference_scope;
                const auto bounds = baseline_bounds(times);
                sample.baseline_begin = bounds.first;
                sample.baseline_end = bounds.second;
                for (int k = 0; k < class_parameter_count(cls); ++k) {
                    const int id = offset + k;
                    sample.metric = (Metric)id;
                    sample.quality = parameter_quality(f, m, id);
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
        const auto& labels = class_labels(metric_cls());
        const size_t frame = (size_t)std::clamp((int64_t)std::llround(app->animation.frame),
            (int64_t)0, (int64_t)frame_count - 1);
        if (ImGui::CollapsingHeader("Current-frame outliers###current_outliers", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SetItemTooltip("All members outlying at the current frame, strongest first.\n"
                "Hover: 3D preview (unless inspecting an event). Click: select.\n"
                "Sigma sign: positive = above baseline, negative = below.");
            const int offset = class_first_metric(metric_cls());
            std::vector<std::pair<int, OutlierAssessment>> ranked;
            for (int m = 0; m < count; ++m) {
                const auto result = assess(frame, (size_t)m);
                if (result.contributors) ranked.push_back({m, result});
            }
            std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
                return std::fabs(a.second.peak_z) > std::fabs(b.second.peak_z);
            });
            ImGui::TextDisabled("%zu / %d outlying in frame %zu", ranked.size(), count, frame);
            ImGui::BeginChild("##outlier_rows", ImVec2(0, outliers_list_height), ImGuiChildFlags_Borders);
            for (const auto& entry : ranked) {
                const int m = entry.first;
                const int peak = offset + entry.second.peak_metric;
                const float v = values[peak][frame * count + m];
                const float z = entry.second.peak_z;
                ImGui::PushID(m);
                char row[512];
                snprintf(row, sizeof(row), "%s    %s %.3f %s (%+.2f sigma)%s",
                    labels[m].c_str(), metric_name((Metric)peak), v, metric_unit((Metric)peak), z,
                    quality_at(frame, (size_t)m) ? " [quality flag]" : "");
                if (ImGui::Selectable(row, member == m)) {
                    member = m;
                    selected_detail_active = true;
                    focus_heatmap_selection = true;
                }
                if (ImGui::IsItemHovered()) {
                    preview.push_back({(uint8_t)metric_cls(), (uint32_t)m, outlier_parameter_mask()});
                    ImGui::BeginTooltip();
                    for (int k = 0; k < class_parameter_count(metric_cls()); ++k) {
                        if (!(entry.second.contributors & (1u << k))) continue;
                        const int id = offset + k;
                        const float v = values[id][frame * count + m];
                        ImGui::Text("%s: %.3f %s (%+.2f sigma)%s", metric_name((Metric)id), v, metric_unit((Metric)id),
                            statistics[id].members[m].zscore(v), state_text((Metric)id, (size_t)m, v).c_str());
                        draw_quality(parameter_quality(frame, (size_t)m, id), std::isfinite(v));
                    }
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            height_handle("##outlier_list_height", &outliers_list_height, 160);
        }
    }

    void draw_heatmap(int count, size_t frame_count, const std::vector<float>& heatmap_data, int* selected_member) {
        auto& labels = class_labels(metric_cls());
        if (ImGui::SmallButton("Reset view")) reset_heatmap_view = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("Go to selection")) focus_heatmap_selection = true;
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        ImGui::SetItemTooltip("Heatmap of signed sigma for every member and frame.\n"
            "Ctrl-click / Ctrl-drag: jump / scrub. Click: select member. Drag: pan. Wheel: zoom.\n"
            "Red: positive deviation. Blue: negative deviation.");
        ImGui::SameLine();
        ImGui::TextDisabled("%s z-score", statistics_reference_scope == Reference_HelixHistory ? "Pooled-helix" : "Temporal");
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
            ImPlot::SetupAxes("Frame", class_name(metric_cls()));
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
                if (self.metric_cls() == Class_Step) {
                    return snprintf(buffer, size, "%u-%u", self.steps[index].a + 1, self.steps[index].b + 1);
                }
                if (self.metric_cls() == Class_Nucleotide) return snprintf(buffer, size, "%d%c", index / 2 + 1, index % 2 ? 'b' : 'a');
                return snprintf(buffer, size, "%d", index + 1);
            }, this);
            uint64_t display_key = md_hash64(&metric, sizeof(metric), statistics_key);
            if (heatmap_summary_key != display_key) {
                heatmap_summaries.clear();
                heatmap_summaries.resize(count);
                for (int row = 0; row < count; ++row)
                    if (!heatmap_summaries[row].build(heatmap_data.data() + (size_t)row * frame_count, frame_count))
                        MD_LOG_ERROR("Lavery: could not build heatmap display summary");
                heatmap_summary_key = display_key;
                heatmap_bin_width = 0;
            }
            const auto visible = ImPlot::GetPlotLimits().X;
            const size_t first = (size_t)std::clamp(std::floor(visible.Min + 0.5), 0.0, (double)frame_count - 1);
            const size_t end = (size_t)std::clamp(std::ceil(visible.Max + 0.5), (double)first + 1, (double)frame_count);
            const size_t pixels = (size_t)std::max(1.0f, ImPlot::GetPlotSize().x);
            const size_t bin_width = std::max((size_t)1, (end - first + pixels - 1) / pixels);
            const size_t bins = (end - first + bin_width - 1) / bin_width;
            if (first != heatmap_first || end != heatmap_end || bin_width != heatmap_bin_width) {
                heatmap_display.assign((size_t)count * bins, std::numeric_limits<float>::quiet_NaN());
                for (int row = 0; row < count; ++row) {
                    const float* data = heatmap_data.data() + (size_t)row * frame_count;
                    for (size_t bin = 0; bin < bins; ++bin) {
                        const size_t begin = first + bin * bin_width;
                        float& peak = heatmap_display[(size_t)row * bins + bin];
                        for (uint32_t f : heatmap_summaries[row].indices(begin, std::min(end, begin + bin_width), 1)) {
                            if (std::isfinite(data[f]) && (!std::isfinite(peak) || std::fabs(data[f]) > std::fabs(peak)))
                                peak = data[f];
                        }
                    }
                }
                heatmap_first = first;
                heatmap_end = end;
                heatmap_bin_width = bin_width;
            }
            ImPlot::PlotHeatmap("##lavery_heatmap", heatmap_display.data(), count, (int)bins,
                -outlier_sigma, outlier_sigma,
                nullptr, ImPlotPoint((double)first - 0.5, -0.5),
                ImPlotPoint((double)(first + bins * bin_width) - 0.5, count - 0.5));
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
                    if (metric_cls() == Class_Step) {
                        ImGui::TextUnformatted(pair_labels[steps[col].a].c_str());
                        ImGui::TextUnformatted(pair_labels[steps[col].b].c_str());
                    }
                    ImGui::Text("Frame %d: %.3f %s; z = %+.2f%s", frame, value, metric_unit((Metric)metric), z,
                        state_text((Metric)metric, (size_t)col, value).c_str());
                    draw_quality(quality_at((size_t)frame, (size_t)col), std::isfinite(value));
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
                    selected_detail_active = true;
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
        if (inspected_switch != SIZE_MAX && (inspected_switch >= state_switches.size() ||
            app->animation.mode == PlaybackMode::Playing || app->animation.frame != inspection_frame)) inspected_switch = SIZE_MAX;
        if (inspected_switch == SIZE_MAX && (sync_viewport || inspected_event != SIZE_MAX)) {
            event_context_groups.assign(bases.size(), 0);
            std::vector<size_t> owners(bases.size(), SIZE_MAX);
            for (size_t e = 0; e < outlier_events.size(); ++e) {
                const auto& event = outlier_events[e];
                if (!event_visible(e, frame)) continue;
                std::vector<uint32_t> region;
                for_each_event_base(event, [&](uint32_t base) { region.push_back(base); });
                std::sort(region.begin(), region.end());
                region.erase(std::unique(region.begin(), region.end()), region.end());
                for (uint32_t base : region) {
                    event_context_bases.push_back(base);
                    const size_t previous = owners[base];
                    // A local child owns its contour inside the parent; otherwise stronger events win.
                    if (previous == SIZE_MAX ||
                        (event.parent != SIZE_MAX && outlier_events[previous].parent == SIZE_MAX) ||
                        ((event.parent == SIZE_MAX) == (outlier_events[previous].parent == SIZE_MAX) &&
                            event.score > outlier_events[previous].score)) {
                        owners[base] = e;
                        const size_t slot = e < event_colors.size() ? event_colors[e] : SIZE_MAX;
                        event_context_groups[base] = (uint8_t)(std::min(slot, EVENT_PALETTE_SIZE)+1);
                    }
                }
            }
            std::sort(event_context_bases.begin(), event_context_bases.end());
            event_context_bases.erase(std::unique(event_context_bases.begin(), event_context_bases.end()), event_context_bases.end());
            for (const auto& contribution : event_contributions(outlier_events, frame, displayed_event())) {
                const auto result = assess_member(frame, contribution.cls, contribution.member,
                    contribution.parameters, nullptr, event_end_sigma());
                markers.push_back({contribution.cls, contribution.member, contribution.parameters, contribution.parameters,
                    std::isfinite(result.peak_z) ? result.peak_z : 0.0f, contribution.lost});
            }
        }
        markers_explicit = inspected_event == SIZE_MAX && !preview.empty() && app->animation.mode != PlaybackMode::Playing;
        if (markers_explicit) {
            for (const auto& target : preview) {
                if (target.index >= class_members(target.cls)) continue;
                const bool duplicate = std::any_of(markers.begin(), markers.end(), [&](const Marker& m) {
                    return m.cls == target.cls && m.index == target.index;
                });
                if (duplicate) continue;
                const auto result = assess_member(frame, target.cls, target.index, target.mask);
                markers.push_back({target.cls, target.index, target.mask, result.contributors,
                    std::isfinite(result.peak_z) ? result.peak_z : 0.0f});
            }
        }
        detail_metric = -1;
        if (app->animation.mode != PlaybackMode::Playing &&
            selected_detail_active && sync_viewport && member >= 0 && (size_t)member < class_members(metric_cls())) {
            detail_target = {(uint8_t)metric_cls(), (uint32_t)member, 1u << (metric - class_first_metric(metric_cls()))};
            detail_metric = metric;
        }
        if (app->animation.mode != PlaybackMode::Playing && !preview.empty()) {
            const auto& target = preview.back();
            if (target.mask && !(target.mask & (target.mask - 1))) {
                detail_target = target;
                int k = 0;
                while (!(target.mask & (1u << k))) ++k;
                detail_metric = class_first_metric(target.cls) + k;
            }
        }
        if (app->animation.mode != PlaybackMode::Playing && preview.empty() &&
            hovered_metric >= 0 && hovered_frame == ImGui::GetFrameCount() - 1) {
            detail_target = hovered_target;
            detail_metric = hovered_metric;
        }
        if (detail_metric >= 0) {
            const auto& target = detail_target;
            const size_t displayed = displayed_event();
            const bool allowed = displayed == SIZE_MAX || std::any_of(
                outlier_events[displayed].members.begin(), outlier_events[displayed].members.end(),
                [&](const EventMember& m) { return m.cls == target.cls && m.member == target.index; });
            if (!allowed) detail_metric = -1;
            else if (std::none_of(markers.begin(), markers.end(), [&](const Marker& m) {
                return m.cls == target.cls && m.index == target.index;
            })) {
                const auto result = assess_member(frame, target.cls, target.index, target.mask);
                markers.push_back({target.cls, target.index, target.mask, result.contributors,
                    std::isfinite(result.peak_z) ? result.peak_z : 0.0f});
            }
            if (inspected_switch != SIZE_MAX) {
                const auto& change = state_switches[inspected_switch];
                detail_metric = change.metric;
                detail_target = {Class_Nucleotide, change.member, 1u << (change.metric-Alpha)};
                markers.clear();
                const auto result = assess_member(frame, Class_Nucleotide, change.member, detail_target.mask);
                markers.push_back({Class_Nucleotide, change.member, detail_target.mask, result.contributors,
                    std::isfinite(result.peak_z) ? result.peak_z : 0.0f});
                const uint32_t base = nucleotide_base(change.member);
                event_context_bases = {base};
                event_context_groups.assign(bases.size(), 0);
                event_context_groups[base] = HIGHLIGHT_GROUP_COLORS;
            }
        }
    }

    void update_outlier_highlight() {
        if (!event_context_bases.empty() && !markers_explicit) {
            clear_highlight();
            return;
        }
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
        auto add_backbone = [&](uint32_t base_index) {
            for (int k = 0; k <= BB_O4; ++k) {
                const int32_t atom = bases[base_index].backbone[k];
                if (atom >= 0) md_bitmask_set_bit(&mask, atom);
            }
        };
        for (const auto& marker : markers) {
            if (marker.cls == Class_Step) {
                uint32_t parameters = marker.contributors ? marker.contributors : marker.mask;
                if (detail_target.cls == Class_Step && detail_target.index == marker.index && detail_metric >= 0)
                    parameters |= detail_target.mask;
                if (parameters & 63u) {
                    add_pair(steps[marker.index].a);
                    add_pair(steps[marker.index].b);
                }
                for (Metric id : {MinorGroove, MajorGroove}) {
                    if (!(parameters & (1u << (id - Tilt)))) continue;
                    const auto& grooves = display_grooves[id - MinorGroove];
                    if (marker.index >= grooves.size()) continue;
                    for (const auto& span : grooves[marker.index]) for (int32_t atom : span.atoms) {
                        md_bitmask_set_bit(&mask, atom);
                        if ((size_t)atom < atom_base.size() && atom_base[atom] >= 0) add_backbone((uint32_t)atom_base[atom]);
                    }
                }
            } else if (marker.cls == Class_Nucleotide) {
                const auto& base = bases[nucleotide_base(marker.index)];
                uint32_t parameters = marker.contributors ? marker.contributors : marker.mask;
                if (detail_target.cls == Class_Nucleotide && detail_target.index == marker.index && detail_metric >= 0)
                    parameters |= detail_target.mask;
                for (int k : {BB_C1, BB_C2, BB_C3, BB_C4, BB_O4}) if (base.backbone[k] >= 0) md_bitmask_set_bit(&mask, base.backbone[k]);
                for (Metric id : {Alpha, Beta, Gamma, Delta, Epsilon, Zeta, Chi}) {
                    const bool difference = (id == Epsilon || id == Zeta) && (parameters & (1u << (EpsilonZeta - Alpha)));
                    if (!(parameters & (1u << (id - Alpha))) && !difference) continue;
                    for (int32_t atom : nucleotide_torsion_atoms(base, id)) if (atom >= 0) md_bitmask_set_bit(&mask, atom);
                }
                if (parameters & (1u << (Chi - Alpha))) for (uint32_t atom : base.atoms) md_bitmask_set_bit(&mask, atom);
            } else {
                add_pair(marker.index);
            }
        }
        if (detail_metric >= (int)Alpha && detail_metric <= (int)Chi) {
            const auto atoms = nucleotide_torsion_atoms(bases[nucleotide_base(detail_target.index)], (Metric)detail_metric);
            for (int32_t atom : atoms) if (atom >= 0) md_bitmask_set_bit(&mask, atom);
        }
        highlight_owned = !md_bitmask_empty(&mask);
        highlight_hash = md_bitset_hash64(md_bitmask_view(&mask), 0);
        if (highlight_hash != previous_hash) {
            viamd::event_system_broadcast_event(viamd::EventType_ViamdHighlightMaskChanged,
                viamd::EventPayloadType_ApplicationState, app);
        }
    }

    // Base-pair range a member covers (a step both pairs, a nucleotide its pair).
    void member_pair_range(int cls, uint32_t index, uint32_t* lo, uint32_t* hi) const {
        if (cls == Class_Step && index < steps.size()) {
            *lo = std::min(steps[index].a, steps[index].b);
            *hi = std::max(steps[index].a, steps[index].b);
        } else *lo = *hi = cls == Class_Nucleotide ? index / 2 : index;
    }

    bool members_overlap(int cls_a, uint32_t a, int cls_b, uint32_t b) const {
        uint32_t lo_a, hi_a, lo_b, hi_b;
        member_pair_range(cls_a, a, &lo_a, &hi_a);
        member_pair_range(cls_b, b, &lo_b, &hi_b);
        return lo_a <= hi_b && lo_b <= hi_a;
    }

    void tooltip_member(PickingTooltipTextRequest* req, int cls, uint32_t index, uint32_t mask) const {
        const auto& labels = class_labels(cls);
        if (index >= labels.size()) return;
        const size_t frame = current_frame();
        const int offset = class_first_metric(cls);
        float z[MAX_CLASS_PARAMETERS];
        const auto result = assess_member(frame, cls, index, mask, z);
        static const char* titles[] = {"intra-base pair", "inter-base pair", "nucleotide"};
        tooltip_title(req, "Lavery %s" TOOLTIP_SEP "%s", titles[cls], labels[index].c_str());
        if (cls == Class_Nucleotide) {
            for (const auto& change : state_switches) {
                if (change.member != index || frame < change.begin || frame > change.end) continue;
                tooltip_row(req, STR_LIT("state switch"), "%s: %s -> %s" TOOLTIP_SEP "%+.1f deg" TOOLTIP_SEP "frames %zu-%zu",
                    metric_name(change.metric), metric_state_name(change.metric,change.from), metric_state_name(change.metric,change.to),
                    change.angular_change, change.begin,change.end);
            }
        }
        uint32_t event_parameters_now = 0;
        for (size_t e = 0; e < outlier_events.size(); ++e) {
            const auto& event = outlier_events[e];
            if (inspected_switch != SIZE_MAX || (!sync_viewport && inspected_event == SIZE_MAX) || !event_visible(e, frame)) continue;
            bool context = false;
            const EventMember::Sample* contribution = nullptr;
            for (const auto& m : event.members) {
                if (m.cls == cls && m.member == index) contribution = event_member_sample(m, frame);
                context |= members_overlap(m.cls, m.member, cls, index);
            }
            if (context) {
                tooltip_row(req, STR_LIT("event"), "#%zu" TOOLTIP_SEP "%s" TOOLTIP_SEP "frames %zu-%zu",
                    e + 1, event_kind(event).c_str(), event.begin, event.end);
                if (event.truncated_begin || event.truncated_end) tooltip_row(req, STR_LIT("range boundary"),
                    "Potentially truncated%s%s", event.truncated_begin ? " at start" : "", event.truncated_end ? " at end" : "");
                if (event.parent != SIZE_MAX) tooltip_row(req, STR_LIT("hierarchy"),
                    event.residual ? "Stands out from #%zu" TOOLTIP_SEP "%.2f sigma above background" :
                    "Local burst inside #%zu" TOOLTIP_SEP "%.2f sigma", event.parent + 1, event.burst_sigma);
                tooltip_row(req, STR_LIT("event detail"), "%s", !contribution ? "Context only in this frame" :
                    contribution->lost ? "Pair lost" : "Contributing in this frame");
                if (contribution) {
                    event_parameters_now |= contribution->parameters;
                    std::string names;
                    for (int k = 0; k < class_parameter_count(cls); ++k) {
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
        tooltip_row(req, STR_LIT("reference"), "%s", reference_scope_name((ReferenceScope)statistics_reference_scope));
        for (int k = 0; k < class_parameter_count(cls); ++k) {
            const int id = offset + k;
            const float v = values[id][frame * columns[id] + index];
            const str_t key = str_from_cstr(metric_name((Metric)id));
            const uint16_t parameter_flags = parameter_quality(frame, index, id);
            if (parameter_flags || !std::isfinite(v)) tooltip_row(req, key, "%s", quality_status(parameter_flags, std::isfinite(v)));
            if (cls == Class_Nucleotide) {
                for (uint16_t bit = 1; bit <= Quality_Last; bit <<= 1) {
                    if (parameter_flags & bit) tooltip_row(req, key, "%s", quality_description(bit));
                }
            }
            if (!std::isfinite(v)) {
                tooltip_row(req, key, "no valid sample");
                continue;
            }
            const char* unit = metric_uses_degrees((Metric)id) ? "\xC2\xB0" : " " TOOLTIP_ANGSTROM;
            const bool contributes = (event_parameters_now & (1u << k)) != 0;
            const bool outlier = (result.contributors & (1u << k)) != 0;
            const int state = metric_state((Metric)id, v);
            const auto& stats = statistics[id].members[index];
            if (!stats.reference(v).count) {
                tooltip_row(req, key, "%.2f%s" TOOLTIP_SEP "no baseline for this sample/state", v, unit);
                continue;
            }
            char state_note[64] = {};
            if (state >= 0) snprintf(state_note, sizeof(state_note), TOOLTIP_SEP "%s%s", metric_state_name((Metric)id, state),
                stats.state >= 0 && state != stats.state ? " (non-dominant)" : "");
            tooltip_row(req, key, "%.2f%s" TOOLTIP_SEP "%+.2f sigma%s%s", v, unit, z[k], state_note,
                contributes ? "  EVENT CONTRIBUTOR" :
                outlier ? (k == result.peak_metric ? "  OUTLIER (peak)" : "  OUTLIER") : "");
        }
        const uint16_t flags = cls == Class_Step ? step_quality_flags[frame * steps.size() + index] :
            cls == Class_Pair ? pair_quality_flags[frame * pairs.size() + index] : 0;
        for (uint16_t bit = 1; bit && bit <= Quality_Last; bit <<= 1) {
            if (flags & bit) tooltip_row(req, STR_LIT("quality"), "%s", quality_description(bit));
        }
    }

    void tooltip_pair(PickingTooltipTextRequest* req, uint32_t pair) const {
        bool described = false;
        for (const auto& marker : markers) {
            if (members_overlap(marker.cls, marker.index, Class_Pair, pair)) {
                tooltip_member(req, marker.cls, marker.index, marker.mask);
                described = true;
            }
        }
        if (!described) tooltip_member(req, Class_Pair, pair, 0);
    }

    int32_t picked_base(const PickingHit& hit) const {
        if (hit.domain == PickingDomain_Atom)
            return hit.local_idx < atom_base.size() ? atom_base[hit.local_idx] : -1;
        if (hit.domain == PickingDomain_CartoonGlyph) {
            const auto& glyphs = app->mold.cartoon_glyphs;
            const uint32_t point = cartoon_glyphs_picked_point(glyphs, hit.local_idx);
            const int32_t component = cartoon_glyphs_point_component(glyphs, point);
            for (size_t b = 0; b < bases.size(); ++b)
                if (component >= 0 && bases[b].component == (uint32_t)component) return (int32_t)b;
        }
        if (hit.domain == PickingDomain_Bond) {
            md_atom_pair_t atoms;
            md_bond_flags_t flags;
            bool formed;
            if (picked_bond(*app, hit.local_idx, &atoms, &flags, &formed)) {
                for (uint32_t atom : atoms.idx) if (atom < atom_base.size() && atom_base[atom] >= 0) return atom_base[atom];
            }
        }
        if (hit.domain == PickingDomain_Ring) {
            const int32_t* atoms = nullptr;
            size_t count = 0;
            md_ring_flags_t flags;
            if (picked_ring(*app, hit.local_idx, &atoms, &count, &flags)) {
                for (size_t i = 0; i < count; ++i)
                    if ((size_t)atoms[i] < atom_base.size() && atom_base[atoms[i]] >= 0) return atom_base[atoms[i]];
            }
        }
        return -1;
    }

    void fill_tooltip(PickingTooltipTextRequest* req) const {
        const PickingHit& hit = req->hit;
        if (hit.domain == PickingDomain_Lavery) {
            int cls = Class_Pair;
            int parameter = -1;
            uint32_t index = 0;
            if (decode_pick(hit.local_idx, &cls, &index, &parameter)) {
                if (parameter >= 0) {
                    tooltip_member(req, cls, index, 1u << (parameter - class_first_metric(cls)));
                    tooltip_row(req, STR_LIT("measurement"), "%s", metric_name((Metric)parameter));
                    if (parameter == MinorGroove || parameter == MajorGroove) {
                        tooltip_row(req, STR_LIT("groove definition"), "Reported width = %s P-P distance minus 5.8 " TOOLTIP_ANGSTROM,
                            parameter == MinorGroove ? "mean of two" : "");
                        const auto& spans = display_grooves[parameter - MinorGroove];
                        if (index < spans.size()) for (const auto& span : spans[index]) {
                            tooltip_row(req, STR_LIT("phosphates"), "%s / %s" TOOLTIP_SEP "%.2f " TOOLTIP_ANGSTROM " P-P",
                                measurement_atom_label(span.atoms[0]).c_str(), measurement_atom_label(span.atoms[1]).c_str(),
                                vec3_distance(span.points[0], span.points[1]));
                        }
                    } else if (parameter >= (int)Alpha && parameter <= (int)Chi) {
                        const auto atoms = nucleotide_torsion_atoms(bases[nucleotide_base(index)], (Metric)parameter);
                        for (int32_t atom : atoms) if (atom >= 0) tooltip_row(req, STR_LIT("defining atom"), "%s", measurement_atom_label(atom).c_str());
                    } else if (parameter <= (int)Rise) {
                        tooltip_row(req, STR_LIT("geometry"), "Local mid-frame %s component, not an atom %s",
                            metric_is_angular((Metric)parameter) ? "rotation-vector" : "translation",
                            metric_is_angular((Metric)parameter) ? "angle" : "distance");
                    }
                    const auto& stats = statistics[parameter].members[index];
                    const float v = values[parameter][current_frame()*columns[parameter]+index];
                    const auto ref = stats.reference(v);
                    if (ref.count) tooltip_row(req, STR_LIT("reference"), "center %.3f" TOOLTIP_SEP "scale %.3f" TOOLTIP_SEP "%zu baseline samples",
                        ref.mean, ref.sd, ref.count);
                    else tooltip_row(req, STR_LIT("reference"), "No baseline for this sample/state");
                } else if (cls != Class_Pair) tooltip_member(req, cls, index, marker_mask(cls, index));
                else tooltip_pair(req, index);
            }
        } else if (!markers.empty() || !event_context_bases.empty()) {
            const int32_t base = picked_base(hit);
            const int32_t pair = base >= 0 ? base_pair[base] : -1;
            if (pair < 0) return;
            const bool context = std::binary_search(event_context_bases.begin(), event_context_bases.end(), (uint32_t)base);
            const bool involved = std::any_of(markers.begin(), markers.end(), [&](const Marker& marker) {
                return members_overlap(marker.cls, marker.index, Class_Pair, (uint32_t)pair);
            });
            if (context || involved) {
                tooltip_pair(req, (uint32_t)pair);
            }
        }
    }

    std::string measurement_atom_label(int32_t atom) const {
        char label[160];
        const int32_t b = atom_base[atom];
        const str_t name = md_atom_name(&app->mold.sys.atom, atom);
        if (b >= 0) {
            const auto& base = bases[b];
            const str_t residue = md_component_name(&app->mold.sys.component, base.component);
            snprintf(label, sizeof(label), "S%u:" STR_FMT "%d/" STR_FMT, base.strand + 1, STR_ARG(residue),
                (int)md_system_component_seq_id(&app->mold.sys, base.component), STR_ARG(name));
        } else snprintf(label, sizeof(label), "atom %d/" STR_FMT, atom + 1, STR_ARG(name));
        return label;
    }

    void prepare_display_geometry() {
        display_axis = {};
        display_bases.clear();
        display_pair_quality.clear();
        display_step_from.clear();
        display_step_to.clear();
        display_step_valid.clear();
        display_sugar.clear();
        display_phosphate.clear();
        for (auto& grooves : display_grooves) grooves.assign(steps.size(), {});
        display_pair_frames.clear();
        display_step_frames.clear();
        display_coordinates.clear();
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
        display_coordinates = coordinates;
        display_cell = state.unitcell;
        auto rotate_frame = [&](BasePairFrame frame) {
            frame.origin = mat4_mul_vec3(app->operations.state_rotation, frame.origin, 1);
            frame.x = mat4_mul_vec3(app->operations.state_rotation, frame.x, 0);
            frame.y = mat4_mul_vec3(app->operations.state_rotation, frame.y, 0);
            frame.z = mat4_mul_vec3(app->operations.state_rotation, frame.z, 0);
            return frame;
        };
        display_pair_frames = current.pair_frames;
        for (auto& pair : display_pair_frames) pair = rotate_frame(pair);
        display_step_frames.resize(steps.size());
        if (detail_metric >= 0) {
            const auto& rows = detail_target.cls == Class_Pair ? current.pair_values :
                detail_target.cls == Class_Step ? current.step_values : current.nucleotide_values;
            detail_value = rows[detail_target.index].metric[detail_metric];
            if (detail_metric == (int)Phase || detail_metric == (int)Amplitude)
                detail_companion = rows[detail_target.index].metric[detail_metric == (int)Phase ? Amplitude : Phase];
        }
        std::vector<int64_t> step_from(pairs.size(), -1);
        for (size_t s = 0; s < steps.size(); ++s) step_from[steps[s].a] = (int64_t)s;
        for (const auto& chain : helix_chains(pairs.size(), steps)) {
            const auto strand = bases[pairs[chain[0]].a].strand;
            auto phosphate = [&](uint32_t pair, bool second) {
                const auto& p = pairs[pair];
                const auto& a = bases[p.a];
                const auto& b = bases[p.b];
                if ((a.strand == strand) == (b.strand == strand)) return int32_t(-1);
                return ((a.strand == strand) != second ? a : b).backbone[BB_P];
            };
            for (size_t k = 0; k + 1 < chain.size(); ++k) {
                const int64_t s = step_from[chain[k]];
                if (s < 0 || steps[s].b != chain[k + 1]) continue;
                for (Metric id : {MinorGroove, MajorGroove}) {
                    const auto indices = groove_phosphate_indices(id, k, chain.size());
                    auto& spans = display_grooves[id - MinorGroove][s];
                    for (size_t i = 0; i < indices.count; ++i) {
                        const auto& index = indices.indices[i];
                        const int32_t a = phosphate(chain[index[0]], false), b = phosphate(chain[index[1]], true);
                        if (a < 0 || b < 0) break;
                        const vec3_t from = coordinates[a];
                        const vec3_t to = vec3_add(from, min_image(vec3_sub(coordinates[b], from), state.unitcell));
                        spans.push_back({{a, b}, {mat4_mul_vec3(app->operations.state_rotation, from, 1),
                            mat4_mul_vec3(app->operations.state_rotation, to, 1)}});
                    }
                    if (spans.size() != indices.count) spans.clear();
                }
            }
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
            BasePairFrame next = current.pair_frames[b], midpoint;
            next.origin = to;
            Values unused;
            if (compute_step(&unused, current.pair_frames[a], next, &midpoint)) display_step_frames[s] = rotate_frame(midpoint);
            display_step_from[s] = mat4_mul_vec3(app->operations.state_rotation, from, 1);
            display_step_to[s] = mat4_mul_vec3(app->operations.state_rotation, to, 1);
            display_step_valid[s] = 1;
        }
        // Nucleotide markers: sugar ring centroid and own phosphorus, unwrapped around C1'.
        const float nan = std::numeric_limits<float>::quiet_NaN();
        display_sugar.assign(2 * pairs.size(), vec3_t{nan, nan, nan});
        display_phosphate.assign(2 * pairs.size(), vec3_t{nan, nan, nan});
        for (uint32_t n = 0; n < 2 * pairs.size(); ++n) {
            const auto& base = bases[nucleotide_base(n)];
            const int32_t c1 = base.backbone[BB_C1];
            if (c1 < 0) continue;
            const vec3_t anchor = md_state_coord(&state, c1);
            vec3_t sum = {};
            int found = 0;
            for (int k : {BB_C1, BB_C2, BB_C3, BB_C4, BB_O4}) {
                if (base.backbone[k] < 0) continue;
                sum = vec3_add(sum, vec3_add(anchor, min_image(vec3_sub(md_state_coord(&state, base.backbone[k]), anchor), state.unitcell)));
                ++found;
            }
            display_sugar[n] = mat4_mul_vec3(app->operations.state_rotation, vec3_mul1(sum, 1.0f / (float)found), 1);
            if (base.backbone[BB_P] >= 0) {
                const vec3_t p = vec3_add(anchor, min_image(vec3_sub(md_state_coord(&state, base.backbone[BB_P]), anchor), state.unitcell));
                display_phosphate[n] = mat4_mul_vec3(app->operations.state_rotation, p, 1);
            }
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

    void representation_contours(RepresentationContours* request) {
        if (!request) return;
        static_assert(EVENT_PALETTE_SIZE+2 == HIGHLIGHT_GROUP_COLORS);
        contour_atom_groups.assign(app->mold.sys.atom.count, 0);
        for (uint32_t base : event_context_bases) {
            const auto range = md_component_atom_range(&app->mold.sys.component, bases[base].component);
            for (uint32_t atom = range.beg; atom < range.end; ++atom)
                contour_atom_groups[atom] = event_context_groups[base];
        }
        request->atom_groups = contour_atom_groups.data();
        request->num_atoms = contour_atom_groups.size();
        request->thickness = contour_thickness;
        for (size_t slot = 0; slot < EVENT_PALETTE_SIZE; ++slot) {
            const ImVec4 color = ImPlot::SampleColormap((float)slot / (EVENT_PALETTE_SIZE-1), ImPlotColormap_Viridis);
            request->colors[slot] = {color.x, color.y, color.z};
        }
        request->colors[EVENT_PALETTE_SIZE] = {0.62f, 0.62f, 0.62f};
        request->colors[HIGHLIGHT_GROUP_COLORS-1] = {0.75f, 0.5f, 1};
        request->key = md_hash64(contour_atom_groups.data(), contour_atom_groups.size(), 0xC0470u);
        request->key = md_hash64(request->colors, sizeof(request->colors), request->key);
        if (!request->key) request->key = 1;
    }

    void draw_event_glow() {
        if ((event_visual_style != EventVisual_Glow && event_visual_style != EventVisual_Both) ||
            glow_strength <= 0 || event_context_bases.empty()) return;
        immediate::Scope scope(app->gfx.color_overlay, "Lavery event glow");
        for (uint32_t base : event_context_bases) {
            if (base >= display_bases.size()) continue;
            const size_t group = event_context_groups[base]-1;
            const ImVec4 color = group < EVENT_PALETTE_SIZE
                ? ImPlot::SampleColormap((float)group/(EVENT_PALETTE_SIZE-1), ImPlotColormap_Viridis)
                : group == HIGHLIGHT_GROUP_COLORS-1 ? ImVec4(0.75f,0.5f,1,1) : ImVec4(0.62f,0.62f,0.62f,1);
            for (int layer = 2; layer >= 0; --layer) {
                ImVec4 tint = color;
                tint.w = glow_strength*(layer == 0 ? 18.0f : layer == 1 ? 10.0f : 5.0f)/255.0f;
                immediate::sphere(scope, display_bases[base].center, 3.4f+layer,
                    ImGui::ColorConvertFloat4ToU32(tint), UINT32_MAX, 12, 8);
            }
        }
    }

    void measurement_label(vec3_t position, const char* text, ImVec2 offset = ImVec2(0, -24)) {
        measurement_labels.push_back({position, text, offset});
    }

    void draw_measurement_labels() const {
        auto* window = ImGui::FindWindowByName("Main interaction window");
        if (!window) return;
        const mat4_t mvp = app->view.param.matrix.curr.proj_no_jitter * app->view.param.matrix.curr.view;
        std::vector<ImRect> occupied;
        for (const auto& label : measurement_labels) {
            ImVec2 offset = label.offset;
            const vec4_t p = mat4_mul_vec4(mvp, vec4_from_vec3(label.position, 1));
            if (p.w <= 0) continue;
            const ImVec2 text_size = ImGui::CalcTextSize(label.text.c_str());
            const ImVec2 center((p.x/p.w*0.5f+0.5f)*app->app.window.width,
                (-p.y/p.w*0.5f+0.5f)*app->app.window.height);
            ImRect rect;
            for (int attempt = 0; attempt < 8; ++attempt) {
                rect = ImRect(ImVec2(center.x+offset.x-text_size.x*0.5f-4, center.y+offset.y-text_size.y*0.5f-2),
                    ImVec2(center.x+offset.x+text_size.x*0.5f+4, center.y+offset.y+text_size.y*0.5f+2));
                if (std::none_of(occupied.begin(), occupied.end(), [&](const ImRect& r) { return r.Overlaps(rect); })) break;
                offset.y += text_size.y+8;
            }
            occupied.push_back(rect);
            immediate::scene_label(window->DrawList, mvp, ImVec2((float)app->app.window.width, (float)app->app.window.height),
            label.position, label.text.c_str(), label.text.c_str() + label.text.size(), ImGui::ColorConvertFloat4ToU32(ImVec4(
                app->script.text_color.x, app->script.text_color.y, app->script.text_color.z, app->script.text_color.w)),
            ImGui::ColorConvertFloat4ToU32(ImVec4(
                app->script.text_bg_color.x, app->script.text_bg_color.y, app->script.text_bg_color.z, app->script.text_bg_color.w)), offset);
        }
    }

    vec3_t display_atom(int32_t atom, vec3_t anchor) const {
        const vec3_t p = vec3_add(anchor, min_image(vec3_sub(display_coordinates[atom], anchor), display_cell));
        return mat4_mul_vec3(app->operations.state_rotation, p, 1);
    }

    void measurement_segment(immediate::Queue* queue, vec3_t a, vec3_t b, uint32_t color, uint32_t pick, float radius = 0.13f) const {
        if (vec3_distance(a, b) < 1e-5f) return;
        immediate::capsule(queue, a, b, radius, color, pick, 10);
    }

    void draw_native_measurement(const int32_t* atoms, size_t count, uint32_t color, uint32_t pick, const char* name) {
        for (size_t i = 0; i < count; ++i) if (atoms[i] < 0 || (size_t)atoms[i] >= display_coordinates.size()) return;
        auto xyz = display_coordinates;
        vec3_t previous = xyz[atoms[0]];
        for (size_t i = 0; i < count; ++i) {
            const vec3_t next = vec3_add(previous, min_image(vec3_sub(display_coordinates[atoms[i]], previous), display_cell));
            xyz[atoms[i]] = mat4_mul_vec3(app->operations.state_rotation, next, 1);
            previous = next;
        }
        md_system_state_t state = {.num_atoms = xyz.size(), .xyz = xyz.data()};
        md_script_vis_ctx_t ctx = {.sys = &app->mold.sys, .state = &state};
        md_script_vis_t vis = {};
        md_script_vis_init(&vis, md_get_heap_allocator());
        char expression[160];
        if (count == 2) snprintf(expression, sizeof(expression), "distance(%d,%d);", atoms[0]+1, atoms[1]+1);
        else snprintf(expression, sizeof(expression), "dihedral(%d,%d,%d,%d);", atoms[0]+1, atoms[1]+1, atoms[2]+1, atoms[3]+1);
        if (!md_script_vis_eval_string(&vis, str_from_cstr(expression), &ctx, MD_SCRIPT_VISUALIZE_GEOMETRY | MD_SCRIPT_VISUALIZE_TEXT)) {
            MD_LOG_ERROR("Lavery: native measurement visualization failed: %s", expression);
            md_script_vis_free(&vis);
            return;
        }
        immediate::Scope world(app->gfx.depth_overlay, "Lavery native measurement");
        if (picking_range.domain == PickingDomain_Lavery) immediate::set_picking_base_idx(world, picking_range.beg);
        const auto line_style = measurement_line_style(*app);
        immediate::script_geometry(world, vis, app->script.point_color, app->script.line_color,
            app->script.triangle_color, pick, &line_style);
        immediate::Scope ghost(app->gfx.occluded_overlay, "Lavery selected measurement hidden cue");
        for (size_t i = 1; i < count; ++i) {
            for (int s = 0; s < 8; s += 2) immediate::line(ghost,
                vec3_lerp(xyz[atoms[i-1]],xyz[atoms[i]],s/8.0f), vec3_lerp(xyz[atoms[i-1]],xyz[atoms[i]],(s+1)/8.0f),
                ImGui::ColorConvertFloat4ToU32(ImVec4(app->script.line_color.x, app->script.line_color.y,
                    app->script.line_color.z, app->script.line_color.w*40.0f/255.0f)));
        }
        for (size_t i = 0; i < md_array_size(vis.text); ++i) {
            char text[192];
            snprintf(text, sizeof(text), "%s: %.*s", name, (int)vis.text[i].str.len, vis.text[i].str.ptr);
            measurement_label(vis.text[i].pos, text, ImVec2(0, 18));
        }
        md_script_vis_free(&vis);
    }

    void draw_parameter_detail() {
        if (app->animation.mode == PlaybackMode::Playing || detail_metric < 0 || display_coordinates.empty()) return;
        const Metric id = (Metric)detail_metric;
        const auto& target = detail_target;
        const bool pickable = picking_range.domain == PickingDomain_Lavery &&
            picking_range.end - picking_range.beg == picking_count();
        const uint32_t pick = pickable ? parameter_pick_id(id, target.index) : 0xFFFFFFFFu;
        const float z = statistics[id].members[target.index].zscore(detail_value);
        const uint32_t color = !std::isfinite(z) || std::fabs(z) < outlier_sigma ? IM_COL32(245,245,245,240) :
            z > 0 ? IM_COL32(255,65,95,240) : IM_COL32(40,215,255,240);
        immediate::Scope world(app->gfx.depth_overlay, "Lavery selected parameter");
        if (pickable) immediate::set_picking_base_idx(world, picking_range.beg);
        vec3_t anchor = target.cls == Class_Nucleotide ? display_sugar[target.index] :
            target.cls == Class_Pair ? display_pair_frames[target.index].origin : display_step_from[target.index];
        char label[256];
        if (!std::isfinite(detail_value)) {
            if (target.cls == Class_Step) anchor = display_bases[pairs[steps[target.index].a].a].center;
            else if (target.cls == Class_Pair) anchor = display_bases[pairs[target.index].a].center;
            snprintf(label, sizeof(label), "%s: unavailable (%s)", metric_name(id),
                id == MinorGroove || id == MajorGroove ? "missing flanking phosphates" : "missing or invalid geometry");
            if (std::isfinite(anchor.x)) measurement_label(anchor, label);
            return;
        }
        if (id == MinorGroove || id == MajorGroove) {
            const auto& spans = display_grooves[id - MinorGroove][target.index];
            if (spans.empty()) return;
            anchor = vec3_mul1(vec3_add(spans[0].points[0], spans[0].points[1]), 0.5f);
            for (const auto& span : spans) draw_native_measurement(span.atoms.data(), 2, color, pick, "P-P (raw)");
            snprintf(label, sizeof(label), "%s: %.2f %s (P-P%s - 5.8)", metric_name(id), detail_value, metric_unit(id),
                id == MinorGroove ? " mean" : "");
        } else if (target.cls == Class_Nucleotide) {
            const auto& base = bases[nucleotide_base(target.index)];
            if (id >= Alpha && id <= Chi) {
                const auto atoms = nucleotide_torsion_atoms(base, id);
                draw_native_measurement(atoms.data(), atoms.size(), color, pick, metric_name(id));
                const int state = metric_state(id, detail_value);
                if (state >= 0) {
                    const auto& stats = statistics[id].members[target.index];
                    if (!stats.reference(detail_value).count)
                        snprintf(label, sizeof(label), "%s (no baseline)", metric_state_name(id,state));
                    else snprintf(label, sizeof(label), "%s (baseline usually %s, %.0f%%)", metric_state_name(id,state),
                        metric_state_name(id,stats.state), stats.state_fraction*100);
                    measurement_label(anchor, label);
                }
                return;
            }
            if (id == EpsilonZeta) {
                for (Metric m : {Epsilon, Zeta}) {
                    const auto atoms = nucleotide_torsion_atoms(base, m);
                    draw_native_measurement(atoms.data(), atoms.size(), color, pick, metric_name(m));
                }
            }
            const int state = metric_state(id, detail_value);
            snprintf(label, sizeof(label), "%s: %.2f %s%s%s", metric_name(id), detail_value, metric_unit(id),
                state >= 0 ? " / " : "", state >= 0 ? metric_state_name(id, state) : "");
            if (id == Phase || id == Amplitude) {
                const int paired = id == Phase ? Amplitude : Phase;
                const size_t length = strlen(label);
                snprintf(label + length, sizeof(label) - length, "\n%s: %.2f deg", metric_name((Metric)paired), detail_companion);
            }
        } else {
            const auto& frame = target.cls == Class_Pair ? display_pair_frames[target.index] : display_step_frames[target.index];
            anchor = frame.origin;
            const vec3_t axes[] = {frame.x, frame.y, frame.z};
            const int k = (id - class_first_metric(target.cls)) % 3;
            for (int a = 0; a < 3; ++a) measurement_segment(world, anchor, vec3_add(anchor, vec3_mul1(axes[a], 2.0f)),
                IM_COL32(185,185,185,100), pick, 0.04f);
            if (metric_is_angular(id)) {
                const vec3_t u = axes[(k+1)%3], v = axes[(k+2)%3];
                const float angle = detail_value * (3.14159265359f / 180.0f);
                vec3_t previous = vec3_add(anchor, vec3_mul1(u, 2.5f));
                measurement_segment(world, anchor, previous, color, pick, 0.06f);
                const int segments = std::clamp((int)std::ceil(std::fabs(angle)*2.5f/0.25f), 1, 64);
                for (int s = 1; s <= segments; ++s) {
                    const float a = angle * s / segments;
                    const vec3_t point = vec3_add(anchor, vec3_mul1(vec3_add(vec3_mul1(u, std::cos(a)), vec3_mul1(v, std::sin(a))), 2.5f));
                    if (vec3_distance(previous, point) > 1e-5f)
                        immediate::cylinder(world, previous, point, 0.09f, color, pick, 10);
                    previous = point;
                }
                measurement_segment(world, anchor, previous, color, pick, 0.06f);
            } else {
                const vec3_t end = vec3_add(anchor, vec3_mul1(axes[k], detail_value));
                measurement_segment(world, anchor, end, color, pick, 0.16f);
                if (vec3_distance(anchor, end) > 0.1f) {
                    const vec3_t direction = vec3_normalize(vec3_sub(end, anchor));
                    immediate::cone(world, vec3_sub(end, vec3_mul1(direction, std::min(0.5f, std::fabs(detail_value)*0.4f))),
                        end, 0.25f, color, pick);
                }
            }
            snprintf(label, sizeof(label), "%s: %.2f %s\nlocal-frame %s component", metric_name(id), detail_value, metric_unit(id),
                metric_is_angular(id) ? "rotation" : "translation");
        }
        measurement_label(anchor, label);
    }

    void draw_signed_overlay() {
        measurement_labels.clear();
        if (!signed_overlay || display_bases.empty()) return;
        draw_event_glow();
        if (markers.empty()) return;
        immediate::Scope scope(app->gfx.depth_overlay, "Lavery signed outliers");
        const bool pickable = picking_range.domain == PickingDomain_Lavery &&
            picking_range.end - picking_range.beg == picking_count();
        if (pickable) immediate::set_picking_base_idx(scope, picking_range.beg);
        const int layers = overlay_halo ? 3 : 1;
        for (const auto& marker : markers) {
            const bool outlier = marker.contributors != 0;
            if (app->animation.mode == PlaybackMode::Playing && !outlier && !marker.lost) continue;
            const bool positive = marker.z > 0;
            // Previewed members that are not outlying in this frame are drawn neutral.
            auto color = [&](int alpha) {
                if (marker.lost) return IM_COL32(255, 185, 55, alpha);
                if (!outlier) return IM_COL32(235, 235, 235, alpha * 2 / 3);
                return positive ? IM_COL32(255, 65, 95, alpha) : IM_COL32(40, 215, 255, alpha);
            };
            const bool detailed = detail_metric >= 0 && detail_target.cls == marker.cls && detail_target.index == marker.index;
            const uint32_t pick = !pickable ? 0xFFFFFFFFu : detailed && marker.cls != Class_Step
                ? parameter_pick_id(detail_metric, marker.index) : pick_id(marker.cls, marker.index);
            // Layered, unlit transparency is a visual halo, not HDR emission.
            if (marker.cls == Class_Nucleotide) {
                if (marker.index >= display_sugar.size()) continue;
                const vec3_t sugar = display_sugar[marker.index];
                if (!std::isfinite(sugar.x)) continue;
                const auto& base = bases[nucleotide_base(marker.index)];
                const int order[] = {BB_C1, BB_C2, BB_C3, BB_C4, BB_O4, BB_C1};
                const vec3_t anchor = display_coordinates[base.backbone[BB_C1]];
                for (int i = 1; i < 6; ++i) {
                    if (base.backbone[order[i-1]] < 0 || base.backbone[order[i]] < 0) continue;
                    const vec3_t a = display_atom(base.backbone[order[i-1]], anchor);
                    const vec3_t b = display_atom(base.backbone[order[i]], anchor);
                    measurement_segment(scope, a, b, color(220), pick, 0.18f);
                }
                const auto result = assess_member(current_frame(), marker.cls, marker.index, marker.mask);
                const Metric id = detailed ? (Metric)detail_metric : (Metric)(Alpha + std::max(0, result.peak_metric));
                if (id <= Chi) {
                    const auto atoms = nucleotide_torsion_atoms(base, id);
                    for (size_t i = 1; i < atoms.size(); ++i) {
                        if (atoms[i-1] < 0 || atoms[i] < 0) continue;
                        measurement_segment(scope, display_atom(atoms[i-1], anchor), display_atom(atoms[i], anchor), color(160), pick, 0.13f);
                    }
                }
            } else if (marker.cls == Class_Pair) {
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
                uint32_t mask = marker.contributors;
                if (!mask) mask = marker.mask;
                if (detail_metric >= (int)MinorGroove && detail_metric <= (int)MajorGroove &&
                    detail_target.cls == Class_Step && detail_target.index == marker.index) mask |= detail_target.mask;
                bool groove = false;
                for (Metric id : {MinorGroove, MajorGroove}) {
                    if (!(mask & (1u << (id - Tilt)))) continue;
                    groove = true;
                    const uint32_t parameter_pick = pickable ? parameter_pick_id(id, marker.index) : 0xFFFFFFFFu;
                    const float v = values[id][current_frame() * columns[id] + marker.index];
                    const float z = statistics[id].members[marker.index].zscore(v);
                    const uint32_t tint = !std::isfinite(z) || std::fabs(z) < event_end_sigma() ? IM_COL32(235,235,235,220) :
                        z > 0 ? IM_COL32(255,65,95,220) : IM_COL32(40,215,255,220);
                    for (const auto& span : display_grooves[id - MinorGroove][marker.index]) {
                        const auto a = span.points[0], b = span.points[1];
                        const mat4_t inverse_view = mat4_inverse(app->view.param.matrix.curr.view);
                        const vec3_t toward_camera = mat4_mul_vec3(inverse_view, vec3_t{0,0,1}, 0);
                        const vec3_t across = vec3_cross(vec3_sub(b,a), toward_camera);
                        const vec3_t width = vec3_length(across) > 1e-5f ? vec3_mul1(vec3_normalize(across), 0.45f) : vec3_t{};
                        const uint32_t fill = (tint & 0x00FFFFFFu) | (55u << 24);
                        immediate::triangle(scope, vec3_sub(a,width), vec3_add(a,width), vec3_add(b,width), fill, parameter_pick);
                        immediate::triangle(scope, vec3_sub(a,width), vec3_add(b,width), vec3_sub(b,width), fill, parameter_pick);
                        measurement_segment(scope, a, b, tint, parameter_pick, 0.18f);
                        immediate::sphere(scope, a, 0.5f, tint, parameter_pick, 8, 12);
                        immediate::sphere(scope, b, 0.5f, tint, parameter_pick, 8, 12);
                    }
                }
                if (groove && !(mask & 63u)) continue;
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
        draw_parameter_detail();
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
