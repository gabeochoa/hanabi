// Halt / Resume: the pure model -- rows from an observation, satisfied
// predicates, the frame classifier and production wait over scripted frames,
// and the per-session write serializer. No socket, no UI.
#include <chrono>
#include <cstdio>
#include <deque>
#include <string>

#include "../../src/api/halt_state.h"
#include "../../src/api/types.h"

using namespace hanabi::halt;
using json = nlohmann::json;

static int failures = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);          \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

static Observation obs(bool halted, const char* by = "", const char* self = "s1") {
    return Observation::from_state(self, halted, by, "");
}

static json durable(int64_t seq, const char* type, const char* reason = nullptr) {
    json e = {{"type", type}};
    if (reason) e["reason"] = reason;
    return json{{"type", "frame"}, {"frame", "durable"}, {"seq", seq}, {"event", e}};
}
static json error_msg(const char* m) { return json{{"type", "error"}, {"message", m}}; }

// A scripted source: hands out frames in order, then runs dry (discarded).
struct Script {
    std::deque<json> frames;
    json next(std::chrono::steady_clock::time_point) {
        if (frames.empty()) return json(json::value_t::discarded);
        json f = std::move(frames.front());
        frames.pop_front();
        return f;
    }
};

static Outcome run(Script& s, int64_t boundary, Intent i, const Observation& o) {
    return await_echo([&s](auto d) { return s.next(d); },
                      std::chrono::steady_clock::now(), boundary, i, o,
                      [] { return std::string("dry"); });
}

// --- observation --------------------------------------------------------
static void test_observation_splits_own_mark_from_containment() {
    const auto alone = obs(true);
    CHECK(alone.own_halted && !alone.own_mark && !alone.contained());
    const auto root = obs(true, "s1");  // halted_by names self: the root's own mark
    CHECK(root.own_halted && root.own_mark && !root.contained());
    const auto child = obs(false, "root9");  // an ancestor's mark, own flag false
    CHECK(!child.own_halted && !child.own_mark && child.contained() &&
          child.contained_by == "root9");
    CHECK(child.engaged());
    const auto both = obs(true, "root9");  // own-halted AND contained
    CHECK(both.own_halted && both.contained());
}

// --- rows ----------------------------------------------------------------
static void test_rows_follow_the_contract() {
    // no advert -> nothing, whatever the state
    CHECK(!rows_for(obs(false), false, 3).any());
    // contained by an ancestor -> nothing (a Resume here would mislead)
    CHECK(!rows_for(obs(false, "root9"), true, 3).any());
    CHECK(!rows_for(obs(true, "root9"), true, 3).any());
    // not halted, no children -> Halt only
    auto r = rows_for(obs(false), true, 0);
    CHECK(r.halt && !r.halt_subtree && !r.resume);
    // not halted, children -> Halt + Halt with Sub-agents
    r = rows_for(obs(false), true, 2);
    CHECK(r.halt && r.halt_subtree && !r.resume);
    // halted ALONE, children -> Resume AND Halt with Sub-agents
    r = rows_for(obs(true), true, 2);
    CHECK(!r.halt && r.halt_subtree && r.resume);
    // halted alone, no children -> Resume only
    r = rows_for(obs(true), true, 0);
    CHECK(!r.halt && !r.halt_subtree && r.resume);
    // own mark (root of a subtree halt) -> Resume only, even with children
    r = rows_for(obs(true, "s1"), true, 4);
    CHECK(!r.halt && !r.halt_subtree && r.resume);
}

// --- satisfied -----------------------------------------------------------
static void test_satisfied_is_per_kind() {
    CHECK(satisfied(Intent::Halt, obs(true)));
    CHECK(!satisfied(Intent::Halt, obs(false)));
    // a root halted alone does NOT satisfy the subtree intent
    CHECK(!satisfied(Intent::HaltSubtree, obs(true)));
    CHECK(satisfied(Intent::HaltSubtree, obs(true, "s1")));
    // resume is satisfied only with flag AND mark clear
    CHECK(satisfied(Intent::Resume, obs(false)));
    CHECK(!satisfied(Intent::Resume, obs(true)));
    CHECK(!satisfied(Intent::Resume, obs(false, "s1")));  // flag clear, mark still owned
}

