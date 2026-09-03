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

#include <array>
#include <optional>

#include <afterhours/src/core/key_codes.h>
#include <afterhours/src/plugins/input_system.h>

#include "shortcuts.h"

namespace hanabi::keys {

namespace ah = afterhours::keys;

inline constexpr int kLeftSuper = ah::LEFT_SUPER;
inline constexpr int kRightSuper = ah::RIGHT_SUPER;
inline constexpr int kEscape = ah::ESCAPE;
inline constexpr int kComma = ah::COMMA;
inline constexpr int kSlash = ah::SLASH;
// Cmd+\ splits the pane. The chord is what VS Code, Zed and Sublime all use
// for the same action, and it was the one obvious candidate this app had not
// already taken: Cmd + B , C , F , G , K , N , W , comma and slash are bound
// (shortcuts_system.h lists every one), and A / arrows / Backspace belong to
// text editing (input_mapping.h). Backslash was bound to nothing anywhere.
inline constexpr int kBackslash = ah::BACKSLASH;
inline constexpr int kB = ah::B;
inline constexpr int kC = ah::C;
inline constexpr int kF = ah::F;
inline constexpr int kG = ah::G;
inline constexpr int kK = ah::K;
inline constexpr int kN = ah::N;
inline constexpr int kV = ah::V;
inline constexpr int kW = ah::W;

// Tab: the one keystroke in this app whose whole purpose is to move focus, and
// so the one that brings the focus ring out (ui/focus_visible.h). The arrows
// are NOT here — they walk a caret or a list cursor, each of which draws its
// own indicator, and preload.cpp's arrows_tab = false keeps them off focus
// entirely.
inline constexpr int kTab = ah::TAB;

// Transcript navigation.
inline constexpr int kPageUp = ah::PAGE_UP;
inline constexpr int kPageDown = ah::PAGE_DOWN;
inline constexpr int kHome = ah::HOME;
inline constexpr int kEnd = ah::END;
inline constexpr int kUp = ah::UP;
inline constexpr int kDown = ah::DOWN;

// Text editing chords hanabi has to drive itself (text_edit_chords_system.h).
inline constexpr int kBackspace = ah::BACKSPACE;

// Opens the row a list's keyboard cursor is on.
inline constexpr int kEnter = ah::ENTER;

inline constexpr int kSpace = ah::SPACE;

inline bool pressed(int key) { return afterhours::input::is_key_pressed(key); }
inline bool down(int key) { return afterhours::input::is_key_down(key); }

// The Cmd modifier, either side.
inline bool cmd_down() { return down(kLeftSuper) || down(kRightSuper); }

// The Ctrl modifier, either side. Reached for only as an ALIAS of Cmd: on
// macOS Ctrl+Backspace and Ctrl+Arrow mean nothing, and the scripted-UI harness
// cannot hold Super at all -- its CMD+ prefix turns into Ctrl and its SUPER+
// prefix is dropped (afterhours_gaps.md #49, #256). So a chord that reads
// cmd_down() alone is unreachable from every test we can write. The same
// bargain the key table strikes with its Ctrl twins, made here for the chords
// that are read off the key state instead of through an action.
inline bool ctrl_down() {
    return down(ah::LEFT_CONTROL) || down(ah::RIGHT_CONTROL);
}

// Cmd, or the Ctrl that stands in for it. See ctrl_down.
inline bool cmd_or_ctrl_down() { return cmd_down() || ctrl_down(); }

// The Shift modifier, either side. Shift both turns a chord around (Cmd+Shift+G
// is Cmd+G backwards) and tells two chords on the same key apart (Cmd+F finds
// in this thread, Cmd+Shift+F searches all of them), so every chord site has to
// read it the same way it reads Cmd.
inline bool shift_down() {
    return down(ah::LEFT_SHIFT) || down(ah::RIGHT_SHIFT);
}

inline bool option_down() {
    return down(ah::LEFT_ALT) || down(ah::RIGHT_ALT);
}

inline std::uint8_t shortcut_modifiers() {
    std::uint8_t modifiers = 0;
    const bool command = cmd_down();
#ifdef AFTER_HOURS_ENABLE_E2E_TESTING
    const bool commandAlias = !command && ctrl_down();
#else
    const bool commandAlias = false;
#endif
    if (command || commandAlias)
        modifiers |= hanabi::shortcuts::CommandModifier;
    if (shift_down()) modifiers |= hanabi::shortcuts::ShiftModifier;
    if (option_down()) modifiers |= hanabi::shortcuts::OptionModifier;
    if (ctrl_down() && !commandAlias)
        modifiers |= hanabi::shortcuts::ControlModifier;
    return modifiers;
}

inline bool shortcut_pressed(hanabi::shortcuts::Shortcut shortcut) {
    return shortcut.modifiers == shortcut_modifiers() && pressed(shortcut.key);
}

inline std::optional<hanabi::shortcuts::Shortcut> capture_shortcut() {
    using namespace afterhours::keys;
    static constexpr std::array<int, 58> supported{{
        A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W,
        X, Y, Z, ZERO, ONE, TWO, THREE, FOUR, FIVE, SIX, SEVEN, EIGHT, NINE,
        APOSTROPHE, COMMA, MINUS, PERIOD, SLASH, SEMICOLON, EQUAL, LEFT_BRACKET,
        BACKSLASH, RIGHT_BRACKET, GRAVE, SPACE, TAB, ENTER, BACKSPACE, DELETE_KEY,
        LEFT, RIGHT, UP, DOWN, F1, F2,
    }};
    for (const int key : supported)
        if (pressed(key))
            return hanabi::shortcuts::Shortcut{key, shortcut_modifiers()};
    for (int key = F3; key <= F12; ++key)
        if (pressed(key))
            return hanabi::shortcuts::Shortcut{key, shortcut_modifiers()};
    return std::nullopt;
}

}  // namespace hanabi::keys
