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

// What the server lets a client observe about a session's halt state, from
// one Hello (or the folded stream). Three facts, never collapsed into one:
//   own_halted    the session's OWN journal-folded flag (`state.halted`);
//                 moves on `session_halted` / `session_resumed` frames.
//   own_mark      the session's OWN subtree containment mark: the Hello's
//                 `halted_by.by` names THIS session. Serve-time only -- no
//                 frame ever carries it; only a fresh Hello does.
//   contained_by  an ANCESTOR's mark holds this session shut: `halted_by.by`
//                 names another session. Empty when none.
// A contained session may or may not also be own-halted.
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

// The three intents a menu can raise. Directional by construction: a stale
// pick of the wrong one is refused by the snapshot rule, never inverted.
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

// Which rows a menu offers, given what is observed. `capable` is the
// `halt_v1` advert on this session's attach -- the server pushes it only for
// the owner, so it is capability AND permission. `children` is the live
// sub-agent count.
//
//   contained by an ancestor  -> nothing: a Resume here would clear only this
//                                child's own state and leave the containment
//                                standing, with an echo that says success.
//   not halted, no own mark   -> Halt (+ Halt with Sub-agents when children)
//   halted alone, no own mark -> Resume, AND Halt with Sub-agents when
//                                children: a root halted alone must still be
//                                able to contain its tree.
//   own mark                  -> Resume only.
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

// Already satisfied: the state the intent asks for is what is observed. A
// send in this state journals a converging duplicate and echoes -- harmless
// to the server, but the reader asked for a state, not a line.
inline bool satisfied(Intent i, const Observation& o) {
    switch (i) {
        case Intent::Halt: return o.own_halted;
        case Intent::HaltSubtree: return o.own_halted && o.own_mark;
        case Intent::Resume: return !o.own_halted && !o.own_mark;
    }
    return false;
}

// The frame classifier over one socket message, after the send.
//   Ignore   not a durable frame, or at/below the Hello boundary (replay of
//            history the snapshot already folded), or the other verb's echo.
//   Echo     a durable `session_halted` (for a halt) / `session_resumed`
//            (for a resume) with seq > boundary: the OWN FLAG is proven.
//   Refused  a typed `error` on this connection: the one outstanding command
//            was refused, message verbatim.
// An echo never proves a MARK (there is no frame for it); see `Outcome`.
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

// What one attempt observed and proved.
//   Satisfied    nothing was sent: the Hello already showed the wanted state.
//   Confirmed    sent; the flag's echo arrived (seq > boundary). For
//                Halt/Resume that is the whole ask. For HaltSubtree the
//                MARK is still `mark_confirmed=false` until a fresh Hello
//                shows it -- the caller re-observes.
//   Unconfirmed  sent; the source ran dry (timeout / disconnect) before an
//                echo. Success is silent server-side, so the write MAY have
//                committed: the only safe next step is to observe again.
//   Refused      typed refusal, message verbatim. Nothing was written.
//   Failed       transport / attach / capability failure before the send.
//   Contained    nothing was sent: the Hello shows an ANCESTOR's mark holding
//                this session, and a resume here would clear only this
//                session's own state and leave the containment standing. Not
//                an error of the reader's -- the state to name is the root's.
enum class Status { Satisfied, Confirmed, Unconfirmed, Refused, Failed, Contained };

struct Outcome {
    Status status = Status::Failed;
    Intent intent = Intent::Halt;
    Observation observed;   // the Hello's state, before the send
    // True once a Hello was actually read: `observed` is then a fresh
    // attach's word and may be adopted. A failure before the attach carries
    // no observation, and adopting the default would CLEAR a real halt.
    bool observed_fresh = false;
    bool mark_confirmed = false;  // true only when a fresh Hello proved the mark
    std::string detail;     // refusal / failure text, verbatim

    bool flag_settled() const {
        return status == Status::Satisfied || status == Status::Confirmed;
    }
};

// The production wait, over any message source (the socket queue in
// production, a scripted sequence in tests). The Hello has been read
// (`boundary`) and the command sent. Reads until the echo, a refusal, or
// the source runs dry -- which is UNKNOWN, not failure.
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

// Per-session serializer for halt-state WRITES from THIS client.
//
//   * at most one write in flight per session;
//   * a new intent while one is in flight is QUEUED (one slot), and the slot
//     holds only the NEWEST intent -- an opposite intent replaces, a same-kind
//     intent coalesces (attempts are counted before the dedupe);
//   * when the in-flight write settles, the queued intent is handed back to
//     the caller to re-evaluate against the SETTLED observation (satisfied
//     -> dropped, else sent) -- never re-sent from a stale payload;
//   * an Unconfirmed settle does NOT free the session for an opposite send:
//     the unanswered write may have committed. `needs_observation` is set
//     and the caller must observe (a fresh Hello) and clear it before the
//     next write goes out;
//   * each in-flight write carries a ticket; a settle for a ticket that is
//     not the current one is a late answer from an older attempt and is
//     dropped.
class Serializer {
public:
    struct Pending {
        Intent intent;
        std::uint64_t ticket;
    };

    // Raise an intent. Returns the write to issue NOW, or nothing (queued, or
    // waiting on an observation).
    std::optional<Pending> raise(Intent i) {
        ++raised_;
        if (in_flight_ || needs_observation_) {
            queued_ = i;  // newest wins; same kind coalesces
            return std::nullopt;
        }
        return start(i);
    }

    // The in-flight write settled. Returns the next write to issue, if any.
    // `settled_status` decides whether the session is free.
    std::optional<Pending> settled(std::uint64_t ticket, Status status,
                                   const Observation& now, bool now_is_fresh = true) {
        if (!in_flight_ || in_flight_->ticket != ticket) {
            ++late_dropped_;
            return std::nullopt;  // an older attempt's late answer
        }
        in_flight_.reset();
        if (now_is_fresh) last_observed_ = now;
        if (status == Status::Unconfirmed) {
            needs_observation_ = true;
            return std::nullopt;
        }
        return drain();
    }

    // An observation landed. Two sources, kept apart:
    //   * a HELLO (a fresh attach / refetch) speaks to the flag AND to
    //     containment -- it clears the observe-first gate and drains the queue;
    //   * a durable FRAME on the stream moved the OWN FLAG only -- containment
    //     is serve-time and rides no frame, so the flag is adopted (the pane
    //     already shows it) but the gate stays shut and nothing drains: a
    //     queued HaltSubtree or Resume must not start against a containment
    //     view no Hello has refreshed since the unconfirmed write.
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

    // Manual retry of the still-current intent: only while nothing is in
    // flight, nothing newer is queued, and the last observation does not
    // already satisfy it.
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

}  // namespace hanabi::halt
