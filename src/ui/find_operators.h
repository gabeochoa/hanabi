#pragma once

// ---------------------------------------------------------------------------
// Operators for find-in-conversation: `backoff has:tool`, `retry is:user`,
// `deploy state:failed`.
//
// An operator narrows WHICH ROWS the plain text is searched in; it never
// changes what a match is. That split matters because find's counting rule is
// load-bearing: every match in the tally must be one find would paint if that
// message were on screen (tests/ui/find_counts_only_what_it_could_paint.e2e).
// Keeping the operator on the row side and the text on the match side means
// the filter can only ever remove a row from BOTH sides at once — the same
// predicate decides whether a row is counted and whether it is painted, so the
// two cannot drift.
//
// The tally is NOT a count of bands on screen, and never was: bands are
// painted only inside the virtualization window, so a thread longer than the
// screen paints a subset of what it counts. "3 of 47" means 47 in the thread,
// which is what it means in every other editor
// (tests/ui/find_counts_the_thread_not_the_window.e2e).
//
// Only rows the transcript can actually highlight are worth filtering, which
// is why the vocabulary looks the way it does. `is:tool` and `is:thinking` are
// deliberately absent: tool rows have no highlight path at all, and a thinking
// block is excluded from find outright — reasoning is the model talking to
// itself on the way to the answer, it arrives folded, and its body draws
// through a path that paints no bands (main_pane_system.h, collect_matches and
// paint_query_for skip it together). Either operator would be one that can
// only ever answer "no matches", which is exactly the kind of lying tally the
// counting rule exists to prevent. They land in the hint instead, which names
// the operators that do work.
//
// (This used to say a thinking block "is not preserved as a row". That stopped
// being true when reasoning became its own folded row; the operator is still
// absent, for the reason above rather than that one.)
// ---------------------------------------------------------------------------

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "../api/types.h"

namespace hanabi::find_ops {

// Shown under the find bar when a query names an operator we do not have.
inline constexpr const char* kHint =
    "Try: is:user, is:assistant, has:tool, state:failed";

enum class Filter {
    Role,       // the row's own role
    HasTool,    // the row's turn ran a tool
    ToolState,  // the row's turn ran a tool that ended in this state
};

struct Term {
    Filter kind = Filter::Role;
    api::Role role = api::Role::Assistant;
    std::string value;  // normalized tool state, for Filter::ToolState
};

struct Query {
    std::string text;          // what gets searched for and painted
    std::vector<Term> terms;   // ANDed together
    bool invalid = false;      // an is:/has:/state: we do not understand
};

inline char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

// The adapters disagree about vocabulary (the agentcloud one says "success",
// the generic http one passes the backend's word through), and the tool-row
// renderer already treats "error" as a failure — so normalize to the three
// states a person would type. A blank status means the backend said nothing,
// which is NOT a state: claiming it as "running" would invent a fact.
inline std::string tool_state_of(const api::Message& m) {
    std::string s;
    for (char c : m.tool_status) s.push_back(lower(c));
    if (s == "failed" || s == "error") return "failed";
    if (s == "completed" || s == "success" || s == "ok") return "completed";
    if (s == "running" || s == "in_progress") return "running";
    return "";
}

inline Query parse(const std::string& raw) {
    Query q;
    std::vector<std::string> plain;
    bool sawOperator = false;

    size_t i = 0;
    while (i < raw.size()) {
        while (i < raw.size() && (raw[i] == ' ' || raw[i] == '\t')) ++i;
        if (i >= raw.size()) break;
        size_t end = i;
        while (end < raw.size() && raw[end] != ' ' && raw[end] != '\t') ++end;
        const std::string token = raw.substr(i, end - i);
        i = end;

        std::string lowered;
        for (char c : token) lowered.push_back(lower(c));
        const size_t colon = lowered.find(':');
        const std::string key =
            (colon == std::string::npos) ? std::string() : lowered.substr(0, colon);
        const std::string val =
            (colon == std::string::npos) ? std::string() : lowered.substr(colon + 1);

        // Only these three keys are operators. Everything else keeps its colon
        // and stays plain text, so searching for a URL or a "foo: bar" label
        // still works.
        if (key != "is" && key != "has" && key != "state") {
            plain.push_back(token);
            continue;
        }
        sawOperator = true;
        if (key == "is" && val == "user") {
            q.terms.push_back(Term{Filter::Role, api::Role::User, ""});
        } else if (key == "is" && (val == "assistant" || val == "agent")) {
            q.terms.push_back(Term{Filter::Role, api::Role::Assistant, ""});
        } else if (key == "has" && val == "tool") {
            q.terms.push_back(Term{Filter::HasTool, api::Role::Assistant, ""});
        } else if (key == "state" &&
                   (val == "failed" || val == "error")) {
            q.terms.push_back(Term{Filter::ToolState, api::Role::Assistant,
                                   "failed"});
        } else if (key == "state" &&
                   (val == "completed" || val == "success" || val == "ok")) {
            q.terms.push_back(Term{Filter::ToolState, api::Role::Assistant,
                                   "completed"});
        } else if (key == "state" &&
                   (val == "running" || val == "in_progress")) {
            q.terms.push_back(Term{Filter::ToolState, api::Role::Assistant,
                                   "running"});
        } else {
            q.invalid = true;
        }
    }

    // A query with no operators is passed through byte for byte: rebuilding it
    // from tokens would collapse the runs of spaces someone is searching for.
    if (!sawOperator) {
        q.text = raw;
        return q;
    }
    for (const std::string& p : plain) {
        if (!q.text.empty()) q.text.push_back(' ');
        q.text += p;
    }
    return q;
}

inline bool active(const Query& q) { return !q.terms.empty() || q.invalid; }

// The contiguous run of assistant-side rows the transcript shows under one
// author heading — the same grouping main_pane_system's showAuthor uses. A
// user row is a turn of its own, so `has:tool` never matches one.
inline std::pair<size_t, size_t> turn_bounds(const api::Session& s, size_t i) {
    const auto assistant_side = [&](size_t k) {
        return s.messages[k].role == api::Role::Assistant ||
               s.messages[k].role == api::Role::Tool;
    };
    if (i >= s.messages.size() || !assistant_side(i)) return {i, i};
    size_t lo = i;
    while (lo > 0 && assistant_side(lo - 1)) --lo;
    size_t hi = i;
    while (hi + 1 < s.messages.size() && assistant_side(hi + 1)) ++hi;
    return {lo, hi};
}

inline bool row_matches(const api::Session& s, size_t i, const Query& q) {
    if (i >= s.messages.size()) return false;
    const api::Message& m = s.messages[i];
    for (const Term& t : q.terms) {
        switch (t.kind) {
            case Filter::Role:
                if (m.role != t.role) return false;
                break;
            case Filter::HasTool: {
                const auto [lo, hi] = turn_bounds(s, i);
                bool found = false;
                for (size_t k = lo; k <= hi && !found; ++k)
                    found = s.messages[k].role == api::Role::Tool;
                if (!found) return false;
                break;
            }
            case Filter::ToolState: {
                const auto [lo, hi] = turn_bounds(s, i);
                bool found = false;
                for (size_t k = lo; k <= hi && !found; ++k)
                    found = s.messages[k].role == api::Role::Tool &&
                            tool_state_of(s.messages[k]) == t.value;
                if (!found) return false;
                break;
            }
        }
    }
    return true;
}

}  // namespace hanabi::find_ops
