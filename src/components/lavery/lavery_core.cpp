#include "lavery_core.h"
#include <md_system.h>
#include <md_util.h>
#include <core/md_log.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <iterator>
#include <vector>
#include <cstdio>

namespace lavery {
namespace {

constexpr float RAD_TO_DEG = 57.29577951308232f;
constexpr float MIN_COS_PAIR_NORMAL = 0.75f;
constexpr float MIN_PAIR_DISTANCE = 3.0f;
constexpr float MAX_PAIR_DISTANCE = 9.0f;

struct Candidate {
    uint32_t a, b;
    float score;
};

struct ReferenceBase {
    size_t count;
    const char* names[10];
    vec3_t xyz[10];
};

// Numerical standard geometries from Curves+ v3.0 standard_b.lib.
// U uses the same ring/sugar coordinates as T. See README for provenance.
static const ReferenceBase references[] = {
    {10, {"C1'", "N9", "C4", "C8", "N1", "C2", "N3", "C5", "C6", "N7"},
     {{1.57340f,-2.41044f,-0.12190f}, {0.37786f,-1.52945f,-0.00531f},
      {0.38110f,-0.16006f,-0.04011f}, {-0.94428f,-1.87750f,0.15888f},
      {-0.11123f,2.43959f,-0.04964f}, {1.10298f,1.90641f,-0.17768f},
      {1.45731f,0.64079f,-0.18694f}, {-0.91754f,0.23706f,0.10110f},
      {-1.16848f,1.61858f,0.09523f}, {-1.75109f,-0.86494f,0.22638f}}},
    {10, {"C1'", "N9", "C4", "C8", "N1", "C2", "N3", "C5", "C6", "N7"},
     {{1.58195f,-2.39594f,-0.12320f}, {0.37714f,-1.52785f,-0.00520f},
      {0.37551f,-0.14975f,-0.04020f}, {-0.94239f,-1.88689f,0.15880f},
      {-0.10165f,2.41153f,-0.05020f}, {1.18367f,1.92662f,-0.18720f},
      {1.47916f,0.62851f,-0.18920f}, {-0.93480f,0.24071f,0.10280f},
      {-1.25149f,1.62390f,0.10480f}, {-1.76708f,-0.87080f,0.22880f}}},
    {7, {"C1'", "N1", "C2", "C6", "N3", "C4", "C5"},
     {{1.94866f,-1.45161f,-0.19029f}, {0.74385f,-0.58352f,-0.07229f},
      {0.93020f,0.79520f,-0.12929f}, {-0.49500f,-1.12092f,0.08671f},
      {-0.15547f,1.60399f,-0.02329f}, {-1.37969f,1.08626f,0.13371f},
      {-1.59254f,-0.32937f,0.19471f}}},
    {7, {"C1'", "N1", "C2", "C6", "N3", "C4", "C5"},
     {{1.95363f,-1.45659f,-0.19073f}, {0.75787f,-0.57587f,-0.07419f},
      {0.97215f,0.78019f,-0.13405f}, {-0.49400f,-1.10811f,0.08633f},
      {-0.15841f,1.56515f,-0.02137f}, {-1.45350f,1.11665f,0.14102f},
      {-1.57773f,-0.32139f,0.19302f}}},
};

static vec3_t normalize_or_zero(vec3_t v) {
    const float len = vec3_length(v);
    return len > 1.0e-6f ? vec3_mul1(v, 1.0f / len) : vec3_zero();
}

static float determinant(const BaseFrame& f) {
    return vec3_dot(f.x, vec3_cross(f.y, f.z));
}

static bool valid_frame(const BaseFrame& f) {
    return std::isfinite(f.origin.x) && std::isfinite(f.origin.y) && std::isfinite(f.origin.z) &&
           std::fabs(vec3_length(f.x) - 1.0f) < 1.0e-3f &&
           std::fabs(vec3_length(f.y) - 1.0f) < 1.0e-3f &&
           std::fabs(vec3_length(f.z) - 1.0f) < 1.0e-3f &&
           std::fabs(determinant(f) - 1.0f) < 1.0e-3f;
}

static float pairing_score(const BaseFrame& a, const BaseFrame& b, const md_unitcell_t* cell) {
    if (!valid_frame(a) || !valid_frame(b)) return std::numeric_limits<float>::infinity();
    const float normal_alignment = std::fabs(vec3_dot(a.z, b.z));
    if (normal_alignment < MIN_COS_PAIR_NORMAL) return std::numeric_limits<float>::infinity();
    vec3_t delta = vec3_sub(b.center, a.center);
    if (cell) md_util_min_image_vec3(&delta, 1, cell);
    const float distance = vec3_length(delta);
    const float plane_offset = std::max(std::fabs(vec3_dot(delta, a.z)), std::fabs(vec3_dot(delta, b.z)));
    if (!std::isfinite(distance) || distance < MIN_PAIR_DISTANCE || distance > MAX_PAIR_DISTANCE ||
        plane_offset > 2.5f) return std::numeric_limits<float>::infinity();
    return distance + 2.0f * plane_offset + 3.0f * (1.0f - normal_alignment);
}

static bool rigid_parameters(BasePairFrame* out, float translation[3], float rotation[3],
                             const BaseFrame& a, const BaseFrame& b) {
    if (!valid_frame(a) || !valid_frame(b)) return false;
    const vec3_t skew = vec3_mul1(vec3_add(vec3_add(vec3_cross(a.x, b.x),
        vec3_cross(a.y, b.y)), vec3_cross(a.z, b.z)), 0.5f);
    const float sine = vec3_length(skew);
    const float cosine = std::clamp((vec3_dot(a.x, b.x) + vec3_dot(a.y, b.y) +
                                    vec3_dot(a.z, b.z) - 1.0f) * 0.5f, -1.0f, 1.0f);
    const float angle = std::atan2(sine, cosine);
    // At pi the two shortest rotations have equally valid opposite axes.
    if (sine < 1.0e-6f && cosine < 0.0f) return false;
    const vec3_t axis = sine > 0.0f ? vec3_mul1(skew, 1.0f / sine) : vec3_t{1, 0, 0};
    const quat_t half = quat_axis_angle(axis, angle * 0.5f);
    out->origin = vec3_mul1(vec3_add(a.origin, b.origin), 0.5f);
    out->x = quat_mul_vec3(half, a.x);
    out->y = quat_mul_vec3(half, a.y);
    out->z = quat_mul_vec3(half, a.z);
    const vec3_t delta = vec3_sub(b.origin, a.origin);
    const vec3_t turn = vec3_mul1(axis, angle * RAD_TO_DEG);
    const vec3_t directions[] = {out->x, out->y, out->z};
    for (int k = 0; k < 3; ++k) {
        translation[k] = vec3_dot(delta, directions[k]);
        rotation[k] = vec3_dot(turn, directions[k]);
    }
    return true;
}

} // namespace

uint16_t pair_quality(const BaseFrame& a, const BaseFrame& b, const md_unitcell_t* cell) {
    if (!valid_frame(a) || !valid_frame(b)) return Quality_InvalidBase;
    vec3_t delta = vec3_sub(b.center, a.center);
    if (cell) md_util_min_image_vec3(&delta, 1, cell);
    const float distance = vec3_length(delta);
    uint16_t flags = 0;
    if (!std::isfinite(distance) || distance < MIN_PAIR_DISTANCE || distance > MAX_PAIR_DISTANCE) flags |= Quality_Separation;
    if (std::fabs(vec3_dot(a.z, b.z)) < MIN_COS_PAIR_NORMAL) flags |= Quality_Alignment;
    if (std::max(std::fabs(vec3_dot(delta, a.z)), std::fabs(vec3_dot(delta, b.z))) > 2.5f) flags |= Quality_PlaneOffset;
    return flags;
}

const char* quality_description(uint16_t flag) {
    switch (flag) {
    case Quality_InvalidBase: return "Missing/unsupported base fit, nonfinite coordinates or degenerate base frame";
    case Quality_Separation: return "Ring separation outside 3-9 Angstrom";
    case Quality_Alignment: return "Absolute base-normal alignment below 0.75";
    case Quality_PlaneOffset: return "Out-of-plane displacement above 2.5 Angstrom";
    case Quality_AmbiguousTransform: return "Invalid or ambiguous relative transform";
    case Quality_AdjacentPair: return "An adjacent pair has no calculable reference frame";
    case Quality_UnsupportedBase: return "Unsupported canonical-base reference";
    case Quality_NonfiniteCoordinates: return "Nonfinite atomic coordinates";
    case Quality_DegenerateBase: return "Degenerate or invalid fitted base frame";
    case Quality_MissingAtoms: return "Required reference-fit or base-ring atoms are missing";
    case Quality_NoPairContacts: return "No canonical H-bond remains, or fewer than two canonical heavy-atom contacts without hydrogens";
    case Quality_NoncanonicalPair: return "Not a supported A-T/U or G-C pair, or canonical contact atoms are missing";
    case Quality_DegenerateNucleotide: return "Undefined nucleotide geometry (collinear torsion or zero-amplitude pucker phase)";
    case Quality_UnusualBond: return "Warning: defining heavy-atom bond outside 1.0-2.1 Angstrom; finite measurement retained";
    case Quality_PoorPuckerFit: return "Warning: sugar ring Fourier-fit RMS residual above 5 degrees; finite measurement retained";
    default: return "Good";
    }
}

OutlierAssessment assess_outliers(const float* values, const MemberStatistics* statistics,
                                  size_t count, uint32_t selected, float sigma) {
    OutlierAssessment out;
    for (size_t i = 0; i < count && i < 32; ++i) {
        if (!(selected & (1u << i)) || !statistics[i].count || !std::isfinite(values[i])) continue;
        ++out.valid;
        const float z = statistics[i].zscore(values[i]);
        if (out.peak_metric < 0 || std::fabs(z) > std::fabs(out.peak_z)) {
            out.peak_z = z;
            out.peak_metric = (int)i;
        }
        if (statistics[i].is_outlier(values[i], sigma)) out.contributors |= 1u << i;
    }
    return out;
}

static double wrap_degrees(double delta) {
    return delta - 360.0 * std::floor((delta + 180.0) / 360.0);
}

MemberStatistics::Reference MemberStatistics::reference(float value) const {
    if (state_metric >= 0) {
        const int s = metric_state((Metric)state_metric, value);
        if (s >= 0 && (size_t)s < state_references.size()) return state_references[s];
        return {};
    }
    return {mean, sd, count};
}

double MemberStatistics::deviation(float value) const {
    const double delta = (double)value - reference(value).mean;
    return angular ? wrap_degrees(delta) : delta;
}

float MemberStatistics::zscore(float value) const {
    const auto ref = reference(value);
    if (!ref.count || !std::isfinite(value)) return std::numeric_limits<float>::quiet_NaN();
    return ref.sd > 0.0 ? (float)(deviation(value) / ref.sd) : 0.0f;
}

bool compute_present_base_pair(Values* out, BasePairFrame* pair_frame, uint16_t* quality,
                               const BaseFrame& a, const BaseFrame& b, const md_unitcell_t* cell) {
    if (!out || !pair_frame || !quality) {
        MD_LOG_ERROR("Lavery: invalid output for paired-frame evaluation");
        return false;
    }
    for (float& value : out->metric) value = std::numeric_limits<float>::quiet_NaN();
    *pair_frame = {};
    *quality = pair_quality(a, b, cell);
    if (*quality != Quality_Good) return false;
    BaseFrame partner = b;
    vec3_t delta = vec3_sub(b.origin, a.origin);
    if (cell) md_util_min_image_vec3(&delta, 1, cell);
    partner.origin = vec3_add(a.origin, delta);
    if (!compute_base_pair(out, pair_frame, a, partner)) {
        *quality |= Quality_AmbiguousTransform;
        *pair_frame = {};
        return false;
    }
    return true;
}

bool MemberStatistics::is_outlier(float value, float sigma) const {
    const auto ref = reference(value);
    return ref.count && ref.sd > 0.0 && std::isfinite(value) &&
        std::fabs(deviation(value)) > (double)sigma * ref.sd;
}

const char* baseline_name(Baseline baseline) {
    switch (baseline) {
    case Baseline_MeanSD: return "Mean / SD";
    case Baseline_MedianMAD: return "Median / MAD";
    default: return "Unknown";
    }
}

static double median_of(std::vector<double>& v) {
    const size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    double m = v[mid];
    if (v.size() % 2 == 0) m = 0.5 * (m + *std::max_element(v.begin(), v.begin() + mid));
    return m;
}

bool compute_statistics(MetricStatistics* out, const std::vector<float>& values, size_t member_count,
                        bool angular, Baseline baseline, int state_metric) {
    if (!out || (member_count == 0 ? !values.empty() : values.size() % member_count != 0) ||
        baseline < 0 || baseline >= Baseline_Count || state_metric >= (int)Metric_Count) {
        MD_LOG_ERROR("Lavery: invalid metric matrix dimensions for statistics");
        return false;
    }
    *out = {};
    out->members.resize(member_count);
    if (!member_count) return true;
    const size_t frames = values.size() / member_count;
    out->heatmap.resize(values.size(), std::numeric_limits<float>::quiet_NaN());
    std::vector<double> samples;
    samples.reserve(frames);
    const Metric state_kind = (Metric)std::max(0, state_metric);
    const int states = state_metric >= 0 ? metric_state_count(state_kind) : 0;
    for (size_t m = 0; m < member_count; ++m) {
        auto& stats = out->members[m];
        stats.angular = angular;
        samples.clear();
        if (states > 0) {
            std::array<std::vector<float>, 3> state_samples;
            for (size_t f = 0; f < frames; ++f) {
                const float value = values[f * member_count + m];
                const int st = metric_state(state_kind, value);
                if (st >= 0) state_samples[st].push_back(value);
            }
            size_t dominant_count = 0;
            for (int s = 0; s < states; ++s) {
                MetricStatistics local;
                if (!compute_statistics(&local, state_samples[s], 1, angular, baseline)) return false;
                const auto& ref = local.members[0];
                stats.state_references[s] = {ref.mean, ref.sd, ref.count};
                stats.count += ref.count;
                if (ref.count > dominant_count) {
                    dominant_count = ref.count;
                    stats.state = s;
                    stats.mean = ref.mean;
                    stats.sd = ref.sd;
                }
            }
            stats.state_metric = state_metric;
            if (stats.count) stats.state_fraction = (float)dominant_count / (float)stats.count;
            for (size_t f = 0; f < frames; ++f)
                out->heatmap[(member_count - 1 - m) * frames + f] = stats.zscore(values[f * member_count + m]);
            continue;
        }
        auto in_baseline = [&](float v) {
            return std::isfinite(v);
        };
        double reference = 0.0;
        if (angular) {
            double s = 0.0, c = 0.0;
            for (size_t f = 0; f < frames; ++f) {
                const float v = values[f * member_count + m];
                if (!in_baseline(v)) continue;
                s += std::sin((double)v * (3.14159265358979323846 / 180.0));
                c += std::cos((double)v * (3.14159265358979323846 / 180.0));
            }
            if (s != 0.0 || c != 0.0) reference = std::atan2(s, c) * (180.0 / 3.14159265358979323846);
        }
        double m2 = 0.0;
        for (size_t f = 0; f < frames; ++f) {
            const float raw = values[f * member_count + m];
            if (!in_baseline(raw)) continue;
            double v = raw;
            // Unwrap to within half a turn of the circular mean; a no-op unless the series crosses +/-180.
            if (angular && std::fabs(v - reference) >= 180.0) v = reference + wrap_degrees(v - reference);
            samples.push_back(v);
            ++stats.count;
            const double delta = v - stats.mean;
            stats.mean += delta / (double)stats.count;
            m2 += delta * (v - stats.mean);
        }
        if (stats.count) stats.sd = std::sqrt(std::max(0.0, m2 / (double)stats.count));
        if (baseline == Baseline_MedianMAD && stats.count) {
            const double median = median_of(samples);
            for (double& v : samples) v = std::fabs(v - median);
            const double mad = 1.4826 * median_of(samples);
            stats.mean = median;
            if (mad > 0.0) stats.sd = mad;
        }
        for (size_t f = 0; f < frames; ++f) {
            out->heatmap[(member_count - 1 - m) * frames + f] = stats.zscore(values[f * member_count + m]);
        }
    }
    return true;
}

std::string csv_quote(const std::string& text) {
    std::string out = "\"";
    for (char c : text) { out += c; if (c == '"') out += '"'; }
    return out + '"';
}

bool detect_state_switches(std::vector<StateSwitch>* out, const std::vector<float>& values,
                           size_t members, Metric metric, const EventOptions& options) {
    if (!out || !metric_state_count(metric) || !members || values.size() % members ||
        !std::isfinite(options.min_duration) || options.min_duration < 0) {
        MD_LOG_ERROR("Lavery: invalid state-switch inputs");
        return false;
    }
    out->clear();
    const size_t frames = values.size() / members;
    const auto& times = options.times;
    if (!times.empty()) {
        if (times.size() != frames) {
            MD_LOG_ERROR("Lavery: state-switch timestamp count does not match values");
            return false;
        }
        for (size_t f = 0; f < frames; ++f) if (!std::isfinite(times[f]) || (f && times[f] <= times[f-1])) {
            MD_LOG_ERROR("Lavery: state-switch timestamps must be finite and strictly increasing");
            return false;
        }
    }
    if (!frames) return true;
    const size_t last = std::min(options.last_frame, frames-1);
    if (options.first_frame > last) {
        MD_LOG_ERROR("Lavery: invalid state-switch detection range");
        return false;
    }
    constexpr double rad = 3.14159265358979323846 / 180;
    for (size_t m = 0; m < members; ++m) {
        int source = -1;
        double source_mean = 0;
        size_t source_begin = 0, source_end = 0;
        for (size_t first = options.first_frame; first <= last;) {
            const int state = metric_state(metric, values[first*members+m]);
            if (state < 0) { source = -1; ++first; continue; }
            size_t end = first;
            double sine = 0, cosine = 0;
            while (end <= last && metric_state(metric, values[end*members+m]) == state) {
                const double angle = values[end*members+m]*rad;
                sine += std::sin(angle);
                cosine += std::cos(angle);
                ++end;
            }
            const size_t stop = end-1;
            const bool qualified = times.empty() ? end-first >= std::max(size_t(1), options.min_frames) :
                (options.min_duration == 0 || (stop > first && times[stop]-times[first] >= options.min_duration));
            if (qualified) {
                const double mean = std::atan2(sine,cosine)/rad;
                if (source >= 0 && source != state) {
                    out->push_back({metric, (uint32_t)m, first, stop, source, state, (float)wrap_degrees(mean-source_mean),
                        source_begin, source_end, source_begin == options.first_frame, stop == last, stop == frames-1});
                }
                source = state;
                source_mean = mean;
                source_begin = first;
                source_end = stop;
            }
            first = end;
        }
    }
    std::stable_sort(out->begin(), out->end(), [](const StateSwitch& a, const StateSwitch& b) {
        return a.begin < b.begin;
    });
    return true;
}

const char* csv_state_switch_header() {
    return "run,class,member,label,parameter,begin_frame,end_frame,begin_time,end_time,time_unit,from_state,to_state,angular_change_deg,residence,min_residence,source_begin_frame,source_end_frame,intervening_samples,source_clipped,destination_clipped,destination_at_trajectory_end,source_begin_time,source_end_time\n";
}

std::string csv_state_switch_row(const std::string& run, const std::string& label, const StateSwitch& change,
                                 double begin, double end, const std::string& time_unit, double min_residence,
                                 double source_begin, double source_end) {
    char buf[512];
    snprintf(buf, sizeof(buf), ",%s,%u,", class_csv_name(metric_class(change.metric)), change.member+1);
    std::string row = csv_quote(run)+buf+csv_quote(label);
    snprintf(buf, sizeof(buf), ",%s,%zu,%zu,%.17g,%.17g,", metric_path(change.metric), change.begin, change.end, begin,end);
    row += buf + csv_quote(time_unit) + "," + csv_quote(metric_state_name(change.metric,change.from)) + "," +
        csv_quote(metric_state_name(change.metric,change.to));
    const double residence = time_unit == "frame" ? (double)(change.end-change.begin+1) : end-begin;
    snprintf(buf, sizeof(buf), ",%.9g,%.17g,%.17g,%zu,%zu,%zu,%d,%d,%d,%.17g,%.17g\n", change.angular_change, residence, min_residence,
        change.source_begin, change.source_end, change.intervening_samples(), change.source_clipped, change.destination_clipped,
        change.destination_at_trajectory_end, source_begin, source_end);
    return row+buf;
}

const char* csv_header() {
    return "run,frame,time,time_unit,class,member,label,pair_a,pair_b,parameter,unit,value,valid,quality_flags,quality_reason,mean,sd,zscore,sigma,parameter_selected,parameter_outlier,member_outlier,selected_mask,baseline\n";
}

std::string csv_sample(const CsvSample& s) {
    char buf[1024];
    snprintf(buf, sizeof(buf), ",%zu,%.17g,", s.frame, s.time);
    std::string row = csv_quote(s.run) + buf + csv_quote(s.time_unit);
    snprintf(buf, sizeof(buf), ",%s,%zu,", class_csv_name(metric_class(s.metric)), s.member);
    row += buf;
    row += csv_quote(s.label) + "," + csv_quote(s.pair_a) + "," + csv_quote(s.pair_b);
    snprintf(buf, sizeof(buf), ",%s,%s,%.9g,%d,%u,", metric_path(s.metric),
        metric_uses_degrees(s.metric) ? "deg" : "Angstrom", s.value, std::isfinite(s.value) ? 1 : 0, s.quality);
    row += buf;
    std::string reason;
    for (uint16_t bit = 1; bit <= Quality_Last; bit <<= 1) {
        if (s.quality & bit) {
            if (!reason.empty()) reason += "; ";
            reason += quality_description(bit);
        }
    }
    row += csv_quote(reason.empty() ? "Good" : reason);
    const auto ref = s.baseline.reference(s.value);
    snprintf(buf, sizeof(buf), ",%.17g,%.17g,%.9g,%.9g,%d,%d,%d,%u,%s\n",
        ref.count ? ref.mean : std::numeric_limits<double>::quiet_NaN(),
        ref.count ? ref.sd : std::numeric_limits<double>::quiet_NaN(),
        s.baseline.zscore(s.value), s.sigma, (s.selected_mask & (1u << (s.metric - class_first_metric(metric_class(s.metric))))) ? 1 : 0,
        s.baseline.is_outlier(s.value, s.sigma) ? 1 : 0, s.member_outlier ? 1 : 0, s.selected_mask,
        s.baseline_method == Baseline_MedianMAD ? "median_mad" : "mean_sd");
    row += buf;
    return row;
}

// Base-pair range a member touches: a step both its pairs, a nucleotide its own pair.
static void member_pairs(int cls, uint32_t member, const std::vector<StepIndex>& steps, uint32_t* lo, uint32_t* hi) {
    if (cls == Class_Step) {
        *lo = std::min(steps[member].a, steps[member].b);
        *hi = std::max(steps[member].a, steps[member].b);
    } else {
        *lo = *hi = cls == Class_Nucleotide ? member / 2 : member;
    }
}

const EventMember::Sample* event_member_sample(const EventMember& member, size_t frame) {
    const auto it = std::lower_bound(member.samples.begin(), member.samples.end(), frame,
        [](const EventMember::Sample& sample, size_t f) { return sample.frame < f; });
    return it != member.samples.end() && it->frame == frame ? &*it : nullptr;
}

std::vector<size_t> event_color_slots(const std::vector<OutlierEvent>& events) {
    std::vector<size_t> order(events.size()), slots(events.size(), SIZE_MAX);
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return events[a].begin < events[b].begin; });
    std::vector<size_t> preferred(events.size());
    for (size_t rank = 0; rank < order.size(); ++rank) preferred[order[rank]] = rank % EVENT_COLOR_COUNT;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return events[a].score > events[b].score; });
    std::vector<size_t> assigned;
    std::vector<bool> used;
    for (size_t i : order) {
        used.assign(std::max(assigned.size() + 1, EVENT_COLOR_COUNT), false);
        for (size_t j : assigned) {
            if (events[j].begin <= events[i].end && events[i].begin <= events[j].end && slots[j] < used.size()) used[slots[j]] = true;
        }
        size_t slot = EVENT_COLOR_COUNT;
        for (size_t offset = 0; offset < EVENT_COLOR_COUNT; ++offset) {
            const size_t candidate = (preferred[i] + offset) % EVENT_COLOR_COUNT;
            if (!used[candidate]) { slot = candidate; break; }
        }
        while (slot < used.size() && used[slot]) ++slot;
        slots[i] = slot;
        assigned.push_back(i);
    }
    return slots;
}

