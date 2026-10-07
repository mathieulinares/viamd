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

UTEST(viamd_movie_overlay, a_movie_starts_with_the_logo_in_the_top_left_corner_for_its_whole_length) {
    const MovieOverlay o = movie_overlay_default_logo();
    EXPECT_TRUE(o.type == MovieOverlayType::Logo);
    EXPECT_TRUE(o.anchor == MovieOverlayAnchor::TopLeft);
    EXPECT_NEAR(1.0f, movie_overlay_alpha(o, 0.0), 1.0e-6f);       /* there from the first frame */
    EXPECT_NEAR(1.0f, movie_overlay_alpha(o, 1800.0), 1.0e-6f);
    EXPECT_NEAR(1.0f, movie_overlay_alpha(o, 3600.0), 1.0e-6f);    /* up to the longest a movie can be */
}

UTEST(viamd_movie_overlay, a_size_in_points_scales_with_the_frame_and_in_percent_with_its_height) {
    MovieOverlay o;
    o.size = 0.05f;
    EXPECT_NEAR(54.0f, movie_overlay_size_px(o, 1080.0f), 1.0e-4f);
    EXPECT_NEAR(108.0f, movie_overlay_size_px(o, 2160.0f), 1.0e-4f);

    o.size_unit = MovieOverlaySizeUnit::Points;
    o.size = 24.0f;
    EXPECT_NEAR(24.0f, movie_overlay_size_px(o, 1080.0f), 1.0e-4f);     /* a point is a pixel at 1080 */
    EXPECT_NEAR(48.0f, movie_overlay_size_px(o, 2160.0f), 1.0e-4f);     /* and the same look at 4K */
    EXPECT_NEAR(8.0f, movie_overlay_size_px(o, 360.0f), 1.0e-4f);
}

UTEST(viamd_movie_overlay, changing_the_unit_keeps_the_size) {
    const float pt = movie_overlay_convert_size(0.05f, MovieOverlaySizeUnit::FrameHeight, MovieOverlaySizeUnit::Points);
    EXPECT_NEAR(54.0f, pt, 1.0e-4f);
    EXPECT_NEAR(0.05f, movie_overlay_convert_size(pt, MovieOverlaySizeUnit::Points, MovieOverlaySizeUnit::FrameHeight), 1.0e-6f);
    EXPECT_NEAR(0.07f, movie_overlay_convert_size(0.07f, MovieOverlaySizeUnit::FrameHeight, MovieOverlaySizeUnit::FrameHeight), 1.0e-9f);

    float lo, hi;
    movie_overlay_size_range(MovieOverlaySizeUnit::Points, &lo, &hi);
    EXPECT_LT(lo, hi);
    EXPECT_GT(lo, 1.0f);
}

UTEST(viamd_movie_overlay, an_image_overlay_is_a_picture_like_the_logo_but_with_a_file) {
    MovieOverlay o;
    o.type = MovieOverlayType::Image;
    EXPECT_EQ('\0', o.path[0]);
    EXPECT_TRUE((int)MovieOverlayType::Image > (int)MovieOverlayType::Logo);   /* the numbers are saved: Logo stays 3 */
    EXPECT_EQ(3, (int)MovieOverlayType::Logo);
    EXPECT_EQ(4, (int)MovieOverlayType::Image);
}

/* The time bar counts movement along the trajectory, forward whichever way it plays */

static double ramp_up(double t) { return 10.0 * t; }                         /* 0 .. 100 over 10 s */
static double ramp_down(double t) { return 100.0 - 10.0 * t; }               /* the same, played backward */

UTEST(viamd_movie_overlay, a_trajectory_played_backward_fills_the_bar_forward) {
    MovieTimeBarProfile up, down;
    movie_time_bar_profile(&up, 10.0, 100, ramp_up);
    movie_time_bar_profile(&down, 10.0, 100, ramp_down);
    EXPECT_NEAR(100.0, up.total(), 1.0e-6);
    EXPECT_NEAR(100.0, down.total(), 1.0e-6);
    for (double t = 0.0; t <= 10.0; t += 1.0) {
        EXPECT_NEAR(t / 10.0, movie_time_bar_progress(up, t), 1.0e-9);
        EXPECT_NEAR(t / 10.0, movie_time_bar_progress(down, t), 1.0e-9);   /* not 1 - t/10 */
    }
}

