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
    CHECK(s.mark("a", 10.599, true).lit);
    CHECK(s.mark("a", 10.6, true) == (kf::Mark{false, false, 0.0f}));
    CHECK(s.empty());
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
