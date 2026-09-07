#include "../../src/ui/anchored_surface.h"

#include <cassert>
#include <iostream>

int main() {
    using hanabi::surface::MenuMetrics;
    using hanabi::surface::Rect;
    using hanabi::surface::TipSide;
    using hanabi::surface::place_at;
    using hanabi::surface::place_submenu;
    using hanabi::surface::place_tooltip;

    MenuMetrics m;
    m.width = 184.0f;
    assert(m.height_for(0) == 36.0f);
    assert(m.height_for(7) == 28.0f + 36.0f * 7.0f + 8.0f);
    assert(m.row_width() == 176.0f);
    assert(m.row_x(100.0f) == 104.0f);
    assert(m.row_y(50.0f, 0) == 50.0f + 4.0f + 28.0f);
    assert(m.row_y(50.0f, 2) == 50.0f + 4.0f + 28.0f + 72.0f);


    const auto roomy = place_at(200.0f, 120.0f, 184.0f, 260.0f, 1100.0f, 760.0f);
    assert(roomy.x == 200.0f && roomy.y == 120.0f);
    assert(!roomy.moved_x && !roomy.moved_y);

    const auto offRight =
        place_at(1050.0f, 120.0f, 184.0f, 260.0f, 1100.0f, 760.0f);
    assert(offRight.x == 1100.0f - 184.0f - 4.0f);
    assert(offRight.moved_x && !offRight.moved_y);

    const auto offBottom =
        place_at(200.0f, 700.0f, 184.0f, 260.0f, 1100.0f, 760.0f);
    assert(offBottom.y == 760.0f - 260.0f - 4.0f);
    assert(offBottom.moved_y);

    const auto offTopLeft =
        place_at(-40.0f, -40.0f, 184.0f, 260.0f, 1100.0f, 760.0f);
    assert(offTopLeft.x == 4.0f && offTopLeft.y == 4.0f);

    const auto tooTall = place_at(10.0f, 10.0f, 184.0f, 900.0f, 1100.0f, 760.0f);
    assert(tooTall.y == 4.0f);

    const Rect parent{600.0f, 100.0f, 184.0f, 300.0f};
    const auto right =
        place_submenu(parent, 140.0f, 184.0f, 200.0f, 1100.0f, 760.0f);
    assert(right.x == 600.0f + 184.0f - 4.0f);
    assert(right.y == 140.0f);

    const Rect nearEdge{880.0f, 100.0f, 184.0f, 300.0f};
    const auto flipped =
        place_submenu(nearEdge, 140.0f, 184.0f, 200.0f, 1100.0f, 760.0f);
    assert(flipped.x == 880.0f + 4.0f - 184.0f);
    assert(flipped.moved_x);

    const Rect noRoomEitherSide{10.0f, 100.0f, 184.0f, 300.0f};
    const auto pinned =
        place_submenu(noRoomEitherSide, 140.0f, 184.0f, 200.0f, 240.0f, 760.0f);
    assert(pinned.x >= 4.0f);
    assert(pinned.x + 184.0f <= 240.0f - 4.0f + 0.001f);

    const Rect control{500.0f, 300.0f, 28.0f, 28.0f};
    const auto below = place_tooltip(control, 120.0f, 24.0f, 1100.0f, 760.0f);
    assert(below.side == TipSide::Below);
    assert(below.y == 300.0f + 28.0f + 6.0f);
    assert(below.x == 500.0f + (28.0f - 120.0f) * 0.5f);

    const Rect lowControl{500.0f, 726.0f, 28.0f, 28.0f};
    const auto above = place_tooltip(lowControl, 120.0f, 24.0f, 1100.0f, 760.0f);
    assert(above.side == TipSide::Above);
    assert(above.y == 726.0f - 6.0f - 24.0f);

    const Rect squeezed{500.0f, 10.0f, 28.0f, 28.0f};
    const auto stillBelow =
        place_tooltip(squeezed, 120.0f, 24.0f, 1100.0f, 60.0f);
    assert(stillBelow.side == TipSide::Below);

    const Rect leftEdge{2.0f, 300.0f, 28.0f, 28.0f};
    const auto clamped = place_tooltip(leftEdge, 120.0f, 24.0f, 1100.0f, 760.0f);
    assert(clamped.x == 4.0f);

    const Rect rightEdge{1080.0f, 300.0f, 28.0f, 28.0f};
    const auto clampedRight =
        place_tooltip(rightEdge, 120.0f, 24.0f, 1100.0f, 760.0f);
    assert(clampedRight.x == 1100.0f - 120.0f - 4.0f);

    std::cout << "anchored surface: ok\n";
    return 0;
}
