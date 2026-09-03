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
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#define AFTER_HOURS_ENTITY_HELPER
#define AFTER_HOURS_ENTITY_QUERY
#define AFTER_HOURS_SYSTEM
#include "../../vendor/afterhours/src/ecs.h"

#include "../../src/api/disk_cache.h"
#include "../../src/api/mock_client.h"
#include "../../src/ecs/components.h"
#include "../../src/ecs/pane_state.h"
#include "../../src/ecs/thread_model.h"
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

    api::disk_cache::save_draft("sess-a", "half typed");
    api::disk_cache::outbox_add("sess-b", "send after restart");
    {
        std::ofstream(dir + "/config.json") << "config";
        std::ofstream(dir + "/token.json") << "token";
        std::ofstream(dir + "/keep.me") << "owned by someone else";
        std::ofstream(dir + "/tx_corrupt.json") << "{";
    }
    CHECK(!api::disk_cache::load_transcript("corrupt").has_value());

    const std::uint64_t beforeWipe = api::disk_cache::total_bytes();
    const std::uint64_t epochBefore = api::disk_cache::epoch();
    const auto wiped = api::disk_cache::wipe_all_report();
    CHECK(wiped.files_removed == 5);
    CHECK(wiped.bytes_reclaimed == beforeWipe);
    CHECK(wiped.bytes_remaining == 0);
    CHECK(api::disk_cache::total_bytes() == 0);
    CHECK(api::disk_cache::epoch() != epochBefore);
    CHECK(!api::disk_cache::load_transcript("sess-a").has_value());
    CHECK(api::disk_cache::load_draft("sess-a") == "half typed");
    const auto held = api::disk_cache::outbox_list("sess-b");
    CHECK(held.size() == 1 && held[0] == "send after restart");
    CHECK(std::filesystem::exists(dir + "/config.json"));
    CHECK(std::filesystem::exists(dir + "/token.json"));
    CHECK(std::filesystem::exists(dir + "/keep.me"));

    std::filesystem::remove_all(dir);
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

