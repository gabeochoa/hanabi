#pragma once
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace hanabi::bulk {

struct Action {
    enum class Kind { Archive, Unarchive, Move, MarkRead, MarkUnread };
    Kind kind = Kind::Archive;
    std::string folder;  // Move: the folder id, empty = out of every folder

    static Action archive() { return {Kind::Archive, {}}; }
    static Action unarchive() { return {Kind::Unarchive, {}}; }
    static Action move(std::string folderId) { return {Kind::Move, std::move(folderId)}; }
    static Action mark_read() { return {Kind::MarkRead, {}}; }
    static Action mark_unread() { return {Kind::MarkUnread, {}}; }
    friend bool operator==(const Action&, const Action&) = default;
};

struct Outcome {
    enum class Kind { Unchanged, Succeeded, Failed, Unknown };
    Kind kind = Kind::Unchanged;
    std::string why;

    static Outcome succeeded() { return {Kind::Succeeded, {}}; }
    static Outcome unchanged() { return {Kind::Unchanged, {}}; }
    static Outcome failed(std::string why) { return {Kind::Failed, std::move(why)}; }
    static Outcome unknown(std::string why) { return {Kind::Unknown, std::move(why)}; }
    bool leaves_selection() const { return kind == Kind::Succeeded || kind == Kind::Unchanged; }
    bool is_unknown() const { return kind == Kind::Unknown; }
    bool is_failed() const { return kind == Kind::Failed; }
    int severity() const { return static_cast<int>(kind); }
    friend bool operator==(const Outcome&, const Outcome&) = default;
};

enum class Phase { Idle, Running, Reconciling };

enum class Gesture { Toggle, Extend };

inline Gesture gesture(bool shiftHeld) { return shiftHeld ? Gesture::Extend : Gesture::Toggle; }

using Selected = std::set<std::string>;
using Outcomes = std::map<std::string, Outcome>;

inline std::vector<std::string> range(const std::string& anchor, const std::string& clicked,
                                      const std::vector<std::string>& visible) {
    const auto end = std::find(visible.begin(), visible.end(), clicked);
    if (end == visible.end()) return {};
    const auto start = anchor.empty() ? visible.end() : std::find(visible.begin(), visible.end(), anchor);
    if (start == visible.end()) return {clicked};
    const auto lo = std::min(start, end);
    const auto hi = std::max(start, end);
    return std::vector<std::string>(lo, hi + 1);
}

struct Applied {
    Selected selected;
    std::string anchor;
};

inline Applied apply(Gesture g, const std::string& clicked, Selected selected, const std::string& anchor,
                     const std::vector<std::string>& visible) {
    switch (g) {
        case Gesture::Toggle: {
            if (!selected.erase(clicked)) selected.insert(clicked);
            return {std::move(selected), clicked};
        }
        case Gesture::Extend: {
            const std::vector<std::string> run = range(anchor, clicked, visible);
            if (run.empty()) return {std::move(selected), anchor};
            selected.insert(run.begin(), run.end());
            const bool anchorVisible =
                !anchor.empty() && std::find(visible.begin(), visible.end(), anchor) != visible.end();
            return {std::move(selected), anchorVisible ? anchor : clicked};
        }
    }
    return {std::move(selected), anchor};
}

inline std::vector<std::string> actionable(const Selected& selected, const std::vector<std::string>& visible) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const std::string& id : visible)
        if (selected.count(id) && seen.insert(id).second) out.push_back(id);
    return out;
}

inline Selected remaining(const Selected& selected, const Outcomes& outcomes) {
    Selected out;
    for (const std::string& id : selected) {
        const auto it = outcomes.find(id);
        if (it == outcomes.end() || !it->second.leaves_selection()) out.insert(id);
    }
    return out;
}

inline Selected unresolved(const Outcomes& outcomes, const Selected& selected) {
    Selected out;
    for (const std::string& id : selected) {
        const auto it = outcomes.find(id);
        if (it != outcomes.end() && it->second.is_unknown()) out.insert(id);
    }
    return out;
}

inline Outcome worst(const std::vector<Outcome>& outcomes) {
    Outcome w = Outcome::unchanged();
    for (const Outcome& o : outcomes)
        if (o.severity() > w.severity()) w = o;
    return w;
}

struct Tally {
    int done = 0;
    int failed = 0;
    int unknown = 0;
    friend bool operator==(const Tally&, const Tally&) = default;
};

inline Tally tally(const Outcomes& outcomes, const Selected& selected) {
    Tally t;
    for (const auto& [id, o] : outcomes) {
        switch (o.kind) {
            case Outcome::Kind::Succeeded:
            case Outcome::Kind::Unchanged: ++t.done; break;
            case Outcome::Kind::Failed: if (selected.count(id)) ++t.failed; break;
            case Outcome::Kind::Unknown: if (selected.count(id)) ++t.unknown; break;
        }
    }
    return t;
}

inline std::string count_label(int count) { return std::to_string(count) + " selected"; }

inline std::string detail(int selected, const Tally& t, Phase phase) {
    switch (phase) {
        case Phase::Running: return count_label(selected) + " \xc2\xb7 working\xe2\x80\xa6";
        case Phase::Reconciling: return count_label(selected) + " \xc2\xb7 checking\xe2\x80\xa6";
        case Phase::Idle: break;
    }
    std::string out = count_label(selected);
    if (t.failed > 0) out += " \xc2\xb7 " + std::to_string(t.failed) + " failed";
    if (t.unknown > 0) out += " \xc2\xb7 " + std::to_string(t.unknown) + " unconfirmed";
    return out;
}

inline std::string label(int selected, const Tally& t, Phase phase) {
    switch (phase) {
        case Phase::Running: return "Working\xe2\x80\xa6";
        case Phase::Reconciling: return "Checking\xe2\x80\xa6";
        case Phase::Idle: break;
    }
    if (t.unknown > 0) return std::to_string(t.unknown) + " unconfirmed";
    if (t.failed > 0) return std::to_string(t.failed) + " failed";
    return count_label(selected);
}

inline std::string row_note(const Outcome* o) {
    if (o == nullptr) return {};
    switch (o->kind) {
        case Outcome::Kind::Failed: return "Not done: " + o->why;
        case Outcome::Kind::Unknown: return "Unconfirmed: " + o->why + ". Check again before retrying.";
        case Outcome::Kind::Succeeded:
        case Outcome::Kind::Unchanged: return {};
    }
    return {};
}

inline std::string verb(const Action& a) {
    switch (a.kind) {
        case Action::Kind::Archive: return "Archive";
        case Action::Kind::Unarchive: return "Unarchive";
        case Action::Kind::Move: return "Move to folder";
        case Action::Kind::MarkRead: return "Mark read";
        case Action::Kind::MarkUnread: return "Mark unread";
    }
    return {};
}

inline std::string archive_hint(int targets) {
    return targets == 1 ? std::string("Archive 1 thread")
                        : "Archive " + std::to_string(targets) + " threads, sub-agents included";
}

inline std::string archived_toast(int count) {
    return count == 1 ? std::string("1 thread archived") : std::to_string(count) + " threads archived";
}

inline bool blocks_actions(Phase phase, const Outcomes& outcomes, const Selected& selected) {
    return phase != Phase::Idle || !unresolved(outcomes, selected).empty();
}

}  // namespace hanabi::bulk
