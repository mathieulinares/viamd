#pragma once

#include <core/md_vec_math.h>

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>

struct md_system_t;
struct md_system_state_t;
struct md_unitcell_t;

namespace lavery {

enum Metric : uint32_t {
    Buckle,
    Propeller,
    Opening,
    Shear,
    Stretch,
    Stagger,
    Tilt,
    Roll,
    Twist,
    Shift,
    Slide,
    Rise,
    Metric_Count,
};

struct BaseFrame {
    vec3_t origin = {};
    vec3_t x = {};
    vec3_t y = {};
    vec3_t z = {};
    uint32_t component = 0;
    uint32_t strand = 0;
    // Ring centroid for partner detection; the fitted reference origin is distinct.
    vec3_t center = origin;
};

struct Pair {
    uint32_t a = 0;
    uint32_t b = 0;
};

struct BaseTopology {
    uint32_t component = 0;
    uint32_t strand = 0;
    uint32_t chain_order = UINT32_MAX;
    int32_t glycosidic_atom = -1;
    std::vector<uint32_t> atoms;
    std::vector<uint32_t> ring;
    std::vector<uint32_t> fit_atoms;
    int reference = -1;
};

struct StepIndex {
    uint32_t a = 0;
    uint32_t b = 0;
};

enum SampleQuality : uint16_t {
    Quality_Good = 0,
    Quality_InvalidBase = 1 << 0,
    Quality_Separation = 1 << 1,
    Quality_Alignment = 1 << 2,
    Quality_PlaneOffset = 1 << 3,
    Quality_AmbiguousTransform = 1 << 4,
    Quality_AdjacentPair = 1 << 5,
    Quality_UnsupportedBase = 1 << 6,
    Quality_NonfiniteCoordinates = 1 << 7,
    Quality_DegenerateBase = 1 << 8,
    Quality_MissingAtoms = 1 << 9,
};

uint16_t pair_quality(const BaseFrame& a, const BaseFrame& b, const md_unitcell_t* cell = nullptr);
const char* quality_description(uint16_t flag);

struct OutlierAssessment {
    size_t valid = 0;
    uint32_t contributors = 0;
    float peak_z = 0;
    int peak_metric = -1;
};

struct MemberStatistics;
OutlierAssessment assess_outliers(const float* values, const MemberStatistics* statistics,
                                  size_t count, uint32_t selected, float sigma);

std::vector<BaseTopology> gather_bases(const md_system_t& sys);
bool extract_base_frames(std::vector<BaseFrame>& out, const md_system_t& sys,
                         const md_system_state_t& state, const std::vector<BaseTopology>& topology,
                         std::vector<uint16_t>* quality = nullptr);
std::vector<StepIndex> gather_steps(const md_system_t& sys, const std::vector<BaseTopology>& bases,
                                   const Pair* pairs, size_t pair_count);

struct BasePairFrame {
    vec3_t origin = {};
    vec3_t x = {};
    vec3_t y = {};
    vec3_t z = {};
};

struct Values {
    float metric[Metric_Count] = {};
};

struct MemberStatistics {
    double mean = 0.0;
    double sd = 0.0;
    size_t count = 0;
    float zscore(float value) const;
    bool is_outlier(float value, float sigma) const;
};

// Input is frame-major; heatmap output is member-major with row zero at highest Y.
struct MetricStatistics {
    std::vector<MemberStatistics> members;
    std::vector<float> heatmap;
};

bool compute_statistics(MetricStatistics* out, const std::vector<float>& values, size_t member_count);

struct FrameOutliers {
    size_t outliers = 0;
    size_t valid = 0;
};

struct OutlierEvent {
    size_t begin = 0, end = 0, peak = 0, peak_count = 0;
    float severity = 0;
};

std::vector<OutlierEvent> collect_outlier_events(const std::vector<FrameOutliers>& counts,
                                               const std::vector<float>& severity);
std::string csv_quote(const std::string& text);

struct CsvSample {
    std::string run, time_unit, label, pair_a, pair_b;
    size_t frame = 0, member = 0;
    double time = 0;
    Metric metric = Buckle;
    float value = 0;
    uint16_t quality = 0;
    MemberStatistics baseline;
    float sigma = 2.5f;
    uint32_t selected_mask = 0;
    bool member_outlier = false;
};

const char* csv_header();
std::string csv_sample(const CsvSample& sample);

bool compute_outlier_counts(std::vector<FrameOutliers>* out, const std::vector<float>& values,
                            const std::vector<MemberStatistics>& members, float sigma);

// Timestamps must be ordered. A null timestamp array selects frame-index coordinates.
size_t nearest_frame(double x, const float* times, size_t frame_count);

// Pairs are chosen once from the first frame and are returned in component order.
size_t detect_pairs(Pair* out, size_t cap, const BaseFrame* bases, size_t count,
                    const md_unitcell_t* cell = nullptr);
bool pair_is_present(const BaseFrame& a, const BaseFrame& b, const md_unitcell_t* cell = nullptr);

// The six intra-pair values use the paired bases' relative transform in their midpoint frame.
bool compute_base_pair(Values* out, BasePairFrame* pair_frame, const BaseFrame& a, const BaseFrame& b);

// Trajectory evaluation: reject absent fixed partners and clear stale values/frames.
bool compute_present_base_pair(Values* out, BasePairFrame* pair_frame, uint16_t* quality,
                               const BaseFrame& a, const BaseFrame& b, const md_unitcell_t* cell = nullptr);

// The six step values use consecutive pair midpoint frames.
bool compute_step(Values* out, const BasePairFrame& a, const BasePairFrame& b);

const char* metric_name(Metric metric);
const char* metric_path(Metric metric);
bool metric_is_step(Metric metric);
bool metric_is_angular(Metric metric);

} // namespace lavery