// --- (1c) the sidebar's deep search matches the CONVERSATION --------------
//
// It used to lowercase the whole cache file and call find() on it. The file is
// a JSON document, so its own field names were in the corpus: `state`, `tag`,
// `preview`, `subtitle`, `folder`, `messages`, `has_more_older` and every
// session id matched every thread that had ever been cached. Title matching
// runs first, so it only fired once the title had missed -- which is exactly
// when a false hit reads as a real deep hit. docs/SEARCH.md S3.
//
// Every assertion below that a STRUCTURE word misses was true in reverse
// before the fix.
static void test_content_search_matches_values_not_the_document() {
    std::printf("test_content_search_matches_values_not_the_document\n");
    std::string dir = "/tmp/hanabi_test_json_" + std::to_string(::getpid());
    setenv("HANABI_CACHE_DIR", dir.c_str(), 1);
    api::disk_cache::set_namespace("");
    api::disk_cache::wipe_all();

    api::Session s;
    s.summary.id = "quota-42";
    s.summary.title = "quota migration";
    s.summary.preview = "week three";
    s.summary.folder = "billing";
    api::Message m;
    m.id = "m1";
    m.role = api::Role::Assistant;
    m.subtitle = "thinking";
    m.text = "the quota ledger reconciled cleanly";
    m.tool_status = "completed";
    s.messages.push_back(m);
    api::Message t;
    t.id = "m2";
    t.role = api::Role::Tool;
    t.text = "make test";
    t.tool_result = "214 passed, 0 failed, xylophone";
    s.messages.push_back(t);
    api::disk_cache::save_transcript(s);

    // What the user said is still found.
    CHECK(api::disk_cache::content_matches("quota-42", "ledger"));
    CHECK(api::disk_cache::content_matches("quota-42", "reconciled cleanly"));
    // Including a tool row's own text, which is a message like any other.
    CHECK(api::disk_cache::content_matches("quota-42", "make test"));

    // The document's structure is not the conversation. Every one of these
    // matched before, on any thread with a cached transcript.
    CHECK(!api::disk_cache::content_matches("quota-42", "state"));
    CHECK(!api::disk_cache::content_matches("quota-42", "tag"));
    CHECK(!api::disk_cache::content_matches("quota-42", "preview"));
    CHECK(!api::disk_cache::content_matches("quota-42", "subtitle"));
    CHECK(!api::disk_cache::content_matches("quota-42", "folder"));
    CHECK(!api::disk_cache::content_matches("quota-42", "starred"));
    CHECK(!api::disk_cache::content_matches("quota-42", "messages"));
    CHECK(!api::disk_cache::content_matches("quota-42", "has_more_older"));
    CHECK(!api::disk_cache::content_matches("quota-42", "version"));
    CHECK(!api::disk_cache::content_matches("quota-42", "created_at"));
    // The session id is written into the file twice and is not a word anybody
    // said.
    CHECK(!api::disk_cache::content_matches("quota-42", "quota-42"));
    // Neither are the values of the fields that are not the body: the folder
    // name, the tool status, the reasoning subtitle.
    CHECK(!api::disk_cache::content_matches("quota-42", "billing"));
    CHECK(!api::disk_cache::content_matches("quota-42", "completed"));
    CHECK(!api::disk_cache::content_matches("quota-42", "thinking"));
    // And tool OUTPUT is not searchable, because it is not persisted at all --
    // the comment that used to sit on this function said it was.
    CHECK(!api::disk_cache::content_matches("quota-42", "xylophone"));

    // Not a blocklist: the same words match when somebody actually said them.
    api::Session s2;
    s2.summary.id = "words";
    api::Message m2;
    m2.id = "m1";
    m2.text = "the state machine stalled; check the folder and the preview";
    s2.messages.push_back(m2);
    api::disk_cache::save_transcript(s2);
    CHECK(api::disk_cache::content_matches("words", "state"));
    CHECK(api::disk_cache::content_matches("words", "folder"));
    CHECK(api::disk_cache::content_matches("words", "preview"));

    // The value is DECODED, not read as it sits on disk: a body with a quote
    // and a newline in it is stored escaped, and a query has neither.
    api::Session s3;
    s3.summary.id = "escapes";
    api::Message m3;
    m3.id = "m1";
    m3.text = "he said \"deploy it\"\nand left";
    s3.messages.push_back(m3);
    api::disk_cache::save_transcript(s3);
    CHECK(api::disk_cache::content_matches("escapes", "said \"deploy it\""));
    CHECK(api::disk_cache::content_matches("escapes", "deploy it\"\nand left"));
    // ...and the escape sequence itself is not text anybody typed.
    CHECK(!api::disk_cache::content_matches("escapes", "\\n"));

    api::disk_cache::wipe_all();
    unsetenv("HANABI_CACHE_DIR");
}