std::vector<EventContribution> event_contributions(const std::vector<OutlierEvent>& events, size_t frame, size_t inspected_event) {
    std::vector<EventContribution> result;
    if (inspected_event != SIZE_MAX && inspected_event >= events.size()) {
        MD_LOG_ERROR("Lavery: invalid event inspection index");
        return result;
    }
    for (size_t e = 0; e < events.size(); ++e) {
        if (inspected_event != SIZE_MAX && inspected_event != e) continue;
        const auto& event = events[e];
        if (frame < event.begin || frame > event.end) continue;
        for (const auto& member : event.members) {
            const auto* sample = event_member_sample(member, frame);
            if (sample) result.push_back({member.member, member.cls, sample->parameters, sample->lost});
        }
    }
    std::sort(result.begin(), result.end(), [](const EventContribution& a, const EventContribution& b) {
        return a.cls != b.cls ? a.cls < b.cls : a.member < b.member;
    });
    size_t count = 0;
    for (size_t i = 0; i < result.size(); ++i) {
        if (count && result[count - 1].cls == result[i].cls && result[count - 1].member == result[i].member) {
            result[count - 1].parameters |= result[i].parameters;
            result[count - 1].lost |= result[i].lost;
        } else {
            result[count++] = result[i];
        }
    }
    result.resize(count);
    return result;
}

