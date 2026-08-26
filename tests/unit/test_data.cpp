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
#include "../../src/util/textscan.h"
#include "../../src/ui/slash_commands.h"
#include "../../src/ui/model_menu.h"

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

    // A tool row's STATUS survives the round trip. This is not a field the
    // transcript draws differently — it is the one find's `state:` operator
    // resolves through (find_operators.h, tool_state_of). Dropped, every
    // state: query against a restored thread answered "no matches", as a
    // VALID query, so the bar rendered no hint saying why. docs/SEARCH.md S4.
    api::Session s3;
    s3.summary.id = "sess-c";
    api::Message tm;
    tm.id = "t0";
    tm.role = api::Role::Tool;
    tm.subtitle = "shell";
    tm.text = "make test";
    tm.tool_status = "failed";
    s3.messages.push_back(tm);
    api::disk_cache::save_transcript(s3);
    auto restored = api::disk_cache::load_transcript("sess-c");
    CHECK(restored.has_value());
    CHECK(restored && restored->messages.size() == 1);
    CHECK(restored && restored->messages[0].tool_status == "failed");

    // wipe_all removes exactly our cache files (sessions.json + 3 transcripts).
    std::size_t removed = api::disk_cache::wipe_all();
    CHECK(removed == 4);
    CHECK(api::disk_cache::total_bytes() == 0);
    // After a wipe the transcript is gone (forces a cold refetch).
    CHECK(!api::disk_cache::load_transcript("sess-a").has_value());

    unsetenv("HANABI_CACHE_DIR");
}

