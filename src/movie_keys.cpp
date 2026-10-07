#include "movie_keys.h"

#include <gfx/camera_utils.h>

#include <algorithm>
#include <cmath>
#include <cstring>

bool param_keys_evaluate(float* out, int comps, const ParamKey* keys, size_t count, int param, double time) {
    std::vector<const ParamKey*> mine;
    for (size_t i = 0; i < count; ++i) {
        if (keys[i].param == param) mine.push_back(&keys[i]);
    }
    if (mine.empty()) return false;
    std::stable_sort(mine.begin(), mine.end(), [](const ParamKey* a, const ParamKey* b) { return a->time < b->time; });

    // Keys on the same time are one key
    std::vector<double> times;
    std::vector<KeyEase> eases;
    std::vector<const ParamKey*> unique;
    for (const ParamKey* k : mine) {
        if (!times.empty() && k->time <= times.back()) continue;
        times.push_back(k->time);
        eases.push_back(k->ease);
        unique.push_back(k);
    }

    std::vector<double> values(unique.size());
    for (int c = 0; c < comps && c < 3; ++c) {
        for (size_t i = 0; i < unique.size(); ++i) values[i] = unique[i]->value[c];
        out[c] = (float)keyed_curve_evaluate(times.data(), values.data(), eases.data(), times.size(), time);
    }
    return true;
}

static bool equal(const CameraKeyframe& a, const CameraKeyframe& b) {
    return a.transform.orientation.x == b.transform.orientation.x && a.transform.orientation.y == b.transform.orientation.y &&
           a.transform.orientation.z == b.transform.orientation.z && a.transform.orientation.w == b.transform.orientation.w &&
           a.transform.position.x == b.transform.position.x && a.transform.position.y == b.transform.position.y &&
           a.transform.position.z == b.transform.position.z && a.transform.distance == b.transform.distance &&
           a.fov_y == b.fov_y && a.time == b.time && a.ease == b.ease &&
           a.use_frame == b.use_frame && a.frame == b.frame &&
           a.spin_turns == b.spin_turns && a.spin_axis == b.spin_axis && a.spin_constant_speed == b.spin_constant_speed &&
           a.follow == b.follow && a.follow_center.x == b.follow_center.x && a.follow_center.y == b.follow_center.y &&
           a.follow_center.z == b.follow_center.z && a.follow_atom == b.follow_atom;
}

static bool equal(const ParamKey& a, const ParamKey& b) {
    return a.param == b.param && a.time == b.time && a.ease == b.ease &&
           a.value[0] == b.value[0] && a.value[1] == b.value[1] && a.value[2] == b.value[2];
}

bool rep_keys_evaluate(float* out, const RepKey* keys, size_t count, uint32_t rep, int prop, double time) {
    std::vector<const RepKey*> mine;
    for (size_t i = 0; i < count; ++i) {
        if (keys[i].rep == rep && keys[i].prop == prop) mine.push_back(&keys[i]);
    }
    if (mine.empty()) return false;
    std::stable_sort(mine.begin(), mine.end(), [](const RepKey* a, const RepKey* b) { return a->time < b->time; });

    std::vector<double> times;
    std::vector<KeyEase> eases;
    std::vector<const RepKey*> unique;
    for (const RepKey* k : mine) {
        if (!times.empty() && k->time <= times.back()) continue;
        times.push_back(k->time);
        eases.push_back(prop == (int)RepProp::Visible ? KeyEase::Hold : k->ease);
        unique.push_back(k);
    }

    std::vector<double> values(unique.size());
    for (int c = 0; c < rep_prop_comps(prop); ++c) {
        for (size_t i = 0; i < unique.size(); ++i) values[i] = unique[i]->value[c];
        out[c] = (float)keyed_curve_evaluate(times.data(), values.data(), eases.data(), times.size(), time);
    }
    return true;
}