UTEST(viamd_movie_overlay, the_bar_fills_fast_where_the_trajectory_is_fast_and_stands_still_in_a_hold) {
    /* Held for 4 s, then 100 frames in 2 s, then held again */
    auto frame = [](double t) { return t < 4.0 ? 0.0 : (t < 6.0 ? 50.0 * (t - 4.0) : 100.0); };
    MovieTimeBarProfile p;
    movie_time_bar_profile(&p, 10.0, 1000, frame);
    EXPECT_NEAR(0.0, movie_time_bar_progress(p, 3.9), 1.0e-6);
    EXPECT_NEAR(0.5, movie_time_bar_progress(p, 5.0), 1.0e-2);
    EXPECT_NEAR(1.0, movie_time_bar_progress(p, 6.1), 1.0e-6);
    EXPECT_NEAR(1.0, movie_time_bar_progress(p, 10.0), 1.0e-6);
    EXPECT_NEAR(50.0, movie_time_bar_moved(p, 5.0), 1.0);
}

UTEST(viamd_movie_overlay, a_trajectory_that_does_not_move_gives_an_empty_bar) {
    MovieTimeBarProfile p;
    movie_time_bar_profile(&p, 5.0, 50, [](double) { return 12.0; });
    EXPECT_NEAR(0.0, movie_time_bar_progress(p, 2.5), 1.0e-12);
    EXPECT_NEAR(0.0, movie_time_bar_progress(p, 99.0), 1.0e-12);
}

UTEST(viamd_movie_overlay, the_speed_has_no_direction) {
    EXPECT_NEAR(10.0, movie_quantity_speed(ramp_up, 5.0, 0.1), 1.0e-9);
    EXPECT_NEAR(10.0, movie_quantity_speed(ramp_down, 5.0, 0.1), 1.0e-9);
    EXPECT_NEAR(0.0, movie_quantity_speed([](double) { return 3.0; }, 5.0, 0.1), 1.0e-12);
}

UTEST(viamd_movie_overlay, the_time_bar_comes_after_the_image_in_the_saved_numbers) {
    EXPECT_EQ(5, (int)MovieOverlayType::TimeBar);
}

UTEST(viamd_movie_overlay, the_overlays_that_follow_the_time_bar_keep_their_saved_numbers) {
    EXPECT_EQ(6, (int)MovieOverlayType::Timeline);
    EXPECT_EQ(7, (int)MovieOverlayType::Distribution);
    EXPECT_EQ(8, (int)MovieOverlayType::PropertyVis);
}

/* The stretch of the trajectory that has been played */

UTEST(viamd_movie_overlay, the_visited_stretch_grows_forward_and_backward) {
    MovieTimeBarProfile up, down;
    movie_time_bar_profile(&up, 10.0, 100, ramp_up);
    movie_time_bar_profile(&down, 10.0, 100, ramp_down);
    double lo, hi;
    ASSERT_TRUE(movie_time_bar_visited(up, 0.0, &lo, &hi));
    EXPECT_NEAR(0.0, lo, 1.0e-9);
    EXPECT_NEAR(0.0, hi, 1.0e-9);
    ASSERT_TRUE(movie_time_bar_visited(up, 5.0, &lo, &hi));
    EXPECT_NEAR(0.0, lo, 1.0e-9);
    EXPECT_NEAR(50.0, hi, 1.0e-9);
    ASSERT_TRUE(movie_time_bar_visited(down, 5.0, &lo, &hi));
    EXPECT_NEAR(50.0, lo, 1.0e-9);
    EXPECT_NEAR(100.0, hi, 1.0e-9);
    ASSERT_TRUE(movie_time_bar_visited(down, 10.0, &lo, &hi));
    EXPECT_NEAR(0.0, lo, 1.0e-9);
    EXPECT_NEAR(100.0, hi, 1.0e-9);
}