// --- classifier ---------------------------------------------------------
static void test_settle_boundary_and_kind() {
    std::string refusal, reason;
    // replay at/below the boundary is ignored whatever it says
    CHECK(settle(durable(10, "session_halted"), 10, Intent::Halt, &refusal, &reason) == Settle::Ignore);
    CHECK(settle(durable(9, "session_halted"), 10, Intent::Halt, &refusal, &reason) == Settle::Ignore);
    // beyond it, the right kind is the echo
    CHECK(settle(durable(11, "session_halted", "runaway"), 10, Intent::Halt, &refusal, &reason) == Settle::Echo);
    CHECK(reason == "runaway");
    // the OPPOSITE verb's echo is not ours
    CHECK(settle(durable(11, "session_resumed"), 10, Intent::Halt, &refusal, &reason) == Settle::Ignore);
    CHECK(settle(durable(11, "session_halted"), 10, Intent::Resume, &refusal, &reason) == Settle::Ignore);
    CHECK(settle(durable(11, "session_resumed"), 10, Intent::Resume, &refusal, &reason) == Settle::Echo);
    // a subtree halt's flag echo is the same frame
    CHECK(settle(durable(11, "session_halted"), 10, Intent::HaltSubtree, &refusal, &reason) == Settle::Echo);
    // non-durable, non-frame, other events: ignored
    CHECK(settle(json{{"type", "frame"}, {"frame", "ephemeral"}, {"seq", 12},
                      {"event", {{"type", "session_halted"}}}},
                 10, Intent::Halt, &refusal, &reason) == Settle::Ignore);
    CHECK(settle(json{{"type", "hello"}}, 10, Intent::Halt, &refusal, &reason) == Settle::Ignore);
    CHECK(settle(durable(11, "session_renamed"), 10, Intent::Halt, &refusal, &reason) == Settle::Ignore);
    // typed error = refusal, verbatim
    CHECK(settle(error_msg("insufficient access: owner required"), 10, Intent::Halt, &refusal, &reason) == Settle::Refused);
    CHECK(refusal == "insufficient access: owner required");
}

// --- production wait ----------------------------------------------------
static void test_await_confirms_on_the_first_matching_echo_after_boundary() {
    Script s;
    s.frames = {durable(5, "session_halted"),     // replay: ignored
                durable(11, "session_renamed"),   // unrelated: ignored
                durable(12, "session_resumed"),   // opposite echo (another client): ignored
                durable(13, "session_halted", "x")};
    const auto out = run(s, 10, Intent::Halt, obs(false));
    CHECK(out.status == Status::Confirmed);
    CHECK(out.detail == "x");
    CHECK(!out.mark_confirmed);  // an echo never proves a mark
    CHECK(s.frames.empty());
}

static void test_await_refusal_is_verbatim() {
    Script s;
    s.frames = {durable(11, "noop"), error_msg("halt refused: not the owner")};
    const auto out = run(s, 10, Intent::Halt, obs(false));
    CHECK(out.status == Status::Refused);
    CHECK(out.detail == "halt refused: not the owner");
}

static void test_await_dry_source_is_unconfirmed_not_failed() {
    Script s;
    s.frames = {durable(11, "usage_delta")};
    const auto out = run(s, 10, Intent::Resume, obs(true));
    CHECK(out.status == Status::Unconfirmed);
    CHECK(out.detail == "dry");
    CHECK(out.observed.own_halted);  // what was seen before the send rides along
}

static void test_await_subtree_echo_leaves_the_mark_unconfirmed() {
    Script s;
    s.frames = {durable(11, "session_halted")};
    const auto out = run(s, 10, Intent::HaltSubtree, obs(false));
    CHECK(out.status == Status::Confirmed);
    CHECK(!out.mark_confirmed);
}

// --- serializer -------------------------------------------------------
static void test_serializer_one_in_flight_and_opposite_queued() {
    Serializer sz;
    auto a = sz.raise(Intent::Halt);
    CHECK(a && a->intent == Intent::Halt && sz.in_flight());
    // opposite intent while in flight: queued, not sent
    CHECK(!sz.raise(Intent::Resume));
    CHECK(sz.queued() && *sz.queued() == Intent::Resume);
    // the halt settles confirmed; observation now says halted -> the queued
    // resume is NOT satisfied -> it is issued next, from the queue, fresh
    auto b = sz.settled(a->ticket, Status::Confirmed, obs(true));
    CHECK(b && b->intent == Intent::Resume && b->ticket != a->ticket);
    CHECK(!sz.queued());
    CHECK(sz.raised_count() == 2 && sz.started_count() == 2);
}

