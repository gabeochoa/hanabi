#pragma once

#include <algorithm>
#include <climits>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>

namespace hanabi::snooze_wake {

using Seconds = std::int64_t;
using Millis = std::int64_t;

enum class State { Active, Due, Message, Settled };

struct Verdict {
    State state = State::Active;
    std::optional<Millis> wake_at_ms;
    bool operator==(const Verdict&) const = default;
};

inline constexpr Verdict kStillParked{State::Active, std::nullopt};

inline constexpr Seconds kLargestSecondsInMillis = INT64_MAX / 1000;

inline Millis seconds_to_ms_saturating(Seconds s) {
    if (s < 0) return 0;
    if (s > kLargestSecondsInMillis) return INT64_MAX;
    return s * 1000;
}

inline std::optional<Millis> after_snooze_ms(std::optional<Seconds> snoozed_at,
                                             std::optional<Millis> event_ms) {
    if (!snoozed_at || !event_ms || *event_ms <= 0) return std::nullopt;
    if (*snoozed_at < 0 || *snoozed_at >= kLargestSecondsInMillis) return std::nullopt;
    const Millis provably_after = (*snoozed_at + 1) * 1000;
    return *event_ms >= provably_after ? event_ms : std::nullopt;
}

inline bool is_earlier(Millis candidate, std::optional<Millis> current) {
    return !current || candidate < *current;
}

inline Verdict verdict(Seconds snoozed_until, std::optional<Seconds> snoozed_at, Seconds now_sec,
                       std::optional<Millis> last_run_complete_ms,
                       std::optional<Millis> last_message_ms) {
    Verdict out = now_sec >= snoozed_until ? Verdict{State::Due, seconds_to_ms_saturating(snoozed_until)}
                                           : kStillParked;
    if (const auto message_at = after_snooze_ms(snoozed_at, last_message_ms);
        message_at && is_earlier(*message_at, out.wake_at_ms))
        out = Verdict{State::Message, message_at};
    if (const auto settled_at = after_snooze_ms(snoozed_at, last_run_complete_ms);
        settled_at && is_earlier(*settled_at, out.wake_at_ms))
        out = Verdict{State::Settled, settled_at};
    return out;
}

inline bool acknowledges_on_open(const Verdict& v) { return v.state != State::Active; }

inline std::optional<Seconds> due_at(Seconds snoozed_until, std::optional<Seconds> snoozed_at,
                                     Seconds now_sec,
                                     std::optional<Millis> last_run_complete_ms,
                                     std::optional<Millis> last_message_ms) {
    const Verdict v = verdict(snoozed_until, snoozed_at, now_sec, last_run_complete_ms,
                              last_message_ms);
    if (v.state != State::Active) return std::nullopt;
    return snoozed_until;
}

struct Partition {
    std::set<std::string> parked;
    std::map<std::string, Millis> wake_at_ms;
    bool operator==(const Partition&) const = default;
};

template <typename Map>
std::optional<std::int64_t> lookup(const Map& m, const std::string& key) {
    const auto it = m.find(key);
    if (it == m.end()) return std::nullopt;
    return it->second;
}

inline Partition partition(const std::map<std::string, Seconds>& snoozed_until,
                           const std::map<std::string, Seconds>& snoozed_at, Seconds now_sec,
                           const std::map<std::string, Millis>& last_run_complete_ms,
                           const std::map<std::string, Millis>& last_message_ms) {
    Partition out;
    for (const auto& [session_id, until] : snoozed_until) {
        const Verdict v = verdict(until, lookup(snoozed_at, session_id), now_sec,
                                  lookup(last_run_complete_ms, session_id),
                                  lookup(last_message_ms, session_id));
        if (v.wake_at_ms) out.wake_at_ms[session_id] = *v.wake_at_ms;
        else out.parked.insert(session_id);
    }
    return out;
}

inline std::optional<Millis> effective_attention_ms(std::optional<Millis> activity_ms,
                                                    std::optional<Millis> wake_at_ms) {
    if (!wake_at_ms) return activity_ms;
    if (!activity_ms) return wake_at_ms;
    return std::max(*activity_ms, *wake_at_ms);
}

}