UTEST(viamd_movie_overlay, the_visited_stretch_does_not_shrink_when_the_trajectory_turns_back) {
    auto there_and_back = [](double t) { return t < 5.0 ? 10.0 * t : 100.0 - 10.0 * t; };
    MovieTimeBarProfile p;
    movie_time_bar_profile(&p, 10.0, 200, there_and_back);
    double lo, hi;
    ASSERT_TRUE(movie_time_bar_visited(p, 9.0, &lo, &hi));
    EXPECT_NEAR(0.0, lo, 1.0e-9);
    EXPECT_NEAR(50.0, hi, 1.0e-9);
}

UTEST(viamd_movie_overlay, nothing_is_visited_without_a_profile) {
    MovieTimeBarProfile p;
    double lo = 1.0, hi = 2.0;
    EXPECT_FALSE(movie_time_bar_visited(p, 1.0, &lo, &hi));
}

/* Ticks and histograms for the plot overlays */

UTEST(viamd_movie_overlay, ticks_are_round_and_inside_the_range) {
    double t[16], step = 0.0;
    int n = movie_nice_ticks(0.0, 100.0, 5, t, 16, &step);
    ASSERT_EQ(6, n);
    EXPECT_NEAR(20.0, step, 1.0e-12);
    EXPECT_NEAR(0.0, t[0], 1.0e-12);
    EXPECT_NEAR(100.0, t[5], 1.0e-9);

    n = movie_nice_ticks(0.13, 0.87, 4, t, 16, &step);
    ASSERT_GT(n, 1);
    EXPECT_GE(t[0], 0.13 - 1.0e-9);
    EXPECT_LE(t[n - 1], 0.87 + 1.0e-9);
    EXPECT_LE(n, 5);
}

UTEST(viamd_movie_overlay, no_ticks_in_an_empty_range) {
    double t[4], step = 1.0;
    EXPECT_EQ(0, movie_nice_ticks(3.0, 3.0, 5, t, 4, &step));
    EXPECT_EQ(0, movie_nice_ticks(5.0, 3.0, 5, t, 4, &step));
}

UTEST(viamd_movie_overlay, a_histogram_counts_only_the_frames_that_were_played) {
    const float x[] = {0, 1, 2, 3, 4, 5};
    const float y[] = {0.5f, 0.5f, 1.5f, 2.5f, 2.5f, 2.5f};
    std::vector<float> c;
    movie_histogram_counts(&c, 3, 0.0, 3.0, x, y, 1, 1.0, 6, 0.0, 5.0);
    ASSERT_EQ(3, (int)c.size());
    EXPECT_NEAR(2.0, c[0], 1e-6);
    EXPECT_NEAR(1.0, c[1], 1e-6);
    EXPECT_NEAR(3.0, c[2], 1e-6);
    movie_histogram_counts(&c, 3, 0.0, 3.0, x, y, 1, 1.0, 6, 0.0, 2.0);
    EXPECT_NEAR(2.0, c[0], 1e-6);
    EXPECT_NEAR(1.0, c[1], 1e-6);
    EXPECT_NEAR(0.0, c[2], 1e-6);
}

UTEST(viamd_movie_overlay, a_histogram_takes_the_stride_and_the_scale_of_the_values) {
    const float x[] = {0, 1};
    const float y[] = {1.0f, 99.0f, 2.0f, 99.0f};   /* two values per sample, the first is read */
    std::vector<float> c;
    movie_histogram_counts(&c, 2, 0.0, 10.0, x, y, 2, 2.5, 2, 0.0, 1.0);   /* 2.5 and 5.0 */
    EXPECT_NEAR(1.0, c[0], 1e-6);
    EXPECT_NEAR(1.0, c[1], 1e-6);
}

/* A timeline overlay follows the movie: its axis is the distance moved, so it always grows to the right */

static std::vector<float> samples_every(float step, int n) {
    std::vector<float> x((size_t)n);
    for (int i = 0; i < n; ++i) x[(size_t)i] = step * (float)i;
    return x;
}