static void test_serializer_newest_wins_and_same_kind_coalesces() {
    Serializer sz;
    auto a = sz.raise(Intent::Halt);
    CHECK(!sz.raise(Intent::Resume));
    CHECK(!sz.raise(Intent::Halt));    // flips back: newest wins
    CHECK(!sz.raise(Intent::Halt));    // same kind: coalesces
    CHECK(sz.raised_count() == 4);
    CHECK(*sz.queued() == Intent::Halt);
    // settled halted -> the queued Halt is already satisfied -> dropped
    auto b = sz.settled(a->ticket, Status::Confirmed, obs(true));
    CHECK(!b && !sz.queued() && !sz.in_flight());
    CHECK(sz.started_count() == 1);  // one write for four raises
}

static void test_serializer_unconfirmed_gates_on_observation() {
    Serializer sz;
    auto a = sz.raise(Intent::Halt);
    CHECK(!sz.raise(Intent::Resume));
    // timeout: the halt MAY have committed -> no opposite send yet
    auto b = sz.settled(a->ticket, Status::Unconfirmed, obs(false));
    CHECK(!b && sz.needs_observation() && !sz.in_flight());
    // a further raise while gated is queued, not sent
    CHECK(!sz.raise(Intent::Resume));
    // manual retry is refused while gated
    CHECK(!sz.retry(Intent::Resume));
    // a fresh Hello arrives: halted (the write had committed). The queued
    // Resume is not satisfied -> it goes out now, against the observed state.
    auto c = sz.observed(obs(true));
    CHECK(c && c->intent == Intent::Resume && !sz.needs_observation());
    // ...and had the Hello shown NOT halted with a queued Halt, that would
    // be re-sent; with a queued Resume it is dropped as satisfied:
    Serializer sz2;
    auto d = sz2.raise(Intent::Resume);
    sz2.settled(d->ticket, Status::Unconfirmed, obs(true));
    CHECK(!sz2.raise(Intent::Resume));
    CHECK(!sz2.observed(obs(false)));  // already resumed: nothing sent
}

static void test_serializer_drops_a_late_answer_from_an_older_attempt() {
    Serializer sz;
    auto a = sz.raise(Intent::Halt);
    auto stale_ticket = a->ticket;
    auto b = sz.settled(a->ticket, Status::Confirmed, obs(true));
    CHECK(!b);
    auto c = sz.raise(Intent::Resume);
    CHECK(c && c->ticket != stale_ticket);
    // the old attempt's transport answers late: ignored, the new one stands
    CHECK(!sz.settled(stale_ticket, Status::Confirmed, obs(true)));
    CHECK(sz.in_flight() && *sz.in_flight_intent() == Intent::Resume);
    CHECK(sz.late_dropped_count() == 1);
}

static void test_serializer_manual_retry_only_for_the_current_unsatisfied_intent() {
    Serializer sz;
    auto a = sz.raise(Intent::Halt);
    sz.settled(a->ticket, Status::Unconfirmed, obs(false));
    sz.observed(obs(false));  // the write did NOT commit
    // now a manual retry of the same intent is allowed...
    auto r = sz.retry(Intent::Halt);
    CHECK(r && r->intent == Intent::Halt);
    sz.settled(r->ticket, Status::Confirmed, obs(true));
    // ...but not of one the observation already satisfies
    CHECK(!sz.retry(Intent::Halt));
    // and never while something newer is queued
    auto x = sz.raise(Intent::Resume);
    CHECK(x);
    CHECK(!sz.raise(Intent::Halt));
    CHECK(!sz.retry(Intent::Resume));
}

static void test_serializer_subtree_after_halt_alone_is_a_real_write() {
    // a root halted alone raises Halt with Sub-agents: not satisfied by the
    // flag alone, so it is a write; its flag echo confirms only the flag
    Serializer sz;
    auto a = sz.raise(Intent::HaltSubtree);
    CHECK(a);
    CHECK(!satisfied(Intent::HaltSubtree, obs(true)));
    auto b = sz.settled(a->ticket, Status::Confirmed, obs(true));  // Hello not yet re-read
    CHECK(!b);
    // a fresh Hello with the own mark settles the ask
    CHECK(satisfied(Intent::HaltSubtree, obs(true, "s1")));
}

// F3: a stream frame moves the own flag only. It must not clear the
// observe-first gate -- containment rides no frame -- so a queued Resume
// (or HaltSubtree) stays queued until a Hello has been read.
static void test_a_frame_observation_adopts_the_flag_but_does_not_open_the_gate() {
    Serializer sz;
    auto a = sz.raise(Intent::Halt);
    CHECK(!sz.raise(Intent::Resume));
    sz.settled(a->ticket, Status::Unconfirmed, obs(false));
    CHECK(sz.needs_observation());
    // the stream says "halted": the flag is adopted, nothing drains
    CHECK(!sz.observed(obs(true), Serializer::Source::Frame));
    CHECK(sz.needs_observation());
    CHECK(sz.queued() && *sz.queued() == Intent::Resume);
    // a Hello then does both: gate open, queue drained against it
    auto c = sz.observed(obs(true), Serializer::Source::Hello);
    CHECK(c && c->intent == Intent::Resume && !sz.needs_observation());
    // and a frame's flag does not erase a Hello's containment view
    Serializer sz2;
    sz2.observed(obs(true, "root9"));  // contained, own-halted
    sz2.observed(obs(false), Serializer::Source::Frame);  // own flag cleared by a frame
    auto d = sz2.raise(Intent::Resume);  // still contained: the write goes out and the
    CHECK(d);                            // verb's own Hello refuses it as Contained
}

