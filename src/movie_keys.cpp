#include "movie_keys.h"

#include <gfx/camera_utils.h>

#include <algorithm>
#include <cfloat>
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
           a.follow_center.z == b.follow_center.z && a.follow_atom == b.follow_atom && strcmp(a.name, b.name) == 0;
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

// ## Representations over time as stretches

namespace {
constexpr double REP_EPS = 1.0e-6;
constexpr double REP_MIN_LENGTH = 0.05;

// The indices of the Visible keys of a representation by time, keys on the same time being one (the first)
std::vector<int> visible_key_order(const std::vector<RepKey>& keys, uint32_t rep) {
    std::vector<int> idx;
    for (int i = 0; i < (int)keys.size(); ++i) {
        if (keys[i].rep == rep && keys[i].prop == (int)RepProp::Visible) idx.push_back(i);
    }
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return keys[a].time < keys[b].time; });
    std::vector<int> unique;
    for (int i : idx) {
        if (unique.empty() || keys[i].time > keys[unique.back()].time) unique.push_back(i);
    }
    return unique;
}

RepKey hidden_key(uint32_t rep, double time) {
    RepKey k;
    k.rep = rep;
    k.prop = (int)RepProp::Visible;
    k.time = time;
    k.value[0] = 0.0f;
    k.ease = KeyEase::Hold;
    return k;
}

RepKey shown_key(uint32_t rep, double time) {
    RepKey k = hidden_key(rep, time);
    k.value[0] = 1.0f;
    return k;
}
}

std::vector<RepInterval> rep_shown_intervals(const std::vector<RepKey>& keys, uint32_t rep, double duration) {
    std::vector<RepInterval> out;
    const std::vector<int> order = visible_key_order(keys, rep);
    if (order.empty()) return out;
    bool shown = keys[order[0]].value[0] >= 0.5f;
    RepInterval cur;
    if (shown) {
        cur.begin = 0.0;
        cur.begin_key = order[0];
        cur.begin_is_start = true;
    }
    for (size_t i = 1; i < order.size(); ++i) {
        const RepKey& k = keys[order[i]];
        const bool s = k.value[0] >= 0.5f;
        if (s && !shown) {
            cur = RepInterval();
            cur.begin = k.time;
            cur.begin_key = order[i];
            shown = true;
        } else if (!s && shown) {
            cur.end = k.time;
            cur.end_key = order[i];
            out.push_back(cur);
            shown = false;
        }
    }
    if (shown) {
        cur.end = duration;
        cur.end_key = -1;
        out.push_back(cur);
    }
    return out;
}

void rep_move_interval(std::vector<RepKey>* keys, uint32_t rep, const RepInterval& iv, double new_begin, double new_end, double duration) {
    // The room it has between its neighbours and the movie
    double lo = 0.0, hi = duration;
    const std::vector<RepInterval> all = rep_shown_intervals(*keys, rep, duration);
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i].begin_key != iv.begin_key || all[i].end_key != iv.end_key) continue;
        // A gap is left between stretches: keys on the same time are one key, so they would melt together
        if (i > 0) lo = all[i - 1].end + REP_MIN_LENGTH;
        if (i + 1 < all.size()) hi = all[i + 1].begin - REP_MIN_LENGTH;
    }
    const double length = iv.end - iv.begin;
    const bool only_moved = fabs((new_end - new_begin) - length) < 1.0e-6;
    if (only_moved) {
        if (new_begin < lo) { new_end += lo - new_begin; new_begin = lo; }
        if (new_end > hi) { new_begin -= new_end - hi; new_end = hi; }
    }
    new_begin = std::clamp(new_begin, lo, hi - REP_MIN_LENGTH);
    new_end = std::clamp(new_end, new_begin + REP_MIN_LENGTH, hi);

    if (iv.begin_key >= 0) {
        if (iv.begin_is_start) {
            // Shown from the start by its first key: to start later it needs a hidden key at the start
            if (new_begin > REP_EPS) {
                (*keys)[iv.begin_key].time = new_begin;
                keys->push_back(hidden_key(rep, 0.0));
            }
        } else {
            (*keys)[iv.begin_key].time = new_begin;
        }
    }
    if (iv.end_key >= 0) {
        (*keys)[iv.end_key].time = new_end;
    } else if (new_end < duration - REP_EPS) {
        keys->push_back(hidden_key(rep, new_end));
    }
}

