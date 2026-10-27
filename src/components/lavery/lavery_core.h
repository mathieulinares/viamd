#pragma once

#include <core/md_vec_math.h>
#include <md_hbond.h>
#include <md_attributes.h>

#include <stddef.h>
#include <stdint.h>
#include <functional>
#include <vector>
#include <array>
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
    MinorGroove, // 3DNA direct P-P width minus 5.8 Angstrom; inter-base-pair class
    MajorGroove,
    Alpha,       // Nucleotide class: backbone torsions, glycosidic torsion and sugar pucker
    Beta,
    Gamma,
    Delta,
    Epsilon,
    Zeta,
    Chi,
    Phase,       // Altona-Sundaralingam pseudorotation phase P, [0, 360)
    Amplitude,   // pseudorotation amplitude (degrees, linear)
    EpsilonZeta, // epsilon - zeta, wrapped; BI < 0 < BII
    Metric_Count,
};

// Members are base pairs, steps between consecutive pairs, or nucleotides of paired bases
// (member 2p + side, side 0 = pairs[p].a); a nucleotide sits at its pair's position on the helix.
enum MemberClass : uint8_t {
    Class_Pair,
    Class_Step,
    Class_Nucleotide,
    Class_Count,
};
constexpr int MAX_CLASS_PARAMETERS = 10;
int class_first_metric(int cls);
int class_parameter_count(int cls);
MemberClass metric_class(Metric metric);
const char* class_name(int cls);       // "Intra-base pair", "Inter-base pair", "Nucleotide"
const char* class_csv_name(int cls);   // "bp", "step", "nt"
inline uint32_t class_all_parameters(int cls) { return (1u << class_parameter_count(cls)) - 1u; }


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

// Sugar-phosphate atoms of one nucleotide: its own, the previous nucleotide's O3' and the next one's P and O5'
// along the strand, and the glycosidic N with the base atom defining chi (C4 purine / C2 pyrimidine).
enum BackboneAtom : int {
    BB_P, BB_O5, BB_C5, BB_C4, BB_C3, BB_O3, BB_C1, BB_C2, BB_O4,
    BB_PrevO3, BB_NextP, BB_NextO5, BB_GlycosidicN, BB_ChiBase,
    BB_Count,
};

struct BaseTopology {
    int32_t backbone[BB_Count] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
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
    Quality_DegenerateNucleotide = 1 << 12,
    Quality_UnusualBond = 1 << 13,
    Quality_PoorPuckerFit = 1 << 14,
};
constexpr uint16_t Quality_Last = Quality_PoorPuckerFit;
constexpr uint16_t Quality_NucleotideWarnings = Quality_UnusualBond | Quality_PoorPuckerFit;
using NucleotideQuality = std::array<uint16_t, 10>; // Alpha..EpsilonZeta, per parameter

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
enum ReferenceScope : int {
    Reference_OwnHistory,
    Reference_HelixHistory,
    Reference_Count,
};
const char* reference_scope_name(ReferenceScope scope);

// Display-only index: preserves endpoints, extrema and a missing-data break per bucket.
// Queries refine boundary buckets and return original sample indices in chronological order.
class PlotSummary {
public:
    bool build(const float* values, size_t count, size_t stride = 1);
    std::vector<uint32_t> indices(size_t first, size_t end, size_t pixels) const;
    size_t size() const { return count_; }
private:
    using Bucket = std::array<uint32_t, 5>;
    std::vector<std::vector<Bucket>> levels_;
    size_t count_ = 0;
};

// Categorical display index. Bit 0 is unavailable; other bits are state+1.
// Range masks preserve every state, including single-frame excursions.
class StateSummary {
public:
    bool build(const std::vector<uint8_t>& codes);
    uint32_t mask(size_t first, size_t end) const;
private:
    std::vector<uint8_t> codes_;
    std::vector<std::vector<uint32_t>> levels_;
};

