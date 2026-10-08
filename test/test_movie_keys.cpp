#include "utest.h"

#include <gfx/camera_utils.h>
#include <movie_keys.h>

#include <float.h>
#include <math.h>

UTEST(viamd_movie_keys, changing_length_scales_all_timing_and_can_be_undone) {
    MovieKeys keys;
    keys.duration = 20.0f;
    keys.traj_begin = 4.0f;
    keys.traj_end = 18.0f;
    keys.start_frame = 0.0;
    keys.end_frame = 600.0;
    CameraKeyframe camera;
    camera.time = 10.0;
    camera.use_frame = true;
    camera.frame = 300.0;
    camera.spin_turns = 2;
    keys.camera.push_back(camera);
    ParamKey param;
    param.time = 12.0;
    param.value[0] = 7.0f;
    keys.params.push_back(param);
    MovieOverlay overlay;
    overlay.begin = 2.0;
    overlay.end = 16.0;
    overlay.fade_in = 1.0f;
    overlay.fade_out = 2.0f;
    keys.overlays.push_back(overlay);
    const MovieKeys original = keys;
    MovieHistory history;
    history.clear(keys);

    movie_keys_scale_time(&keys, 2.0);
    EXPECT_EQ(40.0f, keys.duration);
    EXPECT_EQ(8.0f, keys.traj_begin);
    EXPECT_EQ(36.0f, keys.traj_end);
    EXPECT_EQ(20.0, keys.camera[0].time);
    EXPECT_EQ(300.0, keys.camera[0].frame);
    EXPECT_EQ(2, keys.camera[0].spin_turns);
    EXPECT_EQ(24.0, keys.params[0].time);
    EXPECT_EQ(7.0f, keys.params[0].value[0]);
    EXPECT_EQ(4.0, keys.overlays[0].begin);
    EXPECT_EQ(32.0, keys.overlays[0].end);
    EXPECT_EQ(2.0f, keys.overlays[0].fade_in);
    EXPECT_EQ(4.0f, keys.overlays[0].fade_out);
    EXPECT_EQ(600.0, keys.end_frame);
    const MovieKeys doubled = keys;
    history.update(keys, false);
    ASSERT_TRUE(history.undo(&keys));
    EXPECT_TRUE(movie_keys_equal(original, keys));
    ASSERT_TRUE(history.redo(&keys));
    EXPECT_TRUE(movie_keys_equal(doubled, keys));
    movie_keys_scale_time(&keys, 0.5);
    EXPECT_TRUE(movie_keys_equal(original, keys));
}

/* Curve through keyed values with the easing of the key a segment leads to */

UTEST(viamd_movie_keys, a_keyed_curve_passes_through_its_keys_and_holds_outside) {
    const double t[3] = {0.0, 2.0, 4.0};
    const double v[3] = {0.0, 10.0, 20.0};
    const KeyEase e[3] = {KeyEase::Smooth, KeyEase::Smooth, KeyEase::Smooth};

    for (int i = 0; i < 3; ++i) EXPECT_NEAR(v[i], keyed_curve_evaluate(t, v, e, 3, t[i]), 1.0e-9);
    EXPECT_NEAR(5.0, keyed_curve_evaluate(t, v, e, 3, 1.0), 1.0e-9);   /* even spacing of a line stays a line */
    EXPECT_NEAR(0.0, keyed_curve_evaluate(t, v, e, 3, -3.0), 1.0e-12);
    EXPECT_NEAR(20.0, keyed_curve_evaluate(t, v, e, 3, 9.0), 1.0e-12);
    EXPECT_NEAR(0.0, keyed_curve_evaluate(t, v, e, 1, 3.0), 1.0e-12);   /* a single key is its value, whenever */
    EXPECT_NEAR(0.0, keyed_curve_evaluate(t, v, e, 0, 3.0), 1.0e-12);
}

UTEST(viamd_movie_keys, the_easing_of_a_key_shapes_the_stretch_leading_to_it) {
    const double t[3] = {0.0, 2.0, 4.0};
    const double v[3] = {0.0, 10.0, 20.0};

    KeyEase e[3] = {KeyEase::Smooth, KeyEase::Hold, KeyEase::Smooth};
    EXPECT_NEAR(0.0, keyed_curve_evaluate(t, v, e, 3, 1.99), 1.0e-12);    /* stays until the key */
    EXPECT_NEAR(10.0, keyed_curve_evaluate(t, v, e, 3, 2.0), 1.0e-9);

    e[1] = KeyEase::EaseInOut;
    const double u = 0.25;
    EXPECT_NEAR(10.0 * (u * u * (3.0 - 2.0 * u)), keyed_curve_evaluate(t, v, e, 3, 0.5), 1.0e-9);

    e[1] = KeyEase::Smooth;
    e[2] = KeyEase::Linear;
    EXPECT_NEAR(15.0, keyed_curve_evaluate(t, v, e, 3, 3.0), 1.0e-9);
}

UTEST(viamd_movie_keys, a_smooth_curve_does_not_overshoot_a_key) {
    const double t[4] = {0.0, 1.0, 2.0, 3.0};
    const double v[4] = {0.0, 10.0, 0.0, 5.0};   /* the second is a peak */
    const KeyEase e[4] = {KeyEase::Smooth, KeyEase::Smooth, KeyEase::Smooth, KeyEase::Smooth};
    for (double x = 0.0; x <= 3.0; x += 0.01) {
        const double y = keyed_curve_evaluate(t, v, e, 4, x);
        EXPECT_LE(y, 10.0 + 1.0e-9);
        EXPECT_GE(y, -1.0e-9);
    }
}

/* Look parameters */

static ParamKey pk(int param, double time, float a, float b = 0.0f, float c = 0.0f, KeyEase ease = KeyEase::Smooth) {
    ParamKey k;
    k.param = param;
    k.time = time;
    k.value[0] = a; k.value[1] = b; k.value[2] = c;
    k.ease = ease;
    return k;
}

UTEST(viamd_movie_keys, a_parameter_without_keys_is_left_alone) {
    ParamKey keys[1] = { pk(3, 1.0, 5.0f) };
    float v[3] = {-1, -1, -1};
    EXPECT_FALSE(param_keys_evaluate(v, 1, keys, 1, 2, 1.0));
    EXPECT_FALSE(param_keys_evaluate(v, 1, keys, 0, 3, 1.0));
    EXPECT_EQ(-1.0f, v[0]);
}

UTEST(viamd_movie_keys, parameters_are_keyed_independently_of_each_other_and_of_order) {
    /* Out of order, and with keys for another parameter in between */
    ParamKey keys[5] = {
        pk(1, 4.0, 40.0f),
        pk(2, 1.0, 999.0f),
        pk(1, 0.0, 0.0f),
        pk(2, 3.0, -999.0f),
        pk(1, 2.0, 20.0f),
    };
    float v[3] = {};
    ASSERT_TRUE(param_keys_evaluate(v, 1, keys, 5, 1, 1.0));
    EXPECT_NEAR(10.0f, v[0], 1.0e-4f);
    ASSERT_TRUE(param_keys_evaluate(v, 1, keys, 5, 1, 3.0));
    EXPECT_NEAR(30.0f, v[0], 1.0e-4f);
    ASSERT_TRUE(param_keys_evaluate(v, 1, keys, 5, 2, 2.0));
    EXPECT_NEAR(0.0f, v[0], 1.0f);                    /* between its own two keys, the other's do not matter */
    ASSERT_TRUE(param_keys_evaluate(v, 1, keys, 5, 2, 10.0));
    EXPECT_NEAR(-999.0f, v[0], 1.0e-3f);
}

UTEST(viamd_movie_keys, a_colour_is_keyed_in_all_three_channels) {
    ParamKey keys[2] = { pk(0, 0.0, 0.0f, 1.0f, 0.5f, KeyEase::Smooth), pk(0, 2.0, 1.0f, 0.0f, 0.5f, KeyEase::Linear) };
    float v[3] = {};
    ASSERT_TRUE(param_keys_evaluate(v, 3, keys, 2, 0, 1.0));
    EXPECT_NEAR(0.5f, v[0], 1.0e-5f);
    EXPECT_NEAR(0.5f, v[1], 1.0e-5f);
    EXPECT_NEAR(0.5f, v[2], 1.0e-5f);
}

/* Undo */

static MovieKeys keys_with(int n, bool loop = false) {
    MovieKeys k;
    for (int i = 0; i < n; ++i) {
        CameraKeyframe c;
        c.time = (double)i;
        k.camera.push_back(c);
    }
    k.loop = loop;
    return k;
}

UTEST(viamd_movie_keys, equality_looks_at_everything_that_is_edited) {
    MovieKeys a = keys_with(2), b = keys_with(2);
    EXPECT_TRUE(movie_keys_equal(a, b));
    b.camera[1].spin_turns = 1;
    EXPECT_FALSE(movie_keys_equal(a, b));
    b = a;
    b.camera[0].ease = KeyEase::Hold;
    EXPECT_FALSE(movie_keys_equal(a, b));
    b = a;
    b.loop = true;
    EXPECT_FALSE(movie_keys_equal(a, b));
    b = a;
    b.params.push_back(pk(0, 0.0, 1.0f));
    EXPECT_FALSE(movie_keys_equal(a, b));
}

UTEST(viamd_movie_keys, an_edit_is_one_undo_step_and_redo_brings_it_back) {
    MovieHistory h;
    MovieKeys cur = keys_with(1);
    h.clear(cur);
    EXPECT_FALSE(h.can_undo());

    cur = keys_with(2);
    h.update(cur, false);
    EXPECT_TRUE(h.can_undo());
    EXPECT_FALSE(h.can_redo());

    ASSERT_TRUE(h.undo(&cur));
    EXPECT_EQ(1u, (uint32_t)cur.camera.size());
    EXPECT_TRUE(h.can_redo());
    EXPECT_FALSE(h.can_undo());

    h.update(cur, false);   /* being shown the restored keys is not an edit */
    EXPECT_TRUE(h.can_redo());

    ASSERT_TRUE(h.redo(&cur));
    EXPECT_EQ(2u, (uint32_t)cur.camera.size());
    EXPECT_FALSE(h.redo(&cur));
}

