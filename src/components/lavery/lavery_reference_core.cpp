#include "lavery_core.h"

#include <md_system.h>
#include <core/md_allocator.h>
#include <core/md_str.h>
#include <core/md_unit.h>
#include <core/md_log.h>
#include <core/md_hash.h>
#include "../../../ext/mdlib/ext/xtc/xdrfile_xtc.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <filesystem>
#include <charconv>
#include <climits>
#include <cstdio>
#include <limits>
#include <tuple>

namespace lavery {
bool workflow_reference_statistics(MetricStatistics* out, const std::vector<float>& current, size_t members,
    Metric metric, size_t first, size_t end, Baseline baseline, bool raw,
    const std::vector<uint32_t>& groups, const ExternalReference* external, const std::vector<int>& mapping) {
    if (out) *out={};
    if (!out || !members || metric<0 || metric>=Metric_Count || baseline<0 || baseline>=Baseline_Count ||
        (!external && (current.size()%members || first>=end || end>current.size()/members)) ||
        (!groups.empty() && groups.size()!=members) ||
        std::any_of(groups.begin(),groups.end(),[&](uint32_t group) { return group>=members; }) ||
        (external && (!external->records || mapping.size()!=members))) {
        MD_LOG_ERROR("Lavery: invalid workflow reference configuration");
        return false;
    }
    if (external) for (int member : mapping) {
        if (member < -1 || (member>=0 && (size_t)member>=external->members[metric_class(metric)].size())) {
            MD_LOG_ERROR("Lavery: invalid workflow reference mapping");
            return false;
        }
    }
    if (!external) {
        if (raw) return sequence_statistics(&out->members,current,members,metric_is_angular(metric),first,end,groups);
        return compute_statistics(out,current,members,metric_is_angular(metric),baseline,
            metric_state_count(metric) ? (int)metric : -1,first,end,groups);
    }
    if (!raw) return external_statistics(out,*external,mapping,current,metric,baseline,groups);
    out->members.assign(members,{});
    std::vector<MemberStatistics> cached(members);
    std::vector<uint8_t> ready(members,0);
    for (size_t m=0;m<members;++m) {
        if (mapping[m]<0) continue;
        const size_t group=groups.empty() ? m : groups[m];
        if (!ready[group]) {
            std::vector<MemberStatistics> summary;
            if (!sequence_statistics(&summary,external_samples(*external,metric,mapping,m,groups),1,metric_is_angular(metric)))
                return false;
            cached[group]=summary[0]; ready[group]=1;
        }
        out->members[m]=cached[group];
    }
    return true;
}

namespace {
bool fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    MD_LOG_ERROR("Lavery: %s", message.c_str());
    return false;
}

// Read one RFC4180 record, including quoted commas, escaped quotes and embedded newlines.
bool csv_record(std::istream& input, std::vector<std::string>& fields, std::string& error) {
    fields.clear();
    std::string field;
    bool quoted = false, closed = false, started = false;
    char c;
    while (input.get(c)) {
        started = true;
        if (quoted) {
            if (c == '"') {
                if (input.peek() == '"') { input.get(c); field += '"'; }
                else { quoted = false; closed = true; }
            } else field += c;
        } else if (c == ',' || c == '\n' || c == '\r') {
            fields.push_back(field);
            field.clear();
            closed = false;
            if (c != ',') {
                if (c == '\r' && input.peek() == '\n') input.get(c);
                return true;
            }
        } else if (c == '"' && field.empty() && !closed) quoted = true;
        else if (closed || c == '"') { error = "Malformed quoted CSV field"; return false; }
        else field += c;
    }
    if (quoted) { error = "Unterminated CSV quote"; return false; }
    if (started) { fields.push_back(field); return true; }
    return false;
}

bool unsigned_number(const std::string& s, size_t& n) {
    const auto result = std::from_chars(s.data(),s.data()+s.size(),n);
    return result.ec == std::errc() && result.ptr == s.data()+s.size();
}
std::string pair_identity(const std::string& s) {
    const size_t prefix = s.find(": ");
    const std::string pair = prefix == std::string::npos ? s : s.substr(prefix+2);
    const size_t slash = pair.find(" / ");
    if (slash == std::string::npos) return {};
    // Preserve partner order: reversing a pair can reverse signed geometric components.
    return pair;
}
}

std::string reference_identity(int cls, const std::string& label, const std::string& a, const std::string& b) {
    if (cls == Class_Nucleotide) {
        const auto pos = label.find(" (bp ");
        const auto partner = pair_identity(a);
        return pos == std::string::npos || partner.empty() ? std::string{} : label.substr(0,pos)+" | "+partner;
    }
    const auto first = pair_identity(a);
    if (cls == Class_Pair) return first;
    const auto second = pair_identity(b);
    return first.empty() || second.empty() ? std::string{} : first+" -> "+second;
}

bool filter_sequence_profile(std::vector<MemberStatistics>* summary, int cls,
    const std::vector<StepIndex>& steps, const std::vector<std::string>& pairs,
    const std::vector<std::string>& labels, const ComparisonMemberFilter& filter, int partner,
    size_t* metadata_excluded, std::vector<uint8_t>* included) {
    if (!summary || !metadata_excluded || cls<0 || cls>=Class_Count || !filter.bp_first ||
        filter.bp_last<filter.bp_first || partner<0 || partner>2 || filter.pair_type<0 || filter.pair_type>3 ||
        summary->size()!=labels.size() ||
        summary->size()!=(cls==Class_Step ? steps.size() : pairs.size()*(cls==Class_Nucleotide ? 2 : 1))) {
        MD_LOG_ERROR("Lavery: invalid sequence profile filter");
        return false;
    }
    if (cls==Class_Step) for (const auto& step : steps) if (step.a>=pairs.size() || step.b>=pairs.size()) {
        MD_LOG_ERROR("Lavery: invalid sequence profile step endpoints");
        return false;
    }
    *metadata_excluded=0;
    if (included) included->assign(summary->size(),0);
    for (size_t m=0;m<summary->size();++m) {
        if (!sequence_export_member(cls,m,steps,filter.bp_first,filter.bp_last,partner)) {
            (*summary)[m]={};
            continue;
        }
        const auto& a=pairs[cls==Class_Step ? steps[m].a : cls==Class_Nucleotide ? m/2 : m];
        const std::string b=cls==Class_Step ? pairs[steps[m].b] : "";
        bool missing=false;
        if (!comparison_member_matches(filter,cls,labels[m],a,b,&missing)) (*summary)[m]={};
        else if (included) (*included)[m]=1;
        if (missing) ++*metadata_excluded;
    }
    return true;
}

