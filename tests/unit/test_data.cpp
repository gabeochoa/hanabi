// Unit tests for the DATA/LOADER layer additions (wt/data):
//   (1) disk_cache total_bytes() / wipe_all() + transcript round-trip
//   (2) message-send queue ordering + per-session draining logic
//   (3) newest-N windowing (get_session(id, limit)) still correct
//   (4) settings read (get_settings) from the mock + shape mapping
//
// Pure logic only — no network, no graphics. The message-queue test drives the
// AppComponent queue helpers directly (the loader's dispatch decision is a
// simple predicate over the same helpers, exercised here without the ECS).
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

#define AFTER_HOURS_ENTITY_HELPER
#define AFTER_HOURS_ENTITY_QUERY
#define AFTER_HOURS_SYSTEM
#include "../../vendor/afterhours/src/ecs.h"

#include "../../src/api/disk_cache.h"
#include "../../src/api/mock_client.h"
#include "../../src/ecs/components.h"
#include "../../src/util/format.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

// --- (1) disk cache: round-trip + total_bytes + wipe ----------------------
static void test_disk_cache_total_and_wipe() {
    std::printf("test_disk_cache_total_and_wipe\n");
    // Isolate to a temp dir so we never touch a real user cache.
    std::string dir = "/tmp/hanabi_test_cache_" + std::to_string(::getpid());
    setenv("HANABI_CACHE_DIR", dir.c_str(), 1);
    api::disk_cache::set_namespace("");  // flat layout for the test
    // Clean slate.
    api::disk_cache::wipe_all();
    CHECK(api::disk_cache::total_bytes() == 0);

    // Persist a session list + two transcripts.
    std::vector<api::SessionSummary> sums;
    api::SessionSummary a;
    a.id = "sess-a";
    a.title = "Alpha";
    sums.push_back(a);
    api::disk_cache::save_sessions(sums);

    api::Session s1;
    s1.summary.id = "sess-a";
    for (int i = 0; i < 5; ++i) {
        api::Message m;
        m.id = "m" + std::to_string(i);
        m.text = "hello world message body number " + std::to_string(i);
        s1.messages.push_back(m);
    }
    api::disk_cache::save_transcript(s1);

    api::Session s2;
    s2.summary.id = "sess-b";
    api::Message m2;
    m2.id = "x";
    m2.text = "second thread";
    s2.messages.push_back(m2);
    api::disk_cache::save_transcript(s2);

    // total_bytes counts sessions.json + both tx_*.json > 0.
    std::uint64_t bytes = api::disk_cache::total_bytes();
    CHECK(bytes > 0);

    // Round-trip: the transcript reads back with all messages (lazy re-read
    // from disk — the RAM-eviction / lazy-load story rests on this).
    auto back = api::disk_cache::load_transcript("sess-a");
    CHECK(back.has_value());
    CHECK(back && back->messages.size() == 5);
    CHECK(back && back->summary.id == "sess-a");

    // wipe_all removes exactly our cache files (sessions.json + 2 transcripts).
    std::size_t removed = api::disk_cache::wipe_all();
    CHECK(removed == 3);
    CHECK(api::disk_cache::total_bytes() == 0);
    // After a wipe the transcript is gone (forces a cold refetch).
    CHECK(!api::disk_cache::load_transcript("sess-a").has_value());

    unsetenv("HANABI_CACHE_DIR");
}

// --- (2) message-send queue: ordering + per-session draining --------------
// The loader's rule: enqueue when a session is busy; dispatch the FIFO head of
// a FREE session, one per turn. We model "busy" with sendPending/sendSessionId
// (the same fields sending_for() reads) and verify order + independence.
static void test_message_queue_ordering() {
    std::printf("test_message_queue_ordering\n");
    ecs::AppComponent app;

    // Session "s1" has a reply in flight; three sends arrive for it.
    app.sendPending = true;
    app.sendSessionId = "s1";
    CHECK(app.sending_for("s1"));
    app.enqueue_send("s1", "first");
    app.enqueue_send("s1", "second");
    app.enqueue_send("s1", "third");
    // A different, IDLE session gets one send — its queue is independent.
    app.enqueue_send("s2", "other");

    CHECK(app.pending_send_count("s1") == 3);
    CHECK(app.pending_send_count("s2") == 1);

    // While s1 is busy, the loader dispatches nothing FOR s1 but drains s2 (its
    // one queued send) since s2 is free. Emulate the loader's drain predicate.
    auto drain_one = [&](const std::string& openId) -> std::string {
        for (auto it = app.pendingSendQueue.begin();
             it != app.pendingSendQueue.end(); ++it) {
            if (it->sessionId != openId) continue;      // only the open thread
            if (app.sending_for(it->sessionId)) continue;  // busy: keep queued
            std::string p = it->prompt;
            app.pendingSendQueue.erase(it);
            return p;
        }
        return "";
    };

    // Open s2 -> its one send drains.
    CHECK(drain_one("s2") == "other");
    CHECK(app.pending_send_count("s2") == 0);

    // s1 still busy -> nothing drains even when it's the open thread.
    CHECK(drain_one("s1").empty());
    CHECK(app.pending_send_count("s1") == 3);

    // The reply completes -> s1 is free -> sends drain in FIFO order.
    app.sendPending = false;
    app.sendSessionId.clear();
    CHECK(!app.sending_for("s1"));
    CHECK(drain_one("s1") == "first");
    CHECK(drain_one("s1") == "second");
    CHECK(drain_one("s1") == "third");
    CHECK(app.pending_send_count("s1") == 0);
    CHECK(app.pendingSendQueue.empty());
}

