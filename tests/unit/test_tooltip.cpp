#include "../../src/ui/tooltip.h"

#include <cassert>
#include <iostream>

int main() {
    using hanabi::tip::Timer;
    using hanabi::tip::kRevealSeconds;

    Timer t;
    assert(!t.visible());

    t.hold("tab_close", 0.2f);
    assert(!t.visible());
    assert(t.held() > 0.19f && t.held() < 0.21f);

    t.hold("tab_close", 0.2f);
    assert(!t.visible());

    t.hold("tab_close", 0.2f);
    assert(t.visible());
    assert(t.shown() == "tab_close");

    t.hold("tab_pin", 0.2f);
    assert(!t.visible());
    assert(t.pending() == "tab_pin");

    t.hold("tab_pin", kRevealSeconds);
    assert(t.visible());

    t.interrupt();
    assert(!t.visible());
    assert(t.suppressed());

    t.hold("tab_pin", 10.0f);
    assert(!t.visible());

    t.leave();
    assert(!t.suppressed());
    t.hold("tab_pin", kRevealSeconds);
    assert(t.visible());

    t.hold("", 1.0f);
    assert(!t.visible());
    assert(t.pending().empty());

    Timer exact;
    exact.hold("row_star", kRevealSeconds);
    assert(exact.visible());

    Timer justUnder;
    justUnder.hold("row_star", kRevealSeconds - 0.001f);
    assert(!justUnder.visible());

    Timer forced;
    forced.force_show("composer_attach");
    assert(forced.visible());
    assert(forced.shown() == "composer_attach");

    assert(hanabi::tip::text_for("tab_close") == "Close this tab");
    assert(hanabi::tip::text_for("composer_send") == "Send this message");
    assert(hanabi::tip::text_for("row_menu").empty());
    assert(hanabi::tip::text_for("").empty());

    assert(hanabi::tip::width_for("abcd") ==
           hanabi::tip::kTipPadH * 2.0f + 4.0f * hanabi::tip::kTipCharW);

    std::cout << "tooltip timing: ok\n";
    return 0;
}