bool comparison_member_matches(const ComparisonMemberFilter& filter, int cls,
    const std::string& label, const std::string& pair_a, const std::string& pair_b, bool* metadata_missing) {
    *metadata_missing=false;
    auto base=[](const std::string& text) {
        const auto colon=text.find(':');
        if (colon==std::string::npos) return char(0);
        const auto end=text.find_first_of("0123456789 -",colon+1);
        const auto name=text.substr(colon+1,end==std::string::npos ? end : end-colon-1);
        if (name=="A" || name=="DA" || name=="ADE") return 'A';
        if (name=="T" || name=="DT" || name=="THY") return 'T';
        if (name=="U" || name=="DU" || name=="URA") return 'U';
        if (name=="G" || name=="DG" || name=="GUA") return 'G';
        if (name=="C" || name=="DC" || name=="CYT") return 'C';
        return char(0);
    };
    auto pair_matches=[&](const std::string& pair) {
        if (filter.bp_first!=1 || filter.bp_last!=SIZE_MAX) {
            size_t bp=0;
            const auto colon=pair.find(':');
            if (colon==std::string::npos) { *metadata_missing=true; return false; }
            const auto parsed=std::from_chars(pair.data(),pair.data()+colon,bp);
            if (parsed.ec!=std::errc() || parsed.ptr!=pair.data()+colon || !bp) {
                *metadata_missing=true;
                return false;
            }
            if (bp<filter.bp_first || bp>filter.bp_last) return false;
        }
        if (filter.pair_type) {
            const auto identity=pair_identity(pair);
            const auto slash=identity.find(" / ");
            if (slash==std::string::npos) { *metadata_missing=true; return false; }
            const char a=base(identity.substr(0,slash)), b=base(identity.substr(slash+3));
            if (!a || !b) { *metadata_missing=true; return false; }
            const bool at=(a=='A' && (b=='T' || b=='U')) || (b=='A' && (a=='T' || a=='U'));
            const bool gc=(a=='G' && b=='C') || (a=='C' && b=='G');
            if (filter.pair_type!=(at ? 1 : gc ? 2 : 3)) return false;
        }
        return true;
    };
    if (!pair_matches(pair_a) || (cls==Class_Step && !pair_matches(pair_b))) return false;
    if (cls==Class_Nucleotide && filter.nucleotide) {
        const char nucleotide=base(label);
        if (!nucleotide) { *metadata_missing=true; return false; }
        if (nucleotide!=filter.nucleotide) return false;
    }
    return true;
}

bool load_external_reference(ExternalReference* out, const std::string& path, std::string* error) {
    if (!out) return fail(error,"Missing external reference output");
    *out = {};
    if (error) error->clear();
    std::ifstream input(path,std::ios::binary);
    if (!input) return fail(error,"Cannot open reference CSV: "+path);
    std::vector<std::string> row;
    std::string parse_error;
    if (!csv_record(input,row,parse_error)) return fail(error,"Cannot read reference CSV header: "+parse_error);
    if (!row.empty() && row[0].starts_with("\xEF\xBB\xBF")) row[0].erase(0,3);
    std::map<std::string,size_t> headers;
    for (size_t i = 0; i < row.size(); ++i)
        if (!headers.emplace(row[i],i).second) return fail(error,"Duplicate CSV column: "+row[i]);
    for (const char* name : {"run","frame","class","member","label","pair_a","pair_b","parameter","unit","value","valid"})
        if (!headers.count(name)) return fail(error,std::string("Missing CSV column: ")+name);
    const size_t width = row.size();
    uint64_t fingerprint = 0x4c4156455259;
    auto hash_record = [&]() {
        for (const auto& field : row) {
            const size_t length=field.size();
            fingerprint=md_hash64(&length,sizeof(length),fingerprint);
            fingerprint=md_hash64(field.data(),field.size(),fingerprint);
        }
    };
    hash_record();
    ExternalReference result;
    result.path = path;
    std::array<std::map<size_t,size_t>,Class_Count> member_ids;
    std::array<std::vector<std::array<std::vector<size_t>,Metric_Count>>,Class_Count> frame_ids;
    while (csv_record(input,row,parse_error)) {
        if (row.size()==1 && row[0].empty()) continue;
        if (row.size()!=width) return fail(error,"Reference CSV record has wrong number of columns");
        hash_record();
        const auto get = [&](const char* name) -> const std::string& { return row[headers.at(name)]; };
        if (!result.records) result.run = get("run");
        if (get("run") != result.run) return fail(error,"Reference CSV contains multiple runs; export one run per reference");
        int cls = -1, metric = -1;
        for (int c = 0; c < Class_Count; ++c) if (get("class")==class_csv_name(c)) cls=c;
        for (int k = 0; k < (int)Metric_Count; ++k) if (get("parameter")==metric_path((Metric)k)) metric=k;
        size_t member, frame;
        if (cls<0 || metric<0 || metric_class((Metric)metric)!=cls ||
            !unsigned_number(get("member"),member) || !member || !unsigned_number(get("frame"),frame))
            return fail(error,"Invalid reference class, parameter, member or frame");
        if (get("unit") != (metric_uses_degrees((Metric)metric) ? "deg" : "Angstrom"))
            return fail(error,"Reference unit mismatch for "+get("parameter"));
        auto& ids = member_ids[cls];
        auto [it,inserted] = ids.emplace(member,result.members[cls].size());
        const auto identity = reference_identity(cls,get("label"),get("pair_a"),get("pair_b"));
        if (inserted) {
            result.members[cls].push_back({get("label"),identity,{},get("pair_a"),get("pair_b")});
            frame_ids[cls].emplace_back();
        }
        frame_ids[cls][it->second][metric].push_back(frame);
        auto& target = result.members[cls][it->second];
        if (target.label!=get("label") || target.identity!=identity ||
            target.pair_a!=get("pair_a") || target.pair_b!=get("pair_b"))
            return fail(error,"Reference member identity changes within CSV");
        if (get("valid")!="0" && get("valid")!="1") return fail(error,"Invalid CSV valid flag");
        if (get("valid")=="1") {
            float value = 0;
            const auto& text = get("value");
            const auto parsed = std::from_chars(text.data(),text.data()+text.size(),value);
            if (parsed.ec!=std::errc() || parsed.ptr!=text.data()+text.size() || !std::isfinite(value))
                return fail(error,"Nonfinite or invalid value marked valid in reference CSV");
            target.values[metric].push_back(value);
        }
        ++result.records;
    }
    if (!parse_error.empty() || input.bad()) return fail(error,"Reference CSV read failed: "+parse_error);
    if (!result.records) return fail(error,"Reference CSV has no samples");
    for (auto& cls : frame_ids) for (auto& member : cls) for (auto& frames : member) {
        std::sort(frames.begin(),frames.end());
        if (std::adjacent_find(frames.begin(),frames.end())!=frames.end())
            return fail(error,"Duplicate reference sample (frame/member/parameter)");
    }
    result.fingerprint=std::to_string(fingerprint);
    *out = std::move(result);
    return true;
}

