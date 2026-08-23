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

}  // namespace hanabi::test_hooks
