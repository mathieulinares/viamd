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

// A camera pose that a movie passes through at 'time' seconds on the movie timeline.
struct CameraKeyframe {
    ViewTransform transform = {};
    float  fov_y = (3.1415926534f / 4.0f);
    double time = 0.0;
};