bool rep_visible_factor(float* out, const RepKey* keys, size_t count, uint32_t rep, double time, double transition) {
    std::vector<const RepKey*> mine;
    for (size_t i = 0; i < count; ++i) {
        if (keys[i].rep == rep && keys[i].prop == (int)RepProp::Visible) mine.push_back(&keys[i]);
    }
    if (mine.empty()) return false;
    std::stable_sort(mine.begin(), mine.end(), [](const RepKey* a, const RepKey* b) { return a->time < b->time; });

    // Keys on the same time are one key
    std::vector<const RepKey*> unique;
    for (const RepKey* k : mine) {
        if (unique.empty() || k->time > unique.back()->time) unique.push_back(k);
    }

    auto smooth = [](double u) { u = std::clamp(u, 0.0, 1.0); return u * u * (3.0 - 2.0 * u); };
    auto target = [](const RepKey* k) { return k->value[0] >= 0.5f ? 1.0 : 0.0; };

    if (time < unique[0]->time) {
        *out = (float)target(unique[0]);
        return true;
    }
    size_t last = 0;
    while (last + 1 < unique.size() && unique[last + 1]->time <= time) ++last;

    // Each change starts from where the one before it had got to by then
    auto ramp = [&](double since) { return transition > 0.0 ? smooth(since / transition) : 1.0; };
    double start = target(unique[0]);
    for (size_t i = 1; i <= last; ++i) {
        start += (target(unique[i - 1]) - start) * ramp(unique[i]->time - unique[i - 1]->time);
    }
    const double end = target(unique[last]);
    *out = (float)(start + (end - start) * ramp(time - unique[last]->time));
    return true;
}

static bool equal(const RepKey& a, const RepKey& b) {
    return a.rep == b.rep && a.prop == b.prop && a.time == b.time && a.ease == b.ease &&
           a.value[0] == b.value[0] && a.value[1] == b.value[1] && a.value[2] == b.value[2];
}

bool movie_keys_equal(const MovieKeys& a, const MovieKeys& b) {
    if (a.loop != b.loop || a.duration != b.duration || a.traj_begin != b.traj_begin || a.traj_end != b.traj_end ||
        a.start_frame != b.start_frame || a.end_frame != b.end_frame || a.camera.size() != b.camera.size() ||
        a.params.size() != b.params.size() || a.reps.size() != b.reps.size() || a.overlays.size() != b.overlays.size()) return false;
    for (size_t i = 0; i < a.camera.size(); ++i) {
        if (!equal(a.camera[i], b.camera[i])) return false;
    }
    for (size_t i = 0; i < a.params.size(); ++i) {
        if (!equal(a.params[i], b.params[i])) return false;
    }
    for (size_t i = 0; i < a.reps.size(); ++i) {
        if (!equal(a.reps[i], b.reps[i])) return false;
    }
    for (size_t i = 0; i < a.overlays.size(); ++i) {
        const MovieOverlay& x = a.overlays[i];
        const MovieOverlay& y = b.overlays[i];
        if (x.type != y.type || x.enabled != y.enabled || x.begin != y.begin || x.end != y.end ||
            x.fade_in != y.fade_in || x.fade_out != y.fade_out || x.anchor != y.anchor ||
            x.size != y.size || x.size_unit != y.size_unit || x.width != y.width || x.show_elapsed != y.show_elapsed || x.show_speed != y.show_speed || x.legacy_subplot_mask != y.legacy_subplot_mask || x.plot_axis != y.plot_axis || x.font_points != y.font_points || x.line_points != y.line_points || x.palette != y.palette || x.show_markers != y.show_markers || x.show_titles != y.show_titles || x.panels.size() != y.panels.size() || x.reveal != y.reveal || x.show_value != y.show_value || x.length != y.length || strcmp(x.text, y.text) != 0 || strcmp(x.path, y.path) != 0) return false;
        for (int c = 0; c < 4; ++c) {
            if (x.color[c] != y.color[c] || x.background[c] != y.background[c]) return false;
        }
        for (size_t p = 0; p < x.panels.size(); ++p) {
            if (x.panels[p].view != y.panels[p].view || x.panels[p].subplot != y.panels[p].subplot ||
                x.panels[p].begin != y.panels[p].begin || x.panels[p].end != y.panels[p].end || strcmp(x.panels[p].title, y.panels[p].title) != 0) return false;
        }
    }
    if (a.markers.size() != b.markers.size()) return false;
    for (size_t i = 0; i < a.markers.size(); ++i) {
        if (a.markers[i].time != b.markers[i].time || strcmp(a.markers[i].label, b.markers[i].label) != 0) return false;
    }
    return true;
}

