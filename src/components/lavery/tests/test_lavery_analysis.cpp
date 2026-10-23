#include <components/lavery/lavery_core.h>
#include <utest.h>
#include <md_system.h>
#include <md_gro.h>
#include <md_pdb.h>
#include <md_script.h>
#include <md_util.h>
#include <core/md_arena_allocator.h>
#include <core/md_writer.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <filesystem>
#include "data/lavery_rna_curves_reference.h"

using namespace lavery;

UTEST(lavery_core, contacts_require_canonical_hbonds_when_hydrogens_exist) {
    vec3_t xyz[] = {{0, 0, 0}, {3, 0, 0}, {0, 3, 0}, {3, 3, 0}};
    md_system_state_t state = {.num_atoms = 4, .xyz = xyz};
    PairChemistry pair;
    pair.contacts = {{0, 1, true}, {2, 3, true}};
    pair.uses_hydrogens = true;
    uint8_t count = 99;
    md_hbond_set_t bonds = {};
    EXPECT_EQ(evaluate_pair_contacts(&count, pair, state, bonds), (uint16_t)Quality_NoPairContacts);
    EXPECT_EQ(count, 0);
    uint32_t donor[] = {0, 0}, acceptor[] = {1, 1};
    bonds.count = 2;
    bonds.donor = donor;
    bonds.acceptor = acceptor;
    EXPECT_EQ(evaluate_pair_contacts(&count, pair, state, bonds), (uint16_t)Quality_Good);
    EXPECT_EQ(count, 1); // Two hydrogens cannot double-count the same canonical contact.
    acceptor[0] = acceptor[1] = 3;
    EXPECT_EQ(evaluate_pair_contacts(&count, pair, state, bonds), (uint16_t)Quality_NoPairContacts);
    // Partial hydrogen coverage must not silently switch to a distance-only fallback.
    pair.contacts[0].explicit_hydrogen = false;
    acceptor[0] = acceptor[1] = 1;
    EXPECT_EQ(evaluate_pair_contacts(&count, pair, state, bonds), (uint16_t)Quality_NoPairContacts);
}

UTEST(lavery_core, hydrogen_free_contacts_are_explicit_periodic_heavy_atom_proxies) {
    vec3_t xyz[] = {{1, 0, 0}, {28, 0, 0}, {1, 3, 0}, {28, 3, 0}};
    md_system_state_t state = {.num_atoms = 4, .xyz = xyz, .unitcell = md_unitcell_from_extent(30, 30, 30)};
    PairChemistry pair;
    pair.contacts = {{0, 1, false}, {2, 3, false}};
    uint8_t count = 0;
    const md_hbond_set_t bonds = {};
    EXPECT_EQ(evaluate_pair_contacts(&count, pair, state, bonds), (uint16_t)Quality_Good);
    EXPECT_EQ(count, 2);
    xyz[3].x = 20;
    EXPECT_EQ(evaluate_pair_contacts(&count, pair, state, bonds), (uint16_t)Quality_NoPairContacts);
    EXPECT_EQ(count, 1);
    xyz[3].x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(evaluate_pair_contacts(&count, pair, state, bonds), (uint16_t)Quality_NoPairContacts);
    pair.contacts.pop_back();
    EXPECT_EQ(evaluate_pair_contacts(&count, pair, state, bonds), (uint16_t)Quality_NoncanonicalPair);
    EXPECT_STREQ(quality_description(Quality_NoPairContacts),
        "No canonical H-bond remains, or fewer than two canonical heavy-atom contacts without hydrogens");
}

UTEST(lavery_core, axis_preserves_straight_segments_and_unit_tangents) {
    std::vector<BasePairFrame> frames(5);
    for (size_t i = 0; i < frames.size(); ++i) {
        frames[i].origin = {(float)i * 2, 1, (float)i * 3};
        frames[i].z = {0, 0, 1};
    }
    const auto axis = compute_axis(frames, {{0, 1}, {1, 2}, {2, 3}, {3, 4}});
    ASSERT_EQ(axis.offsets.size(), (size_t)2);
    ASSERT_EQ(axis.points.size(), (size_t)33);
    ASSERT_EQ(axis.tangents.size(), axis.points.size());
    for (size_t i = 0; i < axis.points.size(); ++i) {
        const float t = (float)i / 8;
        EXPECT_NEAR(axis.points[i].x, 2*t, 1.0e-5f);
        EXPECT_NEAR(axis.points[i].y, 1, 1.0e-5f);
        EXPECT_NEAR(axis.points[i].z, 3*t, 1.0e-5f);
        EXPECT_NEAR(vec3_length(axis.tangents[i]), 1, 1.0e-6f);
    }
}

