#include "../../src/ui/control_state.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string_view>

using hanabi::control::Phase;
using hanabi::control::State;

static int failures = 0;

static void expect(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

static bool same(theme::Color a, theme::Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static double channel(unsigned char value) {
    const double v = static_cast<double>(value) / 255.0;
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

static double luminance(theme::Color c) {
    return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) +
           0.0722 * channel(c.b);
}

static void check_mode(const char* label) {
    const theme::Color base = theme::panel_bg();

    State normal;
    State hovered;
    hovered.hovered = true;
    State pressed;
    pressed.pressed = true;
    pressed.hovered = true;
    State disabled;
    disabled.disabled = true;
    disabled.hovered = true;
    disabled.pressed = true;
    State selected;
    selected.selected = true;

    expect(normal.phase() == Phase::Normal, "normal phase");
    expect(hovered.phase() == Phase::Hover, "hover phase");
    expect(pressed.phase() == Phase::Press, "press outranks hover");
    expect(disabled.phase() == Phase::Disabled, "disabled outranks everything");

    const theme::Color restFill = hanabi::control::fill(normal, base);
    const theme::Color hoverFill = hanabi::control::fill(hovered, base);
    const theme::Color pressFill = hanabi::control::fill(pressed, base);
    const theme::Color disabledFill = hanabi::control::fill(disabled, base);
    const theme::Color selectedFill = hanabi::control::fill(selected, base);

    expect(same(restFill, base), "resting fill is the control's own");
    expect(!same(hoverFill, restFill), "hover differs from rest");
    expect(!same(pressFill, hoverFill), "press differs from hover");
    expect(same(disabledFill, theme::disabled_bg()), "disabled fill");
    expect(same(selectedFill, theme::selected_bg()), "selected fill");

    const double rest = luminance(restFill);
    const double hover = luminance(hoverFill);
    const double press = luminance(pressFill);
    const bool dark = theme::mode() == theme::Mode::Dark;
    if (dark) {
        expect(hover > rest, "dark hover lifts");
        expect(press > hover, "dark press lifts further");
    } else {
        expect(hover < rest, "light hover deepens");
        expect(press < hover, "light press deepens further");
    }
    std::printf("%s rest %.4f hover %.4f press %.4f\n", label, rest, hover,
                press);

    expect(same(hanabi::control::ink(disabled), theme::disabled_text()),
           "disabled ink");
    expect(same(hanabi::control::ink(normal), theme::text_secondary()),
           "resting ink");
    expect(same(hanabi::control::ink(hovered), theme::text_primary()),
           "hover ink");

    const theme::Color pressOnSelected =
        hanabi::control::fill(State{true, true, false, true}, base);
    expect(!same(pressOnSelected, theme::selected_bg()),
           "a selected row still shows a press");
}

int main() {
    expect(hanabi::control::kMinHitTarget == 28.0f, "hit target is 28");
    expect(hanabi::control::kMinHitTarget == theme::chrome::HIT,
           "the shared floor is theme::chrome::HIT");

    expect(hanabi::control::meets_hit_target(57.0f, 28.0f), "57x28 passes");
    expect(hanabi::control::meets_hit_target(28.0f, 400.0f), "28x400 passes");
    expect(!hanabi::control::meets_hit_target(28.0f, 18.0f), "28x18 fails");
    expect(!hanabi::control::meets_hit_target(10.0f, 40.0f), "10x40 fails");

    theme::set_mode(theme::Mode::Dark);
    check_mode("dark");
    theme::set_mode(theme::Mode::Light);
    check_mode("light");
    theme::set_mode(theme::Mode::Dark);


    const auto slop = hanabi::control::grow_to_target(100.0f, 50.0f, 14.0f,
                                                      14.0f);
    expect(slop.width == 28.0f && slop.height == 28.0f, "slop grows to 28");
    expect(slop.x == 93.0f && slop.y == 43.0f, "slop stays centred");
    const auto already =
        hanabi::control::grow_to_target(10.0f, 10.0f, 40.0f, 30.0f);
    expect(already.width == 40.0f && already.height == 30.0f,
           "a big control is not shrunk");
    expect(already.x == 10.0f && already.y == 10.0f, "a big control is not moved");

    theme::set_mode(theme::Mode::Dark);
    const theme::Color transparent{0, 0, 0, 0};
    expect(hanabi::control::press_over(theme::panel_bg()).a == 255,
           "a press over an opaque backdrop is opaque");
    expect(!same(hanabi::control::press_over(theme::panel_bg()),
                 theme::panel_bg()),
           "a press over the panel differs from the panel");
    (void)transparent;

    const theme::Color clear{0, 0, 0, 0};
    expect(same(hanabi::control::effective_backdrop(theme::panel_bg(),
                                                    theme::window_bg()),
                theme::panel_bg()),
           "an opaque control keeps its own fill as the backdrop");
    expect(same(hanabi::control::effective_backdrop(clear, theme::panel_bg()),
                theme::panel_bg()),
           "a transparent control borrows the surface it sits on");
    expect(hanabi::control::press_over(
               hanabi::control::effective_backdrop(clear, theme::panel_bg()))
                   .a == 255,
           "a transparent control's press is opaque, not a ghost");
    expect(!same(hanabi::control::press_over(clear),
                 hanabi::control::press_over(
                     hanabi::control::effective_backdrop(clear,
                                                         theme::panel_bg()))),
           "pressing alpha-zero directly is not the same colour");

    State disabledHover;
    disabledHover.disabled = true;
    disabledHover.hovered = true;
    expect(same(hanabi::control::hover_fill(disabledHover, theme::panel_bg()),
                theme::disabled_bg()),
           "a disabled control does not light up on hover");
    State plain;
    expect(!same(hanabi::control::hover_fill(plain, theme::panel_bg()),
                 theme::panel_bg()),
           "hover_fill lifts a resting control");

    if (failures != 0) {
        std::printf("control state: %d FAILURES\n", failures);
        return 1;
    }
    std::printf("control state: ok\n");
    return 0;
}