bool event_time_range(EventOptions* options, double begin, double end) {
    if (!options || options->times.empty() || !std::isfinite(begin) || !std::isfinite(end) || begin > end) {
        MD_LOG_ERROR("Lavery: invalid event detection time range");
        return false;
    }
    for (size_t f = 0; f < options->times.size(); ++f) {
        if (!std::isfinite(options->times[f]) || (f && options->times[f] <= options->times[f - 1])) {
            MD_LOG_ERROR("Lavery: event timestamps must be finite and strictly increasing");
            return false;
        }
    }
    const auto first = std::lower_bound(options->times.begin(), options->times.end(), begin);
    const auto last = std::upper_bound(options->times.begin(), options->times.end(), end);
    if (first == last) {
        MD_LOG_ERROR("Lavery: event detection time range contains no samples");
        return false;
    }
    options->first_frame = (size_t)(first - options->times.begin());
    options->last_frame = (size_t)(last - options->times.begin() - 1);
    return true;
}

std::vector<OutlierEvent> detect_events(const std::vector<std::vector<MemberOutlier>>& frames, size_t pair_count,
                                        const std::vector<StepIndex>& steps, const EventOptions& options) {
    std::vector<OutlierEvent> events;
    if ((!options.times.empty() && options.times.size() != frames.size()) ||
        !std::isfinite(options.min_duration) || options.min_duration < 0 ||
        !std::isfinite(options.max_gap_duration) || options.max_gap_duration < 0 ||
        options.first_frame > options.last_frame) {
        MD_LOG_ERROR("Lavery: invalid event time/range options");
        return events;
    }
    for (size_t f = 0; f < options.times.size(); ++f) {
        if (!std::isfinite(options.times[f]) || (f && options.times[f] <= options.times[f - 1])) {
            MD_LOG_ERROR("Lavery: event timestamps must be finite and strictly increasing");
            return events;
        }
    }
    if (!std::isfinite(options.burst_sigma) || options.burst_sigma < 0 ||
        !std::isfinite(options.burst_hysteresis) || options.burst_hysteresis < 0 ||
        !std::isfinite(options.residual_sigma) || options.residual_sigma < 0 ||
        options.burst_neighbourhood < 0) {
        MD_LOG_ERROR("Lavery: invalid hierarchical event options");
        return events;
    }
    const size_t node_count = pair_count + steps.size();
    for (const auto& step : steps) {
        if (step.a >= pair_count || step.b >= pair_count) {
            MD_LOG_ERROR("Lavery: invalid step topology for event detection");
            return events;
        }
    }

    // Region of each node and the regions within reach. Pairs and steps form a chain graph
    // (pair - step - pair); one edge is half a base pair, so the reach is 2 * neighbourhood edges.
    const bool whole = options.neighbourhood < 0;
    std::vector<std::vector<uint32_t>> reach(whole ? 1 : node_count);
    if (!whole) {
        std::vector<std::vector<uint32_t>> adjacent(node_count);
        for (size_t s = 0; s < steps.size(); ++s) {
            const uint32_t node = (uint32_t)(pair_count + s);
            for (uint32_t p : {steps[s].a, steps[s].b}) {
                adjacent[node].push_back(p);
                adjacent[p].push_back(node);
            }
        }
        const int max_hops = 2 * options.neighbourhood;
        std::vector<int> hops(node_count, -1);
        for (uint32_t n = 0; n < node_count; ++n) {
            std::vector<uint32_t> queue = {n};
            hops[n] = 0;
            for (size_t q = 0; q < queue.size(); ++q) {
                const uint32_t cur = queue[q];
                if (hops[cur] == max_hops) continue;
                for (uint32_t next : adjacent[cur]) {
                    if (hops[next] >= 0) continue;
                    hops[next] = hops[cur] + 1;
                    queue.push_back(next);
                }
            }
            reach[n] = queue;
            for (uint32_t v : queue) hops[v] = -1;
        }
    } else {
        reach[0] = {0};
    }

    struct Point { size_t frame; const MemberOutlier* sample; uint32_t node; };
    std::vector<Point> points;
    for (size_t f = 0; f < frames.size(); ++f) {
        if (f < options.first_frame || f > options.last_frame) continue;
        for (const auto& sample : frames[f]) {
            if (!sample.contributors && !sample.lost) continue;
            const size_t limit = sample.cls == Class_Step ? steps.size() : sample.cls == Class_Nucleotide ? 2 * pair_count : pair_count;
            if (sample.cls >= Class_Count || sample.member >= limit || (sample.lost && sample.cls != Class_Pair)) {
                MD_LOG_ERROR("Lavery: invalid event sample");
                return {};
            }
            const size_t node = sample.cls == Class_Step ? pair_count + sample.member :
                sample.cls == Class_Nucleotide ? sample.member / 2 : sample.member;
            points.push_back({f, &sample, (uint32_t)node});
        }
    }

    std::vector<size_t> parent(points.size());
    for (size_t i = 0; i < parent.size(); ++i) parent[i] = i;
    auto find = [&](size_t i) {
        while (parent[i] != i) i = parent[i] = parent[parent[i]];
        return i;
    };
    const size_t none = SIZE_MAX;
    std::vector<size_t> last(reach.size(), none);
    for (size_t i = 0; i < points.size(); ++i) {
        const uint32_t region = whole ? 0 : points[i].node;
        for (uint32_t other : reach[region]) {
            const size_t j = last[other];
            if (j == none) continue;
            const size_t current = points[i].frame, previous = points[j].frame;
            if (options.times.empty()) {
                if (current - previous > options.max_gap + 1) continue;
            } else {
                // Quiet-gap time excludes the final sampling interval: consecutive samples join at zero gap.
                const double gap = current == previous ? 0 : options.times[current - 1] - options.times[previous];
                if (gap > options.max_gap_duration) continue;
            }
            parent[find(i)] = find(j);
        }
        last[region] = i;
    }

    // Clusters keep the order of their first point, which is chronological.
    std::vector<size_t> cluster(points.size(), none);
    std::vector<std::vector<size_t>> members;
    for (size_t i = 0; i < points.size(); ++i) {
        const size_t root = find(i);
        if (cluster[root] == none) {
            cluster[root] = members.size();
            members.emplace_back();
        }
        members[cluster[root]].push_back(i);
    }

    const size_t min_frames = std::max<size_t>(1, options.min_frames);
    for (const auto& group : members) {
        bool strong = false;
        for (size_t i : group) strong |= points[i].sample->strong != 0 || points[i].sample->lost;
        if (!strong) continue;

        OutlierEvent event;
        event.begin = points[group.front()].frame;
        event.end = points[group.back()].frame;
        event.peak = event.begin;
        event.pair_first = UINT32_MAX;
        bool have_peak = false;
        size_t frame = none, frame_count = 0;
        for (size_t i : group) {
            const Point& point = points[i];
            const MemberOutlier& sample = *point.sample;
            if (point.frame != frame) {
                frame = point.frame;
                frame_count = 0;
                ++event.outlier_frames;
            }
            event.peak_count = std::max(event.peak_count, ++frame_count);
            uint32_t lo = 0, hi = 0;
            member_pairs(sample.cls, sample.member, steps, &lo, &hi);
            event.pair_first = std::min(event.pair_first, lo);
            event.pair_last = std::max(event.pair_last, hi);

            auto it = std::find_if(event.members.begin(), event.members.end(), [&](const EventMember& m) {
                return m.member == sample.member && m.cls == sample.cls;
            });
            if (it == event.members.end()) {
                EventMember entry;
                entry.member = sample.member;
                entry.cls = sample.cls;
                entry.peak = point.frame;
                event.members.push_back(entry);
                it = event.members.end() - 1;
            }
            ++it->frames;
            it->samples.push_back({point.frame, sample.contributors, sample.lost});
            if (sample.lost) {
                ++it->lost_frames;
                ++event.lost_samples;
                event.score += options.lost_weight;
                continue;
            }
            float largest = 0;
            for (int k = 0; k < class_parameter_count(sample.cls); ++k) {
                if (!(sample.contributors & (1u << k))) continue;
                const float z = sample.z[k];
                const int metric = class_first_metric(sample.cls) + k;
                event.metric_score[metric] += std::fabs(z);
                event.metric_signed[metric] += z;
                largest = std::max(largest, std::fabs(z));
                const bool first = !(it->parameters & (1u << k));
                it->parameters |= 1u << k;
                if (first || std::fabs(z) > std::fabs(it->parameter_peak_z[k])) {
                    it->parameter_peak_z[k] = z;
                    it->parameter_peak[k] = point.frame;
                }
                if (it->peak_parameter < 0 || std::fabs(z) > std::fabs(it->peak_z)) {
                    it->peak_z = z;
                    it->peak = point.frame;
                    it->peak_parameter = k;
                }
                if (!have_peak || std::fabs(z) > event.severity) {
                    event.severity = std::fabs(z);
                    event.peak = point.frame;
                    have_peak = true;
                }
            }
            event.score += largest;
        }
        if (event.outlier_frames < min_frames) continue;
        if (!options.times.empty() && options.times[event.end] - options.times[event.begin] < options.min_duration) continue;
        event.truncated_begin = options.first_frame > 0 && event.begin == options.first_frame;
        const size_t last_frame = frames.empty() ? 0 : std::min(options.last_frame, frames.size() - 1);
        event.truncated_end = last_frame + 1 < frames.size() && event.end == last_frame;
        std::stable_sort(event.members.begin(), event.members.end(), [](const EventMember& a, const EventMember& b) {
            if ((a.peak_parameter < 0) != (b.peak_parameter < 0)) return b.peak_parameter < 0;
            return std::fabs(a.peak_z) > std::fabs(b.peak_z);
        });
        events.push_back(std::move(event));
    }
    if (options.burst_sigma > 0 && !events.empty()) {
        const bool residual = options.residual_sigma > 0 && options.member_z;
        const float start_sigma = residual ? options.residual_sigma : options.burst_sigma;
        const float continue_sigma = std::max(0.5f, start_sigma - options.burst_hysteresis);
        std::vector<std::vector<MemberOutlier>> elevated(frames.size());
        auto elevate = [&](MemberOutlier sample, const float* background) {
            const uint32_t original = sample.contributors;
            sample.contributors = sample.strong = 0;
            for (int k = 0; k < class_parameter_count(sample.cls); ++k) {
                if (!(original & (1u << k))) continue;
                const float z = std::fabs(background ? sample.z[k] - background[k] : sample.z[k]);
                if (!std::isfinite(z)) continue;
                if (z >= continue_sigma) sample.contributors |= 1u << k;
                if (z >= start_sigma) sample.strong |= 1u << k;
            }
            return sample;
        };
        if (!residual) {
            for (size_t f = 0; f < frames.size(); ++f) {
                for (const auto& original : frames[f]) {
                    const auto sample = elevate(original, nullptr);
                    if (sample.contributors || sample.lost) elevated[f].push_back(sample);
                }
            }
        } else {
            // Each sample belongs to exactly one parent; subtract that parent's per-frame regional median.
            std::vector<float> scratch;
            for (const auto& parent : events) {
                const size_t span = parent.end - parent.begin + 1;
                std::vector<float> background[Class_Count];
                std::vector<uint8_t> ready[Class_Count];
                constexpr int K = MAX_CLASS_PARAMETERS;
                auto background_at = [&](size_t f, int cls) -> const float* {
                    auto& bg = background[cls];
                    auto& done = ready[cls];
                    if (bg.empty()) { bg.assign(span * K, std::numeric_limits<float>::quiet_NaN()); done.assign(span, 0); }
                    const size_t i = f - parent.begin;
                    if (!done[i]) {
                        done[i] = 1;
                        const size_t count = cls == Class_Step ? steps.size() : cls == Class_Nucleotide ? 2 * pair_count : pair_count;
                        for (int k = 0; k < class_parameter_count(cls); ++k) {
                            scratch.clear();
                            for (size_t m = 0; m < count; ++m) {
                                uint32_t lo = 0, hi = 0;
                                member_pairs(cls, (uint32_t)m, steps, &lo, &hi);
                                if (lo < parent.pair_first || hi > parent.pair_last) continue;
                                const float z = options.member_z(f, cls, (uint32_t)m, k);
                                if (std::isfinite(z)) scratch.push_back(z);
                            }
                            if (scratch.size() < std::max<size_t>(1, options.background_min_members)) continue;
                            const size_t mid = scratch.size() / 2;
                            std::nth_element(scratch.begin(), scratch.begin() + mid, scratch.end());
                            float median = scratch[mid];
                            if (scratch.size() % 2 == 0) {
                                median = 0.5f * (median + *std::max_element(scratch.begin(), scratch.begin() + mid));
                            }
                            bg[i * K + k] = median;
                        }
                    }
                    return bg.data() + i * K;
                };
                for (const auto& member : parent.members) {
                    for (const auto& s : member.samples) {
                        const auto& frame = frames[s.frame];
                        const auto it = std::find_if(frame.begin(), frame.end(), [&](const MemberOutlier& o) {
                            return o.cls == member.cls && o.member == member.member;
                        });
                        if (it == frame.end()) continue;
                        const auto sample = elevate(*it, it->lost ? nullptr : background_at(s.frame, member.cls));
                        if (sample.contributors || sample.lost) elevated[s.frame].push_back(sample);
                    }
                }
            }
        }
        EventOptions child_options = options;
        child_options.burst_sigma = 0;
        child_options.residual_sigma = 0;
        child_options.member_z = nullptr;
        if (options.neighbourhood < 0) child_options.neighbourhood = options.burst_neighbourhood;
        auto children = detect_events(elevated, pair_count, steps, child_options);
        auto region_size = [&](const OutlierEvent& event) {
            std::vector<uint32_t> region;
            for (const auto& member : event.members) {
                uint32_t lo = 0, hi = 0;
                member_pairs(member.cls, member.member, steps, &lo, &hi);
                region.push_back(lo);
                region.push_back(hi);
            }
            std::sort(region.begin(), region.end());
            return (size_t)(std::unique(region.begin(), region.end()) - region.begin());
        };
        const size_t root_count = events.size();
        std::vector<size_t> region_sizes;
        for (const auto& event : events) region_sizes.push_back(region_size(event));
        for (auto& child : children) {
            const size_t child_size = region_size(child);
            for (size_t p = 0; p < root_count; ++p) {
                const auto& root = events[p];
                if (child.begin < root.begin || child.end > root.end || child_size >= region_sizes[p]) continue;
                bool contained = true;
                for (const auto& member : child.members) {
                    const auto parent_member = std::find_if(root.members.begin(), root.members.end(), [&](const EventMember& m) {
                        return m.cls == member.cls && m.member == member.member;
                    });
                    if (parent_member == root.members.end()) { contained = false; break; }
                    for (const auto& sample : member.samples) {
                        const auto* parent_sample = event_member_sample(*parent_member, sample.frame);
                        if (!parent_sample || (sample.parameters & ~parent_sample->parameters) ||
                            (sample.lost && !parent_sample->lost)) {
                            contained = false;
                            break;
                        }
                    }
                    if (!contained) break;
                }
                if (!contained) continue;
                child.parent = p;
                child.burst_sigma = start_sigma;
                child.residual = residual;
                events.push_back(std::move(child));
                break;
            }
        }
    }
    std::vector<size_t> order(events.size()), new_index(events.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return events[a].begin != events[b].begin ? events[a].begin < events[b].begin :
            events[a].pair_first < events[b].pair_first;
    });
    for (size_t i = 0; i < order.size(); ++i) new_index[order[i]] = i;
    std::vector<OutlierEvent> sorted;
    sorted.reserve(events.size());
    for (size_t i : order) {
        if (events[i].parent != SIZE_MAX) events[i].parent = new_index[events[i].parent];
        sorted.push_back(std::move(events[i]));
    }
    events = std::move(sorted);
    return events;
}