UTEST(viamd_movie_overlay, a_curve_of_a_backward_movie_grows_forward_and_reads_the_series_backward) {
    const std::vector<float> xs = samples_every(1.0f, 101);   /* series: value = x * 2 */
    auto of_sample = [&](int i) { return 2.0 * (double)xs[(size_t)i]; };
    auto at = [](double x) { return 2.0 * x; };
    MovieTimeBarProfile down;
    movie_time_bar_profile(&down, 10.0, 100, ramp_down);   /* 100 -> 0 over 10 s */
    std::vector<MovieCurvePoint> c;
    movie_elapsed_curve(&c, down, 5.0, xs.data(), (int)xs.size(), of_sample, at);
    ASSERT_GT((int)c.size(), 10);
    EXPECT_NEAR(0.0, c.front().s, 1e-9);
    EXPECT_NEAR(200.0, c.front().v, 1e-6);   /* starts where the movie starts, at 100 */
    EXPECT_NEAR(50.0, c.back().s, 1e-6);     /* half of the distance */
    EXPECT_NEAR(100.0, c.back().v, 1e-6);    /* at 50 */
    for (size_t i = 1; i < c.size(); ++i) EXPECT_GE(c[i].s, c[i - 1].s);
}

UTEST(viamd_movie_overlay, a_curve_of_a_forward_movie_is_the_series_itself) {
    const std::vector<float> xs = samples_every(1.0f, 101);
    auto of_sample = [&](int i) { return (double)xs[(size_t)i] * 0.5; };
    auto at = [](double x) { return x * 0.5; };
    MovieTimeBarProfile up;
    movie_time_bar_profile(&up, 10.0, 100, ramp_up);
    std::vector<MovieCurvePoint> c;
    movie_elapsed_curve(&c, up, 10.0, xs.data(), (int)xs.size(), of_sample, at);
    for (const MovieCurvePoint& p : c) EXPECT_NEAR(p.s * 0.5, p.v, 1e-6);
    EXPECT_NEAR(100.0, c.back().s, 1e-6);
}

UTEST(viamd_movie_overlay, a_curve_goes_on_to_the_right_when_the_movie_turns_back) {
    const std::vector<float> xs = samples_every(1.0f, 101);
    auto of_sample = [&](int i) { return (double)xs[(size_t)i]; };
    auto at = [](double x) { return x; };
    auto there_and_back = [](double t) { return t < 5.0 ? 10.0 * t : 100.0 - 10.0 * t; };   /* 0 -> 50 -> 0 */
    MovieTimeBarProfile p;
    movie_time_bar_profile(&p, 10.0, 200, there_and_back);
    std::vector<MovieCurvePoint> c;
    movie_elapsed_curve(&c, p, 10.0, xs.data(), (int)xs.size(), of_sample, at);
    EXPECT_NEAR(100.0, c.back().s, 1e-6);
    EXPECT_NEAR(0.0, c.back().v, 1e-6);
    for (size_t i = 1; i < c.size(); ++i) EXPECT_GE(c[i].s, c[i - 1].s);
    double top = 0.0, at_top = 0.0;
    for (const MovieCurvePoint& q : c) if (q.v > top) { top = q.v; at_top = q.s; }
    EXPECT_NEAR(50.0, top, 1e-6);
    EXPECT_NEAR(50.0, at_top, 1.0);
}

UTEST(viamd_movie_overlay, a_hold_adds_nothing_to_the_curve) {
    const std::vector<float> xs = samples_every(1.0f, 101);
    auto of_sample = [&](int i) { return (double)xs[(size_t)i]; };
    auto at = [](double x) { return x; };
    MovieTimeBarProfile p;
    movie_time_bar_profile(&p, 10.0, 100, [](double) { return 40.0; });
    std::vector<MovieCurvePoint> c;
    movie_elapsed_curve(&c, p, 6.0, xs.data(), (int)xs.size(), of_sample, at);
    ASSERT_EQ(1, (int)c.size());
    EXPECT_NEAR(40.0, c[0].v, 1e-9);
}

