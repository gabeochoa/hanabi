#pragma once

// ---------------------------------------------------------------------------
// The effort ladder, and the one place that decides what a level is called on
// screen.
//
// WHERE THE LADDER COMES FROM. `EFFORT_MENU` and `DEFAULT_MENU_EFFORT` in
// fbcode/agentcloud/shared/inference/src/config.rs: five tokens, low through
// max, with "high" the level a session gets when nobody asks for one. The
// spellings are the server's own — `ThinkingEffort::parse` takes exactly
// these, and anything else is refused with an unrecognized-effort error.
//
// WHAT PICKING ONE DOES. It is stored locally (settings.json, `default_effort`)
// and it is the LAUNCH default: AppComponent::request_kickoff stamps it on a
// new session's kickoff request and the create command carries it as
// `options.llm.effort` (an empty knob is omitted). An existing session's
// effort is changed through the model panel's `patch_session_options`, never
// by this default. It is NOT pushed as a user preference: agentcloud's
// preferences are a WWW GraphQL mutation with snake_case `default_effort`,
// and hanabi's legacy camelCase `update_settings` body is another backend's
// contract that does not carry it.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hanabi::effort {

struct Level {
    // The token the server parses.
    std::string_view id;
    // What the picker calls it.
    std::string_view name;
    // Why you would pick it, in the picker's second line.
    std::string_view note;
};

inline const std::vector<Level>& all() {
    static const std::vector<Level> kLevels = {
        {"low", "Low", "quickest, least thinking"},
        {"medium", "Medium", "middle of the ladder"},
        {"high", "High", "the default"},
        {"xhigh", "XHigh", "slower, more thinking"},
        {"max", "Max", "as much as the model serves"},
    };
    return kLevels;
}

// The level a session gets when nobody asks for one.
inline std::string_view default_id() { return "high"; }

inline size_t index_of(std::string_view id) {
    const auto& list = all();
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].id == id) return i;
    return list.size();
}

// An unknown token is shown as itself rather than redrawn as the default: a
// settings file from a later build may name a level this one does not have.
inline std::string display_name(std::string_view id) {
    const size_t i = index_of(id);
    if (i < all().size()) return std::string(all()[i].name);
    return std::string(id);
}

}  // namespace hanabi::effort
