#pragma once

#include <gfx/camera.h>
#include <movie_overlay.h>

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

// What is keyed on a movie's timeline besides the camera: look parameters (background, depth of field,
// clipping ...), each with its own keys. The application has a table of the parameters, a key refers to
// one by its index there.
struct ParamKey {
    int     param = 0;
    double  time = 0.0;
    float   value[3] = {0, 0, 0};   // A colour uses all three, a scalar the first
    KeyEase ease = KeyEase::Smooth; // Shapes the stretch leading to this key
};

// The value of a parameter at 'time' from the keys that are for it, which need not be sorted. Returns
// false if there are none. Passes through every key and holds outside the first and last.
bool param_keys_evaluate(float* out, int comps, const ParamKey* keys, size_t count, int param, double time);

// What can be keyed on a representation. The numbers are saved in workspaces: never renumber, add at the end.
// Scale0..2 are the components of the representation's scale (what each is depends on its type).
enum class RepProp : int {
    Visible = 0,
    Scale0,
    Scale1,
    Scale2,
    TintScale,
    Saturation,
    BaseColor,   // Shown where the colour mapping is Uniform
    TintColor,   // Shown where the tint scale is above zero
    Count,
};

// How many numbers a property has: a colour has three
inline int rep_prop_comps(int prop) { return prop == (int)RepProp::BaseColor || prop == (int)RepProp::TintColor ? 3 : 1; }

// One key of one property of one representation, which is named by its id (not its place in the list)
struct RepKey {
    uint32_t rep = 0;
    int      prop = 0;               // A RepProp
    double   time = 0.0;
    float    value[3] = {0, 0, 0};   // A colour uses all three, the others the first; Visible is 0 or 1
    KeyEase  ease = KeyEase::Smooth; // Shapes the stretch leading to this key
};

// The value of a property of a representation at 'time' from the keys for it, which need not be sorted.
// Returns false if there are none. Visible is held: it changes at its keys, it is not blended between them.
// 'out' has rep_prop_comps(prop) numbers.
bool rep_keys_evaluate(float* out, const RepKey* keys, size_t count, uint32_t rep, int prop, double time);

// How much of a representation is there at 'time', 0 (gone) to 1 (all of it), from its Visible keys. At a key the
// factor starts to go to the key's value (1 shown, 0 hidden), taking 'transition' seconds, smoothly; from wherever
// it was if the previous change has not finished. The first key holds from the start. With a transition of 0 it
// is held and changes at its keys, as rep_keys_evaluate does. Returns false if there are no keys.
bool rep_visible_factor(float* out, const RepKey* keys, size_t count, uint32_t rep, double time, double transition);

// ## Representations over time as stretches
//
// What the Visible keys of a representation say, as the times it is shown: from the key that shows it to the key that hides
// it, or to the end of the movie. (The transition of Visible is not part of this: it comes after the key.)

struct RepInterval {
    double begin = 0.0;
    double end = 0.0;
    int    begin_key = -1;            // The key (an index into the keys it was made from) that shows it
    int    end_key = -1;              // The key that hides it again, -1 when nothing does: it lasts to the end of the movie
    bool   begin_is_start = false;    // Shown from the start by its first key, which can be later: before it the first key holds
};

std::vector<RepInterval> rep_shown_intervals(const std::vector<RepKey>& keys, uint32_t rep, double duration);

// Moves the ends of a stretch to new times, with the keys it is made of (and a hidden key at the start of the movie or at the
// new end where there was none, so that nothing else changes). The new times are kept inside the neighbouring stretches and the
// movie; a stretch that was only moved keeps its length. Not sorted afterwards.
void rep_move_interval(std::vector<RepKey>* keys, uint32_t rep, const RepInterval& iv, double new_begin, double new_end, double duration);

// Adds a stretch from 'begin' to 'end' where the representation is hidden, ending before the next one. False if it is shown at
// 'begin' or there is not room (less than 'min_length').
bool rep_add_interval(std::vector<RepKey>* keys, uint32_t rep, double begin, double end, double duration);