UTEST(lavery_core, axis_unwraps_each_connected_segment_and_never_bridges_missing_pairs) {
    std::vector<BasePairFrame> frames(7);
    for (auto& frame : frames) frame.z = {0, 0, 1};
    frames[0].origin = {28, 0, 0};
    frames[1].origin = {1, 0, 0};
    frames[2].origin = {4, 0, 0};
    frames[3] = {}; // Break must split the spline, not interpolate over pairing loss.
    frames[4].origin = {20, 0, 0};
    frames[5].origin = {23, 0, 0};
    frames[6].origin = {0, 0, 0}; // Separate topology, no incoming step.
    const auto cell = md_unitcell_from_extent(30, 30, 30);
    const auto axis = compute_axis(frames, {{0,1}, {1,2}, {2,3}, {3,4}, {4,5}}, &cell);
    ASSERT_EQ(axis.offsets.size(), (size_t)3);
    EXPECT_EQ(axis.offsets[1], (size_t)17);
    EXPECT_EQ(axis.offsets[2], (size_t)26);
    EXPECT_NEAR(axis.points[0].x, 28, 1.0e-5f);
    EXPECT_NEAR(axis.points[16].x, 34, 1.0e-5f);
    EXPECT_NEAR(axis.points[17].x, 20, 1.0e-5f);
    EXPECT_NEAR(axis.points.back().x, 23, 1.0e-5f);
    EXPECT_EQ(compute_axis({}, {}).offsets.size(), (size_t)1);
    EXPECT_TRUE(compute_axis({frames[0]}, {}).points.empty());
}

UTEST(lavery_core, axis_smooths_local_noise_and_handles_coincident_centers) {
    std::vector<BasePairFrame> frames(5);
    for (size_t i = 0; i < frames.size(); ++i) {
        frames[i].origin = {(float)i, i == 2 ? 8.0f : 0, 0};
        frames[i].z = {0, 0, 1};
    }
    auto axis = compute_axis(frames, {{0,1}, {1,2}, {2,3}, {3,4}});
    ASSERT_EQ(axis.points.size(), (size_t)33);
    EXPECT_NEAR(axis.points[16].y, 3, 1.0e-5f);
    for (auto& f : frames) f.origin = {};
    axis = compute_axis(frames, {{0,1}, {1,2}});
    for (size_t i = 0; i < axis.points.size(); ++i) {
        EXPECT_TRUE(std::isfinite(axis.points[i].x));
        EXPECT_EQ(vec3_length(axis.tangents[i]), 0);
    }
    EXPECT_TRUE(compute_axis(frames, {{0,1}, {0,2}}).points.empty());
    EXPECT_TRUE(compute_axis(frames, {}, nullptr, 0).points.empty());
}

UTEST(lavery_core, distribution_comparisons_share_bins_and_normalize_each_members_finite_samples) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    DistributionComparison hist;
    ASSERT_TRUE(compute_distributions(&hist, {0, 0, nan, 1, nan, nan, 2, 2, nan}, 3, 2));
    EXPECT_EQ(hist.valid[0], (size_t)3);
    EXPECT_EQ(hist.valid[1], (size_t)2);
    EXPECT_EQ(hist.valid[2], (size_t)0);
    EXPECT_EQ(hist.centers[0], 0.5);
    EXPECT_EQ(hist.centers[1], 1.5);
    EXPECT_EQ(hist.bin_width, 1);
    for (size_t m = 0; m < 2; ++m) {
        EXPECT_NEAR((hist.density[m * 2] + hist.density[m * 2 + 1]) * hist.bin_width, 1, 1.0e-12);
    }
    EXPECT_EQ(hist.density[3], 0.5); // Upper edge belongs to the final bin.
    EXPECT_EQ(hist.density[4], 0);
    ASSERT_TRUE(compute_distributions(&hist, {7, 7, 7, 7}, 2));
    EXPECT_GT(hist.bin_width, 0);
    ASSERT_TRUE(compute_distributions(&hist, {nan, nan}, 2));
    EXPECT_TRUE(hist.centers.empty());
    EXPECT_FALSE(compute_distributions(&hist, {1}, 2));
}