bool load_curves_geometry_reference(ExternalCurvesGeometryReference* out, const std::string& path,
                                    std::string* error) {
    if (!out) return fail(error,"Missing Curves+ geometry reference output");
    *out = {};
    if (error) error->clear();
    std::ifstream input(path,std::ios::binary);
    if (!input) return fail(error,"Cannot open Curves+ geometry CSV: "+path);
    std::vector<std::string> row;
    std::string parse_error;
    if (!csv_record(input,row,parse_error)) return fail(error,"Cannot read Curves+ geometry CSV header: "+parse_error);
    if (!row.empty() && row[0].starts_with("\xEF\xBB\xBF")) row[0].erase(0,3);
    std::map<std::string,size_t> headers;
    for (size_t i=0;i<row.size();++i)
        if (!headers.emplace(row[i],i).second) return fail(error,"Duplicate CSV column: "+row[i]);
    static const char* required[] = {"run","frame","helix","quantity","member","level",
        "x_displacement","y_displacement","inclination","tip","axis_bend","rise","twist",
        "W12","D12","W21","D21","axis_quality","groove_quality"};
    for (const char* name : required)
        if (!headers.count(name)) return fail(error,std::string("Missing Curves+ geometry CSV column: ")+name);
    const size_t width=row.size();
    uint64_t fingerprint=0x435552564553ULL;
    auto hash_record=[&]() {
        for (const auto& field : row) {
            const size_t length=field.size();
            fingerprint=md_hash64(&length,sizeof(length),fingerprint);
            fingerprint=md_hash64(field.data(),field.size(),fingerprint);
        }
    };
    hash_record();
    static const char* quantity_columns[] = {"x_displacement","y_displacement","inclination","tip","axis_bend",
        "rise","twist","W12","D12","W21","D21"};
    ExternalCurvesGeometryReference result;
    result.path=path;
    std::map<std::tuple<size_t,std::string,int>,size_t> series_ids;
    std::set<std::pair<size_t,size_t>> frame_series;
    struct PendingSample {
        size_t frame, series;
        std::array<float,CurvesGeometryQuantity_Count> values;
    };
    std::vector<PendingSample> pending;
    while (csv_record(input,row,parse_error)) {
        if (row.size()==1 && row[0].empty()) continue;
        if (row.size()!=width) return fail(error,"Curves+ geometry CSV record has wrong number of columns");
        hash_record();
        const auto get=[&](const char* name) -> const std::string& { return row[headers.at(name)]; };
        if (get("run").empty()) return fail(error,"Curves+ geometry CSV has an empty run label");
        if (!result.records) result.run=get("run");
        if (get("run")!=result.run) return fail(error,"Curves+ geometry CSV contains multiple runs");
        size_t frame=0, helix=0, axis_quality=0, groove_quality=0;
        if (!unsigned_number(get("frame"),frame) || frame==SIZE_MAX ||
            !unsigned_number(get("helix"),helix) || !helix ||
            !unsigned_number(get("axis_quality"),axis_quality) || axis_quality>UINT16_MAX ||
            !unsigned_number(get("groove_quality"),groove_quality) || groove_quality>UINT16_MAX)
            return fail(error,"Invalid Curves+ geometry frame, helix or quality value");
        const std::string& kind=get("quantity");
        const int group=kind=="pair_axis" ? 0 : kind=="inter_axis" ? 1 : kind=="groove" ? 2 : -1;
        if (group<0) return fail(error,"Unknown Curves+ geometry quantity group: "+kind);
        float level=std::numeric_limits<float>::quiet_NaN();
        if (group==2) {
            const auto& text=get("level");
            const auto parsed=std::from_chars(text.data(),text.data()+text.size(),level);
            if (text.empty() || parsed.ec!=std::errc() || parsed.ptr!=text.data()+text.size() || !std::isfinite(level))
                return fail(error,"Invalid groove level in Curves+ geometry CSV");
        } else if (!get("level").empty()) return fail(error,"Unexpected level for non-groove Curves+ geometry row");
        if (get("member").empty()) return fail(error,"Curves+ geometry CSV member label is empty");
        auto [series_it,inserted]=series_ids.emplace(std::make_tuple(helix,get("member"),group),result.series.size());
        if (inserted) {
            CurvesGeometryReferenceSeries series;
            series.helix=helix;
            series.category=group;
            series.member=get("member");
            series.level=level;
            result.series.push_back(std::move(series));
        }
        const size_t series_id=series_it->second;
        auto& series=result.series[series_id];
        if (group==2) {
            if (!std::isfinite(series.level) || std::fabs(series.level-level)>1.0e-5f)
                return fail(error,"Curves+ groove member level changes within the CSV");
        } else if (std::isfinite(series.level)) return fail(error,"Curves+ member category changes within the CSV");
        if (!frame_series.emplace(frame,series_id).second)
            return fail(error,"Duplicate Curves+ geometry sample (frame/helix/member)");
        const size_t first_allowed=group==0 ? 0 : group==1 ? 5 : 7;
        const size_t end_allowed=group==0 ? 5 : group==1 ? 7 : CurvesGeometryQuantity_Count;
        const bool quality_good=group==2 ? groove_quality==Quality_Good : axis_quality==Quality_Good;
        PendingSample sample{frame,series_id,{}};
        sample.values.fill(std::numeric_limits<float>::quiet_NaN());
        for (size_t q=0;q<CurvesGeometryQuantity_Count;++q) {
            const auto& text=get(quantity_columns[q]);
            if (q<first_allowed || q>=end_allowed) {
                if (!text.empty()) return fail(error,"Unexpected quantity value in Curves+ geometry row");
                continue;
            }
            if (text.empty() || !quality_good) continue;
            float value=0;
            const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
            if (parsed.ec!=std::errc() || parsed.ptr!=text.data()+text.size() || !std::isfinite(value))
                return fail(error,"Invalid Curves+ geometry value");
            sample.values[q]=value;
        }
        result.frames=std::max(result.frames,frame+1);
        pending.push_back(std::move(sample));
        ++result.records;
    }
    if (!parse_error.empty() || input.bad()) return fail(error,"Curves+ geometry CSV read failed: "+parse_error);
    if (!result.records) return fail(error,"Curves+ geometry CSV has no samples");
    if (result.frames>result.records)
        return fail(error,"Curves+ geometry CSV frame indices are implausibly sparse");
    for (auto& series : result.series) for (auto& samples : series.values)
        samples.assign(result.frames,std::numeric_limits<float>::quiet_NaN());
    for (const auto& sample : pending) for (size_t q=0;q<CurvesGeometryQuantity_Count;++q)
        if (std::isfinite(sample.values[q])) result.series[sample.series].values[q][sample.frame]=sample.values[q];
    result.fingerprint=std::to_string(fingerprint);
    *out=std::move(result);
    return true;
}

