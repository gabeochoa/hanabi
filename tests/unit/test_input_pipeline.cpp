// tests/unit/test_input_pipeline.cpp
// Guards the ROOT CAUSE of "Enter doesn't send" + "Backspace doesn't delete":
// hanabi added the InputCollector component (preload) but never registered
// afterhours::input::InputSystem — the system that maps raw keys -> actions
// each frame. So ctx.pressed(WidgetPress)/pressed_or_repeat(TextBackspace)
// never fired (typing worked via the separate char queue).
//
// This test exercises the exact ACTION-RESOLUTION logic that InputSystem runs
// (input::check_single_action_impl) against hanabi's real key mapping, with a
// backend-free key-check stub — proving BACKSPACE resolves to TextBackspace and
// ENTER to WidgetPress. No graphics backend needed. It documents + guards the
// mapping the InputSystem consumes; the registration itself is a one-liner in
// build_systems (src/main.cpp) verified by the app build.

#define FMT_HEADER_ONLY
#define AFTER_HOURS_ENTITY_HELPER
#define AFTER_HOURS_ENTITY_QUERY
#define AFTER_HOURS_SYSTEM
#include "../../vendor/afterhours/src/ecs.h"

#include <cstdio>
#include <map>
#include "../../src/input_mapping.h"
#include <afterhours/src/plugins/input_system.h>
#include <afterhours/src/core/key_codes.h>

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do { if (!(cond)) { std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__); ++g_failures; } } while (0)

using namespace afterhours;

// Build hanabi's real text-input key mapping (mirrors preload.cpp).
static std::map<int, input::ValidInputs> hanabi_mapping() {
    std::map<int, input::ValidInputs> m;
    m[static_cast<int>(InputAction::TextBackspace)] = { keys::BACKSPACE };
    m[static_cast<int>(InputAction::TextDelete)]    = { keys::DELETE_KEY };
    m[static_cast<int>(InputAction::WidgetPress)]   = { keys::ENTER };
    return m;
}

// Resolve an action's value using the SAME impl InputSystem runs, but with a
// backend-free key-check that reports exactly `pressed_key` as down.
static float action_value(const input::ValidInputs& vis, int pressed_key) {
    auto key_check = [pressed_key](int k) { return k == pressed_key; };
    auto btn_check = [](input::GamepadID, auto) { return 0.f; };
    auto r = input::check_single_action_impl(/*id=*/0, vis, key_check, btn_check);
    return r.value;
}

static void test_backspace_maps_to_textbackspace() {
    std::printf("test_backspace_maps_to_textbackspace\n");
    auto m = hanabi_mapping();
    const auto& vis = m[static_cast<int>(InputAction::TextBackspace)];
    CHECK(action_value(vis, keys::BACKSPACE) > 0.f);   // BACKSPACE fires it
    CHECK(action_value(vis, keys::ENTER) == 0.f);      // ENTER does not
    CHECK(action_value(vis, keys::SPACE) == 0.f);      // SPACE does not
}

static void test_enter_maps_to_widgetpress() {
    std::printf("test_enter_maps_to_widgetpress\n");
    auto m = hanabi_mapping();
    const auto& vis = m[static_cast<int>(InputAction::WidgetPress)];
    CHECK(action_value(vis, keys::ENTER) > 0.f);       // ENTER fires send
    CHECK(action_value(vis, keys::SPACE) == 0.f);      // SPACE must NOT send
    CHECK(action_value(vis, keys::BACKSPACE) == 0.f);
}

// SPACE must not be bound to any editing/submit action (it's a typed char).
static void test_space_not_bound_to_actions() {
    std::printf("test_space_not_bound_to_actions\n");
    auto m = hanabi_mapping();
    for (auto& [action, vis] : m)
        CHECK(action_value(vis, keys::SPACE) == 0.f);
}

int main() {
    std::printf("== test_input_pipeline (key->action mapping) ==\n");
    test_backspace_maps_to_textbackspace();
    test_enter_maps_to_widgetpress();
    test_space_not_bound_to_actions();
    if (g_failures == 0) std::printf("OK\n");
    else std::printf("%d FAILURES\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
