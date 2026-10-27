#include <components/lavery/lavery_core.h>
#include <md_gro.h>
#include <md_xtc.h>
#include <md_system.h>
#include <core/md_allocator.h>
#include <core/md_arena_allocator.h>
#include <core/md_str.h>
#include <utest.h>

#include <filesystem>
#include <fstream>
#include <chrono>
#include <cmath>
#include <limits>

using namespace lavery;
namespace {
struct Files {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("viamd-lavery-reference-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Files() { std::filesystem::create_directory(dir); }
    ~Files() {
        for (const char* file : {"reference.csv","frames.xtc","frames.gro","frames.csv","failed.xtc","failed.gro","failed.csv"})
            std::filesystem::remove(dir/file);
        std::filesystem::remove(dir);
    }
};
std::string sample(size_t frame, float value, size_t member=1, Metric metric=Rise) {
    CsvSample row;
    row.run="control,\"DNA\"";
    row.frame=frame;
    row.member=member;
    row.label="1-2: S1:DC1 -> S1:DG2";
    row.pair_a="1: S1:DC1 / S2:DG24";
    row.pair_b="2: S1:DG2 / S2:DC23";
    row.metric=metric;
    row.value=value;
    return csv_sample(row);
}
}

UTEST(lavery_core, external_csv_reads_raw_values_quotes_and_legacy_schema) {
    Files files;
    const auto path=(files.dir/"reference.csv").string();
    std::ofstream file(path);
    file << csv_header() << sample(0,3) << sample(1,4) << sample(2,NAN);
    file.close();
    ExternalReference reference;
    std::string error;
    ASSERT_TRUE(load_external_reference(&reference,path,&error));
    EXPECT_EQ(reference.records,(size_t)3);
    EXPECT_FALSE(reference.fingerprint.empty());
    EXPECT_STREQ(reference.run.c_str(),"control,\"DNA\"");
    ASSERT_EQ(reference.members[Class_Step].size(),(size_t)1);
    EXPECT_EQ(reference.members[Class_Step][0].values[Rise].size(),(size_t)2);
    EXPECT_EQ(reference.members[Class_Step][0].values[Rise][0],3);
    EXPECT_STREQ(reference.members[Class_Step][0].identity.c_str(),"S1:DC1 / S2:DG24 -> S1:DG2 / S2:DC23");
    // Old parameter CSVs have no appended reference_source column.
    std::string header=csv_header(), row=sample(0,3);
    header.erase(header.find(",reference_source"));
    header += '\n';
    row.erase(row.rfind(",\"current_trajectory\""));
    row += '\n';
    std::ofstream(path) << header << row;
    ASSERT_TRUE(load_external_reference(&reference,path,&error));
    EXPECT_EQ(reference.records,(size_t)1);
    std::ofstream(path) << csv_header() << sample(0,3) << sample(0,4);
    EXPECT_FALSE(load_external_reference(&reference,path,&error));
    EXPECT_EQ(reference.records,(size_t)0);
    EXPECT_FALSE(error.empty());
    auto invalid=sample(0,3);
    invalid.replace(invalid.find("Angstrom"),8,"nm");
    std::ofstream(path) << csv_header() << invalid;
    EXPECT_FALSE(load_external_reference(&reference,path,&error));
    invalid=sample(0,3);
    invalid.replace(invalid.find("step/rise"),9,"bp/rise");
    std::ofstream(path) << csv_header() << invalid;
    EXPECT_FALSE(load_external_reference(&reference,path,&error));
    std::ofstream(path) << csv_header() << "\"unfinished";
    EXPECT_FALSE(load_external_reference(&reference,path,&error));
    EXPECT_FALSE(load_external_reference(&reference,(files.dir/"absent.csv").string(),&error));
    CsvSample multiline;
    multiline.run="control\nDNA"; multiline.member=1; multiline.metric=Rise; multiline.value=3;
    std::string crlf=csv_header();
    crlf.replace(crlf.size()-1,1,"\r\n");
    std::ofstream(path,std::ios::binary) << "\xEF\xBB\xBF" << crlf << csv_sample(multiline);
    ASSERT_TRUE(load_external_reference(&reference,path,&error));
    EXPECT_STREQ(reference.run.c_str(),"control\nDNA");
    std::ofstream(path) << csv_header() << sample(0,3) << csv_sample(multiline);
    EXPECT_FALSE(load_external_reference(&reference,path,&error));
    invalid=sample(0,3);
    invalid.replace(invalid.find(",3,1,"),5,",nan,1,");
    std::ofstream(path) << csv_header() << invalid;
    EXPECT_FALSE(load_external_reference(&reference,path,&error));
    std::ofstream(path) << csv_header() << sample(2,3) << sample(0,4) << sample(1,5);
    ASSERT_TRUE(load_external_reference(&reference,path,&error));
}

UTEST(lavery_core, external_reference_matches_partner_identity_not_member_order) {
    const auto identity=reference_identity(Class_Pair,"","7: S2:DT9 / S1:DA2","");
    EXPECT_STREQ(identity.c_str(),"S2:DT9 / S1:DA2");
    EXPECT_TRUE(reference_identity(Class_Nucleotide,"S1:DA2 (bp 7)","S1:DA2 / S2:DT9","")=="S1:DA2 | S1:DA2 / S2:DT9");
    std::vector<ReferenceMember> source={{"old9","b",{}},{"old2","a",{}}};
    const auto mapping=match_reference_members(source,{"a","b","c",""});
    ASSERT_EQ(mapping.size(),(size_t)4);
    EXPECT_EQ(mapping[0],1); EXPECT_EQ(mapping[1],0); EXPECT_EQ(mapping[2],-1); EXPECT_EQ(mapping[3],-1);
    source.push_back({"duplicate","a",{}});
    EXPECT_EQ(match_reference_members(source,{"a"})[0],-1);
    EXPECT_EQ(match_reference_members(source,{"b","b"})[0],-1);
}

UTEST(lavery_core, external_statistics_preserve_missing_states_pooling_and_current_values) {
    ExternalReference reference;
    reference.members[Class_Step].resize(2);
    reference.members[Class_Step][0].values[Rise]={2,3,4};
    reference.members[Class_Step][1].values[Rise]={4,5,6};
    MetricStatistics stats;
    const std::vector<float> current={7,7,7,7,7,7};
    ASSERT_TRUE(external_statistics(&stats,reference,{0,-1},current,Rise,Baseline_MeanSD));
    EXPECT_NEAR(stats.members[0].mean,3,1e-8);
    EXPECT_EQ(stats.members[1].count,(size_t)0);
    EXPECT_TRUE(stats.members[0].is_outlier(7,2.5));
    EXPECT_TRUE(std::isnan(stats.heatmap[0]));
    EXPECT_FALSE(external_statistics(&stats,reference,{99},{70},Chi,Baseline_MeanSD));
    EXPECT_TRUE(stats.members.empty());
    ASSERT_TRUE(external_statistics(&stats,reference,{0,1},current,Rise,Baseline_MedianMAD,{0,0}));
    EXPECT_NEAR(stats.members[0].mean,4,1e-8);
    EXPECT_EQ(stats.members[0].count,(size_t)6);
    EXPECT_EQ(external_samples(reference,Rise,{0,0},0,{0,0}).size(),(size_t)3);
    reference.members[Class_Nucleotide].resize(1);
    reference.members[Class_Nucleotide][0].values[Chi]={-130,-120,-110};
    ASSERT_TRUE(external_statistics(&stats,reference,{0},{70,-120},Chi,Baseline_MeanSD));
    EXPECT_EQ(stats.members[0].reference(70).count,(size_t)0);
    EXPECT_EQ(stats.members[0].reference(-120).count,(size_t)3);
    EXPECT_TRUE(std::isnan(stats.heatmap[0]));
    reference.members[Class_Nucleotide][0].values[Alpha]={179,-179};
    ASSERT_TRUE(external_statistics(&stats,reference,{0},{180},Alpha,Baseline_MeanSD));
    EXPECT_NEAR(std::fabs(stats.members[0].mean),180,1e-6);
}

UTEST(lavery_core, representatives_rank_exact_rms_scores_and_temporal_separation) {
    std::array<std::vector<float>,Metric_Count> values;
    std::array<MetricStatistics,Metric_Count> stats;
    std::array<size_t,Metric_Count> columns={};
    columns[Rise]=columns[Twist]=1;
    values[Rise]={10,11,10,12,NAN,10};
    values[Twist]={30,30,34,30,30,30};
    stats[Rise].members={{10,1,100}};
    stats[Twist].members={{30,2,100}};
    const std::vector<FrameFeature> features={{Rise,0},{Twist,0}};
    std::vector<FrameCandidate> frames;
    std::string error;
    ASSERT_TRUE(representative_frames(&frames,values,stats,columns,features,0,6,6,0,{},&error));
    ASSERT_EQ(frames.size(),(size_t)5);
    EXPECT_EQ(frames[0].frame,(size_t)0); EXPECT_EQ(frames[1].frame,(size_t)5);
    EXPECT_EQ(frames[2].frame,(size_t)1); EXPECT_NEAR(frames[2].score,std::sqrt(.5),1e-7);
    ASSERT_TRUE(representative_frames(&frames,values,stats,columns,features,0,6,6,10,{0,1,10,11,12,20},&error));
    ASSERT_EQ(frames.size(),(size_t)3);
    EXPECT_EQ(frames[0].frame,(size_t)0); EXPECT_EQ(frames[1].frame,(size_t)5); EXPECT_EQ(frames[2].frame,(size_t)2);
    stats[Rise].members[0].sd=0;
    EXPECT_FALSE(representative_frames(&frames,values,stats,columns,features,0,6,6,0,{},&error));
    EXPECT_EQ(frames.size(),(size_t)0);
    stats[Rise].members[0].sd=1;
    EXPECT_FALSE(representative_frames(&frames,values,stats,columns,{{Rise,0},{Rise,0}},0,6,6,0,{},&error));
    EXPECT_FALSE(representative_frames(&frames,values,stats,columns,features,0,6,6,0,{0,0,1,2,3,4},&error));
    ASSERT_TRUE(representative_frames(&frames,values,stats,columns,features,1,4,1,0,{},&error));
    EXPECT_EQ(frames[0].frame,(size_t)1);
    columns[Chi]=1;
    values[Chi]={70,-120,70,-120,70,-120};
    MetricStatistics chi_stats;
    ASSERT_TRUE(compute_statistics(&chi_stats,{-130,-120,-110},1,true,Baseline_MeanSD,Chi));
    stats[Chi]=chi_stats;
    ASSERT_TRUE(representative_frames(&frames,values,stats,columns,{{Chi,0}},0,6,6,0,{},&error));
    ASSERT_EQ(frames.size(),(size_t)3);
    for (const auto& f : frames) EXPECT_EQ(f.frame%2,(size_t)1);
}

UTEST(lavery_core, representative_bulk_features_combine_classes_components_and_members) {
    std::vector<FrameFeature> features;
    ASSERT_TRUE(add_frame_features(&features,Class_Pair,class_all_parameters(Class_Pair),{0,1}));
    EXPECT_EQ(features.size(),(size_t)(2*class_parameter_count(Class_Pair)));
    const size_t before=features.size();
    ASSERT_TRUE(add_frame_features(&features,Class_Pair,class_all_parameters(Class_Pair),{1,0,0}));
    EXPECT_EQ(features.size(),before);
    ASSERT_TRUE(add_frame_features(&features,Class_Step,(1u<<(Rise-class_first_metric(Class_Step))) |
        (1u<<(Twist-class_first_metric(Class_Step))),{2,4}));
    ASSERT_TRUE(add_frame_features(&features,Class_Nucleotide,1u<<(Chi-class_first_metric(Class_Nucleotide)),{3}));
    EXPECT_EQ(features.size(),before+5);
    EXPECT_EQ(features.back().metric,Chi);
    EXPECT_EQ(features.back().member,(size_t)3);
    ASSERT_TRUE(add_frame_features(&features,Class_Step,0,{0}));
    EXPECT_EQ(features.size(),before+5);
    EXPECT_FALSE(add_frame_features(&features,Class_Count,1,{0}));
    EXPECT_FALSE(add_frame_features(&features,Class_Pair,1u<<31,{0}));
    features.clear();
    size_t total=0;
    for (int cls=0; cls<Class_Count; ++cls) {
        ASSERT_TRUE(add_frame_features(&features,cls,class_all_parameters(cls),{0,1,2}));
        total+=3*class_parameter_count(cls);
    }
    EXPECT_EQ(features.size(),total);

    std::array<std::vector<float>,Metric_Count> values;
    std::array<MetricStatistics,Metric_Count> stats;
    std::array<size_t,Metric_Count> columns={};
    const std::vector<FrameFeature> mixed={{Opening,0},{Rise,0},{Beta,0}};
    for (const auto& f : mixed) {
        columns[f.metric]=1;
        stats[f.metric].members={{0,1,10}};
    }
    values[Opening]={0,1,0};
    values[Rise]={0,1,0};
    values[Beta]={3,1,NAN};
    std::vector<FrameCandidate> candidates;
    std::string error;
    ASSERT_TRUE(representative_frames(&candidates,values,stats,columns,mixed,0,3,3,0,{},&error));
    ASSERT_EQ(candidates.size(),(size_t)2);
    EXPECT_EQ(candidates[0].frame,(size_t)1);
    EXPECT_NEAR(candidates[0].score,1,1e-8);
    EXPECT_NEAR(candidates[1].score,std::sqrt(3.),1e-8);
}

UTEST(lavery_core, period_summary_preserves_missing_states_and_reference_denominators) {
    PeriodSummary summary;
    EXPECT_TRUE(std::isnan(summary.mean_z()));
    summary.add(1,2,0);
    summary.add(2,-2,1);
    summary.add(3,NAN,1);
    summary.add(NAN,3,2);
    EXPECT_EQ(summary.valid,(size_t)3);
    EXPECT_EQ(summary.missing,(size_t)1);
    EXPECT_EQ(summary.referenced,(size_t)2);
    EXPECT_EQ(summary.states[0],(size_t)1);
    EXPECT_EQ(summary.states[1],(size_t)2);
    EXPECT_EQ(summary.states[2],(size_t)0);
    EXPECT_NEAR(summary.mean_z(),0,1e-8);
    EXPECT_NEAR(summary.rms_z(),2,1e-8);
}

UTEST(lavery_core, pairing_disruption_explanations_do_not_reclassify_chemistry) {
    EXPECT_TRUE(pairing_disruption_reasons(Quality_Good).empty());
    EXPECT_TRUE(pairing_disruption_reasons(Quality_MissingAtoms).empty());
    const auto reasons=pairing_disruption_reasons(Quality_Separation|Quality_NoPairContacts);
    EXPECT_TRUE(reasons.find("Ring separation")!=std::string::npos);
    EXPECT_TRUE(reasons.find("canonical")!=std::string::npos);
    PairingEvidence evidence;
    evidence.complete=true; evidence.expected=2;
    EXPECT_EQ(classify_pairing(evidence,-120,-120,Quality_Good),Pairing_Other);
    EXPECT_EQ(classify_pairing(evidence,-120,-120,Quality_Separation),Pairing_Unpaired);
    evidence.canonical=1;
    EXPECT_EQ(classify_pairing(evidence,-120,-120,Quality_Separation),Pairing_Other);
    evidence.complete=false;
    EXPECT_EQ(classify_pairing(evidence,-120,-120,Quality_Separation),Pairing_Unavailable);
}

UTEST(lavery_core, representative_xtc_gro_export_preserves_atoms_times_coordinates_and_rank) {
    Files files;
    auto* alloc=md_arena_allocator_create(md_get_heap_allocator(),MEGABYTES(16));
    md_system_t sys={.alloc=alloc};
    md_system_state_t state={.alloc=alloc};
    ASSERT_TRUE(md_gro_system_init_from_file(&sys,&state,STR_LIT(VIAMD_TEST_DATA_DIR "/nucl-dna.gro")));
    const double times[]={100,120,140};
    const md_attribute_desc_t time={.path=STR_INIT("run/test/time"),
        .format={.type=MD_ATTRIBUTE_TYPE_F64,.components=1,.rank=1,.shape={3}},
        .flags=MD_ATTRIBUTE_FLAG_TEMPORAL,.unit=md_unit_picosecond(),.data=times,.byte_size=sizeof(times)};
    ASSERT_NE(md_attributes_create(&sys.attributes,&time),MD_ATTRIBUTE_INVALID);
    std::vector<vec3_t> xyz;
    for (int f=0; f<3; ++f) for (size_t a=0; a<state.num_atoms; ++a) xyz.push_back(vec3_add(state.xyz[a],{(float)f,0,0}));
    const md_attribute_desc_t position={.path=STR_INIT("run/test/atom/position"),
        .format={.type=MD_ATTRIBUTE_TYPE_F32,.components=3,.rank=2,.shape={3,(uint32_t)state.num_atoms}},
        .flags=MD_ATTRIBUTE_FLAG_TEMPORAL,.unit=md_unit_angstrom(),.data=xyz.data(),.byte_size=xyz.size()*sizeof(vec3_t)};
    ASSERT_NE(md_attributes_create(&sys.attributes,&position),MD_ATTRIBUTE_INVALID);
    const float cells[]={30,0,0,2,31,0,3,4,32, 30,0,0,2,31,0,3,4,32, 30,0,0,2,31,0,3,4,32};
    const md_attribute_desc_t cell={.path=STR_INIT("run/test/unitcell"),
        .format={.type=MD_ATTRIBUTE_TYPE_F32,.components=1,.rank=3,.shape={3,3,3}},
        .flags=MD_ATTRIBUTE_FLAG_TEMPORAL,.unit=md_unit_angstrom(),.data=cells,.byte_size=sizeof(cells)};
    ASSERT_NE(md_attributes_create(&sys.attributes,&cell),MD_ATTRIBUTE_INVALID);
    std::string error;
    const auto path=(files.dir/"frames.xtc").string();
    EXPECT_FALSE(export_representative_frames(sys,"run/test",(files.dir/"frames.csv").string(),{{0,0}},"",&error));
    EXPECT_FALSE(export_representative_frames(sys,"run/test",path,{{0,0},{0,1}},"",&error));
    EXPECT_FALSE(std::filesystem::exists(path));
    ASSERT_TRUE(export_representative_frames(sys,"run/test",path,{{2,.1},{0,.2}},"ref,\"test\"",&error));
    EXPECT_FALSE(export_representative_frames(sys,"run/test",path,{{1,0}},"",&error));
    md_system_t reload={.alloc=alloc};
    md_system_state_t loaded={.alloc=alloc};
    const auto gro=(files.dir/"frames.gro").string();
    ASSERT_TRUE(md_gro_system_init_from_file(&reload,&loaded,str_from_cstr(gro.c_str())));
    ASSERT_EQ(loaded.num_atoms,state.num_atoms);
    for (size_t a=0; a<state.num_atoms; ++a) {
        EXPECT_NEAR(loaded.xyz[a].x,state.xyz[a].x,.0002);
        EXPECT_NEAR(loaded.xyz[a].y,state.xyz[a].y,.0002);
        EXPECT_NEAR(loaded.xyz[a].z,state.xyz[a].z,.0002);
    }
    ASSERT_TRUE(md_xtc_system_publish_run(&reload,str_from_cstr(path.c_str()),STR_LIT("run/export"),MD_RUN_FLAG_DISABLE_CACHE_WRITE));
    const auto* time_check=md_attributes_find(&reload.attributes,STR_LIT("run/export/time"));
    ASSERT_TRUE(time_check && time_check->data);
    EXPECT_EQ(time_check->format.shape[0],2u);
    EXPECT_EQ(((const double*)time_check->data)[0],100);
    EXPECT_EQ(((const double*)time_check->data)[1],140);
    const str_t paths[]={STR_LIT("atom/position"),STR_LIT("unitcell")};
    auto* extract=md_system_extract_begin(&reload,STR_LIT("run/export"),paths,2,alloc);
    ASSERT_TRUE(extract!=nullptr);
    for (size_t f=0; f<2; ++f) {
        ASSERT_TRUE(md_system_extract_frame(extract,f,&loaded));
        for (size_t a=0; a<state.num_atoms; ++a) {
            EXPECT_NEAR(loaded.xyz[a].x,state.xyz[a].x+2*f,.011f);
            EXPECT_NEAR(loaded.xyz[a].y,state.xyz[a].y,.011f);
            EXPECT_NEAR(loaded.xyz[a].z,state.xyz[a].z,.011f);
        }
        EXPECT_NEAR(loaded.unitcell.xy,2,.001);
        EXPECT_NEAR(loaded.unitcell.yz,4,.001);
    }
    md_system_extract_end(extract);
    std::ifstream manifest(files.dir/"frames.csv");
    std::string text((std::istreambuf_iterator<char>(manifest)),std::istreambuf_iterator<char>());
    EXPECT_TRUE(text.find("0,0,100,0.20000000000000001,2,\"ref,\"\"test\"\"\"")!=std::string::npos);
    EXPECT_TRUE(text.find("1,2,140,0.10000000000000001,1,")!=std::string::npos);
    auto* positions=md_attributes_find(&sys.attributes,STR_LIT("run/test/atom/position"));
    ASSERT_TRUE(positions && positions->data);
    ((vec3_t*)positions->data)[0].x=NAN;
    EXPECT_FALSE(export_representative_frames(sys,"run/test",(files.dir/"failed.xtc").string(),{{0,0}},"",&error));
    for (const char* name : {"failed.xtc","failed.gro","failed.csv"}) EXPECT_FALSE(std::filesystem::exists(files.dir/name));
    md_arena_allocator_destroy(alloc);
}

UTEST(lavery_core, transition_filters_preserve_scope_direction_and_missing_selection) {
    TransitionFilter filter;
    EXPECT_TRUE(filter.matches(Class_Pair,7,Pairing_WatsonCrick,Pairing_Hoogsteen));
    EXPECT_TRUE(filter.matches(Class_Nucleotide,14,0,1));
    filter.scope=TransitionFilter::SelectedPair;
    filter.pair=7;
    EXPECT_TRUE(filter.matches(Class_Pair,7,1,2));
    EXPECT_FALSE(filter.matches(Class_Pair,6,1,2));
    EXPECT_TRUE(filter.matches(Class_Nucleotide,14,0,1));
    EXPECT_TRUE(filter.matches(Class_Nucleotide,15,1,0));
    EXPECT_FALSE(filter.matches(Class_Nucleotide,16,0,1));
    filter.from=0; filter.to=1;
    EXPECT_TRUE(filter.matches(Class_Nucleotide,14,0,1));
    EXPECT_FALSE(filter.matches(Class_Nucleotide,14,1,0));
    filter.from=-1;
    EXPECT_TRUE(filter.matches(Class_Nucleotide,15,2,1));
    filter.scope=TransitionFilter::SelectedNucleotide;
    EXPECT_FALSE(filter.matches(Class_Nucleotide,15,0,1));
    filter.nucleotide=15;
    EXPECT_TRUE(filter.matches(Class_Nucleotide,15,0,1));
    EXPECT_FALSE(filter.matches(Class_Nucleotide,14,0,1));
    EXPECT_FALSE(filter.matches(Class_Pair,7,0,1));
    EXPECT_FALSE(filter.matches(Class_Step,15,0,1));
    filter=TransitionFilter{};
    EXPECT_TRUE(filter.matches(Class_Nucleotide,14,1,0));
    EXPECT_TRUE(filter.matches(Class_Pair,6,Pairing_Other,Pairing_Unpaired));
    filter.scope=TransitionFilter::SelectedPair;
    filter.pair=SIZE_MAX;
    EXPECT_FALSE(filter.matches(Class_Pair,0,1,2));
    EXPECT_FALSE(filter.matches(Class_Nucleotide,0,0,1));
}

UTEST(lavery_core, pairing_filters_keep_all_pair_detection_and_raw_history_intact) {
    const std::vector<PairingState> states={
        Pairing_WatsonCrick,Pairing_Unpaired,
        Pairing_WatsonCrick,Pairing_Unpaired,
        Pairing_Hoogsteen,Pairing_WatsonCrick,
        Pairing_Hoogsteen,Pairing_WatsonCrick};
    EventOptions options;
    options.min_frames=2;
    TransitionFilter filter;
    size_t detected=0, shown=0;
    for (size_t pair=0; pair<2; ++pair) {
        std::vector<PairingRun> history;
        std::vector<PairingTransition> changes;
        ASSERT_TRUE(pairing_runs(&history,states,2,pair));
        ASSERT_TRUE(pairing_transitions(&changes,history,options));
        ASSERT_EQ(history.size(),(size_t)2);
        ASSERT_EQ(changes.size(),(size_t)1);
        const auto& change=changes.front();
        detected+=changes.size();
        EXPECT_TRUE(filter.matches(Class_Pair,pair,change.source.state,change.destination.state));
        TransitionFilter selected;
        selected.scope=TransitionFilter::SelectedPair;
        selected.pair=0;
        selected.from=Pairing_WatsonCrick;
        selected.to=Pairing_Hoogsteen;
        shown+=selected.matches(Class_Pair,pair,change.source.state,change.destination.state);
        EXPECT_EQ(history.front().begin,(size_t)0);
        EXPECT_EQ(history.back().end,(size_t)3);
    }
    EXPECT_EQ(detected,(size_t)2);
    EXPECT_EQ(shown,(size_t)1);
    EXPECT_EQ(states.front(),Pairing_WatsonCrick);
    EXPECT_EQ(states.back(),Pairing_WatsonCrick);
}
UTEST(lavery_core, comparison_unequal_groups_share_bins_and_unit_integrals) {
    SampleComparison result;
    const float nan=std::numeric_limits<float>::quiet_NaN();
    ASSERT_TRUE(compare_samples(&result,{{1,2,3,nan},{1,1,2,2,3,3},{},{nan}},false,12));
    ASSERT_EQ(result.histogram.valid.size(),(size_t)4);
    EXPECT_EQ(result.histogram.valid[0],(size_t)3);
    EXPECT_EQ(result.histogram.valid[1],(size_t)6);
    EXPECT_EQ(result.missing[0],(size_t)1);
    EXPECT_EQ(result.missing[3],(size_t)1);
    EXPECT_NEAR(result.summaries[0].mean,2,1e-8);
    EXPECT_NEAR(result.summaries[0].sd,std::sqrt(2./3),1e-8);
    for (size_t g=0;g<4;++g) {
        double integral=0;
        for (size_t b=0;b<12;++b) integral+=result.histogram.density[g*12+b]*result.histogram.bin_width;
        EXPECT_NEAR(integral,g<2 ? 1. : 0.,1e-8);
    }
    for (size_t b=0;b<12;++b) EXPECT_NEAR(result.histogram.density[b],result.histogram.density[12+b],1e-8);
    ASSERT_TRUE(compare_samples(&result,{{5,5},{5}},false));
    EXPECT_GT(result.histogram.bin_width,0);
    EXPECT_NEAR(result.summaries[0].sd,0,1e-8);
}

UTEST(lavery_core, comparison_angles_wrap_across_the_branch_cut_without_changing_samples) {
    SampleComparison result;
    const std::vector<std::vector<float>> groups={{179,-179},{539,181}};
    ASSERT_TRUE(compare_samples(&result,groups,true,16));
    EXPECT_LT(result.histogram.centers.back()-result.histogram.centers.front(),3);
    EXPECT_NEAR(result.summaries[0].sd,1,1e-6);
    EXPECT_NEAR(std::remainder(result.summaries[0].mean-result.summaries[1].mean,360.),0,1e-6);
    for (size_t b=0;b<16;++b) EXPECT_NEAR(result.histogram.density[b],result.histogram.density[16+b],1e-6);
    EXPECT_EQ(groups[0][1],-179.f);
    ASSERT_TRUE(compare_samples(&result,{{},{NAN}},true));
    EXPECT_TRUE(result.histogram.centers.empty());
    EXPECT_EQ(result.histogram.valid[0],(size_t)0);
    EXPECT_EQ(result.missing[1],(size_t)1);
    EXPECT_FALSE(compare_samples(&result,{},false));
    EXPECT_FALSE(compare_samples(&result,{{1}},false,0));
}

UTEST(lavery_core, comparison_external_csv_and_period_samples_share_units_not_time) {
    Files files;
    const auto path=(files.dir/"reference.csv").string();
    {
        std::ofstream csv(path);
        csv<<csv_header();
        csv<<sample(9000,3,1,Rise)<<sample(9500,5,1,Rise);
    }
    ExternalReference external;
    std::string error;
    ASSERT_TRUE(load_external_reference(&external,path,&error));
    const auto control=external_samples(external,Rise,{0},0);
    const std::vector<float> period={2,4,6,NAN};
    SampleComparison result;
    ASSERT_TRUE(compare_samples(&result,{control,period},false));
    EXPECT_EQ(result.histogram.valid[0],(size_t)2);
    EXPECT_EQ(result.histogram.valid[1],(size_t)3);
    EXPECT_NEAR(result.summaries[0].mean,4,1e-8);
    EXPECT_NEAR(result.summaries[1].mean,4,1e-8);
    EXPECT_EQ(result.missing[1],(size_t)1);
    EXPECT_EQ(external.members[Class_Step][0].values[Rise].size(),(size_t)2);
    EXPECT_EQ(period[0],2.f);
    ASSERT_TRUE(compare_samples(&result,{external_samples(external,Rise,{-1},0),period},false));
    EXPECT_EQ(result.histogram.valid[0],(size_t)0);
    EXPECT_EQ(result.histogram.valid[1],(size_t)3);
}

UTEST(lavery_core, dashboard_pools_all_csv_members_and_keeps_32_normalized_bins) {
    ExternalReference source;
    source.members[Class_Step].resize(2);
    source.members[Class_Step][0].values[Rise]={2,3};
    source.members[Class_Step][1].values[Rise]={4};
    source.members[Class_Step][0].values[Twist]={179,-179};
    source.members[Class_Step][1].values[Twist]={180};
    source.members[Class_Pair].resize(1);
    source.members[Class_Pair][0].values[Buckle]={90};
    const auto pooled=pooled_external_samples(source,Rise);
    ASSERT_EQ(pooled.size(),(size_t)3);
    EXPECT_EQ(pooled[0],2.f);
    EXPECT_EQ(pooled[2],4.f);
    EXPECT_TRUE(pooled_external_samples(source,Slide).empty());
    SampleComparison result;
    ASSERT_TRUE(compare_samples(&result,{pooled,{2,3,4,NAN},{}},false,32));
    ASSERT_EQ(result.histogram.centers.size(),(size_t)32);
    ASSERT_EQ(result.histogram.density.size(),(size_t)96);
    EXPECT_EQ(result.histogram.valid[0],(size_t)3);
    EXPECT_EQ(result.histogram.valid[2],(size_t)0);
    EXPECT_EQ(result.missing[1],(size_t)1);
    EXPECT_NEAR(result.summaries[0].mean,3,1e-8);
    double area=0;
    for (size_t b=0;b<32;++b) {
        area+=result.histogram.density[b]*result.histogram.bin_width;
        EXPECT_NEAR(result.histogram.density[b],result.histogram.density[32+b],1e-8);
    }
    EXPECT_NEAR(area,1,1e-8);
    ASSERT_TRUE(compare_samples(&result,{pooled_external_samples(source,Twist),{180}},true,32));
    EXPECT_LT(result.histogram.centers.back()-result.histogram.centers.front(),3);
    EXPECT_EQ(class_parameter_count(Class_Pair),6);
    EXPECT_EQ(class_parameter_count(Class_Step),8);
    EXPECT_EQ(class_parameter_count(Class_Nucleotide),10);
}

UTEST(lavery_core, comparison_overlay_filters_are_independent_and_require_metadata) {
    const std::string gc="1: S1:DC1 / S2:DG24", at="2: S1:DA2 / S2:DT23";
    ComparisonMemberFilter all, filtered;
    bool missing=false;
    filtered.bp_first=filtered.bp_last=1;
    filtered.pair_type=2;
    EXPECT_TRUE(comparison_member_matches(filtered,Class_Pair,"",gc,"",&missing));
    EXPECT_FALSE(missing);
    EXPECT_FALSE(comparison_member_matches(filtered,Class_Pair,"",at,"",&missing));
    EXPECT_FALSE(missing);
    EXPECT_TRUE(comparison_member_matches(all,Class_Pair,"",at,"",&missing));
    EXPECT_FALSE(comparison_member_matches(filtered,Class_Step,"",gc,at,&missing));
    filtered.bp_last=2; filtered.pair_type=0;
    EXPECT_TRUE(comparison_member_matches(filtered,Class_Step,"",gc,at,&missing));
    filtered.nucleotide='G';
    EXPECT_TRUE(comparison_member_matches(filtered,Class_Nucleotide,"S2:DG24 (bp 1)",gc,"",&missing));
    EXPECT_FALSE(comparison_member_matches(filtered,Class_Nucleotide,"S1:DC1 (bp 1)",gc,"",&missing));
    EXPECT_FALSE(comparison_member_matches(filtered,Class_Nucleotide,"unknown",gc,"",&missing));
    EXPECT_TRUE(missing);
    EXPECT_FALSE(comparison_member_matches(filtered,Class_Pair,"","","",&missing));
    EXPECT_TRUE(missing);
    EXPECT_TRUE(comparison_member_matches(all,Class_Pair,"","","",&missing));
    filtered={}; filtered.pair_type=1;
    EXPECT_TRUE(comparison_member_matches(filtered,Class_Pair,"",at,"",&missing));
    EXPECT_TRUE(comparison_member_matches(filtered,Class_Pair,"","3: S1:A3 / S2:U22","",&missing));
    filtered.pair_type=3;
    EXPECT_TRUE(comparison_member_matches(filtered,Class_Pair,"","3: S1:DA3 / S2:DA22","",&missing));
    EXPECT_FALSE(comparison_member_matches(filtered,Class_Pair,"","3: S1:UNK3 / S2:DA22","",&missing));
    EXPECT_TRUE(missing);
    Files files;
    const auto path=(files.dir/"reference.csv").string();
    { std::ofstream csv(path); csv<<csv_header()<<sample(0,3); }
    ExternalReference external;
    std::string error;
    ASSERT_TRUE(load_external_reference(&external,path,&error));
    ASSERT_EQ(external.members[Class_Step].size(),(size_t)1);
    EXPECT_STREQ(external.members[Class_Step][0].pair_a.c_str(),gc.c_str());
    EXPECT_STREQ(external.members[Class_Step][0].pair_b.c_str(),"2: S1:DG2 / S2:DC23");
}