UTEST(lavery_core, production_frame_analysis_checks_chemistry_and_recovers_after_complete_pair_loss) {
    auto* alloc = md_arena_allocator_create(md_get_heap_allocator(), MEGABYTES(16));
    md_system_t sys = {.alloc = alloc};
    md_system_state_t state = {.alloc = alloc};
    ASSERT_TRUE(md_gro_system_init_from_file(&sys, &state, STR_LIT(VIAMD_TEST_DATA_DIR "/nucl-dna.gro")));
    ASSERT_TRUE(md_util_system_infer(&sys, &state, MD_UTIL_INFER_ALL));
    const auto bases = gather_bases(sys);
    std::vector<BaseFrame> frames;
    ASSERT_TRUE(extract_base_frames(frames, sys, state, bases));
    std::vector<Pair> pairs(bases.size());
    pairs.resize(detect_pairs(pairs.data(), pairs.size(), frames.data(), frames.size(), &state.unitcell));
    const auto steps = gather_steps(sys, bases, pairs.data(), pairs.size());
    PairingValidator validator;
    ASSERT_TRUE(validator.initialize(sys, bases, pairs));
    FrameAnalysis result;
    ASSERT_TRUE(analyze_frame(&result, sys, state, bases, pairs, steps, validator));
    size_t present = 0, with_hydrogens = 0;
    for (size_t p = 0; p < pairs.size(); ++p) {
        if (!result.pair_quality[p]) ++present;
        if (validator.chemistry()[p].uses_hydrogens) ++with_hydrogens;
    }
    // Pair 127's canonical D-A distances are 4.855 and 4.197 A:
    // the geometric transform is calculable but there is no canonical H-bond.
    EXPECT_EQ(present, (size_t)146);
    EXPECT_EQ(result.pair_quality[126], (uint16_t)Quality_NoPairContacts);
    EXPECT_EQ(result.contacts[126], 0);
    EXPECT_TRUE(std::isnan(result.pair_values[126].metric[Opening]));
    EXPECT_EQ(std::count(result.step_quality.begin(), result.step_quality.end(), (uint16_t)Quality_Good), 144);
    EXPECT_EQ(with_hydrogens, pairs.size());
    const size_t p = 50;
    const auto original = result.pair_values[p];
    const auto range = md_component_atom_range(&sys.component, bases[pairs[p].b].component);
    std::vector<vec3_t> saved(state.xyz + range.beg, state.xyz + range.end);
    for (size_t i = range.beg; i < range.end; ++i) state.xyz[i] = vec3_add(state.xyz[i], {12, 0, 0});
    ASSERT_TRUE(analyze_frame(&result, sys, state, bases, pairs, steps, validator));
    EXPECT_TRUE(result.pair_quality[p] & Quality_NoPairContacts);
    EXPECT_TRUE(std::isnan(result.pair_values[p].metric[Buckle]));
    EXPECT_EQ(vec3_length(result.pair_frames[p].z), 0);
    for (size_t s = 0; s < steps.size(); ++s) {
        if (steps[s].a != p && steps[s].b != p) continue;
        EXPECT_TRUE(result.step_quality[s] & Quality_AdjacentPair);
        EXPECT_TRUE(std::isnan(result.step_values[s].metric[Twist]));
    }
    std::copy(saved.begin(), saved.end(), state.xyz + range.beg);
    ASSERT_TRUE(analyze_frame(&result, sys, state, bases, pairs, steps, validator));
    EXPECT_EQ(result.pair_quality[p], (uint16_t)Quality_Good);
    EXPECT_NEAR(result.pair_values[p].metric[Buckle], original.metric[Buckle], 1.0e-5f);
    md_arena_allocator_destroy(alloc);
}