// 'mean' and 'sd' are the baseline's center and scale (mean/SD or median/scaled MAD).
// Angular baselines measure deviations on the circle, wrapped to [-180, 180) degrees.
struct MemberStatistics {
    double mean = 0.0;
    double sd = 0.0;
    size_t count = 0;
    bool angular = false;
    int state = -1;            // dominant conformational state, for occupancy display
    float state_fraction = 0;  // share of valid samples in that state
    struct Reference {
        double mean = 0, sd = 0;
        size_t count = 0;
    };
    int state_metric = -1;
    std::array<Reference, 3> state_references = {};
    Reference reference(float value) const;
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
// State metrics use a separate baseline within each conformational state. State switches are detected
// separately by residence time, not by distance from the dominant state's center.
// Baseline frames are [first_frame, end_frame); heatmaps still cover every input frame.
// An empty interval or absent state has no reference (NaN z-scores), never a borrowed baseline.
// Nonempty member_groups pools baseline samples within each group (including the selected member),
// preserving the original matrix/heatmap layout and state-conditioned angular treatment.
bool compute_statistics(MetricStatistics* out, const std::vector<float>& values, size_t member_count,
                        bool angular = false, Baseline baseline = Baseline_MeanSD, int state_metric = -1,
                        size_t first_frame = 0, size_t end_frame = SIZE_MAX,
                        const std::vector<uint32_t>& member_groups = {});

struct ReferenceMember {
    std::string label, identity;
    std::array<std::vector<float>, Metric_Count> values;
    std::string pair_a, pair_b;
};
struct ComparisonMemberFilter {
    size_t bp_first = 1, bp_last = SIZE_MAX;
    int pair_type = 0; // 0 all, 1 AT/AU, 2 GC, 3 other; both endpoints for steps
    char nucleotide = 0;
};
bool comparison_member_matches(const ComparisonMemberFilter& filter, int cls,
    const std::string& label, const std::string& pair_a, const std::string& pair_b, bool* metadata_missing);
struct ExternalReference {
    std::string path, run, fingerprint;
    std::array<std::vector<ReferenceMember>, Class_Count> members;
    size_t records = 0;
};
using ReferenceMapping = std::array<std::vector<int>, Class_Count>;
std::string reference_identity(int cls, const std::string& label, const std::string& pair_a, const std::string& pair_b);
bool load_external_reference(ExternalReference* out, const std::string& path, std::string* error);
std::vector<int> match_reference_members(const std::vector<ReferenceMember>& source, const std::vector<std::string>& identities);
bool external_statistics(MetricStatistics* out, const ExternalReference& source, const std::vector<int>& mapping,
                         const std::vector<float>& current, Metric metric, Baseline baseline,
                         const std::vector<uint32_t>& groups = {});
std::vector<float> external_samples(const ExternalReference& source, Metric metric, const std::vector<int>& mapping,
                                    size_t member, const std::vector<uint32_t>& groups = {});
std::vector<float> pooled_external_samples(const ExternalReference& source, Metric metric);
struct FrameCandidate { size_t frame = 0; double score = 0; };
struct FrameFeature { Metric metric = Buckle; size_t member = 0; };
struct PeriodSummary {
    size_t valid = 0, missing = 0, referenced = 0;
    std::array<size_t,5> states = {};
    double z_sum = 0, z_square_sum = 0;
    void add(float value, float z, int state = -1);
    double mean_z() const;
    double rms_z() const;
};
bool add_frame_features(std::vector<FrameFeature>* features, int cls, uint32_t parameter_mask,
                        const std::vector<size_t>& members);
bool representative_frames(std::vector<FrameCandidate>* out, const std::array<std::vector<float>, Metric_Count>& values,
    const std::array<MetricStatistics, Metric_Count>& statistics, const std::array<size_t, Metric_Count>& columns,
    const std::vector<FrameFeature>& features, size_t first, size_t end, size_t count, double separation,
    const std::vector<double>& times, std::string* error);
bool export_representative_frames(const md_system_t& sys, const std::string& run, const std::string& xtc_path,
    const std::vector<FrameCandidate>& frames, const std::string& provenance, std::string* error);

// Conformational states of bimodal nucleotide parameters; 0 states for other metrics.
int metric_state_count(Metric metric);
int metric_state(Metric metric, float value); // -1 when not a state metric or not finite
const char* metric_state_name(Metric metric, int state);

// Dihedral angle a-b-c-d in degrees (-180, 180], with minimum-image bond vectors; NaN for missing atoms.
float torsion(vec3_t a, vec3_t b, vec3_t c, vec3_t d, const md_unitcell_t* cell = nullptr);
// Altona-Sundaralingam pseudorotation from nu0..nu4 (degrees): phase in [0, 360) and amplitude.
void pseudorotation(const float nu[5], float* phase, float* amplitude);
// Fills Alpha..EpsilonZeta and optional per-parameter quality; present_atoms distinguishes absent
// topology atoms from nonfinite coordinates. Distortion warnings retain finite values.
// A zero-amplitude sugar has undefined phase but retains its finite amplitude.
void compute_nucleotide(Values* out, const vec3_t atoms[BB_Count], const md_unitcell_t* cell = nullptr,
                        NucleotideQuality* quality = nullptr, uint32_t present_atoms = (1u << BB_Count)-1);
// 3DNA direct P-P groove widths (El Hassan & Calladine 1998) minus 5.8 A, for the n - 1 steps of one helix
// of n consecutive pairs. p1/p2: phosphorus of the strand I / strand II nucleotide of each pair (non-finite
// when missing). Minor (1-based step i) = mean(|P1(i+2) - P2(i-2)|, |P1(i+3) - P2(i-1)|), major = |P1(i-1) - P2(i+2)|;
// NaN when an index falls outside the helix.
void groove_widths(float* minor, float* major, const vec3_t* p1, const vec3_t* p2, size_t n,
                   const md_unitcell_t* cell = nullptr);
// Pair chains linked by steps (step.a -> step.b), each in helix order.
std::vector<std::vector<uint32_t>> helix_chains(size_t pair_count, const std::vector<StepIndex>& steps);
// One helix ID per pair, step or nucleotide; pooled references never mix helices.
std::vector<uint32_t> helix_member_groups(size_t pair_count, const std::vector<StepIndex>& steps, int cls);
// Zero-based phosphate indices on strands I and II, in helix order.
struct GroovePhosphates {
    std::array<std::array<uint32_t, 2>, 2> indices = {};
    size_t count = 0;
};
GroovePhosphates groove_phosphate_indices(Metric metric, size_t step, size_t pair_count);
// Defining atoms for a true nucleotide torsion; -1 for unavailable atoms or a derived parameter.
std::array<int32_t, 4> nucleotide_torsion_atoms(const BaseTopology& base, Metric metric);

struct DistributionComparison {
    std::vector<double> centers, density; // density is member-major, shared bins
    std::vector<size_t> valid;
    double bin_width = 0;
};
bool compute_distributions(DistributionComparison* out, const std::vector<float>& values,
                           size_t member_count, size_t bins = 48);

struct SampleComparison {
    DistributionComparison histogram;
    std::vector<MemberStatistics> summaries;
    std::vector<size_t> missing;
    double angular_origin = 0;
};
// Unequal-sized groups share physical-unit bins and independently normalized densities.
// Angular samples share a pooled circular origin; summaries use wrapped deviations.
bool compare_samples(SampleComparison* out, const std::vector<std::vector<float>>& groups,
                     bool angular, size_t bins = 48);
const char* csv_comparison_header();
std::string csv_comparison_group(const std::string& run, Metric metric, const std::string& name,
    const std::string& source_filters, const SampleComparison& comparison, size_t group, bool missing_known);

struct EventDistribution {
    std::vector<double> samples, density;
    size_t missing = 0;
    double median = 0, minimum = 0, maximum = 0;
};
// Inclusive event span, for one member, using the full-trajectory histogram's bins.
bool compute_event_distribution(EventDistribution* out, const std::vector<float>& values,
                                size_t member_count, size_t member, size_t begin, size_t end,
                                const DistributionComparison& reference);

struct FrameOutliers {
    size_t outliers = 0;
    size_t valid = 0;
};

struct TransitionFilter {
    enum Scope { All, SelectedPair, SelectedNucleotide };
    Scope scope = All;
    size_t pair = 0, nucleotide = SIZE_MAX;
    int from = -1, to = -1;
    bool matches(int cls, size_t member, int source, int destination) const;
};

// One member (base pair, step or nucleotide) in one frame that takes part in event detection: outlying at the
// lower "continue" threshold, or a base pair whose pairing was lost. z holds the class's signed z-scores.
struct MemberOutlier {
    uint32_t member = 0;
    uint8_t cls = Class_Pair;
    bool lost = false;          // base pair only: pairing broke in this frame (no values)
    uint32_t contributors = 0;  // parameter bits within the class, |z| >= continue threshold
    uint32_t strong = 0;        // subset of contributors with |z| >= start threshold
    float z[MAX_CLASS_PARAMETERS] = {};
};

// A member's involvement over a whole event.
struct EventMember {
    struct Sample {
        size_t frame = 0;
        uint32_t parameters = 0;
        bool lost = false;
    };
    uint32_t member = 0;
    uint8_t cls = Class_Pair;
    uint32_t parameters = 0;   // union of contributing parameter bits
    size_t frames = 0;         // frames of the event in which the member is outlying or lost
    size_t lost_frames = 0;    // of which the pair was lost
    size_t peak = 0;           // frame of its most extreme contributing z-score (first lost frame if none)
    float peak_z = 0;
    int peak_parameter = -1;   // index within the class
    size_t parameter_peak[MAX_CLASS_PARAMETERS] = {};
    float parameter_peak_z[MAX_CLASS_PARAMETERS] = {};
    std::vector<Sample> samples; // Frame-sorted contributions belonging to this event, excluding bridged gaps.
};

const EventMember::Sample* event_member_sample(const EventMember& member, size_t frame);

// Display filter only. Zero is unlimited; physical duration is last minus first time,
// frame fallback is inclusive span. Children are evaluated independently of parents.
bool event_within_duration(size_t begin, size_t end, const float* times, size_t frames, double maximum);

struct OutlierEvent {
    size_t parent = SIZE_MAX;  // Index in the returned event list, SIZE_MAX for a top-level event.
    float burst_sigma = 0;    // Child start threshold; zero for a top-level event.
    bool residual = false;    // Child detected against the parent's background (residual z), not raw z.
    bool truncated_begin = false, truncated_end = false;
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
    uint8_t cls = Class_Pair;
    uint32_t parameters = 0;
    bool lost = false;
};

// Current-frame contributions merged by member and class. SIZE_MAX includes all active events;
// an inspection index restricts feedback to that event alone.
std::vector<EventContribution> event_contributions(const std::vector<OutlierEvent>& events, size_t frame,
                                                  size_t inspected_event = SIZE_MAX);

constexpr size_t EVENT_COLOR_COUNT = 6;
// Chronological palette cycling; inclusive overlaps never share a slot. Higher scores get first
// choice, so overflow slots beyond EVENT_COLOR_COUNT belong to weaker simultaneous events.
std::vector<size_t> event_color_slots(const std::vector<OutlierEvent>& events);

struct EventOptions {
    std::vector<double> times; // Optional strictly increasing timestamps, one per frame.
    double min_duration = 0, max_gap_duration = 0; // Same unit as times.
    size_t first_frame = 0, last_frame = SIZE_MAX; // Inclusive detection range.
    size_t min_frames = 1;   // drop events with fewer outlying frames
    size_t max_gap = 0;      // bridge up to this many consecutive frames without outliers
    int neighbourhood = 2;   // join members at most this many base pairs apart along the helix; < 0: whole structure
    float lost_weight = 0;   // score of one lost-pair sample
    float burst_sigma = 0;   // >0 enables a second, higher-threshold level of localized child events.
    float burst_hysteresis = 0.5f;
    int burst_neighbourhood = 2; // Local reach for children when the parent uses Whole structure.
    // Background mode (with burst_sigma > 0, residual_sigma > 0 and member_z): children are detected on residual
    // z = z - median signed z, per frame and parameter, of all valid members of the same class in the parent's
    // base-pair region (needs at least background_min_members). A global shift cancels; a local burst stands out.
    float residual_sigma = 0;
    size_t background_min_members = 5;
    std::function<float(size_t frame, int cls, uint32_t member, int parameter)> member_z; // NaN when invalid
};

struct StateSwitch {
    Metric metric = Phase;
    uint32_t member = 0;
    size_t begin = 0, end = 0;
    int from = -1, to = -1;
    float angular_change = 0; // shortest signed difference between source/destination run circular means
    size_t source_begin = 0, source_end = 0;
    // Clipped means the run touches a detection boundary; its full residence is unknown.
    bool source_clipped = false, destination_clipped = false;
    bool destination_at_trajectory_end = false;
    size_t intervening_samples() const { return begin > source_end ? begin-source_end-1 : 0; }
};
// Both source and destination runs must meet min_duration (or min_frames without timestamps).
// Short excursions are ignored; missing values reset the source. No transitions inferred at range boundaries.
bool detect_state_switches(std::vector<StateSwitch>* out, const std::vector<float>& values,
                           size_t members, Metric metric, const EventOptions& options);
const char* csv_state_switch_header();
std::string csv_state_switch_row(const std::string& run, const std::string& label, const StateSwitch& change,
                                 double begin, double end, const std::string& time_unit, double min_residence,
                                 double source_begin, double source_end);

// Resolve inclusive physical-time bounds to sampled frame bounds. Reports invalid/empty ranges.
bool event_time_range(EventOptions* options, double begin, double end, const char* context = "detection");

// Groups outlying (frame, member) samples into events. Two samples join when they are at most
// max_gap + 1 frames apart (or within max_gap_duration of quiet time when times are supplied)
// and at most 'neighbourhood' base pairs apart along the helix: pair i
// and pair j are |i - j| apart when steps link them, a step sits half-way between its two pairs,
// and members of different duplexes never join. An event needs at least one strong (start
// threshold) or lost sample; weak samples only extend events (hysteresis). Events are ordered by
// first frame, then by first pair. With burst_sigma > 0, localized higher-threshold
// components are retained as children of their containing event, without removing parent samples.
std::vector<OutlierEvent> detect_events(const std::vector<std::vector<MemberOutlier>>& frames, size_t pair_count,
                                        const std::vector<StepIndex>& steps, const EventOptions& options = {});

// Short rule-based description from the dominant parameter and its sign, e.g. "Unwinding",
// "Opening (breathing / flip)"; "Pairing disruption" leads for disrupted original pairing.
std::string event_kind(const OutlierEvent& event);
std::string pairing_disruption_reasons(uint16_t quality);
// Index (Metric) of the parameter with the largest summed |z|, -1 when none.
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
    bool residual = false;
    bool truncated_begin = false, truncated_end = false;
    double min_duration = 0, max_gap_duration = 0, detection_begin = 0, detection_end = 0;
    double baseline_begin = 0, baseline_end = 0;
    ReferenceScope reference_scope = Reference_OwnHistory;
    std::string reference_source = "current_trajectory";
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
    ReferenceScope reference_scope = Reference_OwnHistory;
    std::string reference_source = "current_trajectory";
    double baseline_begin = 0, baseline_end = 0;
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
    std::vector<uint32_t> hydrogens;
};