UTEST(viamd_movie_keys, a_drag_is_one_step_not_many) {
    MovieHistory h;
    MovieKeys cur = keys_with(2);
    h.clear(cur);

    for (int i = 1; i <= 50; ++i) {
        cur.camera[1].time = 1.0 + 0.01 * i;
        h.update(cur, true);          /* the mouse is down */
    }
    EXPECT_FALSE(h.can_undo());
    h.update(cur, false);             /* released */
    ASSERT_TRUE(h.can_undo());

    ASSERT_TRUE(h.undo(&cur));
    EXPECT_NEAR(1.0, cur.camera[1].time, 1.0e-12);
    EXPECT_FALSE(h.can_undo());       /* it was the only step */
}

UTEST(viamd_movie_keys, undoing_in_the_middle_of_an_edit_counts_the_edit_first) {
    MovieHistory h;
    MovieKeys cur = keys_with(2);
    h.clear(cur);

    cur.camera[0].time = 0.5;
    h.update(cur, true);              /* not committed yet */
    ASSERT_TRUE(h.undo(&cur));
    EXPECT_NEAR(0.0, cur.camera[0].time, 1.0e-12);
}

UTEST(viamd_movie_keys, a_new_edit_after_an_undo_drops_what_could_be_redone) {
    MovieHistory h;
    MovieKeys cur = keys_with(1);
    h.clear(cur);
    cur = keys_with(2); h.update(cur, false);
    cur = keys_with(3); h.update(cur, false);

    ASSERT_TRUE(h.undo(&cur));
    EXPECT_TRUE(h.can_redo());
    cur = keys_with(5, true);
    h.update(cur, false);
    EXPECT_FALSE(h.can_redo());
    ASSERT_TRUE(h.undo(&cur));
    EXPECT_EQ(2u, (uint32_t)cur.camera.size());
}

UTEST(viamd_movie_keys, clearing_forgets_the_past) {
    MovieHistory h;
    MovieKeys cur = keys_with(1);
    h.clear(cur);
    cur = keys_with(2); h.update(cur, false);
    EXPECT_TRUE(h.can_undo());

    h.clear(cur);
    EXPECT_FALSE(h.can_undo());
    EXPECT_FALSE(h.can_redo());
    h.update(cur, false);
    EXPECT_FALSE(h.can_undo());       /* what was cleared to is the present */
}

/* Which frames a recording makes, how big they are, how long it has left */

UTEST(viamd_movie_keys, without_a_range_every_frame_is_rendered) {
    int a = -1, b = -1;
    movie_frame_range(121, 24.0, false, 2.0, 3.0, &a, &b);
    EXPECT_EQ(0, a);
    EXPECT_EQ(120, b);
}

UTEST(viamd_movie_keys, a_range_picks_the_frames_inside_it) {
    int a = -1, b = -1;
    movie_frame_range(121, 24.0, true, 1.0, 2.0, &a, &b);   /* 24 .. 48 */
    EXPECT_EQ(24, a);
    EXPECT_EQ(48, b);

    movie_frame_range(121, 24.0, true, 1.01, 1.99, &a, &b); /* between frames: the ones inside */
    EXPECT_EQ(25, a);
    EXPECT_EQ(47, b);
}

UTEST(viamd_movie_keys, a_range_always_holds_a_frame_and_stays_inside_the_movie) {
    int a = -1, b = -1;
    movie_frame_range(121, 24.0, true, 3.0, 3.0, &a, &b);
    EXPECT_EQ(72, a);
    EXPECT_EQ(72, b);

    movie_frame_range(121, 24.0, true, 1.01, 1.02, &a, &b); /* nothing exactly inside: the first after begin */
    EXPECT_EQ(25, a);
    EXPECT_GE(b, a);

    movie_frame_range(121, 24.0, true, 4.0, 90.0, &a, &b);
    EXPECT_EQ(96, a);
    EXPECT_EQ(120, b);

    movie_frame_range(121, 24.0, true, 90.0, 95.0, &a, &b);
    EXPECT_EQ(120, a);
    EXPECT_EQ(120, b);
}

UTEST(viamd_movie_keys, a_scaled_size_is_even_and_never_larger) {
    int w = 1920, h = 1080;
    movie_scaled_size(&w, &h, 100);
    EXPECT_EQ(1920, w);
    EXPECT_EQ(1080, h);

    movie_scaled_size(&w, &h, 50);
    EXPECT_EQ(960, w);
    EXPECT_EQ(540, h);

    w = 1001; h = 667;
    movie_scaled_size(&w, &h, 75);
    EXPECT_EQ(0, w % 2);
    EXPECT_EQ(0, h % 2);
    EXPECT_LE(w, 751);

    w = 20; h = 10;
    movie_scaled_size(&w, &h, 10);
    EXPECT_GE(w, 2);
    EXPECT_GE(h, 2);
}

UTEST(viamd_movie_keys, time_left_follows_the_pace_so_far) {
    double s = 0.0;
    EXPECT_FALSE(movie_time_left(1, 100, 5.0, &s));    /* too early to tell */
    EXPECT_FALSE(movie_time_left(50, 100, 0.2, &s));
    ASSERT_TRUE(movie_time_left(25, 100, 50.0, &s));   /* 2 s a frame, 75 to go */
    EXPECT_NEAR(150.0, s, 1.0e-9);
    ASSERT_TRUE(movie_time_left(100, 100, 50.0, &s));
    EXPECT_NEAR(0.0, s, 1.0e-12);
}

UTEST(viamd_movie_keys, a_time_snaps_to_the_nearest_frame_inside_the_movie) {
    EXPECT_NEAR(24.0 / 24.0, movie_snap_to_frame(1.0 + 0.4 / 24.0, 24.0, 5.0), 1.0e-9);
    EXPECT_NEAR(25.0 / 24.0, movie_snap_to_frame(1.0 + 0.6 / 24.0, 24.0, 5.0), 1.0e-9);
    EXPECT_NEAR(0.0, movie_snap_to_frame(-3.0, 24.0, 5.0), 1.0e-12);
    EXPECT_NEAR(5.0, movie_snap_to_frame(9.0, 24.0, 5.0), 1.0e-12);
    /* A length that is not a whole number of frames: the end is the length, not a frame past it */
    EXPECT_NEAR(5.01, movie_snap_to_frame(5.2, 10.0, 5.01), 1.0e-12);
}

/* Keys of properties of representations */

static RepKey rk(uint32_t rep, RepProp prop, double time, float value, KeyEase ease = KeyEase::Smooth) {
    RepKey k;
    k.rep = rep;
    k.prop = (int)prop;
    k.time = time;
    k.value[0] = value;
    k.ease = ease;
    return k;
}

UTEST(viamd_movie_keys, rep_keys_follow_their_own_representation_and_property) {
    const RepKey keys[] = {
        rk(1, RepProp::Scale0, 0.0, 1.0f, KeyEase::Linear), rk(1, RepProp::Scale0, 4.0, 3.0f, KeyEase::Linear),
        rk(2, RepProp::Scale0, 0.0, 10.0f), rk(1, RepProp::Saturation, 0.0, 0.5f),
    };
    float v = 0.0f;
    ASSERT_TRUE(rep_keys_evaluate(&v, keys, 4, 1, (int)RepProp::Scale0, 2.0));
    EXPECT_NEAR(2.0f, v, 1.0e-6f);
    ASSERT_TRUE(rep_keys_evaluate(&v, keys, 4, 2, (int)RepProp::Scale0, 2.0));
    EXPECT_NEAR(10.0f, v, 1.0e-6f);
    ASSERT_TRUE(rep_keys_evaluate(&v, keys, 4, 1, (int)RepProp::Saturation, 9.0));
    EXPECT_NEAR(0.5f, v, 1.0e-6f);
    EXPECT_FALSE(rep_keys_evaluate(&v, keys, 4, 3, (int)RepProp::Scale0, 2.0));
    EXPECT_FALSE(rep_keys_evaluate(&v, keys, 4, 1, (int)RepProp::TintScale, 2.0));
}

UTEST(viamd_movie_keys, a_visible_key_changes_at_its_key_and_is_not_blended) {
    /* Even with a smooth ease on the keys: shown, hidden at 2 s, shown again at 4 s */
    const RepKey keys[] = {
        rk(1, RepProp::Visible, 4.0, 1.0f), rk(1, RepProp::Visible, 0.0, 1.0f), rk(1, RepProp::Visible, 2.0, 0.0f),
    };
    float v = -1.0f;
    ASSERT_TRUE(rep_keys_evaluate(&v, keys, 3, 1, (int)RepProp::Visible, 1.99));
    EXPECT_NEAR(1.0f, v, 1.0e-6f);
    ASSERT_TRUE(rep_keys_evaluate(&v, keys, 3, 1, (int)RepProp::Visible, 2.0));
    EXPECT_NEAR(0.0f, v, 1.0e-6f);
    ASSERT_TRUE(rep_keys_evaluate(&v, keys, 3, 1, (int)RepProp::Visible, 3.99));
    EXPECT_NEAR(0.0f, v, 1.0e-6f);
    ASSERT_TRUE(rep_keys_evaluate(&v, keys, 3, 1, (int)RepProp::Visible, 4.0));
    EXPECT_NEAR(1.0f, v, 1.0e-6f);
}

UTEST(viamd_movie_keys, rep_keys_are_part_of_the_undo_state_and_scale_with_time) {
    MovieKeys a, b;
    a.reps.push_back(rk(1, RepProp::Scale0, 2.0, 1.0f));
    EXPECT_FALSE(movie_keys_equal(a, b));
    b.reps.push_back(rk(1, RepProp::Scale0, 2.0, 1.0f));
    EXPECT_TRUE(movie_keys_equal(a, b));
    b.reps[0].rep = 2;
    EXPECT_FALSE(movie_keys_equal(a, b));

    movie_keys_scale_time(&a, 2.0);
    EXPECT_NEAR(4.0, a.reps[0].time, 1.0e-12);
}

UTEST(viamd_movie_keys, a_color_key_blends_all_three_components) {
    RepKey a = rk(1, RepProp::BaseColor, 0.0, 0.0f, KeyEase::Linear);
    RepKey b = rk(1, RepProp::BaseColor, 2.0, 1.0f, KeyEase::Linear);
    a.value[1] = 1.0f; a.value[2] = 0.5f;
    b.value[1] = 0.0f; b.value[2] = 0.5f;
    const RepKey keys[] = {a, b};
    EXPECT_EQ(3, rep_prop_comps((int)RepProp::BaseColor));
    EXPECT_EQ(3, rep_prop_comps((int)RepProp::TintColor));
    EXPECT_EQ(1, rep_prop_comps((int)RepProp::Saturation));

    float v[3] = {};
    ASSERT_TRUE(rep_keys_evaluate(v, keys, 2, 1, (int)RepProp::BaseColor, 1.0));
    EXPECT_NEAR(0.5f, v[0], 1.0e-6f);
    EXPECT_NEAR(0.5f, v[1], 1.0e-6f);
    EXPECT_NEAR(0.5f, v[2], 1.0e-6f);
    ASSERT_TRUE(rep_keys_evaluate(v, keys, 2, 1, (int)RepProp::BaseColor, 9.0));
    EXPECT_NEAR(1.0f, v[0], 1.0e-6f);
    EXPECT_NEAR(0.0f, v[1], 1.0e-6f);
}

