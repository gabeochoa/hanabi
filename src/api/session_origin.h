#pragma once

// ---------------------------------------------------------------------------
// WHERE a session was started, and whether a machine started it.
//
// `origin_application` is stamped once at create and never rewritten; every
// catalog row carries it. This header turns the wire name into the TERM a
// person reads (the section heading of Group by Origin, the word the web's
// `origin:` search takes), and answers the reference's automation question in
// the reference's order (its AutomationOrigin, a port of the web's
// automationOrigin.ts):
//
//   1. a reserved MACHINE-RUN NAME is automation-born whatever else says --
//      MEDI's `medi_mission_<32 lowercase hex>`, MATI's
//      `Automation TaskAttempt <n>`; anchored and exact, outer whitespace
//      ignored, never a prefix match;
//   2. a HUMAN-SET TITLE (`title_is_human`) exempts the row;
//   3. only then does the origin decide: the `metamate` term is the one
//      automation origin.
//
// A pin outranks all of it; that check lives with the caller that knows what
// is pinned. A row with no recorded origin is never an automation: hiding on
// a not-known is how a person's own session leaves their own list.
//
// Graphics-free: asserted in tests/unit/test_session_origin.cpp.
// ---------------------------------------------------------------------------

#include <string>
#include <string_view>

namespace api::origin {

// What a row with no recorded origin, or one the web app made, answers. Both
// were started by a person at a keyboard in this product, and nothing on the
// row tells them apart.
inline constexpr std::string_view kUnknownTerm = "web";

// The registered wire names mapped onto the term a person types; nullptr for
// a name this build has no word for (it then answers its own wire word, so an
// unregistered origin stays reachable under its own name rather than being
// folded into "web").
inline const char* key_of(std::string_view wire) {
    struct Pair {
        std::string_view wire;
        const char* key;
    };
    static constexpr Pair kKeys[] = {
        {"gchat", "chat"},
        {"slack", "slack"},
        {"workplace", "workplace"},
        {"agentcloud_cli", "cli"},
        {"metamate", "metamate"},
        // The canonical automation origin shares legacy metamate's key.
        {"metamate_automation", "metamate"},
        {"vc", "vc"},
        // Retired spellings of vc, still on rows from older journals.
        {"zoom", "vc"},
        {"google_meet", "vc"},
        {"glasses", "glasses"},
        {"planner", "planner"},
        {"phabricator", "phabricator"},
        {"aai_ideation_network", "aai_ideation_network"},
        {"meeting_intelligence", "meeting_intelligence"},
        {"tasks", "tasks"},
    };
    for (const Pair& p : kKeys)
        if (p.wire == wire) return p.key;
    return nullptr;
}

// The term a row answers for. Never empty: the facet is total, so every row
// lands in exactly one Origin section.
inline std::string term_of(std::string_view wire) {
    if (wire.empty() || wire == "agentcloud_web") return std::string(kUnknownTerm);
    if (const char* k = key_of(wire)) return k;
    return std::string(wire);
}

inline bool is_automation_origin(std::string_view wire) {
    return !wire.empty() && term_of(wire) == "metamate";
}

inline std::string_view trimmed(std::string_view s) {
    const auto ws = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    };
    while (!s.empty() && ws(s.front())) s.remove_prefix(1);
    while (!s.empty() && ws(s.back())) s.remove_suffix(1);
    return s;
}

// A reserved machine-run name, not a title a person chose. A naming rule for
// runs whose creator declared no origin, not evidence of who titled a row.
inline bool is_machine_run_name(std::string_view title) {
    const std::string_view t = trimmed(title);
    constexpr std::string_view kMission = "medi_mission_";
    constexpr std::string_view kAttempt = "Automation TaskAttempt ";
    if (t.substr(0, kMission.size()) == kMission) {
        const std::string_view rest = t.substr(kMission.size());
        if (rest.size() != 32) return false;
        for (char c : rest)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        return true;
    }
    if (t.substr(0, kAttempt.size()) == kAttempt) {
        const std::string_view rest = t.substr(kAttempt.size());
        if (rest.empty()) return false;
        for (char c : rest)
            if (c < '0' || c > '9') return false;
        return true;
    }
    return false;
}

inline bool is_automation_born(std::string_view origin, std::string_view title, bool titleIsHuman) {
    if (is_machine_run_name(title)) return true;
    if (titleIsHuman) return false;
    return is_automation_origin(origin);
}

}  // namespace api::origin