void movie_keys_scale_time(MovieKeys* keys, double scale) {
    for (CameraKeyframe& k : keys->camera) k.time *= scale;
    for (ParamKey& k : keys->params) k.time *= scale;
    for (RepKey& k : keys->reps) k.time *= scale;
    for (MovieOverlay& o : keys->overlays) {
        o.begin *= scale;
        o.end *= scale;
        o.fade_in *= (float)scale;
        o.fade_out *= (float)scale;
        for (MoviePlotPanel& p : o.panels) {
            p.begin *= scale;
            p.end *= scale;
        }
    }
    for (MovieMarker& k : keys->markers) k.time *= scale;
    keys->duration = (float)(keys->duration * scale);
    keys->traj_begin = (float)(keys->traj_begin * scale);
    keys->traj_end = (float)(keys->traj_end * scale);
}

void MovieHistory::clear(const MovieKeys& current) {
    undo_.clear();
    redo_.clear();
    committed_ = current;
    has_committed_ = true;
}

void MovieHistory::update(const MovieKeys& current, bool editing) {
    if (!has_committed_) {
        committed_ = current;
        has_committed_ = true;
        return;
    }
    if (editing || movie_keys_equal(committed_, current)) return;

    undo_.push_back(committed_);
    if (undo_.size() > MAX_STEPS) undo_.erase(undo_.begin());
    redo_.clear();
    committed_ = current;
}

bool MovieHistory::undo(MovieKeys* state) {
    // An edit that is still under way counts as done before it is undone
    update(*state, false);
    if (undo_.empty()) return false;
    redo_.push_back(*state);
    *state = undo_.back();
    undo_.pop_back();
    committed_ = *state;
    return true;
}

bool MovieHistory::redo(MovieKeys* state) {
    update(*state, false);
    if (redo_.empty()) return false;
    undo_.push_back(*state);
    *state = redo_.back();
    redo_.pop_back();
    committed_ = *state;
    return true;
}

void movie_frame_range(int num_frames, double fps, bool use_range, double begin, double end, int* first, int* last) {
    const int n = num_frames < 1 ? 1 : num_frames;
    *first = 0;
    *last = n - 1;
    if (!use_range) return;
    *first = std::clamp((int)std::ceil(begin * fps - 1.0e-6), 0, n - 1);
    *last  = std::clamp((int)std::floor(end * fps + 1.0e-6), *first, n - 1);
}

void movie_scaled_size(int* w, int* h, int percent) {
    const int s = std::clamp(percent, 10, 100);
    if (s >= 100) return;
    *w = std::max(2, (*w * s / 100) & ~1);
    *h = std::max(2, (*h * s / 100) & ~1);
}

bool movie_time_left(int done, int total, double active_seconds, double* seconds) {
    if (done < 2 || active_seconds < 1.0) return false;
    *seconds = active_seconds / (double)done * (double)std::max(total - done, 0);
    return true;
}

double movie_snap_to_frame(double time, double fps, double duration) {
    if (fps > 0.0) time = std::round(time * fps) / fps;
    return std::clamp(time, 0.0, std::max(duration, 0.0));
}