int event_dominant_metric(const OutlierEvent& event) {
    int best = -1;
    for (int m = 0; m < (int)Metric_Count; ++m) {
        if (event.metric_score[m] > 0 && (best < 0 || event.metric_score[m] > event.metric_score[best])) best = m;
    }
    return best;
}

std::string event_kind(const OutlierEvent& event) {
    const int m = event_dominant_metric(event);
    const bool positive = m >= 0 && event.metric_signed[m] >= 0;
    const char* kind = "";
    switch (m) {
    case Buckle: case Propeller: kind = "Buckle / propeller"; break;
    case Opening: kind = "Opening (breathing / flip)"; break;
    case Shear: kind = "Shear (wobble)"; break;
    case Stretch: kind = positive ? "Pair stretching" : "Pair compression"; break;
    case Stagger: kind = "Stagger"; break;
    case Tilt: kind = "Tilt"; break;
    case Roll: kind = "Kink (roll)"; break;
    case Twist: kind = positive ? "Overwinding" : "Unwinding"; break;
    case Shift: case Slide: kind = "Step translation"; break;
    case Rise: kind = positive ? "Unstacking" : "Compressed step"; break;
    case MinorGroove: kind = positive ? "Minor groove widening" : "Minor groove narrowing"; break;
    case MajorGroove: kind = positive ? "Major groove widening" : "Major groove narrowing"; break;
    case Phase: case Amplitude: case Delta: kind = "Sugar fluctuation"; break;
    case Epsilon: case Zeta: case EpsilonZeta: kind = "Epsilon / zeta fluctuation"; break;
    case Alpha: case Gamma: kind = "Alpha / gamma fluctuation"; break;
    case Beta: kind = "Beta crankshaft"; break;
    case Chi: kind = "Chi fluctuation"; break;
    default: break;
    }
    if (event.lost_samples) return *kind ? std::string("Pair loss + ") + kind : std::string("Pair loss");
    return kind;
}

