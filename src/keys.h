#pragma once

// ---------------------------------------------------------------------------
// Keyboard reads for app shortcuts.
//
// This used to hand-write an #ifdef choosing between the e2e injector and the
// graphics layer. afterhours::input already IS that branch (input_system.h),
// and platform_test_input.h says so in its own header -- "this replaces the
// need for per-project backend-specific wrappers". So these now forward, which
// means the shipping build and the scripted-test build run the SAME path
// instead of two that can drift.
//
// The key codes come from afterhours::keys for the same reason: they were a
// second copy of core/key_codes.h, and two copies of the same numbers drift.
//
// What remains here is only the app's vocabulary: which chords hanabi binds,
// named so a shortcut site reads as its chord rather than as a magic number.
// ---------------------------------------------------------------------------

#include <afterhours/src/core/key_codes.h>
#include <afterhours/src/plugins/input_system.h>

namespace hanabi::keys {

namespace ah = afterhours::keys;

inline constexpr int kLeftSuper = ah::LEFT_SUPER;
inline constexpr int kRightSuper = ah::RIGHT_SUPER;
inline constexpr int kEscape = ah::ESCAPE;
inline constexpr int kComma = ah::COMMA;
inline constexpr int kSlash = ah::SLASH;
inline constexpr int kB = ah::B;
inline constexpr int kC = ah::C;
inline constexpr int kF = ah::F;
inline constexpr int kK = ah::K;
inline constexpr int kN = ah::N;
inline constexpr int kW = ah::W;

// Transcript navigation.
inline constexpr int kPageUp = ah::PAGE_UP;
inline constexpr int kPageDown = ah::PAGE_DOWN;
inline constexpr int kHome = ah::HOME;
inline constexpr int kEnd = ah::END;
inline constexpr int kUp = ah::UP;
inline constexpr int kDown = ah::DOWN;

// Opens the row a list's keyboard cursor is on.
inline constexpr int kEnter = ah::ENTER;

inline bool pressed(int key) { return afterhours::input::is_key_pressed(key); }
inline bool down(int key) { return afterhours::input::is_key_down(key); }

// The Cmd modifier, either side.
inline bool cmd_down() { return down(kLeftSuper) || down(kRightSuper); }

}  // namespace hanabi::keys