UTEST(viamd_movie_overlay, the_plot_axis_comes_with_a_default_that_follows_the_movie) {
    MovieOverlay o;
    EXPECT_EQ((int)MoviePlotAxis::Elapsed, (int)o.plot_axis);
}

/* Timelines and distributions are separate overlays that refer to subplots by id */

UTEST(viamd_movie_overlay, the_saved_numbers_of_the_plot_overlays_are_kept) {
    EXPECT_EQ(6, (int)MovieOverlayType::Timeline);
    EXPECT_EQ(7, (int)MovieOverlayType::Distribution);
    EXPECT_EQ(9, (int)MovieOverlayType::Figure);
}

UTEST(viamd_movie_overlay, a_timeline_is_wide_at_the_bottom_and_a_distribution_narrow_at_the_right) {
    MovieOverlay t, d;
    t.type = MovieOverlayType::Timeline;
    d.type = MovieOverlayType::Distribution;
    movie_overlay_plot_defaults(&t);
    movie_overlay_plot_defaults(&d);
    EXPECT_EQ((int)MovieOverlayAnchor::BottomCenter, (int)t.anchor);
    EXPECT_EQ((int)MovieOverlayAnchor::MiddleRight, (int)d.anchor);
    EXPECT_GT(t.width, 2.0f * d.width);          /* elongated */
    EXPECT_GT(d.size, t.size);                   /* the distribution is taller */
}

UTEST(viamd_movie_overlay, other_overlays_keep_their_place_when_the_plot_defaults_are_asked_for) {
    MovieOverlay o;
    o.type = MovieOverlayType::TimeBar;
    o.anchor = MovieOverlayAnchor::TopLeft;
    movie_overlay_plot_defaults(&o);
    EXPECT_EQ((int)MovieOverlayAnchor::TopLeft, (int)o.anchor);
}

UTEST(viamd_movie_overlay, an_old_timeline_overlay_gets_the_ids_of_the_subplots_it_had) {
    std::vector<MovieOverlay> v(1);
    v[0].type = MovieOverlayType::Timeline;
    v[0].legacy_subplot_mask = 0b101;
    const uint32_t t[] = {11, 12, 13};
    const uint32_t d[] = {21, 22};
    movie_overlays_migrate(&v, t, 3, d, 2);
    ASSERT_EQ(1, (int)v.size());
    EXPECT_EQ((int)MovieOverlayType::Timeline, (int)v[0].type);
    ASSERT_EQ(2, (int)v[0].panels.size());
    EXPECT_EQ((int)MoviePlotView::Timeline, (int)v[0].panels[0].view);
    EXPECT_EQ(11u, v[0].panels[0].subplot);
    EXPECT_EQ(13u, v[0].panels[1].subplot);
    EXPECT_EQ(0, v[0].legacy_subplot_mask);
}

UTEST(viamd_movie_overlay, an_old_distribution_overlay_takes_the_ids_of_the_distributions_window) {
    std::vector<MovieOverlay> v(1);
    v[0].type = MovieOverlayType::Distribution;
    v[0].legacy_subplot_mask = 0b10;
    const uint32_t t[] = {11, 12};
    const uint32_t d[] = {21, 22};
    movie_overlays_migrate(&v, t, 2, d, 2);
    ASSERT_EQ(1, (int)v[0].panels.size());
    EXPECT_EQ((int)MoviePlotView::Distribution, (int)v[0].panels[0].view);
    EXPECT_EQ(22u, v[0].panels[0].subplot);
}

UTEST(viamd_movie_overlay, positions_of_hidden_subplots_are_not_taken) {
    std::vector<MovieOverlay> v(1);
    v[0].type = MovieOverlayType::Timeline;
    v[0].legacy_subplot_mask = 0b110;
    const uint32_t t[] = {11, 12, 13};
    const uint32_t d[] = {21};
    movie_overlays_migrate(&v, t, 2, d, 1);   /* only two are shown */
    ASSERT_EQ(1, (int)v[0].panels.size());
    EXPECT_EQ(12u, v[0].panels[0].subplot);
}