// sending_for() must cover the streaming paths too (collect + active).
static void test_sending_for_covers_stream() {
    std::printf("test_sending_for_covers_stream\n");
    ecs::AppComponent app;
    CHECK(!app.sending_for("z"));
    app.streamCollecting = true;
    app.streamPendingSession = "z";
    CHECK(app.sending_for("z"));
    app.streamCollecting = false;
    app.streamPendingSession.clear();
    app.streamActive = true;
    app.streamSessionId = "z";
    CHECK(app.sending_for("z"));
    CHECK(!app.sending_for("other"));
}

// --- (3) newest-N windowing still correct ---------------------------------
static void test_newest_n_window() {
    std::printf("test_newest_n_window\n");
    setenv("HANABI_BIG_TRANSCRIPT", "1", 1);
    api::MockClient m;
    // Full transcript (no limit).
    auto full = m.get_session("rbig");
    CHECK(full.ok);
    const size_t total = full.value.messages.size();
    CHECK(total > 40);  // the big fixture is large enough to window
    CHECK(!full.value.has_more_older);  // full load => complete

    // Windowed to newest 10: exactly the LAST 10 messages, in order, with
    // has_more_older set.
    auto win = m.get_session("rbig", 10);
    CHECK(win.ok);
    CHECK(win.value.messages.size() == 10);
    CHECK(win.value.has_more_older);
    // The windowed messages are the tail of the full transcript (same ids,
    // same order — newest-N is a suffix, not a reshuffle).
    for (size_t i = 0; i < 10; ++i) {
        CHECK(win.value.messages[i].id ==
              full.value.messages[total - 10 + i].id);
    }

    // A limit >= total returns everything with has_more_older == false.
    auto all = m.get_session("rbig", static_cast<int>(total) + 100);
    CHECK(all.ok);
    CHECK(all.value.messages.size() == total);
    CHECK(!all.value.has_more_older);
    unsetenv("HANABI_BIG_TRANSCRIPT");
}

// --- (4) settings read from the api ---------------------------------------
static void test_settings_read_mock() {
    std::printf("test_settings_read_mock\n");
    api::MockClient m;
    CHECK(m.supports_settings());
    auto r = m.get_settings();
    CHECK(r.ok);
    CHECK(r.value.ok);
    CHECK(!r.value.user_id.empty());
    CHECK(!r.value.bank_id.empty());
    CHECK(!r.value.raw_json.empty());
    CHECK(r.value.session_count >= 0);
}

// The http adapter's settings config is opt-in but defaults to a reachable
// path; a base-URL-less config is honestly "not ready".
static void test_settings_config_gate() {
    std::printf("test_settings_config_gate\n");
    api::Config c;               // no base_url
    CHECK(!c.settings_ready());  // no base url => not ready
    c.base_url = "https://example.invalid/api";
    CHECK(c.settings_ready());   // default settings_path "/whoami" is set
    c.settings_path.clear();
    CHECK(!c.settings_ready());  // cleared path => opt-out
}

// --- (5) composer footer figures --------------------------------------------
// The composer prints the open thread's size as "~4.2k tokens". Both halves are
// pure: the char->token estimate and the short-count formatting.
static void test_compact_count() {
    std::printf("test_compact_count\n");
    CHECK(fmtutil::compact_count(0) == "0");
    CHECK(fmtutil::compact_count(940) == "940");
    CHECK(fmtutil::compact_count(999) == "999");
    CHECK(fmtutil::compact_count(1000) == "1k");
    CHECK(fmtutil::compact_count(4200) == "4.2k");
    CHECK(fmtutil::compact_count(4999) == "4.9k");   // truncates, never rounds up
    CHECK(fmtutil::compact_count(10500) == "10k");   // no decimal past 10k
    CHECK(fmtutil::compact_count(130000) == "130k");
    CHECK(fmtutil::compact_count(2500000) == "2M");
    CHECK(fmtutil::compact_count(-1).empty());
}

static void test_clock_time() {
    std::printf("test_clock_time\n");
    CHECK(fmtutil::clock_time(0).empty());     // unset stamp prints nothing
    CHECK(fmtutil::clock_time(-5).empty());
    const std::string t = fmtutil::clock_time(1700000000);
    CHECK(t.size() == 5);                      // HH:MM
    CHECK(t[2] == ':');
    CHECK(t[0] >= '0' && t[0] <= '2');
    // Whatever the machine's zone, an hour later reads an hour later.
    const std::string later = fmtutil::clock_time(1700000000 + 3600);
    CHECK(later != t);
    CHECK(later.substr(3) == t.substr(3));     // same minute-of-hour
}

int main() {
    std::printf("=== test_data ===\n");
    test_disk_cache_total_and_wipe();
    test_message_queue_ordering();
    test_sending_for_covers_stream();
    test_newest_n_window();
    test_settings_read_mock();
    test_settings_config_gate();
    test_compact_count();
    test_clock_time();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
