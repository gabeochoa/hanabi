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

// HANABI_SYNTAX_AUDIT=1 makes a fenced code block print, in its language bar,
// how many coloured runs of each kind it actually handed to the renderer.
// Colour is the whole feature and the scripted harness cannot see a colour —
// `assert_ui` reads x/y/w/h/hidden/text and nothing else (afterhours_gaps.md
// #58) — so without this the only assertable thing is that the code is still
// on screen, which was true before the highlighter existed. Same contract as
// the two above: read once, hard no-op when unset.
inline bool syntax_audit() {
    static const bool on = [] {
        const char* v = std::getenv("HANABI_SYNTAX_AUDIT");
        return v != nullptr && *v != '\0' && std::string_view(v) != "0";
    }();
    return on;
}

}  // namespace hanabi::test_hooks