UTEST(viamd_movie_overlay, a_figure_is_split_into_a_timeline_and_a_distribution_with_the_place_of_their_kind) {
    std::vector<MovieOverlay> v(1);
    v[0].type = MovieOverlayType::Figure;
    v[0].anchor = MovieOverlayAnchor::BottomRight;
    v[0].begin = 3.0;
    v[0].end = 40.0;
    v[0].font_points = 18.0f;
    v[0].panels = {{MoviePlotView::Timeline, 1}, {MoviePlotView::Distribution, 11}, {MoviePlotView::Timeline, 2}};
    const uint32_t ids[] = {1};
    movie_overlays_migrate(&v, ids, 1, ids, 1);
    ASSERT_EQ(2, (int)v.size());
    EXPECT_EQ((int)MovieOverlayType::Timeline, (int)v[0].type);
    EXPECT_EQ((int)MovieOverlayType::Distribution, (int)v[1].type);
    ASSERT_EQ(2, (int)v[0].panels.size());
    ASSERT_EQ(1, (int)v[1].panels.size());
    EXPECT_EQ(11u, v[1].panels[0].subplot);
    EXPECT_EQ((int)MovieOverlayAnchor::BottomCenter, (int)v[0].anchor);
    EXPECT_EQ((int)MovieOverlayAnchor::MiddleRight, (int)v[1].anchor);
    for (const MovieOverlay& o : v) {
        EXPECT_NEAR(3.0, o.begin, 1e-9);        /* everything else is kept */
        EXPECT_NEAR(40.0, o.end, 1e-9);
        EXPECT_NEAR(18.0f, o.font_points, 1e-6);
    }
}

UTEST(viamd_movie_overlay, a_figure_of_one_kind_becomes_one_overlay_and_an_empty_one_a_timeline) {
    std::vector<MovieOverlay> v(2);
    v[0].type = MovieOverlayType::Figure;
    v[0].panels = {{MoviePlotView::Distribution, 11}};
    v[1].type = MovieOverlayType::Figure;
    const uint32_t ids[] = {1};
    movie_overlays_migrate(&v, ids, 1, ids, 1);
    ASSERT_EQ(2, (int)v.size());
    EXPECT_EQ((int)MovieOverlayType::Distribution, (int)v[0].type);
    EXPECT_EQ((int)MovieOverlayType::Timeline, (int)v[1].type);
}

UTEST(viamd_movie_overlay, other_overlays_are_not_touched_by_the_migration) {
    std::vector<MovieOverlay> v(1);
    v[0].type = MovieOverlayType::TimeBar;
    const uint32_t t[] = {1};
    movie_overlays_migrate(&v, t, 1, t, 1);
    ASSERT_EQ(1, (int)v.size());
    EXPECT_EQ((int)MovieOverlayType::TimeBar, (int)v[0].type);
    EXPECT_TRUE(v[0].panels.empty());
}

UTEST(viamd_movie_overlay, timelines_over_timelines_share_the_axis_and_a_distribution_has_its_own) {
    const MoviePlotView v[] = {MoviePlotView::Timeline, MoviePlotView::Timeline, MoviePlotView::Distribution, MoviePlotView::Timeline};
    EXPECT_FALSE(movie_figure_x_labels(v, 4, 0));   /* the timeline below is a timeline */
    EXPECT_TRUE(movie_figure_x_labels(v, 4, 1));    /* a distribution is below */
    EXPECT_TRUE(movie_figure_x_labels(v, 4, 2));    /* a distribution */
    EXPECT_TRUE(movie_figure_x_labels(v, 4, 3));    /* the last */
    EXPECT_TRUE(movie_figure_x_labels(v, 1, 0));
}

UTEST(viamd_movie_overlay, a_palette_that_does_not_exist_has_the_name_of_the_plots_colours) {
    EXPECT_STREQ("Colours of the plots", movie_plot_palette_name(0));
    EXPECT_STREQ("Colours of the plots", movie_plot_palette_name(99));
    for (int i = 1; i < MOVIE_PLOT_PALETTE_COUNT; ++i) EXPECT_TRUE(movie_plot_palette_name(i)[0] != 0);
}