double chance_outlier_probability(float sigma, int parameters) {
    if (!std::isfinite(sigma) || parameters <= 0) return 0.0;
    const double single = std::erfc(std::max(0.0f, sigma) / std::sqrt(2.0));
    return 1.0 - std::pow(1.0 - single, (double)parameters);
}

const char* csv_event_header() {
    return "run,event,begin_frame,end_frame,begin_time,end_time,time_unit,event_peak_frame,event_peak_sigma,event_score,event_type,"
           "first_pair,last_pair,scope,baseline,sigma,end_sigma,neighbourhood,"
           "class,member,label,member_outlier_frames,member_lost_frames,parameter,unit,contributing,parameter_peak_frame,parameter_peak_zscore,parameter_peak_value,"
           "parent_event,burst_sigma,truncated_begin,truncated_end,min_duration,max_gap_duration,detection_begin,detection_end,burst_mode\n";
}

std::string csv_event_row(const CsvEventRow& r) {
    char buf[512];
    std::string row = csv_quote(r.run);
    snprintf(buf, sizeof(buf), ",%zu,%zu,%zu,%.17g,%.17g,", r.event, r.begin, r.end, r.begin_time, r.end_time);
    row += buf;
    row += csv_quote(r.time_unit);
    snprintf(buf, sizeof(buf), ",%zu,%.9g,%.9g,", r.peak, r.severity, r.score);
    row += buf;
    row += csv_quote(r.kind);
    snprintf(buf, sizeof(buf), ",%zu,%zu,", r.pair_first, r.pair_last);
    row += buf;
    row += csv_quote(r.scope) + "," + csv_quote(r.baseline);
    snprintf(buf, sizeof(buf), ",%.9g,%.9g,%d,%s,%zu,", r.sigma, r.end_sigma, r.neighbourhood, class_csv_name(metric_class(r.metric)), r.member);
    row += buf;
    row += csv_quote(r.label);
    snprintf(buf, sizeof(buf), ",%zu,%zu,%s,%s,%d,%zu,%.9g,%.9g,%zu,%.9g,%d,%d,%.17g,%.17g,%.17g,%.17g,%s\n", r.member_frames, r.member_lost, metric_path(r.metric),
        metric_uses_degrees(r.metric) ? "deg" : "Angstrom", r.contributing ? 1 : 0, r.parameter_peak, r.parameter_z, r.value,
        r.parent_event, r.burst_sigma, r.truncated_begin ? 1 : 0, r.truncated_end ? 1 : 0,
        r.min_duration, r.max_gap_duration, r.detection_begin, r.detection_end,
        !r.parent_event ? "" : r.residual ? "background" : "stronger");
    row += buf;
    return row;
}

