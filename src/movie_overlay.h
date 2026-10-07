#pragma once

#include <stddef.h>

#include <functional>
#include <vector>

// Text and a scale bar that are put on the frames of a movie, shown for a stretch of its timeline
enum class MovieOverlayType : int {
    Text,        // A title or a caption
    Timestamp,   // The time of the trajectory frame that is shown
    ScaleBar,    // A bar of a given length in the structure, which follows the camera
    Logo,        // The VIAMD logo
    Image,       // An image file (png or jpg)
    TimeBar,     // How far the trajectory has gone, counted forward even when it is played backward
    Count,
};

enum class MovieOverlayAnchor : int {
    TopLeft, TopCenter, TopRight,
    MiddleLeft, Center, MiddleRight,
    BottomLeft, BottomCenter, BottomRight,
    Count,
};

// What 'size' of an overlay is. A point is a pixel of a frame that is 1080 pixels high, scaled with the frame, so
// the same number looks the same at any resolution.
enum class MovieOverlaySizeUnit : int {
    FrameHeight,   // A part of the height of the frame
    Points,
    Count,
};

constexpr float MOVIE_OVERLAY_POINT_REFERENCE_HEIGHT = 1080.0f;

struct MovieOverlay {
    MovieOverlayType   type = MovieOverlayType::Text;
    bool               enabled = true;
    double             begin = 0.0;           // On the movie's timeline, in seconds
    double             end = 5.0;
    float              fade_in = 0.5f;        // Seconds to appear and to go away, within begin..end
    float              fade_out = 0.5f;
    MovieOverlayAnchor anchor = MovieOverlayAnchor::BottomLeft;
    float              size = 0.05f;          // The height of the text (of the logo), in size_unit
    MovieOverlaySizeUnit size_unit = MovieOverlaySizeUnit::FrameHeight;
    float              color[4] = {1, 1, 1, 1};
    float              background[4] = {0, 0, 0, 0};  // A plate behind it, none while its alpha is 0
    char               text[128] = "";
    float              length = 0.0f;         // Scale bar: its length in Angstrom, 0 chooses one
    char               path[512] = "";         // Image: the file
    float              width = 0.4f;          // Time bar: its width, as a part of the width of the frame
    bool               show_elapsed = true;   // Time bar: the time that has gone, over the whole
    bool               show_speed = false;    // Time bar: how fast the trajectory plays, relative to the Animation panel
};

// The logo in the top left corner for the whole movie, which a movie starts with
MovieOverlay movie_overlay_default_logo();

// The height of an overlay in pixels of a frame 'frame_height_px' high
float movie_overlay_size_px(const MovieOverlay& o, float frame_height_px);

// The same height written in another unit, e.g. to keep it as it is when the unit is changed
float movie_overlay_convert_size(float size, MovieOverlaySizeUnit from, MovieOverlaySizeUnit to);

// The range a size can be set in, in its unit
void movie_overlay_size_range(MovieOverlaySizeUnit unit, float* lo, float* hi);

// How visible an overlay is at a time, 0..1. Zero outside begin..end, rising over fade_in and falling over
// fade_out; where they do not both fit, they share the time in proportion.
float movie_overlay_alpha(const MovieOverlay& o, double time);

// A length in the structure that suits a bar a fraction 'target' of a frame 'span_px' wide, of the form 1, 2
// or 5 times a power of ten. units_per_pixel is how much of the structure one pixel covers.
float movie_scale_bar_length(double units_per_pixel, double span_px, double target);

// How much of the structure one pixel of the frame covers, at the distance the camera looks at
double movie_units_per_pixel(float distance, float fov_y, float frame_height_px);

// How far a movie has taken the trajectory by each time, for the time bar. What is counted is the movement along the
// trajectory, whatever its direction: a trajectory that is played backward still fills the bar forward, and a stretch
// that is played fast fills it fast, a slow one slowly, a hold not at all.
struct MovieTimeBarProfile {
    std::vector<double> distance;   // Moved by each sample, in the unit of the quantity
    double duration = 0.0;          // The movie's length, the samples are even over it
    double total() const { return distance.empty() ? 0.0 : distance.back(); }
};

// 'quantity_at(t)' is the trajectory time (or frame) at a movie time. Sampled 'samples' times over 0 .. duration.
void movie_time_bar_profile(MovieTimeBarProfile* out, double duration, int samples, const std::function<double(double)>& quantity_at);

// 0 (nothing moved yet) .. 1 (all moved) at a movie time. A trajectory that does not move at all gives 0.
double movie_time_bar_progress(const MovieTimeBarProfile& p, double time);

// How much has moved by a movie time, in the unit of the quantity
double movie_time_bar_moved(const MovieTimeBarProfile& p, double time);

// How fast the quantity changes at a movie time, per second of the movie, whatever the direction
double movie_quantity_speed(const std::function<double(double)>& quantity_at, double time, double step);