/* A representation that grows in and shrinks away at its Visible keys */

static RepKey vk(double time, bool shown) {
    return rk(1, RepProp::Visible, time, shown ? 1.0f : 0.0f);
}

UTEST(viamd_movie_keys, a_visible_key_starts_a_smooth_transition) {
    const RepKey keys[] = { vk(0.0, false), vk(10.0, true), vk(30.0, false) };
    float f = -1.0f;
    ASSERT_TRUE(rep_visible_factor(&f, keys, 3, 1, 5.0, 2.0));   /* before the first change */
    EXPECT_NEAR(0.0f, f, 1.0e-6f);
    ASSERT_TRUE(rep_visible_factor(&f, keys, 3, 1, 10.0, 2.0));  /* it starts at the key */
    EXPECT_NEAR(0.0f, f, 1.0e-6f);
    ASSERT_TRUE(rep_visible_factor(&f, keys, 3, 1, 11.0, 2.0));  /* halfway */
    EXPECT_NEAR(0.5f, f, 1.0e-6f);
    ASSERT_TRUE(rep_visible_factor(&f, keys, 3, 1, 12.0, 2.0));  /* done */
    EXPECT_NEAR(1.0f, f, 1.0e-6f);
    ASSERT_TRUE(rep_visible_factor(&f, keys, 3, 1, 20.0, 2.0));
    EXPECT_NEAR(1.0f, f, 1.0e-6f);
    ASSERT_TRUE(rep_visible_factor(&f, keys, 3, 1, 31.0, 2.0));  /* shrinking away */
    EXPECT_NEAR(0.5f, f, 1.0e-6f);
    ASSERT_TRUE(rep_visible_factor(&f, keys, 3, 1, 99.0, 2.0));
    EXPECT_NEAR(0.0f, f, 1.0e-6f);
}

UTEST(viamd_movie_keys, the_transition_never_overshoots_and_only_goes_one_way) {
    const RepKey keys[] = { vk(0.0, false), vk(10.0, true) };
    float prev = 0.0f, f = 0.0f;
    for (double t = 10.0; t <= 13.0; t += 0.01) {
        ASSERT_TRUE(rep_visible_factor(&f, keys, 2, 1, t, 2.0));
        EXPECT_GE(f, prev - 1.0e-6f);
        EXPECT_LE(f, 1.0f);
        prev = f;
    }
}

UTEST(viamd_movie_keys, a_transition_of_zero_changes_at_the_key) {
    const RepKey keys[] = { vk(0.0, true), vk(4.0, false) };
    float f = -1.0f;
    ASSERT_TRUE(rep_visible_factor(&f, keys, 2, 1, 3.99, 0.0));
    EXPECT_NEAR(1.0f, f, 1.0e-6f);
    ASSERT_TRUE(rep_visible_factor(&f, keys, 2, 1, 4.0, 0.0));
    EXPECT_NEAR(0.0f, f, 1.0e-6f);
}

UTEST(viamd_movie_keys, a_change_that_comes_before_the_last_is_done_turns_around_from_where_it_was) {
    /* Shown at 10 (takes 4 s), hidden again at 12: at 12 it was halfway up, and goes down from there */
    const RepKey keys[] = { vk(0.0, false), vk(10.0, true), vk(12.0, false) };
    float f = -1.0f;
    ASSERT_TRUE(rep_visible_factor(&f, keys, 3, 1, 12.0, 4.0));
    EXPECT_NEAR(0.5f, f, 1.0e-6f);
    ASSERT_TRUE(rep_visible_factor(&f, keys, 3, 1, 14.0, 4.0));
    EXPECT_NEAR(0.25f, f, 1.0e-6f);
    ASSERT_TRUE(rep_visible_factor(&f, keys, 3, 1, 16.0, 4.0));
    EXPECT_NEAR(0.0f, f, 1.0e-6f);
}

UTEST(viamd_movie_keys, no_visible_keys_means_no_factor) {
    const RepKey keys[] = { rk(1, RepProp::Scale0, 0.0, 2.0f), vk(0.0, true) };
    float f = 0.0f;
    EXPECT_FALSE(rep_visible_factor(&f, keys, 2, 2, 1.0, 2.0));
    EXPECT_FALSE(rep_visible_factor(&f, keys, 1, 1, 1.0, 2.0));
}

UTEST(viamd_movie_keys, an_overlay_background_is_part_of_the_undo_state) {
    MovieKeys a, b;
    a.overlays.push_back(MovieOverlay{});
    b.overlays.push_back(MovieOverlay{});
    EXPECT_TRUE(movie_keys_equal(a, b));
    b.overlays[0].background[3] = 0.5f;
    EXPECT_FALSE(movie_keys_equal(a, b));
}

UTEST(viamd_movie_keys, an_overlay_image_file_is_part_of_the_undo_state) {
    MovieKeys a, b;
    a.overlays.push_back(MovieOverlay{});
    b.overlays.push_back(MovieOverlay{});
    snprintf(b.overlays[0].path, sizeof(b.overlays[0].path), "/pics/group.png");
    EXPECT_FALSE(movie_keys_equal(a, b));
    snprintf(a.overlays[0].path, sizeof(a.overlays[0].path), "/pics/group.png");
    EXPECT_TRUE(movie_keys_equal(a, b));
}

UTEST(viamd_movie_keys, the_panels_and_the_look_of_a_figure_are_part_of_the_undo_state) {
    MovieKeys a, b;
    a.overlays.push_back(MovieOverlay{});
    b.overlays.push_back(MovieOverlay{});
    b.overlays[0].panels.push_back({MoviePlotView::Timeline, 3});
    EXPECT_FALSE(movie_keys_equal(a, b));
    a.overlays[0].panels.push_back({MoviePlotView::Timeline, 4});
    EXPECT_FALSE(movie_keys_equal(a, b));   /* another subplot */
    a.overlays[0].panels[0].subplot = 3;
    EXPECT_TRUE(movie_keys_equal(a, b));
    a.overlays[0].panels[0].view = MoviePlotView::Distribution;
    EXPECT_FALSE(movie_keys_equal(a, b));
    a.overlays[0].panels[0].view = MoviePlotView::Timeline;
    b.overlays[0].font_points = 24.0f;
    EXPECT_FALSE(movie_keys_equal(a, b));
    b.overlays[0].font_points = 0.0f;
    b.overlays[0].palette = 2;
    EXPECT_FALSE(movie_keys_equal(a, b));
}

UTEST(viamd_movie_keys, markers_are_part_of_the_undo_state_and_scale_with_the_length) {
    MovieKeys a, b;
    a.markers.push_back({10.0, "water"});
    EXPECT_FALSE(movie_keys_equal(a, b));
    b.markers.push_back({10.0, "water"});
    EXPECT_TRUE(movie_keys_equal(a, b));
    b.markers[0].label[0] = 'W';
    EXPECT_FALSE(movie_keys_equal(a, b));
    movie_keys_scale_time(&a, 2.0);
    EXPECT_NEAR(20.0, a.markers[0].time, 1e-9);
}

UTEST(viamd_movie_keys, bin_counts_and_marker_targets_can_be_undone_and_redone) {
    MovieKeys cur;
    cur.overlays.push_back(MovieOverlay{});
    cur.overlays[0].type = MovieOverlayType::Distribution;
    cur.markers.push_back({10.0, "water"});
    MovieHistory history;
    history.clear(cur);

    cur.overlays[0].num_bins = 37;
    history.update(cur, false);
    cur.markers[0].subplot = 4;
    history.update(cur, false);

    ASSERT_TRUE(history.undo(&cur));
    EXPECT_EQ(0u, cur.markers[0].subplot);
    EXPECT_EQ(37, cur.overlays[0].num_bins);
    ASSERT_TRUE(history.undo(&cur));
    EXPECT_EQ(0, cur.overlays[0].num_bins);
    ASSERT_TRUE(history.redo(&cur));
    ASSERT_TRUE(history.redo(&cur));
    EXPECT_EQ(37, cur.overlays[0].num_bins);
    EXPECT_EQ(4u, cur.markers[0].subplot);

    movie_keys_scale_time(&cur, 2.0);
    EXPECT_NEAR(20.0, cur.markers[0].time, 1e-9);
    EXPECT_EQ(4u, cur.markers[0].subplot);
    EXPECT_EQ(37, cur.overlays[0].num_bins);
}

UTEST(viamd_movie_keys, the_time_of_a_panel_is_part_of_the_undo_state_and_scales_with_the_length) {
    MovieKeys a, b;
    a.overlays.push_back(MovieOverlay{});
    b.overlays.push_back(MovieOverlay{});
    a.overlays[0].panels.push_back({MoviePlotView::Timeline, 3, 0.0, 0.0});
    b.overlays[0].panels.push_back({MoviePlotView::Timeline, 3, 0.0, 0.0});
    EXPECT_TRUE(movie_keys_equal(a, b));
    b.overlays[0].panels[0].begin = 12.0;
    EXPECT_FALSE(movie_keys_equal(a, b));
    a.overlays[0].panels[0].begin = 12.0;
    a.overlays[0].panels[0].end = 20.0;
    EXPECT_FALSE(movie_keys_equal(a, b));
    movie_keys_scale_time(&a, 2.0);
    EXPECT_NEAR(24.0, a.overlays[0].panels[0].begin, 1e-9);
    EXPECT_NEAR(40.0, a.overlays[0].panels[0].end, 1e-9);
}

UTEST(viamd_movie_keys, the_title_of_a_panel_is_part_of_the_undo_state) {
    MovieKeys a, b;
    a.overlays.push_back(MovieOverlay{});
    b.overlays.push_back(MovieOverlay{});
    a.overlays[0].panels.push_back({MoviePlotView::Timeline, 3, 0.0, 0.0});
    b.overlays[0].panels.push_back({MoviePlotView::Timeline, 3, 0.0, 0.0});
    EXPECT_TRUE(movie_keys_equal(a, b));
    snprintf(b.overlays[0].panels[0].title, sizeof(b.overlays[0].panels[0].title), "Distance to the ligand");
    EXPECT_FALSE(movie_keys_equal(a, b));
    snprintf(a.overlays[0].panels[0].title, sizeof(a.overlays[0].panels[0].title), "Distance to the ligand");
    EXPECT_TRUE(movie_keys_equal(a, b));
}

