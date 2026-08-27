#include <cstdio>

#include "../../src/ecs/tab_colors.h"

static int failures = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__);          \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

static bool same(afterhours::Color a, afterhours::Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static void check_mode(theme::Mode mode) {
    theme::set_mode(mode);
    const auto strip = ecs::tab_colors::strip_bg();
    const auto inactive = ecs::tab_colors::tab_inactive();
    const auto active = ecs::tab_colors::tab_active();
    const auto hovered = ecs::tab_colors::tab_hover();
    const auto close = ecs::tab_colors::close_hover(active);

    CHECK(same(strip, theme::window_bg()));
    CHECK(same(inactive, strip));
    CHECK(!same(active, inactive));
    CHECK(!same(hovered, inactive));
    CHECK(!same(close, active));
    CHECK(same(ecs::tab_colors::tab_text_act(), theme::text_primary()));
    CHECK(ecs::tab_colors::pin_ink().a == 179);
    CHECK(!same(ecs::tab_colors::pin_ink(),
                ecs::tab_colors::tab_text_act()));
    CHECK(ecs::tab_colors::kTabCorner == theme::chrome::RADIUS);
    CHECK(ecs::tab_colors::kCloseBoxPx >= 14.0f);
}

int main() {
    check_mode(theme::Mode::Dark);
    check_mode(theme::Mode::Light);
    theme::set_mode(theme::Mode::Dark);
    if (failures != 0) {
        std::printf("FAILED (%d)\n", failures);
        return 1;
    }
    std::puts("tab state colors: PASS");
    return 0;
}