bool curves_geometry_reference_statistics(MemberStatistics* out, const ExternalCurvesGeometryReference& reference,
                                          size_t helix, const std::string& member, int quantity, float level) {
    if (!out || quantity<0 || quantity>=CurvesGeometryQuantity_Count || !reference.records ||
        !reference.frames || member.empty() || !helix ||
        (quantity>=CurvesGrooveW12 && !std::isfinite(level))) {
        MD_LOG_ERROR("Lavery: invalid Curves+ geometry reference statistics input");
        return false;
    }
    *out={};
    const int group=quantity>=CurvesGrooveW12 ? 2 : quantity>=CurvesInterAxisRise ? 1 : 0;
    const CurvesGeometryReferenceSeries* match=nullptr;
    for (const auto& series : reference.series) {
        if (series.helix!=helix || series.category!=group || series.member!=member ||
            (group==2) != std::isfinite(series.level) ||
            (group==2 && std::fabs(series.level-level)>1.0e-5f)) continue;
        if (match) {
            MD_LOG_ERROR("Lavery: ambiguous Curves+ geometry reference member mapping");
            return false;
        }
        match=&series;
    }
    if (!match) return true;
    const bool angular=quantity==CurvesAxisInclination || quantity==CurvesAxisTip ||
        quantity==CurvesAxisBend || quantity==CurvesInterAxisTwist;
    std::vector<MemberStatistics> summary;
    if (!sequence_statistics(&summary,match->values[quantity],1,angular)) return false;
    *out=summary[0];
    return true;
}

bool detect_curves_geometry_events(std::vector<CurvesGeometryEvent>* out,
    const CurvesTrajectoryAnalysis& trajectory, const CurvesGeometryEventOptions& options,
    const std::atomic<bool>* cancel) {
    if (!out) return fail(nullptr,"Missing Curves+ geometry events output");
    out->clear();
    const size_t frames=trajectory.frames;
    const uint32_t all_quantities=(1u<<CurvesGeometryQuantity_Count)-1u;
    if (!frames || trajectory.helices.empty() || trajectory.pair_axis.size()!=frames*trajectory.pairs ||
        trajectory.step_axis.size()!=frames*trajectory.steps ||
        trajectory.axis_quality.size()!=frames*trajectory.helices.size() ||
        trajectory.groove_quality.size()!=frames*trajectory.helices.size() ||
        trajectory.grooves.size()%frames || !options.quantities || (options.quantities&~all_quantities) ||
        !std::isfinite(options.sigma) || options.sigma<=0 ||
        !std::isfinite(options.hysteresis) || options.hysteresis<0 ||
        options.baseline_first>=options.baseline_end || options.baseline_end>frames ||
        options.first_frame>options.last_frame || options.last_frame>=frames || !options.min_frames)
        return fail(nullptr,"Invalid Curves+ geometry event configuration");
    if (!std::isfinite(options.min_duration) || options.min_duration<0 ||
        !std::isfinite(options.max_gap_duration) || options.max_gap_duration<0 ||
        (!options.times.empty() && options.times.size()!=frames))
        return fail(nullptr,"Invalid Curves+ geometry event time configuration");
    for (size_t f=0;f<options.times.size();++f)
        if (!std::isfinite(options.times[f]) || (f && options.times[f]<=options.times[f-1]))
            return fail(nullptr,"Curves+ geometry event times must increase strictly");
    const float continuation=std::max(0.5f,options.sigma-options.hysteresis);
    const size_t groove_members=trajectory.grooves.size()/frames;
    auto angle_quantity=[](int quantity) {
        return quantity==CurvesAxisInclination || quantity==CurvesAxisTip ||
            quantity==CurvesAxisBend || quantity==CurvesInterAxisTwist;
    };
    auto quantity_value=[](const CurvesTrajectoryAnalysis& data,size_t frame,size_t helix,
                           size_t member,int quantity,size_t groove_count) {
        if (quantity>=CurvesGrooveW12) {
            const auto& value=data.grooves[frame*groove_count+
                data.helices[helix].groove_offset+member];
            const float values[]={value.width12,value.depth12,value.width21,value.depth21};
            return values[quantity-CurvesGrooveW12];
        }
        if (quantity>=CurvesInterAxisRise) {
            const auto& value=data.step_axis[frame*data.steps+data.helices[helix].steps[member]];
            return quantity==CurvesInterAxisRise ? value.rise : value.twist;
        }
        const auto& value=data.pair_axis[frame*data.pairs+data.helices[helix].pairs[member]];
        const float values[]={value.x_displacement,value.y_displacement,value.inclination,value.tip,value.axis_bend};
        return values[quantity-CurvesAxisXDisplacement];
    };
    for (size_t h=0;h<trajectory.helices.size();++h) {
        if (cancel && cancel->load()) { out->clear(); return true; }
        const auto& helix=trajectory.helices[h];
        if (helix.pairs.size()<2 || helix.steps.size()+1!=helix.pairs.size() ||
            helix.groove_offset>groove_members || helix.groove_count>groove_members-helix.groove_offset)
            return fail(nullptr,"Invalid Curves+ helix layout for event detection");
        for (size_t pair : helix.pairs) if (pair>=trajectory.pairs)
            return fail(nullptr,"Curves+ event pair index is out of range");
        for (size_t step : helix.steps) if (step>=trajectory.steps)
            return fail(nullptr,"Curves+ event step index is out of range");
        for (int quantity=0;quantity<CurvesGeometryQuantity_Count;++quantity) {
            if (!(options.quantities&(1u<<quantity))) continue;
            const size_t members=quantity>=CurvesGrooveW12 ? helix.groove_count :
                quantity>=CurvesInterAxisRise ? helix.steps.size() : helix.pairs.size();
            for (size_t m=0;m<members;++m) {
                if (cancel && cancel->load()) { out->clear(); return true; }
                const bool groove=quantity>=CurvesGrooveW12;
                const bool step=quantity>=CurvesInterAxisRise && !groove;
                const std::string label=groove ? "sample_"+std::to_string(m+1) :
                    step ? (helix.step_labels.size()==helix.steps.size() ? helix.step_labels[m] :
                        "step_"+std::to_string(helix.pairs[m]+1)+"_"+std::to_string(helix.pairs[m+1]+1)) :
                    helix.pair_labels.size()==helix.pairs.size() ? helix.pair_labels[m] :
                        "bp_"+std::to_string(helix.pairs[m]+1);
                const float level=groove ? trajectory.grooves[helix.groove_offset+m].level :
                    std::numeric_limits<float>::quiet_NaN();
                std::vector<float> samples(frames,std::numeric_limits<float>::quiet_NaN());
                for (size_t f=0;f<frames;++f) {
                    const size_t quality=f*trajectory.helices.size()+h;
                    const uint16_t flags=quantity>=CurvesGrooveW12
                        ? trajectory.groove_quality[quality] : trajectory.axis_quality[quality];
                    if (flags==Quality_Good) samples[f]=quantity_value(trajectory,f,h,m,quantity,groove_members);
                }
                MemberStatistics reference;
                if (options.reference) {
                    if (!curves_geometry_reference_statistics(&reference,*options.reference,h+1,label,quantity,level))
                        return fail(nullptr,"Could not map Curves+ geometry event reference");
                } else {
                    std::vector<MemberStatistics> baseline;
                    if (!sequence_statistics(&baseline,samples,1,angle_quantity(quantity),
                            options.baseline_first,options.baseline_end))
                        return fail(nullptr,"Could not calculate Curves+ event baseline");
                    reference=baseline[0];
                }
                if (reference.count<2 || !std::isfinite(reference.sd) || reference.sd<=1e-8) continue;
                CurvesGeometryEvent active;
                bool started=false,strong=false;
                size_t pending_gap=0;
                auto finish=[&]() {
                    const double duration=options.times.empty() ? (double)(active.end-active.begin+1) :
                        options.times[active.end]-options.times[active.begin];
                    if (started && strong && active.outlier_frames>=options.min_frames &&
                        (options.min_duration==0 || duration>=options.min_duration)) out->push_back(active);
                    active={};
                    started=false;
                    strong=false;
                    pending_gap=0;
                };
                for (size_t f=options.first_frame;f<=options.last_frame;++f) {
                    if (cancel && cancel->load()) { out->clear(); return true; }
                    if (!std::isfinite(samples[f])) { finish(); continue; }
                    const float z=reference.zscore(samples[f]);
                    const float magnitude=std::fabs(z);
                    if (std::isfinite(z) && magnitude>=continuation) {
                        if (!started) {
                            started=true;
                            active.helix=h+1;
                            active.quantity=quantity;
                            active.member=label;
                            active.level=groove ? trajectory.grooves[helix.groove_offset+m].level :
                                std::numeric_limits<float>::quiet_NaN();
                            active.begin=f;
                        }
                        pending_gap=0;
                        active.end=f;
                        ++active.outlier_frames;
                        if (magnitude>=options.sigma) strong=true;
                        if (magnitude>std::fabs(active.peak_z)) {
                            active.peak=f;
                            active.peak_z=z;
                        }
                    } else if (started && (options.times.empty() ? pending_gap<options.max_gap :
                        options.times[f]-options.times[active.end]<=options.max_gap_duration)) {
                        ++pending_gap;
                    } else finish();
                }
                finish();
            }
        }
    }
    std::stable_sort(out->begin(),out->end(),[](const CurvesGeometryEvent& a,const CurvesGeometryEvent& b) {
        if (a.begin!=b.begin) return a.begin<b.begin;
        if (a.helix!=b.helix) return a.helix<b.helix;
        if (a.quantity!=b.quantity) return a.quantity<b.quantity;
        return a.member<b.member;
    });
    return true;
}