// --- (1b) the content-search memo: fast, and never stale ------------------
//
// content_matches is asked for every non-title-matching session on every frame
// a sidebar query is live, and it answers by reading a file. It is memoized on
// (id, query), which is only safe if the memo can see the corpus change --
// so what this pins is the INVALIDATION, not the speed. A stale `false` is a
// thread that has silently dropped out of your search results, which is worse
// than a slow search.
static void test_content_search_memo_is_not_stale() {
    std::printf("test_content_search_memo_is_not_stale\n");
    std::string dir = "/tmp/hanabi_test_search_" + std::to_string(::getpid());
    setenv("HANABI_CACHE_DIR", dir.c_str(), 1);
    api::disk_cache::set_namespace("");
    api::disk_cache::wipe_all();

    const auto write = [](const std::string& id, const std::string& body) {
        api::Session s;
        s.summary.id = id;
        api::Message m;
        m.id = "m1";
        m.text = body;
        s.messages.push_back(m);
        api::disk_cache::save_transcript(s);
    };

    write("t1", "the retry queue is still draining");
    write("t2", "nothing to see in this one");

    CHECK(api::disk_cache::content_matches("t1", "retry"));
    CHECK(!api::disk_cache::content_matches("t2", "retry"));
    // Asked twice, same answer -- this is the memo's hit path.
    CHECK(api::disk_cache::content_matches("t1", "retry"));
    CHECK(!api::disk_cache::content_matches("t2", "retry"));

    // THE INVALIDATION, and the ORDER of these lines is the test.
    //
    // The memo is dropped whenever the query changes, so a check that wanders
    // off to another query first has already thrown away the entry it meant to
    // catch going stale -- and then passes with the invalidation deleted,
    // which is how the first draft of this test proved nothing. Nothing
    // between the read and the rewrite may touch content_matches.
    //
    // A remembered MISS, against a file that now contains the word:
    write("t2", "the retry queue moved here");
    CHECK(api::disk_cache::content_matches("t2", "retry"));
    // ...and a remembered HIT, against a file that no longer does:
    write("t1", "nothing about queues at all");
    CHECK(!api::disk_cache::content_matches("t1", "retry"));

    // NARROWING. Typing extends the query, and the memo keeps the previous
    // query's MISSES on the argument that a longer query cannot match where a
    // shorter substring of it did not. What it must never keep is a HIT: t1
    // contains "queues", so it matches "q" and does not match "qz", and a
    // carried-forward hit would report the wrong one.
    CHECK(api::disk_cache::content_matches("t1", "q"));
    CHECK(!api::disk_cache::content_matches("t1", "qz"));
    // The kept misses have to be right too: t2 matches every prefix of a
    // phrase it contains, in the order a person types them.
    CHECK(api::disk_cache::content_matches("t2", "m"));
    CHECK(api::disk_cache::content_matches("t2", "mo"));
    CHECK(api::disk_cache::content_matches("t2", "mov"));
    CHECK(api::disk_cache::content_matches("t2", "move"));
    // And a miss under a narrow query must not survive a WIDENING one, which
    // is the direction backspacing goes: "zzq" misses, "q" hits.
    CHECK(!api::disk_cache::content_matches("t1", "zzq"));
    CHECK(api::disk_cache::content_matches("t1", "q"));

    // The FILE is matched case-insensitively; the QUERY is not folded -- the
    // parameter is named lowerQuery and the caller lowercases (the sidebar
    // does). Pinned in both directions because the name is the only thing
    // saying so, and an unfolded query silently matching nothing is the kind
    // of bug that reads as "search is broken for capital letters".
    CHECK(api::disk_cache::content_matches("t1", "queues"));
    CHECK(!api::disk_cache::content_matches("t1", "QUEUES"));
    CHECK(!api::disk_cache::content_matches("t3", "queues"));  // no transcript
    CHECK(!api::disk_cache::content_matches("t1", ""));

    // A wipe is a corpus change too, and it happens with a live memo.
    CHECK(api::disk_cache::content_matches("t2", "retry"));
    api::disk_cache::wipe_all();
    CHECK(!api::disk_cache::content_matches("t2", "retry"));

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
    // Millions get the decimal thousands get. Without it a 1M-token budget
    // renders every reading from 1.0M to 1.9M as the same "1M".
    CHECK(fmtutil::compact_count(1000000) == "1M");
    CHECK(fmtutil::compact_count(1500000) == "1.5M");
    CHECK(fmtutil::compact_count(1999999) == "1.9M");  // truncates here too
    CHECK(fmtutil::compact_count(2500000) == "2.5M");
    CHECK(fmtutil::compact_count(12500000) == "12M");  // no decimal past 10M
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

// --- (6) find-in-conversation match scanning --------------------------------
// The offsets the highlight paints from. Case-insensitive, non-overlapping.
// --- (6) transcript date rows -----------------------------------------------
// The divider asks two questions of a pair of stamps: are they the same local
// day, and what is that day called. Both are pure, and both are wrong in ways
// a screenshot cannot show — an hours-apart rule that straddles midnight, or a
// zero-padded "August 09".
static void test_day_boundaries_and_labels() {
    std::printf("test_day_boundaries_and_labels\n");
    // Local noon today, so every offset below stays inside the day it means.
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    tm.tm_hour = 12; tm.tm_min = 0; tm.tm_sec = 0; tm.tm_isdst = -1;
    const int64_t noon = static_cast<int64_t>(std::mktime(&tm));

    CHECK(fmtutil::same_local_day(noon, noon + 3600));
    CHECK(fmtutil::same_local_day(noon - 6 * 3600, noon + 6 * 3600));
    // Eleven hours apart, but either side of midnight: a different day.
    CHECK(!fmtutil::same_local_day(noon + 8 * 3600, noon + 19 * 3600));
    CHECK(!fmtutil::same_local_day(noon, noon - 86400));

    CHECK(fmtutil::day_label(noon, noon) == "Today");
    CHECK(fmtutil::day_label(noon + 8 * 3600, noon) == "Today");
    CHECK(fmtutil::day_label(noon - 86400, noon) == "Yesterday");
    CHECK(fmtutil::day_label(0, noon).empty());   // unset stamp names no day

    // Older than yesterday: a named day, no zero-padding, and the year only
    // when it is not this one.
    const std::string old = fmtutil::day_label(noon - 5 * 86400, noon);
    CHECK(old != "Today" && old != "Yesterday");
    CHECK(old.find(", ") != std::string::npos);   // "Monday, August 19"
    CHECK(old.find(" 0") == std::string::npos);   // never "August 09"
    const std::string lastYear = fmtutil::day_label(noon - 400 * 86400, noon);
    CHECK(lastYear.find("20") != std::string::npos);  // carries its year
}

static void test_find_occurrences() {
    std::printf("test_find_occurrences\n");
    using textscan::occurrences;
    CHECK(occurrences("", "x").empty());
    CHECK(occurrences("abc", "").empty());
    CHECK(occurrences("abc", "abcd").empty());        // needle longer than hay
    {
        auto o = occurrences("ledger and Ledger and LEDGER", "ledger");
        CHECK(o.size() == 3);
        if (o.size() == 3) {
            CHECK(o[0] == 0);
            CHECK(o[1] == 11);
            CHECK(o[2] == 22);
        }
    }
    {
        // Non-overlapping: "aa" occurs twice in "aaaa", not three times.
        auto o = occurrences("aaaa", "aa");
        CHECK(o.size() == 2);
        if (o.size() == 2) { CHECK(o[0] == 0); CHECK(o[1] == 2); }
    }
    {
        auto o = occurrences("acct 8842 - ledger $128.60", "LEDGER");
        CHECK(o.size() == 1);
        if (o.size() == 1) CHECK(o[0] == 12);
    }
}

// --- slash-command parsing ------------------------------------------------
static void test_slash_parsing() {
    std::printf("test_slash_parsing\n");
    namespace sl = hanabi::slash;

    // A verb and its argument, verbatim after the space.
    {
        auto p = sl::parse("/btw Why did it refuse?");
        CHECK(p.matched);
        CHECK(p.known);
        CHECK(p.verb == "btw");
        CHECK(p.args == "Why did it refuse?");
    }
    // A verb with the space typed but nothing after it.
    {
        auto p = sl::parse("/btw ");
        CHECK(p.matched);
        CHECK(p.verb == "btw");
        CHECK(p.args.empty());
    }
    // A slash and a space is a sentence, not a command.
    {
        auto p = sl::parse("/ btw");
        CHECK(!p.matched);
    }
    // The slash has to be first: a message that mentions a path is not one.
    CHECK(!sl::parse("see /etc/hosts").matched);
    CHECK(!sl::is_command_text("see /etc/hosts"));
    // A bare slash is a command in progress with no verb yet.
    {
        auto p = sl::parse("/");
        CHECK(p.matched);
        CHECK(p.verb.empty());
        CHECK(!p.known);
    }
    // Case does not decide whether a verb is recognized.
    CHECK(sl::parse("/COMPACT").known);
    // A verb this client does not have is parsed but not known.
    CHECK(!sl::parse("/autocompact").known);

    // The menu offers everything for a bare slash, narrows on the prefix, and
    // has nothing to say once the argument has started.
    CHECK(sl::filter("/").size() == sl::all().size());
    {
        auto rows = sl::filter("/mod");
        CHECK(rows.size() == 1);
        if (rows.size() == 1) CHECK(rows[0]->name == "model");
    }
    CHECK(sl::filter("/zzz").empty());
    CHECK(sl::filter("/model gpt").empty());

    // Completing a verb that wants an argument leaves room to type it.
    CHECK(sl::completion(*sl::find("model")) == "/model ");
    CHECK(sl::completion(*sl::find("new")) == "/new");

    // /rename is deliberately absent while rename_v1's request flag is being
    // built elsewhere: it must not turn into a second wire call.
    CHECK(sl::find("rename") == nullptr);
    // Only /new can actually be carried out today.
    for (const auto& c : sl::all())
        CHECK(c.runnable == (c.name == "new"));
}

// --- the model menu -------------------------------------------------------
static void test_model_menu() {
    std::printf("test_model_menu\n");
    namespace mm = hanabi::models;

    // The stored default is the first entry, so a fresh install has a real
    // answer rather than an id off the menu.
    CHECK(mm::all().front().id == "default");
    CHECK(mm::display_name("default") == "Server default");

    // Ids are the gateway's own spelling — the tidier "muse-spark-1.2" is a
    // 404, so the suffix has to survive.
    CHECK(mm::index_of("muse-spark-1.2-internal") < mm::all().size());
    CHECK(mm::index_of("muse-spark-1.2") == mm::all().size());

    CHECK(mm::display_name("claude-sonnet-5") == "Sonnet 5");
    // An id the curated menu does not carry shows as itself: family routing
    // takes ids this list does not, and a later build may have written one.
    CHECK(mm::display_name("claude-opus-9") == "claude-opus-9");
    // The retired "fast"/"reasoning" tokens were never served by anything;
    // they now read as themselves rather than posing as a real model.
    CHECK(mm::index_of("fast") == mm::all().size());
}

int main() {
    std::printf("=== test_data ===\n");
    test_disk_cache_total_and_wipe();
    test_message_queue_ordering();
    test_sending_for_covers_stream();
    test_newest_n_window();
    test_content_search_memo_is_not_stale();
    test_settings_read_mock();
    test_settings_config_gate();
    test_compact_count();
    test_clock_time();
    test_day_boundaries_and_labels();
    test_find_occurrences();
    test_slash_parsing();
    test_model_menu();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
