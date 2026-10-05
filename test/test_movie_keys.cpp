#include "utest.h"

#include <gfx/camera_utils.h>
#include <movie_keys.h>

#include <math.h>

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