/* Representations over time as stretches */

static RepKey vis_key(uint32_t rep, double time, bool shown) {
    RepKey k;
    k.rep = rep;
    k.prop = (int)RepProp::Visible;
    k.time = time;
    k.value[0] = shown ? 1.0f : 0.0f;
    k.ease = KeyEase::Hold;
    return k;
}

UTEST(viamd_movie_keys, the_stretches_a_representation_is_shown_are_read_from_its_keys) {
    const std::vector<RepKey> keys = {vis_key(8, 0, false), vis_key(8, 2.4, true), vis_key(8, 9.6, false), vis_key(8, 60, true), vis_key(3, 0, true)};
    const std::vector<RepInterval> iv = rep_shown_intervals(keys, 8, 80.0);
    ASSERT_EQ(2, (int)iv.size());
    EXPECT_NEAR(2.4, iv[0].begin, 1e-9);
    EXPECT_NEAR(9.6, iv[0].end, 1e-9);
    EXPECT_EQ(1, iv[0].begin_key);
    EXPECT_EQ(2, iv[0].end_key);
    EXPECT_NEAR(60.0, iv[1].begin, 1e-9);
    EXPECT_NEAR(80.0, iv[1].end, 1e-9);
    EXPECT_EQ(-1, iv[1].end_key);
}

UTEST(viamd_movie_keys, a_first_key_that_shows_it_holds_from_the_start_of_the_movie) {
    const std::vector<RepKey> keys = {vis_key(1, 3, true), vis_key(1, 20, false)};
    const std::vector<RepInterval> iv = rep_shown_intervals(keys, 1, 50.0);
    ASSERT_EQ(1, (int)iv.size());
    EXPECT_NEAR(0.0, iv[0].begin, 1e-9);
    EXPECT_TRUE(iv[0].begin_is_start);
    EXPECT_NEAR(20.0, iv[0].end, 1e-9);
}

UTEST(viamd_movie_keys, a_representation_without_keys_has_no_stretches) {
    const std::vector<RepKey> keys = {vis_key(2, 5, true)};
    EXPECT_TRUE(rep_shown_intervals(keys, 1, 50.0).empty());
}

UTEST(viamd_movie_keys, the_end_of_a_stretch_is_moved_with_its_key) {
    std::vector<RepKey> keys = {vis_key(8, 0, false), vis_key(8, 2.4, true), vis_key(8, 9.6, false), vis_key(8, 60, true)};
    const RepInterval first = rep_shown_intervals(keys, 8, 80.0)[0];
    rep_move_interval(&keys, 8, first, 2.4, 12.0, 80.0);
    const std::vector<RepInterval> iv = rep_shown_intervals(keys, 8, 80.0);
    EXPECT_NEAR(2.4, iv[0].begin, 1e-9);
    EXPECT_NEAR(12.0, iv[0].end, 1e-9);
    EXPECT_EQ(4, (int)keys.size());
}

UTEST(viamd_movie_keys, starting_a_stretch_that_began_at_the_start_later_adds_a_hidden_key_before_it) {
    std::vector<RepKey> keys = {vis_key(1, 3, true), vis_key(1, 20, false)};
    const RepInterval iv = rep_shown_intervals(keys, 1, 50.0)[0];
    rep_move_interval(&keys, 1, iv, 5.0, 20.0, 50.0);
    const std::vector<RepInterval> after = rep_shown_intervals(keys, 1, 50.0);
    ASSERT_EQ(1, (int)after.size());
    EXPECT_NEAR(5.0, after[0].begin, 1e-9);
    EXPECT_NEAR(20.0, after[0].end, 1e-9);
    EXPECT_FALSE(after[0].begin_is_start);
    EXPECT_EQ(3, (int)keys.size());
}

UTEST(viamd_movie_keys, ending_a_stretch_that_lasted_to_the_end_earlier_adds_a_hidden_key) {
    std::vector<RepKey> keys = {vis_key(8, 0, false), vis_key(8, 60, true)};
    const RepInterval iv = rep_shown_intervals(keys, 8, 80.0)[0];
    rep_move_interval(&keys, 8, iv, 60.0, 70.0, 80.0);
    const std::vector<RepInterval> after = rep_shown_intervals(keys, 8, 80.0);
    ASSERT_EQ(1, (int)after.size());
    EXPECT_NEAR(70.0, after[0].end, 1e-9);
}

UTEST(viamd_movie_keys, a_stretch_stays_between_its_neighbours) {
    std::vector<RepKey> keys = {vis_key(8, 0, false), vis_key(8, 2.4, true), vis_key(8, 9.6, false), vis_key(8, 60, true), vis_key(8, 70, false)};
    const RepInterval second = rep_shown_intervals(keys, 8, 80.0)[1];
    rep_move_interval(&keys, 8, second, 5.0, 15.0, 80.0);    /* moved only, into the first one */
    const std::vector<RepInterval> iv = rep_shown_intervals(keys, 8, 80.0);
    ASSERT_EQ(2, (int)iv.size());
    EXPECT_NEAR(9.65, iv[1].begin, 1e-9);   /* a little gap is left */
    EXPECT_NEAR(19.65, iv[1].end, 1e-9);    /* its length of 10 s is kept */
}

UTEST(viamd_movie_keys, a_stretch_keeps_a_least_length) {
    std::vector<RepKey> keys = {vis_key(1, 0, false), vis_key(1, 10, true), vis_key(1, 20, false)};
    const RepInterval iv = rep_shown_intervals(keys, 1, 50.0)[0];
    rep_move_interval(&keys, 1, iv, 10.0, 5.0, 50.0);
    const std::vector<RepInterval> after = rep_shown_intervals(keys, 1, 50.0);
    EXPECT_GT(after[0].end - after[0].begin, 0.0);
}

UTEST(viamd_movie_keys, a_stretch_is_added_where_it_is_hidden_and_ends_before_the_next) {
    std::vector<RepKey> keys = {vis_key(8, 0, false), vis_key(8, 2.4, true), vis_key(8, 9.6, false), vis_key(8, 60, true)};
    EXPECT_TRUE(rep_add_interval(&keys, 8, 20.0, 80.0, 100.0));
    std::vector<RepInterval> iv = rep_shown_intervals(keys, 8, 100.0);
    ASSERT_EQ(3, (int)iv.size());
    EXPECT_NEAR(20.0, iv[1].begin, 1e-9);
    EXPECT_NEAR(59.95, iv[1].end, 1e-9);    /* it stops a little before the next one */
    EXPECT_FALSE(rep_add_interval(&keys, 8, 3.0, 5.0, 100.0));   /* shown there already */
}

UTEST(viamd_movie_keys, a_stretch_added_to_a_representation_without_keys_is_hidden_before_it) {
    std::vector<RepKey> keys;
    EXPECT_TRUE(rep_add_interval(&keys, 4, 10.0, 20.0, 50.0));
    const std::vector<RepInterval> iv = rep_shown_intervals(keys, 4, 50.0);
    ASSERT_EQ(1, (int)iv.size());
    EXPECT_NEAR(10.0, iv[0].begin, 1e-9);
    EXPECT_NEAR(20.0, iv[0].end, 1e-9);
    float f = 1.0f;
    ASSERT_TRUE(rep_visible_factor(&f, keys.data(), keys.size(), 4, 5.0, 0.0));
    EXPECT_NEAR(0.0f, f, 1e-6);
}

UTEST(viamd_movie_keys, removing_a_stretch_leaves_the_others) {
    std::vector<RepKey> keys = {vis_key(8, 0, false), vis_key(8, 2.4, true), vis_key(8, 9.6, false), vis_key(8, 60, true)};
    rep_remove_interval(&keys, rep_shown_intervals(keys, 8, 80.0)[0]);
    const std::vector<RepInterval> iv = rep_shown_intervals(keys, 8, 80.0);
    ASSERT_EQ(1, (int)iv.size());
    EXPECT_NEAR(60.0, iv[0].begin, 1e-9);
}

UTEST(viamd_movie_keys, a_swap_hides_one_and_shows_the_other_at_the_same_time) {
    std::vector<RepKey> keys;    /* neither has keys: 'from' was shown, 'to' hidden */
    rep_swap_at(&keys, 1, 2, 10.0);
    const std::vector<RepInterval> a = rep_shown_intervals(keys, 1, 50.0);
    const std::vector<RepInterval> b = rep_shown_intervals(keys, 2, 50.0);
    ASSERT_EQ(1, (int)a.size());
    ASSERT_EQ(1, (int)b.size());
    EXPECT_NEAR(0.0, a[0].begin, 1e-9);
    EXPECT_NEAR(10.0, a[0].end, 1e-9);
    EXPECT_NEAR(10.0, b[0].begin, 1e-9);
    EXPECT_NEAR(50.0, b[0].end, 1e-9);
}

UTEST(viamd_movie_keys, a_swap_on_a_key_that_is_there_changes_it_instead_of_adding_one) {
    std::vector<RepKey> keys = {vis_key(1, 0, true), vis_key(1, 10, true)};
    rep_swap_at(&keys, 1, 2, 10.0);
    int at_ten = 0;
    for (const RepKey& k : keys) if (k.rep == 1 && fabs(k.time - 10.0) < 1e-9) { at_ten += 1; EXPECT_NEAR(0.0f, k.value[0], 1e-9); }
    EXPECT_EQ(1, at_ten);
}

UTEST(viamd_movie_keys, dragging_the_left_end_of_a_group_moves_the_members_that_start_there) {
    std::vector<RepKey> keys = {vis_key(1, 0, false), vis_key(1, 10, true), vis_key(1, 20, false), vis_key(2, 0, false), vis_key(2, 10, true), vis_key(2, 30, false),
        vis_key(3, 0, false), vis_key(3, 12, true), vis_key(3, 18, false)};
    rep_move_group(&keys, {1, 2, 3}, 10.0, 30.0, 15.0, 30.0, 50.0);
    EXPECT_NEAR(15.0, rep_shown_intervals(keys, 1, 50.0)[0].begin, 1e-9);
    EXPECT_NEAR(15.0, rep_shown_intervals(keys, 2, 50.0)[0].begin, 1e-9);
    EXPECT_NEAR(12.0, rep_shown_intervals(keys, 3, 50.0)[0].begin, 1e-9);   /* does not start at the end that moved */
}

