#pragma once

#include <core/md_vec_math.h>

struct ViewTransform {
    quat_t orientation = {0, 0, 0, 1};
    vec3_t position = {0, 0, 0};
	float  distance = 10.0f;
};

struct Camera : ViewTransform {
    float near_plane = 1.0f;
    float far_plane = 10000.0f;
    float fov_y = (3.1415926534f / 4.0f);

    Camera& operator = (const ViewTransform& t) {
        this->orientation = t.orientation;
        this->position = t.position;
        this->distance = t.distance;
        return *this;
	}
};

// How the camera, and the trajectory frame, move in the stretch leading to a keyframe
enum class KeyEase : int {
    Smooth,     // Through the key without stopping
    EaseInOut,  // Starts and ends slowly, so it stops at the keys
    Linear,     // Constant speed in a straight line
    Hold,       // Stays as it was until the key, where it jumps
    Count,
};

// What a keyframe's extra turns go around
enum class SpinAxis : int {
    ViewUp,   // The up direction of the camera at the start of the segment
    WorldY,
    WorldX,
    WorldZ,
    Count,
};

// A camera pose that a movie passes through at 'time' seconds on the movie timeline.
struct CameraKeyframe {
    ViewTransform transform = {};
    float  fov_y = (3.1415926534f / 4.0f);
    double time = 0.0;

    KeyEase ease = KeyEase::Smooth;
    // The trajectory frame shown at this time. The keys that have one decide how the trajectory plays in the movie.
    bool   use_frame = false;
    double frame = 0.0;

    // Extra whole turns of the camera around what it looks at, made over the segment that ends at this
    // key (positive is counter-clockwise seen from the tip of the axis). A whole number so that the camera
    // still arrives at this key's pose.
    int      spin_turns = 0;
    SpinAxis spin_axis = SpinAxis::ViewUp;
    bool     spin_constant_speed = false;  // Otherwise it eases in and out

    // The look-at point moves with a target (the movie's follow target) rather than staying put. follow_center
    // is where the target was when the key was made, so the key's look-at is kept relative to it.
    bool   follow = false;
    vec3_t follow_center = {0, 0, 0};
    // With follow set: the atom this key's look-at tracks. Negative means the movie's follow target.
    int32_t follow_atom = -1;
};