size_t nearest_frame(double x, const float* times, size_t frame_count) {
    if (!frame_count || !std::isfinite(x)) {
        MD_LOG_ERROR("Lavery: invalid frame navigation input");
        return 0;
    }
    if (!times) return (size_t)std::floor(std::clamp(x, 0.0, (double)frame_count - 1) + 0.5);
    const float* it = std::lower_bound(times, times + frame_count, x);
    if (it == times) return 0;
    if (it == times + frame_count) return frame_count - 1;
    return (size_t)(it - times) - (x - *(it - 1) < *it - x ? 1 : 0);
}

bool compute_outlier_counts(std::vector<FrameOutliers>* out, const std::vector<float>& values,
                            const std::vector<MemberStatistics>& members, float sigma) {
    if (!out || !std::isfinite(sigma) || sigma < 0 ||
        (members.empty() ? !values.empty() : values.size() % members.size() != 0)) {
        MD_LOG_ERROR("Lavery: invalid input for outlier counts");
        return false;
    }
    out->assign(members.empty() ? 0 : values.size() / members.size(), {});
    for (size_t f = 0; f < out->size(); ++f) {
        for (size_t m = 0; m < members.size(); ++m) {
            const float value = values[f * members.size() + m];
            if (!members[m].count || !std::isfinite(value)) continue;
            ++(*out)[f].valid;
            if (members[m].is_outlier(value, sigma)) ++(*out)[f].outliers;
        }
    }
    return true;
}

std::vector<BaseTopology> gather_bases(const md_system_t& sys) {
    std::vector<BaseTopology> bases;
    const auto& bb = sys.nucleic_backbone;
    for (uint32_t r = 0; r < bb.range.count; ++r) {
        if (md_nucleic_backbone_range_is_coarse_grained(&bb, r)) continue;
        for (uint32_t s = bb.range.offset[r]; s < bb.range.offset[r + 1]; ++s) {
            const int32_t comp = bb.segment.comp_idx[s];
            const int32_t c1 = bb.segment.atoms[s].c1;
            if (comp < 0 || c1 < 0) continue;
            BaseTopology base;
            base.component = (uint32_t)comp;
            base.strand = r;
            base.chain_order = s - bb.range.offset[r];
            base.glycosidic_atom = c1;
            const auto range = md_component_atom_range(&sys.component, comp);
            // Like the cartoon, identify base rings by their bonds to the sugar,
            // not by per-atom nucleobase flags.
            std::vector<uint32_t> ring_ids;
            for (uint32_t ring = 0; ring < md_index_data_num_ranges(&sys.ring); ++ring) {
                const int32_t* atoms = md_index_range_beg(&sys.ring, ring);
                const size_t n = md_index_range_size(&sys.ring, ring);
                if (n < 5 || n > 6) continue;
                bool within = true;
                bool sugar = false;
                for (size_t k = 0; k < n; ++k) {
                    within &= atoms[k] >= 0 && (uint32_t)atoms[k] >= range.beg && (uint32_t)atoms[k] < range.end;
                    sugar |= atoms[k] == c1;
                }
                if (within && !sugar) ring_ids.push_back(ring);
            }
            int32_t nitrogen = -1;
            for (uint32_t ring : ring_ids) {
                const int32_t* atoms = md_index_range_beg(&sys.ring, ring);
                for (size_t k = 0; k < md_index_range_size(&sys.ring, ring); ++k) {
                    if (md_atom_atomic_number(&sys.atom, atoms[k]) == 7 &&
                        md_bond_find(&sys.bond, atoms[k], c1) != -1) nitrogen = atoms[k];
                }
            }
            if (nitrogen < 0) continue;
            auto atom_named = [&](const char* name) -> int32_t {
                for (uint32_t atom = range.beg; atom < range.end; ++atom) {
                    if (str_eq_cstr(md_atom_name(&sys.atom, atom), name)) return (int32_t)atom;
                }
                return -1;
            };
            if (atom_named("N9") == nitrogen) {
                if (atom_named("N6") >= 0) base.reference = 0;
                else if (atom_named("O6") >= 0) base.reference = 1;
            } else if (atom_named("N1") == nitrogen) {
                if (atom_named("N4") >= 0) base.reference = 2;
                else if (atom_named("O4") >= 0) base.reference = 3;
            }
            if (base.reference >= 0) {
                const auto& ref = references[base.reference];
                for (size_t k = 0; k < ref.count; ++k) {
                    const int32_t atom = k == 0 ? c1 : atom_named(ref.names[k]);
                    if (atom < 0) { base.fit_atoms.clear(); break; }
                    base.fit_atoms.push_back((uint32_t)atom);
                }
            }
            base.atoms.push_back((uint32_t)nitrogen);
            for (size_t pass = 0; pass < ring_ids.size(); ++pass) {
                for (uint32_t ring : ring_ids) {
                    const int32_t* atoms = md_index_range_beg(&sys.ring, ring);
                    const size_t n = md_index_range_size(&sys.ring, ring);
                    bool connected = false;
                    for (size_t k = 0; k < n; ++k) {
                        connected |= std::find(base.atoms.begin(), base.atoms.end(), (uint32_t)atoms[k]) != base.atoms.end();
                    }
                    if (!connected) continue;
                    if (n > base.ring.size()) base.ring.assign(atoms, atoms + n);
                    for (size_t k = 0; k < n; ++k) {
                        if (std::find(base.atoms.begin(), base.atoms.end(), (uint32_t)atoms[k]) == base.atoms.end()) {
                            base.atoms.push_back((uint32_t)atoms[k]);
                        }
                    }
                }
            }
            if (base.ring.size() < 3) continue;
            const auto& na = bb.segment.atoms[s];
            const int32_t own[] = {na.p, na.o5, na.c5, na.c4, na.c3, na.o3, na.c1, na.c2, na.o4};
            for (int k = 0; k <= BB_O4; ++k) base.backbone[k] = own[k];
            // Neighbouring backbone atoms only across an actual O3'-P bond (no chain breaks).
            if (s > bb.range.offset[r]) {
                const int32_t o3 = bb.segment.atoms[s - 1].o3;
                if (o3 >= 0 && na.p >= 0 && md_bond_find(&sys.bond, o3, na.p) != -1) base.backbone[BB_PrevO3] = o3;
            }
            if (s + 1 < bb.range.offset[r + 1]) {
                const auto& next = bb.segment.atoms[s + 1];
                if (na.o3 >= 0 && next.p >= 0 && md_bond_find(&sys.bond, na.o3, next.p) != -1) {
                    base.backbone[BB_NextP] = next.p;
                    base.backbone[BB_NextO5] = next.o5;
                }
            }
            base.backbone[BB_GlycosidicN] = nitrogen;
            base.backbone[BB_ChiBase] = atom_named("N9") == nitrogen ? atom_named("C4") : atom_named("C2");
            bases.push_back(std::move(base));
        }
    }
    return bases;
}

bool extract_base_frames(std::vector<BaseFrame>& out, const md_system_t&, const md_system_state_t& state,
                         const std::vector<BaseTopology>& topology, std::vector<uint16_t>* quality) {
    out.assign(topology.size(), BaseFrame{});
    if (quality) quality->assign(topology.size(), Quality_DegenerateBase);
    for (size_t i = 0; i < topology.size(); ++i) {
        const BaseTopology& base = topology[i];
        if (base.reference < 0 || (size_t)base.reference >= std::size(references)) {
            if (quality) (*quality)[i] = Quality_UnsupportedBase;
            continue;
        }
        const auto& ref = references[base.reference];
        if (base.ring.size() < 3 || base.glycosidic_atom < 0 || base.atoms.empty() || base.fit_atoms.size() != ref.count) {
            if (quality) (*quality)[i] = Quality_MissingAtoms;
            continue;
        }
        const vec3_t anchor = md_state_coord(&state, base.ring[0]);
        auto position = [&](uint32_t atom) {
            vec3_t delta = vec3_sub(md_state_coord(&state, atom), anchor);
            md_util_min_image_vec3(&delta, 1, &state.unitcell);
            return vec3_add(anchor, delta);
        };
        vec3_t origin = {};
        for (uint32_t atom : base.atoms) origin = vec3_add(origin, position(atom));
        origin = vec3_mul1(origin, 1.0f / (float)base.atoms.size());
        vec3_t actual[10] = {};
        vec3_t centers[2] = {};
        bool finite = true;
        for (size_t k = 0; k < ref.count; ++k) {
            actual[k] = position(base.fit_atoms[k]);
            finite &= std::isfinite(actual[k].x) && std::isfinite(actual[k].y) && std::isfinite(actual[k].z);
            centers[0] = vec3_add(centers[0], ref.xyz[k]);
            centers[1] = vec3_add(centers[1], actual[k]);
        }
        if (!finite) {
            if (quality) (*quality)[i] = Quality_NonfiniteCoordinates;
            continue;
        }
        if (vec3_length(vec3_cross(vec3_sub(actual[0], actual[1]), vec3_sub(actual[2], actual[1]))) < 1.0e-4f) continue;
        centers[0] = vec3_mul1(centers[0], 1.0f / (float)ref.count);
        centers[1] = vec3_mul1(centers[1], 1.0f / (float)ref.count);
        const vec3_t* coords[] = {ref.xyz, actual};
        // mdlib returns the rotation from actual back onto reference coordinates.
        const mat3_t fit = mat3_transpose(mat3_optimal_rotation(coords, nullptr, nullptr, ref.count, centers));
        auto fitted = [&](size_t k) {
            return vec3_add(centers[1], mat3_mul_vec3(fit, vec3_sub(ref.xyz[k], centers[0])));
        };
        const vec3_t sugar = fitted(0), nitrogen = fitted(1), ring_atom = fitted(2);
        const vec3_t glyco = normalize_or_zero(vec3_sub(sugar, nitrogen));
        const vec3_t normal = normalize_or_zero(vec3_cross(glyco, vec3_sub(ring_atom, nitrogen)));
        constexpr float DEG_TO_RAD = 1.0f / RAD_TO_DEG;
        const vec3_t offset = quat_mul_vec3(quat_axis_angle(normal, 141.47f * DEG_TO_RAD), glyco);
        const vec3_t y = quat_mul_vec3(quat_axis_angle(normal, -54.41f * DEG_TO_RAD), glyco);
        const vec3_t x = vec3_cross(y, normal);
        BaseFrame frame = {vec3_add(nitrogen, vec3_mul1(offset, 4.702f)),
                           x, y, normal, base.component, base.strand, origin};
        if (valid_frame(frame)) {
            out[i] = frame;
            if (quality) (*quality)[i] = Quality_Good;
        }
    }
    return true;
}