UTEST(viamd_movie_keys, dragging_a_whole_group_moves_every_stretch_in_it_by_the_same_time) {
    std::vector<RepKey> keys = {vis_key(1, 0, false), vis_key(1, 10, true), vis_key(1, 20, false), vis_key(2, 0, false), vis_key(2, 15, true), vis_key(2, 30, false)};
    rep_move_group(&keys, {1, 2}, 10.0, 30.0, 14.0, 34.0, 60.0);
    const RepInterval a = rep_shown_intervals(keys, 1, 60.0)[0];
    const RepInterval b = rep_shown_intervals(keys, 2, 60.0)[0];
    EXPECT_NEAR(14.0, a.begin, 1e-9);
    EXPECT_NEAR(24.0, a.end, 1e-9);
    EXPECT_NEAR(19.0, b.begin, 1e-9);
    EXPECT_NEAR(34.0, b.end, 1e-9);
}

UTEST(viamd_movie_keys, stretches_that_touch_or_overlap_are_one_in_the_union) {
    std::vector<RepInterval> in(3);
    in[0].begin = 10; in[0].end = 20;
    in[1].begin = 20; in[1].end = 30;
    in[2].begin = 40; in[2].end = 50;
    const std::vector<RepInterval> u = rep_union_intervals(in);
    ASSERT_EQ(2, (int)u.size());
    EXPECT_NEAR(10.0, u[0].begin, 1e-9);
    EXPECT_NEAR(30.0, u[0].end, 1e-9);
    EXPECT_NEAR(40.0, u[1].begin, 1e-9);
}

UTEST(viamd_movie_keys, a_name_is_split_at_its_first_hyphen) {
    std::string g, m;
    EXPECT_TRUE(rep_name_split("protein-cpk", &g, &m));
    EXPECT_STREQ("protein", g.c_str());
    EXPECT_STREQ("cpk", m.c_str());
    EXPECT_TRUE(rep_name_split("protein-cpk-blue", &g, &m));
    EXPECT_STREQ("protein", g.c_str());
    EXPECT_STREQ("cpk-blue", m.c_str());
    EXPECT_FALSE(rep_name_split("water", &g, &m));
    EXPECT_STREQ("water", g.c_str());
    EXPECT_TRUE(m.empty());
    EXPECT_FALSE(rep_name_split("-odd", &g, &m));
    EXPECT_STREQ("-odd", g.c_str());
    EXPECT_FALSE(rep_name_split("odd-", &g, &m));
    EXPECT_STREQ("odd-", g.c_str());
}

UTEST(viamd_movie_keys, the_rows_of_a_lane_put_a_group_together_at_the_place_of_its_first_member) {
    const std::vector<std::string> names = {"protein-cartoon", "ligand", "protein-cpk", "water", "ligand-vdw"};
    const std::vector<RepRow> rows = rep_group_rows(names, {});
    ASSERT_EQ(7, (int)rows.size());
    EXPECT_TRUE(rows[0].header);  EXPECT_STREQ("protein", rows[0].label.c_str());  EXPECT_EQ(2, rows[0].members);
    EXPECT_STREQ("cartoon", rows[1].label.c_str());  EXPECT_TRUE(rows[1].indented);  EXPECT_EQ(0, rows[1].rep);
    EXPECT_STREQ("cpk", rows[2].label.c_str());      EXPECT_EQ(2, rows[2].rep);
    EXPECT_TRUE(rows[3].header);  EXPECT_STREQ("ligand", rows[3].label.c_str());
    EXPECT_STREQ("ligand", rows[4].label.c_str());   /* a member with the name of the group alone keeps its whole name */
    EXPECT_STREQ("vdw", rows[5].label.c_str());
    EXPECT_FALSE(rows[6].header);  EXPECT_STREQ("water", rows[6].label.c_str());  EXPECT_FALSE(rows[6].indented);
}

UTEST(viamd_movie_keys, a_collapsed_group_keeps_only_its_row) {
    const std::vector<std::string> names = {"protein-cartoon", "protein-cpk", "water"};
    const std::vector<RepRow> rows = rep_group_rows(names, {"protein"});
    ASSERT_EQ(2, (int)rows.size());
    EXPECT_TRUE(rows[0].header);
    EXPECT_STREQ("water", rows[1].label.c_str());
}

static CameraKeyframe cam_key(double time, float distance, bool follow = false, int atom = -1) {
    CameraKeyframe k = {};
    k.time = time;
    k.transform.distance = distance;
    k.follow = follow;
    k.follow_atom = atom;
    return k;
}

UTEST(viamd_movie_keys, system_rows_have_one_label_per_system_in_first_occurrence_order) {
    const auto rows = rep_system_rows({"protein-cartoon", "ligand", "protein-cpk", "water-vdw", "ligand-vdw"});
    ASSERT_EQ(3, (int)rows.size());
    EXPECT_STREQ("protein", rows[0].label.c_str());
    EXPECT_EQ(2, rows[0].members);
    EXPECT_EQ(0, rows[0].rep);
    EXPECT_STREQ("ligand", rows[1].label.c_str());
    EXPECT_EQ(2, rows[1].members);
    EXPECT_STREQ("water", rows[2].label.c_str());
    EXPECT_TRUE(rep_system_rows({}).empty());
}

UTEST(viamd_movie_keys, overlapping_blocks_stack_and_nonoverlapping_blocks_reuse_slots) {
    std::vector<RepBlock> blocks = {
        {0, 0, {0.0, 10.0}}, {1, 0, {5.0, 15.0}}, {2, 0, {10.0, 20.0}}, {0, 1, {20.0, 30.0}},
    };
    EXPECT_EQ(2, rep_pack_blocks(&blocks, 0.0));
    EXPECT_EQ(0, blocks[0].slot);
    EXPECT_EQ(1, blocks[1].slot);
    EXPECT_EQ(0, blocks[2].slot);
    EXPECT_EQ(0, blocks[3].slot);
    EXPECT_EQ(1, blocks[1].rep);
    EXPECT_EQ(1, blocks[3].interval_index);
}

UTEST(viamd_movie_keys, block_packing_accounts_for_transition_tails_and_unsorted_input) {
    std::vector<RepBlock> blocks = {{1, 0, {10.0, 20.0}}, {0, 0, {0.0, 10.0}}};
    EXPECT_EQ(2, rep_pack_blocks(&blocks, 2.0));
    EXPECT_EQ(0, blocks[1].slot);
    EXPECT_EQ(1, blocks[0].slot);
    std::vector<RepBlock> empty;
    EXPECT_EQ(1, rep_pack_blocks(&empty, 2.0));
}

UTEST(viamd_movie_keys, unkeyed_enabled_representations_cover_the_movie_but_hidden_ones_do_not) {
    const std::vector<RepKey> keys;
    const auto spans = rep_effective_intervals(keys, 1, 80.0, true);
    ASSERT_EQ(1, (int)spans.size());
    EXPECT_EQ(0.0, spans[0].begin);
    EXPECT_EQ(80.0, spans[0].end);
    EXPECT_EQ(-1, spans[0].begin_key);
    EXPECT_TRUE(rep_effective_intervals(keys, 1, 80.0, false).empty());
    const std::vector<RepKey> hidden = {vis_key(1, 0, false)};
    EXPECT_TRUE(rep_effective_intervals(hidden, 1, 80.0, true).empty());
}

UTEST(viamd_movie_keys, switching_a_block_merges_target_overlaps_and_preserves_other_representations) {
    std::vector<RepKey> keys = {
        vis_key(1, 0, false), vis_key(1, 10, true), vis_key(1, 20, false),
        vis_key(2, 0, false), vis_key(2, 15, true), vis_key(2, 25, false),
        vis_key(3, 0, true),
    };
    RepKey scale;
    scale.rep = 2;
    scale.prop = (int)RepProp::Scale0;
    scale.value[0] = 2.5f;
    keys.push_back(scale);
    rep_transfer_interval(&keys, 1, 2, rep_shown_intervals(keys, 1, 80.0)[0], 80.0);
    EXPECT_TRUE(rep_shown_intervals(keys, 1, 80.0).empty());
    const auto target = rep_shown_intervals(keys, 2, 80.0);
    ASSERT_EQ(1, (int)target.size());
    EXPECT_EQ(10.0, target[0].begin);
    EXPECT_EQ(25.0, target[0].end);
    EXPECT_EQ(80.0, rep_shown_intervals(keys, 3, 80.0)[0].end);
    float value[3] = {};
    ASSERT_TRUE(rep_keys_evaluate(value, keys.data(), keys.size(), 2, (int)RepProp::Scale0, 12.0));
    EXPECT_EQ(2.5f, value[0]);
}

UTEST(viamd_movie_keys, removing_initial_or_only_blocks_keeps_the_system_hidden_in_the_gap) {
    std::vector<RepKey> keys = {vis_key(1, 0, true), vis_key(1, 10, false), vis_key(1, 20, true)};
    rep_remove_interval(&keys, rep_shown_intervals(keys, 1, 80.0)[0]);
    const auto spans = rep_shown_intervals(keys, 1, 80.0);
    ASSERT_EQ(1, (int)spans.size());
    EXPECT_EQ(20.0, spans[0].begin);
    rep_remove_interval(&keys, spans[0]);
    EXPECT_TRUE(rep_effective_intervals(keys, 1, 80.0, true).empty());
    std::vector<RepKey> only = {vis_key(2, 0, true)};
    rep_remove_interval(&only, rep_shown_intervals(only, 2, 80.0)[0]);
    EXPECT_TRUE(rep_effective_intervals(only, 2, 80.0, true).empty());
}

UTEST(viamd_movie_keys, switching_representation_blocks_is_undoable_without_changing_other_keys) {
    MovieKeys cur;
    cur.reps = {vis_key(1, 0, false), vis_key(1, 10, true), vis_key(1, 20, false)};
    const MovieKeys before = cur;
    MovieHistory history;
    history.clear(cur);
    rep_transfer_interval(&cur.reps, 1, 2, rep_shown_intervals(cur.reps, 1, 80.0)[0], 80.0);
    const MovieKeys after = cur;
    history.update(cur, false);
    ASSERT_TRUE(history.undo(&cur));
    EXPECT_TRUE(movie_keys_equal(before, cur));
    ASSERT_TRUE(history.redo(&cur));
    EXPECT_TRUE(movie_keys_equal(after, cur));
}