UTEST(lavery_core, hydrogen_free_rna_1rna_matches_independent_curves_reference_for_all_12_parameters) {
    auto* alloc = md_arena_allocator_create(md_get_heap_allocator(), MEGABYTES(4));
    md_system_t sys = {.alloc = alloc};
    md_system_state_t state = {.alloc = alloc};
    const auto path = (std::filesystem::path(__FILE__).parent_path() / "data/lavery_1rna.pdb").string();
    ASSERT_TRUE(md_pdb_system_init_from_file(&sys, &state, str_from_cstr(path.c_str()), {}));
    ASSERT_TRUE(md_util_system_infer(&sys, &state, MD_UTIL_INFER_ALL));
    const auto bases = gather_bases(sys);
    ASSERT_EQ(bases.size(), (size_t)28);
    std::vector<BaseFrame> frames;
    ASSERT_TRUE(extract_base_frames(frames, sys, state, bases));
    std::vector<Pair> pairs(bases.size());
    pairs.resize(detect_pairs(pairs.data(), pairs.size(), frames.data(), frames.size(), &state.unitcell));
    ASSERT_EQ(pairs.size(), (size_t)14);
    const auto steps = gather_steps(sys, bases, pairs.data(), pairs.size());
    ASSERT_EQ(steps.size(), (size_t)13);
    PairingValidator validator;
    ASSERT_TRUE(validator.initialize(sys, bases, pairs));
    FrameAnalysis result;
    ASSERT_TRUE(analyze_frame(&result, sys, state, bases, pairs, steps, validator));
    for (size_t p = 0; p < pairs.size(); ++p) {
        EXPECT_FALSE(validator.chemistry()[p].uses_hydrogens);
        EXPECT_EQ(result.pair_quality[p], (uint16_t)Quality_Good);
        EXPECT_EQ(result.contacts[p], 2);
        for (int k = 0; k < 6; ++k) EXPECT_NEAR(result.pair_values[p].metric[k], rna_curves_bp[p][k], k < 3 ? 0.06f : 0.006f);
    }
    for (size_t s = 0; s < steps.size(); ++s) {
        EXPECT_EQ(result.step_quality[s], (uint16_t)Quality_Good);
        for (int k = 0; k < 6; ++k) EXPECT_NEAR(result.step_values[s].metric[Tilt + k], rna_curves_step[s][k], k < 3 ? 0.06f : 0.006f);
    }
    const auto axis = compute_axis(result.pair_frames, steps, &state.unitcell);
    EXPECT_EQ(axis.offsets.size(), (size_t)2);
    EXPECT_EQ(axis.points.size(), (size_t)105);
    md_arena_allocator_destroy(alloc);
}

