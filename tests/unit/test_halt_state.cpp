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

static void test_observation_splits_own_mark_from_containment() {
    const auto alone = obs(true);
    CHECK(alone.own_halted && !alone.own_mark && !alone.contained());
    const auto root = obs(true, "s1");
    CHECK(root.own_halted && root.own_mark && !root.contained());
    const auto child = obs(false, "root9");
    CHECK(!child.own_halted && !child.own_mark && child.contained() &&
          child.contained_by == "root9");
    CHECK(child.engaged());
    const auto both = obs(true, "root9");
    CHECK(both.own_halted && both.contained());
}

static void test_rows_follow_the_contract() {
    CHECK(!rows_for(obs(false), false, 3).any());
    CHECK(!rows_for(obs(false, "root9"), true, 3).any());
    CHECK(!rows_for(obs(true, "root9"), true, 3).any());
    auto r = rows_for(obs(false), true, 0);
    CHECK(r.halt && !r.halt_subtree && !r.resume);
    r = rows_for(obs(false), true, 2);
    CHECK(r.halt && r.halt_subtree && !r.resume);
    r = rows_for(obs(true), true, 2);
    CHECK(!r.halt && r.halt_subtree && r.resume);
    r = rows_for(obs(true), true, 0);
    CHECK(!r.halt && !r.halt_subtree && r.resume);
    r = rows_for(obs(true, "s1"), true, 4);
    CHECK(!r.halt && !r.halt_subtree && r.resume);
}

static void test_satisfied_is_per_kind() {
    CHECK(satisfied(Intent::Halt, obs(true)));
    CHECK(!satisfied(Intent::Halt, obs(false)));
    CHECK(!satisfied(Intent::HaltSubtree, obs(true)));
    CHECK(satisfied(Intent::HaltSubtree, obs(true, "s1")));
    CHECK(satisfied(Intent::Resume, obs(false)));
    CHECK(!satisfied(Intent::Resume, obs(true)));
    CHECK(!satisfied(Intent::Resume, obs(false, "s1")));
}

static void test_settle_boundary_and_kind() {
    std::string refusal, reason;
    CHECK(settle(durable(10, "session_halted"), 10, Intent::Halt, &refusal, &reason) == Settle::Ignore);
    CHECK(settle(durable(9, "session_halted"), 10, Intent::Halt, &refusal, &reason) == Settle::Ignore);
    CHECK(settle(durable(11, "session_halted", "runaway"), 10, Intent::Halt, &refusal, &reason) == Settle::Echo);
    CHECK(reason == "runaway");
    CHECK(settle(durable(11, "session_resumed"), 10, Intent::Halt, &refusal, &reason) == Settle::Ignore);
    CHECK(settle(durable(11, "session_halted"), 10, Intent::Resume, &refusal, &reason) == Settle::Ignore);
    CHECK(settle(durable(11, "session_resumed"), 10, Intent::Resume, &refusal, &reason) == Settle::Echo);
    CHECK(settle(durable(11, "session_halted"), 10, Intent::HaltSubtree, &refusal, &reason) == Settle::Echo);
    CHECK(settle(json{{"type", "frame"}, {"frame", "ephemeral"}, {"seq", 12},
                      {"event", {{"type", "session_halted"}}}},
                 10, Intent::Halt, &refusal, &reason) == Settle::Ignore);
    CHECK(settle(json{{"type", "hello"}}, 10, Intent::Halt, &refusal, &reason) == Settle::Ignore);
    CHECK(settle(durable(11, "session_renamed"), 10, Intent::Halt, &refusal, &reason) == Settle::Ignore);
    CHECK(settle(error_msg("insufficient access: owner required"), 10, Intent::Halt, &refusal, &reason) == Settle::Refused);
    CHECK(refusal == "insufficient access: owner required");
}

static void test_await_confirms_on_the_first_matching_echo_after_boundary() {
    Script s;
    s.frames = {durable(5, "session_halted"),
                durable(11, "session_renamed"),
                durable(12, "session_resumed"),
                durable(13, "session_halted", "x")};
    const auto out = run(s, 10, Intent::Halt, obs(false));
    CHECK(out.status == Status::Confirmed);
    CHECK(out.detail == "x");
    CHECK(!out.mark_confirmed);
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
    CHECK(out.observed.own_halted);
}

static void test_await_subtree_echo_leaves_the_mark_unconfirmed() {
    Script s;
    s.frames = {durable(11, "session_halted")};
    const auto out = run(s, 10, Intent::HaltSubtree, obs(false));
    CHECK(out.status == Status::Confirmed);
    CHECK(!out.mark_confirmed);
}

static void test_serializer_one_in_flight_and_opposite_queued() {
    Serializer sz;
    auto a = sz.raise(Intent::Halt);
    CHECK(a && a->intent == Intent::Halt && sz.in_flight());
    CHECK(!sz.raise(Intent::Resume));
    CHECK(sz.queued() && *sz.queued() == Intent::Resume);
    auto b = sz.settled(a->ticket, Status::Confirmed, obs(true));
    CHECK(b && b->intent == Intent::Resume && b->ticket != a->ticket);
    CHECK(!sz.queued());
    CHECK(sz.raised_count() == 2 && sz.started_count() == 2);
}

