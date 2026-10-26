#include <components/lavery/lavery_core.h>
#include <timeline_periods.h>

#include <utest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <md_gro.h>
#include <md_system.h>
#include <md_util.h>
#include <core/md_arena_allocator.h>
#include <core/md_unit.h>
#include "data/lavery_curves_reference.h"
using namespace lavery;

UTEST(lavery_core, pairing_states_require_complete_directional_evidence_and_orientation) {
    PairingEvidence ev;
    ev.complete = true;
    ev.expected = 2;
    ev.purine_side = 1;
    ev.canonical = 2;
    EXPECT_EQ(classify_pairing(ev, -110, -95, 0), Pairing_WatsonCrick);
    EXPECT_EQ(classify_pairing(ev, 250, 265, 0), Pairing_WatsonCrick);
    EXPECT_EQ(classify_pairing(ev, 180, -95, 0), Pairing_WatsonCrick);
    ev.canonical = 1;
    ev.hoogsteen = 2;
    EXPECT_EQ(classify_pairing(ev, -110, 77, 0), Pairing_Hoogsteen);
    EXPECT_EQ(classify_pairing(ev, -110, -95, 0), Pairing_Other);
    EXPECT_EQ(classify_pairing(ev, -110, 125, 0), Pairing_Other);
    ev.purine_side = 0;
    EXPECT_EQ(classify_pairing(ev, 77, -110, 0), Pairing_Hoogsteen);
    ev.hoogsteen = 1;
    EXPECT_EQ(classify_pairing(ev, 77, -110, 0), Pairing_Other);
    ev.canonical = ev.hoogsteen = 0;
    EXPECT_EQ(classify_pairing(ev, 77, -110, Quality_Separation), Pairing_Unpaired);
    EXPECT_EQ(classify_pairing(ev, 77, -110, 0), Pairing_Other);
    EXPECT_EQ(classify_pairing(ev, NAN, -110, Quality_Separation), Pairing_Unavailable);
    EXPECT_EQ(classify_pairing(ev, 77, -110, Quality_MissingAtoms), Pairing_Unavailable);
    ev.complete = false;
    EXPECT_EQ(classify_pairing(ev, 77, -110, Quality_Separation), Pairing_Unavailable);
    ev.complete = true;
    ev.expected = 3;
    ev.purine_side = -1;
    ev.canonical = 3;
    EXPECT_EQ(classify_pairing(ev, -110, -140, 0), Pairing_WatsonCrick);
    EXPECT_EQ(classify_pairing(ev, 77, -140, 0), Pairing_Other);
    ev.canonical = 2;
    EXPECT_EQ(classify_pairing(ev, -110, -140, 0), Pairing_Other);
}

UTEST(lavery_core, pairing_csv_preserves_raw_runs_units_and_exact_residence_thresholds) {
    const PairingRun span = {10, 12, Pairing_Hoogsteen};
    EXPECT_EQ(std::count(csv_pairing_header(), csv_pairing_header()+strlen(csv_pairing_header()), ','), 11);
    const auto row = csv_pairing_run("run,\"dna\"", "pair,\"15\"", 14, span, 100, 140, "ps", 40, true);
    EXPECT_TRUE(row.find("\"run,\"\"dna\"\"\",15,\"pair,\"\"15\"\"\",\"Hoogsteen (A-T/U)\",10,12,100,140,\"ps\",40,40,1\n") != std::string::npos);
    EXPECT_TRUE(csv_pairing_run("", "", 0, span, 10, 12, "frame", 3, false).ends_with(",3,3,1\n"));
    EXPECT_TRUE(csv_pairing_run("", "", 0, span, 100, 140, "ps", 40.001, true).ends_with(",40,40.001,0\n"));
    const PairingRun missing = {0, 2, Pairing_Unavailable};
    EXPECT_TRUE(csv_pairing_run("", "", 0, missing, 0, 40, "ps", 0, true).ends_with(",40,0,0\n"));
}

UTEST(lavery_core, pairing_residence_filters_transitions_without_repainting_raw_states) {
    const std::vector<PairingState> states = {Pairing_WatsonCrick, Pairing_WatsonCrick,
        Pairing_Other, Pairing_Unpaired, Pairing_Unpaired, Pairing_Unpaired,
        Pairing_Hoogsteen, Pairing_Hoogsteen, Pairing_Unavailable, Pairing_WatsonCrick, Pairing_WatsonCrick};
    std::vector<PairingRun> runs;
    ASSERT_TRUE(pairing_runs(&runs, states, 1, 0));
    ASSERT_EQ(runs.size(), (size_t)6);
    std::vector<PairingTransition> changes;
    EventOptions opts;
    opts.min_frames = 2;
    ASSERT_TRUE(pairing_transitions(&changes, runs, opts));
    ASSERT_EQ(changes.size(), (size_t)2);
    EXPECT_EQ(changes[0].source.state, Pairing_WatsonCrick);
    EXPECT_EQ(changes[0].destination.state, Pairing_Unpaired);
    EXPECT_EQ(changes[0].destination.begin, (size_t)3);
    EXPECT_EQ(changes[1].destination.state, Pairing_Hoogsteen);
    EXPECT_EQ(runs[1].state, Pairing_Other);
    opts.times = {0, 20, 40, 60, 80, 100, 120, 140, 160, 180, 200};
    opts.min_duration = 20;
    ASSERT_TRUE(pairing_transitions(&changes, runs, opts));
    EXPECT_EQ(changes.size(), (size_t)2);
    opts.min_duration = 20.001;
    ASSERT_TRUE(pairing_transitions(&changes, runs, opts));
    EXPECT_EQ(changes.size(), (size_t)0);
    opts.min_duration = 0;
    opts.first_frame = 3;
    opts.last_frame = 7;
    ASSERT_TRUE(pairing_transitions(&changes, runs, opts));
    ASSERT_EQ(changes.size(), (size_t)1);
    EXPECT_EQ(changes[0].source.state, Pairing_Unpaired);
    opts.times[1] = 0;
    EXPECT_FALSE(pairing_transitions(&changes, runs, opts));
    EXPECT_FALSE(pairing_runs(&runs, states, 2, 0));
    EXPECT_FALSE(pairing_runs(&runs, states, 1, 1));
}

UTEST(lavery_core, categorical_summary_retains_all_states_at_half_million_frames) {
    std::vector<uint8_t> data(500000, 1);
    data[123457] = 2;
    data[200000] = 0;
    data[300001] = 3;
    StateSummary summary;
    ASSERT_TRUE(summary.build(data));
    EXPECT_EQ(summary.mask(0, data.size()), 15u);
    EXPECT_EQ(summary.mask(123456, 123459), 6u);
    EXPECT_EQ(summary.mask(123457, 123458), 4u);
    EXPECT_EQ(summary.mask(0, 123457), 2u);
    EXPECT_EQ(summary.mask(200000, 200001), 1u);
    for (size_t first = 123400; first < 123500; ++first) for (size_t end = first; end < 123500; ++end) {
        uint32_t mask = 0;
        for (size_t f = first; f < end; ++f) mask |= 1u << data[f];
        EXPECT_EQ(summary.mask(first, end), mask);
    }
    for (size_t i = 0; i < 500; ++i) {
        const size_t first = i*997;
        const size_t end = std::min(data.size(), first+3999);
        uint32_t mask = 0;
        for (size_t f = first; f < end; ++f) mask |= 1u << data[f];
        EXPECT_EQ(summary.mask(first,end),mask);
    }
    const auto begin = std::chrono::steady_clock::now();
    uint32_t combined = 0;
    for (size_t row = 0; row < 40; ++row) for (size_t bin = 0; bin < 1000; ++bin)
        combined |= summary.mask(bin*500,(bin+1)*500);
    EXPECT_EQ(combined,15u);
    printf("Categorical display: 40 x 500000 samples, 1000 bins/row, %.3f ms queries (not GUI FPS)\n",
        std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
    EXPECT_FALSE(summary.build({32}));
    EXPECT_TRUE(summary.build({}));
    EXPECT_EQ(summary.mask(0, 0), 0u);
}

UTEST(lavery_core, maximum_shown_duration_uses_elapsed_time_and_inclusive_frame_span) {
    const float times[] = {100, 101, 104, 110, 111};
    EXPECT_TRUE(event_within_duration(0, 4, times, 5, 0));
    EXPECT_TRUE(event_within_duration(0, 4, nullptr, 5, 0));
    EXPECT_TRUE(event_within_duration(1, 3, times, 5, 9));
    EXPECT_FALSE(event_within_duration(1, 3, times, 5, 8.999));
    EXPECT_TRUE(event_within_duration(1, 3, nullptr, 5, 3));
    EXPECT_FALSE(event_within_duration(1, 3, nullptr, 5, 2));
    EXPECT_TRUE(event_within_duration(3, 3, times, 5, 0.001));
    EXPECT_TRUE(event_within_duration(3, 3, nullptr, 5, 1));
    EXPECT_FALSE(event_within_duration(3, 3, nullptr, 5, 0.5));
    const float ns[] = {0.100f, 0.101f, 0.104f, 0.110f, 0.111f};
    EXPECT_TRUE(event_within_duration(1, 3, ns, 5, 0.00901));
    EXPECT_FALSE(event_within_duration(1, 3, ns, 5, 0.00899));
}

UTEST(lavery_core, maximum_shown_duration_does_not_remove_children_or_mutate_detection) {
    OutlierEvent parent, child;
    parent.begin = 0;
    parent.end = 499999;
    child.begin = 200000;
    child.end = 200002;
    child.parent = 0;
    child.outlier_frames = 2; // A gap still counts toward displayed span.
    const std::vector<OutlierEvent> events = {parent, child};
    std::vector<size_t> shown;
    for (size_t i = 0; i < events.size(); ++i)
        if (event_within_duration(events[i].begin, events[i].end, nullptr, 500000, 3)) shown.push_back(i);
    ASSERT_EQ(shown.size(), (size_t)1);
    EXPECT_EQ(shown[0], (size_t)1);
    EXPECT_FALSE(event_within_duration(child.begin, child.end, nullptr, 500000, 2));
    EXPECT_EQ(events[1].parent, (size_t)0);
    EXPECT_EQ(events[0].end, (size_t)499999);
    EXPECT_EQ(events[1].outlier_frames, (size_t)2);
    for (const auto& e : events) EXPECT_TRUE(event_within_duration(e.begin, e.end, nullptr, 500000, 0));
}

UTEST(lavery_core, maximum_shown_duration_rejects_invalid_limits_and_spans) {
    const float times[] = {1, 0, NAN};
    EXPECT_FALSE(event_within_duration(0, 1, nullptr, 3, -1));
    EXPECT_FALSE(event_within_duration(0, 1, nullptr, 3, NAN));
    EXPECT_FALSE(event_within_duration(0, 1, nullptr, 3, INFINITY));
    EXPECT_FALSE(event_within_duration(2, 1, nullptr, 3, 0));
    EXPECT_FALSE(event_within_duration(0, 3, nullptr, 3, 0));
    EXPECT_FALSE(event_within_duration(0, 0, nullptr, 0, 0));
    EXPECT_FALSE(event_within_duration(0, 1, times, 3, 0));
    EXPECT_FALSE(event_within_duration(0, 2, times, 3, 0));
}

UTEST(lavery_core, display_summary_bounds_half_million_frames_without_losing_peaks_or_gaps) {
    constexpr size_t frames = 500000, pixels = 1200;
    std::vector<float> data(frames * 2, 99999);
    for (size_t f = 0; f < frames; ++f) data[2*f] = std::sin((float)f * 0.01f);
    data[2*123457] = 100;
    data[2*400003] = -150;
    data[2*222222] = std::numeric_limits<float>::quiet_NaN();
    PlotSummary summary;
    const auto build_begin = std::chrono::steady_clock::now();
    ASSERT_TRUE(summary.build(data.data(), frames, 2));
    const auto build_end = std::chrono::steady_clock::now();
    const auto indices = summary.indices(0, frames, pixels);
    ASSERT_FALSE(indices.empty());
    EXPECT_LT(indices.size(), pixels * 20 + 256);
    EXPECT_EQ(indices.front(), (uint32_t)0);
    EXPECT_EQ(indices.back(), (uint32_t)frames - 1);
    for (uint32_t f : {123457u, 400003u, 222222u})
        EXPECT_TRUE(std::binary_search(indices.begin(), indices.end(), f));
    for (size_t i = 1; i < indices.size(); ++i) EXPECT_LT(indices[i-1], indices[i]);
    const auto zoom = summary.indices(123420, 123520, pixels);
    ASSERT_EQ(zoom.size(), (size_t)100);
    for (size_t i = 0; i < zoom.size(); ++i) EXPECT_EQ(zoom[i], (uint32_t)(123420 + i));
    size_t points = 0;
    const auto query_begin = std::chrono::steady_clock::now();
    for (size_t i = 0; i < 1000; ++i) points += summary.indices(i, frames - i, pixels).size();
    const auto query_end = std::chrono::steady_clock::now();
    printf("500000-frame display: %zu points at %zu pixels; build %.3f ms; mean query %.3f ms (%zu points total)\n",
        indices.size(), pixels, std::chrono::duration<double, std::milli>(build_end-build_begin).count(),
        std::chrono::duration<double, std::milli>(query_end-query_begin).count()/1000, points);
}

UTEST(lavery_core, display_summary_refines_boundaries_and_preserves_heatmap_peak_sign) {
    std::vector<float> data(8197);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = (float)((int)(i * 7919 % 997) - 498);
        if (i % 71 == 0) data[i] = std::numeric_limits<float>::quiet_NaN();
    }
    PlotSummary summary;
    ASSERT_TRUE(summary.build(data.data(), data.size()));
    for (size_t begin = 0; begin < data.size(); begin += 137) {
        const size_t end = std::min(data.size(), begin + 1653);
        const auto indices = summary.indices(begin, end, 1);
        ASSERT_FALSE(indices.empty());
        EXPECT_EQ(indices.front(), (uint32_t)begin);
        EXPECT_EQ(indices.back(), (uint32_t)end-1);
        float lo = INFINITY, hi = -INFINITY, peak = 0;
        bool missing = false, sampled_missing = false;
        for (size_t i = begin; i < end; ++i) {
            missing |= !std::isfinite(data[i]);
            if (std::isfinite(data[i])) {
                lo = std::min(lo, data[i]);
                hi = std::max(hi, data[i]);
                peak = std::max(peak, std::fabs(data[i]));
            }
        }
        float sampled_lo = INFINITY, sampled_hi = -INFINITY, sampled_peak = 0;
        for (uint32_t i : indices) {
            EXPECT_GE((size_t)i, begin);
            EXPECT_LT((size_t)i, end);
            sampled_missing |= !std::isfinite(data[i]);
            if (std::isfinite(data[i])) {
                sampled_lo = std::min(sampled_lo, data[i]);
                sampled_hi = std::max(sampled_hi, data[i]);
                if (std::fabs(data[i]) > std::fabs(sampled_peak)) sampled_peak = data[i];
            }
        }
        EXPECT_EQ(sampled_lo, lo);
        EXPECT_EQ(sampled_hi, hi);
        EXPECT_EQ(std::fabs(sampled_peak), peak);
        EXPECT_EQ(sampled_missing, missing);
    }
}

UTEST(lavery_core, display_summary_handles_small_empty_missing_and_invalid_series) {
    PlotSummary summary;
    ASSERT_TRUE(summary.build(nullptr, 0));
    EXPECT_TRUE(summary.indices(0, 0, 1).empty());
    const float data[] = {NAN, 2, 2, NAN};
    ASSERT_TRUE(summary.build(data, 4));
    EXPECT_EQ(summary.indices(0, 4, 1).size(), (size_t)4);
    EXPECT_TRUE(summary.indices(2, 2, 1).empty());
    EXPECT_TRUE(summary.indices(0, 5, 1).empty());
    EXPECT_TRUE(summary.indices(3, 2, 1).empty());
    EXPECT_TRUE(summary.indices(0, 4, 0).empty());
    EXPECT_FALSE(summary.build(nullptr, 4));
    EXPECT_FALSE(summary.build(data, 4, 0));
    EXPECT_FALSE(summary.build(data, UINT32_MAX));
    std::vector<float> missing(1024, NAN);
    ASSERT_TRUE(summary.build(missing.data(), missing.size()));
    for (uint32_t i : summary.indices(0, missing.size(), 1)) EXPECT_TRUE(std::isnan(missing[i]));
    missing.assign(1024, 3);
    missing[511] = 90;
    ASSERT_TRUE(summary.build(missing.data(), missing.size()));
    const auto indices = summary.indices(0, missing.size(), 1);
    EXPECT_TRUE(std::binary_search(indices.begin(), indices.end(), 511u));
}

UTEST(lavery_core, event_distribution_uses_full_span_shared_bins_and_independent_normalization) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::vector<float> values = {0, 100, 2, 200, nan, 300, 4, 400, 8, 500};
    DistributionComparison reference;
    ASSERT_TRUE(compute_distributions(&reference, values, 2, 8));
    EventDistribution event;
    ASSERT_TRUE(compute_event_distribution(&event, values, 2, 0, 1, 3, reference));
    ASSERT_EQ(event.samples.size(), (size_t)2);
    EXPECT_EQ(event.missing, (size_t)1);
    EXPECT_EQ(event.minimum, 2.0);
    EXPECT_EQ(event.maximum, 4.0);
    EXPECT_EQ(event.median, 3.0);
    EXPECT_EQ(event.density.size(), reference.centers.size());
    double integral = 0;
    for (double density : event.density) integral += density * reference.bin_width;
    EXPECT_NEAR(integral, 1.0, 1e-12);
    ASSERT_TRUE(compute_event_distribution(&event, values, 2, 1, 1, 3, reference));
    EXPECT_EQ(event.samples.size(), (size_t)3);
    EXPECT_EQ(event.median, 300.0);
}

