#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace hanabi::halt {

struct Observation {
    bool own_halted = false;
    bool own_mark = false;
    std::string contained_by;
    std::string reason;

    static Observation from_state(const std::string& self_id, bool halted,
                                  const std::string& halted_by,
                                  const std::string& halted_reason) {
        Observation o;
        o.own_halted = halted;
        o.reason = halted_reason;
        if (!halted_by.empty()) {
            if (halted_by == self_id) o.own_mark = true;
            else o.contained_by = halted_by;
        }
        return o;
    }
    bool contained() const { return !contained_by.empty(); }
    bool engaged() const { return own_halted || own_mark || contained(); }
};

enum class Intent { Halt, HaltSubtree, Resume };

inline const char* action_id(Intent i) {
    switch (i) {
        case Intent::Halt: return "halt";
        case Intent::HaltSubtree: return "halt_subtree";
        case Intent::Resume: return "resume";
    }
    return "";
}
inline const char* verb(Intent i) {
    switch (i) {
        case Intent::Halt:
        case Intent::HaltSubtree: return "halt";
        case Intent::Resume: return "resume";
    }
    return "";
}
inline bool wants_halted(Intent i) { return i != Intent::Resume; }
inline bool subtree(Intent i) { return i == Intent::HaltSubtree; }

struct Rows {
    bool halt = false;
    bool halt_subtree = false;
    bool resume = false;
    bool any() const { return halt || halt_subtree || resume; }
};

inline Rows rows_for(const Observation& o, bool capable, std::size_t children) {
    Rows r;
    if (!capable || o.contained()) return r;
    if (o.own_mark) {
        r.resume = true;
        return r;
    }
    r.resume = o.own_halted;
    r.halt = !o.own_halted;
    r.halt_subtree = children > 0;
    return r;
}

inline bool satisfied(Intent i, const Observation& o) {
    switch (i) {
        case Intent::Halt: return o.own_halted;
        case Intent::HaltSubtree: return o.own_halted && o.own_mark;
        case Intent::Resume: return !o.own_halted && !o.own_mark;
    }
    return false;
}

enum class Settle { Ignore, Echo, Refused };

inline Settle settle(const nlohmann::json& msg, int64_t boundary, Intent intent,
                     std::string* refusal, std::string* echoed_reason) {
    if (!msg.is_object()) return Settle::Ignore;
    const auto str = [](const nlohmann::json& j, const char* k) -> std::string {
        if (!j.is_object() || !j.contains(k) || !j[k].is_string()) return {};
        return j[k].get<std::string>();
    };
    const std::string type = str(msg, "type");
    if (type == "error") {
        if (refusal) *refusal = str(msg, "message").empty() ? std::string(verb(intent)) + " refused"
                                                            : str(msg, "message");
        return Settle::Refused;
    }
    if (type != "frame") return Settle::Ignore;
    if (str(msg, "frame") != "durable") return Settle::Ignore;
    int64_t seq = 0;
    if (msg.contains("seq") && msg["seq"].is_number_integer()) seq = msg["seq"].get<int64_t>();
    if (seq <= boundary) return Settle::Ignore;
    if (!msg.contains("event") || !msg["event"].is_object()) return Settle::Ignore;
    const nlohmann::json& e = msg["event"];
    const std::string want = wants_halted(intent) ? "session_halted" : "session_resumed";
    if (str(e, "type") != want) return Settle::Ignore;
    if (echoed_reason) *echoed_reason = str(e, "reason");
    return Settle::Echo;
}

enum class Status { Satisfied, Confirmed, Unconfirmed, Refused, Failed, Contained };

struct Outcome {
    Status status = Status::Failed;
    Intent intent = Intent::Halt;
    Observation observed;
    bool observed_fresh = false;
    bool mark_confirmed = false;
    std::string detail;

    bool flag_settled() const {
        return status == Status::Satisfied || status == Status::Confirmed;
    }
};

inline Outcome await_echo(
    const std::function<nlohmann::json(std::chrono::steady_clock::time_point)>& next,
    std::chrono::steady_clock::time_point deadline, int64_t boundary, Intent intent,
    const Observation& observed, const std::function<std::string()>& dry_note) {
    Outcome out;
    out.intent = intent;
    out.observed = observed;
    for (;;) {
        const nlohmann::json msg = next(deadline);
        if (msg.is_discarded()) {
            out.status = Status::Unconfirmed;
            out.detail = dry_note();
            return out;
        }
        std::string refusal, reason;
        switch (settle(msg, boundary, intent, &refusal, &reason)) {
            case Settle::Echo:
                out.status = Status::Confirmed;
                out.detail = reason;
                return out;
            case Settle::Refused:
                out.status = Status::Refused;
                out.detail = refusal;
                return out;
            case Settle::Ignore: break;
        }
    }
}

class Serializer {
public:
    struct Pending {
        Intent intent;
        std::uint64_t ticket;
    };

    std::optional<Pending> raise(Intent i) {
        ++raised_;
        if (in_flight_ || needs_observation_) {
            queued_ = i;
            return std::nullopt;
        }
        return start(i);
    }

    std::optional<Pending> settled(std::uint64_t ticket, Status status,
                                   const Observation& now, bool now_is_fresh = true) {
        if (!in_flight_ || in_flight_->ticket != ticket) {
            ++late_dropped_;
            return std::nullopt;
        }
        in_flight_.reset();
        if (now_is_fresh) last_observed_ = now;
        if (status == Status::Unconfirmed) {
            needs_observation_ = true;
            return std::nullopt;
        }
        return drain();
    }

    enum class Source { Hello, Frame };
    std::optional<Pending> observed(const Observation& now, Source source = Source::Hello) {
        if (source == Source::Frame) {
            if (last_observed_) last_observed_->own_halted = now.own_halted;
            else last_observed_ = now;
            return std::nullopt;
        }
        last_observed_ = now;
        needs_observation_ = false;
        if (in_flight_) return std::nullopt;
        return drain();
    }

    std::optional<Pending> retry(Intent i) {
        if (in_flight_ || queued_ || needs_observation_) return std::nullopt;
        if (last_observed_ && satisfied(i, *last_observed_)) return std::nullopt;
        return start(i);
    }

    bool in_flight() const { return in_flight_.has_value(); }
    std::optional<Intent> in_flight_intent() const {
        if (!in_flight_) return std::nullopt;
        return in_flight_->intent;
    }
    std::optional<Intent> queued() const { return queued_; }
    bool needs_observation() const { return needs_observation_; }
    std::uint64_t raised_count() const { return raised_; }
    std::uint64_t started_count() const { return started_; }
    std::uint64_t late_dropped_count() const { return late_dropped_; }

private:
    std::optional<Pending> start(Intent i) {
        ++started_;
        in_flight_ = Pending{i, ++ticket_};
        return in_flight_;
    }
    std::optional<Pending> drain() {
        if (!queued_) return std::nullopt;
        const Intent next = *queued_;
        queued_.reset();
        if (last_observed_ && satisfied(next, *last_observed_)) return std::nullopt;
        return start(next);
    }

    std::optional<Pending> in_flight_;
    std::optional<Intent> queued_;
    std::optional<Observation> last_observed_;
    bool needs_observation_ = false;
    std::uint64_t ticket_ = 0;
    std::uint64_t raised_ = 0;
    std::uint64_t started_ = 0;
    std::uint64_t late_dropped_ = 0;
};

}
