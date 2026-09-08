#pragma once

// ---------------------------------------------------------------------------
// Where a bare keystroke and a pane click put the caret.
//
// Three entry points meant to land in one place -- the focused pane's composer
// -- and only one of them did. New Thread already routes through the focused
// pane (new_thread.h). A pane CLICK moved the focus edge and left the caret
// wherever it was, and a keystroke at a transcript went nowhere at all: the
// character queue is drained by whatever field has focus, and when none does,
// by nothing.
//
// THE RANKING IS NOT NEW. Everything the escape ladder puts above the composer
// (escape_system.h) -- a sheet, a menu, a popover, the find bar, a card holding
// the keyboard -- outranks it here too, and `typingLive` is that same question
// asked once (components.h composer_typing_live). A second opinion about who
// owns the keyboard is how two systems come to answer one keystroke.
//
// WHY A CLICK IS ANSWERED A FRAME LATE. A click that lands in a field inside
// the pane focuses that field during the same frame's build, and an offer made
// before the build cannot know that. So the click records a pending pane and
// the offer is made on the next frame, when `textFieldFocused` is the answer to
// "did the click land in a field" and the offer can be withdrawn.
//
// Pure, so the routing is assertable with no window and no ECS.
// ---------------------------------------------------------------------------

#include <string>

namespace ecs::model {

struct FocusRouting {
    bool typingLive = false;
    bool textFieldFocused = false;
    bool chatView = false;
    bool modifierHeld = false;
};

[[nodiscard]] inline bool pane_click_takes_caret(const FocusRouting& in) {
    return in.typingLive && in.chatView && !in.textFieldFocused;
}

// A chord is not typing: Cmd B folds the sidebar, and without this it would
// also leave a "b" in the draft.
[[nodiscard]] inline bool composer_takes_typing(const FocusRouting& in) {
    return pane_click_takes_caret(in) && !in.modifierHeld;
}

inline constexpr unsigned kSurfaceModalSheet = 1u << 0;
inline constexpr unsigned kSurfaceRecordingShortcut = 1u << 1;
inline constexpr unsigned kSurfaceSessionSearch = 1u << 2;
inline constexpr unsigned kSurfaceRowMenu = 1u << 3;
inline constexpr unsigned kSurfaceTabMenu = 1u << 4;
inline constexpr unsigned kSurfaceFind = 1u << 5;
inline constexpr unsigned kSurfaceAskFocused = 1u << 6;
inline constexpr unsigned kSurfaceAskWaiting = 1u << 7;

struct KeyboardClaim {
    unsigned issued = 0;
    unsigned surfaces = 0;
    unsigned surfaceClaimedAt = 0;
    unsigned composerClaimedAt = 0;
    bool caretInComposer = false;

    void observe(unsigned surfacesNow, bool caretInComposerNow,
                 bool claimedOnPurpose) {
        if ((surfacesNow & ~surfaces) != 0) surfaceClaimedAt = ++issued;
        if (caretInComposerNow && !caretInComposer && claimedOnPurpose)
            composerClaimedAt = ++issued;
        surfaces = surfacesNow;
        caretInComposer = caretInComposerNow;
    }
};

[[nodiscard]] inline bool composer_holds_keyboard(const KeyboardClaim& claim) {
    return claim.surfaces == 0 ||
           claim.composerClaimedAt > claim.surfaceClaimedAt;
}

[[nodiscard]] inline constexpr bool is_high_surrogate(int unit) {
    return unit >= 0xD800 && unit <= 0xDBFF;
}

[[nodiscard]] inline constexpr bool is_low_surrogate(int unit) {
    return unit >= 0xDC00 && unit <= 0xDFFF;
}

[[nodiscard]] inline constexpr bool is_surrogate(int unit) {
    return is_high_surrogate(unit) || is_low_surrogate(unit);
}

[[nodiscard]] inline bool typed_char_is_text(int codepoint) {
    return codepoint >= 32 && codepoint != 127 && !is_surrogate(codepoint);
}

inline void append_utf8(std::string& out, int codepoint) {
    if (codepoint < 0x80) {
        out += static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
        out += static_cast<char>(0xC0 | (codepoint >> 6));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        out += static_cast<char>(0xE0 | (codepoint >> 12));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (codepoint >> 18));
        out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
}

struct TypedRun {
    std::string text;
    int high = 0;

    void offer(int unit) {
        if (high != 0) {
            const int pending = high;
            high = 0;
            if (is_low_surrogate(unit)) {
                append_utf8(text, 0x10000 + ((pending - 0xD800) << 10) +
                                      (unit - 0xDC00));
                return;
            }
        }
        if (is_high_surrogate(unit)) {
            high = unit;
            return;
        }
        if (!typed_char_is_text(unit)) return;
        append_utf8(text, unit);
    }
};

}  // namespace ecs::model