UTEST(lavery_core, event_distribution_handles_single_missing_constant_and_invalid_spans) {
    DistributionComparison reference;
    const std::vector<float> values = {5, 5, 5};
    ASSERT_TRUE(compute_distributions(&reference, values, 1));
    EventDistribution event;
    ASSERT_TRUE(compute_event_distribution(&event, values, 1, 0, 1, 1, reference));
    ASSERT_EQ(event.samples.size(), (size_t)1);
    EXPECT_EQ(event.median, 5.0);
    EXPECT_EQ(event.minimum, event.maximum);
    const std::vector<float> missing(3, std::numeric_limits<float>::quiet_NaN());
    ASSERT_TRUE(compute_distributions(&reference, missing, 1));
    ASSERT_TRUE(compute_event_distribution(&event, missing, 1, 0, 0, 2, reference));
    EXPECT_TRUE(event.samples.empty());
    EXPECT_EQ(event.missing, (size_t)3);
    EXPECT_FALSE(compute_event_distribution(&event, values, 0, 0, 0, 2, reference));
    EXPECT_FALSE(compute_event_distribution(&event, values, 1, 1, 0, 2, reference));
    EXPECT_FALSE(compute_event_distribution(&event, values, 1, 0, 2, 1, reference));
    EXPECT_FALSE(compute_event_distribution(&event, values, 1, 0, 0, 3, reference));
    EXPECT_FALSE(compute_event_distribution(&event, values, 1, 0, 0, 2, reference));
}

UTEST(lavery_core, navigation_uses_nearest_frame_and_clamps_time_and_frame_axes) {
    const float times[] = {100, 110, 140, 200};
    EXPECT_EQ(nearest_frame(0, times, 4), (size_t)0);
    EXPECT_EQ(nearest_frame(100, times, 4), (size_t)0);
    EXPECT_EQ(nearest_frame(104.9, times, 4), (size_t)0);
    EXPECT_EQ(nearest_frame(105, times, 4), (size_t)1);
    EXPECT_EQ(nearest_frame(124, times, 4), (size_t)1);
    EXPECT_EQ(nearest_frame(125, times, 4), (size_t)2);
    EXPECT_EQ(nearest_frame(200, times, 4), (size_t)3);
    EXPECT_EQ(nearest_frame(1000, times, 4), (size_t)3);
    EXPECT_EQ(nearest_frame(-1, nullptr, 4), (size_t)0);
    EXPECT_EQ(nearest_frame(1.49, nullptr, 4), (size_t)1);
    EXPECT_EQ(nearest_frame(1.5, nullptr, 4), (size_t)2);
    EXPECT_EQ(nearest_frame(1000, nullptr, 4), (size_t)3);
    EXPECT_EQ(nearest_frame(1000, times, 1), (size_t)0);
    const float ns_times[] = {0.1f, 0.11f, 0.14f, 0.2f};
    EXPECT_EQ(nearest_frame(0.124, ns_times, 4), (size_t)1);
    EXPECT_EQ(nearest_frame(0.126, ns_times, 4), (size_t)2);
}

UTEST(lavery_core, caches_nan_aware_population_statistics_and_heatmap_layout) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const std::vector<float> values = {
        1, 7, nan, 10,
        2, 7, nan, nan,
        3, 7, inf, 14,
    };
    MetricStatistics stats;
    ASSERT_TRUE(compute_statistics(&stats, values, 4));
    ASSERT_EQ(stats.members.size(), (size_t)4);
    ASSERT_EQ(stats.heatmap.size(), values.size());
    EXPECT_EQ(stats.members[0].count, (size_t)3);
    EXPECT_NEAR(stats.members[0].mean, 2.0, 1.0e-12);
    EXPECT_NEAR(stats.members[0].sd, std::sqrt(2.0 / 3.0), 1.0e-12);
    EXPECT_EQ(stats.members[1].count, (size_t)3);
    EXPECT_EQ(stats.members[1].sd, 0.0);
    EXPECT_EQ(stats.members[2].count, (size_t)0);
    EXPECT_EQ(stats.members[3].count, (size_t)2);
    EXPECT_EQ(stats.members[3].mean, 12.0);
    EXPECT_EQ(stats.members[3].sd, 2.0);
    // Highest member is the first heatmap row; constant finite members are neutral.
    EXPECT_EQ(stats.heatmap[0], -1.0f);
    EXPECT_TRUE(std::isnan(stats.heatmap[1]));
    EXPECT_EQ(stats.heatmap[2], 1.0f);
    for (size_t i = 3; i < 6; ++i) EXPECT_TRUE(std::isnan(stats.heatmap[i]));
    for (size_t i = 6; i < 9; ++i) EXPECT_EQ(stats.heatmap[i], 0.0f);
    EXPECT_NEAR(stats.heatmap[9], -std::sqrt(1.5f), 1.0e-6f);
    EXPECT_EQ(stats.heatmap[10], 0.0f);
    EXPECT_NEAR(stats.heatmap[11], std::sqrt(1.5f), 1.0e-6f);
}

UTEST(lavery_core, statistics_handle_empty_and_invalid_matrices) {
    MetricStatistics stats;
    ASSERT_TRUE(compute_statistics(&stats, {}, 0));
    EXPECT_TRUE(stats.members.empty());
    EXPECT_TRUE(stats.heatmap.empty());
    ASSERT_TRUE(compute_statistics(&stats, {}, 2));
    EXPECT_EQ(stats.members.size(), (size_t)2);
    EXPECT_TRUE(std::isnan(stats.members[0].zscore(1)));
    EXPECT_FALSE(compute_statistics(&stats, {1}, 0));
    EXPECT_FALSE(compute_statistics(&stats, {1, 2, 3}, 2));
    EXPECT_FALSE(compute_statistics(nullptr, {}, 0));
}

UTEST(lavery_core, statistics_are_stable_for_large_offsets_and_multiple_outliers) {
    MetricStatistics stats;
    ASSERT_TRUE(compute_statistics(&stats, {1000000, 10, 1000001, 12, 1000002, 14}, 2));
    EXPECT_EQ(stats.members[0].mean, 1000001.0);
    EXPECT_NEAR(stats.members[0].sd, std::sqrt(2.0 / 3.0), 1.0e-12);
    const float threshold = stats.members[0].zscore(1000002);
    EXPECT_TRUE(std::fabs(stats.members[0].zscore(1000002)) > 1.0f);
    EXPECT_TRUE(std::fabs(stats.members[1].zscore(14)) > 1.0f);
    EXPECT_FALSE(std::fabs(stats.members[0].zscore(1000002)) > threshold);
    EXPECT_EQ(stats.members[0].zscore(1000001), 0.0f);
    EXPECT_TRUE(stats.members[0].is_outlier(1000002, 1.0f));
    EXPECT_TRUE(stats.members[1].is_outlier(14, 1.0f));
    const MemberStatistics exact = {10.0, 2.0, 3};
    EXPECT_FALSE(exact.is_outlier(14, 2));
    EXPECT_FALSE(exact.is_outlier(6, 2));
    EXPECT_TRUE(exact.is_outlier(14.01f, 2));
    EXPECT_TRUE(exact.is_outlier(5.99f, 2));
    EXPECT_FALSE(exact.is_outlier(std::numeric_limits<float>::quiet_NaN(), 2));
    EXPECT_FALSE(exact.is_outlier(std::numeric_limits<float>::infinity(), 2));
    EXPECT_FALSE((MemberStatistics{7.0, 0.0, 3}).is_outlier(7, 2));
    EXPECT_FALSE((MemberStatistics{}).is_outlier(7, 2));
}

UTEST(lavery_core, outlier_counts_follow_tolerance_and_exclude_missing_samples) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const std::vector<MemberStatistics> members = {{10, 2, 4}, {5, 0, 4}, {0, 1, 4}, {}};
    const std::vector<float> data = {
        14, 5, -3, nan,
        16, 5, 0, nan,
        nan, 5, inf, nan,
        nan, nan, nan, nan,
    };
    std::vector<FrameOutliers> counts;
    ASSERT_TRUE(compute_outlier_counts(&counts, data, members, 2));
    ASSERT_EQ(counts.size(), (size_t)4);
    EXPECT_EQ(counts[0].outliers, (size_t)1);
    EXPECT_EQ(counts[0].valid, (size_t)3);
    EXPECT_EQ(counts[1].outliers, (size_t)1);
    EXPECT_EQ(counts[1].valid, (size_t)3);
    EXPECT_EQ(counts[2].outliers, (size_t)0);
    EXPECT_EQ(counts[2].valid, (size_t)1);
    EXPECT_EQ(counts[3].outliers, (size_t)0);
    EXPECT_EQ(counts[3].valid, (size_t)0);
    ASSERT_TRUE(compute_outlier_counts(&counts, data, members, 1.5f));
    EXPECT_EQ(counts[0].outliers, (size_t)2);
    ASSERT_TRUE(compute_outlier_counts(&counts, data, members, 3));
    for (const auto& c : counts) EXPECT_EQ(c.outliers, (size_t)0);
    ASSERT_TRUE(compute_outlier_counts(&counts, data, members, 0));
    EXPECT_EQ(counts[0].outliers, (size_t)2);
    for (size_t f = 0; f < counts.size(); ++f) {
        size_t expected = 0;
        for (size_t m = 0; m < members.size(); ++m) {
            expected += members[m].is_outlier(data[f * members.size() + m], 0) ? 1 : 0;
        }
        EXPECT_EQ(counts[f].outliers, expected);
    }
}