const char* csv_curves_geometry_event_header() {
    return "run,helix,quantity,member,level,begin_frame,end_frame,peak_frame,begin_time,end_time,peak_time,time_unit,outlier_frames,peak_signed_z\n";
}

std::string csv_curves_geometry_event_row(const std::string& run,const CurvesGeometryEvent& event,
                                          const float* times,const char* time_unit,size_t frame_count) {
    static const char* quantities[]={"axis_x_displacement","axis_y_displacement","axis_inclination",
        "axis_tip","axis_bend","inter_axis_rise","inter_axis_twist",
        "groove_W12","groove_D12","groove_W21","groove_D21"};
    if (run.empty() || !time_unit || !*time_unit || event.quantity<0 ||
        event.quantity>=CurvesGeometryQuantity_Count ||
        !event.helix || event.member.empty() || event.begin>event.end || event.peak<event.begin ||
        event.peak>event.end || !std::isfinite(event.peak_z)) {
        MD_LOG_ERROR("Lavery: invalid Curves+ geometry event CSV row");
        return {};
    }
    if (times && (event.end>=frame_count || !std::isfinite(times[event.begin]) ||
        !std::isfinite(times[event.end]) || !std::isfinite(times[event.peak]))) {
        MD_LOG_ERROR("Lavery: invalid Curves+ geometry event CSV time values");
        return {};
    }
    char values[256];
    snprintf(values,sizeof(values),",%zu,%s,",event.helix,quantities[event.quantity]);
    std::string row=csv_quote(run)+values+csv_quote(event.member)+",";
    if (std::isfinite(event.level)) {
        char level[64];
        snprintf(level,sizeof(level),"%.9g",event.level);
        row+=level;
    }
    snprintf(values,sizeof(values),",%zu,%zu,%zu,%.9g,%.9g,%.9g,",
        event.begin,event.end,event.peak,times ? (double)times[event.begin] : (double)event.begin,
        times ? (double)times[event.end] : (double)event.end,
        times ? (double)times[event.peak] : (double)event.peak);
    row+=values+csv_quote(time_unit);
    snprintf(values,sizeof(values),",%zu,%.9g\n",event.outlier_frames,event.peak_z);
    return row+values;
}

std::vector<int> match_reference_members(const std::vector<ReferenceMember>& source, const std::vector<std::string>& ids) {
    std::vector<int> mapping(ids.size(),-1);
    for (size_t m = 0; m < ids.size(); ++m) {
        if (ids[m].empty()) continue;
        size_t matches = 0;
        for (size_t r = 0; r < source.size(); ++r) if (ids[m]==source[r].identity) { mapping[m]=(int)r; ++matches; }
        if (matches!=1 || std::count(ids.begin(),ids.end(),ids[m])!=1) mapping[m]=-1;
    }
    return mapping;
}

std::string reference_mapping_text(const ReferenceMapping& mapping, const ExternalReference& source, const MemberLabels& labels) {
    std::string text;
    for (int cls = 0; cls < Class_Count; ++cls) {
        if (!labels[cls]) continue;
        for (size_t m = 0; m < mapping[cls].size() && m < labels[cls]->size(); ++m) {
            const int r = mapping[cls][m];
            text += std::to_string(cls)+"\t"+(*labels[cls])[m]+"\t"+
                (r>=0 && (size_t)r<source.members[cls].size() ? source.members[cls][r].label : std::string{})+"\n";
        }
    }
    return text;
}

