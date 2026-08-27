// The local-first OUTBOX, both halves.
//
// The write half (disk_cache::outbox_add/remove) shipped with the local-first
// branch and has been exercised by nothing: `grep -rn outbox src tests tools`
// before this file returned only the implementation and one call site.
// docs/COMMIT_AUDIT.md CB3: "There is also no test touching `outbox` at all."
//
// Two things are proven here, and they are the two the feature promises:
//
//   1. A PROMPT SURVIVES A RESTART. Not "survives a second call in the same
//      process" -- this test re-execs ITSELF with a marker argument and reads
//      the store back in a genuinely new process, because the interesting
//      failure (a store that is really an in-memory map with a save that never
//      happens) passes a same-process round trip.
//   2. A FAILED SEND IS RETRIED. The policy in src/api/outbox.h: FIFO within a
//      thread, round-robin across threads, one attempt in flight, exponential
//      backoff, and nothing is ever dropped except by a server confirmation.
//
// Pure logic and a filesystem. No network, no graphics, no ECS.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

#include "../../src/api/disk_cache.h"
#include "../../src/api/outbox.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

// ---------------------------------------------------------------------------
// (1) The store, across a real process boundary.
//
// The parent writes three prompts across two threads and re-execs itself with
// --relaunch pointing at the SAME cache dir. The child enumerates from cold --
// outbox_sessions() first, because after a restart nothing in memory knows
// which threads have unsent work, which is exactly the reason outbox_list
// alone was never enough to build a recovery path on.
// ---------------------------------------------------------------------------

static int relaunched_reader() {
    std::printf("test_outbox_survives_a_restart (relaunched process %d)\n",
                static_cast<int>(getpid()));

    const auto ids = api::disk_cache::outbox_sessions();
    CHECK(ids.size() == 2);
    if (ids.size() == 2) {
        CHECK(ids[0] == "s-alpha");   // sorted, so a restore is deterministic
        CHECK(ids[1] == "s-beta");
    }

    const auto alpha = api::disk_cache::outbox_list("s-alpha");
    CHECK(alpha.size() == 2);
    if (alpha.size() == 2) {
        CHECK(alpha[0] == "first thing I typed");   // FIFO, in typing order
        CHECK(alpha[1] == "second thing I typed");
    }
    const auto beta = api::disk_cache::outbox_list("s-beta");
    CHECK(beta.size() == 1);
    if (beta.size() == 1) CHECK(beta[0] == "a prompt in another thread");

    // The confirmed one is gone and stayed gone.
    CHECK(api::disk_cache::outbox_list("s-gamma").empty());

    std::printf(g_failures == 0 ? "  relaunched reader: OK\n"
                                : "  relaunched reader: FAILED\n");
    return g_failures == 0 ? 0 : 1;
}

static void test_survives_a_restart(const char* self, const char* dir) {
    std::printf("test_outbox_survives_a_restart\n");

    api::disk_cache::outbox_add("s-alpha", "first thing I typed");
    api::disk_cache::outbox_add("s-alpha", "second thing I typed");
    api::disk_cache::outbox_add("s-beta", "a prompt in another thread");
    // One that the server DID confirm: it must not come back from the dead.
    api::disk_cache::outbox_add("s-gamma", "this one landed");
    api::disk_cache::outbox_remove("s-gamma", "this one landed");

    // Visible in this process first, so a failure below is attributable.
    CHECK(api::disk_cache::outbox_sessions().size() == 2);

    std::string cmd = std::string("HANABI_CACHE_DIR='") + dir + "' '" + self +
                      "' --relaunch";
    const int rc = std::system(cmd.c_str());
    CHECK(rc == 0);
    if (rc != 0)
        std::printf("  (the relaunched reader exited %d — the store did not "
                    "survive the process boundary)\n", rc);
}

// ---------------------------------------------------------------------------
// (2) The retry policy.
// ---------------------------------------------------------------------------

static std::vector<api::outbox::Entry> store(
    std::initializer_list<std::pair<const char*, const char*>> rows) {
    std::vector<api::outbox::Entry> v;
    for (const auto& r : rows)
        v.push_back(api::outbox::Entry{r.first, r.second, 0, 0});
    return v;
}

static void test_backoff_curve() {
    std::printf("test_outbox_backoff_curve\n");
    using api::outbox::Retry;
    // Never wait before the first attempt: a restored entry is ready NOW,
    // because the reason the app is starting is usually that the last one died.
    CHECK(Retry::backoff_for(0) == 0);
    CHECK(Retry::backoff_for(1) == 2);
    CHECK(Retry::backoff_for(2) == 4);
    CHECK(Retry::backoff_for(3) == 8);
    CHECK(Retry::backoff_for(4) == 16);
    CHECK(Retry::backoff_for(5) == 32);
    // Capped, and capped forever: a backend down for an hour is retried every
    // minute, not once a fortnight.
    CHECK(Retry::backoff_for(6) == 60);
    CHECK(Retry::backoff_for(40) == 60);
}