UTEST(lavery_core, outlier_counts_validate_input_and_clear_empty_results) {
    std::vector<FrameOutliers> counts(3);
    ASSERT_TRUE(compute_outlier_counts(&counts, {}, {}, 2));
    EXPECT_TRUE(counts.empty());
    ASSERT_TRUE(compute_outlier_counts(&counts, {}, {{0, 1, 1}}, 2));
    EXPECT_TRUE(counts.empty());
    EXPECT_FALSE(compute_outlier_counts(&counts, {1}, {}, 2));
    EXPECT_FALSE(compute_outlier_counts(&counts, {1}, {{0, 1, 1}, {0, 1, 1}}, 2));
    EXPECT_FALSE(compute_outlier_counts(&counts, {}, {}, -1));
    EXPECT_FALSE(compute_outlier_counts(&counts, {}, {}, std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FALSE(compute_outlier_counts(nullptr, {}, {}, 2));
}

static BaseFrame base(uint32_t component, uint32_t strand, vec3_t origin, vec3_t x = {1, 0, 0}, vec3_t y = {0, 1, 0}, vec3_t z = {0, 0, 1}) {
    return {origin, x, y, z, component, strand};
}

UTEST(lavery_core, broken_fixed_pairs_are_missing_and_clear_stale_frames) {
    BaseFrame a = base(0, 0, {0, 0, 0});
    BaseFrame b = base(1, 1, {12, 0, 3}, {1, 0, 0}, {0, -1, 0}, {0, 0, -1});
    EXPECT_FALSE(pair_is_present(a, b));
    uint16_t quality = pair_quality(a, b);
    EXPECT_TRUE(quality & Quality_Separation);
    EXPECT_TRUE(quality & Quality_PlaneOffset);
    EXPECT_FALSE(quality & Quality_InvalidBase);
    Values values;
    BasePairFrame pair;
    const BaseFrame good = base(1, 1, {5, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 0, -1});
    ASSERT_TRUE(compute_present_base_pair(&values, &pair, &quality, a, good));
    const BasePairFrame valid_pair = pair;
    ASSERT_FALSE(compute_present_base_pair(&values, &pair, &quality, a, b));
    EXPECT_TRUE(quality & Quality_Separation);
    EXPECT_TRUE(quality & Quality_PlaneOffset);
    for (int k = 0; k < 6; ++k) EXPECT_TRUE(std::isnan(values.metric[k]));
    EXPECT_FALSE(compute_step(&values, valid_pair, pair));
    EXPECT_FALSE(compute_step(&values, pair, valid_pair));
    b = base(1, 1, {5, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, -1, 0});
    EXPECT_TRUE(pair_quality(a, b) & Quality_Alignment);
    EXPECT_FALSE(compute_present_base_pair(&values, &pair, &quality, a, b));
    EXPECT_TRUE(pair_quality(a, {}) & Quality_InvalidBase);
    b = base(1, 1, {5, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 0, -1});
    EXPECT_EQ(pair_quality(a, b), (uint16_t)Quality_Good);
    ASSERT_TRUE(compute_present_base_pair(&values, &pair, &quality, a, b));
    for (int k = 0; k < 6; ++k) EXPECT_TRUE(std::isfinite(values.metric[k]));
    EXPECT_EQ(quality, (uint16_t)Quality_Good);
}

UTEST(lavery_core, paired_evaluation_uses_periodic_images_and_excludes_loss_from_statistics) {
    const md_unitcell_t cell = md_unitcell_from_extent(30, 30, 30);
    const BaseFrame a = base(0, 0, {1, 0, 0});
    const BaseFrame b = base(1, 1, {25.5f, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 0, -1});
    Values values;
    BasePairFrame frame;
    uint16_t quality = 0;
    ASSERT_TRUE(compute_present_base_pair(&values, &frame, &quality, a, b, &cell));
    const float paired = values.metric[Stretch];
    ASSERT_FALSE(compute_present_base_pair(&values, &frame, &quality, a, b));
    MetricStatistics stats;
    ASSERT_TRUE(compute_statistics(&stats, {paired, values.metric[Stretch], paired}, 1));
    EXPECT_EQ(stats.members[0].count, (size_t)2);
    EXPECT_EQ(stats.members[0].mean, (double)paired);
    EXPECT_EQ(stats.members[0].sd, 0.0);
    std::vector<FrameOutliers> counts;
    ASSERT_TRUE(compute_outlier_counts(&counts, {paired, values.metric[Stretch], paired}, stats.members, 2));
    EXPECT_EQ(counts[1].valid, (size_t)0);
    EXPECT_EQ(counts[1].outliers, (size_t)0);
}

UTEST(lavery_core, extraction_reports_unsupported_and_missing_base_atoms) {
    md_system_t sys = {};
    md_system_state_t state = {};
    std::vector<BaseTopology> bases(2);
    bases[1].reference = 0;
    std::vector<BaseFrame> frames;
    std::vector<uint16_t> quality;
    ASSERT_TRUE(extract_base_frames(frames, sys, state, bases, &quality));
    ASSERT_EQ(quality.size(), (size_t)2);
    EXPECT_EQ(quality[0], (uint16_t)Quality_UnsupportedBase);
    EXPECT_EQ(quality[1], (uint16_t)Quality_MissingAtoms);
    EXPECT_TRUE(pair_quality(frames[0], frames[1]) & Quality_InvalidBase);
}

UTEST(lavery_core, aggregate_outliers_count_a_member_once_and_keep_contributors) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float values[] = {4, -5, 2, nan, 7, 0};
    const MemberStatistics stats[] = {{0,1,5}, {0,1,5}, {0,1,5}, {0,1,5}, {7,0,5}, {}};
    const auto all = assess_outliers(values, stats, 6, 63, 2);
    EXPECT_EQ(all.valid, (size_t)4);
    EXPECT_EQ(all.contributors, (uint32_t)3);
    EXPECT_EQ(all.peak_metric, 1);
    EXPECT_EQ(all.peak_z, -5.0f);
    const auto selected = assess_outliers(values, stats, 6, 4, 2);
    EXPECT_EQ(selected.valid, (size_t)1);
    EXPECT_EQ(selected.contributors, (uint32_t)0);
    const auto empty = assess_outliers(values, stats, 6, 0, 2);
    EXPECT_EQ(empty.valid, (size_t)0);
    EXPECT_EQ(empty.contributors, (uint32_t)0);
    const auto higher = assess_outliers(values, stats, 6, 63, 5);
    EXPECT_EQ(higher.contributors, (uint32_t)0);
}

UTEST(lavery_core, extraction_distinguishes_nonfinite_coordinates_from_degenerate_geometry) {
    md_system_t sys = {};
    vec3_t coordinates[10] = {};
    md_system_state_t state = {};
    state.xyz = coordinates;
    state.num_atoms = 10;
    BaseTopology topology;
    topology.reference = 0;
    topology.glycosidic_atom = 0;
    topology.ring = {1, 2, 3};
    for (uint32_t k = 0; k < 10; ++k) {
        topology.atoms.push_back(k);
        topology.fit_atoms.push_back(k);
    }
    std::vector<BaseFrame> frames;
    std::vector<uint16_t> quality;
    ASSERT_TRUE(extract_base_frames(frames, sys, state, {topology}, &quality));
    EXPECT_EQ(quality[0], (uint16_t)Quality_DegenerateBase);
    coordinates[2].x = std::numeric_limits<float>::quiet_NaN();
    ASSERT_TRUE(extract_base_frames(frames, sys, state, {topology}, &quality));
    EXPECT_EQ(quality[0], (uint16_t)Quality_NonfiniteCoordinates);
}

static MemberOutlier event_sample(uint32_t member, int cls, int parameter, float z, bool strong = true) {
    MemberOutlier sample;
    sample.member = member;
    sample.cls = (uint8_t)cls;
    sample.contributors = 1u << parameter;
    sample.strong = strong ? sample.contributors : 0;
    sample.z[parameter] = z;
    return sample;
}

static MemberOutlier lost_pair(uint32_t member) {
    MemberOutlier sample;
    sample.member = member;
    sample.lost = true;
    return sample;
}

// Ten pairs; steps link 0-1 ... 8-9 except 4-5, so pairs 0-4 and 5-9 are two duplexes.
static std::vector<StepIndex> event_steps() {
    std::vector<StepIndex> steps;
    for (uint32_t p = 0; p + 1 < 10; ++p) if (p != 4) steps.push_back({p, p + 1});
    return steps;
}

UTEST(lavery_core, events_group_consecutive_frames_and_track_peak_severity_and_count) {
    std::vector<std::vector<MemberOutlier>> frames(8);
    frames[1] = {event_sample(0, false, 0, 3), event_sample(1, false, 0, 2.6f)};
    frames[2] = {event_sample(0, false, 0, 4), event_sample(1, false, 0, 2.6f), event_sample(2, false, 0, 2.7f)};
    frames[3] = {event_sample(2, false, 0, -6)};
    frames[5] = {event_sample(0, false, 0, 5)};
    frames[7] = {event_sample(1, false, 0, 7)};
    const auto events = detect_events(frames, 10, event_steps());
    ASSERT_EQ(events.size(), (size_t)3);
    EXPECT_EQ(events[0].begin, (size_t)1);
    EXPECT_EQ(events[0].end, (size_t)3);
    EXPECT_EQ(events[0].peak, (size_t)3);
    EXPECT_EQ(events[0].peak_count, (size_t)3);
    EXPECT_EQ(events[0].severity, 6.0f);
    EXPECT_EQ(events[0].outlier_frames, (size_t)3);
    EXPECT_EQ(events[0].pair_first, (uint32_t)0);
    EXPECT_EQ(events[0].pair_last, (uint32_t)2);
    EXPECT_NEAR(events[0].score, 3 + 2.6f + 4 + 2.6f + 2.7f + 6, 1e-5f);
    EXPECT_EQ(events[1].begin, (size_t)5);
    EXPECT_EQ(events[1].end, (size_t)5);
    EXPECT_EQ(events[2].peak, (size_t)7);
    EXPECT_TRUE(detect_events({}, 10, event_steps()).empty());
    EXPECT_TRUE(detect_events(frames, 3, event_steps()).empty());
}

UTEST(lavery_core, events_bridge_gaps_and_drop_short_events) {
    std::vector<std::vector<MemberOutlier>> frames(10);
    frames[0] = {event_sample(0, false, 0, 3)};
    frames[2] = {event_sample(0, false, 0, 4), event_sample(1, false, 0, 3)};
    frames[5] = {event_sample(1, false, 0, 5)};
    frames[9] = {event_sample(0, false, 0, 6)};
    EXPECT_EQ(detect_events(frames, 10, event_steps()).size(), (size_t)4);
    EventOptions bridge;
    bridge.max_gap = 2;
    const auto bridged = detect_events(frames, 10, event_steps(), bridge);
    ASSERT_EQ(bridged.size(), (size_t)2);
    EXPECT_EQ(bridged[0].begin, (size_t)0);
    EXPECT_EQ(bridged[0].end, (size_t)5);
    EXPECT_EQ(bridged[0].outlier_frames, (size_t)3);
    EXPECT_EQ(bridged[0].peak, (size_t)5);
    EXPECT_EQ(bridged[0].peak_count, (size_t)2);
    EXPECT_EQ(bridged[1].begin, (size_t)9);
    bridge.min_frames = 2;
    const auto long_only = detect_events(frames, 10, event_steps(), bridge);
    ASSERT_EQ(long_only.size(), (size_t)1);
    EXPECT_EQ(long_only[0].begin, (size_t)0);
}

UTEST(lavery_core, event_members_record_classes_parameters_and_peaks) {
    std::vector<std::vector<MemberOutlier>> frames(4);
    MemberOutlier pair = event_sample(3, false, 1, 3);
    frames[0].push_back(pair);
    pair.contributors = pair.strong = (1u << 1) | (1u << 4);
    pair.z[1] = -2.6f;
    pair.z[4] = 2.8f;
    frames[1].push_back(pair);
    frames[1].push_back(event_sample(3, true, 2, -5));
    frames[3].push_back(event_sample(3, true, 2, 4));
    const auto events = detect_events(frames, 10, event_steps());
    ASSERT_EQ(events.size(), (size_t)2);
    ASSERT_EQ(events[0].members.size(), (size_t)2);
    const EventMember& first = events[0].members[0];
    EXPECT_TRUE(first.cls);
    EXPECT_EQ(first.member, (uint32_t)3);
    EXPECT_EQ(first.parameters, (uint32_t)(1u << 2));
    EXPECT_EQ(first.peak, (size_t)1);
    EXPECT_EQ(first.peak_z, -5.0f);
    const EventMember& second = events[0].members[1];
    EXPECT_FALSE(second.cls);
    EXPECT_EQ(second.frames, (size_t)2);
    EXPECT_EQ(second.parameters, (uint32_t)((1u << 1) | (1u << 4)));
    EXPECT_EQ(second.peak_parameter, 1);
    EXPECT_EQ(second.peak, (size_t)0);
    EXPECT_EQ(second.parameter_peak[4], (size_t)1);
    EXPECT_EQ(second.parameter_peak_z[4], 2.8f);
    // step 3 links pairs 3 and 4
    EXPECT_EQ(events[0].pair_first, (uint32_t)3);
    EXPECT_EQ(events[0].pair_last, (uint32_t)4);
    EXPECT_EQ(events[0].metric_score[Twist], 5.0f);
    EXPECT_EQ(events[0].metric_signed[Twist], -5.0f);
    ASSERT_EQ(events[1].members.size(), (size_t)1);
    EXPECT_EQ(events[1].members[0].peak_z, 4.0f);
}

UTEST(lavery_core, physical_duration_filters_elapsed_time_not_sample_count) {
    std::vector<std::vector<MemberOutlier>> frames(5);
    for (auto& frame : frames) frame = {event_sample(0, false, 0, 3)};
    EventOptions options;
    options.times = {0, 1, 2, 10, 20};
    options.min_duration = 20;
    const auto events = detect_events(frames, 10, event_steps(), options);
    ASSERT_EQ(events.size(), (size_t)1);
    EXPECT_EQ(events[0].outlier_frames, (size_t)5);
    options.min_duration = 20.001;
    EXPECT_TRUE(detect_events(frames, 10, event_steps(), options).empty());
    frames = {{}, {}, {event_sample(0, false, 0, 3)}, {}, {}};
    options.min_duration = 0;
    EXPECT_EQ(detect_events(frames, 10, event_steps(), options).size(), (size_t)1);
    options.min_duration = 0.001;
    EXPECT_TRUE(detect_events(frames, 10, event_steps(), options).empty());
}

UTEST(lavery_core, physical_gap_uses_quiet_time_with_irregular_sampling) {
    std::vector<std::vector<MemberOutlier>> frames(5);
    frames[0] = {event_sample(0, false, 0, 3)};
    frames[2] = {event_sample(0, false, 0, 3)};
    frames[4] = {event_sample(0, false, 0, 3)};
    EventOptions options;
    options.times = {0, 2, 5, 15, 16};
    options.max_gap = 100; // Ignored with physical timestamps.
    options.max_gap_duration = 2;
    auto events = detect_events(frames, 10, event_steps(), options);
    ASSERT_EQ(events.size(), (size_t)2);
    EXPECT_EQ(events[0].end, (size_t)2);
    EXPECT_EQ(events[1].begin, (size_t)4);
    options.max_gap_duration = 1.999;
    EXPECT_EQ(detect_events(frames, 10, event_steps(), options).size(), (size_t)3);
    options.max_gap_duration = 10;
    options.min_duration = 16;
    events = detect_events(frames, 10, event_steps(), options);
    ASSERT_EQ(events.size(), (size_t)1);
    EXPECT_EQ(events[0].outlier_frames, (size_t)3);
    EXPECT_EQ(events[0].end, (size_t)4);
}

UTEST(lavery_core, physical_range_is_inclusive_and_marks_only_interior_boundaries) {
    std::vector<std::vector<MemberOutlier>> frames(5);
    for (auto& frame : frames) frame = {event_sample(0, false, 0, 3)};
    EventOptions options;
    options.times = {0, 2, 5, 15, 16};
    ASSERT_TRUE(event_time_range(&options, 2, 15));
    auto events = detect_events(frames, 10, event_steps(), options);
    ASSERT_EQ(events.size(), (size_t)1);
    EXPECT_EQ(events[0].begin, (size_t)1);
    EXPECT_EQ(events[0].end, (size_t)3);
    EXPECT_TRUE(events[0].truncated_begin);
    EXPECT_TRUE(events[0].truncated_end);
    EXPECT_EQ(events[0].members[0].samples.size(), (size_t)3);
    options.min_duration = 14;
    EXPECT_TRUE(detect_events(frames, 10, event_steps(), options).empty());
    options.min_duration = 0;
    ASSERT_TRUE(event_time_range(&options, 2.1, 15.9));
    EXPECT_EQ(options.first_frame, (size_t)2);
    EXPECT_EQ(options.last_frame, (size_t)3);
    ASSERT_TRUE(event_time_range(&options, -1, 17));
    events = detect_events(frames, 10, event_steps(), options);
    ASSERT_EQ(events.size(), (size_t)1);
    EXPECT_FALSE(events[0].truncated_begin);
    EXPECT_FALSE(events[0].truncated_end);
    EXPECT_FALSE(event_time_range(&options, 2.1, 4.9));
    EXPECT_FALSE(event_time_range(&options, 20, 30));
    EXPECT_FALSE(event_time_range(&options, -20, -10));
    EXPECT_FALSE(event_time_range(&options, 5, 2));
    ASSERT_TRUE(event_time_range(&options, 5, 5));
    EXPECT_EQ(options.first_frame, options.last_frame);
}

UTEST(lavery_core, physical_time_rejects_invalid_metadata_and_limits) {
    const std::vector<std::vector<MemberOutlier>> frames = {{event_sample(0, false, 0, 3)}, {}};
    EventOptions options;
    options.times = {0};
    EXPECT_TRUE(detect_events(frames, 10, event_steps(), options).empty());
    for (const auto& times : std::vector<std::vector<double>>{{0, 0}, {2, 1}, {0, NAN}, {0, INFINITY}}) {
        options.times = times;
        EXPECT_TRUE(detect_events(frames, 10, event_steps(), options).empty());
        EXPECT_FALSE(event_time_range(&options, 0, 3));
    }
    options.times = {0, 1};
    options.min_duration = -1;
    EXPECT_TRUE(detect_events(frames, 10, event_steps(), options).empty());
    options.min_duration = NAN;
    EXPECT_TRUE(detect_events(frames, 10, event_steps(), options).empty());
    options.min_duration = 0;
    options.max_gap_duration = -1;
    EXPECT_TRUE(detect_events(frames, 10, event_steps(), options).empty());
    options.max_gap_duration = INFINITY;
    EXPECT_TRUE(detect_events(frames, 10, event_steps(), options).empty());
    EXPECT_FALSE(event_time_range(&options, NAN, 3));
}

UTEST(lavery_core, minimum_three_counts_contributing_frames_not_elapsed_span) {
    std::vector<std::vector<MemberOutlier>> frames(10);
    frames[0] = {event_sample(1, false, 0, 3)};
    frames[4] = {event_sample(1, false, 0, 4)};
    frames[8] = {event_sample(8, false, 0, 4)};
    EventOptions options;
    options.max_gap = 3;
    options.min_frames = 3;
    // Two samples span five frames, but still fail a three-contribution minimum.
    EXPECT_TRUE(detect_events(frames, 10, event_steps(), options).empty());
    frames[5] = {event_sample(1, false, 0, 3)};
    const auto events = detect_events(frames, 10, event_steps(), options);
    ASSERT_EQ(events.size(), (size_t)1);
    EXPECT_EQ(events[0].outlier_frames, (size_t)3);
    EXPECT_EQ(events[0].begin, (size_t)0);
    EXPECT_EQ(events[0].end, (size_t)5);
    options.min_frames = 1;
    const auto spikes = detect_events(frames, 10, event_steps(), options);
    ASSERT_EQ(spikes.size(), (size_t)2);
    EXPECT_EQ(spikes[1].outlier_frames, (size_t)1);
    EXPECT_EQ(spikes[1].end - spikes[1].begin + 1, (size_t)1);
}

UTEST(lavery_core, paused_inspection_isolates_one_event_including_overlapping_parent_and_child) {
    OutlierEvent parent;
    parent.begin = 0;
    parent.end = 5;
    EventMember pair;
    pair.member = 1;
    pair.samples = {{2, 1u, false}};
    parent.members.push_back(pair);
    pair.member = 2;
    parent.members.push_back(pair);
    OutlierEvent child = parent;
    child.parent = 0;
    child.members.resize(1);
    child.members[0].samples[0].parameters = 1u << 2;
    OutlierEvent other = parent;
    other.members.resize(1);
    other.members[0].member = 8;
    const std::vector<OutlierEvent> events = {parent, child, other};
    const auto automatic = event_contributions(events, 2);
    ASSERT_EQ(automatic.size(), (size_t)3);
    EXPECT_EQ(automatic[0].parameters, 1u | (1u << 2));
    const auto inspected = event_contributions(events, 2, 1);
    ASSERT_EQ(inspected.size(), (size_t)1);
    EXPECT_EQ(inspected[0].member, 1u);
    EXPECT_EQ(inspected[0].parameters, 1u << 2);
    EXPECT_EQ(event_contributions(events, 2, 0).size(), (size_t)2);
    EXPECT_TRUE(event_contributions(events, 1, 1).empty());
    EXPECT_TRUE(event_contributions(events, 6, 1).empty());
    EXPECT_TRUE(event_contributions(events, 2, 5).empty());
    EXPECT_EQ(event_contributions(events, 2, SIZE_MAX).size(), automatic.size());
}

UTEST(lavery_core, events_split_by_helix_neighbourhood_and_duplex) {
    std::vector<std::vector<MemberOutlier>> frames(3);
    frames[0] = {event_sample(1, false, 0, 3), event_sample(8, false, 0, 4)};
    // step index 2 links pairs 2 and 3: within two base pairs of pair 1
    frames[1] = {event_sample(2, true, 0, 3)};
    EventOptions local;
    const auto split = detect_events(frames, 10, event_steps(), local);
    ASSERT_EQ(split.size(), (size_t)2);
    EXPECT_EQ(split[0].pair_first, (uint32_t)1);
    EXPECT_EQ(split[0].pair_last, (uint32_t)3);
    EXPECT_EQ(split[0].members.size(), (size_t)2);
    EXPECT_EQ(split[1].pair_first, (uint32_t)8);
    local.neighbourhood = 1;
    EXPECT_EQ(detect_events(frames, 10, event_steps(), local).size(), (size_t)3);
    local.neighbourhood = -1;
    EXPECT_EQ(detect_events(frames, 10, event_steps(), local).size(), (size_t)1);
    // Pairs 4 and 5 are adjacent indices but not linked by a step: never neighbours.
    std::vector<std::vector<MemberOutlier>> duplexes(1);
    duplexes[0] = {event_sample(4, false, 0, 3), event_sample(5, false, 0, 3)};
    local.neighbourhood = 100;
    EXPECT_EQ(detect_events(duplexes, 10, event_steps(), local).size(), (size_t)2);
    local.neighbourhood = -1;
    EXPECT_EQ(detect_events(duplexes, 10, event_steps(), local).size(), (size_t)1);
}

UTEST(lavery_core, event_playback_tracks_exact_parameters_losses_and_bridged_gaps) {
    std::vector<std::vector<MemberOutlier>> frames(5);
    frames[0] = {event_sample(2, false, 0, 3)};
    frames[2] = {event_sample(2, false, 4, -4)};
    frames[3] = {lost_pair(2)};
    EventOptions options;
    options.max_gap = 1;
    const auto events = detect_events(frames, 10, event_steps(), options);
    ASSERT_EQ(events.size(), (size_t)1);
    ASSERT_EQ(events[0].members.size(), (size_t)1);
    const auto& member = events[0].members[0];
    EXPECT_EQ(member.samples.size(), member.frames);
    const auto* first = event_member_sample(member, 0);
    ASSERT_TRUE(first != nullptr);
    EXPECT_EQ(first->parameters, 1u);
    EXPECT_FALSE(first->lost);
    EXPECT_TRUE(event_member_sample(member, 1) == nullptr);
    const auto* next = event_member_sample(member, 2);
    ASSERT_TRUE(next != nullptr);
    EXPECT_EQ(next->parameters, 1u << 4);
    const auto* lost = event_member_sample(member, 3);
    ASSERT_TRUE(lost != nullptr);
    EXPECT_TRUE(lost->lost);
    EXPECT_EQ(lost->parameters, 0u);
    EXPECT_TRUE(event_member_sample(member, 4) == nullptr);
}

UTEST(lavery_core, event_playback_does_not_borrow_samples_from_another_event) {
    std::vector<std::vector<MemberOutlier>> frames(5);
    frames[0] = {event_sample(2, false, 0, 3)};
    frames[1] = {event_sample(3, false, 0, 3)};
    frames[2] = {event_sample(4, false, 0, 3)};
    frames[3] = {event_sample(2, false, 4, -4), event_sample(4, false, 0, 3)};
    EventOptions options;
    options.neighbourhood = 1;
    const auto events = detect_events(frames, 10, event_steps(), options);
    ASSERT_EQ(events.size(), (size_t)2);
    EXPECT_EQ(events[0].end, (size_t)3);
    const auto member = std::find_if(events[0].members.begin(), events[0].members.end(),
        [](const EventMember& m) { return !m.cls && m.member == 2; });
    ASSERT_TRUE(member != events[0].members.end());
    EXPECT_TRUE(event_member_sample(*member, 0) != nullptr);
    EXPECT_TRUE(event_member_sample(*member, 3) == nullptr);
    EXPECT_TRUE(event_member_sample(events[1].members[0], 3) != nullptr);
}

static std::vector<std::vector<MemberOutlier>> hierarchy_samples() {
    std::vector<std::vector<MemberOutlier>> frames(10);
    for (size_t f = 0; f < frames.size(); ++f) {
        for (uint32_t p = 0; p < 5; ++p) frames[f].push_back(event_sample(p, false, 0, 2.7f));
        if (f >= 3 && f <= 5) {
            frames[f][2] = event_sample(2, false, 2, f == 3 ? 4.5f : 3.2f, f == 3);
        }
    }
    return frames;
}

UTEST(lavery_core, hierarchy_retains_a_local_burst_inside_a_long_broad_event) {
    const auto frames = hierarchy_samples();
    EventOptions options;
    options.burst_sigma = 3.5f;
    const auto events = detect_events(frames, 10, event_steps(), options);
    ASSERT_EQ(events.size(), (size_t)2);
    EXPECT_EQ(events[0].parent, SIZE_MAX);
    EXPECT_EQ(events[0].begin, (size_t)0);
    EXPECT_EQ(events[0].end, (size_t)9);
    EXPECT_EQ(events[0].members.size(), (size_t)5);
    EXPECT_EQ(events[1].parent, (size_t)0);
    EXPECT_EQ(events[1].burst_sigma, 3.5f);
    EXPECT_EQ(events[1].begin, (size_t)3);
    EXPECT_EQ(events[1].end, (size_t)5);
    ASSERT_EQ(events[1].members.size(), (size_t)1);
    EXPECT_EQ(events[1].members[0].member, 2u);
    EXPECT_EQ(events[1].members[0].parameters, 1u << 2);
    EXPECT_EQ(events[1].score, 4.5f + 3.2f + 3.2f);
    const auto slots = event_color_slots(events);
    EXPECT_NE(slots[0], slots[1]);
    const auto contributions = event_contributions(events, 3);
    EXPECT_EQ(contributions.size(), (size_t)5);
    EXPECT_EQ(contributions[2].parameters, 1u << 2);
    options.burst_sigma = 0;
    const auto flat = detect_events(frames, 10, event_steps(), options);
    ASSERT_EQ(flat.size(), (size_t)1);
    EXPECT_EQ(flat[0].score, events[0].score);
    EXPECT_EQ(flat[0].members.size(), events[0].members.size());
}

// Ten pairs share a +3 sigma shift (frames 0-9); pairs 6-7 swing to -3 sigma in frames 3-6.
static std::vector<std::vector<float>> background_z() {
    std::vector<std::vector<float>> z(10, std::vector<float>(10, 3.0f));
    for (size_t f = 3; f <= 6; ++f) z[f][6] = z[f][7] = -3.0f;
    return z;
}

static std::vector<std::vector<MemberOutlier>> background_samples(const std::vector<std::vector<float>>& z) {
    std::vector<std::vector<MemberOutlier>> frames(z.size());
    for (size_t f = 0; f < z.size(); ++f) {
        for (uint32_t p = 0; p < z[f].size(); ++p) frames[f].push_back(event_sample(p, false, 0, z[f][p]));
    }
    return frames;
}

UTEST(lavery_core, background_mode_finds_local_activity_not_stronger_than_the_broad_event) {
    const auto z = background_z();
    const auto frames = background_samples(z);
    EventOptions options;
    options.neighbourhood = -1;
    options.burst_sigma = 3.5f;
    // Same |z| everywhere: the intensity hierarchy cannot separate the local swing.
    EXPECT_EQ(detect_events(frames, 10, event_steps(), options).size(), (size_t)1);
    options.residual_sigma = 3.5f;
    options.member_z = [&](size_t f, int cls, uint32_t m, int k) {
        return cls == Class_Pair && k == 0 ? z[f][m] : std::numeric_limits<float>::quiet_NaN();
    };
    const auto events = detect_events(frames, 10, event_steps(), options);
    ASSERT_EQ(events.size(), (size_t)2);
    EXPECT_EQ(events[0].parent, SIZE_MAX);
    EXPECT_FALSE(events[0].residual);
    EXPECT_EQ(events[1].parent, (size_t)0);
    EXPECT_TRUE(events[1].residual);
    EXPECT_EQ(events[1].burst_sigma, 3.5f);
    EXPECT_EQ(events[1].begin, (size_t)3);
    EXPECT_EQ(events[1].end, (size_t)6);
    EXPECT_EQ(events[1].pair_first, 6u);
    EXPECT_EQ(events[1].pair_last, 7u);
    // Reported deviations stay the raw z-scores, not residuals.
    EXPECT_EQ(events[1].severity, 3.0f);
}

UTEST(lavery_core, background_mode_ignores_uniform_shifts_and_small_regions) {
    auto z = background_z();
    for (auto& frame : z) std::fill(frame.begin(), frame.end(), 4.0f);
    auto frames = background_samples(z);
    EventOptions options;
    options.neighbourhood = -1;
    options.burst_sigma = 1;
    options.residual_sigma = 1;
    options.member_z = [&](size_t f, int cls, uint32_t m, int k) {
        return cls == Class_Pair && k == 0 ? z[f][m] : std::numeric_limits<float>::quiet_NaN();
    };
    EXPECT_EQ(detect_events(frames, 10, event_steps(), options).size(), (size_t)1);
    z = background_z();
    frames = background_samples(z);
    options.residual_sigma = 3.5f;
    options.background_min_members = 11; // More than the region holds: no background, no children.
    EXPECT_EQ(detect_events(frames, 10, event_steps(), options).size(), (size_t)1);
    options.background_min_members = 5;
    options.residual_sigma = std::numeric_limits<float>::quiet_NaN();
    EXPECT_TRUE(detect_events(frames, 10, event_steps(), options).empty());
}

UTEST(lavery_core, event_colors_prefer_high_scores) {
    std::vector<OutlierEvent> events(EVENT_COLOR_COUNT + 2);
    for (size_t i = 0; i < events.size(); ++i) {
        events[i].begin = i;
        events[i].end = 10;
        events[i].score = (float)i;
    }
    const auto slots = event_color_slots(events);
    for (size_t i = 0; i < events.size(); ++i) {
        if (i < 2) EXPECT_GE(slots[i], EVENT_COLOR_COUNT);
        else EXPECT_LT(slots[i], EVENT_COLOR_COUNT);
        for (size_t j = i + 1; j < events.size(); ++j) EXPECT_NE(slots[i], slots[j]);
    }
}

// Long-trajectory timing check: 100k frames, 24 pairs, 23 steps, sparse noise, one long
// whole-structure event with a local burst. Prints timings; asserts results only.
UTEST(lavery_core, benchmark_long_trajectory_event_detection) {
    const size_t frame_count = 100000;
    const uint32_t pairs = 24;
    std::vector<StepIndex> steps;
    for (uint32_t p = 0; p + 1 < pairs; ++p) steps.push_back({p, p + 1});
    std::vector<std::vector<MemberOutlier>> frames(frame_count);
    uint32_t seed = 12345;
    auto next = [&]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    for (size_t f = 0; f < frame_count; ++f) {
        if (next() % 50 == 0) frames[f].push_back(event_sample(next() % pairs, false, (int)(next() % 6), 2.6f));
        if (f >= 40000 && f < 60000) {
            for (uint32_t p = 0; p < pairs; ++p) {
                const bool local = f >= 50000 && f < 50200 && p >= 10 && p <= 12;
                frames[f].push_back(event_sample(p, false, 0, local ? -3.0f : 3.0f));
            }
        }
    }
    auto member_z = [](size_t f, int cls, uint32_t m, int k) {
        if (cls != Class_Pair || k != 0) return 0.0f;
        if (f >= 50000 && f < 50200 && m >= 10 && m <= 12) return -3.0f;
        return f >= 40000 && f < 60000 ? 3.0f : 0.0f;
    };
    EventOptions options;
    options.neighbourhood = -1;
    options.min_frames = 20;
    options.burst_sigma = 3.5f;
    auto start = std::chrono::steady_clock::now();
    const auto plain = detect_events(frames, pairs, steps, options);
    const double plain_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    options.residual_sigma = 3.5f;
    options.member_z = member_z;
    start = std::chrono::steady_clock::now();
    const auto residual = detect_events(frames, pairs, steps, options);
    const double residual_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    start = std::chrono::steady_clock::now();
    const auto slots = event_color_slots(residual);
    const double color_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    printf("lavery benchmark (100k frames): stronger %.1f ms, background %.1f ms, colours %.2f ms\n", plain_ms, residual_ms, color_ms);
    ASSERT_EQ(plain.size(), (size_t)1);
    EXPECT_EQ(plain[0].begin, (size_t)40000);
    ASSERT_EQ(residual.size(), (size_t)2);
    EXPECT_TRUE(residual[1].residual);
    EXPECT_EQ(residual[1].begin, (size_t)50000);
    EXPECT_EQ(residual[1].pair_first, 10u);
    EXPECT_EQ(residual[1].pair_last, 12u);
    EXPECT_EQ(slots.size(), residual.size());
}

UTEST(lavery_core, hierarchy_rejects_weak_cores_duplicates_and_short_bursts) {
    auto frames = hierarchy_samples();
    EventOptions options;
    options.burst_sigma = 5;
    EXPECT_EQ(detect_events(frames, 10, event_steps(), options).size(), (size_t)1);
    options.burst_sigma = 3.5f;
    options.min_frames = 4;
    EXPECT_EQ(detect_events(frames, 10, event_steps(), options).size(), (size_t)1);
    options.min_frames = 1;
    for (auto& frame : frames) for (auto& sample : frame) sample.z[0] = sample.z[2] = 5;
    // Raising the threshold must not duplicate an unchanged spatial component.
    EXPECT_EQ(detect_events(frames, 10, event_steps(), options).size(), (size_t)1);
}

UTEST(lavery_core, hierarchy_inherits_physical_duration_and_detection_range) {
    const auto frames = hierarchy_samples();
    EventOptions options;
    options.burst_sigma = 3.5f;
    options.times = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90};
    options.min_duration = 20;
    auto events = detect_events(frames, 10, event_steps(), options);
    ASSERT_EQ(events.size(), (size_t)2);
    options.min_duration = 20.001;
    EXPECT_EQ(detect_events(frames, 10, event_steps(), options).size(), (size_t)1);
    options.min_duration = 10;
    ASSERT_TRUE(event_time_range(&options, 30, 40));
    events = detect_events(frames, 10, event_steps(), options);
    ASSERT_EQ(events.size(), (size_t)2);
    for (const auto& event : events) {
        EXPECT_EQ(event.begin, (size_t)3);
        EXPECT_EQ(event.end, (size_t)4);
        EXPECT_TRUE(event.truncated_begin);
        EXPECT_TRUE(event.truncated_end);
    }
    options.min_duration = 10.001;
    EXPECT_TRUE(detect_events(frames, 10, event_steps(), options).empty());
}

