#include <components/lavery/lavery_core.h>

#include <utest.h>

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

UTEST(lavery_core, events_group_consecutive_frames_and_track_peak_severity_and_count) {
    const std::vector<FrameOutliers> counts = {{0,5}, {2,5}, {3,5}, {1,5}, {0,0}, {1,5}, {0,5}, {2,5}};
    const auto events = collect_outlier_events(counts, {0,3,4,6,0,5,0,7});
    ASSERT_EQ(events.size(), (size_t)3);
    EXPECT_EQ(events[0].begin, (size_t)1);
    EXPECT_EQ(events[0].end, (size_t)3);
    EXPECT_EQ(events[0].peak, (size_t)3);
    EXPECT_EQ(events[0].peak_count, (size_t)3);
    EXPECT_EQ(events[0].severity, 6.0f);
    EXPECT_EQ(events[1].begin, (size_t)5);
    EXPECT_EQ(events[1].end, (size_t)5);
    EXPECT_EQ(events[2].peak, (size_t)7);
    EXPECT_TRUE(collect_outlier_events({}, {}).empty());
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
    EXPECT_STREQ(row.c_str(), "\"run1\",3,60,\"ps\",bp,2,\"pair2\",\"S1:DA2 / S2:DT9\",\"\",bp/stretch,Angstrom,8,1,0,\"Good\",2,2,3,2,1,1,1,16\n");
    size_t columns = 1;
    for (const char* p = csv_header(); *p; ++p) if (*p == ',') ++columns;
    EXPECT_EQ(columns, (size_t)23);
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
