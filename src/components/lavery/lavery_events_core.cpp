#include "lavery_core.h"
#include <core/md_log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iterator>
#include <limits>

namespace lavery {

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

bool event_within_duration(size_t begin, size_t end, const float* times, size_t frames, double maximum) {
    if (!std::isfinite(maximum) || maximum < 0 || begin > end || end >= frames ||
        (times && (!std::isfinite(times[begin]) || !std::isfinite(times[end]) || times[end] < times[begin]))) {
        MD_LOG_ERROR("Lavery: invalid maximum-duration filter or event span");
        return false;
    }
    return maximum == 0 || (times ? (double)times[end] - times[begin] : (double)(end - begin + 1)) <= maximum;
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

uint32_t restore_event_parameters(uint32_t mask, bool legacy) {
    if (legacy && mask < (1u << MinorGroove))
        mask |= DEFAULT_EVENT_PARAMETERS & ~((1u << MinorGroove) - 1u);
    return mask & ALL_METRIC_PARAMETERS;
}

template <typename T>
static bool resolve_sample_time_range(size_t* first, size_t* last, const T* times, size_t frames,
    double scale, double begin, double end, const char* context) {
    if (first) *first = 0;
    if (last) *last = 0;
    if (!first || !last || !times || !frames || !std::isfinite(scale) || scale <= 0 ||
        !std::isfinite(begin) || !std::isfinite(end) || begin > end) {
        MD_LOG_ERROR("Lavery: invalid %s time range", context);
        return false;
    }
    for (size_t f = 0; f < frames; ++f) {
        const double time = (double)times[f] * scale;
        if (!std::isfinite(time) || (f && time <= (double)times[f-1] * scale)) {
            MD_LOG_ERROR("Lavery: %s timestamps must be finite and strictly increasing", context);
            return false;
        }
    }
    const auto lower = std::lower_bound(times, times+frames, begin,
        [scale](T time, double bound) { return (double)time * scale < bound; });
    const auto upper = std::upper_bound(lower, times+frames, end,
        [scale](double bound, T time) { return bound < (double)time * scale; });
    if (lower == upper) {
        MD_LOG_ERROR("Lavery: %s time range contains no samples", context);
        return false;
    }
    *first = (size_t)(lower-times);
    *last = (size_t)(upper-times);
    return true;
}

bool sample_time_range(size_t* first, size_t* end, const float* times, size_t frames,
    double time_scale, double begin, double last, const char* context) {
    return resolve_sample_time_range(first,end,times,frames,time_scale,begin,last,context);
}

bool event_time_range(EventOptions* options, double begin, double end, const char* context) {
    size_t first = 0, last = 0;
    if (!resolve_sample_time_range(&first,&last,options ? options->times.data() : nullptr,
        options ? options->times.size() : 0,1.0,begin,end,context)) return false;
    options->first_frame = first;
    options->last_frame = last-1;
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

std::string pairing_disruption_reasons(uint16_t quality) {
    std::string reasons;
    for (uint16_t flag : {Quality_Separation,Quality_Alignment,Quality_PlaneOffset,Quality_NoPairContacts}) {
        if (!(quality & flag)) continue;
        if (!reasons.empty()) reasons += "; ";
        reasons += quality_description(flag);
    }
    return reasons;
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
    case XDisplacement: case YDisplacement: kind = "Helical displacement"; break;
    case HelicalRise: kind = positive ? "Helical unstacking" : "Helical compression"; break;
    case Inclination: case Tip: kind = "Base-pair inclination"; break;
    case HelicalTwist: kind = positive ? "Helical overwinding" : "Helical unwinding"; break;
    case Phase: case Amplitude: case Delta: kind = "Sugar fluctuation"; break;
    case Epsilon: case Zeta: case EpsilonZeta: kind = "Epsilon / zeta fluctuation"; break;
    case Alpha: case Gamma: kind = "Alpha / gamma fluctuation"; break;
    case Beta: kind = "Beta crankshaft"; break;
    case Chi: kind = "Chi fluctuation"; break;
    default: break;
    }
    if (event.lost_samples) return *kind ? std::string("Pairing disruption + ") + kind : std::string("Pairing disruption");
    return kind;
}

double chance_outlier_probability(float sigma, int parameters) {
    if (!std::isfinite(sigma) || parameters <= 0) return 0.0;
    const double single = std::erfc(std::max(0.0f, sigma) / std::sqrt(2.0));
    return 1.0 - std::pow(1.0 - single, (double)parameters);
}

const char* multiple_testing_name(MultipleTesting method) {
    switch (method) {
    case MultipleTesting_None: return "None";
    case MultipleTesting_Bonferroni: return "Bonferroni";
    case MultipleTesting_BenjaminiHochberg: return "Benjamini-Hochberg";
    default: return "Unknown";
    }
}

float gaussian_sigma_for_probability(double p) {
    if (!(p > 0.0) || !(p < 1.0)) return std::numeric_limits<float>::quiet_NaN();
    double lo = 0.0, hi = 40.0;
    for (int i = 0; i < 200 && hi - lo > 1e-12; ++i) {
        const double mid = 0.5 * (lo + hi);
        (std::erfc(mid / std::sqrt(2.0)) > p ? lo : hi) = mid;
    }
    return (float)(0.5 * (lo + hi));
}

float corrected_sigma(MultipleTesting method, double alpha, std::vector<float>* abs_z) {
    const float missing = std::numeric_limits<float>::quiet_NaN();
    if (!abs_z || !(alpha > 0.0) || !(alpha < 1.0) ||
        (method != MultipleTesting_Bonferroni && method != MultipleTesting_BenjaminiHochberg)) return missing;
    const size_t tests = std::max<size_t>(1, abs_z->size());
    const float bonferroni = gaussian_sigma_for_probability(alpha / (double)tests);
    if (method == MultipleTesting_Bonferroni || abs_z->empty()) return bonferroni;
    std::sort(abs_z->begin(), abs_z->end(), std::greater<float>());
    for (size_t i = tests; i-- > 0;) {
        const double p = std::erfc((double)(*abs_z)[i] / std::sqrt(2.0));
        if (p <= (double)(i + 1) * alpha / (double)tests) {
            // Outliers are strictly above the tolerance, so stay just below the last rejected |z|.
            return std::min(bonferroni, (*abs_z)[i] * (1.0f - 1e-6f));
        }
    }
    return bonferroni;
}

const char* csv_event_header() {
    return "run,event,begin_frame,end_frame,begin_time,end_time,time_unit,event_peak_frame,event_peak_sigma,event_score,event_type,"
           "first_pair,last_pair,scope,baseline,sigma,end_sigma,neighbourhood,"
           "class,member,label,member_outlier_frames,member_lost_frames,parameter,unit,contributing,parameter_peak_frame,parameter_peak_zscore,parameter_peak_value,"
           "parent_event,burst_sigma,truncated_begin,truncated_end,min_duration,max_gap_duration,detection_begin,detection_end,burst_mode,baseline_begin,baseline_end,reference_scope,reference_source\n";
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
    snprintf(buf, sizeof(buf), ",%zu,%zu,%s,%s,%d,%zu,%.9g,%.9g,%zu,%.9g,%d,%d,%.17g,%.17g,%.17g,%.17g,%s,%.17g,%.17g,%s,", r.member_frames, r.member_lost, metric_path(r.metric),
        metric_uses_degrees(r.metric) ? "deg" : "Angstrom", r.contributing ? 1 : 0, r.parameter_peak, r.parameter_z, r.value,
        r.parent_event, r.burst_sigma, r.truncated_begin ? 1 : 0, r.truncated_end ? 1 : 0,
        r.min_duration, r.max_gap_duration, r.detection_begin, r.detection_end,
        !r.parent_event ? "" : r.residual ? "background" : "stronger", r.baseline_begin, r.baseline_end,
        r.reference_scope == Reference_HelixHistory ? "helix_history" : "own_history");
    row += std::string(buf)+csv_quote(r.reference_source)+"\n";
    return row;
}

} // namespace lavery
