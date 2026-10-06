#include "utest.h"

#include <gfx/camera_utils.h>
#include <movie_keys.h>

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
    k.value = value;
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