static void test_a_failure_before_attach_keeps_the_last_observation() {
    // Observed halted; a Resume is raised and in flight. The
    // transport fails before any Hello: the settle carries NO fresh
    // observation, so the serializer must keep "halted" -- a manual retry of
    // Resume is then still allowed (not satisfied by a stale default).
    Serializer sz;
    sz.observed(obs(true));
    auto a = sz.raise(Intent::Resume);
    CHECK(a);
    CHECK(!sz.settled(a->ticket, Status::Failed, Observation{}, /*now_is_fresh=*/false));
    auto r = sz.retry(Intent::Resume);
    CHECK(r && r->intent == Intent::Resume);
    // Had the default been adopted, Resume would read as satisfied and the
    // retry refused:
    Serializer sz2;
    sz2.observed(obs(true));
    auto b = sz2.raise(Intent::Resume);
    sz2.settled(b->ticket, Status::Failed, Observation{}, /*now_is_fresh=*/true);
    CHECK(!sz2.retry(Intent::Resume));
}

// The menu gate over an attached session: access x advert. Only the owner's
// attach that advertised halt_v1 offers rows; a write-level operator does not,
// nor does an owner on a server that did not advertise.
static void test_session_may_halt_is_owner_and_advert() {
    using api::SessionAccess;
    const SessionAccess levels[] = {SessionAccess::Owner, SessionAccess::Write,
                                    SessionAccess::Read, SessionAccess::None,
                                    SessionAccess::Unknown};
    for (SessionAccess a : levels)
        for (bool advert : {true, false}) {
            api::Session s;
            s.access = a;
            s.can_halt = advert;
            const bool want = a == SessionAccess::Owner && advert;
            CHECK(api::session_may_halt(s) == want);
            const auto r = rows_for(obs(false), api::session_may_halt(s), 2);
            CHECK(r.any() == want);
        }
}

// A confirmed subtree halt whose mark is not yet observed parks the session
// exactly as an unconfirmed write does: the caller settles it as Unconfirmed
// (the app's rule), so a queued Resume waits for the Hello.
static void test_confirmed_flag_with_unproven_mark_gates_like_unconfirmed() {
    Serializer sz;
    auto a = sz.raise(Intent::HaltSubtree);
    CHECK(!sz.raise(Intent::Resume));
    // the app settles a flag-only confirmation as Unconfirmed for gating
    CHECK(!sz.settled(a->ticket, Status::Unconfirmed, obs(true)));
    CHECK(sz.needs_observation() && *sz.queued() == Intent::Resume);
    auto c = sz.observed(obs(true, "s1"));  // the Hello shows the mark
    CHECK(c && c->intent == Intent::Resume);
}

int main() {
    test_confirmed_flag_with_unproven_mark_gates_like_unconfirmed();
    test_session_may_halt_is_owner_and_advert();
    test_a_frame_observation_adopts_the_flag_but_does_not_open_the_gate();
    test_a_failure_before_attach_keeps_the_last_observation();
    test_observation_splits_own_mark_from_containment();
    test_rows_follow_the_contract();
    test_satisfied_is_per_kind();
    test_settle_boundary_and_kind();
    test_await_confirms_on_the_first_matching_echo_after_boundary();
    test_await_refusal_is_verbatim();
    test_await_dry_source_is_unconfirmed_not_failed();
    test_await_subtree_echo_leaves_the_mark_unconfirmed();
    test_serializer_one_in_flight_and_opposite_queued();
    test_serializer_newest_wins_and_same_kind_coalesces();
    test_serializer_unconfirmed_gates_on_observation();
    test_serializer_drops_a_late_answer_from_an_older_attempt();
    test_serializer_manual_retry_only_for_the_current_unsatisfied_intent();
    test_serializer_subtree_after_halt_alone_is_a_real_write();
    if (failures == 0) std::printf("test_halt_state: OK\n");
    return failures == 0 ? 0 : 1;
}
