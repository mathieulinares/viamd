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
#include <iomanip>
#include <sstream>
#include "data/lavery_rna_curves_reference.h"
#include "data/lavery_1rna_3dna_frames.h"

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
    auto* alloc = md_arena_allocator_create(md_get_heap_allocator(), MD_MEGABYTES(32));
    md_system_t sys = {.alloc = alloc};
    md_system_state_t state = {.alloc = alloc};
    ASSERT_TRUE(md_gro_system_init_from_file(&sys, &state, md_str_from_cstr(gro), nullptr));
    ASSERT_TRUE(md_util_system_infer(&sys, &state, MD_UTIL_INFER_ALL, nullptr));
    ASSERT_TRUE(md_xtc_system_publish_run(&sys, md_str_from_cstr(xtc), MD_STR_LIT("run/check"), MD_RUN_FLAG_DISABLE_CACHE_WRITE));
    const auto* times = md_attributes_find(&sys.attributes, MD_STR_LIT("run/check/time"));
    ASSERT_TRUE(times && times->data);
    const size_t frames = times->format.shape[0];
    ASSERT_GT(frames, (size_t)0);
    const md_str_t paths[] = {MD_STR_LIT("atom/position"), MD_STR_LIT("unitcell")};
    auto* extract = md_system_extract_begin(&sys, MD_STR_LIT("run/check"), paths, 2, alloc);
    ASSERT_TRUE(extract);
    ASSERT_TRUE(md_system_extract_frame(extract, 0, &state));
    const auto bases = gather_bases(sys);
    std::vector<BaseFrame> base_frames;
    ASSERT_TRUE(extract_base_frames(base_frames, sys, state, bases));
    std::vector<Pair> pairs(bases.size());
    pairs.resize(detect_pairs(pairs.data(), pairs.size(), base_frames.data(), base_frames.size(), &state.unitcell));
    PairingValidator first_validator;
    std::vector<uint16_t> first_quality;
    std::vector<uint8_t> first_contacts;
    std::vector<PairingEvidence> first_evidence;
    ASSERT_TRUE(first_validator.initialize(sys, bases, pairs));
    ASSERT_TRUE(first_validator.evaluate(first_quality, first_contacts, state, &first_evidence));
    // Match the application's frame-0 pair typing and orientation before gathering steps.
    size_t retained = 0;
    for (size_t p = 0; p < pairs.size(); ++p) {
        const bool parallel = vec3_dot(base_frames[pairs[p].a].z, base_frames[pairs[p].b].z) > 0.0f;
        const PairType type = identify_pair_type(first_validator.chemistry()[p], first_evidence[p], parallel);
        if (type == PairType_Count) continue;
        pairs[p].type = (uint8_t)type;
        pairs[p].reversed = type == PairType_ReverseWatsonCrick;
        pairs[retained++] = pairs[p];
    }
    pairs.resize(retained);
    ASSERT_GT(pairs.size(), (size_t)0);
    const auto steps = gather_steps(sys, bases, pairs.data(), pairs.size());
    const auto chains = helix_chains(pairs.size(), steps);
    printf("Local topology: %zu bases, %zu helix chains\n", bases.size(), chains.size());
    for (size_t c = 0; c < chains.size(); ++c) {
        printf("Helix %zu: %zu pairs\n", c+1, chains[c].size());
    }
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
            EXPECT_EQ(records[id], id == MinorGroove || id == MajorGroove || (HELICAL_PARAMETERS & (1u << id))
                ? (size_t)0 : reference[id].size());
        }
    }
    const char* snapshot_dir = std::getenv("VIAMD_LAVERY_VALIDATION_EXPORT");
    if (snapshot_dir) {
        ASSERT_TRUE(std::filesystem::is_directory(snapshot_dir));
        // Explicit pair ordering avoids redetecting a different duplex in the reference tool.
        ASSERT_GT(chains.size(), (size_t)0);
    }
    std::ofstream exported_values;
    std::ofstream exported_pairing;
    const char* pairing_path = std::getenv("VIAMD_LAVERY_VALIDATION_PAIRING");
    if (pairing_path || snapshot_dir) {
        exported_pairing.open(pairing_path ? std::filesystem::path(pairing_path) : std::filesystem::path(snapshot_dir)/"pairing-states.csv");
        ASSERT_TRUE(exported_pairing.good());
        exported_pairing << "frame,pair,component_a,component_b,state,canonical_contacts\n";
    }
    if (snapshot_dir) {
        exported_values.open(std::filesystem::path(snapshot_dir)/"viamd.csv");
        ASSERT_TRUE(exported_values.good());
        exported_values << "frame,class,member,metric,value,quality\n" << std::setprecision(9);
        std::ofstream pairing(std::filesystem::path(snapshot_dir)/"pairing.txt");
        ASSERT_TRUE(pairing.good());
        for (size_t c = 0; c < chains.size(); ++c) {
            std::ofstream chain_pairing(std::filesystem::path(snapshot_dir)/("pairing_"+std::to_string(c)+".txt"));
            ASSERT_TRUE(chain_pairing.good());
            chain_pairing << "2 1 -1 0 0\n";
            for (int side = 0; side < 2; ++side) {
                for (uint32_t p : chains[c]) {
                    chain_pairing << bases[side ? pairs[p].b : pairs[p].a].component+1 << ' ';
                }
                chain_pairing << '\n';
            }
            ASSERT_TRUE(chain_pairing.good());
        }
        std::ofstream topology(std::filesystem::path(snapshot_dir)/"members.csv");
        ASSERT_TRUE(topology.good());
        topology << "class,member,a,b\n";
        for (size_t p = 0; p < pairs.size(); ++p) {
            topology << "0," << p << ',' << bases[pairs[p].a].component+1 << ',' << bases[pairs[p].b].component+1 << '\n';
        }
        for (size_t s = 0; s < steps.size(); ++s) {
            topology << "1," << s << ',' << steps[s].a << ',' << steps[s].b << '\n';
        }
        ASSERT_TRUE(topology.good());
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
    std::array<std::vector<uint16_t>, Metric_Count> sample_quality;
    const std::array<size_t, Class_Count> members = {pairs.size(), steps.size(), 2*pairs.size()};
    auto base_label=[&](size_t b) {
        const auto& base=bases[b];
        const md_str_t name=md_system_residue_name(&sys,base.component);
        return "S"+std::to_string(base.strand+1)+":"+std::string(name.ptr,name.len)+
            std::to_string(md_system_residue_seq_id(&sys,base.component));
    };
    auto pair_label=[&](size_t p) {
        return std::to_string(p+1)+": "+base_label(pairs[p].a)+" / "+base_label(pairs[p].b);
    };
    auto member_sample=[&](int cls,size_t m,size_t f) {
        CsvSample sample;
        sample.run="local_validation";
        sample.frame=f; sample.time=((const double*)times->data)[f]; sample.time_unit="ps";
        sample.member=m+1;
        sample.pair_a=pair_label(cls==Class_Step ? steps[m].a : cls==Class_Nucleotide ? m/2 : m);
        if (cls==Class_Step) {
            sample.pair_b=pair_label(steps[m].b);
            sample.label=std::to_string(steps[m].a+1)+"-"+std::to_string(steps[m].b+1)+": "+sample.pair_a+" -> "+sample.pair_b;
        } else if (cls==Class_Nucleotide) {
            sample.label=base_label(m%2 ? pairs[m/2].b : pairs[m/2].a)+" (bp "+std::to_string(m/2+1)+")";
        } else sample.label=sample.pair_a;
        return sample;
    };
    std::ofstream sample_file;
    if (const char* path=std::getenv("VIAMD_LAVERY_VALIDATION_SAMPLES")) {
        sample_file.open(path);
        ASSERT_TRUE(sample_file.good());
        sample_file << csv_header();
    }
    std::ofstream evidence_file;
    if (const char* path=std::getenv("VIAMD_LAVERY_VALIDATION_EVIDENCE")) {
        evidence_file.open(path);
        ASSERT_TRUE(evidence_file.good());
        evidence_file << "frame,pair,quality,canonical_contacts\n";
    }
    std::ofstream dna_pair_step_file;
    if (const char* path = std::getenv("VIAMD_LAVERY_VALIDATION_3DNA_PAIR_STEP")) {
        dna_pair_step_file.open(path);
        ASSERT_TRUE(dna_pair_step_file.good());
        dna_pair_step_file << "frame,class,member,a,b,metric,value,quality\n" << std::setprecision(9);
    }
    std::ofstream dna_groove_file;
    if (const char* path = std::getenv("VIAMD_LAVERY_VALIDATION_3DNA_GROOVES")) {
        dna_groove_file.open(path);
        ASSERT_TRUE(dna_groove_file.good());
        dna_groove_file << "frame,member,metric,value\n" << std::setprecision(9);
    }
    std::ofstream dna_nucleotide_file;
    if (const char* path = std::getenv("VIAMD_LAVERY_VALIDATION_3DNA_NUCLEOTIDES")) {
        dna_nucleotide_file.open(path);
        ASSERT_TRUE(dna_nucleotide_file.good());
        dna_nucleotide_file << "frame,member,metric,value,quality\n" << std::setprecision(9);
    }
    std::ofstream axis_file;
    if (const char* path = std::getenv("VIAMD_LAVERY_VALIDATION_CURVES_AXIS")) {
        ASSERT_EQ(chains.size(), (size_t)1);
        axis_file.open(path);
        ASSERT_TRUE(axis_file.good());
        axis_file << "frame,pair,ux,uy,uz,x,y,z,xdisp,ydisp,inclin,tip,bend,hrise,htwist\n" << std::setprecision(9);
    }
    std::ofstream curves_groove_file;
    if (const char* path = std::getenv("VIAMD_LAVERY_VALIDATION_CURVES_GROOVES")) {
        ASSERT_EQ(chains.size(), (size_t)1);
        curves_groove_file.open(path);
        ASSERT_TRUE(curves_groove_file.good());
        curves_groove_file << "frame,index,level,width12,depth12,width21,depth21\n" << std::setprecision(9);
    }
    std::ofstream curves_nucleotide_file;
    if (const char* path = std::getenv("VIAMD_LAVERY_VALIDATION_CURVES_NUCLEOTIDES")) {
        ASSERT_GT(chains.size(), (size_t)0);
        curves_nucleotide_file.open(path);
        ASSERT_TRUE(curves_nucleotide_file.good());
        curves_nucleotide_file << "frame,class,member,metric,value,quality\n" << std::setprecision(9);
    }
    std::ofstream curves_pair_step_file;
    if (const char* path = std::getenv("VIAMD_LAVERY_VALIDATION_CURVES_PAIR_STEP")) {
        curves_pair_step_file.open(path);
        ASSERT_TRUE(curves_pair_step_file.good());
        curves_pair_step_file << "frame,class,member,metric,value,quality\n" << std::setprecision(9);
    }
    std::vector<uint16_t> pair_quality;
    std::array<std::array<size_t, 4>, 10> nucleotide_quality_counts = {};
    double analysis_ms = 0;
    for (size_t f = 0; f < frames; ++f) {
        ASSERT_TRUE(md_system_extract_frame(extract, f, &state));
        FrameAnalysis result;
        const auto analysis_begin = std::chrono::steady_clock::now();
        ASSERT_TRUE(analyze_frame(&result, sys, state, bases, pairs, steps, validator));
        CurvesFrameAnalysis curves_frame;
        if (curves_nucleotide_file.is_open() || curves_pair_step_file.is_open() ||
            axis_file.is_open() || curves_groove_file.is_open())
            ASSERT_TRUE(analyze_curves_frame(&curves_frame, sys, state, bases, pairs, steps, validator));
        if (curves_pair_step_file.is_open()) {
            for (size_t p = 0; p < pairs.size(); ++p)
                for (int id = Buckle; id <= (int)Stagger; ++id)
                    curves_pair_step_file << f << ",0," << p << ',' << id << ','
                                          << curves_frame.frame.pair_values[p].metric[id] << ','
                                          << curves_frame.frame.pair_quality[p] << '\n';
            for (size_t s = 0; s < steps.size(); ++s)
                for (int id = Tilt; id <= (int)Rise; ++id)
                    curves_pair_step_file << f << ",1," << s << ',' << id << ','
                                          << curves_frame.frame.step_values[s].metric[id] << ','
                                          << curves_frame.frame.step_quality[s] << '\n';
            ASSERT_TRUE(curves_pair_step_file.good());
        }
        if (curves_nucleotide_file.is_open()) {
            for (size_t p = 0; p < pairs.size(); ++p) {
                for (int side = 0; side < 2; ++side) {
                    const auto& nucleotide = curves_frame.frame.nucleotide_values[2*p + side];
                    const auto& quality = curves_frame.frame.nucleotide_quality[2*p + side];
                    for (int id = Alpha; id <= (int)EpsilonZeta; ++id)
                        curves_nucleotide_file << f << ",2," << 2*p+side << ',' << id << ','
                                               << nucleotide.metric[id] << ',' << quality[id-Alpha] << '\n';
                }
            }
            ASSERT_TRUE(curves_nucleotide_file.good());
        }
        FrameAnalysis dna;
        if (dna_pair_step_file.is_open() || dna_groove_file.is_open() || dna_nucleotide_file.is_open())
            ASSERT_TRUE(analyze_3dna_frame(&dna, sys, state, bases, pairs, steps, validator));
        if (dna_nucleotide_file.is_open()) {
            for (size_t member = 0; member < dna.nucleotide_values.size(); ++member)
                for (int id = Alpha; id <= (int)EpsilonZeta; ++id)
                    dna_nucleotide_file << f << ',' << member << ',' << id << ','
                                        << dna.nucleotide_values[member].metric[id] << ','
                                        << dna.nucleotide_quality[member][id-Alpha] << '\n';
            ASSERT_TRUE(dna_nucleotide_file.good());
        }
        if (dna_pair_step_file.is_open()) {
            for (size_t p = 0; p < pairs.size(); ++p)
                for (int id = Buckle; id <= (int)Stagger; ++id)
                    dna_pair_step_file << f << ",0," << p << ',' << bases[pairs[p].a].component+1 << ','
                        << bases[pairs[p].b].component+1 << ',' << id << ',' << dna.pair_values[p].metric[id]
                        << ',' << dna.pair_quality[p] << '\n';
            for (size_t s = 0; s < steps.size(); ++s)
                for (int id = Tilt; id <= (int)Rise; ++id)
                    dna_pair_step_file << f << ",1," << s << ',' << steps[s].a << ',' << steps[s].b << ','
                        << id << ',' << dna.step_values[s].metric[id] << ',' << dna.step_quality[s] << '\n';
            for (size_t s = 0; s < steps.size(); ++s)
                for (int id = XDisplacement; id <= (int)HelicalTwist; ++id)
                    dna_pair_step_file << f << ",1," << s << ',' << steps[s].a << ',' << steps[s].b << ','
                        << id << ',' << dna.step_values[s].metric[id] << ',' << dna.helical_quality[s] << '\n';
            ASSERT_TRUE(dna_pair_step_file.good());
        }
        if (dna_groove_file.is_open()) {
            std::vector<int64_t> step_from(pairs.size(), -1);
            for (size_t s = 0; s < steps.size(); ++s) step_from[steps[s].a] = (int64_t)s;
            const vec3_t missing = {NAN, NAN, NAN};
            auto position = [&](int32_t atom) { return atom >= 0 ? state.xyz[atom] : missing; };
            for (const auto& chain : chains) {
                std::vector<vec3_t> p1, p2, c1, c2;
                for (uint32_t p : chain) {
                    const auto& a = bases[pairs[p].a];
                    const auto& b = bases[pairs[p].b];
                    p1.push_back(position(a.backbone[BB_P]));
                    p2.push_back(position(b.backbone[BB_P]));
                    c1.push_back(position(a.backbone[BB_C1]));
                    c2.push_back(position(b.backbone[BB_C1]));
                }
                std::vector<float> minor(chain.size() - 1), major(chain.size() - 1);
                groove_widths(minor.data(), major.data(), p1.data(), p2.data(), chain.size(),
                              &state.unitcell, c1.data(), c2.data(), Groove_3DnaRefined);
                for (size_t i = 0; i + 1 < chain.size(); ++i) {
                    ASSERT_GE(step_from[chain[i]], 0);
                    ASSERT_EQ(steps[step_from[chain[i]]].b, chain[i+1]);
                    const auto& calculated = dna.step_values[(size_t)step_from[chain[i]]];
                    for (int id : {MinorGroove, MajorGroove}) {
                        const float expected = id == MinorGroove ? minor[i] : major[i];
                        const float actual = calculated.metric[id];
                        EXPECT_EQ(std::isfinite(actual), std::isfinite(expected));
                        if (std::isfinite(expected)) EXPECT_NEAR(actual, expected, 1e-5f);
                    }
                    dna_groove_file << f << ',' << step_from[chain[i]] << ',' << (int)MinorGroove << ','
                                    << calculated.metric[MinorGroove] << '\n'
                                    << f << ',' << step_from[chain[i]] << ',' << (int)MajorGroove << ','
                                    << calculated.metric[MajorGroove] << '\n';
                }
            }
            ASSERT_TRUE(dna_groove_file.good());
        }
        ASSERT_EQ(result.pairing_states.size(), pairs.size());
        if (evidence_file.is_open()) for (size_t p=0; p<pairs.size(); ++p)
            evidence_file << f << ',' << p+1 << ',' << result.pair_quality[p] << ',' << (unsigned)result.contacts[p] << '\n';
        if (exported_pairing.is_open()) for (size_t p = 0; p < pairs.size(); ++p)
            exported_pairing << f << ',' << p+1 << ',' << bases[pairs[p].a].component+1 << ','
                << bases[pairs[p].b].component+1 << ',' << pairing_state_name(result.pairing_states[p]) << ','
                << (unsigned)result.contacts[p] << '\n';
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
            for (size_t m=0; m<rows.size(); ++m) {
                values[id].push_back(rows[m].metric[id]);
                sample_quality[id].push_back((HELICAL_PARAMETERS & (1u << id)) ? result.helical_quality[m] :
                    cls==Class_Nucleotide ? result.nucleotide_quality[m][id-Alpha] :
                    cls==Class_Step ? result.step_quality[m] : result.pair_quality[m]);
            }
            if (sample_file.is_open()) for (size_t m=0; m<rows.size(); ++m) {
                auto sample=member_sample(cls,m,f);
                sample.metric=(Metric)id;
                sample.value=rows[m].metric[id];
                sample.quality=(HELICAL_PARAMETERS & (1u << id)) ? result.helical_quality[m] :
                    cls==Class_Nucleotide ? result.nucleotide_quality[m][id-Alpha] :
                    cls==Class_Step ? result.step_quality[m] : result.pair_quality[m];
                sample_file << csv_sample(sample);
            }
            if (curves_dir && id != MinorGroove && id != MajorGroove && !(HELICAL_PARAMETERS & (1u << id))) {
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
                    exported_values << f << ',' << (int)cls << ',' << m << ',' << id << ',' << rows[m].metric[id]
                                    << ',' << sample_quality[id][f*rows.size()+m] << '\n';
                }
            }
        }
        if (snapshot_dir || axis_file.is_open() || curves_groove_file.is_open()) {
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
                const auto range = md_system_residue_atom_range(&sys, base.component);
                for (uint32_t atom = range.beg; atom < range.end; ++atom) whole.xyz[atom] = vec3_add(whole.xyz[atom], shift);
            }
            whole.unitcell = {};
            FrameAnalysis unwrapped;
            ASSERT_TRUE(analyze_frame(&unwrapped, sys, whole, bases, pairs, steps, validator));
            for (size_t p = 0; p < pairs.size(); ++p) EXPECT_EQ(unwrapped.pairing_states[p], result.pairing_states[p]);
            for (int id = 0; id < (int)Metric_Count; ++id) {
                const auto cls = metric_class((Metric)id);
                const auto& rows = cls == Class_Pair ? unwrapped.pair_values :
                                   cls == Class_Step ? unwrapped.step_values : unwrapped.nucleotide_values;
                for (size_t m = 0; m < rows.size(); ++m) {
                    const float original = values[id][f*rows.size()+m];
                    EXPECT_EQ(std::isfinite(original), std::isfinite(rows[m].metric[id]));
                    if (std::isfinite(original)) {
                        const double delta = rows[m].metric[id]-original;
                        const double difference = metric_is_angular((Metric)id) ? std::remainder(delta, 360.0) : delta;
                        if (std::fabs(difference) > 0.003)
                            printf("Unwrapped mismatch frame=%zu class=%d member=%zu metric=%s periodic=%.9g whole=%.9g delta=%.9g\n",
                                   f, (int)cls, m, metric_name((Metric)id), original, rows[m].metric[id], difference);
                        EXPECT_NEAR(difference, 0.0, 0.003);
                    }
                }
            }
            if (axis_file.is_open() || curves_groove_file.is_open()) {
                std::vector<BaseFrame> axis_bases;
                ASSERT_TRUE(extract_base_frames(axis_bases, sys, whole, bases));
                std::vector<BaseFrame> a, b;
                std::vector<BasePairFrame> midpoint;
                bool valid = true;
                for (uint32_t p : chains[0]) {
                    a.push_back(axis_bases[pairs[p].a]);
                    b.push_back(axis_bases[pairs[p].b]);
                    BasePairFrame frame;
                    Values unused;
                    if (!compute_base_pair(&unused, &frame, a.back(), b.back(), pairs[p].reversed)) valid = false;
                    midpoint.push_back(frame);
                }
                std::vector<CurvesAxisPoint> axis;
                if (valid) valid = curves_duplex_axis(&axis, a, b, midpoint);
                std::vector<StepIndex> adjacent;
                for (uint32_t i = 0; i + 1 < midpoint.size(); ++i) adjacent.push_back({i, i + 1});
                const std::vector<uint8_t> available(midpoint.size(), valid ? 1 : 0);
                const auto inversion = curves_pair_inversions(midpoint, available, adjacent);
                std::vector<CurvesPairAxisParameters> pair_axis;
                std::vector<CurvesStepAxisParameters> step_axis;
                const bool valid_params = valid && curves_pair_axis_parameters(&pair_axis, midpoint, axis, inversion) &&
                                          curves_step_axis_parameters(&step_axis, midpoint, axis, inversion);
                const auto& integrated = curves_frame.helices;
                ASSERT_EQ(integrated.size(), (size_t)1);
                ASSERT_TRUE(integrated[0].pairs == chains[0]);
                ASSERT_EQ(integrated[0].axis_quality == Quality_Good, valid_params);
                vec3_t image_shift = {};
                if (valid_params) {
                    ASSERT_EQ(integrated[0].axis.size(), axis.size());
                    image_shift = vec3_sub(axis[0].origin, integrated[0].axis[0].origin);
                    for (size_t i = 0; i < axis.size(); ++i) {
                        EXPECT_NEAR(vec3_length(vec3_sub(integrated[0].axis[i].direction, axis[i].direction)), 0.0f, 0.003f);
                        EXPECT_NEAR(vec3_length(vec3_sub(vec3_add(integrated[0].axis[i].origin, image_shift),
                                                         axis[i].origin)), 0.0f, 0.003f);
                    }
                }
                if (axis_file.is_open()) {
                    for (size_t i = 0; i < chains[0].size(); ++i) {
                        const vec3_t u = valid_params ? integrated[0].axis[i].direction : vec3_t{NAN, NAN, NAN};
                        const vec3_t p = valid_params ? vec3_add(integrated[0].axis[i].origin, image_shift) : vec3_t{NAN, NAN, NAN};
                        axis_file << f << ',' << chains[0][i] << ',' << u.x << ',' << u.y << ',' << u.z << ','
                                  << p.x << ',' << p.y << ',' << p.z << ','
                                  << (valid_params ? integrated[0].pair_parameters[i].x_displacement : NAN) << ','
                                  << (valid_params ? integrated[0].pair_parameters[i].y_displacement : NAN) << ','
                                  << (valid_params ? integrated[0].pair_parameters[i].inclination : NAN) << ','
                                  << (valid_params ? integrated[0].pair_parameters[i].tip : NAN) << ','
                                  << (valid_params ? integrated[0].pair_parameters[i].axis_bend : NAN) << ','
                                  << (valid_params && i < integrated[0].step_parameters.size() ? integrated[0].step_parameters[i].rise : NAN) << ','
                                  << (valid_params && i < integrated[0].step_parameters.size() ? integrated[0].step_parameters[i].twist : NAN) << '\n';
                    }
                    ASSERT_TRUE(axis_file.good());
                }
                if (curves_groove_file.is_open()) {
                    std::array<std::vector<vec3_t>, 2> phosphates, backbones, depth;
                    for (size_t i = 0; i < chains[0].size(); ++i) {
                        const auto pair = pairs[chains[0][i]];
                        depth[0].push_back(axis_bases[pair.a].groove_reference);
                        depth[1].push_back(axis_bases[pair.b].groove_reference);
                        for (int side = 0; side < 2; ++side) {
                            if ((!side && i == 0) || (side && i + 1 == chains[0].size())) continue;
                            const auto& base = bases[side ? pair.b : pair.a];
                            const int32_t atom = base.backbone[BB_P];
                            phosphates[side].push_back(atom >= 0 ? whole.xyz[atom] : vec3_t{NAN, NAN, NAN});
                        }
                    }
                    std::vector<CurvesGrooveSample> grooves;
                    const bool groove_valid = valid && curves_groove_backbone(&backbones[0], phosphates[0]) &&
                        curves_groove_backbone(&backbones[1], phosphates[1]) &&
                        curves_grooves(&grooves, backbones[0], backbones[1], midpoint, axis, depth[0], depth[1]);
                    ASSERT_EQ(integrated[0].groove_quality == Quality_Good, groove_valid);
                    for (size_t i = 0; i < 2 * chains[0].size() - 3; ++i) {
                        const CurvesGrooveSample v = groove_valid ? integrated[0].grooves[i] :
                            CurvesGrooveSample{1.5f + 0.5f * (float)i, NAN, NAN, NAN, NAN};
                        curves_groove_file << f << ',' << i << ',' << v.level << ',' << v.width12 << ',' << v.depth12
                                           << ',' << v.width21 << ',' << v.depth21 << '\n';
                    }
                    ASSERT_TRUE(curves_groove_file.good());
                }
            }
            if (snapshot_dir) {
                const auto path = (std::filesystem::path(snapshot_dir)/("frame_"+std::to_string(f)+".pdb")).string();
                md_file_t file = {};
                ASSERT_TRUE(md_file_open(&file, md_str_from_cstr(path.c_str()), MD_FILE_WRITE | MD_FILE_CREATE | MD_FILE_TRUNCATE));
                ASSERT_TRUE(md_pdb_system_write_state_to_file(file, &sys, &whole, nullptr, whole.num_atoms, 0));
                ASSERT_TRUE(md_file_close(&file));
            }
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
            ASSERT_TRUE(md_script_vis_eval_string(&vis, md_str_from_cstr(expression), &ctx,
                MD_SCRIPT_VISUALIZE_GEOMETRY | MD_SCRIPT_VISUALIZE_TEXT));
            ASSERT_EQ(md_array_size(vis.text), (size_t)1);
            const double degrees = vis.text[0].value * md_unit_scale(vis.text[0].unit) / md_unit_scale(md_unit_degree());
            EXPECT_NEAR(degrees, result.nucleotide_values[n].metric[k], 0.002);
            ++torsion_checks;
        }
        for (const auto& chain : chains) {
            const auto strand = bases[pairs[chain[0]].a].strand;
            auto atom = [&](uint32_t p, bool second, int backbone) {
                const auto pair = pairs[p];
                const auto& a = bases[pair.a];
                const auto& b = bases[pair.b];
                return ((a.strand == strand) != second ? a : b).backbone[backbone];
            };
            auto phosphate = [&](uint32_t p, bool second) { return atom(p, second, BB_P); };
            const vec3_t missing = {NAN, NAN, NAN};
            std::vector<vec3_t> p1(chain.size()), p2(chain.size()), c1(chain.size()), c2(chain.size());
            for (size_t i = 0; i < chain.size(); ++i) {
                auto coord = [&](bool second, int backbone) {
                    const int32_t index = atom(chain[i], second, backbone);
                    return index < 0 ? missing : state.xyz[index];
                };
                p1[i] = coord(false, BB_P); p2[i] = coord(true, BB_P);
                c1[i] = coord(false, BB_C1); c2[i] = coord(true, BB_C1);
            }
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
                        const size_t ia = spans.indices[i][0], ib = spans.indices[i][1];
                        vec3_t d;
                        if (!groove_span(&d, p1.data(), p2.data(), c1.data(), c2.data(), chain.size(), ia, ib, &state.unitcell)) {
                            valid = false; break;
                        }
                        double width = vec3_length(d);
                        if (ia > 0 && ia+1 < chain.size() && ib > 0 && ib+1 < chain.size()) {
                            const int32_t a0 = phosphate(chain[ia-1], false), a1 = phosphate(chain[ia+1], false);
                            const int32_t b0 = phosphate(chain[ib-1], true), b1 = phosphate(chain[ib+1], true);
                            if (a0 >= 0 && a1 >= 0 && b0 >= 0 && b1 >= 0) {
                                vec3_t da0 = vec3_sub(state.xyz[a], state.xyz[a0]);
                                vec3_t da1 = vec3_sub(state.xyz[a1], state.xyz[a]);
                                vec3_t db0 = vec3_sub(state.xyz[b], state.xyz[b0]);
                                vec3_t db1 = vec3_sub(state.xyz[b1], state.xyz[b]);
                                md_util_min_image_vec3(&da0, 1, &state.unitcell);
                                md_util_min_image_vec3(&da1, 1, &state.unitcell);
                                md_util_min_image_vec3(&db0, 1, &state.unitcell);
                                md_util_min_image_vec3(&db1, 1, &state.unitcell);
                                const vec3_t da = vec3_add(da0, da1), db = vec3_add(db0, db1);
                                const float la = vec3_length(da), lb = vec3_length(db);
                                if (la >= 1e-6f && lb >= 1e-6f && width >= 1e-6) {
                                    const vec3_t mean = vec3_add(vec3_div1(da, la), vec3_div1(db, lb));
                                    const float lm = vec3_length(mean);
                                    if (lm >= 1e-6f) {
                                        const double projection = vec3_dot(d, mean) / lm;
                                        width = std::sqrt(std::max(0.0, width*width-projection*projection));
                                    }
                                }
                            }
                        }
                        sum += width;
                        char expression[96];
                        snprintf(expression, sizeof(expression), "distance(%d,%d);", a+1, b+1);
                        ASSERT_TRUE(md_script_vis_clear(&vis));
                        ASSERT_TRUE(md_script_vis_eval_string(&vis, md_str_from_cstr(expression), &ctx,
                            MD_SCRIPT_VISUALIZE_GEOMETRY | MD_SCRIPT_VISUALIZE_TEXT));
                        ASSERT_EQ(md_array_size(vis.text), (size_t)1);
                        const double distance = vis.text[0].value * md_unit_scale(vis.text[0].unit) / md_unit_scale(md_unit_angstrom());
                        vec3_t nearest = vec3_sub(state.xyz[b], state.xyz[a]);
                        md_util_min_image_vec3(&nearest, 1, &state.unitcell);
                        EXPECT_NEAR(distance, vec3_length(nearest), 0.0001);
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
    if (sample_file.is_open()) { sample_file.close(); ASSERT_TRUE(sample_file.good()); }
    if (evidence_file.is_open()) { evidence_file.close(); ASSERT_TRUE(evidence_file.good()); }
    if (const char* reference_path=std::getenv("VIAMD_LAVERY_VALIDATION_REFERENCE")) {
        size_t reference_first=0,reference_end=frames;
        if (const char* range=std::getenv("VIAMD_LAVERY_VALIDATION_REFERENCE_RANGE_PS")) {
            double begin,end;
            char extra;
            ASSERT_EQ(sscanf(range,"%lf,%lf%c",&begin,&end,&extra),2);
            EventOptions range_options;
            const auto* t=(const double*)times->data;
            range_options.times.assign(t,t+frames);
            ASSERT_TRUE(event_time_range(&range_options,begin,end,"validation reference"));
            reference_first=range_options.first_frame;
            reference_end=range_options.last_frame+1;
        }
        std::ofstream reference_file(reference_path);
        ASSERT_TRUE(reference_file.good());
        reference_file << csv_header();
        ReferenceMapping mapping;
        std::array<std::vector<std::string>,Class_Count> identities;
        for (int cls=0; cls<Class_Count; ++cls) for (size_t m=0; m<members[cls]; ++m) {
            auto sample=member_sample(cls,m,0);
            identities[cls].push_back(reference_identity(cls,sample.label,sample.pair_a,sample.pair_b));
            for (size_t f=reference_first; f<reference_end; ++f) for (int k=0; k<class_parameter_count(cls); ++k) {
                const int id=class_first_metric(cls)+k;
                sample.frame=f; sample.time=((const double*)times->data)[f];
                sample.metric=(Metric)id; sample.value=values[id][f*members[cls]+m];
                sample.quality=sample_quality[id][f*members[cls]+m];
                reference_file << csv_sample(sample);
            }
        }
        reference_file.close();
        ASSERT_TRUE(reference_file.good());
        ExternalReference imported;
        std::string error;
        ASSERT_TRUE(load_external_reference(&imported,reference_path,&error));
        for (int cls=0; cls<Class_Count; ++cls) {
            mapping[cls]=match_reference_members(imported.members[cls],identities[cls]);
            for (int r : mapping[cls]) EXPECT_GE(r,0);
        }
        std::array<MetricStatistics,Metric_Count> ranking_stats;
        std::array<size_t,Metric_Count> ranking_columns={};
        for (int id=0; id<(int)Metric_Count; ++id) {
            const int cls=metric_class((Metric)id);
            ranking_columns[id]=members[cls];
            for (Baseline baseline : {Baseline_MeanSD,Baseline_MedianMAD}) {
                MetricStatistics expected,actual;
                ASSERT_TRUE(compute_statistics(&expected,values[id],members[cls],metric_is_angular((Metric)id),baseline,
                    metric_state_count((Metric)id) ? id : -1,reference_first,reference_end));
                ASSERT_TRUE(external_statistics(&actual,imported,mapping[cls],values[id],(Metric)id,baseline));
                for (size_t m=0; m<members[cls]; ++m) {
                    EXPECT_EQ(actual.members[m].count,expected.members[m].count);
                    if (expected.members[m].count) {
                        EXPECT_NEAR(actual.members[m].mean,expected.members[m].mean,.000001);
                        EXPECT_NEAR(actual.members[m].sd,expected.members[m].sd,.000001);
                    }
                }
                for (size_t i=0; i<actual.heatmap.size(); ++i) {
                    if (std::isnan(expected.heatmap[i])) EXPECT_TRUE(std::isnan(actual.heatmap[i]));
                    else EXPECT_NEAR(actual.heatmap[i],expected.heatmap[i],.00001);
                }
                if (baseline==Baseline_MeanSD) ranking_stats[id]=std::move(actual);
            }
        }
        if (const char* output=std::getenv("VIAMD_LAVERY_VALIDATION_REPRESENTATIVES")) {
            std::vector<FrameCandidate> candidates;
            const double* time_data=(const double*)times->data;
            const std::vector<double> physical_times(time_data,time_data+frames);
            const size_t central=pairs.size()/2;
            std::vector<FrameFeature> features={{Opening,central},{Rise,steps.size()/2},{Twist,steps.size()/2},{Chi,2*central},{Chi,2*central+1}};
            if (const char* selected=std::getenv("VIAMD_LAVERY_VALIDATION_FEATURES")) {
                features.clear();
                std::istringstream input(selected);
                std::string feature;
                while (std::getline(input,feature,',')) {
                    const size_t split=feature.find(':');
                    ASSERT_NE(split,std::string::npos);
                    size_t member=0;
                    char extra;
                    ASSERT_EQ(sscanf(feature.c_str()+split+1,"%zu%c",&member,&extra),1);
                    ASSERT_GT(member,(size_t)0);
                    int id=-1;
                    for (int k=0; k<(int)Metric_Count; ++k) if (feature.substr(0,split)==metric_path((Metric)k)) id=k;
                    ASSERT_GE(id,0);
                    ASSERT_LT(member-1,ranking_columns[id]);
                    features.push_back({(Metric)id,member-1});
                }
                ASSERT_FALSE(features.empty());
            }
            size_t search_first=0,search_end=frames;
            if (const char* range=std::getenv("VIAMD_LAVERY_VALIDATION_SEARCH_RANGE_PS")) {
                double begin,end;
                char extra;
                ASSERT_EQ(sscanf(range,"%lf,%lf%c",&begin,&end,&extra),2);
                EventOptions options;
                options.times=physical_times;
                ASSERT_TRUE(event_time_range(&options,begin,end,"validation representative search"));
                search_first=options.first_frame;
                search_end=options.last_frame+1;
            }
            ASSERT_TRUE(representative_frames(&candidates,values,ranking_stats,ranking_columns,features,
                search_first,search_end,5,100,physical_times,&error));
            ASSERT_GT(candidates.size(),(size_t)0);
            std::string provenance=imported.path+"; fingerprint="+imported.fingerprint+
                "; baseline_frames="+std::to_string(reference_first)+".."+std::to_string(reference_end-1)+
                "; MeanSD own history; RMS(z); search_frames="+std::to_string(search_first)+".."+std::to_string(search_end-1)+
                "; separation=100 ps; features=";
            for (const auto& f : features) provenance+=std::string(metric_path(f.metric))+":"+std::to_string(f.member+1)+" ";
            ASSERT_TRUE(export_representative_frames(sys,"run/check",output,candidates,provenance,&error));
            md_system_t restored={.alloc=alloc};
            md_system_state_t restored_state={.alloc=alloc};
            auto gro_path=std::filesystem::path(output);
            gro_path.replace_extension(".gro");
            ASSERT_TRUE(md_gro_system_init_from_file(&restored,&restored_state,md_str_from_cstr(gro_path.string().c_str()), nullptr));
            ASSERT_EQ(restored.atom.count,sys.atom.count);
            ASSERT_TRUE(md_xtc_system_publish_run(&restored,md_str_from_cstr(output),MD_STR_LIT("run/restored"),MD_RUN_FLAG_DISABLE_CACHE_WRITE));
            const auto* restored_times=md_attributes_find(&restored.attributes,MD_STR_LIT("run/restored/time"));
            ASSERT_TRUE(restored_times && restored_times->data);
            ASSERT_EQ(restored_times->format.shape[0],candidates.size());
            const md_str_t export_paths[]={MD_STR_LIT("atom/position"),MD_STR_LIT("unitcell")};
            auto* restored_extract=md_system_extract_begin(&restored,MD_STR_LIT("run/restored"),export_paths,2,alloc);
            ASSERT_TRUE(restored_extract!=nullptr);
            std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b) { return a.frame<b.frame; });
            for (size_t n=0; n<candidates.size(); ++n) {
                const size_t original=candidates[n].frame;
                EXPECT_EQ(((const double*)restored_times->data)[n],(double)(float)physical_times[original]);
                ASSERT_TRUE(md_system_extract_frame(extract,original,&state));
                ASSERT_TRUE(md_system_extract_frame(restored_extract,n,&restored_state));
                for (size_t a=0; a<sys.atom.count; ++a) {
                    EXPECT_NEAR(restored_state.xyz[a].x,state.xyz[a].x,.011);
                    EXPECT_NEAR(restored_state.xyz[a].y,state.xyz[a].y,.011);
                    EXPECT_NEAR(restored_state.xyz[a].z,state.xyz[a].z,.011);
                }
            }
            md_system_extract_end(restored_extract);
            printf("External reference: %zu records; %zu ranked/exported real frames\n",imported.records,candidates.size());
        }
    }
    if (curves_dir) for (int id = 0; id < (int)Metric_Count; ++id) {
        if (id == MinorGroove || id == MajorGroove || (HELICAL_PARAMETERS & (1u << id))) continue;
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
        auto restricted_options = state_options;
        restricted_options.first_frame = frames/4;
        restricted_options.last_frame = frames-1-frames/4;
        std::vector<StateSwitch> restricted_changes;
        ASSERT_TRUE(detect_state_switches(&restricted_changes, values[id], 2*pairs.size(), (Metric)id, restricted_options));
        for (const auto& change : restricted_changes) {
            EXPECT_GE(change.source_begin, restricted_options.first_frame);
            EXPECT_LE(change.end, restricted_options.last_frame);
            EXPECT_GE(time_data[change.end]-time_data[change.begin], restricted_options.min_duration);
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
    if (exported_pairing.is_open()) {
        exported_pairing.close();
        ASSERT_TRUE(exported_pairing.good());
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
        for (double scale : {0.5, 1.0, 2.0, 5.0}) {
            const double residence = scale*state_options.min_duration;
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
                if (scale == 1.0) {
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
    for (size_t p = 0; p < pairs.size(); ++p) {
        size_t valid = 0;
        std::array<size_t, 16> reasons = {};
        for (size_t f = 0; f < frames; ++f) {
            const uint16_t q = pair_quality[f*pairs.size()+p];
            valid += q == Quality_Good;
            for (size_t bit = 0; bit < reasons.size(); ++bit) reasons[bit] += (q & (1u << bit)) != 0;
        }
        printf("Pair %zu (components %u/%u): %zu/%zu good", p+1,
            bases[pairs[p].a].component+1, bases[pairs[p].b].component+1, valid, frames);
        for (size_t bit = 0; bit < reasons.size(); ++bit) {
            if (reasons[bit]) printf("; %s=%zu", quality_description((uint16_t)(1u << bit)), reasons[bit]);
        }
        printf("\n");
    }
    for (int baseline = 0; baseline < Baseline_Count; ++baseline) {
        if (!steps.empty()) {
            MetricStatistics pooled_rise;
            const auto groups = helix_member_groups(pairs.size(), steps, Class_Step);
            ASSERT_TRUE(compute_statistics(&pooled_rise, values[Rise], steps.size(), false, (Baseline)baseline,
                -1, 0, SIZE_MAX, groups));
            size_t strongest = 0;
            double peak_mean_z = -1;
            size_t above = 0, own_above = 0, available = 0;
            std::vector<float> own_values(frames);
            for (size_t m = 0; m < steps.size(); ++m) {
                double sum = 0;
                size_t finite = 0;
                for (size_t f = 0; f < frames; ++f) {
                    const float v = values[Rise][f * steps.size() + m];
                    if (!std::isfinite(v)) continue;
                    sum += pooled_rise.members[m].zscore(v);
                    ++finite;
                }
                if (finite && std::fabs(sum / finite) > peak_mean_z) {
                    strongest = m;
                    peak_mean_z = std::fabs(sum / finite);
                }
            }
            for (size_t f = 0; f < frames; ++f) {
                const float v = values[Rise][f * steps.size() + strongest];
                own_values[f] = v;
                available += std::isfinite(v);
                above += pooled_rise.members[strongest].is_outlier(v, 2.5f);
            }
            MetricStatistics own_rise;
            ASSERT_TRUE(compute_statistics(&own_rise, own_values, 1, false, (Baseline)baseline));
            for (float v : own_values) own_above += own_rise.members[0].is_outlier(v, 2.5f);
            printf("Pooled Rise %s: strongest step %zu (pairs %u-%u), |mean z| %.3f; %zu/%zu finite samples above 2.5 sigma (own history %zu/%zu)\n",
                baseline_name((Baseline)baseline), strongest+1, steps[strongest].a+1, steps[strongest].b+1,
                peak_mean_z, above, available, own_above, available);
        }
        const auto stats_begin = std::chrono::steady_clock::now();
        std::array<MetricStatistics, Metric_Count> stats;
        std::array<DistributionComparison, Metric_Count> distributions;
        std::array<std::vector<float>, Metric_Count> zscores;
        for (int id = 0; id < (int)Metric_Count; ++id) {
            const auto metric = (Metric)id;
            const size_t count = members[metric_class(metric)];
            ASSERT_TRUE(compute_statistics(&stats[id], values[id], count, metric_is_angular(metric), (Baseline)baseline,
                metric_state_count(metric) ? id : -1));
            const size_t reference_end = std::max<size_t>(1, frames/4);
            MetricStatistics restricted;
            ASSERT_TRUE(compute_statistics(&restricted, values[id], count, metric_is_angular(metric), (Baseline)baseline,
                metric_state_count(metric) ? id : -1, 0, reference_end));
            EXPECT_EQ(restricted.heatmap.size(), values[id].size());
            for (size_t m = 0; m < count; ++m) {
                size_t finite = 0;
                for (size_t f = 0; f < reference_end; ++f) finite += std::isfinite(values[id][f*count+m]);
                EXPECT_EQ(restricted.members[m].count, finite);
            }
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
                for (double duration : {state_options.min_duration, 5*state_options.min_duration}) for (bool background : {false, true}) {
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
                    options.first_frame = frames/4;
                    options.last_frame = frames-1-frames/4;
                    const auto restricted_events = detect_events(samples, pairs.size(), steps, options);
                    for (const auto& event : restricted_events) {
                        EXPECT_GE(event.begin, options.first_frame);
                        EXPECT_LE(event.end, options.last_frame);
                        for (const auto& member : event.members) for (const auto& sample : member.samples) {
                            EXPECT_GE(sample.frame, options.first_frame);
                            EXPECT_LE(sample.frame, options.last_frame);
                        }
                    }
                }
            }
        }
    }
    md_script_vis_free(&vis);
    md_system_extract_end(extract);
    md_arena_allocator_destroy(alloc);
}

UTEST(lavery_core, native_dihedral_visual_values_agree_with_nucleotide_torsions) {
    auto* alloc = md_arena_allocator_create(md_get_heap_allocator(), MD_MEGABYTES(16));
    md_system_t sys = {.alloc = alloc};
    md_system_state_t state = {.alloc = alloc};
    ASSERT_TRUE(md_gro_system_init_from_file(&sys, &state, MD_STR_LIT(VIAMD_TEST_DATA_DIR "/nucl-dna.gro"), nullptr));
    ASSERT_TRUE(md_util_system_infer(&sys, &state, MD_UTIL_INFER_ALL, nullptr));
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
        ASSERT_TRUE(md_script_vis_eval_string(&vis, md_str_from_cstr(expression), &ctx,
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
    auto* alloc = md_arena_allocator_create(md_get_heap_allocator(), MD_MEGABYTES(16));
    md_system_t sys = {.alloc = alloc};
    md_system_state_t state = {.alloc = alloc};
    ASSERT_TRUE(md_gro_system_init_from_file(&sys, &state, MD_STR_LIT(VIAMD_TEST_DATA_DIR "/nucl-dna.gro"), nullptr));
    ASSERT_TRUE(md_util_system_infer(&sys, &state, MD_UTIL_INFER_ALL, nullptr));
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
    EXPECT_EQ(result.pairing_states[p], Pairing_WatsonCrick);
    const auto original = result.pair_values[p];
    const auto range = md_system_residue_atom_range(&sys, bases[pairs[p].b].component);
    std::vector<vec3_t> saved(state.xyz + range.beg, state.xyz + range.end);
    for (size_t i = range.beg; i < range.end; ++i) state.xyz[i] = vec3_add(state.xyz[i], {12, 0, 0});
    ASSERT_TRUE(analyze_frame(&result, sys, state, bases, pairs, steps, validator));
    EXPECT_TRUE(result.pair_quality[p] & Quality_NoPairContacts);
    EXPECT_EQ(result.pairing_states[p], Pairing_Unpaired);
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
    EXPECT_EQ(result.pairing_states[p], Pairing_WatsonCrick);
    const auto& contact = validator.chemistry()[p].contacts[0];
    ASSERT_GT(contact.hydrogens.size(), (size_t)0);
    const uint32_t h = contact.hydrogens[0];
    const vec3_t saved_h = state.xyz[h];
    state.xyz[h].x = NAN;
    ASSERT_TRUE(analyze_frame(&result, sys, state, bases, pairs, steps, validator));
    EXPECT_EQ(result.pairing_states[p], Pairing_Unavailable);
    state.xyz[h] = saved_h;
    md_arena_allocator_destroy(alloc);
}

UTEST(lavery_core, production_frame_analysis_applies_curves_inversion_to_pair_and_step_values) {
    auto* alloc = md_arena_allocator_create(md_get_heap_allocator(), MD_MEGABYTES(16));
    md_system_t sys = {.alloc = alloc};
    md_system_state_t state = {.alloc = alloc};
    ASSERT_TRUE(md_gro_system_init_from_file(&sys, &state, MD_STR_LIT(VIAMD_TEST_DATA_DIR "/nucl-dna.gro"), nullptr));
    ASSERT_TRUE(md_util_system_infer(&sys, &state, MD_UTIL_INFER_ALL, nullptr));
    const auto bases = gather_bases(sys);
    std::vector<BaseFrame> frames;
    ASSERT_TRUE(extract_base_frames(frames, sys, state, bases));
    std::vector<Pair> pairs(bases.size());
    pairs.resize(detect_pairs(pairs.data(), pairs.size(), frames.data(), frames.size(), &state.unitcell));
    const auto steps = gather_steps(sys, bases, pairs.data(), pairs.size());
    PairingValidator validator;
    ASSERT_TRUE(validator.initialize(sys, bases, pairs));
    FrameAnalysis original;
    ASSERT_TRUE(analyze_frame(&original, sys, state, bases, pairs, steps, validator));
    const size_t s = 50;
    ASSERT_LT(s, steps.size());
    const auto step = steps[s];
    ASSERT_EQ(original.pair_quality[step.a], (uint16_t)Quality_Good);
    ASSERT_EQ(original.pair_quality[step.b], (uint16_t)Quality_Good);
    vec3_t delta = vec3_sub(original.pair_frames[step.b].origin, original.pair_frames[step.a].origin);
    md_util_min_image_vec3(&delta, 1, &state.unitcell);
    const vec3_t normal = original.pair_frames[step.b].z;
    const float projection = vec3_dot(delta, normal);
    ASSERT_GT(projection, 1.0f);
    const vec3_t shift = vec3_mul1(normal, -2.0f * projection);
    for (uint32_t base_index : {pairs[step.b].a, pairs[step.b].b}) {
        const auto range = md_system_residue_atom_range(&sys, bases[base_index].component);
        for (uint32_t atom = range.beg; atom < range.end; ++atom) state.xyz[atom] = vec3_add(state.xyz[atom], shift);
    }
    FrameAnalysis result;
    ASSERT_TRUE(analyze_frame(&result, sys, state, bases, pairs, steps, validator));
    ASSERT_EQ(result.pair_quality[step.a], (uint16_t)Quality_Good);
    ASSERT_EQ(result.step_quality[s], (uint16_t)Quality_Good);
    ASSERT_EQ(result.pair_inversions[step.a], 1);
    ASSERT_TRUE(extract_base_frames(frames, sys, state, bases));
    const auto pair = pairs[step.a];
    BasePairFrame raw_pair;
    Values raw_pair_values;
    uint16_t quality;
    ASSERT_TRUE(compute_present_base_pair(&raw_pair_values, &raw_pair, &quality,
        frames[pair.a], frames[pair.b], &state.unitcell, pair.reversed));
    EXPECT_NEAR(result.pair_values[step.a].metric[Shear], -raw_pair_values.metric[Shear], 1e-5);
    EXPECT_NEAR(result.pair_values[step.a].metric[Buckle], -raw_pair_values.metric[Buckle], 1e-5);
    auto next = result.pair_frames[step.b];
    delta = vec3_sub(next.origin, raw_pair.origin);
    md_util_min_image_vec3(&delta, 1, &state.unitcell);
    next.origin = vec3_add(raw_pair.origin, delta);
    Values raw_step_values;
    ASSERT_TRUE(compute_step(&raw_step_values, raw_pair, next));
    EXPECT_NEAR(result.step_values[s].metric[Rise], -raw_step_values.metric[Rise], 1e-5);
    EXPECT_NEAR(result.step_values[s].metric[Twist], -raw_step_values.metric[Twist], 1e-5);
    EXPECT_NEAR(result.step_values[s].metric[Roll], raw_step_values.metric[Roll], 1e-5);
    md_arena_allocator_destroy(alloc);
}

UTEST(lavery_core, hydrogen_free_rna_1rna_matches_independent_curves_reference_for_all_12_parameters) {
    auto* alloc = md_arena_allocator_create(md_get_heap_allocator(), MD_MEGABYTES(4));
    md_system_t sys = {.alloc = alloc};
    md_system_state_t state = {.alloc = alloc};
    const auto path = (std::filesystem::path(__FILE__).parent_path() / "data/lavery_1rna.pdb").string();
    ASSERT_TRUE(md_pdb_system_init_from_file(&sys, &state, md_str_from_cstr(path.c_str()), {}, nullptr));
    ASSERT_TRUE(md_util_system_infer(&sys, &state, MD_UTIL_INFER_ALL, nullptr));
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
    for (auto pairing : result.pairing_states) EXPECT_EQ(pairing, Pairing_Unavailable);
    // Curves+ v3.0nc, test=.t.: smoothed axis directions and points, printed to 0.01.
    const float rna_curves_axis[14][6] = {
        {0.33f,0.51f,-0.79f,6.81f,7.03f,21.15f},
        {0.33f,0.52f,-0.79f,7.58f,8.64f,18.96f},
        {0.32f,0.52f,-0.79f,8.48f,10.55f,16.57f},
        {0.32f,0.52f,-0.79f,9.15f,12.09f,14.78f},
        {0.31f,0.51f,-0.80f,9.95f,13.92f,12.52f},
        {0.30f,0.51f,-0.81f,10.60f,15.68f,10.36f},
        {0.29f,0.48f,-0.83f,11.19f,17.07f,8.34f},
        {0.29f,0.45f,-0.85f,11.80f,18.19f,6.35f},
        {0.30f,0.41f,-0.86f,12.50f,19.25f,4.18f},
        {0.31f,0.38f,-0.87f,13.31f,20.17f,1.90f},
        {0.32f,0.34f,-0.88f,14.21f,20.91f,-0.50f},
        {0.33f,0.32f,-0.89f,15.10f,21.55f,-2.78f},
        {0.34f,0.29f,-0.89f,16.34f,22.24f,-5.56f},
        {0.35f,0.26f,-0.90f,17.48f,22.61f,-7.80f},
    };
    std::vector<BaseFrame> axis_strand1, axis_strand2;
    for (const auto& pair : pairs) {
        axis_strand1.push_back(frames[pair.a]);
        axis_strand2.push_back(frames[pair.b]);
    }
    std::vector<CurvesAxisPoint> curves_axis;
    ASSERT_TRUE(curves_duplex_axis(&curves_axis, axis_strand1, axis_strand2, result.pair_frames));
    ASSERT_EQ(curves_axis.size(), pairs.size());
    for (size_t p = 0; p < pairs.size(); ++p)
        for (size_t k = 0; k < 3; ++k) {
            EXPECT_NEAR(curves_axis[p].direction.elem[k], rna_curves_axis[p][k], 0.0052f);
            EXPECT_NEAR(curves_axis[p].origin.elem[k], rna_curves_axis[p][k+3], 0.0052f);
        }
    const float rna_pair_axis[14][5] = {
        {-2.20f,-0.48f,15.4f,5.8f,NAN}, {-2.80f,-0.30f,12.8f,1.5f,0.7f},
        {-3.48f,-0.53f,12.8f,7.1f,0.3f}, {-3.27f,-0.68f,19.2f,0.2f,0.5f},
        {-3.83f,-0.66f,15.2f,4.7f,0.7f}, {-4.01f,0.24f,19.0f,0.6f,0.9f},
        {-3.89f,0.06f,21.3f,1.4f,1.8f}, {-4.06f,-0.17f,25.0f,0.7f,2.3f},
        {-3.94f,-0.44f,22.2f,1.9f,2.1f}, {-4.23f,0.18f,19.9f,-3.2f,2.5f},
        {-3.48f,-0.44f,20.6f,1.9f,2.1f}, {-4.23f,-0.13f,16.4f,-6.0f,1.6f},
        {-3.86f,0.34f,13.5f,1.1f,1.7f}, {-3.15f,0.84f,7.7f,0.1f,1.8f},
    };
    std::vector<CurvesPairAxisParameters> axis_parameters;
    ASSERT_TRUE(curves_pair_axis_parameters(&axis_parameters, result.pair_frames, curves_axis, result.pair_inversions));
    ASSERT_EQ(axis_parameters.size(), pairs.size());
    for (size_t p = 0; p < pairs.size(); ++p) {
        const auto& v = axis_parameters[p];
        EXPECT_NEAR(v.x_displacement, rna_pair_axis[p][0], 0.0052f);
        EXPECT_NEAR(v.y_displacement, rna_pair_axis[p][1], 0.0052f);
        EXPECT_NEAR(v.inclination, rna_pair_axis[p][2], 0.051f);
        EXPECT_NEAR(v.tip, rna_pair_axis[p][3], 0.051f);
        if (p) EXPECT_NEAR(v.axis_bend, rna_pair_axis[p][4], 0.051f);
        else EXPECT_TRUE(std::isnan(v.axis_bend));
    }
    const float rna_inter_axis[13][2] = {
        {2.81f,35.5f}, {3.17f,33.0f}, {2.44f,28.8f}, {3.00f,32.6f},
        {2.83f,37.5f}, {2.51f,30.7f}, {2.37f,31.9f}, {2.51f,33.2f},
        {2.59f,37.9f}, {2.67f,24.0f}, {2.52f,35.1f}, {3.11f,36.7f},
        {2.51f,36.4f},
    };
    std::vector<CurvesStepAxisParameters> step_axis;
    ASSERT_TRUE(curves_step_axis_parameters(&step_axis, result.pair_frames, curves_axis, result.pair_inversions));
    ASSERT_EQ(step_axis.size(), steps.size());
    for (size_t s = 0; s < steps.size(); ++s) {
        EXPECT_NEAR(step_axis[s].rise, rna_inter_axis[s][0], 0.0052f);
        EXPECT_NEAR(step_axis[s].twist, rna_inter_axis[s][1], 0.051f);
    }
    // Curves+ v3.0nc ref_B.pdb sampled backbone coordinates, printed to 0.001 A.
    const size_t backbone_indices[] = {0, 5, 10, 25, 50, 75, 100, 115, 120};
    const vec3_t rna_backbone[2][9] = {
        {{14.592f,13.271f,22.144f}, {16.209f,12.052f,19.947f}, {17.215f,10.889f,17.385f},
         {15.882f,8.439f,9.171f}, {6.866f,14.003f,0.471f}, {7.631f,27.234f,0.342f},
         {20.701f,29.211f,-1.910f}, {26.885f,23.933f,-4.895f}, {27.769f,21.738f,-6.433f}},
        {{6.812f,-1.758f,18.092f}, {4.904f,-0.653f,16.333f}, {3.084f,1.008f,14.959f},
         {-0.705f,8.561f,13.649f}, {4.769f,21.283f,16.020f}, {17.871f,20.966f,12.798f},
         {18.808f,12.718f,2.176f}, {13.933f,12.786f,-5.225f}, {12.020f,14.052f,-7.052f}},
    };
    std::array<std::vector<vec3_t>, 2> groove_backbones;
    for (int side = 0; side < 2; ++side) {
        std::vector<vec3_t> phosphates;
        for (size_t i = 0; i + 1 < pairs.size(); ++i) {
            const auto pair = pairs[side == 0 ? i + 1 : i];
            const int32_t atom = bases[side == 0 ? pair.a : pair.b].backbone[BB_P];
            ASSERT_GE(atom, 0);
            phosphates.push_back(state.xyz[atom]);
        }
        auto& backbone = groove_backbones[side];
        ASSERT_TRUE(curves_groove_backbone(&backbone, phosphates));
        ASSERT_EQ(backbone.size(), (size_t)121);
        for (size_t i = 0; i < std::size(backbone_indices); ++i)
            for (int k = 0; k < 3; ++k)
                EXPECT_NEAR(backbone[backbone_indices[i]].elem[k], rna_backbone[side][i].elem[k], 0.0007f);
    }
    const float rna_grooves[25][4] = {
        {NAN,NAN,NAN,NAN}, {NAN,NAN,NAN,NAN}, {NAN,NAN,NAN,NAN},
        {10.2f,1.0f,NAN,NAN}, {9.7f,1.1f,NAN,NAN}, {9.6f,1.2f,NAN,NAN},
        {9.7f,1.2f,NAN,NAN}, {9.8f,1.1f,6.1f,8.7f}, {9.9f,1.0f,5.6f,8.8f},
        {10.3f,0.6f,4.8f,9.1f}, {10.5f,0.4f,4.0f,9.1f}, {10.5f,0.0f,3.1f,9.2f},
        {10.3f,0.2f,2.5f,9.3f}, {10.4f,0.0f,2.1f,9.5f}, {10.4f,0.3f,2.1f,9.5f},
        {10.5f,0.2f,2.4f,9.8f}, {10.6f,0.7f,2.8f,9.6f}, {10.8f,0.6f,NAN,NAN},
        {10.5f,0.7f,NAN,NAN}, {9.9f,0.8f,NAN,NAN}, {9.5f,1.2f,NAN,NAN},
        {9.6f,1.3f,NAN,NAN}, {NAN,NAN,NAN,NAN}, {NAN,NAN,NAN,NAN}, {NAN,NAN,NAN,NAN},
    };
    std::vector<vec3_t> depth1, depth2;
    for (const auto& pair : pairs) {
        depth1.push_back(frames[pair.a].groove_reference);
        depth2.push_back(frames[pair.b].groove_reference);
    }
    std::vector<CurvesGrooveSample> grooves;
    ASSERT_TRUE(curves_grooves(&grooves, groove_backbones[0], groove_backbones[1],
                               result.pair_frames, curves_axis, depth1, depth2));
    ASSERT_EQ(grooves.size(), std::size(rna_grooves));
    for (size_t i = 0; i < grooves.size(); ++i) {
        EXPECT_NEAR(grooves[i].level, 1.5f + 0.5f * i, 1e-5f);
        const float got[] = {grooves[i].width12, grooves[i].depth12, grooves[i].width21, grooves[i].depth21};
        for (size_t k = 0; k < 4; ++k) {
            EXPECT_EQ(std::isfinite(got[k]), std::isfinite(rna_grooves[i][k]));
            if (std::isfinite(rna_grooves[i][k])) EXPECT_NEAR(got[k], rna_grooves[i][k], 0.051f);
        }
    }
    std::vector<CurvesHelixGeometry> integrated_helices;
    ASSERT_TRUE(analyze_curves_helices(&integrated_helices, sys, state, bases, pairs, steps));
    ASSERT_EQ(integrated_helices.size(), (size_t)1);
    ASSERT_EQ(integrated_helices[0].axis_quality, (uint16_t)Quality_Good);
    ASSERT_EQ(integrated_helices[0].groove_quality, (uint16_t)Quality_Good);
    ASSERT_EQ(integrated_helices[0].axis.size(), curves_axis.size());
    ASSERT_EQ(integrated_helices[0].grooves.size(), grooves.size());
    for (size_t i = 0; i < curves_axis.size(); ++i) {
        EXPECT_NEAR(vec3_length(vec3_sub(integrated_helices[0].axis[i].origin, curves_axis[i].origin)), 0.0f, 1e-4f);
        EXPECT_NEAR(vec3_length(vec3_sub(integrated_helices[0].axis[i].direction, curves_axis[i].direction)), 0.0f, 1e-4f);
    }
    for (size_t i = 0; i < grooves.size(); ++i) {
        const auto& g = integrated_helices[0].grooves[i];
        const float got[] = {g.width12, g.depth12, g.width21, g.depth21};
        const float expected[] = {grooves[i].width12, grooves[i].depth12, grooves[i].width21, grooves[i].depth21};
        for (size_t k = 0; k < 4; ++k) {
            EXPECT_EQ(std::isfinite(got[k]), std::isfinite(expected[k]));
            if (std::isfinite(expected[k])) EXPECT_NEAR(got[k], expected[k], 1e-4f);
        }
    }
    std::vector<CurvesHelixGeometry> missing_groove;
    // Curves+ rejects grooves when an internal or required endpoint phosphate is absent.
    for (uint32_t base : {pairs[1].a, pairs[2].a, pairs[9].a, pairs[13].a,
                          pairs[0].b, pairs[8].b, pairs[12].b}) {
        auto missing_phosphate_bases = bases;
        missing_phosphate_bases[base].backbone[BB_P] = -1;
        ASSERT_TRUE(analyze_curves_helices(&missing_groove, sys, state, missing_phosphate_bases, pairs, steps));
        ASSERT_EQ(missing_groove.size(), (size_t)1);
        EXPECT_EQ(missing_groove[0].axis_quality, (uint16_t)Quality_Good);
        EXPECT_TRUE(missing_groove[0].groove_quality & Quality_MissingAtoms);
        EXPECT_TRUE(missing_groove[0].grooves.empty());
    }
    auto two_bases = bases;
    for (auto base : bases) {
        base.strand += 100;
        two_bases.push_back(std::move(base));
    }
    auto two_pairs = pairs;
    for (auto pair : pairs) {
        pair.a += (uint32_t)bases.size();
        pair.b += (uint32_t)bases.size();
        two_pairs.push_back(pair);
    }
    auto two_steps = steps;
    for (auto step : steps) {
        step.a += (uint32_t)pairs.size();
        step.b += (uint32_t)pairs.size();
        two_steps.push_back(step);
    }
    two_bases[bases.size() + pairs[2].a].backbone[BB_P] = -1;
    ASSERT_TRUE(analyze_curves_helices(&missing_groove, sys, state, two_bases, two_pairs, two_steps));
    ASSERT_EQ(missing_groove.size(), (size_t)2);
    EXPECT_EQ(missing_groove[0].axis_quality, (uint16_t)Quality_Good);
    EXPECT_EQ(missing_groove[0].groove_quality, (uint16_t)Quality_Good);
    EXPECT_EQ(missing_groove[0].grooves.size(), grooves.size());
    EXPECT_EQ(missing_groove[1].axis_quality, (uint16_t)Quality_Good);
    EXPECT_TRUE(missing_groove[1].groove_quality & Quality_MissingAtoms);
    EXPECT_TRUE(missing_groove[1].grooves.empty());
    auto bad_fit_bases = bases;
    bad_fit_bases[pairs[2].a].fit_atoms[0] = bad_fit_bases[pairs[2].a].fit_atoms[1];
    ASSERT_TRUE(analyze_curves_helices(&missing_groove, sys, state, bad_fit_bases, pairs, steps));
    EXPECT_NE(missing_groove[0].axis_quality, (uint16_t)Quality_Good);
    EXPECT_TRUE(missing_groove[0].axis.empty());
    EXPECT_TRUE(missing_groove[0].grooves.empty());
    auto invalid_curves_steps = steps;
    invalid_curves_steps[0].a = (uint32_t)pairs.size();
    EXPECT_FALSE(analyze_curves_helices(&integrated_helices, sys, state, bases, pairs, invalid_curves_steps));
    EXPECT_EQ(integrated_helices[0].axis.size(), curves_axis.size());
    CurvesFrameAnalysis curves_frame;
    ASSERT_TRUE(analyze_curves_frame(&curves_frame, sys, state, bases, pairs, steps, validator));
    ASSERT_EQ(curves_frame.helices.size(), (size_t)1);
    EXPECT_TRUE(curves_frame.frame.helical_pair_frames.empty());
    EXPECT_TRUE(curves_frame.frame.helical_quality.empty());
    ASSERT_EQ(curves_frame.frame.pair_values.size(), pairs.size());
    ASSERT_EQ(curves_frame.frame.nucleotide_values.size(), 2*pairs.size());
    for (size_t p = 0; p < pairs.size(); ++p) {
        for (int id = Buckle; id <= (int)Stagger; ++id)
            EXPECT_NEAR(curves_frame.frame.pair_values[p].metric[id], result.pair_values[p].metric[id], 1e-5f);
        for (int side = 0; side < 2; ++side) {
            const auto& base = bases[side ? pairs[p].b : pairs[p].a];
            const auto expected = curves_fourier_pucker(base, state);
            const auto& actual = curves_frame.frame.nucleotide_values[2*p + side];
            EXPECT_NEAR(std::remainder(actual.metric[Phase] - expected[0], 360.0f), 0.0f, 1e-4f);
            EXPECT_NEAR(actual.metric[Amplitude], expected[1], 1e-4f);
        }
    }
    for (const auto& step : curves_frame.frame.step_values) {
        EXPECT_TRUE(std::isnan(step.metric[MinorGroove]));
        EXPECT_TRUE(std::isnan(step.metric[MajorGroove]));
        for (int id = XDisplacement; id <= (int)HelicalTwist; ++id) EXPECT_TRUE(std::isnan(step.metric[id]));
    }
    EXPECT_FALSE(analyze_curves_frame(&curves_frame, sys, state, bases, pairs, invalid_curves_steps, validator));
    EXPECT_EQ(curves_frame.frame.pair_values.size(), pairs.size());
    CurvesTrajectoryAnalysis curves_trajectory;
    ASSERT_TRUE(initialize_curves_trajectory(&curves_trajectory, 2, pairs.size(), steps, curves_frame));
    ASSERT_EQ(curves_trajectory.pair_axis.size(), pairs.size() * 2);
    ASSERT_EQ(curves_trajectory.step_axis.size(), steps.size() * 2);
    ASSERT_EQ(curves_trajectory.grooves.size(), curves_frame.helices[0].grooves.size() * 2);
    ASSERT_TRUE(store_curves_trajectory_frame(&curves_trajectory, 0, curves_frame));
    for (size_t p = 0; p < pairs.size(); ++p) {
        EXPECT_NEAR(curves_trajectory.pair_axis[p].tip, curves_frame.helices[0].pair_parameters[p].tip, 1e-5f);
    }
    for (size_t s = 0; s < steps.size(); ++s) {
        EXPECT_NEAR(curves_trajectory.step_axis[s].twist, curves_frame.helices[0].step_parameters[s].twist, 1e-5f);
    }
    for (size_t g = 0; g < curves_frame.helices[0].grooves.size(); ++g) {
        EXPECT_NEAR(curves_trajectory.grooves[g].level, curves_frame.helices[0].grooves[g].level, 1e-5f);
        if (std::isfinite(curves_frame.helices[0].grooves[g].width12))
            EXPECT_NEAR(curves_trajectory.grooves[g].width12, curves_frame.helices[0].grooves[g].width12, 1e-5f);
        else
            EXPECT_TRUE(std::isnan(curves_trajectory.grooves[g].width12));
    }
    const std::string curves_csv = csv_curves_trajectory_frame("curves,run", curves_trajectory, 0, true);
    EXPECT_TRUE(curves_csv.find(csv_curves_trajectory_header()) == 0);
    EXPECT_TRUE(curves_csv.find("\"curves,run\",0,1,pair_axis,\"bp_1\"") != std::string::npos);
    EXPECT_TRUE(curves_csv.find(",groove,\"sample_1\",1.5,") != std::string::npos);
    EXPECT_TRUE((size_t)std::count(curves_csv.begin(), curves_csv.end(), '\n') ==
                1 + pairs.size() + steps.size() + curves_frame.helices[0].grooves.size());
    CurvesFrameAnalysis no_groove_frame = curves_frame;
    no_groove_frame.helices[0].groove_quality = Quality_MissingAtoms;
    no_groove_frame.helices[0].grooves.clear();
    ASSERT_TRUE(store_curves_trajectory_frame(&curves_trajectory, 1, no_groove_frame));
    EXPECT_EQ(curves_trajectory.axis_quality[1], (uint16_t)Quality_Good);
    EXPECT_EQ(curves_trajectory.groove_quality[1], (uint16_t)Quality_MissingAtoms);
    EXPECT_TRUE(std::isfinite(curves_trajectory.pair_axis[pairs.size()].tip));
    EXPECT_TRUE(std::isnan(curves_trajectory.grooves[curves_frame.helices[0].grooves.size()].width12));
    CurvesFrameAnalysis invalid_layout_frame = curves_frame;
    invalid_layout_frame.helices[0].pairs[0] = (uint32_t)pairs.size();
    EXPECT_FALSE(store_curves_trajectory_frame(&curves_trajectory, 1, invalid_layout_frame));
    EXPECT_EQ(curves_trajectory.groove_quality[1], (uint16_t)Quality_MissingAtoms);
    const vec3_t shift = {12.0f, -7.0f, 4.0f};
    auto rotate = [](vec3_t p) { return vec3_t{-p.y, p.x, p.z}; };
    auto transform = [&](vec3_t p) { return vec3_add(rotate(p), shift); };
    auto shifted_backbones = groove_backbones;
    auto shifted_midpoints = result.pair_frames;
    auto shifted_axis = curves_axis;
    auto shifted_depth1 = depth1, shifted_depth2 = depth2;
    for (auto& backbone : shifted_backbones) for (auto& p : backbone) p = transform(p);
    for (auto& midpoint : shifted_midpoints) {
        midpoint.origin = transform(midpoint.origin);
        midpoint.x = rotate(midpoint.x);
        midpoint.y = rotate(midpoint.y);
        midpoint.z = rotate(midpoint.z);
    }
    for (auto& point : shifted_axis) {
        point.origin = transform(point.origin);
        point.direction = rotate(point.direction);
    }
    for (auto* depth : {&shifted_depth1, &shifted_depth2}) for (auto& p : *depth) p = transform(p);
    std::vector<CurvesGrooveSample> shifted_grooves;
    ASSERT_TRUE(curves_grooves(&shifted_grooves, shifted_backbones[0], shifted_backbones[1],
                               shifted_midpoints, shifted_axis, shifted_depth1, shifted_depth2));
    for (size_t i = 0; i < grooves.size(); ++i) {
        const float got[] = {shifted_grooves[i].width12, shifted_grooves[i].depth12,
                             shifted_grooves[i].width21, shifted_grooves[i].depth21};
        const float expected[] = {grooves[i].width12, grooves[i].depth12, grooves[i].width21, grooves[i].depth21};
        for (size_t k = 0; k < 4; ++k) {
            EXPECT_EQ(std::isfinite(got[k]), std::isfinite(expected[k]));
            if (std::isfinite(expected[k])) EXPECT_NEAR(got[k], expected[k], 1e-4);
        }
    }
    const auto preserved = shifted_grooves;
    shifted_backbones[0][0].x = NAN;
    EXPECT_FALSE(curves_grooves(&shifted_grooves, shifted_backbones[0], shifted_backbones[1],
                                shifted_midpoints, shifted_axis, shifted_depth1, shifted_depth2));
    shifted_backbones[0][0] = transform(groove_backbones[0][0]);
    shifted_depth1[0] = BaseFrame{}.groove_reference;
    EXPECT_FALSE(curves_grooves(&shifted_grooves, shifted_backbones[0], shifted_backbones[1],
                                shifted_midpoints, shifted_axis, shifted_depth1, shifted_depth2));
    shifted_depth1[0] = transform(depth1[0]);
    shifted_depth2.pop_back();
    EXPECT_FALSE(curves_grooves(&shifted_grooves, shifted_backbones[0], shifted_backbones[1],
                                shifted_midpoints, shifted_axis, shifted_depth1, shifted_depth2));
    ASSERT_EQ(shifted_grooves.size(), preserved.size());
    for (size_t i = 0; i < preserved.size(); ++i) {
        const float got[] = {shifted_grooves[i].level, shifted_grooves[i].width12, shifted_grooves[i].depth12,
                             shifted_grooves[i].width21, shifted_grooves[i].depth21};
        const float expected[] = {preserved[i].level, preserved[i].width12, preserved[i].depth12,
                                  preserved[i].width21, preserved[i].depth21};
        for (size_t k = 0; k < 5; ++k) {
            EXPECT_EQ(std::isfinite(got[k]), std::isfinite(expected[k]));
            if (std::isfinite(expected[k])) EXPECT_EQ(got[k], expected[k]);
        }
    }
    std::vector<vec3_t> flat_backbone(groove_backbones[0].size(), vec3_t{});
    std::vector<CurvesGrooveSample> ambiguous_grooves;
    ASSERT_TRUE(curves_grooves(&ambiguous_grooves, flat_backbone, flat_backbone,
                               result.pair_frames, curves_axis, depth1, depth2));
    ASSERT_EQ(ambiguous_grooves.size(), grooves.size());
    for (const auto& sample : ambiguous_grooves) {
        EXPECT_FALSE(std::isfinite(sample.width12));
        EXPECT_FALSE(std::isfinite(sample.depth12));
        EXPECT_FALSE(std::isfinite(sample.width21));
        EXPECT_FALSE(std::isfinite(sample.depth21));
    }
    for (const auto& base : bases) {
        ASSERT_LT(base.component, (uint32_t)28);
        const auto pucker = curves_fourier_pucker(base, state);
        EXPECT_NEAR(pucker[0], rna_curves_pucker[base.component][0], 0.06f);
        EXPECT_NEAR(pucker[1], rna_curves_pucker[base.component][1], 0.06f);
        vec3_t atoms[BB_Count] = {};
        uint32_t present = 0;
        for (int k = 0; k < BB_Count; ++k) if (base.backbone[k] >= 0) {
            atoms[k] = state.xyz[base.backbone[k]];
            present |= 1u << k;
        }
        Values fourier;
        NucleotideQuality quality;
        compute_nucleotide(&fourier, atoms, &state.unitcell, &quality, present, Pucker_Fourier);
        EXPECT_NEAR(std::remainder((double)fourier.metric[Phase] - rna_curves_pucker[base.component][0], 360.0), 0.0, 0.06);
        EXPECT_NEAR(fourier.metric[Amplitude], rna_curves_pucker[base.component][1], 0.06f);
        EXPECT_FALSE(quality[Phase-Alpha] & ~Quality_NucleotideWarnings);
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
    // 3DNA v2.4, find_pair + analyze, 1RNA: X/Y displacement, rise, inclination, tip, twist.
    const float rna_3dna_helical[13][6] = {
        {-1.62f,-0.23f,3.00f,8.05f,9.15f,35.13f},
        {-4.32f,0.17f,2.81f,22.32f,4.51f,33.91f},
        {-2.62f,-0.08f,2.90f,3.27f,-10.56f,29.47f},
        {-4.57f,-0.87f,2.66f,23.67f,10.17f,33.14f},
        {-2.60f,0.53f,3.02f,9.66f,-2.24f,38.21f},
        {-4.68f,0.40f,2.49f,20.27f,-0.11f,30.30f},
        {-4.45f,0.68f,2.38f,25.34f,-2.85f,32.41f},
        {-4.88f,-0.99f,2.30f,29.95f,6.41f,33.35f},
        {-3.00f,-0.00f,2.92f,16.56f,0.85f,37.58f},
        {-7.03f,-1.24f,2.12f,34.24f,-7.35f,25.20f},
        {-2.76f,0.64f,2.98f,5.81f,1.70f,34.82f},
        {-4.14f,-0.20f,2.66f,23.21f,-0.26f,38.07f},
        {-2.32f,-0.62f,2.49f,6.31f,10.56f,36.72f},
    };
    const float rna_3dna_grooves[7][2] = {
        {15.4f,14.4f}, {15.7f,13.8f}, {16.1f,12.4f}, {16.3f,11.7f},
        {16.3f,10.3f}, {16.5f,11.0f}, {16.1f,12.8f},
    };
    const float rna_3dna_steps[13][6] = {
        {-0.33f,-0.57f,3.09f,-5.48f,4.82f,34.39f},
        {-0.34f,-1.32f,3.52f,-2.57f,12.71f,31.41f},
        {0.56f,-1.17f,2.91f,5.34f,1.66f,28.94f},
        {-0.04f,-1.36f,3.50f,-5.62f,13.08f,30.00f},
        {-0.23f,-1.20f,3.27f,1.46f,6.30f,37.68f},
        {-0.20f,-1.47f,3.17f,0.06f,10.39f,28.51f},
        {-0.24f,-1.27f,3.21f,1.54f,13.71f,29.39f},
        {0.25f,-1.33f,3.41f,-3.52f,16.44f,28.91f},
        {-0.04f,-1.07f,3.34f,-0.54f,10.53f,36.12f},
        {0.90f,-1.41f,3.38f,3.02f,14.07f,20.75f},
        {-0.47f,-1.36f,3.11f,-1.01f,3.47f,34.64f},
        {0.15f,-1.50f,3.48f,0.17f,14.77f,35.18f},
        {-0.06f,-1.19f,2.66f,-6.61f,3.95f,35.94f},
    };
    const float rna_3dna_pairs[14][6] = {
        {-0.05f,-0.12f,0.01f,-4.90f,-28.80f,-0.60f},
        {0.39f,-0.18f,0.13f,7.50f,-15.96f,0.65f},
        {0.13f,-0.11f,0.15f,-3.38f,-17.65f,-3.06f},
        {0.26f,-0.17f,-0.33f,10.74f,-20.03f,3.45f},
        {-0.72f,-0.34f,0.35f,-1.52f,-16.45f,0.80f},
        {0.44f,-0.24f,-0.03f,-0.20f,-18.75f,2.02f},
        {0.08f,-0.11f,0.14f,0.21f,-18.58f,-1.16f},
        {-0.28f,0.00f,-0.29f,5.12f,-18.95f,-3.06f},
        {-0.49f,-0.21f,0.14f,1.28f,-23.69f,1.36f},
        {0.05f,-0.16f,-0.01f,0.86f,-17.33f,-0.26f},
        {-0.78f,-0.31f,-0.05f,-3.70f,-15.98f,0.30f},
        {0.03f,-0.09f,0.07f,3.78f,-20.80f,-4.07f},
        {-0.37f,-0.18f,-0.40f,-8.71f,-21.69f,4.34f},
        {0.26f,-0.11f,0.78f,10.57f,-15.07f,-5.72f},
    };
    ThreeDnaPairStepAnalysis dna;
    ASSERT_TRUE(analyze_3dna_pair_step(&dna, sys, state, bases, pairs, steps, validator));
    ASSERT_EQ(dna.pair_values.size(), pairs.size());
    ASSERT_EQ(dna.step_values.size(), steps.size());
    EXPECT_FALSE(analyze_3dna_pair_step(&dna, sys, state, bases, pairs, steps, validator,
                                        static_cast<PairValuePolicy>(255)));
    EXPECT_EQ(dna.pair_values.size(), pairs.size());
    EXPECT_EQ(dna.step_values.size(), steps.size());
    for (size_t p = 0; p < pairs.size(); ++p) {
        ASSERT_EQ(dna.pair_quality[p], (uint16_t)Quality_Good);
        for (int k = 0; k < 3; ++k) {
            EXPECT_NEAR(dna.pair_values[p].metric[Shear+k], rna_3dna_pairs[p][k], 0.0055f);
            EXPECT_NEAR(dna.pair_values[p].metric[Buckle+k], rna_3dna_pairs[p][k+3], 0.0055f);
        }
        EXPECT_TRUE(std::isnan(dna.pair_values[p].metric[Phase]));
    }
    for (size_t s = 0; s < steps.size(); ++s) {
        ASSERT_EQ(dna.step_quality[s], (uint16_t)Quality_Good);
        for (int k = 0; k < 3; ++k) {
            EXPECT_NEAR(dna.step_values[s].metric[Shift+k], rna_3dna_steps[s][k], 0.0055f);
            EXPECT_NEAR(dna.step_values[s].metric[Tilt+k], rna_3dna_steps[s][k+3], 0.0055f);
        }
        EXPECT_TRUE(std::isnan(dna.step_values[s].metric[MinorGroove]));
    }
    std::vector<vec3_t> dna_p1(pairs.size()), dna_p2(pairs.size());
    for (size_t p = 0; p < pairs.size(); ++p) {
        const auto a = bases[pairs[p].a].backbone[BB_P];
        const auto b = bases[pairs[p].b].backbone[BB_P];
        const vec3_t missing = {NAN, NAN, NAN};
        dna_p1[p] = a >= 0 ? state.xyz[a] : missing;
        dna_p2[p] = b >= 0 ? state.xyz[b] : missing;
    }
    std::vector<float> dna_minor(steps.size()), dna_major(steps.size());
    groove_widths(dna_minor.data(), dna_major.data(), dna_p1.data(), dna_p2.data(), pairs.size(),
                  nullptr, nullptr, nullptr, Groove_3DnaRefined);
    for (size_t s = 0; s < steps.size(); ++s) {
        if (s >= 3 && s <= 9) {
            EXPECT_NEAR(dna_minor[s], rna_3dna_grooves[s-3][0]-5.8f, 0.051f);
            EXPECT_NEAR(dna_major[s], rna_3dna_grooves[s-3][1]-5.8f, 0.051f);
        } else {
            EXPECT_TRUE(std::isnan(dna_minor[s]));
            EXPECT_TRUE(std::isnan(dna_major[s]));
        }
    }
    FrameAnalysis dna_complete;
    ASSERT_TRUE(analyze_3dna_frame(&dna_complete, sys, state, bases, pairs, steps, validator));
    ASSERT_EQ(dna_complete.pair_values.size(), pairs.size());
    ASSERT_EQ(dna_complete.step_values.size(), steps.size());
    EXPECT_TRUE(dna_complete.pair_inversions.empty());
    for (size_t p = 0; p < pairs.size(); ++p) {
        EXPECT_EQ(dna_complete.pair_quality[p], dna.pair_quality[p]);
        for (int id = Buckle; id <= (int)Stagger; ++id)
            EXPECT_NEAR(dna_complete.pair_values[p].metric[id], dna.pair_values[p].metric[id], 1e-5f);
    }
    for (size_t s = 0; s < steps.size(); ++s) {
        EXPECT_EQ(dna_complete.step_quality[s], dna.step_quality[s]);
        EXPECT_EQ(dna_complete.helical_quality[s], (uint16_t)Quality_Good);
        for (int id = Tilt; id <= (int)Rise; ++id)
            EXPECT_NEAR(dna_complete.step_values[s].metric[id], dna.step_values[s].metric[id], 1e-5f);
        for (int k = 0; k < 6; ++k)
            EXPECT_NEAR(dna_complete.step_values[s].metric[XDisplacement+k], rna_3dna_helical[s][k], 0.0055f);
        EXPECT_EQ(std::isfinite(dna_complete.step_values[s].metric[MinorGroove]), std::isfinite(dna_minor[s]));
        EXPECT_EQ(std::isfinite(dna_complete.step_values[s].metric[MajorGroove]), std::isfinite(dna_major[s]));
        if (std::isfinite(dna_minor[s]))
            EXPECT_NEAR(dna_complete.step_values[s].metric[MinorGroove], dna_minor[s], 1e-5f);
        if (std::isfinite(dna_major[s]))
            EXPECT_NEAR(dna_complete.step_values[s].metric[MajorGroove], dna_major[s], 1e-5f);
    }
    auto parallel_bases = bases;
    parallel_bases[pairs[1].b].chain_order = parallel_bases[pairs[0].b].chain_order + 1;
    EXPECT_FALSE(analyze_3dna_frame(&dna_complete, sys, state, parallel_bases, pairs, steps, validator));
    EXPECT_EQ(dna_complete.pair_values.size(), pairs.size());
    EXPECT_NEAR(dna_complete.step_values[3].metric[MinorGroove], dna_minor[3], 1e-5f);
    auto branching = steps;
    branching.push_back(steps[0]);
    EXPECT_FALSE(analyze_3dna_frame(&dna_complete, sys, state, bases, pairs, branching, validator));
    EXPECT_EQ(dna_complete.step_values.size(), steps.size());
    ConventionFrameAnalysis selected;
    ASSERT_TRUE(analyze_convention_frame(&selected, AnalysisConvention::Curves, sys, state, bases, pairs, steps, validator));
    ASSERT_TRUE(std::holds_alternative<CurvesFrameAnalysis>(selected));
    EXPECT_EQ(std::get<CurvesFrameAnalysis>(selected).helices[0].grooves.size(), grooves.size());
    ASSERT_TRUE(analyze_convention_frame(&selected, AnalysisConvention::ThreeDna, sys, state, bases, pairs, steps, validator));
    ASSERT_TRUE(std::holds_alternative<FrameAnalysis>(selected));
    EXPECT_TRUE(std::get<FrameAnalysis>(selected).pair_inversions.empty());
    EXPECT_NEAR(std::get<FrameAnalysis>(selected).step_values[3].metric[MinorGroove], dna_minor[3], 1e-5f);
    ASSERT_TRUE(analyze_convention_frame(&selected, AnalysisConvention::LegacyMixed, sys, state, bases, pairs, steps, validator));
    ASSERT_TRUE(std::holds_alternative<FrameAnalysis>(selected));
    EXPECT_NEAR(std::get<FrameAnalysis>(selected).pair_values[0].metric[Buckle], result.pair_values[0].metric[Buckle], 1e-5f);
    EXPECT_NEAR(std::get<FrameAnalysis>(selected).step_values[3].metric[MinorGroove],
                result.step_values[3].metric[MinorGroove], 1e-5f);
    EXPECT_FALSE(analyze_convention_frame(&selected, AnalysisConvention::Curves, sys, state, bases, pairs,
                                          invalid_curves_steps, validator));
    EXPECT_TRUE(std::holds_alternative<FrameAnalysis>(selected));
    ASSERT_TRUE(analyze_convention_frame(&selected, AnalysisConvention::Curves, sys, state, bases, pairs, steps, validator));
    ASSERT_TRUE(std::holds_alternative<CurvesFrameAnalysis>(selected));
    EXPECT_TRUE(std::isnan(std::get<CurvesFrameAnalysis>(selected).frame.step_values[3].metric[MinorGroove]));
    std::vector<BaseFrame> dna_frames;
    ASSERT_TRUE(extract_3dna_base_frames(dna_frames, sys, state, bases));
    for (size_t p = 0; p < pairs.size(); ++p) {
        ASSERT_EQ(bases[pairs[p].a].component, (uint32_t)p);
        ASSERT_EQ(bases[pairs[p].b].component, (uint32_t)(27-p));
        const auto& a = dna_frames[pairs[p].a];
        auto b = dna_frames[pairs[p].b];
        if (vec3_dot(a.z, b.z) < 0.0f) {
            b.y = vec3_mul1(b.y, -1.0f);
            b.z = vec3_mul1(b.z, -1.0f);
        }
        Values unused = {};
        BasePairFrame pair_frame;
        ASSERT_TRUE(compute_3dna_step(&unused, {b.origin,b.x,b.y,b.z}, {a.origin,a.x,a.y,a.z}, &pair_frame));
        for (int k = 0; k < 3; ++k) {
            EXPECT_NEAR(unused.metric[Shift+k], rna_3dna_pairs[p][k], 0.015f);
            EXPECT_NEAR(unused.metric[Tilt+k], rna_3dna_pairs[p][k+3], 0.03f);
        }
        const vec3_t got[] = {pair_frame.origin, pair_frame.x, pair_frame.y, pair_frame.z};
        for (size_t axis = 0; axis < 4; ++axis)
            for (size_t k = 0; k < 3; ++k)
                EXPECT_NEAR(got[axis].elem[k], rna_3dna_frames[p][axis][k], 0.003f);
    }
    for (size_t s = 0; s < steps.size(); ++s) {
        ASSERT_EQ(steps[s].a, (uint32_t)s);
        ASSERT_EQ(steps[s].b, (uint32_t)(s+1));
        auto reference_frame = [](size_t index) {
            const auto& f = rna_3dna_frames[index];
            return BasePairFrame{{f[0][0],f[0][1],f[0][2]}, {f[1][0],f[1][1],f[1][2]},
                                 {f[2][0],f[2][1],f[2][2]}, {f[3][0],f[3][1],f[3][2]}};
        };
        Values matched_frames = {};
        ASSERT_TRUE(compute_helical_step(&matched_frames, reference_frame(s), reference_frame(s+1)));
        for (int k = 0; k < 6; ++k)
            EXPECT_NEAR(matched_frames.metric[XDisplacement+k], rna_3dna_helical[s][k], 0.03f);
        Values cehs = {};
        ASSERT_TRUE(compute_3dna_step(&cehs, reference_frame(s), reference_frame(s+1)));
        for (int k = 0; k < 3; ++k) {
            EXPECT_NEAR(cehs.metric[Shift+k], rna_3dna_steps[s][k], 0.015f);
            EXPECT_NEAR(cehs.metric[Tilt+k], rna_3dna_steps[s][k+3], 0.03f);
        }
        for (int k = 0; k < 6; ++k)
            EXPECT_NEAR(result.step_values[s].metric[XDisplacement+k], rna_3dna_helical[s][k], 0.03f);
        if (s == 0) {
            Values curves_step = {};
            ASSERT_TRUE(compute_step(&curves_step, reference_frame(s), reference_frame(s+1)));
            EXPECT_NEAR(curves_step.metric[Tilt], -5.57f, 0.02f);
            EXPECT_NEAR(curves_step.metric[Roll], 4.90f, 0.02f);
            // Classic 3DNA reports tilt -5.48 and roll 4.82 for these same frames.
            EXPECT_GT(std::fabs(curves_step.metric[Tilt] - (-5.48f)), 0.06f);
            EXPECT_GT(std::fabs(curves_step.metric[Roll] - 4.82f), 0.06f);
        }
        if (s >= 3 && s <= 9) {
            EXPECT_NEAR(result.step_values[s].metric[MinorGroove], rna_3dna_grooves[s-3][0]-5.8f, 0.051f);
            EXPECT_NEAR(result.step_values[s].metric[MajorGroove], rna_3dna_grooves[s-3][1]-5.8f, 0.051f);
        }
    }
    const auto axis = compute_axis(result.pair_frames, steps, &state.unitcell);
    EXPECT_EQ(axis.offsets.size(), (size_t)2);
    EXPECT_EQ(axis.points.size(), (size_t)105);
    // A-form signature of the 3DNA local helical parameters: displaced and inclined pairs, small tip.
    float mean[Metric_Count] = {};
    for (const auto& step : result.step_values)
        for (int k = XDisplacement; k <= (int)HelicalTwist; ++k) mean[k] += step.metric[k] / steps.size();
    EXPECT_LT(mean[XDisplacement], -3.0f);
    EXPECT_NEAR(mean[YDisplacement], 0.0f, 0.5f);
    EXPECT_NEAR(mean[HelicalRise], 2.7f, 0.3f);
    EXPECT_GT(mean[Inclination], 12.0f);
    EXPECT_NEAR(mean[Tip], 0.0f, 3.0f);
    EXPECT_NEAR(mean[HelicalTwist], 33.0f, 3.0f);

    auto degenerate_3dna_bases = bases;
    auto& fit = degenerate_3dna_bases[pairs[0].a].fit_atoms;
    fit[3] = fit[2];
    FrameAnalysis degenerate;
    ASSERT_TRUE(analyze_frame(&degenerate, sys, state, degenerate_3dna_bases, pairs, steps, validator));
    ASSERT_EQ(degenerate.pair_quality[0], (uint16_t)Quality_Good);
    ASSERT_EQ(degenerate.step_quality[0], (uint16_t)Quality_Good);
    EXPECT_TRUE(degenerate.helical_quality[0] & Quality_DegenerateBase);
    EXPECT_TRUE(std::isfinite(degenerate.step_values[0].metric[Twist]));
    for (int k = XDisplacement; k <= (int)HelicalTwist; ++k)
        EXPECT_TRUE(std::isnan(degenerate.step_values[0].metric[k]));

    ThreeDnaPairStepAnalysis dna_degenerate;
    ASSERT_TRUE(analyze_3dna_pair_step(&dna_degenerate, sys, state, degenerate_3dna_bases, pairs, steps, validator));
    EXPECT_TRUE(dna_degenerate.pair_quality[0] & Quality_DegenerateBase);
    EXPECT_TRUE(dna_degenerate.step_quality[0] & Quality_AdjacentPair);
    for (int k = Buckle; k <= (int)Stagger; ++k)
        EXPECT_TRUE(std::isnan(dna_degenerate.pair_values[0].metric[k]));
    for (int k = Tilt; k <= (int)Rise; ++k)
        EXPECT_TRUE(std::isnan(dna_degenerate.step_values[0].metric[k]));
    EXPECT_EQ(dna_degenerate.pair_quality[1], (uint16_t)Quality_Good);
    EXPECT_EQ(dna_degenerate.step_quality[1], (uint16_t)Quality_Good);
    EXPECT_TRUE(std::isfinite(dna_degenerate.step_values[1].metric[Twist]));

    auto degenerate_curves_bases = bases;
    auto& curves_fit = degenerate_curves_bases[pairs[0].a].fit_atoms;
    curves_fit[0] = curves_fit[1];
    FrameAnalysis mixed_degenerate, independent_dna;
    ASSERT_TRUE(analyze_frame(&mixed_degenerate, sys, state, degenerate_curves_bases, pairs, steps, validator));
    ASSERT_NE(mixed_degenerate.pair_quality[0], (uint16_t)Quality_Good);
    ASSERT_TRUE(analyze_3dna_frame(&independent_dna, sys, state, degenerate_curves_bases, pairs, steps, validator));
    EXPECT_EQ(independent_dna.pair_quality[0], (uint16_t)Quality_Good);
    EXPECT_EQ(independent_dna.helical_quality[0], (uint16_t)Quality_Good);
    for (int id = Buckle; id <= (int)Stagger; ++id)
        EXPECT_NEAR(independent_dna.pair_values[0].metric[id], dna.pair_values[0].metric[id], 1e-5f);
    for (int id = XDisplacement; id <= (int)HelicalTwist; ++id)
        EXPECT_NEAR(independent_dna.step_values[0].metric[id], rna_3dna_helical[0][id-XDisplacement], 0.0055f);

    auto invalid_steps = steps;
    invalid_steps[0].a = (uint32_t)pairs.size();
    EXPECT_FALSE(analyze_3dna_pair_step(&dna_degenerate, sys, state, bases, pairs, invalid_steps, validator));
    EXPECT_TRUE(dna_degenerate.pair_quality[0] & Quality_DegenerateBase);

    state.unitcell = md_unitcell_from_extent(100, 100, 100);
    ThreeDnaPairStepAnalysis whole, wrapped;
    ASSERT_TRUE(analyze_3dna_pair_step(&whole, sys, state, bases, pairs, steps, validator));
    CurvesFrameAnalysis whole_curves, wrapped_curves;
    ASSERT_TRUE(analyze_curves_frame(&whole_curves, sys, state, bases, pairs, steps, validator));
    const auto residue_atoms = md_system_residue_atom_range(&sys, bases[pairs[2].b].component);
    for (uint32_t atom = residue_atoms.beg; atom < residue_atoms.end; ++atom) state.xyz[atom].x += 100.0f;
    ASSERT_TRUE(analyze_3dna_pair_step(&wrapped, sys, state, bases, pairs, steps, validator));
    ASSERT_TRUE(analyze_curves_frame(&wrapped_curves, sys, state, bases, pairs, steps, validator));
    ASSERT_EQ(wrapped_curves.helices.size(), (size_t)1);
    EXPECT_EQ(wrapped_curves.helices[0].axis_quality, (uint16_t)Quality_Good);
    EXPECT_EQ(wrapped_curves.helices[0].groove_quality, (uint16_t)Quality_Good);
    for (size_t i = 0; i < whole_curves.helices[0].axis.size(); ++i) {
        EXPECT_NEAR(vec3_length(vec3_sub(wrapped_curves.helices[0].axis[i].origin,
                                         whole_curves.helices[0].axis[i].origin)), 0.0f, 0.001f);
        EXPECT_NEAR(vec3_length(vec3_sub(wrapped_curves.helices[0].axis[i].direction,
                                         whole_curves.helices[0].axis[i].direction)), 0.0f, 0.001f);
    }
    for (size_t i = 0; i < whole_curves.helices[0].grooves.size(); ++i) {
        const auto& a = whole_curves.helices[0].grooves[i];
        const auto& b = wrapped_curves.helices[0].grooves[i];
        const float expected[] = {a.width12, a.depth12, a.width21, a.depth21};
        const float got[] = {b.width12, b.depth12, b.width21, b.depth21};
        for (size_t k = 0; k < 4; ++k) {
            EXPECT_EQ(std::isfinite(expected[k]), std::isfinite(got[k]));
            if (std::isfinite(expected[k])) EXPECT_NEAR(expected[k], got[k], 0.001f);
        }
    }
    for (size_t p = 0; p < pairs.size(); ++p) {
        ASSERT_EQ(wrapped.pair_quality[p], whole.pair_quality[p]);
        ASSERT_EQ(wrapped.pair_quality[p], (uint16_t)Quality_Good);
        for (int k = Buckle; k <= (int)Stagger; ++k)
            EXPECT_NEAR(wrapped.pair_values[p].metric[k], whole.pair_values[p].metric[k], 0.001f);
    }
    for (size_t s = 0; s < steps.size(); ++s) {
        ASSERT_EQ(wrapped.step_quality[s], whole.step_quality[s]);
        ASSERT_EQ(wrapped.step_quality[s], (uint16_t)Quality_Good);
        for (int k = Tilt; k <= (int)Rise; ++k)
            EXPECT_NEAR(wrapped.step_values[s].metric[k], whole.step_values[s].metric[k], 0.001f);
    }
    for (uint32_t atom = residue_atoms.beg; atom < residue_atoms.end; ++atom) state.xyz[atom].x -= 85.0f;
    ThreeDnaPairStepAnalysis disrupted;
    ASSERT_TRUE(analyze_3dna_pair_step(&disrupted, sys, state, bases, pairs, steps, validator));
    EXPECT_NE(disrupted.pair_quality[2], (uint16_t)Quality_Good);
    EXPECT_TRUE(disrupted.step_quality[1] & Quality_AdjacentPair);
    EXPECT_TRUE(disrupted.step_quality[2] & Quality_AdjacentPair);
    EXPECT_TRUE(std::isnan(disrupted.pair_values[2].metric[Shear]));
    EXPECT_TRUE(std::isnan(disrupted.step_values[1].metric[Twist]));
    EXPECT_EQ(disrupted.step_quality[3], (uint16_t)Quality_Good);
    ThreeDnaPairStepAnalysis flagged;
    ASSERT_TRUE(analyze_3dna_pair_step(&flagged, sys, state, bases, pairs, steps, validator,
                                       PairValues_ReferenceWithQualityFlags));
    EXPECT_NE(flagged.pair_quality[2], (uint16_t)Quality_Good);
    EXPECT_TRUE(std::isfinite(flagged.pair_values[2].metric[Shear]));
    EXPECT_TRUE(std::isfinite(flagged.step_values[1].metric[Twist]));
    EXPECT_NE(flagged.step_quality[1], (uint16_t)Quality_Good);
    FrameAnalysis reference_frame;
    ASSERT_TRUE(analyze_3dna_frame(&reference_frame, sys, state, bases, pairs, steps, validator));
    EXPECT_TRUE(std::isfinite(reference_frame.pair_values[2].metric[Shear]));
    EXPECT_TRUE(std::isfinite(reference_frame.step_values[1].metric[Twist]));
    EXPECT_TRUE(std::isfinite(reference_frame.step_values[1].metric[HelicalTwist]));
    EXPECT_NE(reference_frame.pair_quality[2], (uint16_t)Quality_Good);
    EXPECT_NE(reference_frame.helical_quality[1], (uint16_t)Quality_Good);
    CurvesFrameAnalysis flagged_curves;
    ASSERT_TRUE(analyze_curves_frame(&flagged_curves, sys, state, bases, pairs, steps, validator));
    EXPECT_NE(flagged_curves.frame.pair_quality[2], (uint16_t)Quality_Good);
    EXPECT_TRUE(std::isfinite(flagged_curves.frame.pair_values[2].metric[Shear]));
    EXPECT_TRUE(std::isfinite(flagged_curves.frame.step_values[1].metric[Twist]));
    EXPECT_TRUE(std::isnan(flagged_curves.frame.step_values[1].metric[HelicalTwist]));

    md_arena_allocator_destroy(alloc);
}

UTEST(lavery_core, published_series_copy_values_support_run_relative_scripts_and_clean_up_on_reset) {
    auto* alloc = md_arena_allocator_create(md_get_heap_allocator(), MD_MEGABYTES(4));
    md_system_t sys = {.alloc = alloc};
    md_system_state_t state = {.alloc = alloc};
    const auto path = (std::filesystem::path(__FILE__).parent_path() / "data/lavery_1rna.pdb").string();
    ASSERT_TRUE(md_pdb_system_init_from_file(&sys, &state, md_str_from_cstr(path.c_str()), {}, nullptr));
    ASSERT_TRUE(md_util_system_infer(&sys, &state, MD_UTIL_INFER_ALL, nullptr));
    const double times[] = {0, 10, 20};
    const md_attribute_desc_t time = {
        .path = MD_STR_INIT("run/test/time"),
        .format = {.type = MD_ATTRIBUTE_TYPE_F64, .components = 1, .rank = 1, .shape = {3}},
        .flags = MD_ATTRIBUTE_FLAG_TEMPORAL, .unit = md_unit_picosecond(),
        .data = times, .byte_size = sizeof(times),
    };
    ASSERT_NE(md_attributes_create(&sys.attributes, &time), MD_ATTRIBUTE_INVALID);
    std::vector<vec3_t> coordinates;
    for (int f = 0; f < 3; ++f) coordinates.insert(coordinates.end(), state.xyz, state.xyz + state.num_atoms);
    const float cells[27] = {};
    const md_attribute_desc_t position = {
        .path = MD_STR_INIT("run/test/atom/position"),
        .format = {.type = MD_ATTRIBUTE_TYPE_F32, .components = 3, .rank = 2, .shape = {3, (uint32_t)state.num_atoms}},
        .flags = MD_ATTRIBUTE_FLAG_TEMPORAL, .unit = md_unit_angstrom(),
        .data = coordinates.data(), .byte_size = coordinates.size() * sizeof(vec3_t),
    };
    const md_attribute_desc_t cell = {
        .path = MD_STR_INIT("run/test/unitcell"),
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
    const auto* attr = md_attributes_find(&sys.attributes, MD_STR_LIT("run/test/lavery/step/twist"));
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
        MD_STR_LIT("twist = attr(\"lavery/step/twist\"); quality = attr(\"lavery/step/quality\");"), &sys, nullptr);
    if (!compiled) md_script_ir_write_diagnostics(md_writer_stderr(), ir);
    ASSERT_TRUE(compiled);
    EXPECT_EQ(md_script_ir_property_count(ir), (size_t)2);
    auto* eval = md_script_eval_create(3, ir, sys.alloc);
    ASSERT_TRUE(eval != nullptr);
    ASSERT_TRUE(md_script_eval_frame_range(eval, ir, &sys, MD_STR_LIT("run/test"), 0, 3));
    const auto* twist = md_attributes_find(md_script_eval_attributes(eval), MD_STR_LIT("script/twist"));
    ASSERT_TRUE(twist != nullptr);
    EXPECT_EQ(md_attribute_element_count(&twist->format), (size_t)6);
    EXPECT_EQ(((const float*)twist->data)[0], 1);
    EXPECT_EQ(((const float*)twist->data)[5], 6);
    md_script_eval_free(eval);
    md_script_ir_free(ir);
    ASSERT_TRUE(publish_series(&sys.attributes, "run/test", "step/twist", "Twist", MD_ATTRIBUTE_TYPE_F32,
        3, 2, values.data(), values.size() * sizeof(float), md_unit_degree()));
    attr = md_attributes_find(&sys.attributes, MD_STR_LIT("run/test/lavery/step/twist"));
    ASSERT_TRUE(attr != nullptr);
    EXPECT_EQ(((const float*)attr->data)[0], 99);
    EXPECT_EQ(md_attributes_remove_prefix(&sys.attributes, MD_STR_LIT("run/test/lavery")), (size_t)2);
    EXPECT_TRUE(md_attributes_find(&sys.attributes, MD_STR_LIT("lavery/step/twist")) == nullptr);
    EXPECT_TRUE(md_attributes_find(&sys.attributes, MD_STR_LIT("lavery/step/quality")) == nullptr);
    EXPECT_TRUE(md_attributes_find(&sys.attributes, MD_STR_LIT("run/test/lavery/step/twist")) == nullptr);
    EXPECT_TRUE(md_attributes_find(&sys.attributes, MD_STR_LIT("run/test/time")) != nullptr);
    EXPECT_FALSE(publish_series(&sys.attributes, "run/test", "step/twist", "Twist", MD_ATTRIBUTE_TYPE_F32,
        3, 2, values.data(), sizeof(float), md_unit_degree()));
    EXPECT_TRUE(md_attributes_find(&sys.attributes, MD_STR_LIT("lavery/step/twist")) == nullptr);
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