static void test_cache_wipe_keeps_visible_panes_and_rejects_old_reads() {
    std::printf("test_cache_wipe_keeps_visible_panes_and_rejects_old_reads\n");
    ecs::AppComponent app;
    api::Session left;
    left.summary.id = "left";
    left.messages.push_back(api::Message{"m1", api::Role::User, "draft context"});
    api::Session right;
    right.summary.id = "right";
    right.messages.push_back(api::Message{"m2", api::Role::Assistant, "visible reply"});
    app.panes[0].selectedId = left.summary.id;
    app.panes[0].openSession = left;
    app.panes[0].findOpen = true;
    app.panes[0].findQuery = "draft";
    app.panes[1].selectedId = right.summary.id;
    app.panes[1].openSession = right;
    app.expandedPiles.insert("tool-pile");
    app.expandedMsgs.insert("long-message");
    app.expandedThinking.insert("thinking-message");
    ecs::model::pane_states().clear();
    auto& state = ecs::model::pane_states().touch(
        ecs::model::pane_key(0, left.summary.id));
    state.replyDraft = "unsent reply";
    state.unreadComputed = true;
    state.unreadFirst = 1;
    state.latch.follow = false;
    app.transcriptCache.put(left);
    app.transcriptCache.put(right);

    const std::uint64_t oldEpoch = api::disk_cache::epoch();
    app.panes[0].diskReadPending = true;
    app.panes[0].diskReadId = left.summary.id;
    app.panes[0].diskReadEpoch = oldEpoch;
    app.clear_transcript_cache();
    api::disk_cache::wipe_all();

    CHECK(app.transcriptCache.empty());
    CHECK(app.panes[0].openSession.has_value());
    CHECK(app.panes[1].openSession.has_value());
    CHECK(app.panes[0].openSession->messages[0].text == "draft context");
    CHECK(app.panes[1].openSession->messages[0].text == "visible reply");
    CHECK(app.panes[0].findOpen && app.panes[0].findQuery == "draft");
    CHECK(app.expandedPiles.count("tool-pile") == 1);
    CHECK(app.expandedMsgs.count("long-message") == 1);
    CHECK(app.expandedThinking.count("thinking-message") == 1);
    const auto* kept = ecs::model::pane_states().peek(
        ecs::model::pane_key(0, left.summary.id));
    CHECK(kept != nullptr && kept->replyDraft == "unsent reply");
    CHECK(kept != nullptr && kept->unreadComputed && kept->unreadFirst == 1);
    CHECK(kept != nullptr && !kept->latch.follow);
    CHECK(!app.panes[0].diskReadPending);
    CHECK(!app.panes[0].accepts_disk_read("left", oldEpoch,
                                         api::disk_cache::epoch()));
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

    CHECK(sl::completion(*sl::find("model")) == "/model");
    CHECK(sl::completion(*sl::find("new")) == "/new");

    CHECK(sl::find("rename") == nullptr);
    for (const auto& c : sl::all())
        CHECK(c.runnable == (c.name == "new" || c.name == "model" ||
                             c.name == "effort" || c.name == "btw"));

    CHECK(sl::btw_title("why?") == "BTW: why?");
    CHECK(sl::btw_title("  why   now?  ") == "BTW: why now?");
    CHECK(sl::btw_title("\xE2\x80\x8B\xE2\x80\xAE\n") == "BTW: Question");
    CHECK(sl::btw_title("what about \xF0\x9F\x94\xA5?") ==
          "BTW: what about \xF0\x9F\x94\xA5?");
    std::string longPrompt(240, 'a');
    CHECK(sl::btw_title(longPrompt).size() == 200);
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

static void test_mock_forks_and_subagent_catalog() {
    std::printf("test_mock_forks_and_subagent_catalog\n");
    api::MockClient mock;
    const auto sourceBefore = mock.get_session("t2");
    CHECK(sourceBefore.ok);
    const std::size_t sourceMessages =
        sourceBefore.ok ? sourceBefore.value.messages.size() : 0;

    const auto fork = mock.fork_with_prompt("t2", "why did it refuse?",
                                            "BTW: why did it refuse?");
    CHECK(fork.ok);
    if (fork.ok) {
        const auto destination = mock.get_session(fork.value);
        CHECK(destination.ok);
        if (destination.ok) {
            CHECK(destination.value.summary.forked_from == "t2");
            CHECK(destination.value.summary.title == "BTW: why did it refuse?");
            CHECK(destination.value.messages.size() == sourceMessages + 1);
            CHECK(destination.value.messages.back().role == api::Role::User);
            CHECK(destination.value.messages.back().text ==
                  "why did it refuse?");
            CHECK(destination.value.sub_agents.empty());
            CHECK(destination.value.summary.state == api::ThreadState::Running);
        }
    }
    const auto sourceAfter = mock.get_session("t2");
    CHECK(sourceAfter.ok);
    if (sourceAfter.ok)
        CHECK(sourceAfter.value.messages.size() == sourceMessages);

    const auto bare = mock.fork_session("t2");
    CHECK(bare.ok);
    if (bare.ok) {
        const auto destination = mock.get_session(bare.value);
        CHECK(destination.ok);
        if (destination.ok)
            CHECK(destination.value.messages.size() == sourceMessages);
        CHECK(destination.value.sub_agents.empty());
        CHECK(destination.value.summary.title ==
              sourceBefore.value.summary.title);
    }

    const auto children = mock.list_subagents(2);
    CHECK(children.ok);
    if (children.ok) {
        CHECK(children.value.size() == 2);
        for (const auto& child : children.value)
            CHECK(!child.parent_id.empty());
        if (!children.value.empty())
            CHECK(mock.get_session(children.value.front().id).ok);
    }
}

// --- (1b) disk cache: the brakes survive a restart ------------------------
// A frozen thread that came back unfrozen from the cache would draw the wrong
// mark and take a message the server will never answer, for the whole window
// between startup and the first refresh. Both cache files share one
// serializer, so both are checked here; and a file written by a build that
// predates these fields must still load, as a thread with no brakes.
static void test_disk_cache_round_trips_the_brakes() {
    std::printf("test_disk_cache_round_trips_the_brakes\n");
    std::string dir = "/tmp/hanabi_test_brakes_" + std::to_string(::getpid());
    setenv("HANABI_CACHE_DIR", dir.c_str(), 1);
    api::disk_cache::set_namespace("");
    api::disk_cache::wipe_all();

    api::SessionSummary frozen;
    frozen.id = "ice";
    frozen.title = "canary cohort rollout";
    frozen.frozen = true;
    frozen.frozen_by = "root7";
    frozen.frozen_reason = "canary owner is reviewing";
    api::SessionSummary paused;
    paused.id = "quiet";
    paused.title = "nightly digest";
    paused.replies_paused = true;
    api::SessionSummary filed;
    filed.id = "filed";
    filed.title = "filed from the web";
    filed.server_archived_at_ms = 1781520000000LL;
    api::disk_cache::save_sessions({frozen, paused, filed});

    auto rows = api::disk_cache::load_sessions();
    CHECK(rows.has_value());
    if (!rows.has_value()) return;
    CHECK(rows->size() == 3);
    if (rows->size() != 3) return;
    CHECK((*rows)[0].frozen);
    CHECK((*rows)[0].frozen_by == "root7");
    CHECK((*rows)[0].frozen_reason == "canary owner is reviewing");
    CHECK(!(*rows)[0].replies_paused);
    CHECK((*rows)[1].replies_paused);
    CHECK(!(*rows)[1].frozen);
    CHECK((*rows)[2].server_archived_at_ms == 1781520000000LL);
    // The whole point of persisting them: the mark and the archive view are
    // right on the first frame, before any refresh has happened.
    CHECK(ecs::model::status_glyph((*rows)[0]) ==
          ecs::model::StatusGlyph::Frozen);
    CHECK(ecs::model::status_glyph((*rows)[1]) ==
          ecs::model::StatusGlyph::Paused);
    CHECK(ecs::model::is_archived((*rows)[2]));

    // The transcript cache uses the same serializer, so it carries them too.
    api::Session s;
    s.summary = frozen;
    s.messages.push_back({"m1", api::Role::User, "hello", 1000, ""});
    s.plan = api::SessionPlan{
        "Ship", {{"s1", "Verify", "running now",
                   api::SessionPlanStep::Status::InProgress}}, 3};
    s.goal = api::SessionGoal{"Ship the fix", "tests pass",
                              api::GoalPhase::Active, "", "user", 2};
    api::disk_cache::save_transcript(s);
    auto back = api::disk_cache::load_transcript("ice");
    CHECK(back.has_value());
    CHECK(back->summary.frozen);
    CHECK(back->summary.frozen_by == "root7");
    CHECK(back->summary.frozen_reason == "canary owner is reviewing");
    CHECK(back->plan.has_value());
    CHECK(back->plan->title == "Ship");
    CHECK(back->plan->current() != nullptr);
    CHECK(back->plan->current()->note == "running now");
    CHECK(back->goal.has_value());
    CHECK(back->goal->objective == "Ship the fix");
    CHECK(back->goal->done_when == "tests pass");

    api::disk_cache::wipe_all();
}

// A cache file written before these fields existed has none of the keys. It
// must load as a thread with no brakes rather than failing to load.
static void test_an_old_cache_file_still_loads() {
    std::printf("test_an_old_cache_file_still_loads\n");
    std::string dir = "/tmp/hanabi_test_oldcache_" + std::to_string(::getpid());
    setenv("HANABI_CACHE_DIR", dir.c_str(), 1);
    api::disk_cache::set_namespace("");
    api::disk_cache::wipe_all();

    // Exactly the shape the previous build wrote: nine keys, no brakes.
    const std::string legacy =
        R"({"sessions":[{"id":"old","title":"Before the brakes",)"
        R"("updated_at":1781520000,"status":"idle","preview":"",)"
        R"("state":0,"tag":0,"folder":"","starred":false}]})";
    std::filesystem::create_directories(api::disk_cache::cache_dir());
    const std::string path = api::disk_cache::cache_dir() + "/sessions.json";
    std::ofstream(path) << legacy;

    auto rows = api::disk_cache::load_sessions();
    CHECK(rows.has_value());
    if (!rows.has_value()) return;
    CHECK(rows->size() == 1);
    if (rows->size() != 1) return;
    CHECK((*rows)[0].id == "old");
    CHECK((*rows)[0].title == "Before the brakes");
    CHECK(!(*rows)[0].frozen);
    CHECK((*rows)[0].frozen_by.empty());
    CHECK((*rows)[0].frozen_reason.empty());
    CHECK(!(*rows)[0].replies_paused);
    CHECK((*rows)[0].server_archived_at_ms == 0);
    CHECK(!ecs::model::is_archived((*rows)[0]));

    api::disk_cache::wipe_all();
}

// THE RESTART PATH, end to end: a paused thread must survive a relaunch AND
// the key-absent catalog refresh that immediately follows it.
//
// The sequence that broke it: the cache restores `replies_paused=true`, the
// app paints it, the first poll returns rows with no such key (the server
// never sends it on a summary row), the fresh rows REPLACE the cached ones,
// and the mark is gone -- then the re-save writes the unpaused rows back, so
// the next launch has lost it too. Seeding the overlay from the cache at
// restore time is what closes it, and this walks the whole loop.
static void test_paused_survives_restart_and_the_next_refresh() {
    std::printf("test_paused_survives_restart_and_the_next_refresh\n");
    std::string dir = "/tmp/hanabi_test_restart_" + std::to_string(::getpid());
    setenv("HANABI_CACHE_DIR", dir.c_str(), 1);
    api::disk_cache::set_namespace("");
    api::disk_cache::wipe_all();

    // Launch 1 learned the pause on an attach and saved the catalog.
    api::SessionSummary paused;
    paused.id = "quiet";
    paused.title = "nightly digest";
    paused.replies_paused = true;
    api::SessionSummary other;
    other.id = "busy";
    other.title = "profiling the disk";
    api::disk_cache::save_sessions({paused, other});

    // Launch 2: restore from cache, and SEED the overlay from what it carries.
    ecs::AppComponent app;
    auto restored = api::disk_cache::load_sessions();
    CHECK(restored.has_value());
    if (!restored.has_value()) return;
    app.seed_attach_brakes_from(*restored);
    app.overlay_attach_brakes(*restored);
    CHECK((*restored)[0].replies_paused);
    CHECK(ecs::model::status_glyph((*restored)[0]) ==
          ecs::model::StatusGlyph::Paused);

    // The first poll of the new launch. The server sends no such key -- this
    // is exactly what the real catalog looks like -- and these rows replace
    // the restored ones.
    std::vector<api::SessionSummary> fresh;
    api::SessionSummary q;
    q.id = "quiet";
    q.title = "nightly digest";
    api::SessionSummary b;
    b.id = "busy";
    b.title = "profiling the disk";
    fresh.push_back(q);
    fresh.push_back(b);
    CHECK(!fresh[0].replies_paused);  // the wire really is silent
    app.overlay_attach_brakes(fresh);
    CHECK(fresh[0].replies_paused);   // ...and the overlay speaks for it
    CHECK(!fresh[1].replies_paused);  // without inventing one for anyone else
    CHECK(ecs::model::status_glyph(fresh[0]) ==
          ecs::model::StatusGlyph::Paused);

    // The re-save writes the OVERLAID rows, so launch 3 starts where launch 2
    // did rather than losing the flag one launch later.
    api::disk_cache::save_sessions(fresh);
    auto again = api::disk_cache::load_sessions();
    CHECK(again.has_value());
    if (!again.has_value()) return;
    CHECK((*again)[0].replies_paused);

    // A resume clears it everywhere: the set, the catalog, and the next save.
    app.replace_sessions(std::vector<api::SessionSummary>(*again));
    app.apply_attach_brakes("quiet", ecs::AppComponent::AttachBrakes{});
    CHECK(app.attachPausedIds.empty());
    std::vector<api::SessionSummary> after = app.sessions;
    app.overlay_attach_brakes(after);
    CHECK(!after[0].replies_paused);

    api::disk_cache::wipe_all();
}


// THE CATALOG IS WHAT THE UI READS. `brake_for` is handed
// `app.find_summary(id)`, not the pane's own copy -- so an attach fact that
// stays on `Session::summary` changes nothing anybody can see. This is the
// stale-row race a live client hits whenever a freeze lands between two polls.
static void test_an_attach_freeze_reaches_the_catalog_row() {
    std::printf("test_an_attach_freeze_reaches_the_catalog_row\n");
    ecs::AppComponent app;

    // The catalog, as the last poll left it: running, no freeze.
    api::SessionSummary stale;
    stale.id = "bz6";
    stale.title = "frozen since the last poll";
    stale.state = api::ThreadState::Running;
    std::vector<api::SessionSummary> rows{stale};
    app.replace_sessions(std::move(rows));
    CHECK(app.find_summary("bz6") != nullptr);
    CHECK(!app.find_summary("bz6")->frozen);
    CHECK(!ecs::model::brake_for("bz6", app.find_summary("bz6"), nullptr)
               .engaged);

    // The attach says otherwise, and it is the fresher answer.
    ecs::AppComponent::AttachBrakes frozen;
    frozen.frozen = true;
    frozen.frozen_by = "bz6";
    frozen.frozen_reason = "frozen after the catalog was read";
    app.apply_attach_brakes("bz6", frozen);

    // The CATALOG row carries it now -- which is what the composer reads.
    const api::SessionSummary* row = app.find_summary("bz6");
    CHECK(row != nullptr);
    if (row == nullptr) return;
    CHECK(row->frozen);
    CHECK(row->frozen_reason == "frozen after the catalog was read");
    CHECK(ecs::model::status_glyph(*row) == ecs::model::StatusGlyph::Frozen);
    const ecs::model::Brake b = ecs::model::brake_for("bz6", row, nullptr);
    CHECK(b.engaged);
    CHECK(b.refuses_input);

    // ...and the NEXT LIVE POLL is allowed to take it away again. `frozen` is
    // a summary-row key, so a row that arrives without it is the server
    // AFFIRMING the thread is not frozen -- not a row that simply cannot
    // speak. Re-applying the remembered freeze here would outvote the server
    // and pin a brake the user could never clear, which is why the sticky
    // overlay covers `replies_paused` and nothing else.
    std::vector<api::SessionSummary> poll{stale};
    CHECK(!poll[0].frozen);
    app.overlay_attach_brakes(poll);
    CHECK(!poll[0].frozen);
    app.replace_sessions(std::move(poll));
    CHECK(!app.find_summary("bz6")->frozen);
    CHECK(!ecs::model::brake_for("bz6", app.find_summary("bz6"), nullptr)
               .engaged);

    // A reattach thaw clears it on the spot too, without waiting for a poll.
    app.apply_attach_brakes("bz6", frozen);
    CHECK(app.find_summary("bz6")->frozen);
    app.apply_attach_brakes("bz6", ecs::AppComponent::AttachBrakes{});
    CHECK(!app.find_summary("bz6")->frozen);
    CHECK(app.find_summary("bz6")->frozen_by.empty());
    CHECK(!ecs::model::brake_for("bz6", app.find_summary("bz6"), nullptr)
               .engaged);
}

// COLD START, then a thaw that happened while the app was closed. The cache
// remembers a freeze; the first live poll does not. The server wins: the mark
// goes, the brake lifts, and the re-save must not write the freeze back --
// otherwise the stale brake reappears on every launch forever.
static void test_a_server_thaw_beats_a_cached_freeze() {
    std::printf("test_a_server_thaw_beats_a_cached_freeze\n");
    std::string dir = "/tmp/hanabi_test_thaw_" + std::to_string(::getpid());
    setenv("HANABI_CACHE_DIR", dir.c_str(), 1);
    api::disk_cache::set_namespace("");
    api::disk_cache::wipe_all();

    api::SessionSummary cached;
    cached.id = "ice";
    cached.title = "canary cohort rollout";
    cached.frozen = true;
    cached.frozen_by = "ice";
    cached.frozen_reason = "under review by the canary owner";
    api::SessionSummary alsoPaused;
    alsoPaused.id = "quiet";
    alsoPaused.title = "nightly digest";
    alsoPaused.replies_paused = true;
    api::disk_cache::save_sessions({cached, alsoPaused});

    // Launch: the cached rows paint immediately, freeze and all.
    ecs::AppComponent app;
    auto restored = api::disk_cache::load_sessions();
    CHECK(restored.has_value());
    if (!restored.has_value()) return;
    app.seed_attach_brakes_from(*restored);
    app.overlay_attach_brakes(*restored);
    CHECK((*restored)[0].frozen);
    CHECK(ecs::model::status_glyph((*restored)[0]) ==
          ecs::model::StatusGlyph::Frozen);
    app.replace_sessions(std::vector<api::SessionSummary>(*restored));

    // The first live poll: the thread was thawed while the app was closed, so
    // the row simply has no `frozen`.
    std::vector<api::SessionSummary> live;
    api::SessionSummary thawed;
    thawed.id = "ice";
    thawed.title = "canary cohort rollout";
    thawed.state = api::ThreadState::Running;
    api::SessionSummary quiet;
    quiet.id = "quiet";
    quiet.title = "nightly digest";
    live.push_back(thawed);
    live.push_back(quiet);
    app.overlay_attach_brakes(live);

    // The freeze is GONE -- the seed did not resurrect it...
    CHECK(!live[0].frozen);
    CHECK(live[0].frozen_by.empty());
    CHECK(ecs::model::status_glyph(live[0]) !=
          ecs::model::StatusGlyph::Frozen);
    CHECK(!ecs::model::brake_for("ice", &live[0], nullptr).engaged);
    // ...while the pause, which no row can carry, is still there.
    CHECK(live[1].replies_paused);
    CHECK(ecs::model::status_glyph(live[1]) ==
          ecs::model::StatusGlyph::Paused);

    app.replace_sessions(std::vector<api::SessionSummary>(live));
    CHECK(!app.find_summary("ice")->frozen);
    CHECK(!ecs::model::brake_for("ice", app.find_summary("ice"), nullptr)
               .engaged);

    // The re-save writes the THAWED row, so the next launch starts clean
    // rather than reviving the freeze from disk.
    api::disk_cache::save_sessions(app.sessions);
    auto next = api::disk_cache::load_sessions();
    CHECK(next.has_value());
    if (!next.has_value()) return;
    for (const auto& s : *next) {
        if (s.id == "ice") CHECK(!s.frozen);
        if (s.id == "quiet") CHECK(s.replies_paused);
    }

    api::disk_cache::wipe_all();
}

// A catalog row can never carry `channel_replies_paused` -- it is a
// `WireState` key with no `WireSessionSummary` equivalent -- so the MOCK must
// not project one either. A fixture that leaks it would let the no-attach path
// look tested when the real client can never reach that state.
static void test_the_mock_catalog_cannot_carry_paused() {
    std::printf("test_the_mock_catalog_cannot_carry_paused\n");
    setenv("HANABI_BRAKES_DEMO", "1", 1);
    api::MockClient mock;

    // WITHOUT an attach: every row is unpaused, bz2 included.
    auto listed = mock.list_sessions();
    CHECK(listed.ok);
    bool sawBz2 = false;
    for (const auto& s : listed.value) {
        CHECK(!s.replies_paused);
        if (s.id == "bz2") sawBz2 = true;
    }
    CHECK(sawBz2);
    for (const auto& s : listed.value)
        if (s.id == "bz2")
            CHECK(ecs::model::status_glyph(s) !=
                  ecs::model::StatusGlyph::Paused);

    // The ATTACH is where the flag lives, and it does carry it.
    auto opened = mock.get_session("bz2");
    CHECK(opened.ok);
    CHECK(opened.value.summary.replies_paused);

    // Attach -> overlay -> the row, which is the only route there is.
    ecs::AppComponent app;
    app.replace_sessions(std::vector<api::SessionSummary>(listed.value));
    CHECK(!app.find_summary("bz2")->replies_paused);
    ecs::AppComponent::AttachBrakes brakes;
    brakes.replies_paused = true;
    app.apply_attach_brakes("bz2", brakes);
    CHECK(app.find_summary("bz2")->replies_paused);
    CHECK(ecs::model::status_glyph(*app.find_summary("bz2")) ==
          ecs::model::StatusGlyph::Paused);

    // And it survives the next refresh, which still says nothing.
    auto again = mock.list_sessions();
    CHECK(again.ok);
    app.overlay_attach_brakes(again.value);
    for (const auto& s : again.value)
        if (s.id == "bz2") CHECK(s.replies_paused);

    // The freeze is NOT stripped: it is a real summary-row key, and bz1 is
    // frozen on the row with no attach at all.
    bool sawFrozen = false;
    for (const auto& s : again.value)
        if (s.id == "bz1") {
            CHECK(s.frozen);
            sawFrozen = true;
        }
    CHECK(sawFrozen);
    unsetenv("HANABI_BRAKES_DEMO");
}

int main() {
    std::printf("=== test_data ===\n");
    test_disk_cache_total_and_wipe();
    test_disk_cache_round_trips_the_brakes();
    test_an_old_cache_file_still_loads();
    test_paused_survives_restart_and_the_next_refresh();
    test_an_attach_freeze_reaches_the_catalog_row();
    test_a_server_thaw_beats_a_cached_freeze();
    test_the_mock_catalog_cannot_carry_paused();
    test_cache_wipe_keeps_visible_panes_and_rejects_old_reads();
    test_message_queue_ordering();
    test_sending_for_covers_stream();
    test_newest_n_window();
    test_content_search_memo_is_not_stale();
    test_content_search_matches_values_not_the_document();
    test_settings_read_mock();
    test_settings_config_gate();
    test_compact_count();
    test_clock_time();
    test_day_boundaries_and_labels();
    test_find_occurrences();
    test_slash_parsing();
    test_mock_forks_and_subagent_catalog();
    test_model_menu();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