bool rep_add_interval(std::vector<RepKey>* keys, uint32_t rep, double begin, double end, double duration) {
    const std::vector<RepInterval> all = rep_shown_intervals(*keys, rep, duration);
    double next = duration;
    for (const RepInterval& iv : all) {
        if (begin >= iv.begin - REP_MIN_LENGTH && begin < iv.end + REP_MIN_LENGTH) return false;
        if (iv.begin > begin) next = std::min(next, iv.begin - REP_MIN_LENGTH);
    }
    end = std::min(end, next);
    if (end - begin < REP_MIN_LENGTH) return false;
    if (all.empty() && begin > REP_EPS) keys->push_back(hidden_key(rep, 0.0));
    keys->push_back(shown_key(rep, begin));
    if (end < duration - REP_EPS) keys->push_back(hidden_key(rep, end));
    return true;
}

void rep_remove_interval(std::vector<RepKey>* keys, const RepInterval& iv) {
    int a = iv.begin_key, b = iv.end_key;
    if (a < b) std::swap(a, b);
    if (a >= 0 && a < (int)keys->size()) keys->erase(keys->begin() + a);
    if (b >= 0 && b < (int)keys->size()) keys->erase(keys->begin() + b);
}

void rep_swap_at(std::vector<RepKey>* keys, uint32_t from, uint32_t to, double t) {
    auto put = [&](uint32_t rep, bool shown_before, bool shown_after) {
        bool has_keys = false;
        for (RepKey& k : *keys) {
            if (k.rep != rep || k.prop != (int)RepProp::Visible) continue;
            has_keys = true;
            if (fabs(k.time - t) < REP_EPS) {
                k.value[0] = shown_after ? 1.0f : 0.0f;
                k.ease = KeyEase::Hold;
                return;
            }
        }
        if (!has_keys && t > REP_EPS) keys->push_back(shown_before ? shown_key(rep, 0.0) : hidden_key(rep, 0.0));
        keys->push_back(shown_after ? shown_key(rep, t) : hidden_key(rep, t));
    };
    put(from, true, false);
    put(to, false, true);
}

void rep_move_group(std::vector<RepKey>* keys, const std::vector<uint32_t>& reps, double begin, double end, double new_begin, double new_end, double duration) {
    const double d0 = new_begin - begin, d1 = new_end - end;
    const bool left = fabs(d1) < 1.0e-9 && fabs(d0) > 1.0e-9;
    const bool right = fabs(d0) < 1.0e-9 && fabs(d1) > 1.0e-9;
    for (uint32_t rep : reps) {
        const std::vector<RepInterval> all = rep_shown_intervals(*keys, rep, duration);
        for (const RepInterval& iv : all) {
            if (iv.begin < begin - REP_EPS || iv.end > end + REP_EPS) continue;
            double b = iv.begin, e = iv.end;
            if (left) {
                if (fabs(iv.begin - begin) > REP_EPS) continue;
                b = new_begin;
            } else if (right) {
                if (fabs(iv.end - end) > REP_EPS) continue;
                e = new_end;
            } else {
                b += d0;
                e += d0;
            }
            rep_move_interval(keys, rep, iv, b, e, duration);
            break;   // The keys of the others may have moved: one stretch a representation each time it is asked
        }
    }
}

std::vector<RepInterval> rep_union_intervals(std::vector<RepInterval> intervals) {
    std::sort(intervals.begin(), intervals.end(), [](const RepInterval& a, const RepInterval& b) { return a.begin < b.begin; });
    std::vector<RepInterval> out;
    for (const RepInterval& iv : intervals) {
        if (!out.empty() && iv.begin <= out.back().end + REP_EPS) {
            out.back().end = std::max(out.back().end, iv.end);
        } else {
            RepInterval u;
            u.begin = iv.begin;
            u.end = iv.end;
            out.push_back(u);
        }
    }
    return out;
}

bool rep_name_split(const char* name, std::string* group, std::string* member) {
    const char* h = strchr(name, '-');
    if (h && h != name && h[1] != '\0') {
        group->assign(name, (size_t)(h - name));
        member->assign(h + 1);
        return true;
    }
    group->assign(name);
    member->clear();
    return false;
}

