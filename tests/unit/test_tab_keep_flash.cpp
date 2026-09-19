#include <cmath>
#include <cstdio>

#include "../../src/ecs/tab_keep_flash.h"

namespace kf = hanabi::tab_keep_flash;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 0.002f; }

static void test_an_unpinned_tab_is_never_refused_or_lit() {
    kf::Store s;
    CHECK(!s.refuse_close("a", false, 10.0, false));
    CHECK(!s.is_lit("a") && s.generation() == 0);
    CHECK(s.mark("a", 10.0, false) == (kf::Mark{false, true, 0.0f}));
}

static void test_a_refused_pinned_tab_lights_for_the_hold_then_rests() {
    kf::Store s;
    CHECK(s.refuse_close("a", true, 10.0, false));
    CHECK(s.is_lit("a") && s.generation() == 1);
    CHECK(s.mark("a", 10.0, false).lit && near(s.mark("a", 10.0, false).progress, 0.0f));
    CHECK(near(s.mark("a", 10.06, false).progress, 0.5f));
    CHECK(s.mark("a", 10.12, false) == (kf::Mark{true, true, 1.0f}));
    CHECK(s.mark("a", 10.239, false).lit);
    const kf::Mark off = s.mark("a", 10.24, false);
    CHECK(!off.lit && near(off.progress, 1.0f) && s.generation() == 2);
    CHECK(near(s.mark("a", 10.30, false).progress, 0.5f));
    CHECK(s.mark("a", 10.36, false) == (kf::Mark{false, true, 0.0f}));
    CHECK(s.empty());
}

static void test_reduce_motion_holds_longer_and_never_animates() {
    kf::Store s;
    CHECK(s.refuse_close("a", true, 10.0, true));
    CHECK(s.mark("a", 10.0, true) == (kf::Mark{true, false, 1.0f}));
    CHECK(s.mark("a", 10.5, true) == (kf::Mark{true, false, 1.0f}));
    CHECK(near(kf::scale_for(s.mark("a", 10.5, true)), 1.0f));
    CHECK(near(kf::opacity_for(s.mark("a", 10.5, true)), 1.0f));
    CHECK(s.mark("a", 10.599, true).lit);
    CHECK(s.mark("a", 10.6, true) == (kf::Mark{false, false, 0.0f}));
    CHECK(s.empty());
}

static void test_a_relight_during_the_fade_continues_from_the_shown_progress() {
    kf::Store s;
    s.refuse_close("a", true, 10.0, false);
    const kf::Mark before = s.mark("a", 10.30, false);
    CHECK(!before.lit && s.fading("a") && near(before.progress, 0.5f));
    CHECK(s.refuse_close("a", true, 10.30, false));
    const kf::Mark after = s.mark("a", 10.30, false);
    CHECK(after.lit && !s.fading("a") && near(after.progress, before.progress));
    CHECK(s.mark("a", 10.33, false).progress > after.progress);
    CHECK(near(s.mark("a", 10.36, false).progress, 0.5f + 0.5f * kf::ease_in_out(0.5f)));
    CHECK(near(s.mark("a", 10.42, false).progress, 1.0f));
    CHECK(s.mark("a", 10.53, false).lit && !s.mark("a", 10.541, false).lit);
    const kf::Mark fadeStart = s.mark("a", 10.541, false);
    CHECK(!fadeStart.lit && near(fadeStart.progress, 1.0f));
    CHECK(near(s.mark("a", 10.60, false).progress, 0.5f));
}

static void test_a_fade_cut_short_by_expiry_fades_from_where_it_was() {
    kf::Store s;
    s.refuse_close("a", true, 10.0, false);
    s.refuse_close("a", true, 10.0, false);
    s.sweep(10.24);
    CHECK(s.fading("a") && near(s.mark("a", 10.24, false).progress, 1.0f));
    s.refuse_close("b", true, 20.0, false);
    CHECK(near(s.mark("b", 20.06, false).progress, 0.5f));
    s.refuse_close("c", true, 30.0, false);
    s.sweep(30.24);
    s.refuse_close("c", true, 30.30, false);
    s.sweep(30.30 + 0.24);
    CHECK(near(s.mark("c", 30.54, false).progress, 1.0f));
}

static void test_a_relight_during_the_fade_clears_the_fade_and_sweep_retires_it() {
    kf::Store s;
    s.refuse_close("a", true, 10.0, false);
    CHECK(!s.mark("a", 10.30, false).lit && s.fading("a"));
    CHECK(s.refuse_close("a", true, 10.31, false));
    CHECK(s.is_lit("a") && !s.fading("a"));
    s.sweep(10.31 + 0.24 + 0.12);
    CHECK(!s.is_lit("a") && !s.fading("a") && s.empty());
    s.refuse_close("b", true, 20.0, false);
    s.sweep(20.30);
    CHECK(s.fading("b") && !s.is_lit("b") && !s.empty());
    s.sweep(20.36);
    CHECK(!s.fading("b") && s.empty());
}