bool apply_reference_mapping_text(ReferenceMapping* mapping, const ExternalReference& source, const MemberLabels& labels,
                                  const std::string& text) {
    if (!mapping) return false;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines,line)) {
        std::istringstream fields(line);
        std::string class_text,current,label;
        if (!std::getline(fields,class_text,'\t') || !std::getline(fields,current,'\t')) continue;
        std::getline(fields,label);
        for (int cls = 0; cls < Class_Count; ++cls) {
            if (class_text!=std::to_string(cls) || !labels[cls]) continue;
            auto& members = (*mapping)[cls];
            for (size_t m = 0; m < labels[cls]->size() && m < members.size(); ++m) {
                if ((*labels[cls])[m]!=current) continue;
                members[m] = -1;
                for (size_t r = 0; r < source.members[cls].size(); ++r)
                    if (source.members[cls][r].label==label) members[m]=(int)r;
            }
        }
    }
    bool unique = true;
    for (auto& members : *mapping) for (size_t m = 0; m < members.size(); ++m)
        if (members[m]>=0 && std::count(members.begin(),members.end(),members[m])>1) {
            const int duplicate = members[m];
            for (auto& r : members) if (r==duplicate) r=-1;
            unique = false;
        }
    return unique;
}

std::vector<float> pooled_external_samples(const ExternalReference& source, Metric metric) {
    std::vector<float> samples;
    for (const auto& member : source.members[metric_class(metric)]) {
        const auto& values=member.values[metric];
        samples.insert(samples.end(),values.begin(),values.end());
    }
    return samples;
}

const char* csv_sequence_profile_header() {
    return "run,class,parameter,unit,member,label,bp_position,source,begin_frame,end_frame,valid,missing,mean,sd,current_frame,current_value,reference_source,reference_valid,reference_mean,reference_sd\n";
}

std::string csv_sequence_profile_row(const std::string& run, Metric metric, size_t member, const std::string& label,
    double position, const std::string& source, size_t first, size_t end, const MemberStatistics& summary,
    size_t frame, float value, const std::string& reference_source, const MemberStatistics* reference,
    const std::string& quantity, const std::string& unit, bool included) {
    if (end<=first || summary.count>end-first) {
        MD_LOG_ERROR("Lavery: invalid sequence-profile CSV interval/count");
        return {};
    }
    char row[512];
    snprintf(row,sizeof(row),"%.17g,",position);
    std::string output=csv_quote(run)+","+class_csv_name(metric_class(metric))+","+
        (quantity.empty() ? metric_path(metric) : csv_quote(quantity))+","+
        (unit.empty() ? (metric_uses_degrees(metric) ? "deg" : "Angstrom") : csv_quote(unit))+","+std::to_string(member+1)+","+
        csv_quote(label)+","+row+csv_quote(source)+",";
    snprintf(row,sizeof(row),"%zu,%zu,",first,end-1);
    output+=row;
    if (included) {
        snprintf(row,sizeof(row),"%zu,%zu,%.17g,%.17g,",summary.count,end-first-summary.count,
            summary.count ? summary.mean : NAN,summary.count ? summary.sd : NAN);
        output+=row;
    } else output+=",,,,";
    if (frame==SIZE_MAX || !included) output+=",,";
    else {
        snprintf(row,sizeof(row),"%zu,%.17g,",frame,(double)value);
        output+=row;
    }
    if (reference && included) {
        snprintf(row,sizeof(row),",%zu,%.17g,%.17g\n",reference->count,
            reference->count ? reference->mean : NAN,reference->count ? reference->sd : NAN);
        output+=csv_quote(reference_source)+row;
    } else output+=",,,\n";
    return output;
}

const char* csv_comparison_header() {
    return "run,class,parameter,unit,distribution,source_filters,bin_lower,bin_upper,density,valid,missing,mean,sd,angular_origin\n";
}

std::string csv_comparison_group(const std::string& run, Metric metric, const std::string& name,
    const std::string& source_filters, const SampleComparison& comparison, size_t group, bool missing_known) {
    const auto& h=comparison.histogram;
    if (group>=h.valid.size() || group>=comparison.summaries.size() || group>=comparison.missing.size()) {
        MD_LOG_ERROR("Lavery: invalid comparison CSV group");
        return {};
    }
    const auto& summary=comparison.summaries[group];
    const std::string prefix=csv_quote(run)+","+class_csv_name(metric_class(metric))+","+metric_path(metric)+","+
        (metric_uses_degrees(metric) ? "deg" : "Angstrom")+","+csv_quote(name)+","+csv_quote(source_filters)+",";
    const std::string missing=missing_known ? std::to_string(comparison.missing[group]) : "";
    std::string output;
    const size_t bins=h.centers.size();
    for (size_t b=0;b<std::max((size_t)1,bins);++b) {
        char row[512];
        const double lower=bins ? h.centers[b]-h.bin_width*.5 : NAN;
        const double upper=bins ? h.centers[b]+h.bin_width*.5 : NAN;
        const double density=bins ? h.density[group*bins+b] : 0;
        snprintf(row,sizeof(row),"%.17g,%.17g,%.17g,%zu,%s,%.17g,%.17g,%.17g\n",
            lower,upper,density,h.valid[group],missing.c_str(),
            summary.count ? summary.mean : NAN,summary.count ? summary.sd : NAN,comparison.angular_origin);
        output+=prefix+row;
    }
    return output;
}

std::vector<float> external_samples(const ExternalReference& source, Metric metric, const std::vector<int>& mapping,
                                    size_t member, const std::vector<uint32_t>& groups) {
    std::vector<float> samples;
    if (member>=mapping.size() || mapping[member]<0 || (!groups.empty() && groups.size()!=mapping.size())) return samples;
    std::set<int> used;
    for (size_t m = 0; m < mapping.size(); ++m) {
        if (groups.empty() ? m!=member : groups[m]!=groups[member]) continue;
        const int r = mapping[m];
        const auto& members = source.members[metric_class(metric)];
        if (r<0 || (size_t)r>=members.size() || !used.insert(r).second) continue;
        const auto& v = members[r].values[metric];
        samples.insert(samples.end(),v.begin(),v.end());
    }
    return samples;
}

bool external_statistics(MetricStatistics* out, const ExternalReference& source, const std::vector<int>& mapping,
                         const std::vector<float>& current, Metric metric, Baseline baseline, const std::vector<uint32_t>& groups) {
    if (out) *out = {};
    if (!out || metric>=Metric_Count || mapping.empty() || current.size()%mapping.size() || (!groups.empty() && groups.size()!=mapping.size()))
        return fail(nullptr,"Invalid mapped external reference dimensions");
    for (int r : mapping) if (r < -1 || (r>=0 && (size_t)r>=source.members[metric_class(metric)].size()))
        return fail(nullptr,"External reference mapping points outside the imported members");
    *out = {};
    out->members.resize(mapping.size());
    const size_t frames = current.size()/mapping.size();
    out->heatmap.assign(current.size(),NAN);
    std::map<size_t,MemberStatistics> cached;
    for (size_t m = 0; m < mapping.size(); ++m) {
        const size_t key = groups.empty() ? m : groups[m];
        if (mapping[m]<0) continue;
        if (!cached.count(key)) {
            const auto samples = external_samples(source,metric,mapping,m,groups);
            MetricStatistics stats;
            if (!compute_statistics(&stats,samples,1,metric_is_angular(metric),baseline,
                metric_state_count(metric)?(int)metric:-1)) return false;
            cached[key] = stats.members[0];
        }
        out->members[m] = cached[key];
        for (size_t f = 0; f < frames; ++f)
            out->heatmap[(mapping.size()-1-m)*frames+f] = out->members[m].zscore(current[f*mapping.size()+m]);
    }
    return true;
}

