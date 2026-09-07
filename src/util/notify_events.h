#pragma once

// ---------------------------------------------------------------------------
// Which threads deserve a notification this refresh.
//
// The old rule was a COUNT: notify when the number of blocked threads goes up.
// That misses the case that matters most on a busy day -- one thread unblocks
// while another blocks, the count is unchanged, and the new one never tells
// you. It also cannot say WHICH thread, so the banner names whichever blocked
// thread happens to sort first.
//
// So the rule is per-thread instead: remember what each thread was doing last
// refresh, and report the transitions worth interrupting someone for. Kept as
// a pure function over ids and states -- no clock, no AppKit, no session type
// -- because the interesting cases (a thread appearing already-blocked, a
// thread that vanishes, the first refresh of all) are exactly the ones a
// wall-clock test cannot reach.
//
// The first observation of a thread NEVER notifies. Launching into a blocked
// inbox is not news, and a list refresh that returns a thread you have had for
// a week must not read as "this just happened".
// ---------------------------------------------------------------------------

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace hanabi::notify {

// What a thread is doing, reduced to the states a notification cares about.
enum class Activity {
    Other,     // running, idle, archived — nothing to say
    Blocked,   // waiting on the user
    Finished,  // reached a done/ready state
};

struct Event {
    enum class Kind { Blocked, Finished };
    Kind kind;
    std::string id;
    std::string title;
};

using Snapshot = std::map<std::string, Activity>;

// Transitions since `previous`. A thread absent from `previous` is new to us
// and is only recorded, never reported.
//
// `muted` is the set of threads the user has silenced on this machine. They are
// dropped HERE rather than at the banner: the caller still records them in the
// next snapshot, so a thread that blocks while muted is already accounted for
// and unmuting it later cannot fire a banner about something that happened
// hours ago.
// Which threads are somebody else's child. Membership comes from the session
// model's own parent link, never from a title: a thread called "sub-agent" is
// not one, and a real child whose title says nothing still is.
using Children = std::set<std::string>;

// What the reader asked to be told about. Defaults are the behaviour that
// shipped before there were switches, so an absent preference changes nothing.
struct Wants {
    bool subagents = false;  // sub-agent transitions raise a banner
};

inline std::vector<Event> transitions(
    const Snapshot& previous,
    const std::vector<std::pair<std::string, Activity>>& current,
    const std::map<std::string, std::string>& titles,
    const std::set<std::string>& muted = {},
    const Children& children = {},
    const Wants& wants = Wants{}) {
    std::vector<Event> out;
    for (const auto& [id, now] : current) {
        auto prev = previous.find(id);
        if (prev == previous.end()) continue;
        if (prev->second == now) continue;
        if (muted.count(id) != 0) continue;
        if (!wants.subagents && children.count(id) != 0) continue;

        auto title = titles.find(id);
        const std::string label = title == titles.end() ? std::string{}
                                                        : title->second;
        if (now == Activity::Blocked)
            out.push_back({Event::Kind::Blocked, id, label});
        else if (now == Activity::Finished)
            out.push_back({Event::Kind::Finished, id, label});
    }
    return out;
}

inline std::optional<Event> native_event(
    const Snapshot& previous,
    const std::vector<std::pair<std::string, Activity>>& current,
    const std::map<std::string, std::string>& titles,
    const std::set<std::string>& muted = {},
    const Children& children = {},
    const Wants& wants = Wants{}) {
    const auto events =
        transitions(previous, current, titles, muted, children, wants);
    if (events.empty()) return std::nullopt;
    for (const auto& event : events)
        if (event.kind == Event::Kind::Blocked) return event;
    return events.front();
}

// The chime is a cue, not a notification: a reader who switched banners off may
// still want to hear a run land. It answers to BOTH switches because a chime
// from an app that promised to stay quiet is the same interruption by another
// route -- and only to a run that FINISHED, which is what the cue means.
struct Cue {
    bool chime = false;
};

inline Cue cue_for(const std::optional<Event>& event, bool notifications_on,
                   bool chime_on) {
    if (!event.has_value()) return Cue{};
    if (!notifications_on || !chime_on) return Cue{};
    return Cue{event->kind == Event::Kind::Finished};
}

inline Snapshot snapshot(
    const std::vector<std::pair<std::string, Activity>>& current) {
    Snapshot s;
    for (const auto& [id, activity] : current) s[id] = activity;
    return s;
}

}  // namespace hanabi::notify
