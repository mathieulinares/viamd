#include "movie_keys.h"

#include <gfx/camera_utils.h>

#include <algorithm>

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
           a.spin_turns == b.spin_turns && a.spin_axis == b.spin_axis && a.spin_constant_speed == b.spin_constant_speed;
}

static bool equal(const ParamKey& a, const ParamKey& b) {
    return a.param == b.param && a.time == b.time && a.ease == b.ease &&
           a.value[0] == b.value[0] && a.value[1] == b.value[1] && a.value[2] == b.value[2];
}

bool movie_keys_equal(const MovieKeys& a, const MovieKeys& b) {
    if (a.loop != b.loop || a.camera.size() != b.camera.size() || a.params.size() != b.params.size()) return false;
    for (size_t i = 0; i < a.camera.size(); ++i) {
        if (!equal(a.camera[i], b.camera[i])) return false;
    }
    for (size_t i = 0; i < a.params.size(); ++i) {
        if (!equal(a.params[i], b.params[i])) return false;
    }
    return true;
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