UTEST(lavery_core, physical_time_conversion_keeps_ps_ns_detection_identical) {
    const auto frames = hierarchy_samples();
    EventOptions ps, ns;
    ps.burst_sigma = ns.burst_sigma = 3.5f;
    const double to_ps = md_unit_scale(md_unit_nanosecond()) / md_unit_scale(md_unit_picosecond());
    EXPECT_NEAR(to_ps, 1000.0, 1e-9);
    for (size_t f = 0; f < frames.size(); ++f) {
        ps.times.push_back(f * 1000.0);
        ns.times.push_back(f * to_ps);
    }
    ps.min_duration = ns.min_duration = 2000;
    ASSERT_TRUE(event_time_range(&ps, 3000, 8000));
    ASSERT_TRUE(event_time_range(&ns, 3 * to_ps, 8 * to_ps));
    const auto a = detect_events(frames, 10, event_steps(), ps);
    const auto b = detect_events(frames, 10, event_steps(), ns);
    ASSERT_EQ(a.size(), (size_t)2);
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].begin, b[i].begin);
        EXPECT_EQ(a[i].end, b[i].end);
        EXPECT_EQ(a[i].score, b[i].score);
        EXPECT_EQ(a[i].parent, b[i].parent);
    }
}

UTEST(lavery_core, frame_fallback_range_does_not_borrow_outside_samples) {
    std::vector<std::vector<MemberOutlier>> frames(5);
    for (auto& frame : frames) frame = {event_sample(0, false, 0, 3)};
    EventOptions options;
    options.first_frame = 1;
    options.last_frame = 3;
    options.min_frames = 3;
    auto events = detect_events(frames, 10, event_steps(), options);
    ASSERT_EQ(events.size(), (size_t)1);
    EXPECT_EQ(events[0].outlier_frames, (size_t)3);
    EXPECT_TRUE(events[0].truncated_begin);
    EXPECT_TRUE(events[0].truncated_end);
    options.min_frames = 4;
    EXPECT_TRUE(detect_events(frames, 10, event_steps(), options).empty());
    options.first_frame = 4;
    options.last_frame = 2;
    EXPECT_TRUE(detect_events(frames, 10, event_steps(), options).empty());
}

UTEST(lavery_core, hierarchy_keeps_children_local_under_whole_structure_and_remaps_parents) {
    std::vector<std::vector<MemberOutlier>> frames(10);
    for (size_t f = 0; f < frames.size(); ++f) {
        for (uint32_t p = 0; p < 10; ++p) frames[f].push_back(event_sample(p, false, 0, 2.7f));
    }
    frames[3][1] = event_sample(1, false, 1, 4);
    frames[3][8] = event_sample(8, false, 1, -5);
    EventOptions options;
    options.neighbourhood = -1;
    options.burst_sigma = 3.5f;
    const auto events = detect_events(frames, 10, event_steps(), options);
    ASSERT_EQ(events.size(), (size_t)3);
    EXPECT_EQ(events[1].parent, (size_t)0);
    EXPECT_EQ(events[2].parent, (size_t)0);
    EXPECT_EQ(events[1].pair_first, 1u);
    EXPECT_EQ(events[2].pair_first, 8u);

    // Another root starts before the first root's child. Parent IDs must survive sorting.
    frames = hierarchy_samples();
    frames[2].push_back(event_sample(8, false, 0, 2.7f));
    frames[3].push_back(event_sample(8, false, 0, 5));
    frames[3].push_back(event_sample(9, false, 0, 2.7f));
    options.neighbourhood = 2;
    const auto sorted = detect_events(frames, 10, event_steps(), options);
    ASSERT_EQ(sorted.size(), (size_t)4);
    EXPECT_EQ(sorted[0].parent, SIZE_MAX);
    EXPECT_EQ(sorted[1].parent, SIZE_MAX);
    EXPECT_EQ(sorted[2].parent, (size_t)0);
    EXPECT_EQ(sorted[3].parent, (size_t)1);
}

UTEST(lavery_core, hierarchy_preserves_lost_pairs_and_both_parameter_classes) {
    auto frames = hierarchy_samples();
    frames[4][2] = lost_pair(2);
    frames[3].push_back(event_sample(2, true, 2, -5));
    frames[4].push_back(event_sample(2, true, 2, -3.1f, false));
    EventOptions options;
    options.burst_sigma = 3.5f;
    options.lost_weight = 2.5f;
    const auto events = detect_events(frames, 10, event_steps(), options);
    ASSERT_EQ(events.size(), (size_t)2);
    EXPECT_EQ(events[1].parent, (size_t)0);
    EXPECT_EQ(events[1].lost_samples, (size_t)1);
    EXPECT_EQ(events[1].members.size(), (size_t)2);
    const auto feedback = event_contributions(events, 4);
    EXPECT_EQ(feedback.size(), (size_t)6);
    EXPECT_TRUE(feedback[2].lost);
    EXPECT_TRUE(feedback.back().cls);
}

UTEST(lavery_core, hierarchy_reports_invalid_options) {
    auto frames = hierarchy_samples();
    EventOptions options;
    options.burst_sigma = std::numeric_limits<float>::quiet_NaN();
    EXPECT_TRUE(detect_events(frames, 10, event_steps(), options).empty());
    options.burst_sigma = 3.5f;
    options.burst_neighbourhood = -1;
    EXPECT_TRUE(detect_events(frames, 10, event_steps(), options).empty());
}

UTEST(lavery_core, hierarchy_bridges_local_gaps_without_inventing_child_contributions) {
    auto frames = hierarchy_samples();
    frames[4][2] = event_sample(2, false, 0, 2.7f);
    EventOptions options;
    options.burst_sigma = 3.5f;
    // The last sample continues a child but cannot start a separate one.
    const auto separate = detect_events(frames, 10, event_steps(), options);
    ASSERT_EQ(separate.size(), (size_t)2);
    EXPECT_EQ(separate[1].begin, (size_t)3);
    EXPECT_EQ(separate[1].end, (size_t)3);
    options.max_gap = 1;
    const auto bridged = detect_events(frames, 10, event_steps(), options);
    ASSERT_EQ(bridged.size(), (size_t)2);
    EXPECT_EQ(bridged[1].begin, (size_t)3);
    EXPECT_EQ(bridged[1].end, (size_t)5);
    EXPECT_EQ(bridged[1].outlier_frames, (size_t)2);
    ASSERT_EQ(bridged[1].members.size(), (size_t)1);
    EXPECT_TRUE(event_member_sample(bridged[1].members[0], 4) == nullptr);
    const auto feedback = event_contributions(bridged, 4);
    ASSERT_EQ(feedback.size(), (size_t)5);
    EXPECT_EQ(feedback[2].parameters, 1u);
}

UTEST(lavery_core, hierarchy_never_attaches_children_across_separate_duplexes) {
    auto frames = hierarchy_samples();
    for (size_t f = 0; f < frames.size(); ++f) {
        for (uint32_t p = 5; p < 10; ++p) {
            frames[f].push_back(event_sample(p, false, 0, p == 8 && f == 3 ? 5.0f : 2.7f));
        }
    }
    EventOptions options;
    options.burst_sigma = 3.5f;
    options.neighbourhood = 100;
    const auto events = detect_events(frames, 10, event_steps(), options);
    ASSERT_EQ(events.size(), (size_t)4);
    EXPECT_EQ(events[2].parent, (size_t)0);
    EXPECT_EQ(events[3].parent, (size_t)1);
    for (const auto& child : events) {
        if (child.parent == SIZE_MAX) continue;
        const auto& parent = events[child.parent];
        EXPECT_GE(child.pair_first, parent.pair_first);
        EXPECT_LE(child.pair_last, parent.pair_last);
        for (const auto& member : child.members) {
            const auto it = std::find_if(parent.members.begin(), parent.members.end(), [&](const EventMember& m) {
                return m.cls == member.cls && m.member == member.member;
            });
            ASSERT_TRUE(it != parent.members.end());
            for (const auto& sample : member.samples) {
                const auto* source = event_member_sample(*it, sample.frame);
                ASSERT_TRUE(source != nullptr);
                EXPECT_EQ(sample.parameters & ~source->parameters, 0u);
            }
        }
    }
}

UTEST(lavery_core, event_colors_handle_inclusive_overlap_and_reuse_only_after_end) {
    std::vector<OutlierEvent> events(5);
    events[0].begin = 0; events[0].end = 3;
    events[1].begin = 1; events[1].end = 2;
    events[2].begin = 2; events[2].end = 4;
    events[3].begin = 3; events[3].end = 5;
    events[4].begin = 6; events[4].end = 6;
    const auto slots = event_color_slots(events);
    ASSERT_EQ(slots.size(), events.size());
    EXPECT_EQ(slots[0], (size_t)0);
    EXPECT_EQ(slots[1], (size_t)1);
    EXPECT_EQ(slots[2], (size_t)2);
    EXPECT_EQ(slots[3], (size_t)3);
    EXPECT_EQ(slots[4], (size_t)4);
    EXPECT_TRUE(slots == event_color_slots(events));
    EXPECT_TRUE(event_color_slots({}).empty());
}

UTEST(lavery_core, sequential_events_cycle_palette_in_chronological_not_score_order) {
    std::vector<OutlierEvent> events(2*EVENT_COLOR_COUNT+1);
    for (size_t i = 0; i < events.size(); ++i) {
        events[i].begin = events.size()-i;
        events[i].end = events[i].begin;
        events[i].score = (float)(i%3);
    }
    const auto slots = event_color_slots(events);
    for (size_t i = 0; i < events.size(); ++i) {
        EXPECT_EQ(slots[i], (events.size()-i-1)%EVENT_COLOR_COUNT);
        if (i) EXPECT_NE(slots[i], slots[i-1]);
    }
}

UTEST(lavery_core, event_colors_support_unsorted_input_and_many_simultaneous_events) {
    std::vector<OutlierEvent> events(20);
    for (size_t i = 0; i < events.size(); ++i) {
        events[i].begin = events.size() - i;
        events[i].end = 50;
    }
    const auto slots = event_color_slots(events);
    ASSERT_EQ(slots.size(), events.size());
    for (size_t i = 0; i < slots.size(); ++i) {
        EXPECT_EQ(slots[i], events.size() - i - 1);
        for (size_t j = i + 1; j < slots.size(); ++j) EXPECT_NE(slots[i], slots[j]);
    }
}

