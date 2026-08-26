#pragma once

// ---------------------------------------------------------------------------
// TEST-ONLY render hooks. Gated entirely behind the HANABI_TEST_HOVER env var.
//
// Motivation: the headless `--screenshot` capture path has NO mouse, so true
// hover states (a chat row revealing its star on hover, a hovered content tab)
// can never arise from mouse hit-testing during a headless render. To let the
// end-to-end screenshot harness (scripts/screens.sh) photograph those hover
// states, this hook lets a single named widget be FORCED into its hover branch
// for one capture.
//
// Contract / safety:
//   * force_hover(name) returns true ONLY when the process env var
//     HANABI_TEST_HOVER is set AND equals `name`. When the var is unset (every
//     normal windowed run, every ordinary screenshot, every test that doesn't
//     opt in) it is a hard no-op returning false — the render is byte-identical
//     to before this hook existed.
//   * The env var is read ONCE and cached (getenv on first call), so it costs
//     nothing per frame/per widget after the first lookup.
//   * It only ever turns a hover branch ON; it can never suppress a real
//     mouse-driven hover, and it never mutates any app or UI state.
// ---------------------------------------------------------------------------

#include <cstdlib>
#include <string>
#include <string_view>

namespace hanabi::test_hooks {

// The single widget name that should be forced into its hover branch, or empty
// if the hook is disabled. Read once from HANABI_TEST_HOVER on first use.
inline std::string_view forced_hover_name() {
    static const std::string value = [] {
        const char* v = std::getenv("HANABI_TEST_HOVER");
        return v ? std::string(v) : std::string();
    }();
    return value;
}

// True iff HANABI_TEST_HOVER is set and equals `name`. No-op (false) otherwise.
inline bool force_hover(std::string_view name) {
    std::string_view forced = forced_hover_name();
    return !forced.empty() && forced == name;
}

// HANABI_FIND_AUDIT=1 makes the find bar show the number of highlight bands
// the previous frame actually painted, next to its "N of M" tally. The two
// must agree — that is find's counting rule — and a scripted test can read a
// label but never a painted band, so without this the rule can only be argued
// for, not asserted. Same contract as force_hover: read once, and a hard
// no-op when unset.
inline bool find_audit() {
    static const bool on = [] {
        const char* v = std::getenv("HANABI_FIND_AUDIT");
        return v != nullptr && *v != '\0' && std::string_view(v) != "0";
    }();
    return on;
}

// HANABI_FIND_STEP=<±n> steps the find bar's current match n times (negative
// = backwards) on the first frame that has matches, standing in for the Cmd+G
// chord a script cannot press (afterhours_gaps.md #49). It feeds the same
// find_nav step the chord feeds — only the two key reads are left uncovered.
// Same contract as force_hover: read once, and a hard no-op when unset.
inline int find_step() {
    static const int n = [] {
        const char* v = std::getenv("HANABI_FIND_STEP");
        return (v != nullptr && *v != '\0') ? std::atoi(v) : 0;
    }();
    return n;
}

// HANABI_SNIPPET_AUDIT=1 makes the sidebar show how many highlight bands its
// search snippets painted on the previous frame. Two reasons it exists: a
// script can read a label and never a painted band, and find's tally must be
// unaffected by these bands — asserting both numbers in one run is the only
// way to hold that apart. Same contract as force_hover: read once, hard no-op
// when unset.
inline bool snippet_audit() {
    static const bool on = [] {
        const char* v = std::getenv("HANABI_SNIPPET_AUDIT");
        return v != nullptr && *v != '\0' && std::string_view(v) != "0";
    }();
    return on;
}

// HANABI_ROW_AUDIT=1 makes the sidebar print, in a corner label, how many
// session rows it actually RENDERED and how many matched.
//
// The two numbers being different is the whole of this branch's biggest fix,
// and neither of them is assertable otherwise: a script can read a label, and
// "the list is capped" is a statement about the rows that are NOT there. The
// "Show N more" row would say it, except that it rides at the bottom of two
// viewports by construction (fillCap = viewportRows * 2), so it is never on
// screen without scrolling and the scripted matcher only sees what is on
// screen. Same contract as the audits above: read once, hard no-op when unset.
inline bool row_audit() {
    static const bool on = [] {
        const char* v = std::getenv("HANABI_ROW_AUDIT");
        return v != nullptr && *v != '\0' && std::string_view(v) != "0";
    }();
    return on;
}

// HANABI_WIDGET_AUDIT=1 makes the sidebar print, in a corner label, how many
// widgets are STALE -- built by some earlier screen, never retired, and walked
// by every UI system on every frame since (afterhours_gaps.md #115).
//
// It is the number src/ui/widget_epoch.h exists to hold at zero, and nothing
// else can show it: a retired widget is not on screen, so the scripted matcher
// -- which only reads what rendered -- can never see one directly. What it CAN
// read is a label that counts them. Same contract as the audits above: read
// once, hard no-op when unset.
inline bool widget_audit() {
    static const bool on = [] {
        const char* v = std::getenv("HANABI_WIDGET_AUDIT");
        return v != nullptr && *v != '\0' && std::string_view(v) != "0";
    }();
    return on;
}

// HANABI_CARD_AUDIT=1 makes a digest view (Blocked / Review / Starred /
// Archived) print, in a corner label, how many cards it actually BUILT, how
// many sessions matched its predicate, and which row the build started at.
//
// The sidebar's equivalent is row_audit() above, and this exists for the same
// reason: "the list is windowed" is a statement about the cards that are NOT
// there, and no screenshot and no frame time can assert one. Cards built
// against cards matched is the property itself.
inline bool card_audit() {
    static const bool on = [] {
        const char* v = std::getenv("HANABI_CARD_AUDIT");
        return v != nullptr && *v != '\0' && std::string_view(v) != "0";
    }();
    return on;
}

// The last digest frame's card counts, for a gate to read.
//
// The on-screen label above is what a scripted UI test can assert, and it is
// the wrong instrument for a gate: it needs an env var that changes the
// layout to be set, and a gate that perturbs the thing it measures is a gate
// that measures itself. These three ints are written unconditionally -- three
// stores a frame -- and main.cpp prints them next to the FrameTiming line, so
// `built` against `matched` is available to a shell script with nothing
// enabled and nothing moved.
struct CardAuditCounts {
    int built = 0;
    int matched = 0;
    int first = 0;
};
inline CardAuditCounts& card_audit_counts() {
    static CardAuditCounts c;
    return c;
}

// HANABI_SYNTAX_AUDIT=1 makes a fenced code block print, in its language bar,
// how many coloured runs of each kind it actually handed to the renderer.
// Colour is the whole feature and the scripted harness cannot see a colour —
// `assert_ui` reads x/y/w/h/hidden/text and nothing else (afterhours_gaps.md
// #61) — so without this the only assertable thing is that the code is still
// on screen, which was true before the highlighter existed. Same contract as
// the two above: read once, hard no-op when unset.
inline bool syntax_audit() {
    static const bool on = [] {
        const char* v = std::getenv("HANABI_SYNTAX_AUDIT");
        return v != nullptr && *v != '\0' && std::string_view(v) != "0";
    }();
    return on;
}

// HANABI_FOCUS_AUDIT=1 makes the sidebar footer print whether the focus ring
// is currently painted ("ring on" / "ring off"), next to the version. The ring
// is the whole point of ui/focus_visible.h and `assert_ui` can read a label but
// never an outline (afterhours_gaps.md #61), so without this the rule "no ring
// until the keyboard is used" can only be argued for. The label reports
// afterhours' OWN theme.focus_ring_thickness — the number the renderer
// consumes — not hanabi's intent, so a build that never suppresses the ring
// fails the test rather than agreeing with itself. Same contract as the hooks
// above: read once, hard no-op when unset.
inline bool focus_audit() {
    static const bool on = [] {
        const char* v = std::getenv("HANABI_FOCUS_AUDIT");
        return v != nullptr && *v != '\0' && std::string_view(v) != "0";
    }();
    return on;
}

// HANABI_UI_SCALE=<float> multiplies every pixels() value in the Adaptive
// scaling cascade, the way a browser's Ctrl+/- does — so a 2360x1898 render at
// 2.0 lays out the SAME UI at twice the size rather than a thin sidebar in a
// big canvas. It exists for the visual-parity harness: the frozen references
// are Puffin captured at 2x on a retina panel and downsampled to 1x, so a 1x
// hanabi capture is scored against 2x-downsampled glyphs and the metric has an
// 8-12% floor in every text region that no design change can reach
// (docs/visual-parity/REFERENCE.md, "The score has a FLOOR").
//
// Clamped to afterhours' own [0.5, 3.0] (theme.h with_ui_scale). Same contract
// as the hooks above: read once, and a hard no-op when unset — an unset var
// returns exactly 1.0f and setup_app_state does not touch theme.ui_scale at
// all, so the scripted UI suite at 1100x760 renders byte-identically to a
// build without this hook.
inline float ui_scale() {
    static const float v = [] {
        const char* s = std::getenv("HANABI_UI_SCALE");
        if (s == nullptr || *s == '\0') return 1.0f;
        const float f = std::strtof(s, nullptr);
        if (f < 0.5f || f > 3.0f) return 1.0f;
        return f;
    }();
    return v;
}

// HANABI_TEST_FOCUS_COMPOSER=1 puts keyboard focus on the composer field for a
// capture, so 28_composer_focus_dark can photograph the caret and the focus
// ring. Setting focus is only half of it: ui/focus_visible.h keeps the ring off
// until Tab has been pressed (the :focus-visible rule), and a headless capture
// presses nothing — so the screen rendered byte-identical to
// 03_transcript_dark, a baseline claiming to watch a focus ring that was never
// in the frame. The hook therefore arms focus-visible as well, which is what a
// keyboard user reaching the field would have done. Same contract as the hooks
// above: read once, hard no-op when unset.
inline bool focus_composer() {
    static const bool on = [] {
        const char* v = std::getenv("HANABI_TEST_FOCUS_COMPOSER");
        return v != nullptr && *v != '\0' && std::string_view(v) != "0";
    }();
    return on;
}

}  // namespace hanabi::test_hooks
