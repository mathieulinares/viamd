#include <components/lavery/lavery_core.h>

#include <utest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <md_gro.h>
#include <md_system.h>
#include <md_util.h>
#include <core/md_arena_allocator.h>
#include "data/lavery_curves_reference.h"
using namespace lavery;

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

static MemberOutlier event_sample(uint32_t member, bool step, int parameter, float z, bool strong = true) {
    MemberOutlier sample;
    sample.member = member;
    sample.step = step;
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
    EXPECT_TRUE(first.step);
    EXPECT_EQ(first.member, (uint32_t)3);
    EXPECT_EQ(first.parameters, (uint32_t)(1u << 2));
    EXPECT_EQ(first.peak, (size_t)1);
    EXPECT_EQ(first.peak_z, -5.0f);
    const EventMember& second = events[0].members[1];
    EXPECT_FALSE(second.step);
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
        [](const EventMember& m) { return !m.step && m.member == 2; });
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
    EXPECT_TRUE(feedback.back().step);
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
                return m.step == member.step && m.member == member.member;
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
    EXPECT_EQ(slots[3], slots[1]);
    EXPECT_EQ(slots[4], slots[0]);
    EXPECT_TRUE(slots == event_color_slots(events));
    EXPECT_TRUE(event_color_slots({}).empty());
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
    EXPECT_FALSE(start[0].step);
    EXPECT_EQ(start[0].parameters, 1u);
    EXPECT_TRUE(start[1].step);
    EXPECT_EQ(start[1].parameters, 1u << 2);
    EXPECT_TRUE(event_contributions(events, 1).empty());
    const auto later = event_contributions(events, 2);
    ASSERT_EQ(later.size(), (size_t)3);
    EXPECT_EQ(later[0].parameters, 1u << 4);
    EXPECT_TRUE(later[1].lost);
    EXPECT_TRUE(later[2].step);
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
    step.step = true;
    second.members.push_back(step);
    const auto combined = event_contributions({first, second}, 2);
    ASSERT_EQ(combined.size(), (size_t)2);
    EXPECT_FALSE(combined[0].step);
    EXPECT_EQ(combined[0].parameters, 1u | (1u << 4));
    EXPECT_TRUE(combined[1].step);
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
    EXPECT_TRUE(event_kind(events[0]) == "Pair loss + Opening (breathing / flip)");
    EXPECT_TRUE(event_kind(events[1]) == "Pair loss");
    EXPECT_EQ(events[1].peak, (size_t)4);
    EXPECT_EQ(events[1].members[0].peak_parameter, -1);
    std::vector<std::vector<MemberOutlier>> bad(1);
    bad[0] = {lost_pair(2)};
    bad[0][0].step = true;
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
    const std::string text = csv_event_row(row);
    EXPECT_STREQ(text.c_str(),
        "\"run1\",2,4,9,40,90,\"ps\",6,4.5,12.25,\"Unwinding\",7,9,\"all\",\"median_mad\",2.5,2,2,step,7,\"7-8: \"\"x\"\"\",3,1,step/twist,deg,1,6,-4.5,21.5,0,0\n");
    size_t header = 1, fields = 1;
    for (const char* p = csv_event_header(); *p; ++p) if (*p == ',') ++header;
    row.label = "plain";
    for (char c : csv_event_row(row)) if (c == ',') ++fields;
    EXPECT_EQ(header, fields);
    EXPECT_EQ(header, (size_t)31);
    row.parent_event = 1;
    row.burst_sigma = 3.5f;
    const auto nested = csv_event_row(row);
    EXPECT_TRUE(nested.find(",1,3.5\n") != std::string::npos);
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
    const auto row = csv_sample(sample);
    EXPECT_STREQ(row.c_str(), "\"run1\",3,60,\"ps\",bp,2,\"pair2\",\"S1:DA2 / S2:DT9\",\"\",bp/stretch,Angstrom,8,1,0,\"Good\",2,2,3,2,1,1,1,16,mean_sd\n");
    size_t columns = 1;
    for (const char* p = csv_header(); *p; ++p) if (*p == ',') ++columns;
    EXPECT_EQ(columns, (size_t)24);
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
    for (uint32_t k = Tilt; k < Metric_Count; ++k) EXPECT_NEAR(before.metric[k], after.metric[k], 0.002f);
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