UTEST(lavery_core, automatic_feedback_includes_all_active_events_and_both_classes) {
    std::vector<std::vector<MemberOutlier>> frames(5);
    frames[0] = {event_sample(1, false, 0, 3), event_sample(6, true, 2, -4)};
    frames[2] = {event_sample(1, false, 4, 3), event_sample(6, true, 1, 4), lost_pair(4)};
    EventOptions options;
    options.max_gap = 1;
    const auto events = detect_events(frames, 10, event_steps(), options);
    ASSERT_EQ(events.size(), (size_t)3);
    const auto start = event_contributions(events, 0);
    ASSERT_EQ(start.size(), (size_t)2);
    EXPECT_FALSE(start[0].cls);
    EXPECT_EQ(start[0].parameters, 1u);
    EXPECT_TRUE(start[1].cls);
    EXPECT_EQ(start[1].parameters, 1u << 2);
    EXPECT_TRUE(event_contributions(events, 1).empty());
    const auto later = event_contributions(events, 2);
    ASSERT_EQ(later.size(), (size_t)3);
    EXPECT_EQ(later[0].parameters, 1u << 4);
    EXPECT_TRUE(later[1].lost);
    EXPECT_TRUE(later[2].cls);
    EXPECT_EQ(later[2].parameters, 1u << 1);
    EXPECT_TRUE(event_contributions(events, 3).empty());
}

UTEST(lavery_core, automatic_feedback_merges_overlapping_members_without_merging_classes) {
    OutlierEvent first;
    first.begin = 1;
    first.end = 3;
    EventMember pair;
    pair.member = 2;
    pair.samples = {{2, 1u, false}};
    first.members.push_back(pair);
    OutlierEvent second = first;
    second.members[0].samples[0].parameters = 1u << 4;
    EventMember step = pair;
    step.cls = Class_Step;
    second.members.push_back(step);
    const auto combined = event_contributions({first, second}, 2);
    ASSERT_EQ(combined.size(), (size_t)2);
    EXPECT_FALSE(combined[0].cls);
    EXPECT_EQ(combined[0].parameters, 1u | (1u << 4));
    EXPECT_TRUE(combined[1].cls);
    EXPECT_EQ(combined[1].parameters, 1u);
    EXPECT_TRUE(event_contributions({first, second}, 0).empty());
    EXPECT_TRUE(event_contributions({first, second}, 4).empty());
}

UTEST(lavery_core, events_need_a_strong_sample_and_weak_samples_extend_them) {
    std::vector<std::vector<MemberOutlier>> frames(6);
    frames[0] = {event_sample(0, false, 2, 2.2f, false)};
    frames[1] = {event_sample(0, false, 2, 3.5f)};
    frames[2] = {event_sample(0, false, 2, 2.1f, false)};
    frames[3] = {event_sample(0, false, 2, 2.3f, false)};
    frames[5] = {event_sample(6, false, 2, 2.4f, false)};
    const auto events = detect_events(frames, 10, event_steps());
    ASSERT_EQ(events.size(), (size_t)1);
    EXPECT_EQ(events[0].begin, (size_t)0);
    EXPECT_EQ(events[0].end, (size_t)3);
    EXPECT_EQ(events[0].peak, (size_t)1);
}

UTEST(lavery_core, lost_pairs_join_events_and_name_them) {
    std::vector<std::vector<MemberOutlier>> frames(5);
    frames[0] = {event_sample(2, false, 2, 4)};
    frames[1] = {lost_pair(2)};
    frames[2] = {lost_pair(2), event_sample(2, true, 5, 3)};
    frames[4] = {lost_pair(7)};
    EventOptions options;
    options.lost_weight = 2.5f;
    const auto events = detect_events(frames, 10, event_steps(), options);
    ASSERT_EQ(events.size(), (size_t)2);
    EXPECT_EQ(events[0].begin, (size_t)0);
    EXPECT_EQ(events[0].end, (size_t)2);
    EXPECT_EQ(events[0].lost_samples, (size_t)2);
    EXPECT_NEAR(events[0].score, 4 + 2.5f + 2.5f + 3, 1e-5f);
    ASSERT_EQ(events[0].members.size(), (size_t)2);
    EXPECT_EQ(events[0].members[1].lost_frames, (size_t)0);
    EXPECT_EQ(events[0].members[0].lost_frames, (size_t)2);
    EXPECT_EQ(events[0].members[0].frames, (size_t)3);
    EXPECT_TRUE(event_kind(events[0]) == "Pairing disruption + Opening (breathing / flip)");
    EXPECT_TRUE(event_kind(events[1]) == "Pairing disruption");
    EXPECT_EQ(events[1].peak, (size_t)4);
    EXPECT_EQ(events[1].members[0].peak_parameter, -1);
    std::vector<std::vector<MemberOutlier>> bad(1);
    bad[0] = {lost_pair(2)};
    bad[0][0].cls = Class_Step;
    EXPECT_TRUE(detect_events(bad, 10, event_steps()).empty());
}

UTEST(lavery_core, event_kind_follows_dominant_parameter_and_sign) {
    OutlierEvent event;
    EXPECT_EQ(event_dominant_metric(event), -1);
    EXPECT_TRUE(event_kind(event) == "");
    event.metric_score[Twist] = 10;
    event.metric_signed[Twist] = -10;
    event.metric_score[Roll] = 4;
    EXPECT_EQ(event_dominant_metric(event), (int)Twist);
    EXPECT_TRUE(event_kind(event) == "Unwinding");
    event.metric_signed[Twist] = 10;
    EXPECT_TRUE(event_kind(event) == "Overwinding");
    event.metric_score[Rise] = 20;
    event.metric_signed[Rise] = 20;
    EXPECT_TRUE(event_kind(event) == "Unstacking");
    event.metric_score[Roll] = 30;
    EXPECT_TRUE(event_kind(event) == "Kink (roll)");
}

UTEST(lavery_core, chance_probability_matches_gaussian_tails) {
    EXPECT_NEAR(chance_outlier_probability(1.959964f, 1), 0.05, 1e-5);
    EXPECT_NEAR(chance_outlier_probability(1.959964f, 6), 1 - std::pow(0.95, 6), 1e-5);
    EXPECT_NEAR(chance_outlier_probability(0, 1), 1.0, 1e-12);
    EXPECT_EQ(chance_outlier_probability(2.5f, 0), 0.0);
    EXPECT_EQ(chance_outlier_probability(std::numeric_limits<float>::quiet_NaN(), 3), 0.0);
}

UTEST(lavery_core, angular_statistics_wrap_across_half_turn) {
    MetricStatistics linear, angular;
    const std::vector<float> values = {179, -179, 178, -178, 180, -177};
    ASSERT_TRUE(compute_statistics(&linear, values, 1));
    ASSERT_TRUE(compute_statistics(&angular, values, 1, true));
    EXPECT_GT(linear.members[0].sd, 100.0);
    EXPECT_LT(angular.members[0].sd, 3.0);
    EXPECT_NEAR(angular.members[0].zscore(-179), angular.members[0].zscore(181), 1e-4f);
    EXPECT_FALSE(angular.members[0].is_outlier(-178, 2.5f));
    EXPECT_TRUE(angular.members[0].is_outlier(0, 2.5f));
    MetricStatistics plain, same;
    const std::vector<float> twist = {30, 35, 40, 33, 31};
    ASSERT_TRUE(compute_statistics(&plain, twist, 1));
    ASSERT_TRUE(compute_statistics(&same, twist, 1, true));
    EXPECT_EQ(plain.members[0].mean, same.members[0].mean);
    EXPECT_EQ(plain.members[0].sd, same.members[0].sd);
}

UTEST(lavery_core, median_mad_baseline_resists_large_excursions) {
    const std::vector<float> values = {10, 11, 9, 10, 11, 9, 10, 60};
    MetricStatistics classic, robust;
    ASSERT_TRUE(compute_statistics(&classic, values, 1));
    ASSERT_TRUE(compute_statistics(&robust, values, 1, false, Baseline_MedianMAD));
    EXPECT_FALSE(classic.members[0].is_outlier(60, 3));
    EXPECT_TRUE(robust.members[0].is_outlier(60, 3));
    EXPECT_EQ(robust.members[0].mean, 10.0);
    EXPECT_NEAR(robust.members[0].sd, 1.4826, 1e-9);
    MetricStatistics constant;
    ASSERT_TRUE(compute_statistics(&constant, {5, 5, 5, 5, 9}, 1, false, Baseline_MedianMAD));
    EXPECT_EQ(constant.members[0].mean, 5.0);
    EXPECT_GT(constant.members[0].sd, 0.0);
    EXPECT_FALSE(compute_statistics(&constant, {1}, 1, false, (Baseline)7));
}

UTEST(lavery_core, baseline_range_uses_only_reference_frames_but_scores_the_whole_matrix) {
    const std::vector<float> values = {1000,1000, 0,10, 1,12, 2,14, 8,30, 9,32};
    for (int method = 0; method < Baseline_Count; ++method) {
        MetricStatistics stats, full, explicit_full;
        ASSERT_TRUE(compute_statistics(&stats, values, 2, false, (Baseline)method, -1, 1, 4));
        ASSERT_TRUE(compute_statistics(&full, values, 2, false, (Baseline)method));
        ASSERT_TRUE(compute_statistics(&explicit_full, values, 2, false, (Baseline)method, -1, 0, 6));
        ASSERT_EQ(stats.heatmap.size(), values.size());
        EXPECT_NEAR(stats.members[0].mean, 1, 1e-9);
        EXPECT_NEAR(stats.members[1].mean, 12, 1e-9);
        const double scale = method == Baseline_MeanSD ? std::sqrt(2.0 / 3.0) : 1.4826;
        EXPECT_NEAR(stats.members[0].sd, scale, 1e-9);
        EXPECT_NEAR(stats.members[1].sd, 2 * scale, 1e-9);
        for (size_t m = 0; m < 2; ++m) {
            EXPECT_EQ(stats.members[m].count, (size_t)3);
            EXPECT_EQ(full.members[m].mean, explicit_full.members[m].mean);
            EXPECT_EQ(full.members[m].sd, explicit_full.members[m].sd);
            for (size_t f = 0; f < 6; ++f) {
                EXPECT_EQ(stats.heatmap[(1-m)*6+f], stats.members[m].zscore(values[f*2+m]));
                EXPECT_EQ(full.heatmap[(1-m)*6+f], explicit_full.heatmap[(1-m)*6+f]);
            }
        }
        EXPECT_TRUE(stats.members[0].is_outlier(8, 3));
        EXPECT_TRUE(stats.members[1].is_outlier(30, 3));
    }
}

UTEST(lavery_core, baseline_range_preserves_circular_and_robust_reference_conventions) {
    const std::vector<float> values = {0, 179,-179,178,-178, 0,0};
    const std::vector<float> reference(values.begin()+1, values.begin()+5);
    for (int method = 0; method < Baseline_Count; ++method) {
        MetricStatistics stats, cropped;
        ASSERT_TRUE(compute_statistics(&stats, values, 1, true, (Baseline)method, -1, 1, 5));
        ASSERT_TRUE(compute_statistics(&cropped, reference, 1, true, (Baseline)method));
        EXPECT_NEAR(std::fabs(stats.members[0].mean), 180, 1e-9);
        EXPECT_EQ(stats.members[0].mean, cropped.members[0].mean);
        EXPECT_EQ(stats.members[0].sd, cropped.members[0].sd);
        EXPECT_EQ(stats.members[0].count, (size_t)4);
        EXPECT_TRUE(stats.members[0].is_outlier(0, 3));
    }
}

UTEST(lavery_core, absent_reference_states_are_not_scored_or_counted_as_valid) {
    const std::vector<float> phase = {10,12,8, 160,165,170};
    MetricStatistics stats;
    ASSERT_TRUE(compute_statistics(&stats, phase, 1, true, Baseline_MeanSD, Phase, 0, 3));
    const auto& member = stats.members[0];
    EXPECT_EQ(member.count, (size_t)3);
    EXPECT_EQ(member.state, 0);
    EXPECT_EQ(member.state_fraction, 1.0f);
    EXPECT_EQ(member.reference(165).count, (size_t)0);
    EXPECT_TRUE(std::isnan(member.zscore(165)));
    EXPECT_FALSE(member.is_outlier(165, 3));
    const auto assessment = assess_outliers(&phase[4], &member, 1, 1, 3);
    EXPECT_EQ(assessment.valid, (size_t)0);
    EXPECT_EQ(assessment.contributors, (uint32_t)0);
    std::vector<FrameOutliers> counts;
    ASSERT_TRUE(compute_outlier_counts(&counts, phase, stats.members, 3));
    for (size_t f = 3; f < phase.size(); ++f) {
        EXPECT_TRUE(std::isnan(stats.heatmap[f]));
        EXPECT_EQ(counts[f].valid, (size_t)0);
    }
    EventOptions options;
    options.min_frames = 3;
    std::vector<StateSwitch> changes;
    ASSERT_TRUE(detect_state_switches(&changes, phase, 1, Phase, options));
    ASSERT_EQ(changes.size(), (size_t)1);
    EXPECT_EQ(changes[0].begin, (size_t)3);
    CsvSample sample;
    sample.metric = Phase;
    sample.value = 165;
    sample.baseline = member;
    EXPECT_TRUE(csv_sample(sample).find(",nan,nan,nan,") != std::string::npos);
}

UTEST(lavery_core, baseline_and_detection_intervals_are_independent_with_irregular_times) {
    const std::vector<float> values = {0,1,2, 8,9,10};
    EventOptions reference, detection;
    reference.times = detection.times = {0,2,5,9,14,20};
    ASSERT_TRUE(event_time_range(&reference, 0, 5, "baseline"));
    ASSERT_TRUE(event_time_range(&detection, 9, 20));
    MetricStatistics stats;
    ASSERT_TRUE(compute_statistics(&stats, values, 1, false, Baseline_MeanSD, -1,
        reference.first_frame, reference.last_frame+1));
    std::vector<std::vector<MemberOutlier>> frames(values.size());
    for (size_t f = 0; f < values.size(); ++f) {
        if (stats.members[0].is_outlier(values[f], 3))
            frames[f].push_back(event_sample(0, Class_Pair, 0, stats.members[0].zscore(values[f])));
    }
    detection.min_frames = 1;
    detection.min_duration = 10;
    const auto events = detect_events(frames, 1, {}, detection);
    ASSERT_EQ(events.size(), (size_t)1);
    EXPECT_EQ(events[0].begin, (size_t)3);
    EXPECT_EQ(events[0].end, (size_t)5);
    EXPECT_EQ(stats.members[0].count, (size_t)3);
    EXPECT_EQ(stats.members[0].mean, 1.0);
    ASSERT_TRUE(event_time_range(&detection, 2, 5));
    EXPECT_TRUE(detect_events(frames, 1, {}, detection).empty());
    EXPECT_EQ(stats.members[0].mean, 1.0);
}

UTEST(lavery_core, timeline_periods_supply_independent_reference_and_detection_frames) {
    TimelinePeriods periods = {};
    TimelinePeriod* reference = timeline_periods_add(&periods, "Equilibrium", 2, 0, false);
    TimelinePeriod* search = timeline_periods_add(&periods, "Heating", 3, 5, false);
    ASSERT_TRUE(reference != nullptr);
    ASSERT_TRUE(search != nullptr);
    const uint32_t reference_id = reference->id, search_id = search->id;
    const std::vector<float> values = {0,1,2, 8,9,10};
    const std::vector<double> times = {0,2,5,9,14,20};
    int first = -1, last = -1;
    ASSERT_TRUE(timeline_period_frames(&first, &last, *timeline_periods_find(&periods, reference_id), 0, 6));
    EXPECT_EQ(first, 0);
    EXPECT_EQ(last, 2);
    MetricStatistics stats;
    ASSERT_TRUE(compute_statistics(&stats, values, 1, false, Baseline_MeanSD, -1, first, last+1));
    EXPECT_EQ(stats.members[0].mean, 1.0);
    std::vector<std::vector<MemberOutlier>> frames(values.size());
    for (size_t f = 0; f < values.size(); ++f) {
        if (stats.members[0].is_outlier(values[f], 3))
            frames[f].push_back(event_sample(0, Class_Pair, 0, stats.members[0].zscore(values[f])));
    }
    ASSERT_TRUE(timeline_period_frames(&first, &last, *timeline_periods_find(&periods, search_id), 0, 6));
    EventOptions options;
    options.times = times;
    options.first_frame = first;
    options.last_frame = last;
    options.min_frames = 1;
    options.min_duration = 10;
    auto events = detect_events(frames, 1, {}, options);
    ASSERT_EQ(events.size(), (size_t)1);
    EXPECT_EQ(events[0].begin, (size_t)3);
    EXPECT_EQ(events[0].end, (size_t)5);
    EXPECT_EQ(times[first], 9.0);
    EXPECT_EQ(times[last], 20.0);
    search->end = 4;
    ASSERT_TRUE(timeline_period_frames(&first, &last, *search, 0, 6));
    options.last_frame = last;
    EXPECT_TRUE(detect_events(frames, 1, {}, options).empty());
    reference->end = 1;
    ASSERT_TRUE(timeline_period_frames(&first, &last, *reference, 0, 6));
    ASSERT_TRUE(compute_statistics(&stats, values, 1, false, Baseline_MeanSD, -1, first, last+1));
    EXPECT_EQ(stats.members[0].mean, 0.5);
    EXPECT_EQ(stats.members[0].count, (size_t)2);
    timeline_periods_remove(&periods, reference_id);
    EXPECT_TRUE(timeline_periods_find(&periods, reference_id) == nullptr);
    EXPECT_TRUE(timeline_periods_find(&periods, search_id) != nullptr);
}

