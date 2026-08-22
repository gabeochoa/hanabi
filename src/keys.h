#pragma once

// ---------------------------------------------------------------------------
// Keyboard reads for app shortcuts.
//
// The app's shortcuts (Cmd+B, Cmd+,, Cmd+W, Cmd+/, Esc) were reading the
// graphics layer directly — afterhours::graphics::is_key_pressed. That works,
// but the e2e input injector cannot reach it: the testing hooks are wired into
// the INPUT plugin (afterhours::input::*, which routes through
// testing::test_input when AFTER_HOURS_ENABLE_E2E_TESTING is on), while the
// graphics key API has no such branch. So a synthetic keypress moved widget
// focus and typed into fields, but no scripted test could press Cmd+B or Esc —
// the whole shortcut surface was untestable (afterhours_gaps.md #50).
//
// These two functions are the one place that decides where a key read comes
// from: the test-aware path in the scripted-UI build, the graphics layer in
// every shipping build, where this compiles to exactly the call it replaced.
// ---------------------------------------------------------------------------

#include "rl.h"

#ifdef AFTER_HOURS_ENABLE_E2E_TESTING
#include "../vendor/afterhours/src/plugins/e2e_testing/platform_test_input.h"
#endif

namespace hanabi::keys {

// Key codes the app binds. Named so a shortcut site reads as its chord rather
// than as a pair of magic numbers.
inline constexpr int kLeftSuper = 343;
inline constexpr int kRightSuper = 347;
inline constexpr int kEscape = 256;
inline constexpr int kComma = 44;
inline constexpr int kSlash = 47;
inline constexpr int kB = 66;
inline constexpr int kC = 67;
inline constexpr int kF = 70;
inline constexpr int kN = 78;
inline constexpr int kW = 87;

inline bool pressed(int key) {
#ifdef AFTER_HOURS_ENABLE_E2E_TESTING
    return afterhours::testing::platform_input::is_key_pressed(key);
#else
    return afterhours::graphics::is_key_pressed(key);
#endif
}

inline bool down(int key) {
#ifdef AFTER_HOURS_ENABLE_E2E_TESTING
    return afterhours::testing::platform_input::is_key_down(key);
#else
    return afterhours::graphics::is_key_down(key);
#endif
}

// The Cmd modifier, either side.
inline bool cmd_down() { return down(kLeftSuper) || down(kRightSuper); }

}  // namespace hanabi::keys