static void test_a_failed_send_is_retried() {
    std::printf("test_outbox_a_failed_send_is_retried\n");
    api::outbox::Retry r;
    auto anywhere = [](const std::string&) { return true; };

    r.restore(store({{"s1", "hello"}}));
    CHECK(r.size() == 1);

    // Ready immediately at t=0.
    const api::outbox::Entry* first = r.next(0, anywhere);
    CHECK(first != nullptr);
    if (!first) return;
    CHECK(first->prompt == "hello");
    r.attempted(*first);

    // ONE at a time: while that attempt is unresolved nothing else is handed
    // out, or two copies of the same turn race into one transcript.
    CHECK(r.in_flight());
    CHECK(r.next(0, anywhere) == nullptr);

    // It failed. Still held -- this is the whole promise -- and now backing off.
    r.failed("s1", "hello", 100);
    CHECK(r.size() == 1);
    CHECK(!r.in_flight());
    CHECK(r.attempts_for("s1", "hello") == 1);
    CHECK(r.next(101, anywhere) == nullptr);   // 2s not elapsed
    const api::outbox::Entry* again = r.next(102, anywhere);
    CHECK(again != nullptr);                   // 2s elapsed: try again
    if (!again) return;
    r.attempted(*again);
    CHECK(r.attempts_for("s1", "hello") == 2);

    // The server took it. Gone, and it does not come back.
    r.confirmed("s1", "hello");
    CHECK(r.empty());
    CHECK(!r.in_flight());
    CHECK(r.next(1000, anywhere) == nullptr);
}

static void test_one_dead_thread_does_not_starve_the_others() {
    std::printf("test_outbox_one_dead_thread_does_not_starve_the_others\n");
    api::outbox::Retry r;
    r.restore(store({{"dead", "p1"}, {"dead", "p2"}, {"live", "p3"}}));

    // "dead" is not dispatchable (its tab is closed, say). The live thread's
    // entry must still be reachable rather than sitting behind two entries
    // that can never go.
    auto only_live = [](const std::string& id) { return id == "live"; };
    const api::outbox::Entry* pick = r.next(0, only_live);
    CHECK(pick != nullptr);
    if (pick) CHECK(pick->sessionId == "live");

    // With everything dispatchable, order within a thread is FIFO and the
    // cursor moves on, so no single thread monopolises the slot.
    api::outbox::Retry r2;
    r2.restore(store({{"a", "a1"}, {"a", "a2"}, {"b", "b1"}}));
    auto anywhere = [](const std::string&) { return true; };
    const api::outbox::Entry* p1 = r2.next(0, anywhere);
    CHECK(p1 != nullptr);
    if (!p1) return;
    CHECK(p1->prompt == "a1");
    r2.attempted(*p1);
    r2.confirmed("a", "a1");
    const api::outbox::Entry* p2 = r2.next(0, anywhere);
    CHECK(p2 != nullptr);
    if (p2) CHECK(p2->prompt == "a2");
}

static void test_restore_does_not_reset_a_backoff() {
    std::printf("test_outbox_restore_does_not_reset_a_backoff\n");
    api::outbox::Retry r;
    auto anywhere = [](const std::string&) { return true; };
    r.restore(store({{"s1", "hello"}}));
    const api::outbox::Entry* e = r.next(0, anywhere);
    CHECK(e != nullptr);
    if (!e) return;
    r.attempted(*e);
    r.failed("s1", "hello", 1000);
    CHECK(r.attempts_for("s1", "hello") == 1);

    // A second enumeration of the same store must not look like a fresh
    // entry. It did, once: re-reading the disk every frame reset the attempt
    // count and turned the backoff into a per-frame retry loop.
    r.restore(store({{"s1", "hello"}}));
    CHECK(r.attempts_for("s1", "hello") == 1);
    CHECK(r.next(1001, anywhere) == nullptr);
}

static void test_retry_now_reuses_the_durable_entry() {
    std::printf("test_outbox_retry_now_reuses_the_durable_entry\n");
    api::outbox::Retry r;
    auto anywhere = [](const std::string&) { return true; };
    r.restore(store({{"s1", "hello"}}));
    const api::outbox::Entry* first = r.next(0, anywhere);
    CHECK(first != nullptr);
    if (!first) return;
    r.attempted(*first);
    r.failed("s1", "hello", 100);
    CHECK(r.next(101, anywhere) == nullptr);
    CHECK(r.retry_now("s1", "hello"));
    CHECK(r.size() == 1);
    const api::outbox::Entry* immediate = r.next(101, anywhere);
    CHECK(immediate != nullptr);
    if (!immediate) return;
    r.attempted(*immediate);
    CHECK(!r.retry_now("s1", "hello"));
    CHECK(r.size() == 1);
}

static void test_a_hand_resend_does_not_double_send() {
    std::printf("test_outbox_a_hand_resend_does_not_double_send\n");
    api::outbox::Retry r;
    auto anywhere = [](const std::string&) { return true; };
    r.restore(store({{"s1", "hello"}}));
    // The user gave up waiting and re-sent it themselves; that send confirmed.
    r.forget("s1", "hello");
    CHECK(r.empty());
    CHECK(r.next(0, anywhere) == nullptr);
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--relaunch") == 0)
        return relaunched_reader();

    std::printf("=== test_outbox ===\n");

    char tmpl[] = "/tmp/hanabi_outbox_test.XXXXXX";
    const char* dir = mkdtemp(tmpl);
    if (!dir) {
        std::printf("  FAIL: could not make a temp cache dir\n");
        return 1;
    }
    setenv("HANABI_CACHE_DIR", dir, 1);

    test_survives_a_restart(argv[0], dir);
    test_backoff_curve();
    test_a_failed_send_is_retried();
    test_one_dead_thread_does_not_starve_the_others();
    test_restore_does_not_reset_a_backoff();
    test_retry_now_reuses_the_durable_entry();
    test_a_hand_resend_does_not_double_send();

    unsetenv("HANABI_CACHE_DIR");
    std::string rm = std::string("rm -rf '") + dir + "'";
    (void)std::system(rm.c_str());

    if (g_failures == 0) {
        std::printf("=== test_outbox: ALL PASSED ===\n");
        return 0;
    }
    std::printf("=== test_outbox: %d FAILED ===\n", g_failures);
    return 1;
}
