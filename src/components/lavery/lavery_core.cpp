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

// Ring coordinates from classic 3DNA v2.4 Atomic_*.pdb; see 3dna-license.txt.
// Atom order follows references above, omitting C1'.
static const vec3_t dna_references[][9] = {
    {{-1.291f,4.498f,0}, {-1.267f,3.124f,0}, {0.024f,4.897f,0},
     {-0.668f,0.532f,0}, {-1.912f,1.023f,0}, {-2.320f,2.290f,0},
     {0.071f,2.771f,0}, {0.369f,1.398f,0}, {0.877f,3.902f,0}},
    {{-1.289f,4.551f,0}, {-1.265f,3.177f,0}, {0.023f,4.962f,0},
     {-0.700f,0.641f,0}, {-1.999f,1.087f,0}, {-2.342f,2.364f,0.001f},
     {0.071f,2.833f,0}, {0.424f,1.460f,0}, {0.870f,3.969f,0}},
    {{-1.285f,4.542f,0}, {-1.472f,3.158f,0}, {-0.023f,5.068f,0},
     {-0.391f,2.344f,0}, {0.837f,2.868f,0}, {1.056f,4.275f,0}},
    {{-1.284f,4.500f,0}, {-1.462f,3.135f,0}, {-0.024f,5.057f,0},
     {-0.298f,2.407f,0}, {0.994f,2.897f,0}, {1.106f,4.338f,0}},
    {{-1.284f,4.500f,0}, {-1.462f,3.131f,0}, {-0.024f,5.053f,0},
     {-0.302f,2.397f,0}, {0.989f,2.884f,0}, {1.089f,4.311f,0}},
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
    case Quality_UnusualBond: return "Warning: defining heavy-atom bond outside 1.0-2.1 Angstrom";
    case Quality_ReferenceBondBreak: return "Curves+ torsion unavailable: defining bond exceeds 2.5 Angstrom";
    case Quality_PoorPuckerFit: return "Warning: sugar ring Fourier-fit RMS residual above 5 degrees; finite measurement retained";
    default: return "Good";
    }
}