UTEST(viamd_movie_keys, camera_bands_are_runs_of_keys_that_follow_the_same_thing) {
    std::vector<CameraKeyframe> keys = {
        cam_key(0, 10), cam_key(1, 10, true), cam_key(2, 10, true), cam_key(3, 10, true, 7), cam_key(4, 10, true, 7), cam_key(5, 10), cam_key(6, 10, true),
    };
    const std::vector<CameraBand> bands = camera_bands(keys);
    ASSERT_EQ(bands.size(), (size_t)3);
    EXPECT_EQ(bands[0].kind, CameraBandKind::FollowTarget);
    EXPECT_EQ(bands[0].first, 1);
    EXPECT_EQ(bands[0].last, 2);
    EXPECT_EQ(bands[1].kind, CameraBandKind::LookAtAtom);
    EXPECT_EQ(bands[1].atom, 7);
    EXPECT_EQ(bands[1].begin, 3.0);
    EXPECT_EQ(bands[1].end, 4.0);
    EXPECT_EQ(bands[2].kind, CameraBandKind::FollowTarget);
    EXPECT_EQ(bands[2].first, 6);
    EXPECT_EQ(bands[2].last, 6);
}

UTEST(viamd_movie_keys, a_spin_is_a_band_over_the_stretch_leading_to_its_key) {
    std::vector<CameraKeyframe> keys = {cam_key(0, 10), cam_key(2, 10), cam_key(5, 10)};
    keys[0].spin_turns = 3;   // Nothing leads to the first key
    keys[2].spin_turns = -2;
    const std::vector<CameraBand> bands = camera_bands(keys);
    ASSERT_EQ(bands.size(), (size_t)1);
    EXPECT_EQ(bands[0].kind, CameraBandKind::Spin);
    EXPECT_EQ(bands[0].begin, 2.0);
    EXPECT_EQ(bands[0].end, 5.0);
    EXPECT_EQ(bands[0].turns, -2);
    EXPECT_TRUE(camera_bands({}).empty());
}

UTEST(viamd_movie_keys, a_key_is_labelled_by_its_number_and_its_name) {
    CameraKeyframe k = {};
    EXPECT_STREQ(camera_key_label(k, 0).c_str(), "1");
    strcpy(k.name, "close-up");
    EXPECT_STREQ(camera_key_label(k, 2).c_str(), "3 close-up");
}

UTEST(viamd_movie_keys, a_key_on_the_path_does_not_move_the_camera) {
    std::vector<CameraKeyframe> keys = {cam_key(0, 10), cam_key(4, 30), cam_key(8, 20)};
    keys[1].spin_turns = 1;
    keys[1].use_frame = true;
    strcpy(keys[1].name, "mid");
    const CameraKeyframe k = camera_key_on_path(keys, 2.0, false);
    ViewTransform vt;
    float fov;
    camera_keyframes_evaluate(&vt, &fov, keys.data(), keys.size(), 2.0, false);
    EXPECT_EQ(k.time, 2.0);
    EXPECT_EQ(k.transform.distance, vt.distance);
    EXPECT_EQ(k.fov_y, fov);
    EXPECT_EQ(k.spin_turns, 0);
    EXPECT_FALSE(k.use_frame);
    EXPECT_EQ(k.name[0], '\0');
    EXPECT_FALSE(k.follow);
}

UTEST(viamd_movie_keys, a_key_between_keys_that_follow_follows_too) {
    std::vector<CameraKeyframe> keys = {cam_key(0, 10, true, 5), cam_key(4, 10, true, 5), cam_key(8, 10)};
    keys[0].follow_center = {0, 0, 0};
    keys[1].follow_center = {4, 0, 0};
    CameraKeyframe k = camera_key_on_path(keys, 1.0, false);
    EXPECT_TRUE(k.follow);
    EXPECT_EQ(k.follow_atom, 5);
    EXPECT_NEAR(k.follow_center.x, 1.0f, 1.0e-5f);

    // Between a key that follows and one that does not it stays fixed, and so it does when the keys on one side follow nothing
    EXPECT_FALSE(camera_key_on_path(keys, 6.0, false).follow);

    // Outside the keys it holds what the end key does
    keys[2].follow = true;
    keys[2].follow_atom = 5;
    keys[2].follow_center = {9, 0, 0};
    k = camera_key_on_path(keys, 10.0, false);
    EXPECT_TRUE(k.follow);
    EXPECT_NEAR(k.follow_center.x, 9.0f, 1.0e-5f);
}

UTEST(viamd_movie_keys, renaming_a_key_is_an_edit) {
    MovieKeys a = keys_with(2), b = keys_with(2);
    EXPECT_TRUE(movie_keys_equal(a, b));
    strcpy(b.camera[1].name, "end");
    EXPECT_FALSE(movie_keys_equal(a, b));
}

UTEST(viamd_movie_keys, the_path_is_found_between_its_samples_and_held_outside) {
    CameraPathSamples s;
    s.time = {0.0, 2.0, 6.0};
    s.eye = {{0, 0, 0}, {2, 0, 0}, {2, 4, 0}};
    s.look = {{0, 0, 1}, {0, 0, 1}, {0, 0, 5}};
    vec3_t e, l;
    camera_path_at(s, 1.0, &e, &l);
    EXPECT_NEAR(e.x, 1.0f, 1.0e-6f);
    camera_path_at(s, 4.0, &e, &l);
    EXPECT_NEAR(e.y, 2.0f, 1.0e-6f);
    EXPECT_NEAR(l.z, 3.0f, 1.0e-6f);
    camera_path_at(s, -3.0, &e, &l);
    EXPECT_EQ(e.x, 0.0f);
    camera_path_at(s, 99.0, &e, &l);
    EXPECT_EQ(e.y, 4.0f);
}

UTEST(viamd_movie_keys, ticks_have_a_round_step_that_gives_few_enough_of_them) {
    EXPECT_EQ(camera_tick_step(10.0, 30), 0.5);
    EXPECT_EQ(camera_tick_step(30.0, 30), 1.0);
    EXPECT_EQ(camera_tick_step(60.0, 30), 2.0);
    EXPECT_EQ(camera_tick_step(300.0, 30), 10.0);
    EXPECT_EQ(camera_tick_step(0.0, 30), 1.0);
    for (double span : {0.7, 3.3, 12.0, 47.0, 1000.0}) EXPECT_LE(span / camera_tick_step(span, 20), 20.0);
}

UTEST(viamd_movie_keys, a_line_is_cut_at_the_camera) {
    vec4_t a = {0, 0, 0, 2.0f}, b = {4, 0, 0, -2.0f};
    EXPECT_TRUE(clip_segment_near(&a, &b));
    EXPECT_EQ(a.w, 2.0f);
    EXPECT_NEAR(b.w, 1.0e-3f, 1.0e-6f);
    EXPECT_NEAR(b.x, 2.0f, 0.01f);
    vec4_t c = {0, 0, 0, 1.0f}, d = {1, 0, 0, 3.0f};
    EXPECT_TRUE(clip_segment_near(&c, &d));
    EXPECT_EQ(d.x, 1.0f);
    vec4_t e = {0, 0, 0, -1.0f}, f = {1, 0, 0, -3.0f};
    EXPECT_FALSE(clip_segment_near(&e, &f));
}

UTEST(viamd_movie_keys, the_spin_ring_goes_through_the_eye_and_turns_the_way_of_the_axis) {
    CameraKeyframe from = cam_key(0, 10), to = cam_key(4, 10);
    to.spin_turns = 1;
    to.spin_axis = SpinAxis::WorldY;
    to.transform.position = {3, 5, 4};   // The look-at point is 10 in front of it, along -z in the camera's own frame
    vec3_t c, u, v;
    float r;
    ASSERT_TRUE(camera_spin_ring(from, to, &c, &u, &v, &r));
    const vec3_t eye = c + u * r;
    EXPECT_NEAR(eye.x, 3.0f, 1.0e-4f);
    EXPECT_NEAR(eye.y, 5.0f, 1.0e-4f);
    EXPECT_NEAR(eye.z, 4.0f, 1.0e-4f);
    EXPECT_NEAR(vec3_dot(u, vec3_t{0, 1, 0}), 0.0f, 1.0e-5f);
    // A positive turn is counter-clockwise seen from the tip of the axis
    const vec3_t axis = vec3_cross(u, v);
    EXPECT_NEAR(axis.y, 1.0f, 1.0e-5f);

    // The eye on the axis has no ring
    to.transform.orientation = quat_axis_angle(vec3_t{1, 0, 0}, -1.5707963f);   // Looks straight down, so the eye is above what it looks at
    EXPECT_FALSE(camera_spin_ring(from, to, &c, &u, &v, &r));
}

UTEST(viamd_movie_keys, a_ray_meets_a_plane_in_front_of_it) {
    vec3_t p;
    EXPECT_TRUE(ray_plane_hit({0, 0, 0}, {0, 0, -1}, {0, 0, -5}, {0, 0, 1}, &p));
    EXPECT_NEAR(p.z, -5.0f, 1.0e-6f);
    EXPECT_FALSE(ray_plane_hit({0, 0, 0}, {1, 0, 0}, {0, 0, -5}, {0, 0, 1}, &p));
    EXPECT_FALSE(ray_plane_hit({0, 0, 0}, {0, 0, 1}, {0, 0, -5}, {0, 0, 1}, &p));
}

UTEST(viamd_movie_keys, the_nearest_point_of_a_polyline_skips_what_cannot_be_used) {
    std::vector<vec2_t> pts = {{0, 0}, {10, 0}, {10, 10}, {20, 10}};
    std::vector<char> ok = {1, 1, 1, 1};
    int seg = -1;
    float u = -1.0f;
    float d = polyline_nearest(pts, ok, {4, 3}, &seg, &u);
    EXPECT_NEAR(d, 3.0f, 1.0e-5f);
    EXPECT_EQ(seg, 0);
    EXPECT_NEAR(u, 0.4f, 1.0e-5f);
    d = polyline_nearest(pts, ok, {12, 7}, &seg, &u);
    EXPECT_EQ(seg, 1);
    ok[2] = 0;   // The middle segments lose an end
    d = polyline_nearest(pts, ok, {12, 7}, &seg, &u);
    EXPECT_EQ(seg, 0);
    EXPECT_NEAR(d, 7.0f * 1.0f + 0.0f, 8.0f);
    EXPECT_EQ(polyline_nearest({}, {}, {0, 0}, &seg, &u), FLT_MAX);
}

