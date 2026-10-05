#include "utest.h"

#include <movie_overlay.h>

#include <math.h>

static MovieOverlay make_overlay(double begin, double end, float fade_in, float fade_out) {
    MovieOverlay o;
    o.begin = begin;
    o.end = end;
    o.fade_in = fade_in;
    o.fade_out = fade_out;
    return o;
}

UTEST(viamd_movie_overlay, it_is_shown_only_within_its_time_range) {
    const MovieOverlay o = make_overlay(2.0, 6.0, 0.0f, 0.0f);
    EXPECT_NEAR(0.0f, movie_overlay_alpha(o, 1.99), 1.0e-6f);
    EXPECT_NEAR(1.0f, movie_overlay_alpha(o, 2.0), 1.0e-6f);
    EXPECT_NEAR(1.0f, movie_overlay_alpha(o, 4.0), 1.0e-6f);
    EXPECT_NEAR(1.0f, movie_overlay_alpha(o, 6.0), 1.0e-6f);
    EXPECT_NEAR(0.0f, movie_overlay_alpha(o, 6.01), 1.0e-6f);
}

UTEST(viamd_movie_overlay, it_fades_in_and_out_within_its_range) {
    const MovieOverlay o = make_overlay(2.0, 10.0, 1.0f, 2.0f);
    EXPECT_NEAR(0.0f, movie_overlay_alpha(o, 2.0), 1.0e-6f);
    EXPECT_NEAR(0.5f, movie_overlay_alpha(o, 2.5), 1.0e-6f);
    EXPECT_NEAR(1.0f, movie_overlay_alpha(o, 3.0), 1.0e-6f);
    EXPECT_NEAR(1.0f, movie_overlay_alpha(o, 8.0), 1.0e-6f);
    EXPECT_NEAR(0.5f, movie_overlay_alpha(o, 9.0), 1.0e-6f);
    EXPECT_NEAR(0.0f, movie_overlay_alpha(o, 10.0), 1.0e-6f);
}

UTEST(viamd_movie_overlay, fades_that_do_not_fit_share_the_time) {
    /* 4 s of range, 3 s + 3 s of fades: scaled to 2 s each, so it just reaches full in the middle */
    const MovieOverlay o = make_overlay(0.0, 4.0, 3.0f, 3.0f);
    EXPECT_NEAR(0.5f, movie_overlay_alpha(o, 1.0), 1.0e-6f);
    EXPECT_NEAR(1.0f, movie_overlay_alpha(o, 2.0), 1.0e-6f);
    EXPECT_NEAR(0.5f, movie_overlay_alpha(o, 3.0), 1.0e-6f);
}

UTEST(viamd_movie_overlay, a_disabled_or_empty_range_is_never_shown) {
    MovieOverlay o = make_overlay(0.0, 4.0, 0.0f, 0.0f);
    o.enabled = false;
    EXPECT_NEAR(0.0f, movie_overlay_alpha(o, 2.0), 1.0e-6f);

    const MovieOverlay empty = make_overlay(3.0, 3.0, 0.0f, 0.0f);
    EXPECT_NEAR(0.0f, movie_overlay_alpha(empty, 3.0), 1.0e-6f);
}

UTEST(viamd_movie_overlay, one_pixel_covers_what_the_field_of_view_says) {
    /* At distance 10 and a right angle of view the frame is 20 units high */
    const double per_px = movie_units_per_pixel(10.0f, 1.5707963268f, 1000.0f);
    EXPECT_NEAR(0.02, per_px, 1.0e-6);
    EXPECT_NEAR(0.0, movie_units_per_pixel(10.0f, 1.0f, 0.0f), 1.0e-12);
}

UTEST(viamd_movie_overlay, a_scale_bar_is_one_two_or_five_of_a_power_of_ten) {
    /* A frame 1000 px wide at 0.02 per pixel is 20 units, and a quarter of it is 5 */
    EXPECT_NEAR(5.0f, movie_scale_bar_length(0.02, 1000.0, 0.25), 1.0e-5f);
    EXPECT_NEAR(2.0f, movie_scale_bar_length(0.02, 1000.0, 0.15), 1.0e-5f);   /* 3 wanted */
    EXPECT_NEAR(1.0f, movie_scale_bar_length(0.02, 1000.0, 0.09), 1.0e-5f);   /* 1.8 wanted */
    EXPECT_NEAR(0.5f, movie_scale_bar_length(0.001, 1000.0, 0.6), 1.0e-6f);   /* 0.6 wanted */
    EXPECT_NEAR(50.0f, movie_scale_bar_length(0.5, 1000.0, 0.15), 1.0e-4f);   /* 75 wanted */
    EXPECT_NEAR(0.0f, movie_scale_bar_length(0.0, 1000.0, 0.25), 1.0e-9f);
}
