#pragma once

#include <core/md_vec_math.h>
#include <md_hbond.h>
#include <md_attributes.h>

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
    Quality_NoPairContacts = 1 << 10,
    Quality_NoncanonicalPair = 1 << 11,
};
constexpr uint16_t Quality_Last = Quality_NoncanonicalPair;

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

enum Baseline : int {
    Baseline_MeanSD,     // mean and population standard deviation
    Baseline_MedianMAD,  // median and 1.4826 * median absolute deviation (falls back to SD when MAD is 0)
    Baseline_Count,
};
const char* baseline_name(Baseline baseline);

// 'mean' and 'sd' are the baseline's center and scale (mean/SD or median/scaled MAD).
// Angular baselines measure deviations on the circle, wrapped to [-180, 180) degrees.
struct MemberStatistics {
    double mean = 0.0;
    double sd = 0.0;
    size_t count = 0;
    bool angular = false;
    double deviation(float value) const;
    float zscore(float value) const;
    bool is_outlier(float value, float sigma) const;
};

// Input is frame-major; heatmap output is member-major with row zero at highest Y.
struct MetricStatistics {
    std::vector<MemberStatistics> members;
    std::vector<float> heatmap;
};

// Angular series are unwrapped around their circular mean before the baseline is computed, so
// series that never approach +/-180 degrees from it give exactly the linear result.
bool compute_statistics(MetricStatistics* out, const std::vector<float>& values, size_t member_count,
                        bool angular = false, Baseline baseline = Baseline_MeanSD);

struct DistributionComparison {
    std::vector<double> centers, density; // density is member-major, shared bins
    std::vector<size_t> valid;
    double bin_width = 0;
};
bool compute_distributions(DistributionComparison* out, const std::vector<float>& values,
                           size_t member_count, size_t bins = 48);

struct FrameOutliers {
    size_t outliers = 0;
    size_t valid = 0;
};

// One member (base pair or step) in one frame that takes part in event detection: outlying at the
// lower "continue" threshold, or a base pair whose pairing was lost. z holds all six signed z-scores.
struct MemberOutlier {
    uint32_t member = 0;
    bool step = false;
    bool lost = false;          // base pair only: pairing broke in this frame (no values)
    uint32_t contributors = 0;  // bits 0-5 within the class, |z| >= continue threshold
    uint32_t strong = 0;        // subset of contributors with |z| >= start threshold
    float z[6] = {};
};

// A member's involvement over a whole event.
struct EventMember {
    struct Sample {
        size_t frame = 0;
        uint32_t parameters = 0;
        bool lost = false;
    };
    uint32_t member = 0;
    bool step = false;
    uint32_t parameters = 0;   // union of contributing parameter bits
    size_t frames = 0;         // frames of the event in which the member is outlying or lost
    size_t lost_frames = 0;    // of which the pair was lost
    size_t peak = 0;           // frame of its most extreme contributing z-score (first lost frame if none)
    float peak_z = 0;
    int peak_parameter = -1;   // 0-5 within the class
    size_t parameter_peak[6] = {};
    float parameter_peak_z[6] = {};
    std::vector<Sample> samples; // Frame-sorted contributions belonging to this event, excluding bridged gaps.
};

const EventMember::Sample* event_member_sample(const EventMember& member, size_t frame);

struct OutlierEvent {
    size_t parent = SIZE_MAX;  // Index in the returned event list, SIZE_MAX for a top-level event.
    float burst_sigma = 0;    // Child start threshold; zero for a top-level event.
    size_t begin = 0, end = 0, peak = 0, peak_count = 0;
    float severity = 0;        // peak |z|
    float score = 0;           // sum over (frame, member) of the largest contributing |z|; lost samples count lost_weight
    size_t outlier_frames = 0; // frames with outliers; less than the span when gaps are bridged
    size_t lost_samples = 0;
    uint32_t pair_first = 0, pair_last = 0; // range of base-pair indices touched (a step touches both its pairs)
    float metric_score[Metric_Count] = {};  // sum of contributing |z| per parameter
    float metric_signed[Metric_Count] = {}; // sum of contributing signed z per parameter
    std::vector<EventMember> members;       // sorted by decreasing |peak_z|, lost-only members last
};

struct EventContribution {
    uint32_t member = 0;
    bool step = false;
    uint32_t parameters = 0;
    bool lost = false;
};

// Current-frame contributions merged by member and class. SIZE_MAX includes all active events;
// an inspection index restricts feedback to that event alone.
std::vector<EventContribution> event_contributions(const std::vector<OutlierEvent>& events, size_t frame,
                                                  size_t inspected_event = SIZE_MAX);

// Stable colour slots in input order; inclusive overlapping intervals never share a slot.
std::vector<size_t> event_color_slots(const std::vector<OutlierEvent>& events);

struct EventOptions {
    size_t min_frames = 1;   // drop events with fewer outlying frames
    size_t max_gap = 0;      // bridge up to this many consecutive frames without outliers
    int neighbourhood = 2;   // join members at most this many base pairs apart along the helix; < 0: whole structure
    float lost_weight = 0;   // score of one lost-pair sample
    float burst_sigma = 0;   // >0 enables a second, higher-threshold level of localized child events.
    float burst_hysteresis = 0.5f;
    int burst_neighbourhood = 2; // Local reach for children when the parent uses Whole structure.
};

