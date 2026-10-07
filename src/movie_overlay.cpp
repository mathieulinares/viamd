#include "movie_overlay.h"

#include <math.h>

MovieOverlay movie_overlay_default_logo() {
    MovieOverlay o;
    o.type = MovieOverlayType::Logo;
    o.begin = 0.0;
    o.end = 3600.0;   // The longest a movie can be, so it follows the length of the movie
    o.fade_in = 0.0f;
    o.fade_out = 0.0f;
    o.anchor = MovieOverlayAnchor::TopLeft;
    o.size = 0.08f;
    return o;
}

float movie_overlay_size_px(const MovieOverlay& o, float frame_height_px) {
    return o.size_unit == MovieOverlaySizeUnit::Points ? o.size * frame_height_px / MOVIE_OVERLAY_POINT_REFERENCE_HEIGHT
                                                       : o.size * frame_height_px;
}

float movie_overlay_convert_size(float size, MovieOverlaySizeUnit from, MovieOverlaySizeUnit to) {
    if (from == to) return size;
    return to == MovieOverlaySizeUnit::Points ? size * MOVIE_OVERLAY_POINT_REFERENCE_HEIGHT : size / MOVIE_OVERLAY_POINT_REFERENCE_HEIGHT;
}

void movie_overlay_size_range(MovieOverlaySizeUnit unit, float* lo, float* hi) {
    if (unit == MovieOverlaySizeUnit::Points) {
        *lo = 6.0f;
        *hi = 400.0f;
    } else {
        *lo = 0.01f;
        *hi = 0.3f;
    }
}

float movie_overlay_alpha(const MovieOverlay& o, double time) {
    if (!o.enabled || o.end <= o.begin || time < o.begin || time > o.end) return 0.0f;

    const double len = o.end - o.begin;
    double fi = o.fade_in  > 0.0f ? (double)o.fade_in  : 0.0;
    double fo = o.fade_out > 0.0f ? (double)o.fade_out : 0.0;
    if (fi + fo > len) {
        const double s = len / (fi + fo);
        fi *= s;
        fo *= s;
    }

    double a = 1.0;
    if (fi > 0.0 && time < o.begin + fi) a = (time - o.begin) / fi;
    if (fo > 0.0 && time > o.end - fo)   a = fmin(a, (o.end - time) / fo);
    return (float)(a < 0.0 ? 0.0 : (a > 1.0 ? 1.0 : a));
}

float movie_scale_bar_length(double units_per_pixel, double span_px, double target) {
    const double want = units_per_pixel * span_px * target;
    if (!(want > 0.0) || !isfinite(want)) return 0.0f;

    // The largest of 1, 2, 5 x 10^k that is not longer than what is wanted
    const double mag = pow(10.0, floor(log10(want)));
    const double m = want / mag;
    const double step = m >= 5.0 ? 5.0 : (m >= 2.0 ? 2.0 : 1.0);
    return (float)(step * mag);
}

double movie_units_per_pixel(float distance, float fov_y, float frame_height_px) {
    if (frame_height_px <= 0.0f) return 0.0;
    return 2.0 * (double)distance * tan((double)fov_y * 0.5) / (double)frame_height_px;
}

void movie_time_bar_profile(MovieTimeBarProfile* out, double duration, int samples, const std::function<double(double)>& quantity_at) {
    out->duration = duration;
    out->distance.assign((size_t)(samples < 1 ? 2 : samples + 1), 0.0);
    const int n = (int)out->distance.size() - 1;
    double prev = quantity_at(0.0);
    for (int i = 1; i <= n; ++i) {
        const double q = quantity_at(duration * (double)i / (double)n);
        out->distance[i] = out->distance[i - 1] + fabs(q - prev);
        prev = q;
    }
}

double movie_time_bar_moved(const MovieTimeBarProfile& p, double time) {
    if (p.distance.size() < 2 || p.duration <= 0.0) return 0.0;
    const double x = fmin(fmax(time / p.duration, 0.0), 1.0) * (double)(p.distance.size() - 1);
    const size_t i = (size_t)floor(x);
    if (i + 1 >= p.distance.size()) return p.distance.back();
    return p.distance[i] + (p.distance[i + 1] - p.distance[i]) * (x - (double)i);
}

double movie_time_bar_progress(const MovieTimeBarProfile& p, double time) {
    const double total = p.total();
    return total > 0.0 ? movie_time_bar_moved(p, time) / total : 0.0;
}

double movie_quantity_speed(const std::function<double(double)>& quantity_at, double time, double step) {
    if (step <= 0.0) return 0.0;
    return fabs(quantity_at(time + step) - quantity_at(time - step)) / (2.0 * step);
}
