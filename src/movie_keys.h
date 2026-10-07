#pragma once

#include <gfx/camera.h>
#include <movie_overlay.h>

#include <stddef.h>
#include <stdint.h>
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