std::vector<RepRow> rep_group_rows(const std::vector<std::string>& names, const std::vector<std::string>& collapsed) {
    struct Group { std::string name; std::vector<int> members; };
    std::vector<Group> groups;
    std::vector<std::string> member_names(names.size());
    for (size_t i = 0; i < names.size(); ++i) {
        std::string g, m;
        rep_name_split(names[i].c_str(), &g, &m);
        member_names[i] = m;
        size_t gi = 0;
        while (gi < groups.size() && groups[gi].name != g) ++gi;
        if (gi == groups.size()) groups.push_back({g, {}});
        groups[gi].members.push_back((int)i);
    }
    std::vector<RepRow> rows;
    for (const Group& g : groups) {
        if (g.members.size() == 1) {
            RepRow r;
            r.rep = g.members[0];
            r.label = names[(size_t)g.members[0]];
            r.group = g.name;
            rows.push_back(r);
            continue;
        }
        RepRow h;
        h.header = true;
        h.rep = g.members[0];
        h.label = g.name;
        h.group = g.name;
        h.members = (int)g.members.size();
        rows.push_back(h);
        if (std::find(collapsed.begin(), collapsed.end(), g.name) != collapsed.end()) continue;
        for (int m : g.members) {
            RepRow r;
            r.rep = m;
            r.label = member_names[(size_t)m].empty() ? names[(size_t)m] : member_names[(size_t)m];
            r.group = g.name;
            r.indented = true;
            rows.push_back(r);
        }
    }
    return rows;
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

std::vector<CameraBand> camera_bands(const std::vector<CameraKeyframe>& keys) {
    std::vector<CameraBand> bands;
    const int n = (int)keys.size();
    for (int i = 0; i < n;) {
        if (!keys[i].follow) { ++i; continue; }
        int j = i;
        while (j + 1 < n && keys[j + 1].follow && keys[j + 1].follow_atom == keys[i].follow_atom) ++j;
        CameraBand b;
        b.kind = keys[i].follow_atom >= 0 ? CameraBandKind::LookAtAtom : CameraBandKind::FollowTarget;
        b.first = i;
        b.last = j;
        b.begin = keys[i].time;
        b.end = keys[j].time;
        b.atom = keys[i].follow_atom;
        bands.push_back(b);
        i = j + 1;
    }
    for (int i = 1; i < n; ++i) {
        if (keys[i].spin_turns == 0) continue;
        CameraBand b;
        b.kind = CameraBandKind::Spin;
        b.first = i - 1;
        b.last = i;
        b.begin = keys[i - 1].time;
        b.end = keys[i].time;
        b.turns = keys[i].spin_turns;
        bands.push_back(b);
    }
    return bands;
}

std::string camera_key_label(const CameraKeyframe& key, int index) {
    std::string s = std::to_string(index + 1);
    if (key.name[0] != '\0') s += std::string(" ") + key.name;
    return s;
}

CameraKeyframe camera_key_on_path(const std::vector<CameraKeyframe>& keys, double time, bool loop) {
    CameraKeyframe key = {};
    ViewTransform vt;
    float fov_y;
    camera_keyframes_evaluate(&vt, &fov_y, keys.data(), keys.size(), time, loop);
    key.transform = vt;
    key.fov_y = fov_y;
    key.time = time;

    const CameraKeyframe* prev = nullptr;
    const CameraKeyframe* next = nullptr;
    for (const CameraKeyframe& k : keys) {
        if (k.time <= time) prev = &k;
        else if (!next) next = &k;
    }
    const CameraKeyframe* ref = prev ? prev : next;
    if (!ref || !ref->follow) return key;
    if (prev && next && (!next->follow || next->follow_atom != prev->follow_atom)) return key;

    key.follow = true;
    key.follow_atom = ref->follow_atom;
    if (prev && next) {
        const double u = (time - prev->time) / std::max(next->time - prev->time, 1.0e-9);
        key.follow_center = prev->follow_center + (next->follow_center - prev->follow_center) * (float)u;
    } else {
        key.follow_center = ref->follow_center;
    }
    return key;
}

void camera_path_at(const CameraPathSamples& s, double time, vec3_t* eye, vec3_t* look) {
    *eye = vec3_t{0, 0, 0};
    *look = vec3_t{0, 0, 0};
    const size_t n = s.time.size();
    if (n == 0 || s.eye.size() < n || s.look.size() < n) return;
    if (n == 1 || time <= s.time[0]) { *eye = s.eye[0]; *look = s.look[0]; return; }
    if (time >= s.time[n - 1]) { *eye = s.eye[n - 1]; *look = s.look[n - 1]; return; }
    const size_t j = (size_t)(std::upper_bound(s.time.begin(), s.time.end(), time) - s.time.begin());
    const size_t i = j - 1;
    const float u = (float)((time - s.time[i]) / std::max(s.time[j] - s.time[i], 1.0e-12));
    *eye = s.eye[i] + (s.eye[j] - s.eye[i]) * u;
    *look = s.look[i] + (s.look[j] - s.look[i]) * u;
}

double camera_tick_step(double span, int max_ticks) {
    if (span <= 0.0 || max_ticks < 1) return 1.0;
    // In hundredths of a second, so that the steps are exact
    int64_t decade = 1;
    for (int k = 0; k < 10; ++k, decade *= 10) {
        for (int64_t m : {1, 2, 5}) {
            const double step = (double)(m * decade) / 100.0;
            if (span / step <= (double)max_ticks) return step;
        }
    }
    return span;
}

bool clip_segment_near(vec4_t* a, vec4_t* b) {
    const float eps = 1.0e-3f;
    const bool in_a = a->w > eps, in_b = b->w > eps;
    if (!in_a && !in_b) return false;
    if (in_a && in_b) return true;
    vec4_t* out = in_a ? b : a;
    const vec4_t in = in_a ? *a : *b;
    const float t = (in.w - eps) / (in.w - out->w);
    out->x = in.x + (out->x - in.x) * t;
    out->y = in.y + (out->y - in.y) * t;
    out->z = in.z + (out->z - in.z) * t;
    out->w = eps;
    return true;
}

bool camera_spin_ring(const CameraKeyframe& from, const CameraKeyframe& to, vec3_t* center, vec3_t* u, vec3_t* v, float* radius) {
    vec3_t axis = {0, 1, 0};
    switch (to.spin_axis) {
    case SpinAxis::ViewUp: axis = from.transform.orientation * vec3_t{0, 1, 0}; break;
    case SpinAxis::WorldX: axis = {1, 0, 0}; break;
    case SpinAxis::WorldZ: axis = {0, 0, 1}; break;
    default: break;
    }
    axis = vec3_normalize(axis);
    const vec3_t look = camera_get_look_at(to.transform);
    const vec3_t d = to.transform.position - look;
    const float h = vec3_dot(d, axis);
    const vec3_t w = d - axis * h;
    const float r = vec3_length(w);
    if (r < 1.0e-4f) return false;
    *center = look + axis * h;
    *u = w / r;
    *v = vec3_cross(axis, *u);
    *radius = r;
    return true;
}

bool ray_plane_hit(vec3_t origin, vec3_t dir, vec3_t plane_point, vec3_t plane_normal, vec3_t* out) {
    const float denom = vec3_dot(dir, plane_normal);
    if (fabsf(denom) < 1.0e-6f) return false;
    const float t = vec3_dot(plane_point - origin, plane_normal) / denom;
    if (t < 0.0f) return false;
    *out = origin + dir * t;
    return true;
}

float polyline_nearest(const std::vector<vec2_t>& pts, const std::vector<char>& ok, vec2_t q, int* segment, float* along) {
    float best = FLT_MAX;
    for (size_t i = 1; i < pts.size() && i < ok.size(); ++i) {
        if (!ok[i - 1] || !ok[i]) continue;
        const float ax = pts[i - 1].x, ay = pts[i - 1].y;
        const float dx = pts[i].x - ax, dy = pts[i].y - ay;
        const float len2 = dx * dx + dy * dy;
        float u = len2 > 1.0e-12f ? ((q.x - ax) * dx + (q.y - ay) * dy) / len2 : 0.0f;
        u = std::min(std::max(u, 0.0f), 1.0f);
        const float ex = q.x - (ax + dx * u), ey = q.y - (ay + dy * u);
        const float dist = sqrtf(ex * ex + ey * ey);
        if (dist < best) {
            best = dist;
            if (segment) *segment = (int)i - 1;
            if (along) *along = u;
        }
    }
    return best;
}

bool camera_key_set_eye(CameraKeyframe* key, vec3_t eye) {
    const vec3_t look = camera_get_look_at(key->transform);
    ViewTransform t = key->transform;
    t.position = eye;
    if (!camera_aim_at(&t, look)) return false;
    key->transform = t;
    return true;
}

bool camera_key_set_look(CameraKeyframe* key, vec3_t look) {
    ViewTransform t = key->transform;
    if (!camera_aim_at(&t, look)) return false;
    key->transform = t;
    return true;
}

void camera_key_translate(CameraKeyframe* key, vec3_t delta) {
    key->transform.position = key->transform.position + delta;
    if (key->follow) key->follow_center = key->follow_center + delta;
}