OutlierAssessment assess_outliers(const float* values, const MemberStatistics* statistics,
                                  size_t count, uint32_t selected, float sigma) {
    OutlierAssessment out;
    for (size_t i = 0; i < count && i < 32; ++i) {
        if (!(selected & (1u << i)) || !std::isfinite(values[i]) || !statistics[i].reference(values[i]).count) continue;
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

double wrap_degrees(double delta) {
    return delta - 360.0 * std::floor((delta + 180.0) / 360.0);
}

float step_bend_degrees(float tilt, float roll, float twist) {
    if (!std::isfinite(tilt) || !std::isfinite(roll) || !std::isfinite(twist)) return NAN;
    const double transverse=std::hypot((double)tilt,(double)roll);
    const double rotation=std::hypot(transverse,(double)twist);
    if (rotation>180.001) return NAN;
    if (rotation<1e-12) return 0;
    constexpr double rad=3.14159265358979323846/180;
    return (float)(2*std::asin(std::clamp(transverse/rotation*std::sin(rotation*rad*.5),0.0,1.0))/rad);
}

bool bend_profile_samples(std::vector<float>* bend, std::vector<float>* candidates,
    const std::vector<float>& tilt, const std::vector<float>& roll, const std::vector<float>& twist, float threshold) {
    if (!bend || !candidates || bend==candidates || tilt.size()!=roll.size() || tilt.size()!=twist.size() ||
        !std::isfinite(threshold) || threshold<=0 || threshold>180) {
        MD_LOG_ERROR("Lavery: invalid bending-profile input/threshold");
        return false;
    }
    bend->resize(tilt.size()); candidates->resize(tilt.size());
    for (size_t i=0;i<tilt.size();++i) {
        const float angle=step_bend_degrees(tilt[i],roll[i],twist[i]);
        (*bend)[i]=angle;
        (*candidates)[i]=std::isfinite(angle) ? (angle>=threshold ? 100.0f : 0.0f) : NAN;
    }
    return true;
}

bool state_profile_samples(std::vector<float>* out, const std::vector<float>& values, Metric metric, int state) {
    if (!out || state<0 || state>=metric_state_count(metric)) {
        MD_LOG_ERROR("Lavery: invalid occupancy-profile category");
        return false;
    }
    out->resize(values.size());
    for (size_t i=0;i<values.size();++i) {
        const int actual=metric_state(metric,values[i]);
        (*out)[i]=actual<0 ? NAN : actual==state ? 100.0f : 0.0f;
    }
    return true;
}

bool twist_difference_samples(std::vector<float>* out, const std::vector<float>& values,
    const std::vector<MemberStatistics>& reference) {
    if (!out || (reference.empty() ? !values.empty() : values.size()%reference.size()!=0)) {
        MD_LOG_ERROR("Lavery: invalid reference-relative twist profile");
        return false;
    }
    out->resize(values.size());
    for (size_t i=0;i<values.size();++i) {
        const auto& r=reference[i%reference.size()];
        (*out)[i]=r.count && std::isfinite(values[i]) ? (float)wrap_degrees(values[i]-r.mean) : NAN;
    }
    return true;
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
                               const BaseFrame& a, const BaseFrame& b, const md_unitcell_t* cell, bool reversed) {
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
    if (!compute_base_pair(out, pair_frame, a, partner, reversed)) {
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

const char* reference_scope_name(ReferenceScope scope) {
    switch (scope) {
    case Reference_OwnHistory: return "Own pair/step over time";
    case Reference_HelixHistory: return "All pairs/steps over time (same helix)";
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

bool PlotSummary::build(const float* values, size_t count, size_t stride) {
    *this = {};
    if ((!values && count) || !stride || count >= UINT32_MAX) {
        MD_LOG_ERROR("Lavery: invalid display series dimensions");
        return false;
    }
    count_ = count;
    if (count < 16) return true;
    auto empty = []() { return Bucket{UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX}; };
    auto add = [&](Bucket& b, uint32_t i) {
        if (i == UINT32_MAX) return;
        if (b[0] == UINT32_MAX || i < b[0]) b[0] = i;
        if (b[1] == UINT32_MAX || i > b[1]) b[1] = i;
        const float v = values[(size_t)i * stride];
        if (!std::isfinite(v)) {
            if (b[4] == UINT32_MAX || i < b[4]) b[4] = i;
        } else {
            if (b[2] == UINT32_MAX || v < values[(size_t)b[2] * stride]) b[2] = i;
            if (b[3] == UINT32_MAX || v > values[(size_t)b[3] * stride]) b[3] = i;
        }
    };
    levels_.emplace_back((count + 15) / 16, empty());
    for (size_t i = 0; i < count; ++i) add(levels_[0][i / 16], (uint32_t)i);
    while (levels_.back().size() > 1) {
        const size_t previous = levels_.size() - 1;
        const size_t n = (levels_[previous].size() + 3) / 4;
        levels_.emplace_back(n, empty());
        for (size_t i = 0; i < levels_[previous].size(); ++i)
            for (uint32_t index : levels_[previous][i]) add(levels_.back()[i / 4], index);
    }
    return true;
}

std::vector<uint32_t> PlotSummary::indices(size_t first, size_t end, size_t pixels) const {
    std::vector<uint32_t> result;
    if (first > end || end > count_ || !pixels) {
        MD_LOG_ERROR("Lavery: invalid display series query");
        return result;
    }
    if (first == end) return result;
    size_t level = 0, block = 16;
    if (levels_.empty() || (end - first) / pixels < block) {
        for (size_t i = first; i < end; ++i) result.push_back((uint32_t)i);
        return result;
    }
    while (level + 1 < levels_.size() && block * 4 <= (end - first) / pixels) {
        ++level;
        block *= 4;
    }
    auto visit = [&](auto&& self, size_t l, size_t b, size_t width) -> void {
        const size_t begin = b * width, finish = std::min(count_, begin + width);
        if (finish <= first || begin >= end) return;
        if (begin >= first && finish <= end) {
            auto indices = levels_[l][b];
            std::sort(indices.begin(), indices.end());
            for (size_t i = 0; i < indices.size(); ++i)
                if (indices[i] != UINT32_MAX && (!i || indices[i] != indices[i-1])) result.push_back(indices[i]);
        } else if (l) {
            for (size_t child = b * 4; child < (b + 1) * 4 && child < levels_[l-1].size(); ++child)
                self(self, l-1, child, width / 4);
        } else {
            for (size_t i = std::max(first, begin); i < std::min(end, finish); ++i) result.push_back((uint32_t)i);
        }
    };
    for (size_t b = first / block; b <= (end - 1) / block; ++b) visit(visit, level, b, block);
    return result;
}

bool compute_statistics(MetricStatistics* out, const std::vector<float>& values, size_t member_count,
                        bool angular, Baseline baseline, int state_metric, size_t first_frame, size_t end_frame,
                        const std::vector<uint32_t>& member_groups) {
    if (!out || (member_count == 0 ? !values.empty() : values.size() % member_count != 0) ||
        baseline < 0 || baseline >= Baseline_Count || state_metric >= (int)Metric_Count ||
        (!member_groups.empty() && (member_groups.size() != member_count ||
            *std::max_element(member_groups.begin(), member_groups.end()) >= member_count))) {
        MD_LOG_ERROR("Lavery: invalid metric matrix dimensions for statistics");
        return false;
    }
    *out = {};
    out->members.resize(member_count);
    if (!member_count) return true;
    const size_t frames = values.size() / member_count;
    if (end_frame == SIZE_MAX) end_frame = frames;
    if (first_frame > end_frame || end_frame > frames) {
        MD_LOG_ERROR("Lavery: invalid baseline frame range");
        return false;
    }
    out->heatmap.resize(values.size(), std::numeric_limits<float>::quiet_NaN());
    if (!member_groups.empty()) {
        const size_t groups = *std::max_element(member_groups.begin(), member_groups.end()) + 1;
        std::vector<std::vector<size_t>> indices(groups);
        for (size_t m = 0; m < member_count; ++m) indices[member_groups[m]].push_back(m);
        for (const auto& group : indices) {
            if (group.empty()) continue;
            std::vector<float> pooled;
            pooled.reserve((end_frame - first_frame) * group.size());
            for (size_t f = first_frame; f < end_frame; ++f)
                for (size_t m : group) pooled.push_back(values[f * member_count + m]);
            MetricStatistics reference;
            if (!compute_statistics(&reference, pooled, 1, angular, baseline, state_metric)) return false;
            for (size_t m : group) {
                out->members[m] = reference.members[0];
                for (size_t f = 0; f < frames; ++f)
                    out->heatmap[(member_count - 1 - m) * frames + f] =
                        out->members[m].zscore(values[f * member_count + m]);
            }
        }
        return true;
    }
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
            for (size_t f = first_frame; f < end_frame; ++f) {
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
            for (size_t f = first_frame; f < end_frame; ++f) {
                const float v = values[f * member_count + m];
                if (!in_baseline(v)) continue;
                s += std::sin((double)v * (3.14159265358979323846 / 180.0));
                c += std::cos((double)v * (3.14159265358979323846 / 180.0));
            }
            if (s != 0.0 || c != 0.0) reference = std::atan2(s, c) * (180.0 / 3.14159265358979323846);
        }
        double m2 = 0.0;
        for (size_t f = first_frame; f < end_frame; ++f) {
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

bool sequence_statistics(std::vector<MemberStatistics>* out, const std::vector<float>& values, size_t member_count,
                         bool angular, size_t first_frame, size_t end_frame,
                         const std::vector<uint32_t>& member_groups) {
    if (!out || (member_count ? values.size()%member_count : !values.empty()) ||
        (!member_groups.empty() && (member_groups.size()!=member_count ||
            *std::max_element(member_groups.begin(),member_groups.end())>=member_count))) {
        MD_LOG_ERROR("Lavery: invalid sequence-profile matrix or groups");
        return false;
    }
    const size_t frames=member_count ? values.size()/member_count : 0;
    if (end_frame==SIZE_MAX) end_frame=frames;
    if (first_frame>end_frame || end_frame>frames) {
        MD_LOG_ERROR("Lavery: invalid sequence-profile frame range");
        return false;
    }
    out->assign(member_count,{});
    const size_t group_count=member_groups.empty() ? member_count :
        (size_t)*std::max_element(member_groups.begin(),member_groups.end())+1;
    std::vector<std::vector<size_t>> groups(group_count);
    for (size_t m=0;m<member_count;++m) groups[member_groups.empty() ? m : member_groups[m]].push_back(m);
    for (const auto& group : groups) {
        if (group.empty()) continue;
        std::vector<float> samples;
        samples.reserve((end_frame-first_frame)*group.size());
        for (size_t f=first_frame;f<end_frame;++f)
            for (size_t m : group) samples.push_back(values[f*member_count+m]);
        MetricStatistics summary;
        if (!compute_statistics(&summary,samples,1,angular)) return false;
        for (size_t m : group) (*out)[m]=summary.members[0];
    }
    return true;
}

std::string csv_quote(const std::string& text) {
    std::string out = "\"";
    for (char c : text) { out += c; if (c == '"') out += '"'; }
    return out + '"';
}

bool StateSummary::build(const std::vector<uint8_t>& codes) {
    codes_.clear();
    levels_.clear();
    for (uint8_t c : codes) if (c >= 32) {
        MD_LOG_ERROR("Lavery: categorical display code must be below 32");
        return false;
    }
    codes_ = codes;
    if (codes.empty()) return true;
    levels_.emplace_back((codes.size()+31)/32, 0);
    for (size_t f = 0; f < codes.size(); ++f) levels_[0][f/32] |= 1u << codes[f];
    while (levels_.back().size() > 1) {
        const size_t n = levels_.back().size();
        std::vector<uint32_t> next((n+3)/4, 0);
        for (size_t i = 0; i < n; ++i) next[i/4] |= levels_.back()[i];
        levels_.push_back(std::move(next));
    }
    return true;
}

uint32_t StateSummary::mask(size_t first, size_t end) const {
    if (first > end || end > codes_.size()) {
        MD_LOG_ERROR("Lavery: invalid categorical display range");
        return 0;
    }
    uint32_t result = 0;
    while (first < end) {
        if (first%32 || end-first < 32) { result |= 1u << codes_[first++]; continue; }
        size_t level = 0, width = 32;
        while (level+1 < levels_.size() && first%(width*4) == 0 && end-first >= width*4) {
            width *= 4;
            ++level;
        }
        result |= levels_[level][first/width];
        first += width;
    }
    return result;
}

const char* csv_header() {
    return "run,frame,time,time_unit,class,member,label,pair_a,pair_b,parameter,unit,value,valid,quality_flags,quality_reason,mean,sd,zscore,sigma,parameter_selected,parameter_outlier,member_outlier,selected_mask,baseline,baseline_begin,baseline_end,reference_scope,reference_source\n";
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
    for (uint16_t bit = 1; bit && bit <= Quality_Last; bit <<= 1) {
        if (s.quality & bit) {
            if (!reason.empty()) reason += "; ";
            reason += quality_description(bit);
        }
    }
    row += csv_quote(reason.empty() ? "Good" : reason);
    if (!s.include_reference) {
        row += ",,,,,,,,,,,,,\n";
        return row;
    }
    const auto ref = s.baseline.reference(s.value);
    snprintf(buf, sizeof(buf), ",%.17g,%.17g,%.9g,%.9g,%d,%d,%d,%u,%s,%.17g,%.17g,%s,",
        ref.count ? ref.mean : std::numeric_limits<double>::quiet_NaN(),
        ref.count ? ref.sd : std::numeric_limits<double>::quiet_NaN(),
        s.baseline.zscore(s.value), s.detection_enabled ? s.sigma : std::numeric_limits<float>::quiet_NaN(),
        s.detection_enabled && (s.selected_mask & (1u << (s.metric - class_first_metric(metric_class(s.metric))))) ? 1 : 0,
        s.detection_enabled && s.baseline.is_outlier(s.value, s.sigma) ? 1 : 0,
        s.detection_enabled && s.member_outlier ? 1 : 0, s.detection_enabled ? s.selected_mask : 0,
        s.baseline_method == Baseline_MedianMAD ? "median_mad" : "mean_sd", s.baseline_begin, s.baseline_end,
        s.reference_scope == Reference_HelixHistory ? "helix_history" : "own_history");
    row += std::string(buf)+csv_quote(s.reference_source)+"\n";
    return row;
}

const char* csv_curves_trajectory_header() {
    return "run,frame,helix,quantity,member,level,x_displacement,y_displacement,inclination,tip,axis_bend,rise,twist,W12,D12,W21,D21,axis_quality,groove_quality\n";
}

std::string csv_curves_trajectory_frame(const std::string& run, const CurvesTrajectoryAnalysis& trajectory,
                                        size_t frame, bool include_header) {
    if (run.empty() || !trajectory.frames || frame >= trajectory.frames ||
        trajectory.pair_axis.size() != trajectory.frames * trajectory.pairs ||
        trajectory.step_axis.size() != trajectory.frames * trajectory.steps ||
        trajectory.axis_quality.size() != trajectory.frames * trajectory.helices.size() ||
        trajectory.groove_quality.size() != trajectory.frames * trajectory.helices.size() ||
        trajectory.grooves.size() % trajectory.frames) {
        MD_LOG_ERROR("Lavery: invalid Curves+ trajectory CSV input");
        return {};
    }
    const size_t groove_members = trajectory.grooves.size() / trajectory.frames;
    std::string output;
    if (include_header) output = csv_curves_trajectory_header();
    auto number = [](float value) {
        if (!std::isfinite(value)) return std::string{};
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%.9g", value);
        return std::string(buffer);
    };
    auto row = [&](size_t helix, const char* quantity, const std::string& member, const std::string& level,
                   const std::string& x, const std::string& y, const std::string& inclination,
                   const std::string& tip, const std::string& bend, const std::string& rise,
                   const std::string& twist, const std::string& width12, const std::string& depth12,
                   const std::string& width21, const std::string& depth21) {
        const size_t quality = frame * trajectory.helices.size() + helix;
        char indices[64];
        snprintf(indices, sizeof(indices), ",%zu,%zu,", frame, helix + 1);
        output += csv_quote(run) + indices + quantity + "," + csv_quote(member) + "," + level + "," +
            x + "," + y + "," + inclination + "," + tip + "," + bend + "," + rise + "," + twist + "," +
            width12 + "," + depth12 + "," + width21 + "," + depth21 + "," +
            std::to_string(trajectory.axis_quality[quality]) + "," +
            std::to_string(trajectory.groove_quality[quality]) + "\n";
    };
    for (size_t h = 0; h < trajectory.helices.size(); ++h) {
        const auto& helix = trajectory.helices[h];
        if (helix.pairs.size() < 2 || helix.steps.size() != helix.pairs.size() - 1 ||
            helix.groove_offset > groove_members || helix.groove_count > groove_members - helix.groove_offset) {
            MD_LOG_ERROR("Lavery: invalid Curves+ helix layout in trajectory CSV");
            return {};
        }
        for (size_t i = 0; i < helix.pairs.size(); ++i) {
            const size_t pair = helix.pairs[i];
            if (pair >= trajectory.pairs) {
                MD_LOG_ERROR("Lavery: invalid pair index in Curves+ trajectory CSV");
                return {};
            }
            const auto& v = trajectory.pair_axis[frame * trajectory.pairs + pair];
            const std::string label = helix.pair_labels.size() == helix.pairs.size()
                ? helix.pair_labels[i] : "bp_" + std::to_string(pair + 1);
            row(h, "pair_axis", label, "",
                number(v.x_displacement), number(v.y_displacement), number(v.inclination),
                number(v.tip), number(v.axis_bend), "", "", "", "", "", "");
        }
        for (size_t i = 0; i < helix.steps.size(); ++i) {
            const size_t step = helix.steps[i];
            if (step >= trajectory.steps) {
                MD_LOG_ERROR("Lavery: invalid step index in Curves+ trajectory CSV");
                return {};
            }
            const auto& v = trajectory.step_axis[frame * trajectory.steps + step];
            const std::string label = helix.step_labels.size() == helix.steps.size()
                ? helix.step_labels[i] : "step_" + std::to_string(helix.pairs[i] + 1) + "_" +
                    std::to_string(helix.pairs[i + 1] + 1);
            row(h, "inter_axis", label, "",
                "", "", "", "", "", number(v.rise), number(v.twist), "", "", "", "");
        }
        for (size_t i = 0; i < helix.groove_count; ++i) {
            const auto& v = trajectory.grooves[frame * groove_members + helix.groove_offset + i];
            row(h, "groove", "sample_" + std::to_string(i + 1), number(v.level),
                "", "", "", "", "", "", "", number(v.width12), number(v.depth12),
                number(v.width21), number(v.depth21));
        }
    }
    return output;
}

double sample_cell_edge(size_t edge, const float* times, size_t count) {
    if (!count || edge>count) {
        MD_LOG_ERROR("Lavery: invalid heatmap sample edge");
        return NAN;
    }
    if (!times) return (double)edge-.5;
    if (count==1) return times[0]+(edge ? .5 : -.5);
    if (!edge) return (double)times[0]-(times[1]-(double)times[0])*.5;
    if (edge==count) return (double)times[count-1]+(times[count-1]-(double)times[count-2])*.5;
    return ((double)times[edge-1]+times[edge])*.5;
}

bool sequence_export_member(int cls, size_t member, const std::vector<StepIndex>& steps,
    size_t bp_first, size_t bp_last, int partner) {
    if (cls<0 || cls>=Class_Count || !bp_first || bp_last<bp_first || partner<0 || partner>2 ||
        (cls==Class_Step && member>=steps.size())) {
        MD_LOG_ERROR("Lavery: invalid sequence export selection");
        return false;
    }
    if (cls==Class_Nucleotide && partner && member%2!=(size_t)partner-1) return false;
    const size_t first=cls==Class_Step ? steps[member].a+1 : cls==Class_Nucleotide ? member/2+1 : member+1;
    const size_t last=cls==Class_Step ? steps[member].b+1 : first;
    return first>=bp_first && last<=bp_last;
}

bool sequence_heatmap_row(double position, int cls, const std::vector<StepIndex>& steps, double* row) {
    if (!row || !std::isfinite(position) || cls<0 || cls>=Class_Count) {
        MD_LOG_ERROR("Lavery: invalid sequence landmark mapping");
        return false;
    }
    if (cls!=Class_Step) {
        *row=cls==Class_Nucleotide ? (position-1)*2+.5 : position-1;
        return true;
    }
    const auto coordinate=[](const StepIndex& step) { return ((double)step.a+step.b)*.5+1; };
    for (size_t s=0;s<steps.size();++s) {
        const double x=coordinate(steps[s]);
        if (position==x) { *row=(double)s; return true; }
        if (s && position<x && position>coordinate(steps[s-1]) && steps[s-1].b==steps[s].a) {
            *row=(double)s-1+(position-coordinate(steps[s-1]))/(x-coordinate(steps[s-1]));
            return true;
        }
    }
    return false;
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
            if (!std::isfinite(value) || !members[m].reference(value).count) continue;
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
            const int32_t comp = bb.segment.res_idx[s];
            const int32_t c1 = bb.segment.atoms[s].c1;
            if (comp < 0 || c1 < 0) continue;
            BaseTopology base;
            base.component = (uint32_t)comp;
            base.strand = r;
            base.chain_order = s - bb.range.offset[r];
            base.glycosidic_atom = c1;
            const auto range = md_system_residue_atom_range(&sys, comp);
            // Like the cartoon, identify base rings by their bonds to the sugar,
            // not by per-atom nucleobase flags.
            std::vector<uint32_t> ring_ids;
            for (uint32_t ring = 0; ring < md_ring_data_count(&sys.ring); ++ring) {
                const int32_t* atoms = md_ring_atoms(&sys.ring, ring);
                const size_t n = md_ring_size(&sys.ring, ring);
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
                const int32_t* atoms = md_ring_atoms(&sys.ring, ring);
                for (size_t k = 0; k < md_ring_size(&sys.ring, ring); ++k) {
                    if (md_atom_atomic_number(&sys.atom, atoms[k]) == 7 &&
                        md_bond_find(&sys.bond, atoms[k], c1) != -1) nitrogen = atoms[k];
                }
            }
            if (nitrogen < 0) continue;
            auto atom_named = [&](const char* name) -> int32_t {
                for (uint32_t atom = range.beg; atom < range.end; ++atom) {
                    if (md_str_eq_cstr(md_atom_name(&sys.atom, atom), name)) return (int32_t)atom;
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
                    const int32_t* atoms = md_ring_atoms(&sys.ring, ring);
                    const size_t n = md_ring_size(&sys.ring, ring);
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
        frame.groove_reference = fitted(3);
        if (valid_frame(frame)) {
            out[i] = frame;
            if (quality) (*quality)[i] = Quality_Good;
        }
    }
    return true;
}

bool extract_3dna_base_frames(std::vector<BaseFrame>& out, const md_system_t& sys,
                              const md_system_state_t& state, const std::vector<BaseTopology>& topology,
                              std::vector<uint16_t>* quality) {
    out.assign(topology.size(), BaseFrame{});
    if (quality) quality->assign(topology.size(), Quality_DegenerateBase);
    for (size_t i = 0; i < topology.size(); ++i) {
        const auto& base = topology[i];
        if (base.reference < 0 || (size_t)base.reference >= std::size(references)) {
            if (quality) (*quality)[i] = Quality_UnsupportedBase;
            continue;
        }
        const size_t count = references[base.reference].count - 1;
        if (base.ring.size() < 3 || base.atoms.empty() || base.fit_atoms.size() != count + 1) {
            if (quality) (*quality)[i] = Quality_MissingAtoms;
            continue;
        }
        size_t ref_index = (size_t)base.reference;
        if (ref_index == 3) {
            const md_str_t residue = md_system_residue_name(&sys, base.component);
            std::string name(residue.ptr, residue.len);
            if (name.size() > 1 && (name.back() == '3' || name.back() == '5')) name.pop_back();
            if (name == "U" || name == "RU" || name == "DU" || name == "URA") ref_index = 4;
            else if (name != "T" && name != "RT" && name != "DT" && name != "THY") {
                if (quality) (*quality)[i] = Quality_UnsupportedBase;
                continue;
            }
        }
        const vec3_t anchor = md_state_coord(&state, base.ring[0]);
        auto position = [&](uint32_t atom) {
            vec3_t delta = vec3_sub(md_state_coord(&state, atom), anchor);
            md_util_min_image_vec3(&delta, 1, &state.unitcell);
            return vec3_add(anchor, delta);
        };
        vec3_t center = {};
        for (uint32_t atom : base.atoms) center = vec3_add(center, position(atom));
        center = vec3_mul1(center, 1.0f / (float)base.atoms.size());
        vec3_t actual[9] = {}, centers[2] = {};
        bool finite = true;
        for (size_t k = 0; k < count; ++k) {
            actual[k] = position(base.fit_atoms[k + 1]);
            finite &= std::isfinite(actual[k].x) && std::isfinite(actual[k].y) && std::isfinite(actual[k].z);
            centers[0] = vec3_add(centers[0], dna_references[ref_index][k]);
            centers[1] = vec3_add(centers[1], actual[k]);
        }
        if (!finite) {
            if (quality) (*quality)[i] = Quality_NonfiniteCoordinates;
            continue;
        }
        if (vec3_length(vec3_cross(vec3_sub(actual[0], actual[1]), vec3_sub(actual[2], actual[1]))) < 1.0e-4f) continue;
        centers[0] = vec3_mul1(centers[0], 1.0f / (float)count);
        centers[1] = vec3_mul1(centers[1], 1.0f / (float)count);
        const vec3_t* coords[] = {dna_references[ref_index], actual};
        const mat3_t fit = mat3_transpose(mat3_optimal_rotation(coords, nullptr, nullptr, count, centers));
        BaseFrame frame = {vec3_sub(centers[1], mat3_mul_vec3(fit, centers[0])),
                           mat3_mul_vec3(fit, {1, 0, 0}), mat3_mul_vec3(fit, {0, 1, 0}),
                           mat3_mul_vec3(fit, {0, 0, 1}), base.component, base.strand, center};
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

const char* pair_type_name(PairType type) {
    switch (type) {
    case PairType_WatsonCrick: return "Watson-Crick";
    case PairType_Wobble: return "Wobble (G-T/U)";
    case PairType_Hoogsteen: return "Hoogsteen (A-T/U)";
    case PairType_ReverseWatsonCrick: return "Reverse Watson-Crick (A-T/U)";
    default: return "Unsupported";
    }
}

const char* pair_type_tag(PairType type) {
    switch (type) {
    case PairType_Wobble: return " [wobble]";
    case PairType_Hoogsteen: return " [Hoogsteen]";
    case PairType_ReverseWatsonCrick: return " [reverse WC]";
    default: return "";
    }
}

bool detect_partner_switches(std::vector<uint8_t>* out, const std::vector<BaseFrame>& frames,
    const std::vector<Pair>& pairs, const std::vector<uint16_t>& geometry, const md_unitcell_t* cell) {
    if (!out || geometry.size() != pairs.size()) {
        MD_LOG_ERROR("Lavery: invalid partner-switch input");
        return false;
    }
    constexpr uint16_t broken = Quality_Separation | Quality_Alignment | Quality_PlaneOffset;
    std::vector<int64_t> base_pair(frames.size(), -1);
    for (size_t p = 0; p < pairs.size(); ++p) {
        if (pairs[p].a >= frames.size() || pairs[p].b >= frames.size()) {
            MD_LOG_ERROR("Lavery: invalid pair index for partner-switch detection");
            return false;
        }
        base_pair[pairs[p].a] = base_pair[pairs[p].b] = (int64_t)p;
    }
    out->assign(pairs.size(), 0);
    for (size_t p = 0; p < pairs.size(); ++p) {
        if (!(geometry[p] & broken) || (geometry[p] & Quality_InvalidBase)) continue;
        for (uint32_t base : {pairs[p].a, pairs[p].b}) {
            const uint32_t own = base == pairs[p].a ? pairs[p].b : pairs[p].a;
            for (uint32_t other = 0; other < frames.size() && !(*out)[p]; ++other) {
                if (other == base || other == own || frames[other].strand == frames[base].strand ||
                    frames[other].component == frames[base].component) continue;
                const int64_t q = base_pair[other];
                if (q >= 0 && !(geometry[q] & broken)) continue;
                if (pair_is_present(frames[base], frames[other], cell)) (*out)[p] = 1;
            }
        }
    }
    return true;
}

bool compute_base_pair(Values* out, BasePairFrame* pair_frame, const BaseFrame& a, const BaseFrame& b, bool reversed) {
    if (!out || !pair_frame) return false;
    BaseFrame aligned = b;
    if (reversed) aligned.x = vec3_mul1(b.x, -1.0f);
    else aligned.z = vec3_mul1(b.z, -1.0f);
    aligned.y = vec3_mul1(b.y, -1.0f);
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
    compute_helical_step(out, a, b);
    return true;
}

std::vector<int8_t> curves_pair_inversions(const std::vector<BasePairFrame>& frames,
    const std::vector<uint8_t>& valid, const std::vector<StepIndex>& steps, const md_unitcell_t* cell) {
    std::vector<int8_t> inverted(frames.size(), -1);
    if (valid.size() != frames.size()) {
        MD_LOG_ERROR("Lavery: invalid Curves+ inversion frame validity");
        return inverted;
    }
    for (const auto& chain : helix_chains(frames.size(), steps)) {
        for (size_t i = 0; i + 1 < chain.size(); ++i) {
            const auto a = chain[i], b = chain[i + 1];
            if (!valid[a] || !valid[b]) continue;
            vec3_t delta = vec3_sub(frames[b].origin, frames[a].origin);
            if (cell) md_util_min_image_vec3(&delta, 1, cell);
            const float dot = vec3_dot(delta, frames[b].z);
            if (std::isfinite(dot)) inverted[a] = dot < 0.0f;
        }
        inverted[chain.back()] = inverted[chain[chain.size() - 2]];
    }
    return inverted;
}

bool curves_duplex_axis(std::vector<CurvesAxisPoint>* out, const std::vector<BaseFrame>& strand1,
                        const std::vector<BaseFrame>& strand2, const std::vector<BasePairFrame>& midpoints) {
    if (!out || midpoints.size() < 2 || strand1.size() != midpoints.size() || strand2.size() != midpoints.size()) {
        MD_LOG_ERROR("Lavery: invalid input for duplex axis");
        return false;
    }
    const size_t n = midpoints.size();
    auto finite = [](vec3_t v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    std::vector<vec3_t> directions(n), points(n);
    std::vector<size_t> counts(n, 0);
    for (size_t i = 0; i < n; ++i) {
        const auto& m = midpoints[i];
        if (!valid_frame({m.origin, m.x, m.y, m.z}) || !valid_frame(strand1[i]) || !valid_frame(strand2[i]))
            return false;
    }
    for (size_t i = 0; i + 1 < n; ++i) {
        for (const auto* strand : {&strand1, &strand2}) {
            const BaseFrame& a = (*strand)[i];
            const BaseFrame& b = (*strand)[i+1];
            const vec3_t skew = vec3_mul1(vec3_add(vec3_add(vec3_cross(a.x, b.x),
                vec3_cross(a.y, b.y)), vec3_cross(a.z, b.z)), 0.5f);
            const float sine = vec3_length(skew);
            const float cosine = std::clamp((vec3_dot(a.x, b.x) + vec3_dot(a.y, b.y) +
                                            vec3_dot(a.z, b.z) - 1.0f) * 0.5f, -1.0f, 1.0f);
            // A vanishing or half-turn rotation does not define a numerically stable screw axis.
            if (sine < 1.0e-6f || cosine < -0.999999f) return false;
            const vec3_t axis = vec3_div1(skew, sine);
            const vec3_t displacement = vec3_sub(b.origin, a.origin);
            const vec3_t perpendicular = vec3_sub(displacement, vec3_mul1(axis, vec3_dot(displacement, axis)));
            const float cotangent = (1.0f + cosine) / sine;
            const vec3_t point = vec3_add(vec3_mul1(vec3_add(a.origin, b.origin), 0.5f),
                                           vec3_mul1(vec3_cross(axis, perpendicular), 0.5f * cotangent));
            if (!finite(axis) || !finite(point)) return false;
            for (size_t j : {i, i+1}) {
                directions[j] = vec3_add(directions[j], axis);
                points[j] = vec3_add(points[j], vec3_add(point,
                    vec3_mul1(axis, vec3_dot(vec3_sub(midpoints[j].origin, point), axis))));
                ++counts[j];
            }
        }
    }
    std::vector<CurvesAxisPoint> local(n);
    for (size_t i = 0; i < n; ++i) {
        const vec3_t direction = normalize_or_zero(directions[i]);
        if (vec3_length(direction) < 0.5f || !counts[i]) return false;
        const vec3_t mean = vec3_div1(points[i], (float)counts[i]);
        local[i] = {vec3_add(mean, vec3_mul1(direction, vec3_dot(vec3_sub(midpoints[i].origin, mean), direction))),
                    direction};
        if (!finite(local[i].origin)) return false;
    }
    std::vector<CurvesAxisPoint> result(n);
    for (size_t i = 0; i < n; ++i) {
        vec3_t direction = {}, point = {};
        float weight_sum = 0;
        const size_t first = i > 4 ? i - 4 : 0;
        const size_t last = std::min(n - 1, i + 4);
        for (size_t j = first; j <= last; ++j) {
            const float offset = (float)((int64_t)j - (int64_t)i);
            const float weight = 1.0f - offset * offset / 25.0f;
            direction = vec3_add(direction, vec3_mul1(local[j].direction, weight));
            const vec3_t projected = vec3_add(local[j].origin, vec3_mul1(local[j].direction,
                vec3_dot(vec3_sub(midpoints[i].origin, local[j].origin), local[j].direction)));
            point = vec3_add(point, vec3_mul1(projected, weight));
            weight_sum += weight;
        }
        direction = normalize_or_zero(direction);
        if (vec3_length(direction) < 0.5f || weight_sum <= 0) return false;
        point = vec3_div1(point, weight_sum);
        result[i] = {vec3_add(point, vec3_mul1(direction,
            vec3_dot(vec3_sub(midpoints[i].origin, point), direction))), direction};
        if (!finite(result[i].origin)) return false;
    }
    *out = std::move(result);
    return true;
}

static bool aligned_curves_axis_frame(BasePairFrame* out, vec3_t* rotation,
                                       const BasePairFrame& m, const CurvesAxisPoint& a) {
    const float length = vec3_length(a.direction);
    if (!valid_frame({m.origin, m.x, m.y, m.z}) ||
        !std::isfinite(a.origin.x) || !std::isfinite(a.origin.y) || !std::isfinite(a.origin.z) ||
        !std::isfinite(length) || std::fabs(length - 1.0f) > 1.0e-3f) return false;
    const vec3_t cross = vec3_cross(m.z, a.direction);
    const float sine = vec3_length(cross);
    const float cosine = std::clamp(vec3_dot(m.z, a.direction), -1.0f, 1.0f);
    if (sine < 1e-6f && cosine < 0) return false;
    const float angle = std::atan2(sine, cosine);
    const vec3_t hinge = sine > 1e-6f ? vec3_div1(cross, sine) : vec3_t{1, 0, 0};
    const vec3_t x = angle > 1e-6f ? quat_mul_vec3(quat_axis_angle(hinge, angle), m.x) : m.x;
    *out = {a.origin, x, vec3_cross(a.direction, x), a.direction};
    *rotation = vec3_mul1(hinge, -angle * RAD_TO_DEG);
    return true;
}

bool curves_pair_axis_parameters(std::vector<CurvesPairAxisParameters>* out,
                                 const std::vector<BasePairFrame>& midpoints,
                                 const std::vector<CurvesAxisPoint>& axis,
                                 const std::vector<int8_t>& inversions) {
    if (!out || midpoints.empty() || axis.size() != midpoints.size() || inversions.size() != midpoints.size()) {
        MD_LOG_ERROR("Lavery: invalid input for pair-axis parameters");
        return false;
    }
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::vector<CurvesPairAxisParameters> result;
    result.reserve(midpoints.size());
    for (size_t i = 0; i < midpoints.size(); ++i) {
        const auto& m = midpoints[i];
        const auto& a = axis[i];
        BasePairFrame frame;
        vec3_t rotation;
        if (inversions[i] < 0 || inversions[i] > 1 || !aligned_curves_axis_frame(&frame, &rotation, m, a))
            return false;
        const vec3_t delta = vec3_sub(m.origin, a.origin);
        CurvesPairAxisParameters p = {vec3_dot(delta, frame.x), vec3_dot(delta, frame.y),
                                      vec3_dot(rotation, frame.x), vec3_dot(rotation, frame.y), nan};
        if (inversions[i]) {
            p.x_displacement = -p.x_displacement;
            p.inclination = -p.inclination;
            p.tip = (float)wrap_degrees((double)p.tip - 180.0);
        }
        if (i) {
            const float dot = std::clamp(vec3_dot(axis[i-1].direction, a.direction), -1.0f, 1.0f);
            p.axis_bend = std::acos(dot) * RAD_TO_DEG;
        }
        if (!std::isfinite(p.x_displacement) || !std::isfinite(p.y_displacement) ||
            !std::isfinite(p.inclination) || !std::isfinite(p.tip) ||
            (i && !std::isfinite(p.axis_bend))) return false;
        result.push_back(p);
    }
    *out = std::move(result);
    return true;
}

bool curves_step_axis_parameters(std::vector<CurvesStepAxisParameters>* out,
                                 const std::vector<BasePairFrame>& midpoints,
                                 const std::vector<CurvesAxisPoint>& axis,
                                 const std::vector<int8_t>& inversions) {
    if (!out || midpoints.size() < 2 || axis.size() != midpoints.size() || inversions.size() != midpoints.size()) {
        MD_LOG_ERROR("Lavery: invalid input for inter-axis parameters");
        return false;
    }
    std::vector<BasePairFrame> frames(midpoints.size());
    for (size_t i = 0; i < frames.size(); ++i) {
        vec3_t rotation;
        if (inversions[i] < 0 || inversions[i] > 1 ||
            !aligned_curves_axis_frame(&frames[i], &rotation, midpoints[i], axis[i])) return false;
    }
    std::vector<CurvesStepAxisParameters> result;
    result.reserve(frames.size() - 1);
    for (size_t i = 0; i + 1 < frames.size(); ++i) {
        const auto& a = frames[i];
        const auto& b = frames[i+1];
        BasePairFrame frame;
        float translation[3], rotation[3];
        if (!rigid_parameters(&frame, translation, rotation,
                              {a.origin, a.x, a.y, a.z}, {b.origin, b.x, b.y, b.z})) return false;
        const float sign = inversions[i] ? -1.0f : 1.0f;
        result.push_back({sign * translation[2], (float)wrap_degrees(sign * rotation[2])});
    }
    *out = std::move(result);
    return true;
}

bool curves_groove_backbone(std::vector<vec3_t>* out, const std::vector<vec3_t>& phosphates) {
    if (!out || phosphates.size() < 3) {
        MD_LOG_ERROR("Lavery: invalid input for groove backbone");
        return false;
    }
    auto finite = [](vec3_t v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    const size_t n = phosphates.size();
    for (const auto& p : phosphates) if (!finite(p)) {
        MD_LOG_ERROR("Lavery: nonfinite phosphate for groove backbone");
        return false;
    }
    std::vector<vec3_t> tangents(n);
    for (size_t i = 1; i + 1 < n; ++i) {
        const vec3_t u = vec3_sub(phosphates[i], phosphates[i-1]);
        const vec3_t v = vec3_sub(phosphates[i+1], phosphates[i-1]);
        const vec3_t normal = vec3_cross(u, v);
        const float normal2 = vec3_dot(normal, normal);
        if (!std::isfinite(normal2) || normal2 < 1e-10f) {
            MD_LOG_ERROR("Lavery: degenerate phosphate geometry for groove backbone");
            return false;
        }
        const vec3_t center = vec3_add(phosphates[i-1], vec3_div1(
            vec3_add(vec3_mul1(vec3_cross(v, normal), vec3_dot(u, u)),
                     vec3_mul1(vec3_cross(normal, u), vec3_dot(v, v))), 2.0f * normal2));
        auto tangent = [&](size_t index) {
            return normalize_or_zero(vec3_cross(normal, vec3_sub(phosphates[index], center)));
        };
        tangents[i] = tangent(i);
        if (i == 1) tangents[0] = tangent(0);
        if (i + 2 == n) tangents[n-1] = tangent(n-1);
    }
    std::vector<vec3_t> result;
    result.reserve((n - 1) * 10 + 1);
    for (size_t i = 0; i + 1 < n; ++i) {
        const vec3_t a = phosphates[i], b = phosphates[i+1];
        const float length = vec3_length(vec3_sub(b, a));
        if (!std::isfinite(length) || length < 1e-6f ||
            vec3_length(tangents[i]) < 0.5f || vec3_length(tangents[i+1]) < 0.5f) {
            MD_LOG_ERROR("Lavery: invalid phosphate interval for groove backbone");
            return false;
        }
        for (int j = 0; j < 10; ++j) {
            const float t = (float)j / 10.0f, t2 = t*t, t3 = t2*t;
            const vec3_t p = vec3_add(
                vec3_add(vec3_mul1(a, 2*t3 - 3*t2 + 1), vec3_mul1(b, -2*t3 + 3*t2)),
                vec3_add(vec3_mul1(tangents[i], length * (t3 - 2*t2 + t)),
                         vec3_mul1(tangents[i+1], length * (t3 - t2))));
            if (!finite(p)) {
                MD_LOG_ERROR("Lavery: nonfinite interpolated groove backbone");
                return false;
            }
            result.push_back(p);
        }
    }
    result.push_back(phosphates.back());
    *out = std::move(result);
    return true;
}

bool curves_grooves(std::vector<CurvesGrooveSample>* out,
                    const std::vector<vec3_t>& backbone1, const std::vector<vec3_t>& backbone2,
                    const std::vector<BasePairFrame>& midpoints, const std::vector<CurvesAxisPoint>& axis,
                    const std::vector<vec3_t>& depth1, const std::vector<vec3_t>& depth2) {
    const size_t n = midpoints.size();
    if (!out || n < 4 || axis.size() != n || depth1.size() != n || depth2.size() != n ||
        backbone1.size() != (n - 2) * 10 + 1 || backbone2.size() != backbone1.size()) {
        MD_LOG_ERROR("Lavery: invalid input for Curves+ groove analysis");
        return false;
    }
    auto finite = [](vec3_t v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    for (const auto* backbone : {&backbone1, &backbone2})
        for (vec3_t p : *backbone) if (!finite(p)) {
            MD_LOG_ERROR("Lavery: nonfinite Curves+ groove backbone");
            return false;
        }
    std::vector<BasePairFrame> axis_frames(n);
    for (size_t i = 0; i < n; ++i) {
        vec3_t unused;
        if (!finite(depth1[i]) || !finite(depth2[i]) ||
            !aligned_curves_axis_frame(&axis_frames[i], &unused, midpoints[i], axis[i])) {
            MD_LOG_ERROR("Lavery: invalid Curves+ groove depth reference or axis frame");
            return false;
        }
    }
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const int64_t count = (int64_t)backbone1.size();
    std::vector<CurvesGrooveSample> result;
    result.reserve(2 * n - 3);
    for (size_t q = 0; q < 2 * n - 3; ++q) {
        const bool pair_level = (q % 2) != 0;
        const size_t lower = pair_level ? (q + 1) / 2 : q / 2;
        vec3_t axis_origin = axis_frames[lower].origin;
        vec3_t dyad = axis_frames[lower].x;
        vec3_t base_center = vec3_mul1(vec3_add(depth1[lower], depth2[lower]), 0.5f);
        if (!pair_level) {
            axis_origin = vec3_mul1(vec3_add(axis_origin, axis_frames[lower+1].origin), 0.5f);
            dyad = normalize_or_zero(vec3_add(dyad, axis_frames[lower+1].x));
            base_center = vec3_mul1(vec3_add(base_center,
                vec3_mul1(vec3_add(depth1[lower+1], depth2[lower+1]), 0.5f)), 0.5f);
            if (vec3_length(dyad) < 0.5f) {
                MD_LOG_ERROR("Lavery: degenerate Curves+ groove dyad");
                return false;
            }
        }
        CurvesGrooveSample sample = {1.5f + 0.5f * (float)q, nan, nan, nan, nan};
        float best[2] = {100.0f, 100.0f};
        const int64_t center = (int64_t)q * 5;
        for (int64_t i = center - 50; i <= center + 50; ++i) {
            const int64_t j = 2 * center - i;
            if (i < 2 || j < 2 || i >= count - 2 || j >= count - 2) continue;
            auto distance = [&](int64_t a, int64_t b) {
                return vec3_length(vec3_sub(backbone1[(size_t)a], backbone2[(size_t)b]));
            };
            const float width = distance(i, j);
            if (!(width < distance(i-1, j+1) && width < distance(i+1, j-1)) || width - 5.8f >= 52.0f)
                continue;
            const vec3_t center_point = vec3_mul1(vec3_add(backbone1[(size_t)i], backbone2[(size_t)j]), 0.5f);
            const float side = vec3_dot(vec3_sub(center_point, axis_origin), vec3_mul1(dyad, -1.0f));
            if (side == 0) continue;
            const int which = side > 0 ? 0 : 1;
            if (width >= best[which]) continue;
            best[which] = width;
            const float depth = vec3_length(vec3_sub(center_point, base_center)) - 3.5f;
            if (which == 0) { sample.width12 = width - 5.8f; sample.depth12 = depth; }
            else { sample.width21 = width - 5.8f; sample.depth21 = depth; }
        }
        result.push_back(sample);
    }
    *out = std::move(result);
    return true;
}

bool landmark_frames(std::vector<BasePairFrame>& pair_frames, std::vector<BasePairFrame>& step_frames,
                     const std::vector<BaseFrame>& bases, const std::vector<Pair>& pairs,
                     const std::vector<StepIndex>& steps, const md_unitcell_t* cell) {
    pair_frames.clear();
    step_frames.clear();
    for (const auto& pair : pairs) if (pair.a >= bases.size() || pair.b >= bases.size()) {
        MD_LOG_ERROR("Lavery: invalid base index for landmark geometry");
        return false;
    }
    for (const auto& step : steps) if (step.a >= pairs.size() || step.b >= pairs.size()) {
        MD_LOG_ERROR("Lavery: invalid pair index for landmark geometry");
        return false;
    }
    pair_frames.resize(pairs.size());
    step_frames.resize(steps.size());
    for (size_t p = 0; p < pairs.size(); ++p) {
        const auto& pair = pairs[p];
        const auto& a = bases[pair.a];
        auto b = bases[pair.b];
        vec3_t delta = vec3_sub(b.origin, a.origin);
        if (cell) md_util_min_image_vec3(&delta, 1, cell);
        b.origin = vec3_add(a.origin, delta);
        Values unused;
        BasePairFrame frame;
        if (compute_base_pair(&unused, &frame, a, b, pair.reversed)) pair_frames[p] = frame;
    }
    for (size_t s = 0; s < steps.size(); ++s) {
        const auto& a = pair_frames[steps[s].a];
        auto b = pair_frames[steps[s].b];
        vec3_t delta = vec3_sub(b.origin, a.origin);
        if (cell) md_util_min_image_vec3(&delta, 1, cell);
        b.origin = vec3_add(a.origin, delta);
        BasePairFrame midpoint;
        float translation[3], rotation[3];
        if (rigid_parameters(&midpoint, translation, rotation,
            {a.origin, a.x, a.y, a.z}, {b.origin, b.x, b.y, b.z})) step_frames[s] = midpoint;
    }
    return true;
}

static vec3_t rotate_about(vec3_t v, vec3_t unit_axis, float radians) {
    const float c = std::cos(radians), s = std::sin(radians);
    return vec3_add(vec3_add(vec3_mul1(v, c), vec3_mul1(vec3_cross(unit_axis, v), s)),
        vec3_mul1(unit_axis, vec3_dot(unit_axis, v) * (1.0f - c)));
}

// Angle (radians) from a to b after projecting both perpendicular to unit reference; positive about reference.
static float signed_angle(vec3_t a, vec3_t b, vec3_t reference) {
    a = vec3_sub(a, vec3_mul1(reference, vec3_dot(a, reference)));
    b = vec3_sub(b, vec3_mul1(reference, vec3_dot(b, reference)));
    return std::atan2(vec3_dot(vec3_cross(a, b), reference), vec3_dot(a, b));
}

bool compute_3dna_step(Values* out, const BasePairFrame& a, const BasePairFrame& b, BasePairFrame* midpoint) {
    if (!out) return false;
    const BaseFrame fa = {a.origin, a.x, a.y, a.z}, fb = {b.origin, b.x, b.y, b.z};
    if (!valid_frame(fa) || !valid_frame(fb)) return false;
    const vec3_t hinge_raw = vec3_cross(a.z, b.z);
    const float sine = vec3_length(hinge_raw);
    const float cosine = std::clamp(vec3_dot(a.z, b.z), -1.0f, 1.0f);
    if (sine < 1.0e-6f && cosine < 0.0f) return false;
    const float bend = std::atan2(sine, cosine);
    const vec3_t hinge = sine >= 1.0e-6f ? vec3_mul1(hinge_raw, 1.0f / sine) : a.x;
    const vec3_t z = normalize_or_zero(rotate_about(b.z, hinge, -0.5f * bend));
    const vec3_t y1 = rotate_about(a.y, hinge, 0.5f * bend);
    const vec3_t y2 = rotate_about(b.y, hinge, -0.5f * bend);
    const float twist = signed_angle(y1, y2, z);
    const vec3_t y = normalize_or_zero(rotate_about(y1, z, 0.5f * twist));
    const vec3_t x = normalize_or_zero(vec3_cross(y, z));
    const vec3_t delta = vec3_sub(b.origin, a.origin);
    const float phi = bend > 1.0e-6f ? signed_angle(hinge, y, z) : 0.0f;
    out->metric[Shift] = vec3_dot(delta, x);
    out->metric[Slide] = vec3_dot(delta, y);
    out->metric[Rise] = vec3_dot(delta, z);
    out->metric[Tilt] = bend * std::sin(phi) * RAD_TO_DEG;
    out->metric[Roll] = bend * std::cos(phi) * RAD_TO_DEG;
    out->metric[Twist] = twist * RAD_TO_DEG;
    if (midpoint) *midpoint = {vec3_mul1(vec3_add(a.origin, b.origin), 0.5f), x, y, z};
    compute_helical_step(out, a, b);
    return true;
}

bool compute_helical_step(Values* out, const BasePairFrame& a, const BasePairFrame& b,
                          BasePairFrame* helical, vec3_t axis_points[2]) {
    if (!out) return false;
    for (int k = XDisplacement; k <= HelicalTwist; ++k) out->metric[k] = std::numeric_limits<float>::quiet_NaN();
    const BaseFrame fa = {a.origin, a.x, a.y, a.z}, fb = {b.origin, b.x, b.y, b.z};
    auto finite = [](vec3_t v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    if (!valid_frame(fa) || !valid_frame(fb) || !finite(a.origin) || !finite(b.origin)) return false;
    // Lu & Olson (2003) / 3DNA helical_par: the axis is perpendicular to both x and y frame differences.
    vec3_t axis = vec3_cross(vec3_sub(b.x, a.x), vec3_sub(b.y, a.y));
    float length = vec3_length(axis);
    if (length < 1.0e-6f) {
        axis = vec3_add(a.z, b.z);
        length = vec3_length(axis);
        if (length < 1.0e-6f) return false;
    }
    axis = vec3_div1(axis, length);
    // Rotate each pair frame about its hinge so that its z coincides with the axis.
    auto align = [&](const BasePairFrame& f, vec3_t* x, vec3_t* y, vec3_t* hinge) {
        *hinge = vec3_cross(axis, f.z);
        const float h = vec3_length(*hinge);
        const float tip_inclination = std::atan2(h, vec3_dot(axis, f.z));
        if (h < 1.0e-6f) { *x = f.x; *y = f.y; *hinge = {}; return 0.0f; }
        *hinge = vec3_div1(*hinge, h);
        *x = rotate_about(f.x, *hinge, -tip_inclination);
        *y = rotate_about(f.y, *hinge, -tip_inclination);
        return tip_inclination;
    };
    vec3_t x1, y1, hinge1, x2, y2, hinge2;
    const float tip_inclination = align(a, &x1, &y1, &hinge1);
    align(b, &x2, &y2, &hinge2);
    const float twist = signed_angle(y1, y2, axis);
    const vec3_t delta = vec3_sub(b.origin, a.origin);
    const float rise = vec3_dot(delta, axis);
    const float phi = vec3_length(hinge1) > 0.5f ? signed_angle(hinge1, y1, axis) : 0.0f;
    // Pair-1 origin on the axis: the two origins are related by a screw of twist about the axis.
    const vec3_t chord = vec3_sub(delta, vec3_mul1(axis, rise));
    vec3_t axis1;
    if (std::fabs(twist * RAD_TO_DEG) < 0.05f) {
        axis1 = vec3_add(a.origin, vec3_mul1(chord, 0.5f));
    } else {
        const float chord_length = vec3_length(chord);
        const vec3_t direction = chord_length > 1.0e-6f ?
            rotate_about(vec3_div1(chord, chord_length), axis, 0.5f * 3.14159265358979f - 0.5f * twist) : vec3_t{};
        axis1 = vec3_add(a.origin, vec3_mul1(direction, 0.5f * chord_length / std::sin(0.5f * twist)));
    }
    const vec3_t displacement = vec3_sub(a.origin, axis1);
    out->metric[XDisplacement] = vec3_dot(displacement, x1);
    out->metric[YDisplacement] = vec3_dot(displacement, y1);
    out->metric[HelicalRise] = rise;
    // As 3DNA: inclination tilts the pair about its x-axis, tip about its y-axis.
    out->metric[Inclination] = tip_inclination * std::sin(phi) * RAD_TO_DEG;
    out->metric[Tip] = tip_inclination * std::cos(phi) * RAD_TO_DEG;
    out->metric[HelicalTwist] = twist * RAD_TO_DEG;
    const vec3_t axis2 = vec3_add(axis1, vec3_mul1(axis, rise));
    if (axis_points) { axis_points[0] = axis1; axis_points[1] = axis2; }
    if (helical) {
        helical->origin = vec3_mul1(vec3_add(axis1, axis2), 0.5f);
        helical->z = axis;
        helical->x = vec3_normalize(vec3_add(x1, x2));
        helical->y = vec3_cross(helical->z, helical->x);
    }
    return true;
}

const char* metric_name(Metric metric) {
    static const char* names[] = {
        "Buckle", "Propeller Twist", "Opening", "Shear", "Stretch", "Stagger",
        "Tilt", "Roll", "Twist", "Shift", "Slide", "Rise", "Minor Groove", "Major Groove",
        "X-displacement", "Y-displacement", "Helical Rise", "Inclination", "Tip", "Helical Twist",
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
        "step/x_displacement", "step/y_displacement", "step/helical_rise", "step/inclination", "step/tip",
        "step/helical_twist",
        "nt/alpha", "nt/beta", "nt/gamma", "nt/delta", "nt/epsilon", "nt/zeta", "nt/chi",
        "nt/pucker_phase", "nt/pucker_amplitude", "nt/epsilon_minus_zeta",
    };
    static_assert(sizeof(paths) / sizeof(paths[0]) == Metric_Count, "metric paths");
    return metric < Metric_Count ? paths[metric] : "";
}

bool metric_is_step(Metric metric) {
    return metric >= Tilt && metric <= HelicalTwist;
}

bool metric_is_angular(Metric metric) {
    return metric == Buckle || metric == Propeller || metric == Opening ||
           metric == Tilt || metric == Roll || metric == Twist ||
           metric == Inclination || metric == Tip || metric == HelicalTwist ||
           (metric >= Alpha && metric <= EpsilonZeta && metric != Amplitude);
}

bool metric_uses_degrees(Metric metric) {
    return metric_is_angular(metric) || metric == Amplitude;
}

int class_first_metric(int cls) {
    return cls == Class_Step ? (int)Tilt : cls == Class_Nucleotide ? (int)Alpha : 0;
}

int class_parameter_count(int cls) {
    return cls == Class_Pair ? 6 : cls == Class_Step ? 14 : cls == Class_Nucleotide ? 10 : 0;
}

int migrate_metric(int metric, int layout) {
    return layout < 1 && metric >= XDisplacement ? metric + (Alpha - XDisplacement) : metric;
}

uint32_t migrate_metric_mask(uint32_t mask, int layout) {
    if (layout >= 1) return mask & ALL_METRIC_PARAMETERS;
    const uint32_t low = mask & ((1u << XDisplacement) - 1u);
    return (low | ((mask >> XDisplacement) << Alpha)) & ALL_METRIC_PARAMETERS;
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

uint32_t convention_available_metric_mask(AnalysisConvention convention) {
    switch (convention) {
    case AnalysisConvention::LegacyMixed:
    case AnalysisConvention::ThreeDna:
        return ALL_METRIC_PARAMETERS;
    case AnalysisConvention::Curves:
        return ALL_METRIC_PARAMETERS & ~HELICAL_PARAMETERS &
            ~(1u << MinorGroove) & ~(1u << MajorGroove);
    }
    MD_LOG_ERROR("Lavery: unknown analysis convention for metric availability");
    return 0;
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

void fourier_pseudorotation(const float nu[5], float* phase, float* amplitude) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    if (phase) *phase = nan;
    if (amplitude) *amplitude = nan;
    for (int i = 0; i < 5; ++i) if (!std::isfinite(nu[i])) return;
    constexpr double rad = 3.14159265358979323846 / 180.0;
    double a = 0, b = 0;
    for (int j = 0; j < 5; ++j) {
        a += 0.4 * nu[j] * std::cos(144.0 * (j - 2) * rad);
        b -= 0.4 * nu[j] * std::sin(144.0 * (j - 2) * rad);
    }
    double p = std::atan2(b, a) / rad;
    if (p < 0) p += 360.0;
    if (phase) *phase = (float)p;
    if (amplitude) *amplitude = (float)std::hypot(a, b);
}

static void compute_nucleotide_impl(Values* out, const vec3_t atoms[BB_Count], const md_unitcell_t* cell,
                                    NucleotideQuality* quality, uint32_t present_atoms, PuckerDefinition pucker,
                                    bool curves_zero_phase) {
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
    if (pucker == Pucker_Fourier) fourier_pseudorotation(nu, &out->metric[Phase], &out->metric[Amplitude]);
    else pseudorotation(nu, &out->metric[Phase], &out->metric[Amplitude]);
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
    if (curves_zero_phase && out->metric[Amplitude] == 0.0f) out->metric[Phase] = 0.0f;
    if (!curves_zero_phase && out->metric[Amplitude] < 1e-4f) {
        out->metric[Phase] = std::numeric_limits<float>::quiet_NaN();
        flags[Phase-Alpha] |= Quality_DegenerateNucleotide;
    }
    const float e = out->metric[Epsilon], z = out->metric[Zeta];
    out->metric[EpsilonZeta] = std::isfinite(e) && std::isfinite(z) ? (float)wrap_degrees((double)e - z)
                                                                     : std::numeric_limits<float>::quiet_NaN();
    flags[EpsilonZeta-Alpha] = flags[Epsilon-Alpha] | flags[Zeta-Alpha];
    if (quality) *quality = flags;
}

void compute_nucleotide(Values* out, const vec3_t atoms[BB_Count], const md_unitcell_t* cell,
                        NucleotideQuality* quality, uint32_t present_atoms, PuckerDefinition pucker) {
    compute_nucleotide_impl(out, atoms, cell, quality, present_atoms, pucker, false);
}

void compute_curves_nucleotide(Values* out, const vec3_t atoms[BB_Count], const md_unitcell_t* cell,
                               NucleotideQuality* quality, uint32_t present_atoms,
                               bool previous_selected, bool next_selected) {
    if (!out || !atoms) {
        MD_LOG_ERROR("Lavery: invalid Curves+ nucleotide input");
        return;
    }
    if (!previous_selected) present_atoms &= ~(1u << BB_PrevO3);
    if (!next_selected) present_atoms &= ~((1u << BB_NextP) | (1u << BB_NextO5));
    NucleotideQuality flags;
    compute_nucleotide_impl(out, atoms, cell, &flags, present_atoms, Pucker_Fourier, true);
    BaseTopology slots;
    for (int k = 0; k < BB_Count; ++k) slots.backbone[k] = k;
    for (int id = Alpha; id <= (int)Chi; ++id) {
        const auto indices = nucleotide_torsion_atoms(slots, (Metric)id);
        for (size_t j = 1; j < indices.size(); ++j) {
            const int a = indices[j-1], b = indices[j];
            if ((present_atoms & (1u << a)) && (present_atoms & (1u << b)) &&
                finite3(atoms[a]) && finite3(atoms[b]) && vec3_length(bond(atoms[a], atoms[b], cell)) > 2.5f) {
                flags[id-Alpha] |= Quality_ReferenceBondBreak;
                out->metric[id] = std::numeric_limits<float>::quiet_NaN();
                break;
            }
        }
    }
    if (!std::isfinite(out->metric[Epsilon]) || !std::isfinite(out->metric[Zeta]))
        out->metric[EpsilonZeta] = std::numeric_limits<float>::quiet_NaN();
    flags[EpsilonZeta-Alpha] = flags[Epsilon-Alpha] | flags[Zeta-Alpha];
    if (quality) *quality = flags;
}

bool groove_span(vec3_t* delta, const vec3_t* p1, const vec3_t* p2, const vec3_t* c1, const vec3_t* c2,
                 size_t n, size_t i, size_t j, const md_unitcell_t* cell) {
    if (!delta || !p1 || !p2 || i >= n || j >= n || !finite3(p1[i]) || !finite3(p2[j])) return false;
    if (!cell || !(cell->flags & MD_UNITCELL_PBC_ALL)) {
        *delta = vec3_sub(p2[j], p1[i]);
        return true;
    }
    if (!c1 || !c2) return false;
    const int direction = i <= j ? 1 : -1;
    bool found = false;
    vec3_t span = {};
    for (size_t k = i;; k = (size_t)((long)k + direction)) {
        if (finite3(p1[k]) && finite3(p2[k]) && finite3(c1[k]) && finite3(c2[k])) {
            vec3_t result = {};
            bool valid = true;
            for (size_t m = i; m != k; m = (size_t)((long)m + direction)) {
                const size_t next = (size_t)((long)m + direction);
                if (!finite3(p1[m]) || !finite3(p1[next])) { valid = false; break; }
                result = vec3_add(result, bond(p1[m], p1[next], cell));
            }
            if (valid) {
                result = vec3_add(result, bond(p1[k], c1[k], cell));
                result = vec3_add(result, bond(c1[k], c2[k], cell));
                result = vec3_add(result, bond(c2[k], p2[k], cell));
                for (size_t m = k; m != j; m = (size_t)((long)m + direction)) {
                    const size_t next = (size_t)((long)m + direction);
                    if (!finite3(p2[m]) || !finite3(p2[next])) { valid = false; break; }
                    result = vec3_add(result, bond(p2[m], p2[next], cell));
                }
                if (valid) {
                    if (found && vec3_length(vec3_sub(result, span)) > 0.01f) return false;
                    span = result;
                    found = true;
                }
            }
        }
        if (k == j) break;
    }
    if (found) *delta = span;
    return found;
}

void groove_widths(float* minor, float* major, const vec3_t* p1, const vec3_t* p2, size_t n,
                   const md_unitcell_t* cell, const vec3_t* c1, const vec3_t* c2, GrooveWidthPolicy policy) {
    if (!minor || !major || n < 2 || !p1 || !p2) return;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    constexpr float offset = 5.8f; // phosphate van der Waals diameter, as in Curves / El Hassan & Calladine
    auto phosphate = [&](const vec3_t* p, long k, vec3_t* out) {
        if (k < 1 || k > (long)n || !finite3(p[k - 1])) return false;
        *out = p[k - 1];
        return true;
    };
    // 1-based pair index k -> P1(k), P2(k); NaN when outside [1, n] or missing. Refined (El Hassan & Calladine
    // 1998, as 3DNA): the P-P distance times the sine of its angle to the mean local backbone direction,
    // P(k+1) - P(k-1) on each strand; the direct distance when a flanking phosphate is missing.
    auto dist = [&](long i, long j) -> float {
        vec3_t a, b, a0, a1, b0, b1;
        if (!phosphate(p1, i, &a) || !phosphate(p2, j, &b)) return nan;
        vec3_t pp;
        if (!groove_span(&pp, p1, p2, c1, c2, n, (size_t)i - 1, (size_t)j - 1, cell)) return nan;
        const float direct = vec3_length(pp);
        if (!phosphate(p1, i - 1, &a0) || !phosphate(p1, i + 1, &a1) ||
            !phosphate(p2, j - 1, &b0) || !phosphate(p2, j + 1, &b1))
            return policy == Groove_3DnaRefined ? nan : direct;
        const vec3_t da = vec3_add(bond(a0, a, cell), bond(a, a1, cell));
        const vec3_t db = vec3_add(bond(b0, b, cell), bond(b, b1, cell));
        const float la = vec3_length(da), lb = vec3_length(db);
        if (la < 1.0e-6f || lb < 1.0e-6f || direct < 1.0e-6f)
            return policy == Groove_3DnaRefined ? nan : direct;
        const vec3_t mean = vec3_add(vec3_div1(da, la), vec3_div1(db, lb));
        const float lm = vec3_length(mean);
        if (lm < 1.0e-6f) return policy == Groove_3DnaRefined ? nan : direct;
        const float cosine = std::clamp(vec3_dot(mean, pp) / (lm * direct), -1.0f, 1.0f);
        return direct * std::sqrt(std::max(0.0f, 1.0f - cosine * cosine));
    };
    for (size_t s = 0; s + 1 < n; ++s) {
        for (Metric metric : {MinorGroove, MajorGroove}) {
            const auto indices = groove_phosphate_indices(metric, s, n);
            float width = 0;
            if (policy == Groove_3DnaRefined && (s < 3 || n - s < 5)) {
                (metric == MinorGroove ? minor[s] : major[s]) = nan;
                continue;
            }
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

std::vector<uint32_t> helix_member_groups(size_t pair_count, const std::vector<StepIndex>& steps, int cls) {
    if (cls < 0 || cls >= Class_Count || std::any_of(steps.begin(), steps.end(),
            [&](const StepIndex& step) { return step.a >= pair_count || step.b >= pair_count; })) {
        MD_LOG_ERROR("Lavery: invalid topology for pooled reference groups");
        return {};
    }
    std::vector<uint32_t> pairs(pair_count, UINT32_MAX);
    uint32_t group = 0;
    for (const auto& chain : helix_chains(pair_count, steps)) {
        for (uint32_t p : chain) pairs[p] = group;
        ++group;
    }
    for (auto& p : pairs) if (p == UINT32_MAX) p = group++;
    if (cls == Class_Pair) return pairs;
    std::vector<uint32_t> members;
    if (cls == Class_Nucleotide) {
        for (uint32_t p : pairs) { members.push_back(p); members.push_back(p); }
    } else {
        // Compact IDs: a chain with only one pair has no step.
        std::vector<uint32_t> step_groups(group, UINT32_MAX);
        uint32_t next = 0;
        for (const auto& step : steps) {
            auto& id = step_groups[pairs[step.a]];
            if (id == UINT32_MAX) id = next++;
            members.push_back(id);
        }
    }
    return members;
}

bool TransitionFilter::matches(int cls, size_t member, int source, int destination) const {
    if (cls != Class_Pair && cls != Class_Nucleotide) return false;
    if (scope == SelectedPair && (cls == Class_Nucleotide ? member / 2 : member) != pair) return false;
    if (scope == SelectedNucleotide && (cls != Class_Nucleotide || member != nucleotide)) return false;
    return (from < 0 || from == source) && (to < 0 || to == destination);
}

} // namespace lavery
