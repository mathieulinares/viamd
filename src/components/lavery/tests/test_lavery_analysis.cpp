#include <components/lavery/lavery_core.h>
#include <utest.h>
#include <md_system.h>
#include <md_gro.h>
#include <md_xtc.h>
#include <md_pdb.h>
#include <md_script.h>
#include <md_util.h>
#include <core/md_arena_allocator.h>
#include <core/md_writer.h>
#include <core/md_unit.h>
#include <core/md_os.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include "data/lavery_rna_curves_reference.h"

using namespace lavery;

static std::array<float, 2> curves_fourier_pucker(const BaseTopology& base, const md_system_state_t& state) {
    const int ring[] = {BB_C1, BB_C2, BB_C3, BB_C4, BB_O4};
    double a = 0, b = 0;
    for (int j = 0; j < 5; ++j) {
        vec3_t xyz[4];
        for (int k = 0; k < 4; ++k) {
            const int32_t atom = base.backbone[ring[(j+k)%5]];
            if (atom < 0) return {NAN, NAN};
            xyz[k] = state.xyz[atom];
        }
        const double nu = torsion(xyz[0], xyz[1], xyz[2], xyz[3], &state.unitcell);
        const double angle = 144.0*j*(3.14159265358979323846/180.0);
        a += 0.4*nu*std::cos(angle);
        b -= 0.4*nu*std::sin(angle);
    }
    return {(float)(std::atan2(b,a)*(180.0/3.14159265358979323846)), (float)std::hypot(a,b)};
}