UTEST(lavery_core, following_period_references_use_native_clamping_and_current_frame) {
    TimelinePeriods periods = {};
    TimelinePeriod* period = timeline_periods_add(&periods, "Recent", -2, 0, true);
    ASSERT_TRUE(period != nullptr);
    const std::vector<float> values = {0,2,4,6,8,10};
    int first = -1, last = -1;
    MetricStatistics stats;
    ASSERT_TRUE(timeline_period_frames(&first, &last, *period, 1, 6));
    EXPECT_EQ(first, 0);
    EXPECT_EQ(last, 1);
    ASSERT_TRUE(compute_statistics(&stats, values, 1, false, Baseline_MeanSD, -1, first, last+1));
    EXPECT_EQ(stats.members[0].mean, 1.0);
    ASSERT_TRUE(timeline_period_frames(&first, &last, *period, 4, 6));
    EXPECT_EQ(first, 2);
    EXPECT_EQ(last, 4);
    ASSERT_TRUE(compute_statistics(&stats, values, 1, false, Baseline_MeanSD, -1, first, last+1));
    EXPECT_EQ(stats.members[0].mean, 6.0);
    period->follow = false;
    period->beg = period->end = 3;
    ASSERT_TRUE(timeline_period_frames(&first, &last, *period, 0, 6));
    ASSERT_TRUE(compute_statistics(&stats, values, 1, false, Baseline_MeanSD, -1, first, last+1));
    EXPECT_EQ(stats.members[0].count, (size_t)1);
    EXPECT_EQ(stats.members[0].sd, 0.0);
    period->beg = 8;
    period->end = 10;
    EXPECT_FALSE(timeline_period_frames(&first, &last, *period, 0, 6));
    EXPECT_FALSE(timeline_period_frames(&first, &last, *period, 0, 0));
}

UTEST(lavery_core, empty_or_invalid_baseline_ranges_do_not_borrow_samples) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    MetricStatistics stats;
    ASSERT_TRUE(compute_statistics(&stats, {1,2,3}, 1, false, Baseline_MeanSD, -1, 1, 1));
    EXPECT_EQ(stats.members[0].count, (size_t)0);
    for (float z : stats.heatmap) EXPECT_TRUE(std::isnan(z));
    ASSERT_TRUE(compute_statistics(&stats, {1,nan,3}, 1, false, Baseline_MeanSD, -1, 1, 2));
    EXPECT_TRUE(std::isnan(stats.members[0].zscore(3)));
    EXPECT_FALSE(compute_statistics(&stats, {1,2,3}, 1, false, Baseline_MeanSD, -1, 2, 1));
    EXPECT_FALSE(compute_statistics(&stats, {1,2,3}, 1, false, Baseline_MeanSD, -1, 0, 4));
    EventOptions options;
    options.times = {0,2,5};
    EXPECT_FALSE(event_time_range(&options, 2.1, 4.9, "baseline"));
    EXPECT_FALSE(event_time_range(&options, 5, 2, "baseline"));
}

UTEST(lavery_core, pooled_history_detects_persistent_local_rise) {
    std::vector<float> values(12 * 20, 5.0f);
    for (size_t f = 0; f < 20; ++f) values[f * 12 + 4] = 7.0f;
    for (int method = 0; method < Baseline_Count; ++method) {
        MetricStatistics own, pooled;
        ASSERT_TRUE(compute_statistics(&own, values, 12, false, (Baseline)method));
        ASSERT_TRUE(compute_statistics(&pooled, values, 12, false, (Baseline)method, -1, 0, SIZE_MAX,
            std::vector<uint32_t>(12, 0)));
        EXPECT_FALSE(own.members[4].is_outlier(7, 2.5f));
        EXPECT_TRUE(pooled.members[4].is_outlier(7, 2.5f));
        EXPECT_FALSE(pooled.members[0].is_outlier(5, 2.5f));
        EXPECT_EQ(pooled.members[4].count, (size_t)240);
        EXPECT_EQ(pooled.heatmap.size(), values.size());
        for (size_t f = 0; f < 20; ++f) {
            EXPECT_NEAR(pooled.heatmap[(12-1-4)*20+f], pooled.members[4].zscore(7), 1e-6);
        }
        std::vector<FrameOutliers> counts;
        ASSERT_TRUE(compute_outlier_counts(&counts, values, pooled.members, 2.5f));
        for (const auto& frame : counts) EXPECT_EQ(frame.outliers, (size_t)1);
        std::vector<std::vector<MemberOutlier>> samples(20);
        for (size_t f = 0; f < samples.size(); ++f) {
            MemberOutlier sample;
            sample.cls = Class_Step;
            sample.member = 4;
            sample.contributors = sample.strong = 1u << (Rise - Tilt);
            sample.z[Rise - Tilt] = pooled.members[4].zscore(7);
            samples[f].push_back(sample);
        }
        std::vector<StepIndex> steps;
        for (uint32_t p = 0; p < 12; ++p) steps.push_back({p,p+1});
        EventOptions options;
        options.min_frames = 3;
        const auto regions = detect_events(samples, 13, steps, options);
        ASSERT_EQ(regions.size(), (size_t)1);
        EXPECT_EQ(regions[0].begin, (size_t)0);
        EXPECT_EQ(regions[0].end, (size_t)19);
        EXPECT_EQ(regions[0].pair_first, (uint32_t)4);
        EXPECT_EQ(regions[0].pair_last, (uint32_t)5);
    }
}

UTEST(lavery_core, pooled_history_respects_helix_range_and_missing_values) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::vector<float> values = {999,999,999, 1,3,100, 3,nan,102, 999,999,999};
    for (int method = 0; method < Baseline_Count; ++method) {
        MetricStatistics pooled, expected;
        ASSERT_TRUE(compute_statistics(&pooled, values, 3, false, (Baseline)method, -1, 1, 3, {0,0,1}));
        ASSERT_TRUE(compute_statistics(&expected, {1,3,3}, 1, false, (Baseline)method));
        EXPECT_EQ(pooled.members[0].count, (size_t)3);
        EXPECT_NEAR(pooled.members[0].mean, expected.members[0].mean, 1e-8);
        EXPECT_NEAR(pooled.members[1].sd, expected.members[0].sd, 1e-8);
        EXPECT_NEAR(pooled.members[2].mean, 101, 1e-8);
        EXPECT_EQ(pooled.members[2].count, (size_t)2);
        EXPECT_TRUE(std::isnan(pooled.heatmap[4+2]));
        EXPECT_TRUE(pooled.members[0].is_outlier(999, 2.5f));
        ASSERT_TRUE(compute_statistics(&pooled, values, 3, false, (Baseline)method, -1, 1, 1, {0,0,1}));
        for (const auto& stats : pooled.members) EXPECT_EQ(stats.count, (size_t)0);
        for (float z : pooled.heatmap) EXPECT_TRUE(std::isnan(z));
        ASSERT_TRUE(compute_statistics(&pooled, {nan,3,nan,5}, 2, false, (Baseline)method, -1, 0, SIZE_MAX, {0,0}));
        EXPECT_EQ(pooled.members[0].count, (size_t)2);
        EXPECT_TRUE(std::isfinite(pooled.members[0].zscore(6)));
        EXPECT_TRUE(std::isnan(pooled.heatmap[2]));
        ASSERT_TRUE(compute_statistics(&pooled, {nan,7,nan,7}, 2, false, (Baseline)method, -1, 0, SIZE_MAX, {0,1}));
        EXPECT_EQ(pooled.members[0].count, (size_t)0);
        EXPECT_TRUE(std::isnan(pooled.members[0].zscore(6)));
        EXPECT_EQ(pooled.members[1].count, (size_t)2);
        EXPECT_EQ(pooled.members[1].zscore(8), 0.0f);
        ASSERT_TRUE(compute_statistics(&pooled, {7}, 1, false, (Baseline)method, -1, 0, SIZE_MAX, {0}));
        EXPECT_EQ(pooled.members[0].count, (size_t)1);
        EXPECT_EQ(pooled.members[0].zscore(8), 0.0f);
    }
    MetricStatistics invalid;
    EXPECT_FALSE(compute_statistics(&invalid, values, 3, false, Baseline_MeanSD, -1, 0, SIZE_MAX, {0,0}));
    EXPECT_FALSE(compute_statistics(&invalid, values, 3, false, Baseline_MeanSD, -1, 0, SIZE_MAX, {0,0,3}));
}

UTEST(lavery_core, pooled_history_preserves_circular_and_state_references) {
    for (int method = 0; method < Baseline_Count; ++method) {
        MetricStatistics circular, state;
        ASSERT_TRUE(compute_statistics(&circular, {179,-179,178,-178}, 2, true, (Baseline)method, -1,
            0, SIZE_MAX, {0,0}));
        EXPECT_LT(std::fabs(circular.members[0].deviation(180)), 2.0);
        EXPECT_LT(circular.members[0].sd, 3.0);
        ASSERT_TRUE(compute_statistics(&state, {10,170,20,160,200,190}, 2, true, (Baseline)method, Phase,
            0, 2, {0,0}));
        EXPECT_EQ(state.members[0].reference(10).count, (size_t)2);
        EXPECT_EQ(state.members[1].reference(170).count, (size_t)2);
        EXPECT_NEAR(state.members[0].reference(10).mean, 15, 1e-6);
        EXPECT_NEAR(state.members[1].reference(170).mean, 165, 1e-6);
    }
}

UTEST(lavery_core, pooled_groups_follow_helix_topology_for_every_class) {
    const std::vector<StepIndex> steps = {{0,1},{1,2},{4,5}};
    const auto pairs = helix_member_groups(6, steps, Class_Pair);
    ASSERT_EQ(pairs.size(), (size_t)6);
    EXPECT_EQ(pairs[0], pairs[2]);
    EXPECT_NE(pairs[0], pairs[4]);
    EXPECT_NE(pairs[3], pairs[4]);
    EXPECT_NE(pairs[3], pairs[0]);
    const auto step = helix_member_groups(6, steps, Class_Step);
    ASSERT_EQ(step.size(), (size_t)3);
    EXPECT_EQ(step[0], step[1]);
    EXPECT_NE(step[1], step[2]);
    EXPECT_LT(step[2], (uint32_t)step.size());
    const auto nt = helix_member_groups(6, steps, Class_Nucleotide);
    ASSERT_EQ(nt.size(), (size_t)12);
    for (size_t p = 0; p < pairs.size(); ++p) {
        EXPECT_EQ(nt[2*p], pairs[p]);
        EXPECT_EQ(nt[2*p+1], pairs[p]);
    }
    EXPECT_TRUE(helix_member_groups(0, {}, Class_Pair).empty());
}

UTEST(lavery_core, event_csv_rows_identify_member_and_parameter) {
    CsvEventRow row;
    row.run = "run1";
    row.event = 2;
    row.begin = 4;
    row.end = 9;
    row.begin_time = 40;
    row.end_time = 90;
    row.time_unit = "ps";
    row.peak = 6;
    row.severity = 4.5f;
    row.score = 12.25f;
    row.kind = "Unwinding";
    row.pair_first = 7;
    row.pair_last = 9;
    row.scope = "all";
    row.baseline = "median_mad";
    row.sigma = 2.5f;
    row.end_sigma = 2;
    row.neighbourhood = 2;
    row.member_lost = 1;
    row.metric = Twist;
    row.member = 7;
    row.label = "7-8: \"x\"";
    row.member_frames = 3;
    row.contributing = true;
    row.parameter_peak = 6;
    row.parameter_z = -4.5f;
    row.value = 21.5f;
    row.baseline_begin = 5;
    row.baseline_end = 25;
    const std::string text = csv_event_row(row);
    EXPECT_STREQ(text.c_str(),
        "\"run1\",2,4,9,40,90,\"ps\",6,4.5,12.25,\"Unwinding\",7,9,\"all\",\"median_mad\",2.5,2,2,step,7,\"7-8: \"\"x\"\"\",3,1,step/twist,deg,1,6,-4.5,21.5,0,0,0,0,0,0,0,0,,5,25,own_history,\"current_trajectory\"\n");
    size_t header = 1, fields = 1;
    for (const char* p = csv_event_header(); *p; ++p) if (*p == ',') ++header;
    row.label = "plain";
    for (char c : csv_event_row(row)) if (c == ',') ++fields;
    EXPECT_EQ(header, fields);
    EXPECT_EQ(header, (size_t)42);
    row.parent_event = 1;
    row.burst_sigma = 3.5f;
    row.truncated_begin = true;
    row.min_duration = 20;
    row.max_gap_duration = 5;
    row.detection_begin = 10;
    row.detection_end = 100;
    const auto nested = csv_event_row(row);
    EXPECT_TRUE(nested.find(",1,3.5,1,0,20,5,10,100,stronger,5,25,own_history,\"current_trajectory\"\n") != std::string::npos);
    row.residual = true;
    EXPECT_TRUE(csv_event_row(row).find(",100,background,5,25,own_history,\"current_trajectory\"\n") != std::string::npos);
    row.reference_scope = Reference_HelixHistory;
    EXPECT_TRUE(csv_event_row(row).find(",helix_history,\"current_trajectory\"\n") != std::string::npos);
}

UTEST(lavery_core, csv_fields_preserve_quotes_commas_and_newlines) {
    const auto label = csv_quote("S1:DA5 / S2:DT6");
    const auto special = csv_quote("a,\"b\"\nc");
    const auto empty = csv_quote("");
    EXPECT_STREQ(label.c_str(), "\"S1:DA5 / S2:DT6\"");
    EXPECT_STREQ(special.c_str(), "\"a,\"\"b\"\"\nc\"");
    EXPECT_STREQ(empty.c_str(), "\"\"");
}

UTEST(lavery_core, csv_sample_has_reproducible_values_quality_baseline_and_flags) {
    CsvSample sample;
    sample.run = "run1";
    sample.frame = 3;
    sample.time = 60;
    sample.time_unit = "ps";
    sample.member = 2;
    sample.label = "pair2";
    sample.pair_a = "S1:DA2 / S2:DT9";
    sample.metric = Stretch;
    sample.value = 8;
    sample.baseline = {2, 2, 4};
    sample.sigma = 2;
    sample.selected_mask = 16;
    sample.member_outlier = true;
    sample.baseline_begin = 10;
    sample.baseline_end = 40;
    const auto row = csv_sample(sample);
    EXPECT_STREQ(row.c_str(), "\"run1\",3,60,\"ps\",bp,2,\"pair2\",\"S1:DA2 / S2:DT9\",\"\",bp/stretch,Angstrom,8,1,0,\"Good\",2,2,3,2,1,1,1,16,mean_sd,10,40,own_history,\"current_trajectory\"\n");
    size_t columns = 1;
    for (const char* p = csv_header(); *p; ++p) if (*p == ',') ++columns;
    EXPECT_EQ(columns, (size_t)28);
    sample.reference_scope = Reference_HelixHistory;
    EXPECT_TRUE(csv_sample(sample).find(",helix_history,\"current_trajectory\"\n") != std::string::npos);
    sample.value = std::numeric_limits<float>::quiet_NaN();
    sample.quality = Quality_NonfiniteCoordinates | Quality_InvalidBase;
    const auto missing = csv_sample(sample);
    EXPECT_TRUE(missing.find("Nonfinite atomic coordinates") != std::string::npos);
    EXPECT_TRUE(missing.find(",nan,0,") != std::string::npos);
    EXPECT_TRUE(missing.find(",2,2,nan,2,1,0,1,16") != std::string::npos);
}

UTEST(lavery_core, detects_geometric_pairs_once_and_in_component_order) {
    const BaseFrame bases[] = {
        base(0, 0, {0, 0, 0}),
        base(1, 0, {0, 0, 3.4f}),
        base(2, 1, {5.5f, 0, 3.4f}),
        base(3, 1, {5.5f, 0, 0}),
    };
    Pair pairs[4] = {};
    const size_t count = detect_pairs(pairs, 4, bases, 4);
    ASSERT_EQ(count, (size_t)2);
    ASSERT_EQ(pairs[0].a, (uint32_t)0);
    ASSERT_EQ(pairs[0].b, (uint32_t)3);
    ASSERT_EQ(pairs[1].a, (uint32_t)1);
    ASSERT_EQ(pairs[1].b, (uint32_t)2);
}

UTEST(lavery_core, nucleosome_uses_all_backbone_bases_and_consecutive_pairs) {
    auto* alloc = md_arena_allocator_create(md_get_heap_allocator(), MEGABYTES(16));
    md_system_t sys = {.alloc = alloc};
    md_system_state_t state = {.alloc = alloc};
    ASSERT_TRUE(md_gro_system_init_from_file(&sys, &state, STR_LIT(VIAMD_TEST_DATA_DIR "/nucl-dna.gro")));
    ASSERT_TRUE(md_util_system_infer(&sys, &state, MD_UTIL_INFER_ALL));
    for (size_t i = 0; i < sys.atom.count; ++i) sys.atom.flags[i] &= ~MD_ATOM_FLAG_NUCLEOBASE;
    auto bases = gather_bases(sys);
    ASSERT_EQ(bases.size(), (size_t)294);
    std::vector<BaseFrame> frames;
    ASSERT_TRUE(extract_base_frames(frames, sys, state, bases));
    std::vector<Pair> pairs(bases.size());
    pairs.resize(detect_pairs(pairs.data(), pairs.size(), frames.data(), frames.size()));
    ASSERT_EQ(pairs.size(), (size_t)147);
    for (const Pair& pair : pairs) EXPECT_EQ(pair.a + pair.b, (uint32_t)293);
    const auto steps = gather_steps(sys, bases, pairs.data(), pairs.size());
    EXPECT_EQ(steps.size(), (size_t)146);
    std::vector<BasePairFrame> pair_frames(pairs.size());
    for (size_t p = 0; p < pairs.size(); ++p) {
        Values values = {};
        uint16_t quality = 0;
        ASSERT_TRUE(compute_present_base_pair(&values, &pair_frames[p], &quality,
            frames[pairs[p].a], frames[pairs[p].b], &state.unitcell));
        EXPECT_EQ(quality, (uint16_t)Quality_Good);
        for (int k = 0; k < 6; ++k) {
            EXPECT_NEAR(values.metric[k], curves_bp[p][k], k < 3 ? 0.06f : 0.006f);
        }
    }
    for (size_t s = 0; s < steps.size(); ++s) {
        Values values = {};
        ASSERT_TRUE(compute_step(&values, pair_frames[steps[s].a], pair_frames[steps[s].b]));
        for (int k = 0; k < 6; ++k) {
            EXPECT_NEAR(values.metric[Tilt + k], curves_step[s][k], k < 3 ? 0.06f : 0.006f);
        }
    }
    md_arena_allocator_destroy(alloc);
}

UTEST(lavery_core, steps_require_adjacent_bases_on_both_strands) {
    md_system_t sys = {};
    std::vector<BaseTopology> bases(4);
    bases[0].strand = bases[1].strand = 0;
    bases[2].strand = bases[3].strand = 1;
    bases[0].chain_order = 0;
    bases[1].chain_order = 1;
    bases[2].chain_order = 2;
    bases[3].chain_order = 0;
    const Pair pairs[] = {{0, 3}, {1, 2}};
    EXPECT_EQ(gather_steps(sys, bases, pairs, 2).size(), (size_t)0);
    bases[2].chain_order = 1;
    EXPECT_EQ(gather_steps(sys, bases, pairs, 2).size(), (size_t)1);
}