std::vector<StepIndex> gather_steps(const md_system_t&, const std::vector<BaseTopology>& bases,
                                   const Pair* pairs, size_t pair_count) {
    std::vector<StepIndex> steps;
    for (uint32_t a = 0; a < pair_count; ++a) {
        const auto& a0 = bases[pairs[a].a];
        const auto& a1 = bases[pairs[a].b];
        for (uint32_t b = 0; b < pair_count; ++b) {
            const auto& b0 = bases[pairs[b].a];
            const auto& b1 = bases[pairs[b].b];
            if (a0.strand != b0.strand || a1.strand != b1.strand ||
                a0.chain_order == UINT32_MAX || a1.chain_order == UINT32_MAX ||
                b0.chain_order == UINT32_MAX || b1.chain_order == UINT32_MAX) continue;
            const bool adjacent_partner = a1.chain_order + 1 == b1.chain_order ||
                                          b1.chain_order + 1 == a1.chain_order;
            if (b0.chain_order == a0.chain_order + 1 && adjacent_partner) steps.push_back({a, b});
        }
    }
    return steps;
}

size_t detect_pairs(Pair* out, size_t cap, const BaseFrame* bases, size_t count, const md_unitcell_t* cell) {
    if (!out || !bases || cap == 0 || count < 2) return 0;

    std::vector<Candidate> candidates;
    candidates.reserve(count * 2);
    for (uint32_t i = 0; i < count; ++i) {
        if (!valid_frame(bases[i])) continue;
        for (uint32_t j = i + 1; j < count; ++j) {
            if (bases[i].strand == bases[j].strand || bases[i].component == bases[j].component) continue;
            const float score = pairing_score(bases[i], bases[j], cell);
            if (std::isfinite(score)) candidates.push_back({i, j, score});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.score < b.score;
    });

    std::vector<uint8_t> used(count, 0);
    size_t num_pairs = 0;
    for (const Candidate& candidate : candidates) {
        if (used[candidate.a] || used[candidate.b] || num_pairs >= cap) continue;
        used[candidate.a] = used[candidate.b] = 1;
        uint32_t a = candidate.a;
        uint32_t b = candidate.b;
        if (bases[b].component < bases[a].component) std::swap(a, b);
        out[num_pairs++] = {a, b};
    }
    std::sort(out, out + num_pairs, [](const Pair& a, const Pair& b) {
        return a.a == b.a ? a.b < b.b : a.a < b.a;
    });
    return num_pairs;
}

bool pair_is_present(const BaseFrame& a, const BaseFrame& b, const md_unitcell_t* cell) {
    return std::isfinite(pairing_score(a, b, cell));
}

bool compute_base_pair(Values* out, BasePairFrame* pair_frame, const BaseFrame& a, const BaseFrame& b) {
    if (!out || !pair_frame) return false;
    BaseFrame aligned = b;
    aligned.y = vec3_mul1(b.y, -1.0f);
    aligned.z = vec3_mul1(b.z, -1.0f);
    float translation[3], rotation[3];
    if (!rigid_parameters(pair_frame, translation, rotation, a, aligned)) return false;
    // Curves+ reports intra-pair displacements with the opposite sense to
    // the frame-1-to-dyad-inverted-frame-2 transform used to form the midpoint.
    out->metric[Buckle] = -rotation[0];
    out->metric[Propeller] = -rotation[1];
    out->metric[Opening] = -rotation[2];
    out->metric[Shear] = -translation[0];
    out->metric[Stretch] = -translation[1];
    out->metric[Stagger] = -translation[2];
    return true;
}

bool compute_step(Values* out, const BasePairFrame& a, const BasePairFrame& b, BasePairFrame* frame) {
    if (!out) return false;
    BasePairFrame midpoint;
    float translation[3], rotation[3];
    if (!rigid_parameters(&midpoint, translation, rotation,
        {a.origin, a.x, a.y, a.z}, {b.origin, b.x, b.y, b.z})) return false;
    if (frame) *frame = midpoint;
    for (int k = 0; k < 3; ++k) {
        out->metric[Tilt + k] = rotation[k];
        out->metric[Shift + k] = translation[k];
    }
    return true;
}

const char* metric_name(Metric metric) {
    static const char* names[] = {
        "Buckle", "Propeller Twist", "Opening", "Shear", "Stretch", "Stagger",
        "Tilt", "Roll", "Twist", "Shift", "Slide", "Rise", "Minor Groove", "Major Groove",
        "Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta", "Chi", "Pucker Phase", "Pucker Amplitude", "Epsilon - Zeta",
    };
    static_assert(sizeof(names) / sizeof(names[0]) == Metric_Count, "metric names");
    return metric < Metric_Count ? names[metric] : "Unknown";
}

const char* metric_path(Metric metric) {
    static const char* paths[] = {
        "bp/buckle", "bp/propeller", "bp/opening", "bp/shear", "bp/stretch", "bp/stagger",
        "step/tilt", "step/roll", "step/twist", "step/shift", "step/slide", "step/rise",
        "step/minor_groove", "step/major_groove",
        "nt/alpha", "nt/beta", "nt/gamma", "nt/delta", "nt/epsilon", "nt/zeta", "nt/chi",
        "nt/pucker_phase", "nt/pucker_amplitude", "nt/epsilon_minus_zeta",
    };
    static_assert(sizeof(paths) / sizeof(paths[0]) == Metric_Count, "metric paths");
    return metric < Metric_Count ? paths[metric] : "";
}

bool metric_is_step(Metric metric) {
    return metric >= Tilt && metric <= MajorGroove;
}

bool metric_is_angular(Metric metric) {
    return metric == Buckle || metric == Propeller || metric == Opening ||
           metric == Tilt || metric == Roll || metric == Twist ||
           (metric >= Alpha && metric <= EpsilonZeta && metric != Amplitude);
}

bool metric_uses_degrees(Metric metric) {
    return metric_is_angular(metric) || metric == Amplitude;
}

int class_first_metric(int cls) {
    return cls == Class_Step ? (int)Tilt : cls == Class_Nucleotide ? (int)Alpha : 0;
}

int class_parameter_count(int cls) {
    return cls == Class_Pair ? 6 : cls == Class_Step ? 8 : cls == Class_Nucleotide ? 10 : 0;
}

MemberClass metric_class(Metric metric) {
    return metric >= Alpha ? Class_Nucleotide : metric >= Tilt ? Class_Step : Class_Pair;
}

const char* class_name(int cls) {
    static const char* names[] = {"Intra-base pair", "Inter-base pair", "Nucleotide"};
    return cls >= 0 && cls < Class_Count ? names[cls] : "Unknown";
}

const char* class_csv_name(int cls) {
    static const char* names[] = {"bp", "step", "nt"};
    return cls >= 0 && cls < Class_Count ? names[cls] : "";
}

int metric_state_count(Metric metric) {
    switch (metric) {
    case Phase: case EpsilonZeta: case Chi: return 2;
    case Alpha: case Gamma: return 3;
    default: return 0;
    }
}

int metric_state(Metric metric, float value) {
    if (!metric_state_count(metric) || !std::isfinite(value)) return -1;
    const float a = (float)(value - 360.0 * std::floor(value / 360.0)); // [0, 360)
    switch (metric) {
    case Phase: return a >= 90.0f && a < 270.0f ? 1 : 0;              // North / South
    case EpsilonZeta: return wrap_degrees(value) < 0.0 ? 0 : 1;       // BI / BII
    case Chi: return a > 270.0f || a < 90.0f ? 1 : 0;                 // anti / syn
    case Alpha: case Gamma: return a < 120.0f ? 0 : a < 240.0f ? 1 : 2; // g+ / t / g-
    default: return -1;
    }
}

const char* metric_state_name(Metric metric, int state) {
    static const char* pucker[] = {"North (C3'-endo)", "South (C2'-endo)"};
    static const char* bi[] = {"BI", "BII"};
    static const char* chi[] = {"anti", "syn"};
    static const char* rot[] = {"g+", "t", "g-"};
    if (state < 0 || state >= metric_state_count(metric)) return "";
    switch (metric) {
    case Phase: return pucker[state];
    case EpsilonZeta: return bi[state];
    case Chi: return chi[state];
    case Alpha: case Gamma: return rot[state];
    default: return "";
    }
}