UTEST(lavery_core, published_series_copy_values_support_run_relative_scripts_and_clean_up_on_reset) {
    auto* alloc = md_arena_allocator_create(md_get_heap_allocator(), MEGABYTES(4));
    md_system_t sys = {.alloc = alloc};
    md_system_state_t state = {.alloc = alloc};
    const auto path = (std::filesystem::path(__FILE__).parent_path() / "data/lavery_1rna.pdb").string();
    ASSERT_TRUE(md_pdb_system_init_from_file(&sys, &state, str_from_cstr(path.c_str()), {}));
    ASSERT_TRUE(md_util_system_infer(&sys, &state, MD_UTIL_INFER_ALL));
    const double times[] = {0, 10, 20};
    const md_attribute_desc_t time = {
        .path = STR_INIT("run/test/time"),
        .format = {.type = MD_ATTRIBUTE_TYPE_F64, .components = 1, .rank = 1, .shape = {3}},
        .flags = MD_ATTRIBUTE_FLAG_TEMPORAL, .unit = md_unit_picosecond(),
        .data = times, .byte_size = sizeof(times),
    };
    ASSERT_NE(md_attributes_create(&sys.attributes, &time), MD_ATTRIBUTE_INVALID);
    std::vector<vec3_t> coordinates;
    for (int f = 0; f < 3; ++f) coordinates.insert(coordinates.end(), state.xyz, state.xyz + state.num_atoms);
    const float cells[27] = {};
    const md_attribute_desc_t position = {
        .path = STR_INIT("run/test/atom/position"),
        .format = {.type = MD_ATTRIBUTE_TYPE_F32, .components = 3, .rank = 2, .shape = {3, (uint32_t)state.num_atoms}},
        .flags = MD_ATTRIBUTE_FLAG_TEMPORAL, .unit = md_unit_angstrom(),
        .data = coordinates.data(), .byte_size = coordinates.size() * sizeof(vec3_t),
    };
    const md_attribute_desc_t cell = {
        .path = STR_INIT("run/test/unitcell"),
        .format = {.type = MD_ATTRIBUTE_TYPE_F32, .components = 1, .rank = 3, .shape = {3, 3, 3}},
        .flags = MD_ATTRIBUTE_FLAG_TEMPORAL, .unit = md_unit_angstrom(),
        .data = cells, .byte_size = sizeof(cells),
    };
    ASSERT_NE(md_attributes_create(&sys.attributes, &position), MD_ATTRIBUTE_INVALID);
    ASSERT_NE(md_attributes_create(&sys.attributes, &cell), MD_ATTRIBUTE_INVALID);
    std::vector<float> values = {1, 2, 3, 4, 5, 6};
    ASSERT_TRUE(publish_series(&sys.attributes, "run/test", "step/twist", "Twist", MD_ATTRIBUTE_TYPE_F32,
        3, 2, values.data(), values.size() * sizeof(float), md_unit_degree()));
    values[0] = 99;
    const auto* attr = md_attributes_find(&sys.attributes, STR_LIT("run/test/lavery/step/twist"));
    ASSERT_TRUE(attr != nullptr);
    EXPECT_EQ(attr->format.rank, 2u);
    EXPECT_EQ(attr->format.shape[0], 3u);
    EXPECT_EQ(attr->format.shape[1], 2u);
    EXPECT_EQ(((const float*)attr->data)[0], 1);
    EXPECT_TRUE(md_unit_equal(attr->unit, md_unit_degree()));
    const uint16_t quality[] = {0, Quality_NoPairContacts, 0, 0, 0, 0};
    ASSERT_TRUE(publish_series(&sys.attributes, "run/test", "step/quality", "Quality", MD_ATTRIBUTE_TYPE_U16,
        3, 2, quality, sizeof(quality), md_unit_none()));
    auto* ir = md_script_ir_create(sys.alloc);
    const bool compiled = md_script_ir_compile_from_source(ir,
        STR_LIT("twist = attr(\"lavery/step/twist\"); quality = attr(\"lavery/step/quality\");"), &sys, nullptr);
    if (!compiled) md_script_ir_write_diagnostics(md_writer_stderr(), ir);
    ASSERT_TRUE(compiled);
    EXPECT_EQ(md_script_ir_property_count(ir), (size_t)2);
    auto* eval = md_script_eval_create(3, ir, sys.alloc);
    ASSERT_TRUE(eval != nullptr);
    ASSERT_TRUE(md_script_eval_frame_range(eval, ir, &sys, STR_LIT("run/test"), 0, 3));
    const auto* twist = md_attributes_find(md_script_eval_attributes(eval), STR_LIT("script/twist"));
    ASSERT_TRUE(twist != nullptr);
    EXPECT_EQ(md_attribute_element_count(&twist->format), (size_t)6);
    EXPECT_EQ(((const float*)twist->data)[0], 1);
    EXPECT_EQ(((const float*)twist->data)[5], 6);
    md_script_eval_free(eval);
    md_script_ir_free(ir);
    ASSERT_TRUE(publish_series(&sys.attributes, "run/test", "step/twist", "Twist", MD_ATTRIBUTE_TYPE_F32,
        3, 2, values.data(), values.size() * sizeof(float), md_unit_degree()));
    attr = md_attributes_find(&sys.attributes, STR_LIT("run/test/lavery/step/twist"));
    ASSERT_TRUE(attr != nullptr);
    EXPECT_EQ(((const float*)attr->data)[0], 99);
    EXPECT_EQ(md_attributes_remove_prefix(&sys.attributes, STR_LIT("run/test/lavery")), (size_t)2);
    EXPECT_TRUE(md_attributes_find(&sys.attributes, STR_LIT("lavery/step/twist")) == nullptr);
    EXPECT_TRUE(md_attributes_find(&sys.attributes, STR_LIT("lavery/step/quality")) == nullptr);
    EXPECT_TRUE(md_attributes_find(&sys.attributes, STR_LIT("run/test/lavery/step/twist")) == nullptr);
    EXPECT_TRUE(md_attributes_find(&sys.attributes, STR_LIT("run/test/time")) != nullptr);
    EXPECT_FALSE(publish_series(&sys.attributes, "run/test", "step/twist", "Twist", MD_ATTRIBUTE_TYPE_F32,
        3, 2, values.data(), sizeof(float), md_unit_degree()));
    EXPECT_TRUE(md_attributes_find(&sys.attributes, STR_LIT("lavery/step/twist")) == nullptr);
    md_arena_allocator_destroy(alloc);
}

UTEST(lavery_core, axis_csv_preserves_segments_fractional_frame_coordinates_and_tangents) {
    AxisPath axis;
    axis.points = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
    axis.tangents = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    axis.offsets = {0, 2, 3};
    const auto csv = csv_axis("run,\"rna\"", 1.5, axis);
    EXPECT_TRUE(csv.find("\"run,\"\"rna\"\"\",1.5,1,0,1,2,3,1,0,0\n") != std::string::npos);
    EXPECT_TRUE(csv.find(",1.5,2,0,7,8,9,0,0,1\n") != std::string::npos);
    EXPECT_EQ(std::count(csv.begin(), csv.end(), '\n'), 4);
    axis.offsets.back() = 4;
    EXPECT_TRUE(csv_axis("", 0, axis).empty());
}