UTEST(lavery_core, optional_local_trajectory_measurement_validation) {
    const char* gro = std::getenv("VIAMD_LAVERY_VALIDATION_GRO");
    const char* xtc = std::getenv("VIAMD_LAVERY_VALIDATION_XTC");
    if (!gro && !xtc) UTEST_SKIP("Set VIAMD_LAVERY_VALIDATION_GRO and VIAMD_LAVERY_VALIDATION_XTC to check local data");
    ASSERT_TRUE(gro && xtc);
    auto* alloc = md_arena_allocator_create(md_get_heap_allocator(), MEGABYTES(32));
    md_system_t sys = {.alloc = alloc};
    md_system_state_t state = {.alloc = alloc};
    ASSERT_TRUE(md_gro_system_init_from_file(&sys, &state, str_from_cstr(gro)));
    ASSERT_TRUE(md_util_system_infer(&sys, &state, MD_UTIL_INFER_ALL));
    ASSERT_TRUE(md_xtc_system_publish_run(&sys, str_from_cstr(xtc), STR_LIT("run/check"), MD_RUN_FLAG_DISABLE_CACHE_WRITE));
    const auto* times = md_attributes_find(&sys.attributes, STR_LIT("run/check/time"));
    ASSERT_TRUE(times && times->data);
    const size_t frames = times->format.shape[0];
    ASSERT_GT(frames, (size_t)0);
    const str_t paths[] = {STR_LIT("atom/position"), STR_LIT("unitcell")};
    auto* extract = md_system_extract_begin(&sys, STR_LIT("run/check"), paths, 2, alloc);
    ASSERT_TRUE(extract);
    ASSERT_TRUE(md_system_extract_frame(extract, 0, &state));
    const auto bases = gather_bases(sys);
    std::vector<BaseFrame> base_frames;
    ASSERT_TRUE(extract_base_frames(base_frames, sys, state, bases));
    std::vector<Pair> pairs(bases.size());
    pairs.resize(detect_pairs(pairs.data(), pairs.size(), base_frames.data(), base_frames.size(), &state.unitcell));
    ASSERT_GT(pairs.size(), (size_t)0);
    const auto steps = gather_steps(sys, bases, pairs.data(), pairs.size());
    const auto chains = helix_chains(pairs.size(), steps);
    const char* curves_dir = std::getenv("VIAMD_LAVERY_VALIDATION_CURVES");
    std::array<std::vector<float>, Metric_Count> reference;
    std::array<std::vector<bool>, Metric_Count> reference_seen;
    std::array<size_t, Metric_Count> reference_checks = {};
    std::array<double, Metric_Count> reference_max = {};
    if (curves_dir) {
        for (int id = 0; id < (int)Metric_Count; ++id) {
            const auto cls = metric_class((Metric)id);
            const size_t count = cls == Class_Pair ? pairs.size() : cls == Class_Step ? steps.size() : 2*pairs.size();
            reference[id].assign(frames*count, NAN);
            reference_seen[id].assign(frames*count, false);
        }
        std::ifstream input(std::filesystem::path(curves_dir)/"curves.csv");
        ASSERT_TRUE(input.good());
        std::string line;
        ASSERT_TRUE((bool)std::getline(input, line));
        EXPECT_STREQ(line.c_str(), "frame,class,member,metric,value");
        std::array<size_t, Metric_Count> records = {};
        while (std::getline(input, line)) {
            size_t frame, member;
            int cls, id;
            float value;
            char extra;
            ASSERT_EQ(sscanf(line.c_str(), "%zu,%d,%zu,%d,%f%c", &frame, &cls, &member, &id, &value, &extra), 5);
            ASSERT_GE(id, 0);
            ASSERT_LT(id, (int)Metric_Count);
            ASSERT_EQ(cls, (int)metric_class((Metric)id));
            ASSERT_LT(frame, frames);
            const size_t count = reference[id].size()/frames;
            ASSERT_LT(member, count);
            ASSERT_FALSE(reference_seen[id][frame*count+member]);
            reference_seen[id][frame*count+member] = true;
            reference[id][frame*count+member] = value;
            ++records[id];
        }
        EXPECT_TRUE(input.eof());
        for (int id = 0; id < (int)Metric_Count; ++id) {
            EXPECT_EQ(records[id], id == MinorGroove || id == MajorGroove ? (size_t)0 : reference[id].size());
        }
    }
    const char* snapshot_dir = std::getenv("VIAMD_LAVERY_VALIDATION_EXPORT");
    if (snapshot_dir) {
        ASSERT_TRUE(std::filesystem::is_directory(snapshot_dir));
        // Explicit pair ordering avoids redetecting a different duplex in the reference tool.
        ASSERT_EQ(chains.size(), (size_t)1);
        ASSERT_EQ(chains[0].size(), pairs.size());
    }
    std::ofstream exported_values;
    if (snapshot_dir) {
        exported_values.open(std::filesystem::path(snapshot_dir)/"viamd.csv");
        ASSERT_TRUE(exported_values.good());
        exported_values << "frame,class,member,metric,value\n";
        std::ofstream pairing(std::filesystem::path(snapshot_dir)/"pairing.txt");
        ASSERT_TRUE(pairing.good());
        pairing << "2 1 -1 0 0\n";
        for (int side = 0; side < 2; ++side) {
            for (uint32_t p : chains[0]) {
                pairing << bases[side ? pairs[p].b : pairs[p].a].component+1 << ' ';
            }
            pairing << '\n';
        }
        ASSERT_TRUE(pairing.good());
    }
    PairingValidator validator;
    ASSERT_TRUE(validator.initialize(sys, bases, pairs));
    md_script_vis_t vis = {};
    md_script_vis_init(&vis, md_get_heap_allocator());
    size_t torsion_checks = 0, groove_checks = 0, distance_checks = 0;
    std::array<std::vector<float>, Metric_Count> values;
    const std::array<size_t, Class_Count> members = {pairs.size(), steps.size(), 2*pairs.size()};
    std::vector<uint16_t> pair_quality;
    std::array<std::array<size_t, 4>, 10> nucleotide_quality_counts = {};
    double analysis_ms = 0;
    for (size_t f = 0; f < frames; ++f) {
        ASSERT_TRUE(md_system_extract_frame(extract, f, &state));
        FrameAnalysis result;
        const auto analysis_begin = std::chrono::steady_clock::now();
        ASSERT_TRUE(analyze_frame(&result, sys, state, bases, pairs, steps, validator));
        ASSERT_EQ(result.nucleotide_quality.size(), result.nucleotide_values.size());
        for (size_t n = 0; n < result.nucleotide_values.size(); ++n) {
            for (int id = Alpha; id <= (int)EpsilonZeta; ++id) {
                const uint16_t q = result.nucleotide_quality[n][id-Alpha];
                const bool finite = std::isfinite(result.nucleotide_values[n].metric[id]);
                const uint16_t invalid = q & ~Quality_NucleotideWarnings;
                EXPECT_EQ(finite, invalid == 0);
                auto& counts = nucleotide_quality_counts[id-Alpha];
                counts[0] += (q & Quality_MissingAtoms) != 0;
                counts[1] += (q & Quality_NonfiniteCoordinates) != 0;
                counts[2] += (q & Quality_DegenerateNucleotide) != 0;
                counts[3] += (q & Quality_NucleotideWarnings) != 0;
            }
        }
        analysis_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-analysis_begin).count();
        pair_quality.insert(pair_quality.end(), result.pair_quality.begin(), result.pair_quality.end());
        for (int id = 0; id < (int)Metric_Count; ++id) {
            const auto cls = metric_class((Metric)id);
            const auto& rows = cls == Class_Pair ? result.pair_values :
                               cls == Class_Step ? result.step_values : result.nucleotide_values;
            ASSERT_EQ(rows.size(), members[cls]);
            for (const auto& row : rows) values[id].push_back(row.metric[id]);
            if (curves_dir && id != MinorGroove && id != MajorGroove) {
                for (size_t m = 0; m < rows.size(); ++m) {
                    float actual = rows[m].metric[id];
                    const float expected = reference[id][f*rows.size()+m];
                    if (!std::isfinite(actual)) continue; // VIAMD rejects failed fixed pairs, unlike Curves+.
                    if (id == Phase || id == Amplitude) {
                        const auto pair = pairs[m/2];
                        const auto fourier = curves_fourier_pucker(bases[m%2 ? pair.b : pair.a], state);
                        actual = fourier[id == Phase ? 0 : 1];
                    }
                    ASSERT_TRUE(std::isfinite(expected));
                    const double delta = metric_is_angular((Metric)id) ?
                        std::remainder((double)actual-expected, 360.0) : actual-expected;
                    const double tolerance = id == EpsilonZeta ? 0.11 : metric_uses_degrees((Metric)id) ? 0.061 : 0.0061;
                    EXPECT_NEAR(delta, 0.0, tolerance);
                    reference_max[id] = std::max(reference_max[id], std::fabs(delta));
                    ++reference_checks[id];
                }
            }
            if (snapshot_dir) {
                for (size_t m = 0; m < rows.size(); ++m) {
                    exported_values << f << ',' << (int)cls << ',' << m << ',' << id << ',' << rows[m].metric[id] << '\n';
                }
            }
        }
        if (snapshot_dir) {
            std::vector<vec3_t> whole_xyz(state.xyz, state.xyz+state.num_atoms);
            auto whole = state;
            whole.xyz = whole_xyz.data();
            md_util_unwrap_system(&whole, &sys);
            const auto first_pair = pairs[chains[0][0]];
            const auto& a = bases[first_pair.a];
            const auto& b = bases[first_pair.b];
            const vec3_t separation = vec3_sub(whole.xyz[b.ring[0]], whole.xyz[a.ring[0]]);
            vec3_t nearest = separation;
            md_util_min_image_vec3(&nearest, 1, &state.unitcell);
            const vec3_t shift = vec3_sub(nearest, separation);
            for (const auto& base : bases) if (base.strand == b.strand) {
                const auto range = md_component_atom_range(&sys.component, base.component);
                for (uint32_t atom = range.beg; atom < range.end; ++atom) whole.xyz[atom] = vec3_add(whole.xyz[atom], shift);
            }
            whole.unitcell = {};
            FrameAnalysis unwrapped;
            ASSERT_TRUE(analyze_frame(&unwrapped, sys, whole, bases, pairs, steps, validator));
            for (int id = 0; id < (int)Metric_Count; ++id) {
                const auto cls = metric_class((Metric)id);
                const auto& rows = cls == Class_Pair ? unwrapped.pair_values :
                                   cls == Class_Step ? unwrapped.step_values : unwrapped.nucleotide_values;
                for (size_t m = 0; m < rows.size(); ++m) {
                    const float original = values[id][f*rows.size()+m];
                    EXPECT_EQ(std::isfinite(original), std::isfinite(rows[m].metric[id]));
                    if (std::isfinite(original)) {
                        const double delta = rows[m].metric[id]-original;
                        EXPECT_NEAR(metric_is_angular((Metric)id) ? std::remainder(delta, 360.0) : delta, 0.0, 0.003);
                    }
                }
            }
            const auto path = (std::filesystem::path(snapshot_dir)/("frame_"+std::to_string(f)+".pdb")).string();
            md_file_t file = {};
            ASSERT_TRUE(md_file_open(&file, str_from_cstr(path.c_str()), MD_FILE_WRITE | MD_FILE_CREATE | MD_FILE_TRUNCATE));
            ASSERT_TRUE(md_pdb_system_write_state_to_file(file, &sys, &whole, nullptr, whole.num_atoms, 0));
            ASSERT_TRUE(md_file_close(&file));
        }
        const md_script_vis_ctx_t ctx = {.sys = &sys, .state = &state};
        const size_t n = f % (2*pairs.size());
        const auto& base = bases[n % 2 ? pairs[n/2].b : pairs[n/2].a];
        for (int k = (int)Alpha; k <= (int)Chi; ++k) {
            const auto atoms = nucleotide_torsion_atoms(base, (Metric)k);
            if (std::any_of(atoms.begin(), atoms.end(), [](int32_t a) { return a < 0; })) {
                EXPECT_TRUE(std::isnan(result.nucleotide_values[n].metric[k]));
                continue;
            }
            if (!std::isfinite(result.nucleotide_values[n].metric[k])) continue;
            char expression[128];
            snprintf(expression, sizeof(expression), "dihedral(%d,%d,%d,%d);", atoms[0]+1, atoms[1]+1, atoms[2]+1, atoms[3]+1);
            ASSERT_TRUE(md_script_vis_clear(&vis));
            ASSERT_TRUE(md_script_vis_eval_string(&vis, str_from_cstr(expression), &ctx,
                MD_SCRIPT_VISUALIZE_GEOMETRY | MD_SCRIPT_VISUALIZE_TEXT));
            ASSERT_EQ(md_array_size(vis.text), (size_t)1);
            const double degrees = vis.text[0].value * md_unit_scale(vis.text[0].unit) / md_unit_scale(md_unit_degree());
            EXPECT_NEAR(degrees, result.nucleotide_values[n].metric[k], 0.002);
            ++torsion_checks;
        }
        for (const auto& chain : chains) {
            const auto strand = bases[pairs[chain[0]].a].strand;
            auto phosphate = [&](uint32_t p, bool second) {
                const auto pair = pairs[p];
                const auto& a = bases[pair.a];
                const auto& b = bases[pair.b];
                return ((a.strand == strand) != second ? a : b).backbone[BB_P];
            };
            for (size_t s = 0; s < steps.size(); ++s) {
                const auto it = std::find(chain.begin(), chain.end(), steps[s].a);
                if (it == chain.end()) continue;
                const size_t k = (size_t)(it-chain.begin());
                if (k+1 >= chain.size() || chain[k+1] != steps[s].b) continue;
                for (Metric metric : {MinorGroove, MajorGroove}) {
                    const auto spans = groove_phosphate_indices(metric, k, chain.size());
                    bool valid = spans.count > 0;
                    double sum = 0;
                    for (size_t i = 0; i < spans.count; ++i) {
                        const int32_t a = phosphate(chain[spans.indices[i][0]], false);
                        const int32_t b = phosphate(chain[spans.indices[i][1]], true);
                        if (a < 0 || b < 0) { valid = false; break; }
                        vec3_t d = vec3_sub(state.xyz[b], state.xyz[a]);
                        md_util_min_image_vec3(&d, 1, &state.unitcell);
                        sum += vec3_length(d);
                        char expression[96];
                        snprintf(expression, sizeof(expression), "distance(%d,%d);", a+1, b+1);
                        ASSERT_TRUE(md_script_vis_clear(&vis));
                        ASSERT_TRUE(md_script_vis_eval_string(&vis, str_from_cstr(expression), &ctx,
                            MD_SCRIPT_VISUALIZE_GEOMETRY | MD_SCRIPT_VISUALIZE_TEXT));
                        ASSERT_EQ(md_array_size(vis.text), (size_t)1);
                        const double distance = vis.text[0].value * md_unit_scale(vis.text[0].unit) / md_unit_scale(md_unit_angstrom());
                        EXPECT_NEAR(distance, vec3_length(d), 0.0001);
                        ++distance_checks;
                    }
                    if (valid) {
                        EXPECT_NEAR(sum/spans.count-5.8, result.step_values[s].metric[metric], 0.0001);
                        ++groove_checks;
                    } else EXPECT_TRUE(std::isnan(result.step_values[s].metric[metric]));
                }
            }
        }
    }
    EXPECT_GT(torsion_checks, (size_t)0);
    EXPECT_GT(groove_checks, (size_t)0);
    if (curves_dir) for (int id = 0; id < (int)Metric_Count; ++id) {
        if (id == MinorGroove || id == MajorGroove) continue;
        EXPECT_GT(reference_checks[id], (size_t)0);
        printf("Independent Curves+ %s%s: %zu checks, max difference %.6f %s\n",
            metric_name((Metric)id), id == Phase || id == Amplitude ? " (Fourier convention)" : "",
            reference_checks[id], reference_max[id], metric_uses_degrees((Metric)id) ? "deg" : "A");
    }
    printf("Local Lavery validation: %zu frames, %zu pairs, %zu steps; %zu native dihedrals, %zu groove widths, %zu native distances\n",
        frames, pairs.size(), steps.size(), torsion_checks, groove_checks, distance_checks);
    for (int id = Alpha; id <= (int)EpsilonZeta; ++id) {
        const auto& counts = nucleotide_quality_counts[id-Alpha];
        printf("Nucleotide quality %s: %zu missing, %zu nonfinite, %zu degenerate, %zu warned samples\n",
            metric_name((Metric)id), counts[0], counts[1], counts[2], counts[3]);
    }
    EventOptions state_options;
    const auto* time_data = static_cast<const double*>(times->data);
    state_options.times.assign(time_data, time_data+frames);
    std::vector<double> intervals;
    for (size_t f = 1; f < frames; ++f) intervals.push_back(time_data[f]-time_data[f-1]);
    if (!intervals.empty()) {
        std::sort(intervals.begin(), intervals.end());
        state_options.min_duration = 2*intervals[intervals.size()/2];
    }
    size_t switches = 0;
    for (int id = (int)Alpha; id <= (int)EpsilonZeta; ++id) {
        if (!metric_state_count((Metric)id)) continue;
        MetricStatistics stats;
        ASSERT_TRUE(compute_statistics(&stats, values[id], 2*pairs.size(), true, Baseline_MeanSD,id));
        std::vector<StateSwitch> changes;
        ASSERT_TRUE(detect_state_switches(&changes, values[id], 2*pairs.size(), (Metric)id, state_options));
        for (const auto& change : changes) {
            EXPECT_NE(change.from, change.to);
            EXPECT_GE(time_data[change.end]-time_data[change.begin], state_options.min_duration);
            EXPECT_LE(std::fabs(change.angular_change), 180.0f);
        }
        size_t outliers = 0;
        for (size_t f = 0; f < frames; ++f) for (size_t m = 0; m < 2*pairs.size(); ++m) {
            const float v = values[id][f*2*pairs.size()+m];
            if (stats.members[m].is_outlier(v,2.5f)) ++outliers;
        }
        printf("State validation %s: %zu persistent switches; %zu within-state outliers (2.5 sigma)\n",
            metric_name((Metric)id), changes.size(), outliers);
        switches += changes.size();
    }
    printf("State validation total: %zu switches, min residence %.3f ps\n", switches, state_options.min_duration);
    if (snapshot_dir) {
        exported_values.close();
        ASSERT_TRUE(exported_values.good());
        printf("Reference export: %zu PDB snapshots, explicit pairing and viamd.csv in %s\n", frames, snapshot_dir);
    }
    for (int id = (int)Alpha; id <= (int)EpsilonZeta; ++id) {
        const auto metric = (Metric)id;
        if (!metric_state_count(metric)) continue;
        std::array<size_t, 3> occupancy = {};
        for (float v : values[id]) {
            const int s = metric_state(metric, v);
            if (s >= 0) ++occupancy[s];
        }
        printf("State occupancy %s:", metric_name(metric));
        for (int s = 0; s < metric_state_count(metric); ++s) {
            printf(" %s=%zu", metric_state_name(metric, s), occupancy[s]);
        }
        printf("\n");
        for (double residence : {10.0, 25.0, 50.0, 100.0}) {
            auto options = state_options;
            options.min_duration = residence;
            std::vector<StateSwitch> changes;
            ASSERT_TRUE(detect_state_switches(&changes, values[id], members[Class_Nucleotide], metric, options));
            for (const auto& change : changes) {
                EXPECT_GE(time_data[change.end]-time_data[change.begin], residence);
                EXPECT_EQ(metric_state(metric, values[id][change.begin*members[Class_Nucleotide]+change.member]), change.to);
                EXPECT_LE(change.source_begin, change.source_end);
                EXPECT_LT(change.source_end, change.begin);
                EXPECT_GE(time_data[change.source_end]-time_data[change.source_begin], residence);
                EXPECT_EQ(change.source_clipped, change.source_begin == options.first_frame);
                EXPECT_EQ(change.destination_clipped, change.end == frames-1);
                EXPECT_EQ(change.destination_at_trajectory_end, change.end == frames-1);
                for (size_t f = change.source_begin; f <= change.source_end; ++f) {
                    EXPECT_EQ(metric_state(metric, values[id][f*members[Class_Nucleotide]+change.member]), change.from);
                }
                if (residence == 50.0) {
                    printf("Switch candidate %s bp %u%c: %s -> %s, %.0f-%.0f ps, change %.2f deg\n",
                        metric_name(metric), change.member/2+1, change.member%2 ? 'b' : 'a',
                        metric_state_name(metric, change.from), metric_state_name(metric, change.to),
                        time_data[change.begin], time_data[change.end], change.angular_change);
                }
            }
            const size_t separated = std::count_if(changes.begin(), changes.end(),
                [](const StateSwitch& change) { return change.intervening_samples() != 0; });
            const size_t ongoing = std::count_if(changes.begin(), changes.end(),
                [](const StateSwitch& change) { return change.destination_at_trajectory_end; });
            printf("Residence sensitivity %s: %.0f ps -> %zu switches (%zu separated, %zu ongoing at trajectory end)\n",
                metric_name(metric), residence, changes.size(), separated, ongoing);
        }
    }
    printf("Pipeline timing: analysis %.3f ms total, %.3f ms/frame (native measurement checks excluded)\n",
        analysis_ms, analysis_ms/frames);
    printf("Pair quality: %zu/%zu flagged samples\n",
        (size_t)std::count_if(pair_quality.begin(), pair_quality.end(), [](uint16_t q) { return q != Quality_Good; }),
        pair_quality.size());
    for (int baseline = 0; baseline < Baseline_Count; ++baseline) {
        const auto stats_begin = std::chrono::steady_clock::now();
        std::array<MetricStatistics, Metric_Count> stats;
        std::array<DistributionComparison, Metric_Count> distributions;
        std::array<std::vector<float>, Metric_Count> zscores;
        for (int id = 0; id < (int)Metric_Count; ++id) {
            const auto metric = (Metric)id;
            const size_t count = members[metric_class(metric)];
            ASSERT_TRUE(compute_statistics(&stats[id], values[id], count, metric_is_angular(metric), (Baseline)baseline,
                metric_state_count(metric) ? id : -1));
            auto& distribution = distributions[id];
            ASSERT_TRUE(compute_distributions(&distribution, values[id], count));
            size_t finite = 0;
            for (size_t m = 0; m < count; ++m) {
                finite += distribution.valid[m];
                double integral = 0;
                for (size_t b = 0; b < distribution.centers.size(); ++b) {
                    integral += distribution.density[m*distribution.centers.size()+b]*distribution.bin_width;
                }
                EXPECT_NEAR(integral, distribution.valid[m] ? 1.0 : 0.0, 1e-6);
                EXPECT_EQ(stats[id].members[m].count, distribution.valid[m]);
                EventDistribution full;
                ASSERT_TRUE(compute_event_distribution(&full, values[id], count, m, 0, frames-1, distribution));
                EXPECT_EQ(full.samples.size(), distribution.valid[m]);
                EXPECT_EQ(full.samples.size()+full.missing, frames);
            }
            for (size_t i = 0; i < values[id].size(); ++i) {
                zscores[id].push_back(stats[id].members[i%count].zscore(values[id][i]));
            }
            if (baseline == Baseline_MeanSD) {
                printf("Metric coverage %s: %zu/%zu finite samples\n", metric_name(metric), finite, values[id].size());
            }
        }
        printf("Pipeline timing %s: statistics, histograms and z-scores %.3f ms\n", baseline_name((Baseline)baseline),
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-stats_begin).count());
        // Keep pair loss in every scope, as in the GUI; only the tested metric classes differ.
        for (int scope = 0; scope < 3; ++scope) {
            const char* scope_name[] = {"pair+step", "nucleotide", "all"};
            for (float sigma : {2.5f, 3.5f}) {
                const auto prepare_begin = std::chrono::steady_clock::now();
                std::vector<std::vector<MemberOutlier>> samples(frames);
                size_t active_frames = 0;
                for (size_t f = 0; f < frames; ++f) {
                    for (int cls = 0; cls < Class_Count; ++cls) {
                        const bool selected = scope == 2 || (scope == 0 ? cls != Class_Nucleotide : cls == Class_Nucleotide);
                        const int offset = class_first_metric(cls);
                        for (size_t m = 0; m < members[cls]; ++m) {
                            MemberOutlier sample;
                            sample.cls = (uint8_t)cls;
                            sample.member = (uint32_t)m;
                            constexpr uint16_t broken = Quality_Separation | Quality_Alignment | Quality_PlaneOffset | Quality_NoPairContacts;
                            const uint16_t q = cls == Class_Pair ? pair_quality[f*pairs.size()+m] : 0;
                            sample.lost = cls == Class_Pair && stats[Buckle].members[m].count && (q & broken) &&
                                !(q & ~(broken | Quality_AdjacentPair));
                            if (selected) for (int k = 0; k < class_parameter_count(cls); ++k) {
                                const float v = values[offset+k][f*members[cls]+m];
                                sample.z[k] = zscores[offset+k][f*members[cls]+m];
                                if (stats[offset+k].members[m].is_outlier(v, sigma-0.5f)) sample.contributors |= 1u << k;
                                if (stats[offset+k].members[m].is_outlier(v, sigma)) sample.strong |= 1u << k;
                            }
                            if (sample.contributors || sample.lost) samples[f].push_back(sample);
                        }
                    }
                    if (!samples[f].empty()) ++active_frames;
                }
                const double prepare_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-prepare_begin).count();
                for (double duration : {10.0, 50.0}) for (bool background : {false, true}) {
                    auto options = state_options;
                    options.min_duration = duration;
                    options.lost_weight = sigma;
                    options.burst_sigma = sigma+1.0f;
                    if (background) {
                        options.residual_sigma = 1.0f;
                        options.member_z = [&](size_t f, int cls, uint32_t m, int k) {
                            const bool selected = scope == 2 || (scope == 0 ? cls != Class_Nucleotide : cls == Class_Nucleotide);
                            return selected ? zscores[class_first_metric(cls)+k][f*members[cls]+m] :
                                std::numeric_limits<float>::quiet_NaN();
                        };
                    }
                    const auto detect_begin = std::chrono::steady_clock::now();
                    const auto events = detect_events(samples, pairs.size(), steps, options);
                    const double detect_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-detect_begin).count();
                    size_t parents = 0;
                    std::vector<bool> coverage(frames);
                    for (const auto& event : events) {
                        ASSERT_LE(event.begin, event.end);
                        ASSERT_LT(event.end, frames);
                        EXPECT_GE(time_data[event.end]-time_data[event.begin], duration);
                        for (const auto& member : event.members) {
                            if (member.peak_parameter < 0) continue;
                            const int id = class_first_metric(member.cls)+member.peak_parameter;
                            EventDistribution selected;
                            ASSERT_TRUE(compute_event_distribution(&selected, values[id], members[member.cls], member.member,
                                event.begin, event.end, distributions[id]));
                            EXPECT_EQ(selected.samples.size()+selected.missing, event.end-event.begin+1);
                        }
                        if (event.parent != SIZE_MAX) {
                            ASSERT_LT(event.parent, events.size());
                            EXPECT_GE(event.begin, events[event.parent].begin);
                            EXPECT_LE(event.end, events[event.parent].end);
                            continue;
                        }
                        ++parents;
                        for (size_t f = event.begin; f <= event.end; ++f) coverage[f] = true;
                    }
                    const size_t covered = (size_t)std::count(coverage.begin(), coverage.end(), true);
                    printf("Event sensitivity %s, %s, %.1f/%.1f sigma, %.0f ps, %s: %zu parents + %zu children, %zu/%zu covered frames, %zu active; prepare %.3f ms, detect %.3f ms\n",
                        baseline_name((Baseline)baseline), scope_name[scope], sigma, sigma-0.5f, duration,
                        background ? "background" : "stronger", parents, events.size()-parents, covered, frames,
                        active_frames, prepare_ms, detect_ms);
                }
            }
        }
    }
    md_script_vis_free(&vis);
    md_system_extract_end(extract);
    md_arena_allocator_destroy(alloc);
}