static void test_serializer_newest_wins_and_same_kind_coalesces() {
    Serializer sz;
    auto a = sz.raise(Intent::Halt);
    CHECK(!sz.raise(Intent::Resume));
    CHECK(!sz.raise(Intent::Halt));
    CHECK(!sz.raise(Intent::Halt));
    CHECK(sz.raised_count() == 4);
    CHECK(*sz.queued() == Intent::Halt);
    auto b = sz.settled(a->ticket, Status::Confirmed, obs(true));
    CHECK(!b && !sz.queued() && !sz.in_flight());
    CHECK(sz.started_count() == 1);
}

static void test_serializer_unconfirmed_gates_on_observation() {
    Serializer sz;
    auto a = sz.raise(Intent::Halt);
    CHECK(!sz.raise(Intent::Resume));
    auto b = sz.settled(a->ticket, Status::Unconfirmed, obs(false));
    CHECK(!b && sz.needs_observation() && !sz.in_flight());
    CHECK(!sz.raise(Intent::Resume));
    CHECK(!sz.retry(Intent::Resume));
    auto c = sz.observed(obs(true));
    CHECK(c && c->intent == Intent::Resume && !sz.needs_observation());
    Serializer sz2;
    auto d = sz2.raise(Intent::Resume);
    sz2.settled(d->ticket, Status::Unconfirmed, obs(true));
    CHECK(!sz2.raise(Intent::Resume));
    CHECK(!sz2.observed(obs(false)));
}

static void test_serializer_drops_a_late_answer_from_an_older_attempt() {
    Serializer sz;
    auto a = sz.raise(Intent::Halt);
    auto stale_ticket = a->ticket;
    auto b = sz.settled(a->ticket, Status::Confirmed, obs(true));
    CHECK(!b);
    auto c = sz.raise(Intent::Resume);
    CHECK(c && c->ticket != stale_ticket);
    CHECK(!sz.settled(stale_ticket, Status::Confirmed, obs(true)));
    CHECK(sz.in_flight() && *sz.in_flight_intent() == Intent::Resume);
    CHECK(sz.late_dropped_count() == 1);
}

static void test_serializer_manual_retry_only_for_the_current_unsatisfied_intent() {
    Serializer sz;
    auto a = sz.raise(Intent::Halt);
    sz.settled(a->ticket, Status::Unconfirmed, obs(false));
    sz.observed(obs(false));
    auto r = sz.retry(Intent::Halt);
    CHECK(r && r->intent == Intent::Halt);
    sz.settled(r->ticket, Status::Confirmed, obs(true));
    CHECK(!sz.retry(Intent::Halt));
    auto x = sz.raise(Intent::Resume);
    CHECK(x);
    CHECK(!sz.raise(Intent::Halt));
    CHECK(!sz.retry(Intent::Resume));
}

static void test_serializer_subtree_after_halt_alone_is_a_real_write() {
    Serializer sz;
    auto a = sz.raise(Intent::HaltSubtree);
    CHECK(a);
    CHECK(!satisfied(Intent::HaltSubtree, obs(true)));
    auto b = sz.settled(a->ticket, Status::Confirmed, obs(true));
    CHECK(!b);
    CHECK(satisfied(Intent::HaltSubtree, obs(true, "s1")));
}

static void test_a_frame_observation_adopts_the_flag_but_does_not_open_the_gate() {
    Serializer sz;
    auto a = sz.raise(Intent::Halt);
    CHECK(!sz.raise(Intent::Resume));
    sz.settled(a->ticket, Status::Unconfirmed, obs(false));
    CHECK(sz.needs_observation());
    CHECK(!sz.observed(obs(true), Serializer::Source::Frame));
    CHECK(sz.needs_observation());
    CHECK(sz.queued() && *sz.queued() == Intent::Resume);
    auto c = sz.observed(obs(true), Serializer::Source::Hello);
    CHECK(c && c->intent == Intent::Resume && !sz.needs_observation());
    Serializer sz2;
    sz2.observed(obs(true, "root9"));
    sz2.observed(obs(false), Serializer::Source::Frame);
    auto d = sz2.raise(Intent::Resume);
    CHECK(d);
}

static void test_a_failure_before_attach_keeps_the_last_observation() {
    Serializer sz;
    sz.observed(obs(true));
    auto a = sz.raise(Intent::Resume);
    CHECK(a);
    CHECK(!sz.settled(a->ticket, Status::Failed, Observation{}, false));
    auto r = sz.retry(Intent::Resume);
    CHECK(r && r->intent == Intent::Resume);
    Serializer sz2;
    sz2.observed(obs(true));
    auto b = sz2.raise(Intent::Resume);
    sz2.settled(b->ticket, Status::Failed, Observation{}, true);
    CHECK(!sz2.retry(Intent::Resume));
}

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

static void test_confirmed_flag_with_unproven_mark_gates_like_unconfirmed() {
    Serializer sz;
    auto a = sz.raise(Intent::HaltSubtree);
    CHECK(!sz.raise(Intent::Resume));
    CHECK(!sz.settled(a->ticket, Status::Unconfirmed, obs(true)));
    CHECK(sz.needs_observation() && *sz.queued() == Intent::Resume);
    auto c = sz.observed(obs(true, "s1"));
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