static bool finite3(vec3_t v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

static vec3_t bond(vec3_t from, vec3_t to, const md_unitcell_t* cell) {
    vec3_t d = vec3_sub(to, from);
    if (cell) md_util_min_image_vec3(&d, 1, cell);
    return d;
}

float torsion(vec3_t a, vec3_t b, vec3_t c, vec3_t d, const md_unitcell_t* cell) {
    if (!finite3(a) || !finite3(b) || !finite3(c) || !finite3(d)) return std::numeric_limits<float>::quiet_NaN();
    const vec3_t b1 = bond(a, b, cell), b2 = bond(b, c, cell), b3 = bond(c, d, cell);
    const vec3_t n1 = vec3_cross(b1, b2), n2 = vec3_cross(b2, b3);
    const float len = vec3_length(b2);
    if (vec3_length(n1) < 1e-6f || vec3_length(n2) < 1e-6f || len < 1e-6f) return std::numeric_limits<float>::quiet_NaN();
    const float y = vec3_dot(vec3_cross(n1, n2), b2) / len;
    const float x = vec3_dot(n1, n2);
    return (float)(std::atan2((double)y, (double)x) * (180.0 / 3.14159265358979323846));
}

void pseudorotation(const float nu[5], float* phase, float* amplitude) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    if (phase) *phase = nan;
    if (amplitude) *amplitude = nan;
    for (int i = 0; i < 5; ++i) if (!std::isfinite(nu[i])) return;
    constexpr double rad = 3.14159265358979323846 / 180.0;
    const double y = ((double)nu[4] + nu[1]) - ((double)nu[3] + nu[0]);
    const double x = 2.0 * nu[2] * (std::sin(36.0 * rad) + std::sin(72.0 * rad));
    double p = std::atan2(y, x) / rad;
    if (p < 0) p += 360.0;
    if (phase) *phase = (float)p;
    // Equals nu2 / cos(P) but stays finite when cos(P) is near zero.
    if (amplitude) *amplitude = (float)(std::sqrt(y * y + x * x) / (2.0 * (std::sin(36.0 * rad) + std::sin(72.0 * rad))));
}

void compute_nucleotide(Values* out, const vec3_t atoms[BB_Count], const md_unitcell_t* cell,
                        NucleotideQuality* quality, uint32_t present_atoms) {
    if (!out || !atoms) return;
    NucleotideQuality flags = {};
    auto t = [&](uint16_t& q, int a, int b, int c, int d) {
        const int slots[] = {a,b,c,d};
        for (int slot : slots) {
            if (!(present_atoms & (1u << slot))) q |= Quality_MissingAtoms;
            else if (!finite3(atoms[slot])) q |= Quality_NonfiniteCoordinates;
        }
        if (q & (Quality_MissingAtoms | Quality_NonfiniteCoordinates))
            return std::numeric_limits<float>::quiet_NaN();
        for (int i = 1; i < 4; ++i) {
            const float length = vec3_length(bond(atoms[slots[i-1]], atoms[slots[i]], cell));
            if (length < 1.0f || length > 2.1f) q |= Quality_UnusualBond;
        }
        const float value = torsion(atoms[a], atoms[b], atoms[c], atoms[d], cell);
        if (!std::isfinite(value)) q |= Quality_DegenerateNucleotide;
        return value;
    };
    BaseTopology slots;
    for (int k = 0; k < BB_Count; ++k) slots.backbone[k] = k;
    for (int id = Alpha; id <= (int)Chi; ++id) {
        const auto a = nucleotide_torsion_atoms(slots, (Metric)id);
        out->metric[id] = t(flags[id-Alpha], a[0], a[1], a[2], a[3]);
    }
    uint16_t ring_quality = 0;
    const float nu[5] = {
        t(ring_quality, BB_C4, BB_O4, BB_C1, BB_C2), t(ring_quality, BB_O4, BB_C1, BB_C2, BB_C3),
        t(ring_quality, BB_C1, BB_C2, BB_C3, BB_C4), t(ring_quality, BB_C2, BB_C3, BB_C4, BB_O4),
        t(ring_quality, BB_C3, BB_C4, BB_O4, BB_C1),
    };
    pseudorotation(nu, &out->metric[Phase], &out->metric[Amplitude]);
    if (std::isfinite(out->metric[Amplitude])) {
        double a = 0, b = 0;
        constexpr double rad = 3.14159265358979323846/180.0;
        for (int j = 0; j < 5; ++j) {
            a += 0.4*nu[j]*std::cos(144*j*rad);
            b += 0.4*nu[j]*std::sin(144*j*rad);
        }
        double residual2 = 0;
        for (int j = 0; j < 5; ++j) {
            const double residual = nu[j] - a*std::cos(144*j*rad) - b*std::sin(144*j*rad);
            residual2 += residual*residual;
        }
        if (std::sqrt(residual2/5) > 5.0) ring_quality |= Quality_PoorPuckerFit;
    }
    flags[Phase-Alpha] = flags[Amplitude-Alpha] = ring_quality;
    if (out->metric[Amplitude] < 1e-4f) {
        out->metric[Phase] = std::numeric_limits<float>::quiet_NaN();
        flags[Phase-Alpha] |= Quality_DegenerateNucleotide;
    }
    const float e = out->metric[Epsilon], z = out->metric[Zeta];
    out->metric[EpsilonZeta] = std::isfinite(e) && std::isfinite(z) ? (float)wrap_degrees((double)e - z)
                                                                     : std::numeric_limits<float>::quiet_NaN();
    flags[EpsilonZeta-Alpha] = flags[Epsilon-Alpha] | flags[Zeta-Alpha];
    if (quality) *quality = flags;
}

void groove_widths(float* minor, float* major, const vec3_t* p1, const vec3_t* p2, size_t n, const md_unitcell_t* cell) {
    if (!minor || !major || n < 2 || !p1 || !p2) return;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    constexpr float offset = 5.8f; // phosphate van der Waals diameter, as in Curves / El Hassan & Calladine
    // 1-based pair index k -> P1(k), P2(k); NaN when outside [1, n] or missing.
    auto dist = [&](long i, long j) -> float {
        if (i < 1 || j < 1 || i > (long)n || j > (long)n) return nan;
        const vec3_t a = p1[i - 1], b = p2[j - 1];
        if (!finite3(a) || !finite3(b)) return nan;
        return vec3_length(bond(a, b, cell));
    };
    for (size_t s = 0; s + 1 < n; ++s) {
        for (Metric metric : {MinorGroove, MajorGroove}) {
            const auto indices = groove_phosphate_indices(metric, s, n);
            float width = 0;
            for (size_t k = 0; k < indices.count; ++k) {
                const auto& index = indices.indices[k];
                width += dist(index[0] + 1, index[1] + 1);
            }
            (metric == MinorGroove ? minor[s] : major[s]) = !indices.count ? nan : width / indices.count - offset;
        }
    }
}

GroovePhosphates groove_phosphate_indices(Metric metric, size_t step, size_t pair_count) {
    if (step >= pair_count || pair_count - step < 2) return {};
    if (metric == MinorGroove && step >= 2 && pair_count - step > 3) {
        return {{{{(uint32_t)step + 2, (uint32_t)step - 2}, {(uint32_t)step + 3, (uint32_t)step - 1}}}, 2};
    }
    if (metric == MajorGroove && step >= 1 && pair_count - step > 2) {
        return {{{{(uint32_t)step - 1, (uint32_t)step + 2}, {0, 0}}}, 1};
    }
    return {};
}

std::array<int32_t, 4> nucleotide_torsion_atoms(const BaseTopology& base, Metric metric) {
    static constexpr int atoms[][4] = {
        {BB_PrevO3, BB_P, BB_O5, BB_C5}, {BB_P, BB_O5, BB_C5, BB_C4},
        {BB_O5, BB_C5, BB_C4, BB_C3}, {BB_C5, BB_C4, BB_C3, BB_O3},
        {BB_C4, BB_C3, BB_O3, BB_NextP}, {BB_C3, BB_O3, BB_NextP, BB_NextO5},
        {BB_O4, BB_C1, BB_GlycosidicN, BB_ChiBase},
    };
    if (metric < Alpha || metric > Chi) return {-1, -1, -1, -1};
    const auto& a = atoms[metric - Alpha];
    return {base.backbone[a[0]], base.backbone[a[1]], base.backbone[a[2]], base.backbone[a[3]]};
}

std::vector<std::vector<uint32_t>> helix_chains(size_t pair_count, const std::vector<StepIndex>& steps) {
    std::vector<int64_t> next(pair_count, -1), prev(pair_count, -1);
    for (const auto& step : steps) {
        if (step.a >= pair_count || step.b >= pair_count || step.a == step.b) continue;
        if (next[step.a] < 0 && prev[step.b] < 0) { next[step.a] = step.b; prev[step.b] = step.a; }
    }
    std::vector<std::vector<uint32_t>> chains;
    std::vector<uint8_t> seen(pair_count, 0);
    for (size_t p = 0; p < pair_count; ++p) {
        if (prev[p] >= 0 || seen[p] || next[p] < 0) continue;
        std::vector<uint32_t> chain;
        for (int64_t q = (int64_t)p; q >= 0 && !seen[q]; q = next[q]) { seen[q] = 1; chain.push_back((uint32_t)q); }
        chains.push_back(std::move(chain));
    }
    return chains;
}

} // namespace lavery