UTEST(lavery_core, native_dihedral_visual_values_agree_with_nucleotide_torsions) {
    auto* alloc = md_arena_allocator_create(md_get_heap_allocator(), MEGABYTES(16));
    md_system_t sys = {.alloc = alloc};
    md_system_state_t state = {.alloc = alloc};
    ASSERT_TRUE(md_gro_system_init_from_file(&sys, &state, STR_LIT(VIAMD_TEST_DATA_DIR "/nucl-dna.gro")));
    ASSERT_TRUE(md_util_system_infer(&sys, &state, MD_UTIL_INFER_ALL));
    const auto bases = gather_bases(sys);
    ASSERT_GT(bases.size(), (size_t)50);
    const auto& base = bases[50];
    vec3_t positions[BB_Count];
    for (int k = 0; k < BB_Count; ++k) {
        ASSERT_GE(base.backbone[k], 0);
        positions[k] = state.xyz[base.backbone[k]];
    }
    Values values;
    compute_nucleotide(&values, positions, &state.unitcell);
    md_script_vis_t vis = {};
    md_script_vis_init(&vis, md_get_heap_allocator());
    const md_script_vis_ctx_t ctx = {.sys = &sys, .state = &state};
    for (int k = (int)Alpha; k <= (int)Chi; ++k) {
        const auto atoms = nucleotide_torsion_atoms(base, (Metric)k);
        char expression[128];
        snprintf(expression, sizeof(expression), "dihedral(%d,%d,%d,%d);",
            atoms[0]+1, atoms[1]+1, atoms[2]+1, atoms[3]+1);
        ASSERT_TRUE(md_script_vis_clear(&vis));
        ASSERT_TRUE(md_script_vis_eval_string(&vis, str_from_cstr(expression), &ctx,
            MD_SCRIPT_VISUALIZE_GEOMETRY | MD_SCRIPT_VISUALIZE_TEXT));
        ASSERT_EQ(md_array_size(vis.text), (size_t)1);
        const double degrees = vis.text[0].value * md_unit_scale(vis.text[0].unit) / md_unit_scale(md_unit_degree());
        EXPECT_NEAR(degrees, values.metric[k], 0.001);
        EXPECT_GT(md_array_size(vis.lines), (size_t)0);
    }
    md_script_vis_free(&vis);
    md_arena_allocator_destroy(alloc);
}

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
    for (const auto& base : bases) {
        ASSERT_LT(base.component, (uint32_t)28);
        const auto pucker = curves_fourier_pucker(base, state);
        EXPECT_NEAR(pucker[0], rna_curves_pucker[base.component][0], 0.06f);
        EXPECT_NEAR(pucker[1], rna_curves_pucker[base.component][1], 0.06f);
    }
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