UTEST(lavery_core, pairing_uses_pairwise_periodic_images) {
    const BaseFrame bases[] = {
        base(0, 0, {1, 0, 0}),
        base(1, 1, {25.5f, 0, 0}),
    };
    const md_unitcell_t cell = md_unitcell_from_extent(30, 30, 30);
    Pair pairs[2] = {};
    EXPECT_EQ(detect_pairs(pairs, 2, bases, 2), (size_t)0);
    EXPECT_EQ(detect_pairs(pairs, 2, bases, 2, &cell), (size_t)1);
}

UTEST(lavery_core, computes_zero_step_rotation_and_rise) {
    const BasePairFrame a = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const BasePairFrame b = {{0, 0, 3.4f}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    Values values = {};
    ASSERT_TRUE(compute_step(&values, a, b));
    ASSERT_NEAR(values.metric[Rise], 3.4f, 1.0e-5f);
    ASSERT_NEAR(values.metric[Tilt], 0.0f, 1.0e-5f);
    ASSERT_NEAR(values.metric[Roll], 0.0f, 1.0e-5f);
    ASSERT_NEAR(values.metric[Twist], 0.0f, 1.0e-5f);
}

UTEST(lavery_core, computes_pair_separation_as_stretch) {
    const BaseFrame a = base(0, 0, {0, 0, 0}, {0, 1, 0}, {-1, 0, 0}, {0, 0, 1});
    const BaseFrame b = base(1, 1, {10, 0, 0}, {0, 1, 0}, {1, 0, 0}, {0, 0, -1});
    Values values = {};
    BasePairFrame pair_frame = {};
    ASSERT_TRUE(compute_base_pair(&values, &pair_frame, a, b));
    ASSERT_NEAR(values.metric[Stretch], 10.0f, 1.0e-5f);
    ASSERT_NEAR(values.metric[Shear], 0.0f, 1.0e-5f);
}

UTEST(lavery_core, compound_rotation_uses_axis_angle_and_midpoint_translation) {
    const float radians = 0.7f;
    const vec3_t axis = vec3_normalize(vec3_t{1, 2, 3});
    const quat_t q = quat_axis_angle(axis, radians);
    const quat_t half = quat_axis_angle(axis, radians * 0.5f);
    const BasePairFrame a = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const BasePairFrame b = {quat_mul_vec3(half, {1, 2, 3}),
        quat_mul_vec3(q, a.x), quat_mul_vec3(q, a.y), quat_mul_vec3(q, a.z)};
    Values values;
    ASSERT_TRUE(compute_step(&values, a, b));
    for (int k = 0; k < 3; ++k) {
        EXPECT_NEAR(values.metric[Tilt + k], axis.elem[k] * radians * 57.295779513f, 0.002f);
        EXPECT_NEAR(values.metric[Shift + k], (float)(k + 1), 0.0001f);
    }
}

UTEST(lavery_core, singular_and_improper_frames_are_rejected) {
    BasePairFrame a = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    BasePairFrame b = a;
    b.y = {0, -1, 0};
    Values values;
    EXPECT_FALSE(compute_step(&values, a, b));
    b.z = {0, 0, -1};
    EXPECT_FALSE(compute_step(&values, a, b));
}

UTEST(lavery_core, separated_or_invalid_fixed_partners_are_not_present) {
    const BaseFrame a = base(0, 0, {0, 0, 0});
    BaseFrame b = base(1, 1, {5.5f, 0, 0});
    EXPECT_TRUE(pair_is_present(a, b));
    b.center = {15, 0, 0};
    EXPECT_FALSE(pair_is_present(a, b));
    b = {};
    EXPECT_FALSE(pair_is_present(a, b));
}

UTEST(lavery_core, rigid_world_motion_does_not_change_parameters) {
    const BasePairFrame a = {{1, 2, 3}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const quat_t local = quat_axis_angle(vec3_normalize(vec3_t{1, 2, 3}), 0.8f);
    const BasePairFrame b = {{2, 4, 7}, quat_mul_vec3(local, a.x),
        quat_mul_vec3(local, a.y), quat_mul_vec3(local, a.z)};
    const quat_t world = quat_axis_angle(vec3_normalize(vec3_t{-2, 1, 4}), 1.2f);
    auto moved = [&](const BasePairFrame& f) -> BasePairFrame {
        return {vec3_add(quat_mul_vec3(world, f.origin), {30, -40, 50}),
                quat_mul_vec3(world, f.x), quat_mul_vec3(world, f.y), quat_mul_vec3(world, f.z)};
    };
    Values before, after;
    ASSERT_TRUE(compute_step(&before, a, b));
    ASSERT_TRUE(compute_step(&after, moved(a), moved(b)));
    for (uint32_t k = Tilt; k <= Rise; ++k) EXPECT_NEAR(before.metric[k], after.metric[k], 0.002f);
    auto strand1 = [](const BasePairFrame& f) -> BaseFrame { return {f.origin, f.x, f.y, f.z}; };
    auto strand2 = [](const BasePairFrame& f) -> BaseFrame {
        return {f.origin, f.x, vec3_mul1(f.y, -1), vec3_mul1(f.z, -1)};
    };
    BasePairFrame midpoint;
    ASSERT_TRUE(compute_base_pair(&before, &midpoint, strand1(a), strand2(b)));
    ASSERT_TRUE(compute_base_pair(&after, &midpoint, strand1(moved(a)), strand2(moved(b))));
    for (uint32_t k = Buckle; k < Tilt; ++k) EXPECT_NEAR(before.metric[k], after.metric[k], 0.002f);
}

UTEST(lavery_core, tiny_rotations_remain_continuous) {
    const quat_t world = quat_axis_angle(vec3_normalize(vec3_t{-1, 2, -3}), 3.14159265358979f);
    const BasePairFrame a = {{0, 0, 0}, quat_mul_vec3(world, {1, 0, 0}),
        quat_mul_vec3(world, {0, 1, 0}), quat_mul_vec3(world, {0, 0, 1})};
    const quat_t tiny = quat_axis_angle(a.z, 1.0e-5f);
    BasePairFrame b = {a.origin, quat_mul_vec3(tiny, a.x), quat_mul_vec3(tiny, a.y), a.z};
    Values values;
    ASSERT_TRUE(compute_step(&values, a, b));
    EXPECT_NEAR(values.metric[Twist], 1.0e-5f * 57.295779513f, 0.00001f);
    EXPECT_NEAR(values.metric[Tilt], 0.0f, 0.00001f);
    EXPECT_NEAR(values.metric[Roll], 0.0f, 0.00001f);
}

UTEST(lavery_core, names_are_suitable_for_attribute_and_script_paths) {
    ASSERT_STREQ(metric_name(Rise), "Rise");
    ASSERT_STREQ(metric_path(Rise), "step/rise");
    ASSERT_TRUE(metric_is_step(Rise));
    ASSERT_TRUE(metric_is_angular(Twist));
    ASSERT_FALSE(metric_is_angular(Slide));
}

UTEST(lavery_core, member_classes_partition_metrics) {
    int next = 0;
    for (int cls = 0; cls < Class_Count; ++cls) {
        EXPECT_EQ(class_first_metric(cls), next);
        EXPECT_LE(class_parameter_count(cls), MAX_CLASS_PARAMETERS);
        for (int k = 0; k < class_parameter_count(cls); ++k) EXPECT_EQ((int)metric_class((Metric)(next + k)), cls);
        next += class_parameter_count(cls);
    }
    EXPECT_EQ(next, (int)Metric_Count);
    EXPECT_STREQ(metric_path(MinorGroove), "step/minor_groove");
    EXPECT_STREQ(metric_path(Phase), "nt/pucker_phase");
    EXPECT_STREQ(class_csv_name(Class_Nucleotide), "nt");
    EXPECT_TRUE(metric_is_step(MajorGroove));
    EXPECT_FALSE(metric_is_step(Alpha));
    EXPECT_TRUE(metric_is_angular(Chi));
    EXPECT_TRUE(metric_is_angular(EpsilonZeta));
    EXPECT_FALSE(metric_is_angular(Amplitude));
    EXPECT_FALSE(metric_is_angular(MinorGroove));
}

UTEST(lavery_core, torsion_follows_iupac_sign_and_minimum_image) {
    const vec3_t a = {1, 0, 0}, b = {0, 0, 0}, c = {0, 1, 0};
    EXPECT_NEAR(torsion(a, b, c, vec3_t{1, 1, 0}), 0.0f, 1e-4f);
    EXPECT_NEAR(std::fabs(torsion(a, b, c, vec3_t{-1, 1, 0})), 180.0f, 1e-4f);
    EXPECT_NEAR(torsion(a, b, c, vec3_t{0, 1, 1}), -90.0f, 1e-4f);
    EXPECT_NEAR(torsion(a, b, c, vec3_t{0, 1, -1}), 90.0f, 1e-4f);
    // The same geometry split across a periodic boundary.
    const md_unitcell_t cell = md_unitcell_from_extent(20, 20, 20);
    EXPECT_NEAR(torsion(vec3_t{21, 5, 5}, vec3_t{20, 5, 5}, vec3_t{20, 6, 5}, vec3_t{0, 6, 6}, &cell), -90.0f, 1e-3f);
    EXPECT_TRUE(std::isnan(torsion(a, b, c, vec3_t{std::numeric_limits<float>::quiet_NaN(), 0, 0})));
}

UTEST(lavery_core, pseudorotation_recovers_north_and_south_puckers) {
    for (const float expected : {18.0f, 162.0f, 350.0f}) {
        float nu[5];
        for (int j = 0; j < 5; ++j) nu[j] = 38.0f * std::cos((expected + 144.0f * (j - 2)) * 3.14159265f / 180.0f);
        float phase = 0, amplitude = 0;
        pseudorotation(nu, &phase, &amplitude);
        EXPECT_NEAR(phase, expected, 0.01f);
        EXPECT_NEAR(amplitude, 38.0f, 0.01f);
    }
    EXPECT_EQ(metric_state(Phase, 18.0f), 0);   // North, C3'-endo
    EXPECT_EQ(metric_state(Phase, 162.0f), 1);  // South, C2'-endo
    EXPECT_EQ(metric_state(Phase, 350.0f), 0);
    EXPECT_EQ(metric_state(EpsilonZeta, -90.0f), 0); // BI
    EXPECT_EQ(metric_state(EpsilonZeta, 90.0f), 1);  // BII
    EXPECT_EQ(metric_state(Chi, -110.0f), 0);        // anti
    EXPECT_EQ(metric_state(Chi, 60.0f), 1);          // syn
    EXPECT_EQ(metric_state(Gamma, 55.0f), 0);        // g+
    EXPECT_EQ(metric_state(Gamma, 180.0f), 1);       // t
    EXPECT_EQ(metric_state(Gamma, -60.0f), 2);       // g-
    EXPECT_EQ(metric_state(Twist, 30.0f), -1);
    EXPECT_EQ(metric_state(Phase, std::numeric_limits<float>::quiet_NaN()), -1);
}

UTEST(lavery_core, nucleotide_values_handle_missing_atoms) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    vec3_t atoms[BB_Count];
    for (auto& atom : atoms) atom = {nan, nan, nan};
    Values values;
    compute_nucleotide(&values, atoms);
    for (int m = (int)Alpha; m <= (int)EpsilonZeta; ++m) EXPECT_TRUE(std::isnan(values.metric[m]));
    // Only the zeta neighbours are missing: epsilon stays defined, zeta and epsilon - zeta do not.
    atoms[BB_C5] = {2.0f, 1.0f, 0.0f};
    atoms[BB_C4] = {1.0f, 0.0f, 0.0f};
    atoms[BB_C3] = {0.0f, 0.0f, 0.0f};
    atoms[BB_O3] = {0.0f, 1.0f, 0.0f};
    atoms[BB_NextP] = {0.0f, 1.0f, 1.0f};
    compute_nucleotide(&values, atoms);
    EXPECT_TRUE(std::isfinite(values.metric[Delta]));
    EXPECT_NEAR(values.metric[Epsilon], -90.0f, 1e-3f);
    EXPECT_TRUE(std::isnan(values.metric[Zeta]));
    EXPECT_TRUE(std::isnan(values.metric[EpsilonZeta]));
    EXPECT_TRUE(std::isnan(values.metric[Phase]));
}

static std::array<vec3_t, BB_Count> nucleotide_quality_coordinates() {
    std::array<vec3_t, BB_Count> atoms;
    for (int k = 0; k < BB_Count; ++k) atoms[k] = {(float)k*0.5f, (float)(k%3), (float)(k%2)};
    const int ring[] = {BB_C1, BB_C2, BB_C3, BB_C4, BB_O4};
    for (int j = 0; j < 5; ++j) {
        const float angle = j*2.0f*3.14159265f/5.0f;
        atoms[ring[j]] = {1.25f*std::cos(angle), 1.25f*std::sin(angle), j == 2 ? 0.4f : 0.0f};
    }
    return atoms;
}

UTEST(lavery_core, nucleotide_quality_distinguishes_missing_nonfinite_and_parameter_scope) {
    auto atoms = nucleotide_quality_coordinates();
    Values values;
    NucleotideQuality quality;
    compute_nucleotide(&values, atoms.data(), nullptr, &quality, ((1u << BB_Count)-1) & ~(1u << BB_C2));
    EXPECT_TRUE(quality[Phase-Alpha] & Quality_MissingAtoms);
    EXPECT_TRUE(quality[Amplitude-Alpha] & Quality_MissingAtoms);
    EXPECT_FALSE(quality[Gamma-Alpha] & Quality_MissingAtoms);
    EXPECT_TRUE(std::isnan(values.metric[Phase]));
    EXPECT_TRUE(std::isnan(values.metric[Amplitude]));
    EXPECT_TRUE(std::isfinite(values.metric[Gamma]));
    atoms[BB_C2].x = std::numeric_limits<float>::quiet_NaN();
    compute_nucleotide(&values, atoms.data(), nullptr, &quality);
    EXPECT_TRUE(quality[Phase-Alpha] & Quality_NonfiniteCoordinates);
    EXPECT_FALSE(quality[Phase-Alpha] & Quality_MissingAtoms);
    EXPECT_FALSE(quality[Chi-Alpha] & Quality_NonfiniteCoordinates);
    EXPECT_TRUE(std::isfinite(values.metric[Chi]));
}

UTEST(lavery_core, nucleotide_quality_keeps_warning_values_and_explains_degenerate_torsions) {
    auto atoms = nucleotide_quality_coordinates();
    atoms[BB_PrevO3] = {20,30,40};
    Values values;
    NucleotideQuality quality;
    compute_nucleotide(&values, atoms.data(), nullptr, &quality);
    EXPECT_TRUE(quality[Alpha-Alpha] & Quality_UnusualBond);
    EXPECT_TRUE(std::isfinite(values.metric[Alpha]));
    const auto before = values.metric[Gamma];
    atoms[BB_PrevO3] = atoms[BB_P];
    compute_nucleotide(&values, atoms.data(), nullptr, &quality);
    EXPECT_TRUE(quality[Alpha-Alpha] & Quality_DegenerateNucleotide);
    EXPECT_TRUE(std::isnan(values.metric[Alpha]));
    EXPECT_EQ(values.metric[Gamma], before);
    compute_nucleotide(&values, atoms.data(), nullptr, &quality, ((1u << BB_Count)-1) & ~(1u << BB_NextO5));
    EXPECT_TRUE(quality[Zeta-Alpha] & Quality_MissingAtoms);
    EXPECT_EQ(quality[EpsilonZeta-Alpha], (uint16_t)(quality[Epsilon-Alpha] | quality[Zeta-Alpha]));
    EXPECT_TRUE(std::isnan(values.metric[EpsilonZeta]));
}

UTEST(lavery_core, planar_sugar_has_zero_amplitude_but_no_defined_phase) {
    auto atoms = nucleotide_quality_coordinates();
    for (int slot : {BB_C1, BB_C2, BB_C3, BB_C4, BB_O4}) atoms[slot].z = 0;
    Values values;
    NucleotideQuality quality;
    compute_nucleotide(&values, atoms.data(), nullptr, &quality);
    EXPECT_TRUE(std::isnan(values.metric[Phase]));
    EXPECT_TRUE(quality[Phase-Alpha] & Quality_DegenerateNucleotide);
    EXPECT_NEAR(values.metric[Amplitude], 0.0f, 1e-4f);
    EXPECT_FALSE(quality[Amplitude-Alpha] & Quality_DegenerateNucleotide);
}

UTEST(lavery_core, distorted_sugar_fit_warns_but_does_not_hide_fluctuations_or_switches) {
    auto atoms = nucleotide_quality_coordinates();
    atoms[BB_C2] = {20,0,20};
    Values values;
    NucleotideQuality quality;
    compute_nucleotide(&values, atoms.data(), nullptr, &quality);
    EXPECT_TRUE(quality[Phase-Alpha] & Quality_PoorPuckerFit);
    EXPECT_TRUE(quality[Amplitude-Alpha] & Quality_PoorPuckerFit);
    EXPECT_TRUE(quality[Phase-Alpha] & Quality_UnusualBond);
    EXPECT_TRUE(std::isfinite(values.metric[Phase]));
    EXPECT_TRUE(std::isfinite(values.metric[Amplitude]));
    EXPECT_FALSE(quality[Gamma-Alpha] & Quality_PoorPuckerFit);
    MetricStatistics statistics;
    const std::vector<float> amplitudes = {35,36,34,35,values.metric[Amplitude]};
    ASSERT_TRUE(compute_statistics(&statistics, amplitudes, 1));
    EXPECT_EQ(statistics.members[0].count, amplitudes.size());
    EXPECT_TRUE(statistics.members[0].is_outlier(values.metric[Amplitude], 1.5f));
    // Warnings are metadata, not a mask: state detection still accepts the finite phase.
    const float phase = values.metric[Phase];
    const float source = metric_state(Phase, phase) == 0 ? 162.0f : 18.0f;
    std::vector<StateSwitch> switches;
    EventOptions options;
    options.min_frames = 3;
    ASSERT_TRUE(detect_state_switches(&switches, {source,source,source,phase,phase,phase}, 1, Phase, options));
    ASSERT_EQ(switches.size(), (size_t)1);
}

UTEST(lavery_core, nucleotide_quality_warnings_are_periodic_and_exported_without_invalidating_values) {
    auto atoms = nucleotide_quality_coordinates();
    const auto cell = md_unitcell_from_extent(50,50,50);
    Values before, after;
    NucleotideQuality qa, qb;
    compute_nucleotide(&before, atoms.data(), &cell, &qa);
    atoms[BB_C2].x += 50;
    atoms[BB_C2].z -= 50;
    compute_nucleotide(&after, atoms.data(), &cell, &qb);
    EXPECT_TRUE(qa == qb);
    for (int id = Alpha; id <= (int)EpsilonZeta; ++id) {
        EXPECT_NEAR(after.metric[id], before.metric[id], 0.001f);
    }
    CsvSample sample;
    sample.metric = Phase;
    sample.value = before.metric[Phase];
    sample.quality = Quality_UnusualBond | Quality_PoorPuckerFit;
    const auto row = csv_sample(sample);
    EXPECT_NE(row.find("finite measurement retained"), std::string::npos);
    EXPECT_NE(row.find("Fourier-fit RMS residual"), std::string::npos);
    EXPECT_NE(row.find(",1,24576,"), std::string::npos);
}

UTEST(lavery_core, groove_widths_use_3dna_phosphorus_indices) {
    const size_t n = 8;
    std::vector<vec3_t> p1(n), p2(n);
    for (size_t k = 0; k < n; ++k) {
        p1[k] = {(float)k, 0.0f, 0.0f};
        p2[k] = {(float)k, 10.0f, 0.0f};
    }
    std::vector<float> minor(n - 1), major(n - 1);
    groove_widths(minor.data(), major.data(), p1.data(), p2.data(), n);
    auto d = [](int i, int j) { return std::sqrt((float)((i - j) * (i - j)) + 100.0f) - 5.8f; };
    // 1-based step i: minor needs i - 2 >= 1 and i + 3 <= n; major needs i - 1 >= 1 and i + 2 <= n.
    for (int i = 1; i <= (int)n - 1; ++i) {
        if (i >= 3 && i + 3 <= (int)n) EXPECT_NEAR(minor[i - 1], 0.5f * (d(i + 2, i - 2) + d(i + 3, i - 1)), 1e-4f);
        else EXPECT_TRUE(std::isnan(minor[i - 1]));
        if (i >= 2 && i + 2 <= (int)n) EXPECT_NEAR(major[i - 1], d(i - 1, i + 2), 1e-4f);
        else EXPECT_TRUE(std::isnan(major[i - 1]));
    }
    p2[3].x = std::numeric_limits<float>::quiet_NaN();
    groove_widths(minor.data(), major.data(), p1.data(), p2.data(), n);
    EXPECT_TRUE(std::isnan(major[1])); // step 2 uses P2(4)
}

UTEST(lavery_core, helix_chains_follow_steps) {
    const std::vector<StepIndex> steps = {{1, 2}, {0, 1}, {4, 5}};
    const auto chains = helix_chains(6, steps);
    ASSERT_EQ(chains.size(), (size_t)2);
    EXPECT_TRUE(chains[0] == (std::vector<uint32_t>{0, 1, 2}));
    EXPECT_TRUE(chains[1] == (std::vector<uint32_t>{4, 5}));
}

UTEST(lavery_core, state_baselines_measure_within_state_not_state_separation) {
    const std::vector<float> phase = {8, 10, 12, 10, 8, 10, 12, 10, 165, 170};
    MetricStatistics stats;
    ASSERT_TRUE(compute_statistics(&stats, phase, 1, true, Baseline_MeanSD, Phase));
    const auto& member = stats.members[0];
    EXPECT_EQ(member.state, 0);
    EXPECT_NEAR(member.state_fraction, 0.8f, 1e-6f);
    EXPECT_NEAR(member.mean, 10.0, 1e-9);
    EXPECT_NEAR(member.sd, std::sqrt(2.0), 1e-9);
    EXPECT_NEAR(member.zscore(165.0f), -1.0f, 1e-5f);
    EXPECT_NEAR(member.zscore(170.0f), 1.0f, 1e-5f);
    EXPECT_EQ(member.count, (size_t)10);
    EXPECT_NEAR(member.reference(165).mean, 167.5, 1e-5);
    EXPECT_NEAR(member.reference(165).sd, 2.5, 1e-5);
    EXPECT_FALSE(member.is_outlier(165, 2.5f));
    // Without a state metric, the excursions inflate the spread.
    ASSERT_TRUE(compute_statistics(&stats, phase, 1, true, Baseline_MeanSD));
    EXPECT_EQ(stats.members[0].state, -1);
    EXPECT_LT(stats.members[0].zscore(165.0f), 3.0f);
}

UTEST(lavery_core, state_switches_require_source_and_destination_residence_and_ignore_spikes) {
    const std::vector<float> phase = {10,12,8, 160, 10,11,9, 160,165,170, 12,10,8};
    EventOptions options;
    options.min_frames = 3;
    std::vector<StateSwitch> changes;
    ASSERT_TRUE(detect_state_switches(&changes, phase, 1, Phase, options));
    ASSERT_EQ(changes.size(), (size_t)2);
    EXPECT_EQ(changes[0].begin, (size_t)7);
    EXPECT_EQ(changes[0].end, (size_t)9);
    EXPECT_EQ(changes[0].from, 0);
    EXPECT_EQ(changes[0].to, 1);
    EXPECT_EQ(changes[0].source_begin, (size_t)4);
    EXPECT_EQ(changes[0].source_end, (size_t)6);
    EXPECT_EQ(changes[0].intervening_samples(), (size_t)0);
    EXPECT_FALSE(changes[0].source_clipped);
    EXPECT_FALSE(changes[0].destination_clipped);
    EXPECT_EQ(changes[1].source_begin, (size_t)7);
    EXPECT_EQ(changes[1].source_end, (size_t)9);
    EXPECT_TRUE(changes[1].destination_clipped);
    EXPECT_TRUE(changes[1].destination_at_trajectory_end);
    EXPECT_NEAR(changes[0].angular_change, 155, 0.01f);
    EXPECT_NEAR(changes[1].angular_change, -155, 0.01f);
    options.times = {0,2,4,6,8,10,12,14,16,18,20,22,24};
    options.min_duration = 4;
    ASSERT_TRUE(detect_state_switches(&changes, phase, 1, Phase, options));
    EXPECT_EQ(changes.size(), (size_t)2);
    options.min_duration = 5;
    ASSERT_TRUE(detect_state_switches(&changes, phase, 1, Phase, options));
    EXPECT_TRUE(changes.empty());
}

UTEST(lavery_core, state_switches_reset_at_missing_data_and_do_not_infer_range_boundary_transitions) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::vector<float> phase = {10,10,10,nan,160,160,160,10,10,10};
    EventOptions options;
    options.min_frames = 3;
    std::vector<StateSwitch> changes;
    ASSERT_TRUE(detect_state_switches(&changes, phase, 1, Phase, options));
    ASSERT_EQ(changes.size(), (size_t)1);
    EXPECT_EQ(changes[0].begin, (size_t)7);
    options.first_frame = 5;
    ASSERT_TRUE(detect_state_switches(&changes, phase, 1, Phase, options));
    EXPECT_TRUE(changes.empty());
    options.first_frame = 4;
    options.last_frame = 8;
    ASSERT_TRUE(detect_state_switches(&changes, phase, 1, Phase, options));
    EXPECT_TRUE(changes.empty());
}