UTEST(viamd_movie_keys, a_key_is_edited_by_its_eye_or_by_what_it_looks_at) {
    CameraKeyframe k = cam_key(0, 10);
    k.transform.position = {0, 0, 10};   // Looks along -z at the origin
    const vec3_t look = camera_get_look_at(k.transform);

    CameraKeyframe a = k;
    ASSERT_TRUE(camera_key_set_eye(&a, {5, 0, 10}));
    const vec3_t look_a = camera_get_look_at(a.transform);
    EXPECT_NEAR(look_a.x, look.x, 1.0e-3f);
    EXPECT_NEAR(look_a.y, look.y, 1.0e-3f);
    EXPECT_NEAR(look_a.z, look.z, 1.0e-3f);
    EXPECT_NEAR(a.transform.position.x, 5.0f, 1.0e-6f);

    CameraKeyframe b = k;
    ASSERT_TRUE(camera_key_set_look(&b, {4, 0, 0}));
    EXPECT_NEAR(b.transform.position.z, 10.0f, 1.0e-6f);
    const vec3_t look_b = camera_get_look_at(b.transform);
    EXPECT_NEAR(look_b.x, 4.0f, 1.0e-3f);

    CameraKeyframe c = k;
    c.follow = true;
    c.follow_center = {1, 1, 1};
    camera_key_translate(&c, {1, 2, 3});
    const vec3_t look_c = camera_get_look_at(c.transform);
    EXPECT_NEAR(look_c.x, look.x + 1.0f, 1.0e-4f);
    EXPECT_NEAR(look_c.z, look.z + 3.0f, 1.0e-4f);
    EXPECT_NEAR(c.follow_center.y, 3.0f, 1.0e-6f);

    // The eye on the point it looks at cannot aim
    CameraKeyframe d = k;
    EXPECT_FALSE(camera_key_set_eye(&d, look));
}

UTEST(viamd_movie_keys, roll_and_keeping_upright_are_edits_that_can_be_undone) {
    MovieKeys a, b;
    a.camera = {cam_key(0, 10)};
    b = a;
    b.camera[0].roll = 0.2f;
    EXPECT_FALSE(movie_keys_equal(a, b));
    b = a;
    b.keep_upright = !a.keep_upright;
    EXPECT_FALSE(movie_keys_equal(a, b));
    b = a;
    b.up_axis = 2;
    EXPECT_FALSE(movie_keys_equal(a, b));
}

UTEST(viamd_movie_keys, a_key_on_an_upright_path_takes_the_roll_there) {
    std::vector<CameraKeyframe> keys = {cam_key(0, 10), cam_key(4, 30)};
    keys[0].roll = 0.0f;
    keys[1].roll = 0.6f;
    const vec3_t up = {0, 1, 0};
    const CameraKeyframe k = camera_key_on_path(keys, 4.0, false, &up);
    EXPECT_NEAR(k.roll, 0.6f, 1.0e-4f);
    EXPECT_EQ(camera_key_on_path(keys, 4.0, false).roll, 0.0f);
}

/* Selecting and moving keys together */

static RepKey rk(uint32_t rep, int prop, double time, float value = 1.0f) {
    RepKey k;
    k.rep = rep;
    k.prop = prop;
    k.time = time;
    k.value[0] = value;
    return k;
}

static MovieKeys select_keys() {
    MovieKeys k;
    k.duration = 20.0f;
    for (double t : {2.0, 5.0, 9.0, 14.0}) k.camera.push_back(cam_key(t, 10));
    k.params.push_back(pk(1, 3.0, 1.0f));
    k.params.push_back(pk(1, 7.0, 2.0f));
    k.params.push_back(pk(2, 3.0, 5.0f));
    k.reps.push_back(rk(4, 2, 6.0, 0.5f));
    return k;
}

UTEST(viamd_movie_keys, a_selection_picks_keys_by_what_they_belong_to_and_their_time) {
    KeySelection s;
    s.add(KeyKind::Camera, 0, 5.0);
    s.add(KeyKind::Camera, 0, 5.0);
    s.add(KeyKind::Param, 1, 5.0);
    EXPECT_EQ(s.size(), (size_t)2);
    EXPECT_TRUE(s.contains(KeyKind::Param, 1, 5.0));
    EXPECT_FALSE(s.contains(KeyKind::Param, 2, 5.0));
    s.toggle(KeyKind::Camera, 0, 5.0);
    EXPECT_FALSE(s.contains(KeyKind::Camera, 0, 5.0));
    s.toggle(KeyKind::Camera, 0, 6.0);
    EXPECT_TRUE(s.contains(KeyKind::Camera, 0, 6.0));
    s.set(KeyKind::Rep, rep_key_subject(4, 2), 1.0);
    EXPECT_EQ(s.size(), (size_t)1);
}

UTEST(viamd_movie_keys, a_selection_forgets_keys_that_are_gone_and_can_take_all_of_a_lane) {
    const MovieKeys keys = select_keys();
    KeySelection s;
    key_selection_all(&s, keys, 1, -1);
    EXPECT_EQ(s.size(), (size_t)6);   // 4 camera keys and 2 of parameter 1
    key_selection_all(&s, keys, -1, rep_key_subject(4, 2));
    EXPECT_EQ(s.size(), (size_t)7);
    s.add(KeyKind::Camera, 0, 99.0);
    key_selection_prune(&s, keys);
    EXPECT_EQ(s.size(), (size_t)7);
    EXPECT_FALSE(s.contains(KeyKind::Camera, 0, 99.0));
}

UTEST(viamd_movie_keys, selected_keys_move_together_and_the_others_stay) {
    const MovieKeys start = select_keys();
    KeySelection sel;
    sel.add(KeyKind::Camera, 0, 5.0);
    sel.add(KeyKind::Camera, 0, 9.0);
    sel.add(KeyKind::Param, 1, 7.0);
    MovieKeys out;
    KeySelection out_sel;
    const double used = movie_keys_shift(&out, &out_sel, start, sel, 1.5, KeyShift{}, 20.0);
    EXPECT_EQ(used, 1.5);
    EXPECT_EQ(out.camera[0].time, 2.0);
    EXPECT_EQ(out.camera[1].time, 6.5);
    EXPECT_EQ(out.camera[2].time, 10.5);
    EXPECT_EQ(out.camera[3].time, 14.0);
    EXPECT_EQ(out.params[0].time, 3.0);
    EXPECT_EQ(out.params[1].time, 8.5);
    EXPECT_EQ(out.params[2].time, 3.0);
    EXPECT_EQ(out.reps[0].time, 6.0);
    EXPECT_TRUE(out_sel.contains(KeyKind::Camera, 0, 6.5));
    EXPECT_TRUE(out_sel.contains(KeyKind::Param, 1, 8.5));
    EXPECT_FALSE(out_sel.contains(KeyKind::Camera, 0, 5.0));
    // What it was made from is left alone
    EXPECT_EQ(start.camera[1].time, 5.0);
}

UTEST(viamd_movie_keys, a_group_stops_at_the_ends_of_the_movie_as_a_whole) {
    const MovieKeys start = select_keys();
    KeySelection sel;
    sel.add(KeyKind::Camera, 0, 9.0);
    sel.add(KeyKind::Camera, 0, 14.0);
    MovieKeys out;
    KeySelection out_sel;
    double used = movie_keys_shift(&out, &out_sel, start, sel, 100.0, KeyShift{}, 20.0);
    EXPECT_EQ(used, 6.0);
    EXPECT_EQ(out.camera[2].time, 15.0);   // The spacing of the group is kept
    EXPECT_EQ(out.camera[3].time, 20.0);
    used = movie_keys_shift(&out, &out_sel, start, sel, -100.0, KeyShift{}, 20.0);
    EXPECT_EQ(used, -9.0);
    EXPECT_EQ(out.camera[2].time, 0.0);
    EXPECT_EQ(out.camera[3].time, 5.0);
    // Nothing selected: nothing moves
    KeySelection none;
    used = movie_keys_shift(&out, &out_sel, start, none, 3.0, KeyShift{}, 20.0);
    EXPECT_EQ(used, 0.0);
    EXPECT_EQ(out.camera[1].time, 5.0);
    EXPECT_EQ(out.params[1].time, 7.0);
}

UTEST(viamd_movie_keys, the_values_of_a_lane_move_with_the_keys_and_stay_in_their_limits) {
    MovieKeys start = select_keys();
    start.camera[0].use_frame = true;
    start.camera[0].frame = 40.0;
    start.camera[1].use_frame = true;
    start.camera[1].frame = 90.0;
    start.camera[0].fov_y = 0.5f;
    KeySelection sel;
    sel.add(KeyKind::Camera, 0, 2.0);
    sel.add(KeyKind::Camera, 0, 5.0);
    sel.add(KeyKind::Param, 1, 3.0);
    sel.add(KeyKind::Rep, rep_key_subject(4, 2), 6.0);
    MovieKeys out;
    KeySelection out_sel;

    KeyShift frame;
    frame.lane = KeyLane::Frame;
    frame.dy = 25.0;
    frame.lo = 0.0;
    frame.hi = 100.0;
    movie_keys_shift(&out, &out_sel, start, sel, 0.0, frame, 20.0);
    EXPECT_EQ(out.camera[0].frame, 65.0);
    EXPECT_EQ(out.camera[1].frame, 100.0);   // Limited

    KeyShift fov;
    fov.lane = KeyLane::Fov;
    fov.dy = 1000.0;
    movie_keys_shift(&out, &out_sel, start, sel, 0.0, fov, 20.0);
    EXPECT_NEAR(out.camera[0].fov_y, 170.0f * 3.14159265f / 180.0f, 1.0e-4f);

    KeyShift param;
    param.lane = KeyLane::Param;
    param.subject = 1;
    param.dy = 3.0;
    param.lo = 0.0;
    param.hi = 10.0;
    movie_keys_shift(&out, &out_sel, start, sel, 0.0, param, 20.0);
    EXPECT_EQ(out.params[0].value[0], 4.0f);
    EXPECT_EQ(out.params[1].value[0], 2.0f);   // Not selected
    EXPECT_EQ(out.params[2].value[0], 5.0f);   // Another parameter

    KeyShift rep;
    rep.lane = KeyLane::Rep;
    rep.subject = rep_key_subject(4, 2);
    rep.dy = 2.0;
    rep.ratio = true;
    rep.lo = 0.0;
    rep.hi = 10.0;
    movie_keys_shift(&out, &out_sel, start, sel, 0.0, rep, 20.0);
    EXPECT_EQ(out.reps[0].value[0], 1.0f);
}