// Groups outlying (frame, member) samples into events. Two samples join when they are at most
// max_gap + 1 frames apart and at most 'neighbourhood' base pairs apart along the helix: pair i
// and pair j are |i - j| apart when steps link them, a step sits half-way between its two pairs,
// and members of different duplexes never join. An event needs at least one strong (start
// threshold) or lost sample; weak samples only extend events (hysteresis). Events are ordered by
// first frame, then by first pair. With burst_sigma > 0, localized higher-threshold
// components are retained as children of their containing event, without removing parent samples.
std::vector<OutlierEvent> detect_events(const std::vector<std::vector<MemberOutlier>>& frames, size_t pair_count,
                                        const std::vector<StepIndex>& steps, const EventOptions& options = {});

// Short rule-based description from the dominant parameter and its sign, e.g. "Unwinding",
// "Opening (breathing / flip)"; "Pair loss" leads when a pair broke.
std::string event_kind(const OutlierEvent& event);
// Index (0-11) of the parameter with the largest summed |z|, -1 when none.
int event_dominant_metric(const OutlierEvent& event);

// Probability that a member with 'parameters' independent Gaussian parameters has at least one
// |z| >= sigma by chance.
double chance_outlier_probability(float sigma, int parameters);

struct CsvEventRow {
    std::string run, scope, baseline, label, time_unit, kind;
    size_t event = 0, begin = 0, end = 0, peak = 0, member = 0, member_frames = 0, member_lost = 0, parameter_peak = 0;
    size_t pair_first = 0, pair_last = 0; // 1-based
    int neighbourhood = -1;
    double begin_time = 0, end_time = 0;
    float severity = 0, score = 0, sigma = 0, end_sigma = 0, parameter_z = 0, value = 0;
    Metric metric = Buckle;
    bool contributing = false;
    size_t parent_event = 0; // One-based parent ID, zero for a top-level event.
    float burst_sigma = 0;
};
const char* csv_event_header();
std::string csv_event_row(const CsvEventRow& row);
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
    Baseline baseline_method = Baseline_MeanSD;
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

struct PairContact {
    uint32_t donor = 0, acceptor = 0;
    bool explicit_hydrogen = false;
};

struct PairChemistry {
    std::vector<PairContact> contacts;
    bool uses_hydrogens = false;
    size_t expected_contacts = 2;
};

class PairingValidator {
public:
    PairingValidator() = default;
    ~PairingValidator();
    PairingValidator(const PairingValidator&) = delete;
    PairingValidator& operator=(const PairingValidator&) = delete;
    bool initialize(const md_system_t& sys, const std::vector<BaseTopology>& bases, const std::vector<Pair>& pairs);
    bool evaluate(std::vector<uint16_t>& quality, std::vector<uint8_t>& contacts,
                  const md_system_state_t& state) const;
    const std::vector<PairChemistry>& chemistry() const { return pairs_; }
private:
    md_hbond_query_t query_ = {};
    std::vector<PairChemistry> pairs_;
    bool initialized_ = false;
};

// Canonical contacts are directional donor -> acceptor. Missing hydrogens use
// >=2 heavy-atom contacts <=3.5 A, explicitly a proxy rather than H-bond evidence.
uint16_t evaluate_pair_contacts(uint8_t* count, const PairChemistry& pair,
                                const md_system_state_t& state, const md_hbond_set_t& bonds);

struct FrameAnalysis {
    std::vector<BasePairFrame> pair_frames;
    std::vector<Values> pair_values, step_values;
    std::vector<uint16_t> pair_quality, step_quality;
    std::vector<uint8_t> contacts;
};
bool analyze_frame(FrameAnalysis* out, const md_system_t& sys, const md_system_state_t& state,
                    const std::vector<BaseTopology>& bases, const std::vector<Pair>& pairs,
                    const std::vector<StepIndex>& steps, const PairingValidator& validator);

struct AxisPath {
    std::vector<vec3_t> points;
    std::vector<vec3_t> tangents;
    std::vector<size_t> offsets; // [segment_count + 1], no connection across gaps
};
AxisPath compute_axis(const std::vector<BasePairFrame>& frames, const std::vector<StepIndex>& steps,
                       const md_unitcell_t* cell = nullptr, int samples_per_step = 8);
std::string csv_axis(const std::string& run, double frame, const AxisPath& axis);

bool publish_series(md_attributes_t* table, const std::string& run, const char* leaf, const char* label,
                     md_attribute_type_t type, size_t frames, size_t members,
                     const void* data, size_t bytes, md_unit_t unit);

// The six step values use consecutive pair midpoint frames.
bool compute_step(Values* out, const BasePairFrame& a, const BasePairFrame& b);

const char* metric_name(Metric metric);
const char* metric_path(Metric metric);
bool metric_is_step(Metric metric);
bool metric_is_angular(Metric metric);

} // namespace lavery