void PeriodSummary::add(float value,float z,int state) {
    if (!std::isfinite(value)) { ++missing; return; }
    ++valid;
    if (state>=0 && state<(int)states.size()) ++states[state];
    if (std::isfinite(z)) { ++referenced; z_sum+=z; z_square_sum+=(double)z*z; }
}

double PeriodSummary::mean_z() const { return referenced ? z_sum/referenced : NAN; }
double PeriodSummary::rms_z() const { return referenced ? std::sqrt(z_square_sum/referenced) : NAN; }

bool add_frame_features(std::vector<FrameFeature>* features, int cls, uint32_t mask, const std::vector<size_t>& members) {
    if (!features || cls<0 || cls>=Class_Count || (mask & ~class_all_parameters(cls)))
        return fail(nullptr,"Invalid representative feature class or parameter mask");
    std::set<std::pair<Metric,size_t>> used;
    for (const auto& f : *features) used.emplace(f.metric,f.member);
    for (size_t m : members) for (int k=0; k<class_parameter_count(cls); ++k) if (mask & (1u<<k)) {
        const auto metric=(Metric)(class_first_metric(cls)+k);
        if (used.emplace(metric,m).second) features->push_back({metric,m});
    }
    return true;
}

bool representative_frames(std::vector<FrameCandidate>* out, const std::array<std::vector<float>, Metric_Count>& values,
    const std::array<MetricStatistics, Metric_Count>& statistics, const std::array<size_t, Metric_Count>& columns,
    const std::vector<FrameFeature>& features, size_t first, size_t end, size_t count, double separation,
    const std::vector<double>& times, std::string* error, double coverage, FrameRankingDiagnostics* diagnostics) {
    if (!out) return fail(error,"Missing frame-ranking output");
    if (error) error->clear();
    out->clear();
    if (diagnostics) *diagnostics={};
    if (features.empty() || first>=end || !count || !std::isfinite(separation) || separation<0 ||
        !std::isfinite(coverage) || coverage<=0 || coverage>1)
        return fail(error,"Choose features, a valid search interval, positive frame count and nonnegative separation");
    if (!times.empty()) {
        if (times.size()<end) return fail(error,"Frame-ranking timestamps do not cover the search interval");
        for (size_t f = 0; f < times.size(); ++f)
            if (!std::isfinite(times[f]) || (f && times[f]<=times[f-1])) return fail(error,"Frame-ranking timestamps must increase");
    }
    std::set<std::pair<Metric,size_t>> unique;
    for (const auto& feature : features) {
        const auto id = feature.metric;
        if (id>=Metric_Count || !columns[id] || feature.member>=columns[id] ||
            statistics[id].members.size()!=columns[id] || values[id].size()%columns[id] ||
            end>values[id].size()/columns[id] || !unique.emplace(id,feature.member).second)
            return fail(error,"Invalid or duplicate representative-frame feature");
    }
    std::vector<FrameCandidate> ranked;
    const size_t required=std::max(size_t(1),(size_t)std::ceil(coverage*features.size()-1e-9));
    if (diagnostics) {
        diagnostics->frames=end-first;
        diagnostics->missing_values.resize(features.size());
        diagnostics->missing_references.resize(features.size());
        diagnostics->invalid_scales.resize(features.size());
    }
    for (size_t f = first; f < end; ++f) {
        double sum = 0;
        size_t used=0;
        std::string omitted;
        for (size_t i=0;i<features.size();++i) {
            const auto& feature=features[i];
            const auto id = feature.metric;
            const auto& stats = statistics[id].members[feature.member];
            const float v = values[id][f*columns[id]+feature.member];
            const auto ref = stats.reference(v);
            const double z = stats.zscore(v);
            const char* reason=nullptr;
            if (!std::isfinite(v)) {
                reason="missing_value";
                if (diagnostics) ++diagnostics->missing_values[i];
            } else if (!ref.count) {
                reason="missing_reference";
                if (diagnostics) ++diagnostics->missing_references[i];
            } else if (!std::isfinite(ref.sd) || ref.sd<=0 || !std::isfinite(z)) {
                reason="invalid_scale";
                if (diagnostics) ++diagnostics->invalid_scales[i];
            }
            if (reason) {
                if (!omitted.empty()) omitted+="; ";
                omitted+=std::string(metric_path(id))+":"+std::to_string(feature.member+1)+"="+reason;
                continue;
            }
            sum += z*z;
            ++used;
        }
        if (diagnostics && used==features.size()) ++diagnostics->strict;
        if (used>=required && std::isfinite(sum)) {
            ranked.push_back({f,std::sqrt(sum/used),used,features.size(),std::move(omitted)});
            if (diagnostics) ++diagnostics->comparable;
        }
    }
    if (ranked.empty()) return fail(error,"No comparable frames at the required feature coverage. Inspect missing values, references and nonzero scales.");
    std::sort(ranked.begin(),ranked.end(),[](const auto& a,const auto& b) { return a.score==b.score ? a.frame<b.frame : a.score<b.score; });
    std::set<double> selected;
    for (const auto& candidate : ranked) {
        const double t = times.empty() ? (double)candidate.frame : times[candidate.frame];
        const auto next = selected.lower_bound(t);
        if (next!=selected.end() && *next-t<separation) continue;
        if (next!=selected.begin() && t-*std::prev(next)<separation) continue;
        selected.insert(t);
        out->push_back(candidate);
        if (out->size()==count) break;
    }
    return true;
}