UTEST(lavery_core, state_switches_validate_dimensions_timestamps_and_export_provenance) {
    std::vector<StateSwitch> changes;
    EventOptions options;
    options.min_frames = 2;
    const std::vector<float> chi = {-160,-160,160,160,-80,-80, 40,40};
    ASSERT_TRUE(detect_state_switches(&changes, chi, 2, Chi, options));
    ASSERT_EQ(changes.size(), (size_t)2);
    EXPECT_EQ(changes[0].member, 0u);
    EXPECT_EQ(changes[1].member, 1u);
    EXPECT_LE(std::fabs(changes[0].angular_change), 180.0f);
    const std::string row = csv_state_switch_row("run/test", "DA5", changes[0], 10,20,"ps",5,0,5);
    EXPECT_NE(row.find("\"anti\",\"syn\""), std::string::npos);
    EXPECT_NE(row.find(",10,5,0,1,0,1,1,1,0,5\n"), std::string::npos);
    EXPECT_EQ((size_t)std::count(row.begin(), row.end(), ','), (size_t)22);
    EXPECT_NE(std::string(csv_state_switch_header()).find("source_begin_frame,source_end_frame,intervening_samples"), std::string::npos);
    EXPECT_FALSE(detect_state_switches(&changes, chi, 3, Chi, options));
    EXPECT_FALSE(detect_state_switches(&changes, chi, 2, Twist, options));
    options.times = {0,1,1,3};
    EXPECT_FALSE(detect_state_switches(&changes, chi, 2, Chi, options));
    options.times = {0,1,2};
    EXPECT_FALSE(detect_state_switches(&changes, chi, 2, Chi, options));
}

UTEST(lavery_core, state_switches_preserve_separated_run_context_and_detection_boundaries) {
    const std::vector<float> phase = {10,10,10,160,10,160,160,160,160};
    EventOptions options;
    options.min_frames = 3;
    std::vector<StateSwitch> changes;
    ASSERT_TRUE(detect_state_switches(&changes, phase, 1, Phase, options));
    ASSERT_EQ(changes.size(), (size_t)1);
    EXPECT_EQ(changes[0].source_begin, (size_t)0);
    EXPECT_EQ(changes[0].source_end, (size_t)2);
    EXPECT_EQ(changes[0].begin, (size_t)5);
    EXPECT_EQ(changes[0].end, (size_t)8);
    EXPECT_EQ(changes[0].intervening_samples(), (size_t)2);
    EXPECT_TRUE(changes[0].source_clipped);
    EXPECT_TRUE(changes[0].destination_clipped);
    EXPECT_TRUE(changes[0].destination_at_trajectory_end);
    const std::string frames_row = csv_state_switch_row("run", "1a", changes[0], 5,8,"frame",3,0,2);
    EXPECT_NE(frames_row.find(",4,3,0,2,2,1,1,1,0,2\n"), std::string::npos);
    options.min_frames = 2;
    options.first_frame = 1;
    options.last_frame = 6;
    ASSERT_TRUE(detect_state_switches(&changes, phase, 1, Phase, options));
    ASSERT_EQ(changes.size(), (size_t)1);
    EXPECT_EQ(changes[0].source_begin, (size_t)1);
    EXPECT_EQ(changes[0].source_end, (size_t)2);
    EXPECT_EQ(changes[0].end, (size_t)6);
    EXPECT_TRUE(changes[0].source_clipped);
    EXPECT_TRUE(changes[0].destination_clipped);
    EXPECT_FALSE(changes[0].destination_at_trajectory_end);
    options.times = {0,3,10,20,22,30,50,75,100};
    options.min_duration = 7;
    ASSERT_TRUE(detect_state_switches(&changes, phase, 1, Phase, options));
    ASSERT_EQ(changes.size(), (size_t)1);
    const std::string row = csv_state_switch_row("run", "1a", changes[0], 30,50,"ps",7,3,10);
    EXPECT_NE(row.find(",20,7,1,2,2,1,1,0,3,10\n"), std::string::npos);
    options.last_frame = 7;
    ASSERT_TRUE(detect_state_switches(&changes, phase, 1, Phase, options));
    ASSERT_EQ(changes.size(), (size_t)1);
    EXPECT_TRUE(changes[0].destination_clipped);
    EXPECT_FALSE(changes[0].destination_at_trajectory_end);
    EventOptions frame_options;
    frame_options.min_frames = 3;
    ASSERT_TRUE(detect_state_switches(&changes, {10,10,10,160,160,160,10,10,10,160,160,160}, 1, Phase,
        frame_options));
    ASSERT_EQ(changes.size(), (size_t)3);
    EXPECT_FALSE(changes[0].destination_clipped);
    EXPECT_FALSE(changes[1].source_clipped);
}

UTEST(lavery_core, state_baselines_support_constant_minority_and_robust_circular_references) {
    MetricStatistics stats;
    const std::vector<float> phase = {350,0,10,160,160};
    ASSERT_TRUE(compute_statistics(&stats, phase, 1, true, Baseline_MedianMAD, Phase));
    EXPECT_NEAR(stats.members[0].zscore(160), 0, 1e-6);
    EXPECT_NEAR(stats.members[0].zscore(0), 0, 1e-6);
    EXPECT_NEAR(stats.members[0].reference(350).mean, 0, 1e-6);
    EXPECT_EQ(stats.members[0].reference(160).count, (size_t)2);
    CsvSample sample;
    sample.metric = Phase;
    sample.value = 160;
    sample.baseline = stats.members[0];
    EXPECT_NE(csv_sample(sample).find(",160,0,0,"), std::string::npos);
}

UTEST(lavery_core, nucleotide_members_join_events_at_their_pair) {
    std::vector<std::vector<MemberOutlier>> frames(6);
    // Nucleotide 5 = pair 2, side b; step 2 links pairs 2 and 3.
    for (size_t f = 1; f < 5; ++f) {
        frames[f].push_back(event_sample(5, Class_Nucleotide, Phase - Alpha, 6.0f));
        frames[f].push_back(event_sample(2, Class_Step, 0, 3.0f));
    }
    EventOptions options;
    options.min_frames = 3;
    options.neighbourhood = 1;
    const auto events = detect_events(frames, 10, event_steps(), options);
    ASSERT_EQ(events.size(), (size_t)1);
    EXPECT_EQ(events[0].pair_first, 2u);
    EXPECT_EQ(events[0].pair_last, 3u);
    ASSERT_EQ(events[0].members.size(), (size_t)2);
    EXPECT_EQ((int)events[0].members[0].cls, (int)Class_Nucleotide);
    EXPECT_EQ(events[0].members[0].member, 5u);
    EXPECT_EQ((int)events[0].members[0].peak_parameter, (int)(Phase - Alpha));
    EXPECT_EQ(event_dominant_metric(events[0]), (int)Phase);
    const std::string kind = event_kind(events[0]);
    EXPECT_STREQ(kind.c_str(), "Sugar fluctuation");
    // A nucleotide index past 2 x pairs is rejected.
    frames[1].push_back(event_sample(20, Class_Nucleotide, 0, 6.0f));
    EXPECT_TRUE(detect_events(frames, 10, event_steps(), options).empty());
}

UTEST(lavery_core, groove_visual_spans_match_numerical_definition_and_boundaries) {
    const auto minor = groove_phosphate_indices(MinorGroove, 2, 12);
    ASSERT_EQ(minor.count, (size_t)2);
    EXPECT_EQ(minor.indices[0][0], 4u);
    EXPECT_EQ(minor.indices[0][1], 0u);
    EXPECT_EQ(minor.indices[1][0], 5u);
    EXPECT_EQ(minor.indices[1][1], 1u);
    const auto major = groove_phosphate_indices(MajorGroove, 2, 12);
    ASSERT_EQ(major.count, (size_t)1);
    EXPECT_EQ(major.indices[0][0], 1u);
    EXPECT_EQ(major.indices[0][1], 4u);
    EXPECT_EQ(groove_phosphate_indices(MinorGroove, 1, 12).count, (size_t)0);
    EXPECT_EQ(groove_phosphate_indices(MinorGroove, 9, 12).count, (size_t)0);
    EXPECT_EQ(groove_phosphate_indices(MajorGroove, 0, 12).count, (size_t)0);
    EXPECT_EQ(groove_phosphate_indices(MajorGroove, 10, 12).count, (size_t)0);
    EXPECT_EQ(groove_phosphate_indices(MinorGroove, SIZE_MAX, 12).count, (size_t)0);
    EXPECT_EQ(groove_phosphate_indices(Twist, 2, 12).count, (size_t)0);
}

UTEST(lavery_core, nucleotide_visual_dihedrals_use_the_numerical_defining_atoms) {
    BaseTopology base;
    vec3_t positions[BB_Count];
    for (int k = 0; k < BB_Count; ++k) {
        base.backbone[k] = k;
        positions[k] = {(float)std::sin(k * 1.3), (float)std::cos(k * 0.9), (float)k * 0.4f};
    }
    Values values;
    compute_nucleotide(&values, positions);
    for (int k = (int)Alpha; k <= (int)Chi; ++k) {
        const auto atoms = nucleotide_torsion_atoms(base, (Metric)k);
        EXPECT_NEAR(torsion(positions[atoms[0]], positions[atoms[1]], positions[atoms[2]], positions[atoms[3]]),
            values.metric[k], 1e-5f);
    }
    base.backbone[BB_PrevO3] = -1;
    EXPECT_EQ(nucleotide_torsion_atoms(base, Alpha)[0], -1);
    for (Metric metric : {Phase, Amplitude, EpsilonZeta, Roll}) {
        for (int32_t atom : nucleotide_torsion_atoms(base, metric)) EXPECT_EQ(atom, -1);
    }
}

UTEST(lavery_core, step_visual_frame_is_the_exact_parameter_midframe) {
    BasePairFrame a = {}, b = {}, midpoint;
    a.x = b.x = {1,0,0};
    a.y = b.y = {0,1,0};
    a.z = b.z = {0,0,1};
    a.origin = {1,2,3};
    b.origin = {3,4,7};
    Values values;
    ASSERT_TRUE(compute_step(&values, a, b, &midpoint));
    EXPECT_NEAR(midpoint.origin.x, 2, 1e-6f);
    EXPECT_NEAR(midpoint.origin.y, 3, 1e-6f);
    EXPECT_NEAR(midpoint.origin.z, 5, 1e-6f);
    EXPECT_NEAR(values.metric[Shift], vec3_dot(vec3_sub(b.origin,a.origin), midpoint.x), 1e-6f);
    EXPECT_NEAR(values.metric[Slide], vec3_dot(vec3_sub(b.origin,a.origin), midpoint.y), 1e-6f);
    EXPECT_NEAR(values.metric[Rise], vec3_dot(vec3_sub(b.origin,a.origin), midpoint.z), 1e-6f);
}

UTEST(lavery_core, pucker_amplitude_has_degree_units_without_circular_statistics) {
    EXPECT_TRUE(metric_uses_degrees(Amplitude));
    EXPECT_FALSE(metric_is_angular(Amplitude));
    EXPECT_FALSE(metric_uses_degrees(MinorGroove));
    EXPECT_TRUE(metric_uses_degrees(Phase));
    CsvSample sample;
    sample.metric = Amplitude;
    sample.value = 38;
    EXPECT_NE(csv_sample(sample).find(",deg,"), std::string::npos);
    CsvEventRow row;
    row.metric = Amplitude;
    EXPECT_NE(csv_event_row(row).find(",deg,"), std::string::npos);
}
