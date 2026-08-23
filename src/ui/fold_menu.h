#pragma once

// ---------------------------------------------------------------------------
// How much of a tool call a thread shows by default.
//
// A tool row already arrived collapsed — `expandedPiles` starts empty, so the
// claim that "tool rows always expand" was never true of this code. What was
// missing is the thread-level control: a reader working through a tool-heavy
// thread had to open every pile by hand, one at a time, and nothing remembered
// that they had.
//
// Three modes, because there are exactly three answers a reader gives: never
// show me the output, always show it, or show it when it is short enough to be
// worth the space. The mode is per session and persisted (settings.json,
// `tool_fold`) — a thread you read expanded is expanded when you come back.
//
// This is a DEFAULT, not a lock: clicking a row still opens or closes that one
// row (`AppComponent::expandedPiles` / `collapsedPiles` hold those overrides),
// and picking a mode clears them, which is what makes "Fold all" and "Expand
// all" read as commands as well as settings.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hanabi::fold {

enum class Mode {
    Fold = 0,    // every tool row closed
    Expand = 1,  // every tool row open
    Auto = 2,    // open when the first captured result is short
};

// A result longer than this is a log, not a glance, so Auto leaves it folded.
// The number is the breakdown doc's (transcript.md, chunk 3).
inline constexpr size_t kAutoResultChars = 200;

struct Choice {
    Mode mode;
    std::string_view name;  // what the picker calls it
    std::string_view note;  // why you would pick it
    std::string_view chip;  // what the composer chip says once it is picked
};

inline const std::vector<Choice>& all() {
    static const std::vector<Choice> kChoices = {
        {Mode::Fold, "Fold all", "tool output stays closed", "Folded"},
        {Mode::Expand, "Expand all", "every tool row open", "Expanded"},
        {Mode::Auto, "Auto", "open short results only", "Auto"},
    };
    return kChoices;
}

// The mode a thread gets when nobody has chosen one. Folded: a tool-heavy
// thread is unreadable when every call dumps its output into the transcript.
inline constexpr Mode kDefault = Mode::Fold;

// A settings file written by a later build may hold a mode this one does not
// have; fall back rather than refusing to render.
inline Mode from_int(int v) {
    switch (v) {
        case 1: return Mode::Expand;
        case 2: return Mode::Auto;
        default: return Mode::Fold;
    }
}
inline int to_int(Mode m) { return static_cast<int>(m); }

inline std::string chip_label(Mode m) {
    for (const Choice& c : all())
        if (c.mode == m) return std::string(c.chip);
    return std::string(all().front().chip);
}

}  // namespace hanabi::fold