bool export_representative_frames(const md_system_t& sys, const std::string& run, const std::string& xtc_path,
    const std::vector<FrameCandidate>& candidates, const std::string& provenance, std::string* error) {
    if (error) error->clear();
    if (candidates.empty() || !sys.atom.count || sys.atom.count>INT_MAX) return fail(error,"No frames/atoms to export, or atom count exceeds XTC limits");
    for (size_t a=0; a<sys.atom.count; ++a) {
        const auto c=md_system_residue_find_by_atom_idx(&sys,a);
        if (c<0 || md_atom_name(&sys.atom,a).len>5 || md_system_residue_name(&sys,c).len>5)
            return fail(error,"Matching GRO requires residue membership and atom/residue names of at most five characters");
    }
    std::filesystem::path path(xtc_path);
    const auto gro_path = path.replace_extension(".gro");
    const auto csv_path = path.replace_extension(".csv");
    const auto xtc = std::filesystem::path(xtc_path);
    if (xtc.extension()!=".xtc") return fail(error,"Choose an output filename ending in .xtc");
    std::error_code ec;
    for (const auto& file : {xtc,gro_path,csv_path}) {
        const bool exists = std::filesystem::exists(file,ec);
        if (ec) return fail(error,"Cannot inspect output path: "+file.string()+": "+ec.message());
        if (exists) return fail(error,"Output already exists; choose a new filename: "+file.string());
    }
    const auto* time_attr = md_attributes_find(&sys.attributes,md_str_from_cstr((run+"/time").c_str()));
    if (!time_attr || !time_attr->data || time_attr->format.type!=MD_ATTRIBUTE_TYPE_F64 ||
        !md_unit_base_equal(time_attr->unit,md_unit_second())) return fail(error,"XTC export requires physical F64 trajectory timestamps");
    const size_t frame_count = time_attr->format.shape[0];
    const double* times = (const double*)time_attr->data;
    auto frames = candidates;
    std::sort(frames.begin(),frames.end(),[](const auto& a,const auto& b) { return a.frame<b.frame; });
    for (size_t i = 0; i < frames.size(); ++i)
        if (frames[i].frame>=frame_count || frames[i].frame>INT_MAX || !std::isfinite(frames[i].score) ||
            frames[i].used_features>frames[i].total_features ||
            (frames[i].total_features && !frames[i].used_features) ||
            (i && frames[i-1].frame==frames[i].frame)) return fail(error,"Invalid or duplicate export frame");
    const double to_ps = md_unit_scale(time_attr->unit)/md_unit_scale(md_unit_picosecond());
    float previous = -std::numeric_limits<float>::infinity();
    for (const auto& frame : frames) {
        const float t = (float)(times[frame.frame]*to_ps);
        if (!std::isfinite(t) || t<=previous) return fail(error,"Selected times are nonfinite or not distinct at XTC float precision");
        previous=t;
    }
    md_system_state_t state = {.alloc=md_get_heap_allocator()};
    if (!md_system_state_init(&state,sys.atom.count)) return fail(error,"Cannot allocate representative-frame coordinates");
    const md_str_t paths[] = {MD_STR_LIT("atom/position"),MD_STR_LIT("unitcell")};
    auto* extract = md_system_extract_begin(&sys,md_str_from_cstr(run.c_str()),paths,2,md_get_heap_allocator());
    if (!extract) { md_system_state_free(&state); return fail(error,"Could not begin representative-frame coordinate extraction"); }
    XDRFILE* output = xdrfile_open(xtc_path.c_str(),"w");
    if (!output) { md_system_extract_end(extract); md_system_state_free(&state); return fail(error,"Cannot create XTC: "+xtc_path); }
    std::ofstream gro(gro_path), csv(csv_path);
    bool ok = gro.good() && csv.good();
    std::vector<std::array<float,3>> xyz(sys.atom.count);
    csv << "output_frame,original_frame,time_ps,score,rank,reference,used_features,total_features,coverage,omitted_features\n";
    for (size_t n = 0; n < frames.size() && ok; ++n) {
        const auto f = frames[n].frame;
        ok = md_system_extract_frame(extract,(int64_t)f,&state);
        if (!ok || state.num_atoms!=sys.atom.count) { ok=false; break; }
        matrix box = {};
        md_unitcell_A_extract_float(box,&state.unitcell);
        for (auto& row : box) for (float& v : row) { v *= .1f; if (!std::isfinite(v)) ok=false; }
        for (size_t a = 0; a < sys.atom.count; ++a) {
            const auto v = md_state_coord(&state,a);
            xyz[a] = {v.x*.1f,v.y*.1f,v.z*.1f};
            for (float x : xyz[a]) if (!std::isfinite(x)) ok=false;
            if (!n) for (float x : xyz[a]) if (x<=-1000 || x>=10000) ok=false;
        }
        const double time = times[f]*to_ps;
        if (!ok || !std::isfinite(time) || std::fabs(time)>std::numeric_limits<float>::max()) { ok=false; break; }
        ok = write_xtc(output,(int)sys.atom.count,(int)f,(float)time,box,
            reinterpret_cast<rvec*>(xyz.data()),1000.f)==exdrOK;
        const auto rank = std::find_if(candidates.begin(),candidates.end(),[&](const auto& c) { return c.frame==f; })-candidates.begin()+1;
        csv.precision(17);
        csv << n << ',' << f << ',' << time << ',' << frames[n].score << ',' << rank << ',' << csv_quote(provenance) << ',';
        if (frames[n].total_features)
            csv << frames[n].used_features << ',' << frames[n].total_features << ','
                << (double)frames[n].used_features/frames[n].total_features;
        else csv << ",,";
        csv << ',' << csv_quote(frames[n].omitted_features) << '\n';
        if (!n) {
            gro << "Lavery representative frame " << f << ", t= " << time << " ps\n" << sys.atom.count << '\n';
            for (size_t a = 0; a < sys.atom.count; ++a) {
                const auto component = md_system_residue_find_by_atom_idx(&sys,a);
                if (component<0) { ok=false; break; }
                const md_str_t res = md_system_residue_name(&sys,component), atom = md_atom_name(&sys.atom,a);
                char line[256];
                // Five decimals retain the decoded coordinates while remaining valid variable-precision GRO.
                const int len = snprintf(line,sizeof(line),"%5d%-5.*s%5.*s%5zu%10.5f%10.5f%10.5f\n",
                    (int)(md_system_residue_seq_id(&sys,component)%100000), (int)std::min(size_t(5),res.len),res.ptr,
                    (int)std::min(size_t(5),atom.len),atom.ptr,(a+1)%100000,xyz[a][0],xyz[a][1],xyz[a][2]);
                if (len<0 || len>=(int)sizeof(line)) { ok=false; break; }
                gro << line;
            }
            gro.precision(9);
            gro << box[0][0] << ' ' << box[1][1] << ' ' << box[2][2] << ' ' << box[0][1] << ' ' << box[0][2] << ' '
                << box[1][0] << ' ' << box[1][2] << ' ' << box[2][0] << ' ' << box[2][1] << '\n';
        }
        ok &= gro.good() && csv.good();
    }
    if (xdrfile_close(output)!=exdrOK) ok=false;
    gro.close(); csv.close();
    ok &= !gro.fail() && !csv.fail();
    md_system_state_free(&state);
    md_system_extract_end(extract);
    if (!ok) {
        for (const auto& file : {xtc,gro_path,csv_path}) {
            std::filesystem::remove(file,ec);
            if (ec) MD_LOG_ERROR("Lavery: could not remove partial export '%s': %s",file.string().c_str(),ec.message().c_str());
        }
        return fail(error,"Representative-frame export failed; partial outputs were removed where possible");
    }
    return true;
}

} // namespace lavery
