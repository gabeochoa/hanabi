#include "../../src/ui/secondary_surface_geometry.h"

#include <cassert>
#include <iostream>

int main() {
    using hanabi::surface::centered;
    using hanabi::surface::top_centered;

    const auto ordinary = centered(1100.0f, 760.0f, 720.0f, 640.0f);
    assert(ordinary.x == 190.0f);
    assert(ordinary.y == 60.0f);
    assert(ordinary.width == 720.0f);
    assert(ordinary.height == 640.0f);

    const auto shortWindow = centered(520.0f, 260.0f, 720.0f, 640.0f);
    assert(shortWindow.x == 24.0f);
    assert(shortWindow.y == 24.0f);
    assert(shortWindow.width == 472.0f);
    assert(shortWindow.height == 212.0f);

    const auto top = top_centered(800.0f, 300.0f, 520.0f, 260.0f);
    assert(top.x == 140.0f);
    assert(top.y == 24.0f);
    assert(top.width == 520.0f);
    assert(top.height == 252.0f);

    std::cout << "secondary surface geometry: ok\n";
    return 0;
}