UTEST(viamd_movie_keys, a_distance_moves_along_the_line_of_sight_and_the_camera_keeps_looking_at_the_same_point) {
    MovieKeys start;
    CameraKeyframe k = cam_key(1.0, 10.0f);
    k.transform.position = {2, 3, 10};
    start.camera.push_back(k);
    const vec3_t look = camera_get_look_at(k.transform);
    KeySelection sel;
    sel.add(KeyKind::Camera, 0, 1.0);
    KeyShift s;
    s.lane = KeyLane::Distance;
    s.unit = 2.0;    // Shown in units that are twice the internal ones
    s.dy = 6.0;      // 6 shown = 3 internal
    MovieKeys out;
    KeySelection out_sel;
    movie_keys_shift(&out, &out_sel, start, sel, 0.0, s, 20.0);
    EXPECT_NEAR(out.camera[0].transform.distance, 13.0f, 1.0e-5f);
    const vec3_t look_after = camera_get_look_at(out.camera[0].transform);
    EXPECT_NEAR(look_after.x, look.x, 1.0e-4f);
    EXPECT_NEAR(look_after.z, look.z, 1.0e-4f);
    s.dy = -1000.0;
    movie_keys_shift(&out, &out_sel, start, sel, 0.0, s, 20.0);
    EXPECT_GT(out.camera[0].transform.distance, 0.0f);
}

UTEST(viamd_movie_keys, a_moved_key_that_lands_on_another_replaces_it_and_everything_is_sorted) {
    MovieKeys keys = select_keys();
    KeySelection sel;
    sel.add(KeyKind::Camera, 0, 5.0);
    sel.add(KeyKind::Param, 1, 3.0);
    keys.camera[1].time = 9.0;           // On the camera key at 9
    keys.camera[1].fov_y = 0.123f;
    sel.ids[0].time = 9.0;
    keys.params[0].time = 7.0;           // On the parameter key at 7
    keys.params[0].value[0] = 42.0f;
    sel.ids[1].time = 7.0;
    keys.camera.push_back(cam_key(1.0, 10));   // Out of order
    movie_keys_resolve(&keys, sel);
    ASSERT_EQ(keys.camera.size(), (size_t)4);
    EXPECT_EQ(keys.camera[0].time, 1.0);
    EXPECT_EQ(keys.camera[2].time, 9.0);
    EXPECT_EQ(keys.camera[2].fov_y, 0.123f);   // The selected one stayed
    ASSERT_EQ(keys.params.size(), (size_t)2);
    EXPECT_EQ(keys.params[0].value[0], 42.0f);
    EXPECT_EQ(keys.params[1].param, 2);
}

UTEST(viamd_movie_keys, selected_keys_are_deleted) {
    MovieKeys keys = select_keys();
    KeySelection sel;
    sel.add(KeyKind::Camera, 0, 2.0);
    sel.add(KeyKind::Param, 2, 3.0);
    sel.add(KeyKind::Rep, rep_key_subject(4, 2), 6.0);
    movie_keys_delete(&keys, &sel);
    EXPECT_EQ(keys.camera.size(), (size_t)3);
    EXPECT_EQ(keys.params.size(), (size_t)2);
    EXPECT_TRUE(keys.reps.empty());
    EXPECT_TRUE(sel.empty());
}

UTEST(viamd_movie_keys, a_copy_is_put_in_with_its_spacing_and_inside_the_movie) {
    MovieKeys keys = select_keys();
    KeySelection sel;
    sel.add(KeyKind::Camera, 0, 2.0);
    sel.add(KeyKind::Camera, 0, 5.0);
    sel.add(KeyKind::Param, 1, 7.0);
    const KeyClip clip = movie_keys_copy(keys, sel);
    EXPECT_EQ(clip.camera.size(), (size_t)2);
    EXPECT_EQ(clip.params.size(), (size_t)1);
    EXPECT_EQ(clip.begin, 2.0);

    KeySelection pasted;
    movie_keys_paste(&keys, &pasted, clip, 10.0, 20.0);
    EXPECT_EQ(keys.camera.size(), (size_t)6);
    EXPECT_TRUE(pasted.contains(KeyKind::Camera, 0, 10.0));
    EXPECT_TRUE(pasted.contains(KeyKind::Camera, 0, 13.0));
    EXPECT_TRUE(pasted.contains(KeyKind::Param, 1, 15.0));
    EXPECT_EQ(pasted.size(), (size_t)3);

    // Near the end it is moved earlier as a whole
    KeySelection late;
    movie_keys_paste(&keys, &late, clip, 19.0, 20.0);
    EXPECT_TRUE(late.contains(KeyKind::Camera, 0, 12.0) || late.contains(KeyKind::Camera, 0, 15.0));
    for (const KeyId& id : late.ids) EXPECT_LE(id.time, 20.0);

    // An empty clip changes nothing
    const size_t count = keys.camera.size();
    movie_keys_paste(&keys, &late, KeyClip{}, 3.0, 20.0);
    EXPECT_EQ(keys.camera.size(), count);
}

static MovieOverlay bar(double begin, double end) {
    MovieOverlay o;
    o.begin = begin;
    o.end = end;
    return o;
}

UTEST(viamd_movie_keys, overlay_bars_move_with_what_is_timed_inside_them) {
    MovieKeys start;
    start.duration = 20.0f;
    start.overlays = {bar(1.0, 4.0), bar(6.0, 9.0), bar(10.0, 12.0)};
    MoviePlotPanel panel;
    panel.begin = 7.0;
    panel.end = 8.0;
    start.overlays[1].panels.push_back(panel);
    start.camera.push_back(cam_key(5.0, 10));
    KeySelection sel;
    sel.add(KeyKind::Overlay, 1, 6.0, 9.0);
    sel.add(KeyKind::Camera, 0, 5.0);
    MovieKeys out;
    KeySelection out_sel;
    double used = movie_keys_shift(&out, &out_sel, start, sel, 2.5, KeyShift{}, 20.0);
    EXPECT_EQ(used, 2.5);
    EXPECT_EQ(out.overlays[0].begin, 1.0);
    EXPECT_EQ(out.overlays[1].begin, 8.5);
    EXPECT_EQ(out.overlays[1].end, 11.5);
    EXPECT_EQ(out.overlays[1].panels[0].begin, 9.5);
    EXPECT_EQ(out.overlays[1].panels[0].end, 10.5);
    EXPECT_EQ(out.camera[0].time, 7.5);
    EXPECT_TRUE(out_sel.contains(KeyKind::Overlay, 1, 8.5));
    EXPECT_EQ(out_sel.ids[0].end, 11.5);

    // The end of the bar is what stops it at the end of the movie
    used = movie_keys_shift(&out, &out_sel, start, sel, 100.0, KeyShift{}, 20.0);
    EXPECT_EQ(used, 11.0);
    EXPECT_EQ(out.overlays[1].end, 20.0);
    used = movie_keys_shift(&out, &out_sel, start, sel, -100.0, KeyShift{}, 20.0);
    EXPECT_EQ(used, -5.0);
    EXPECT_EQ(out.overlays[1].begin, 1.0);
}

UTEST(viamd_movie_keys, representation_blocks_move_together_and_follow_their_keys) {
    MovieKeys start;
    start.duration = 20.0f;
    start.reps = {rk(7, 0, 0.0, 0.0f), rk(7, 0, 2.0, 1.0f), rk(7, 0, 4.0, 0.0f), rk(9, 0, 0.0, 0.0f), rk(9, 0, 10.0, 1.0f), rk(9, 0, 14.0, 0.0f)};
    KeySelection sel;
    sel.add(KeyKind::Block, 7, 2.0, 4.0);
    sel.add(KeyKind::Block, 9, 10.0, 14.0);
    MovieKeys out;
    KeySelection out_sel;
    double used = movie_keys_shift(&out, &out_sel, start, sel, 3.0, KeyShift{}, 20.0);
    EXPECT_EQ(used, 3.0);
    EXPECT_EQ(out.reps[1].time, 5.0);
    EXPECT_EQ(out.reps[2].time, 7.0);
    EXPECT_EQ(out.reps[4].time, 13.0);
    EXPECT_EQ(out.reps[5].time, 17.0);
    EXPECT_TRUE(out_sel.contains(KeyKind::Block, 7, 5.0));
    EXPECT_TRUE(out_sel.contains(KeyKind::Block, 9, 13.0));
    EXPECT_EQ(out_sel.ids[1].end, 17.0);

    // The group stops at the end of the movie by the block that is the furthest
    used = movie_keys_shift(&out, &out_sel, start, sel, 100.0, KeyShift{}, 20.0);
    EXPECT_EQ(used, 6.0);
    EXPECT_EQ(out.reps[5].time, 20.0);
    EXPECT_EQ(out.reps[2].time, 10.0);

    // A block of the selection that no stretch is there for is left alone
    KeySelection stale;
    stale.add(KeyKind::Block, 7, 3.0, 4.0);
    used = movie_keys_shift(&out, &out_sel, start, stale, 3.0, KeyShift{}, 20.0);
    EXPECT_EQ(out.reps[1].time, 2.0);
}

UTEST(viamd_movie_keys, a_picked_block_is_removed_with_its_keys_and_stale_bars_and_blocks_are_forgotten) {
    MovieKeys keys;
    keys.duration = 20.0f;
    keys.reps = {rk(7, 0, 0.0, 0.0f), rk(7, 0, 2.0, 1.0f), rk(7, 0, 4.0, 0.0f)};
    keys.overlays = {bar(1.0, 4.0)};
    KeySelection sel;
    sel.add(KeyKind::Block, 7, 2.0, 4.0);
    sel.add(KeyKind::Overlay, 0, 1.0, 4.0);
    sel.add(KeyKind::Overlay, 3, 1.0, 4.0);      // No such bar
    sel.add(KeyKind::Block, 7, 8.0, 9.0);        // No such stretch
    key_selection_prune(&sel, keys);
    EXPECT_EQ(sel.size(), (size_t)2);
    movie_keys_delete(&keys, &sel);
    for (const RepKey& k : keys.reps) EXPECT_LT(k.value[0], 0.5f);   // Nothing is shown any more
    EXPECT_EQ(keys.overlays.size(), (size_t)1);                      // The bar stays
    EXPECT_TRUE(sel.empty());
}