static void test_two_tabs_do_not_share_generation_or_fade() {
    kf::Store s;
    s.refuse_close("a", true, 10.0, false);
    s.refuse_close("b", true, 10.1, false);
    CHECK(s.generation() == 2);
    s.refuse_close("a", true, 10.15, false);
    CHECK(s.generation() == 2);
    CHECK(s.mark("a", 10.38, false).lit);
    CHECK(!s.mark("a", 10.39, false).lit);
    CHECK(!s.mark("b", 10.39, false).lit && s.fading("b"));
    s.forget("a");
    CHECK(s.fading("b") && near(s.mark("b", 10.40, false).progress, 1.0f - kf::ease_in_out(0.06f / 0.12f)));
    s.refuse_close("c", true, 10.40, true);
    CHECK(s.mark("c", 10.40, true) == (kf::Mark{true, false, 1.0f}));
    CHECK(s.mark("b", 10.40, false).animated);
}

static void test_reduce_motion_is_read_at_mark_as_well_as_at_refusal() {
    kf::Store s;
    s.refuse_close("a", true, 10.0, false);
    CHECK(s.mark("a", 10.03, true) == (kf::Mark{true, false, 1.0f}));
    CHECK(near(s.mark("a", 10.03, false).progress, 0.125f));
    CHECK(s.mark("a", 10.24, true) == (kf::Mark{false, false, 0.0f}));
    CHECK(s.empty());
    s.refuse_close("b", true, 20.0, true);
    CHECK(s.mark("b", 20.3, false) == (kf::Mark{true, true, 1.0f}));
    CHECK(s.mark("b", 20.599, false).lit);
}

static void test_a_repeat_extends_the_deadline_without_a_new_transition() {
    kf::Store s;
    s.refuse_close("a", true, 10.0, false);
    s.refuse_close("a", true, 10.2, false);
    CHECK(s.generation() == 1);
    CHECK(s.mark("a", 10.2, false) == (kf::Mark{true, true, 1.0f}));
    CHECK(s.mark("a", 10.43, false).lit);
    CHECK(!s.mark("a", 10.44, false).lit && s.generation() == 2);
}

static void test_sweep_expires_without_a_mark_and_forget_clears() {
    kf::Store s;
    s.refuse_close("a", true, 10.0, false);
    s.refuse_close("b", true, 10.1, false);
    s.sweep(10.25);
    CHECK(!s.is_lit("a") && s.is_lit("b"));
    s.forget("b");
    CHECK(!s.is_lit("b") && s.mark("b", 10.25, false) == (kf::Mark{false, true, 0.0f}));
    CHECK(near(s.mark("a", 10.25, false).progress, 1.0f - kf::ease_in_out(0.01f / 0.12f)));
}

static void test_paint_values_follow_progress_only() {
    CHECK(near(kf::scale_for(kf::Mark{false, true, 0.0f}), 1.0f));
    CHECK(near(kf::scale_for(kf::Mark{true, true, 1.0f}), 1.6f));
    CHECK(near(kf::scale_for(kf::Mark{true, true, 0.5f}), 1.3f));
    CHECK(near(kf::opacity_for(kf::Mark{false, true, 0.0f}), 0.7f));
    CHECK(near(kf::opacity_for(kf::Mark{true, true, 1.0f}), 1.0f));
    CHECK(near(kf::ease_in_out(0.0f), 0.0f) && near(kf::ease_in_out(0.5f), 0.5f) && near(kf::ease_in_out(1.0f), 1.0f));
    CHECK(kf::ease_in_out(0.25f) < 0.25f && kf::ease_in_out(0.75f) > 0.75f);
    CHECK(near(kf::ease_in_out(-1.0f), 0.0f) && near(kf::ease_in_out(2.0f), 1.0f));
}

int main() {
    std::printf("=== test_tab_keep_flash ===\n");
    test_an_unpinned_tab_is_never_refused_or_lit();
    test_a_refused_pinned_tab_lights_for_the_hold_then_rests();
    test_reduce_motion_holds_longer_and_never_animates();
    test_a_relight_during_the_fade_continues_from_the_shown_progress();
    test_a_fade_cut_short_by_expiry_fades_from_where_it_was();
    test_a_relight_during_the_fade_clears_the_fade_and_sweep_retires_it();
    test_two_tabs_do_not_share_generation_or_fade();
    test_reduce_motion_is_read_at_mark_as_well_as_at_refusal();
    test_a_repeat_extends_the_deadline_without_a_new_transition();
    test_sweep_expires_without_a_mark_and_forget_clears();
    test_paint_values_follow_progress_only();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