struct PairChemistry {
    std::vector<PairContact> contacts;
    bool uses_hydrogens = false;
    size_t expected_contacts = 2;
    std::vector<PairContact> hoogsteen_contacts;
    int purine_side = -1; // A-T/U only; G-C Hoogsteen/protonation is not classified.
};

enum PairingState : uint8_t {
    Pairing_Unavailable, Pairing_WatsonCrick, Pairing_Hoogsteen, Pairing_Other, Pairing_Unpaired,
    Pairing_Count,
};
const char* pairing_state_name(PairingState state);
struct PairingEvidence {
    uint8_t canonical = 0, hoogsteen = 0, expected = 0;
    int purine_side = -1;
    bool complete = false; // All required donor hydrogens and finite contact atoms.
};
PairingState classify_pairing(const PairingEvidence& evidence, float chi_a, float chi_b, uint16_t geometry);
struct PairingRun {
    size_t begin = 0, end = 0;
    PairingState state = Pairing_Unavailable;
};
struct PairingTransition {
    PairingRun source, destination;
};
bool pairing_runs(std::vector<PairingRun>* runs, const std::vector<PairingState>& states, size_t members, size_t member);
bool pairing_transitions(std::vector<PairingTransition>* out, const std::vector<PairingRun>& runs, const EventOptions& options);
const char* csv_pairing_header();
std::string csv_pairing_run(const std::string& run, const std::string& label, size_t pair, const PairingRun& span,
                            double begin, double end, const std::string& unit, double minimum, bool physical_time);

class PairingValidator {
public:
    PairingValidator() = default;
    ~PairingValidator();
    PairingValidator(const PairingValidator&) = delete;
    PairingValidator& operator=(const PairingValidator&) = delete;
    bool initialize(const md_system_t& sys, const std::vector<BaseTopology>& bases, const std::vector<Pair>& pairs);
    bool evaluate(std::vector<uint16_t>& quality, std::vector<uint8_t>& contacts,
                  const md_system_state_t& state, std::vector<PairingEvidence>* evidence = nullptr) const;
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
    std::vector<Values> pair_values, step_values, nucleotide_values; // nucleotide member 2p + side
    std::vector<uint16_t> pair_quality, step_quality;
    std::vector<NucleotideQuality> nucleotide_quality;
    std::vector<uint8_t> contacts;
    std::vector<PairingState> pairing_states;
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
bool compute_step(Values* out, const BasePairFrame& a, const BasePairFrame& b, BasePairFrame* midpoint = nullptr);

const char* metric_name(Metric metric);
const char* metric_path(Metric metric);
bool metric_is_step(Metric metric);
bool metric_is_angular(Metric metric);
bool metric_uses_degrees(Metric metric);

} // namespace lavery
