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

}  // namespace hanabi::test_hooks
