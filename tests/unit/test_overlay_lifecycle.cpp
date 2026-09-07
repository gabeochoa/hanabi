#include "../../src/ui/overlay_lifecycle.h"

#include <cassert>
#include <iostream>

int main() {
    using hanabi::overlay::dismisses;
    using hanabi::overlay::inside;
    using hanabi::surface::Rect;

    const Rect panel{330.0f, 265.0f, 440.0f, 230.0f};

    assert(inside(panel, 400.0f, 300.0f));
    assert(inside(panel, 330.0f, 265.0f));
    assert(inside(panel, 770.0f, 495.0f));
    assert(!inside(panel, 329.0f, 300.0f));
    assert(!inside(panel, 400.0f, 496.0f));

    assert(dismisses(true, 100.0f, 100.0f, panel));
    assert(!dismisses(true, 400.0f, 300.0f, panel));
    assert(!dismisses(false, 100.0f, 100.0f, panel));
    assert(!dismisses(true, 331.0f, 266.0f, panel));

    std::cout << "overlay lifecycle: ok\n";
    return 0;
}