// Takes out the keys that make a stretch: the representation is hidden there afterwards
void rep_remove_interval(std::vector<RepKey>* keys, const RepInterval& iv);

// At time t, 'from' goes and 'to' comes: a hidden key for one and a shown key for the other. A representation without keys is
// taken to have been shown ('from') or hidden ('to') up to then.
void rep_swap_at(std::vector<RepKey>* keys, uint32_t from, uint32_t to, double t);

// Moves the stretches of several representations that lie inside [begin, end] when that group of stretches is dragged to
// [new_begin, new_end]: all of them by the same time when it was only moved, the ones that start at 'begin' when its left end
// was dragged, the ones that end at 'end' when its right end was.
void rep_move_group(std::vector<RepKey>* keys, const std::vector<uint32_t>& reps, double begin, double end, double new_begin, double new_end, double duration);

// The union of stretches (a group's: shown whenever any of its members is)
std::vector<RepInterval> rep_union_intervals(std::vector<RepInterval> intervals);

// "protein-cpk": the group 'protein' and the member 'cpk'. A name without a hyphen in it (or one that starts or ends with it)
// is a group of its own: the member is empty and false is returned.
bool rep_name_split(const char* name, std::string* group, std::string* member);

// The rows of a list of representations in a lane: representations of one group together, at the place of the first of them,
// with a row for the group above its members when it has several (the members are left out of a group that is 'collapsed').
struct RepRow {
    bool        header = false;   // The row of a group
    int         rep = -1;         // The place in the list of a representation row; for a header the first member
    std::string label;
    std::string group;
    int         members = 1;
    bool        indented = false;
};
std::vector<RepRow> rep_group_rows(const std::vector<std::string>& names, const std::vector<std::string>& collapsed);

// Everything on the timeline that the user edits, so that it can be undone as one
struct MovieKeys {
    std::vector<CameraKeyframe> camera;
    std::vector<ParamKey> params;
    std::vector<RepKey> reps;
    std::vector<MovieOverlay> overlays;
    std::vector<MovieMarker> markers;
    bool loop = false;
    // The timing, which is scaled together with the keys
    float  duration = 0.0f;
    float  traj_begin = 0.0f;
    float  traj_end = 0.0f;
    double start_frame = 0.0;
    double end_frame = 0.0;
};

bool movie_keys_equal(const MovieKeys& a, const MovieKeys& b);
void movie_keys_scale_time(MovieKeys* keys, double scale);

// Undo and redo of edits to the keys. It is not told what changed: it is shown the keys every frame, and
// whenever they differ from the last committed state and no edit is under way (a slider being dragged, a
// point being moved) that is one step. A drag is therefore one undo, not hundreds.
class MovieHistory {
public:
    // Forgets everything, e.g. when another workspace is loaded
    void clear(const MovieKeys& current);

    void update(const MovieKeys& current, bool editing);

    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }

    // 'state' is the present keys. On true it holds the ones to go back to (or forward to).
    bool undo(MovieKeys* state);
    bool redo(MovieKeys* state);

private:
    static constexpr size_t MAX_STEPS = 200;

    std::vector<MovieKeys> undo_;
    std::vector<MovieKeys> redo_;
    MovieKeys committed_;
    bool has_committed_ = false;
};

// The frames of a movie that a recording makes, first to last (inclusive), out of 'num_frames' at 'fps'. With a
// range, the frames between begin and end seconds, which always include at least one.
void movie_frame_range(int num_frames, double fps, bool use_range, double begin, double end, int* first, int* last);

// The size of a frame at a percentage of the full size, kept even for the video encoder
void movie_scaled_size(int* w, int* h, int percent);

// How long a recording has left from its pace so far: 'done' of 'total' frames in 'active_seconds'. False
// until there is enough to go on.
bool movie_time_left(int done, int total, double active_seconds, double* seconds);

// A time moved to the nearest frame of the movie (at 'fps'), kept within 0 .. duration
double movie_snap_to_frame(double time, double fps, double duration);
