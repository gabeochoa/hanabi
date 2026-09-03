#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/ecs/home_buckets.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

using api::SessionSummary;
using Members = std::vector<const SessionSummary*>;

static SessionSummary session(int i) {
    SessionSummary s;
    s.id = "session-" + std::to_string(i);
    s.title = "Session " + std::to_string(i);
    s.updated_at = 100000 - i * 997;
    switch (i % 6) {
        case 0:
            s.state = api::ThreadState::Attention;
            s.tag = api::ThreadTag::Blocked;
            break;
        case 1:
            s.state = api::ThreadState::Attention;
            s.tag = api::ThreadTag::Done;
            break;
        case 2:
            s.state = api::ThreadState::Running;
            break;
        case 3:
            s.state = api::ThreadState::Ready;
            break;
        case 4:
            s.state = api::ThreadState::Parked;
            break;
        default:
            s.state = api::ThreadState::Unknown;
            break;
    }
    if (i % 11 == 0) s.archive_override = true;
    return s;
}

struct Expected {
    Members waiting;
    Members finished;
    Members running;
    Members recent;
};

template<class Predicate>
static Members prior_scan(const std::vector<SessionSummary>& sessions,
                          Predicate predicate) {
    Members out;
    for (const SessionSummary& s : sessions)
        if (predicate(s)) out.push_back(&s);
    return out;
}

static Expected prior_behavior(const std::vector<SessionSummary>& sessions,
                               std::size_t recentLimit) {
    Expected out;
    out.waiting = prior_scan(sessions, [](const SessionSummary& s) {
        return s.state == api::ThreadState::Attention &&
               (s.tag == api::ThreadTag::Blocked ||
                s.tag == api::ThreadTag::Waiting);
    });
    out.finished = prior_scan(sessions, [](const SessionSummary& s) {
        return s.state == api::ThreadState::Attention &&
               s.tag != api::ThreadTag::Blocked &&
               s.tag != api::ThreadTag::Waiting;
    });
    out.running = prior_scan(sessions, [](const SessionSummary& s) {
        return s.state == api::ThreadState::Running;
    });
    out.recent = prior_scan(sessions, [](const SessionSummary& s) {
        return !ecs::model::is_archived(s) &&
               s.state != api::ThreadState::Attention &&
               s.state != api::ThreadState::Running;
    });
    const std::size_t keep = std::min(out.recent.size(), recentLimit);
    for (std::size_t i = 0; i < keep; ++i) {
        auto newest = std::max_element(
            out.recent.begin() + static_cast<std::ptrdiff_t>(i),
            out.recent.end(),
            [](const SessionSummary* a, const SessionSummary* b) {
                return a->updated_at < b->updated_at;
            });
        std::iter_swap(out.recent.begin() + static_cast<std::ptrdiff_t>(i),
                       newest);
    }
    return out;
}

static void check_result(const ecs::model::HomeBuckets& got,
                         const Expected& want, std::size_t recentLimit) {
    CHECK(got.waiting() == want.waiting);
    CHECK(got.finished() == want.finished);
    CHECK(got.running() == want.running);
    CHECK(got.recent().size() == want.recent.size());
    const std::size_t keep = std::min(want.recent.size(), recentLimit);
    CHECK(std::equal(got.recent().begin(),
                     got.recent().begin() + static_cast<std::ptrdiff_t>(keep),
                     want.recent.begin()));
}

static void test_matches_prior_independent_scans() {
    for (int count : {0, 1, 7, 20, 73}) {
        std::vector<SessionSummary> sessions;
        for (int i = 0; i < count; ++i) sessions.push_back(session(i));
        for (std::size_t limit : {std::size_t{0}, std::size_t{1},
                                  std::size_t{20}, std::size_t{100}}) {
            ecs::model::HomeBuckets got(limit);
            got.update(1, sessions);
            check_result(got, prior_behavior(sessions, limit), limit);
        }
    }
}

static void test_revision_change_rebuilds_once() {
    constexpr std::size_t limit = 20;
    std::vector<SessionSummary> first;
    for (int i = 0; i < 40; ++i) first.push_back(session(i));
    ecs::model::HomeBuckets buckets(limit);
    buckets.update(7, first);
    CHECK(buckets.rebuilds() == 1);
    buckets.update(7, first);
    CHECK(buckets.rebuilds() == 1);

    std::vector<SessionSummary> replacement;
    for (int i = 100; i < 140; ++i) replacement.push_back(session(i));
    buckets.update(8, replacement);
    CHECK(buckets.rebuilds() == 2);
    check_result(buckets, prior_behavior(replacement, limit), limit);
    buckets.update(8, replacement);
    CHECK(buckets.rebuilds() == 2);
}

static void test_same_revision_keeps_the_existing_snapshot() {
    constexpr std::size_t limit = 20;
    std::vector<SessionSummary> first;
    for (int i = 0; i < 40; ++i) first.push_back(session(i));
    std::vector<SessionSummary> replacement;
    for (int i = 100; i < 140; ++i) replacement.push_back(session(i));

    ecs::model::HomeBuckets buckets(limit);
    buckets.update(7, first);
    buckets.update(7, replacement);
    CHECK(buckets.rebuilds() == 1);
    check_result(buckets, prior_behavior(first, limit), limit);
}

// A thread WAITING on you belongs on the "waiting on you" shelf, not under
// "finished since you looked". It reached the wrong shelf the moment the tag
// was split -- the bucket asked only about Blocked and everything else with an
// Attention state fell through to finished -- and the digest is where that is
// most visibly wrong, because the shelf is captioned with the opposite claim.
static void test_a_waiting_thread_is_not_finished() {
    std::vector<SessionSummary> sessions;
    std::vector<SessionSummary>& v = sessions;
    SessionSummary waiting;
    waiting.id = "waiting";
    waiting.updated_at = 500;
    waiting.state = api::ThreadState::Attention;
    waiting.tag = api::ThreadTag::Waiting;
    v.push_back(waiting);
    SessionSummary blocked;
    blocked.id = "blocked";
    blocked.updated_at = 400;
    blocked.state = api::ThreadState::Attention;
    blocked.tag = api::ThreadTag::Blocked;
    v.push_back(blocked);
    SessionSummary done;
    done.id = "done";
    done.updated_at = 300;
    done.state = api::ThreadState::Attention;
    done.tag = api::ThreadTag::Done;
    v.push_back(done);

    ecs::model::HomeBuckets buckets(20);
    buckets.update(1, sessions);
    CHECK(buckets.waiting().size() == 2);
    CHECK(buckets.finished().size() == 1);
    CHECK(buckets.finished()[0]->id == "done");
}

int main() {
    test_matches_prior_independent_scans();
    test_a_waiting_thread_is_not_finished();
    test_revision_change_rebuilds_once();
    test_same_revision_keeps_the_existing_snapshot();
    if (failures == 0) std::printf("test_home_buckets: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
