// The cross-session index: what it matches, what it says it missed
// (src/search/session_index.h).
//
// This is where the feature's honesty lives. The UI can only show a count and
// a sentence; whether that sentence is TRUE of the corpus is decided here, and
// the cases that matter most — a thread whose transcript was never held, a
// query that matches nothing anywhere — are exactly the ones a scripted UI
// test can only reach by accident.
#include <cstdio>
#include <string>

#include "../../src/search/session_corpus.h"
#include "../../src/search/session_index.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

using hanabi::search::Coverage;
using hanabi::search::coverage_note;
using hanabi::search::Depth;
using hanabi::search::Doc;
using hanabi::search::Hit;
using hanabi::search::Index;
using hanabi::search::snippet_around;

static Doc full(const std::string& id, const std::string& title,
                const std::string& body, const std::string& preview = "") {
    Doc d;
    d.id = id;
    d.title = title;
    d.preview = preview;
    d.body = body;
    d.depth = Depth::Full;
    return d;
}

static Doc shallow(const std::string& id, const std::string& title,
                   const std::string& preview = "") {
    Doc d;
    d.id = id;
    d.title = title;
    d.preview = preview;
    return d;
}

static Doc windowed(const std::string& id, const std::string& title,
                    const std::string& body, const std::string& preview = "") {
    Doc d = full(id, title, body, preview);
    d.depth = Depth::Windowed;
    return d;
}

static Index sample() {
    Index ix;
    ix.add(full("a", "Stars payout reconciliation",
                "Reconciled 4,812 accounts.\nacct 8842 - ledger $128.60, "
                "computed $116.20 (delta $12.40)"));
    ix.add(shallow("b", "Payout worker race fix", "waiting on you"));
    ix.add(shallow("c", "Docs: onboarding runbook", "ledger notes moved here"));
    return ix;
}

static void test_a_body_hit_carries_the_line_it_was_found_in() {
    std::printf("test_a_body_hit_carries_the_line_it_was_found_in\n");
    const auto hits = sample().query("8842", 10);
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    CHECK(hits[0].id == "a");
    CHECK(hits[0].in_body);
    CHECK(!hits[0].partial);
    CHECK(hits[0].snippet.find("acct 8842 - ledger") != std::string::npos);
}

static void test_matching_is_case_insensitive() {
    std::printf("test_matching_is_case_insensitive\n");
    CHECK(sample().query("RECONCILED", 10).size() == 1);
    CHECK(sample().query("stars PAYOUT", 10).size() == 1);
}

static void test_a_thread_we_only_skimmed_is_marked() {
    std::printf("test_a_thread_we_only_skimmed_is_marked\n");
    // THE case: "ledger" is in one thread's conversation and in another's
    // preview. Both are results; only one of them is evidence about what was
    // said, and the caller has to be able to tell them apart.
    const auto hits = sample().query("ledger", 10);
    CHECK(hits.size() == 2);
    if (hits.size() != 2) return;
    CHECK(hits[0].id == "a");
    CHECK(!hits[0].partial);
    CHECK(hits[1].id == "c");
    CHECK(hits[1].partial);
}

static void test_a_title_hit_still_shows_something() {
    std::printf("test_a_title_hit_still_shows_something\n");
    const auto hits = sample().query("worker", 10);
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    CHECK(!hits[0].in_body);
    CHECK(hits[0].snippet == "waiting on you");
}

static void test_an_empty_query_is_not_everything() {
    std::printf("test_an_empty_query_is_not_everything\n");
    CHECK(sample().query("", 10).empty());
    CHECK(sample().query("nothing here", 10).empty());
    // The cap is a cap, not a suggestion.
    CHECK(sample().query("o", 1).size() == 1);
}

static void test_coverage_counts_what_was_actually_read() {
    std::printf("test_coverage_counts_what_was_actually_read\n");
    const Coverage c = sample().coverage();
    CHECK(c.threads == 3);
    CHECK(c.full == 1);
    CHECK(c.shallow() == 2);
    CHECK(!c.complete());
}

static void test_the_note_says_how_partial_the_answer_is() {
    std::printf("test_the_note_says_how_partial_the_answer_is\n");
    // A search that quietly misses most of your history is the failure this
    // sentence exists to prevent, so its NUMBERS are the assertion.
    const std::string note = coverage_note(sample().coverage());
    CHECK(note.find("1 of 3") != std::string::npos);
    CHECK(note.find("title and preview only") != std::string::npos);

    Index all;
    all.add(full("a", "One", "text"));
    all.add(full("b", "Two", "text"));
    const std::string complete = coverage_note(all.coverage());
    CHECK(complete.find("all 2") != std::string::npos);
    CHECK(complete.find("title and preview only") == std::string::npos);

    CHECK(coverage_note(Index().coverage()) == "Nothing to search yet");
}

// A thread read only to its newest messages is neither Full nor TitleOnly, and
// the difference is the whole honesty of the feature: an absent match in the
// tail of a thread is not evidence that the word was never said. Before this,
// the in-memory LRU's 20-message tail was stamped Full and counted as Full.
// docs/SEARCH.md S2.
static void test_a_thread_read_only_to_its_tail_is_its_own_depth() {
    std::printf("test_a_thread_read_only_to_its_tail_is_its_own_depth\n");
    Index ix;
    ix.add(full("a", "Read whole", "the retry queue is draining"));
    ix.add(windowed("b", "Read recently", "the retry budget was raised"));
    ix.add(shallow("c", "Never opened", "retry notes"));

    const Coverage c = ix.coverage();
    CHECK(c.threads == 3);
    CHECK(c.full == 1);
    CHECK(c.windowed == 1);
    CHECK(c.shallow() == 1);
    CHECK(!c.complete());

    // partial keeps its old meaning -- title and preview only -- so the row
    // qualifier that says exactly that does not start appearing on threads
    // whose conversation WAS searched. `windowed` is the new, weaker mark.
    const auto hits = ix.query("retry", 10);
    CHECK(hits.size() == 3);
    if (hits.size() != 3) return;
    CHECK(!hits[0].partial && !hits[0].windowed);
    CHECK(!hits[1].partial && hits[1].windowed);
    CHECK(hits[2].partial && !hits[2].windowed);

    // Three clauses, and each optional one appears only when its count is not
    // zero -- the note is read across a 500px panel.
    const std::string note = coverage_note(c);
    CHECK(note == "Full text for 1 of 3 threads; 1 to their newest messages "
                  "only; the rest by title and preview only");

    Index noShallow;
    noShallow.add(full("a", "One", "text"));
    noShallow.add(windowed("b", "Two", "text"));
    CHECK(coverage_note(noShallow.coverage()) ==
          "Full text for 1 of 2 threads; 1 to their newest messages only");

    // All windowed is not complete, and must not read as "all N threads".
    Index allWindowed;
    allWindowed.add(windowed("a", "One", "text"));
    CHECK(coverage_note(allWindowed.coverage()) ==
          "Full text for 0 of 1 threads; 1 to their newest messages only");
    CHECK(!allWindowed.coverage().complete());
}

// Opening Cmd+Shift+F must not read the disk cache.
//
// build_index used to call load_transcript -- a full nlohmann parse -- once
// per thread not already in memory, synchronously, on the frame the panel
// opened. tools/bench_search_index.cpp measures that at 370 ms for a 2000
// thread cache: a third of a second of frozen UI, growing with the user's
// history forever. docs/SEARCH.md S5.
//
// The gate is a COUNT of loader calls, not a clock. A time budget reads a
// different number of files on a loaded machine than on a quiet one, and this
// box is shared; a count is the same number every run.
static void test_opening_the_panel_reads_nothing_from_disk() {
    std::printf("test_opening_the_panel_reads_nothing_from_disk\n");
    using hanabi::search::CorpusBuilder;
    using hanabi::search::kDeepenPerFrame;
    using hanabi::search::Loaded;
    using hanabi::search::Row;

    constexpr std::size_t kThreads = 500;
    std::vector<Row> rows;
    for (std::size_t i = 0; i < kThreads; ++i) {
        Row r;
        r.id = "t" + std::to_string(i);
        r.title = "thread " + std::to_string(i);
        r.preview = "preview";
        r.updated_at = static_cast<std::int64_t>(i);
        rows.push_back(std::move(r));
    }
    // Two threads are already in memory: one whole, one only its tail.
    rows[10].held = "the retry queue is draining";
    rows[10].has_held = true;
    rows[11].held = "the tail of a much longer thread";
    rows[11].has_held = true;
    rows[11].held_is_tail = true;

    int loads = 0;
    std::vector<std::string> asked;
    const auto load = [&](const std::string& id) -> std::optional<Loaded> {
        ++loads;
        asked.push_back(id);
        Loaded l;
        l.body = "disk body for " + id + " mentioning quota";
        return l;
    };

    CorpusBuilder b;
    b.begin(std::move(rows));
    // THE LEVEL. Opening the panel is free of disk I/O -- this is the number
    // that was 499 before.
    CHECK(loads == 0);
    CHECK(b.index().size() == kThreads);
    // The whole in-memory copy counts as read; the tail does not.
    CHECK(b.index().coverage().full == 1);
    CHECK(b.index().coverage().windowed == 1);
    CHECK(b.pending() == kThreads - 1);  // everything but the whole copy

    // And a frame reads a fixed few, whatever the catalog size is.
    CHECK(b.deepen(kDeepenPerFrame, load) == kDeepenPerFrame);
    CHECK(loads == static_cast<int>(kDeepenPerFrame));
    CHECK(!b.complete());

    // Newest first: the rows were seeded oldest-to-newest, so the first thread
    // asked for is the last one added. A user looking for a conversation is
    // usually looking for a recent one, and the order must be total or the
    // corpus would reshuffle under an arrow key between frames.
    CHECK(asked[0] == "t499");
    CHECK(asked[1] == "t498");

    // It converges, and no thread is read twice.
    int frames = 1;
    while (!b.complete()) {
        b.deepen(kDeepenPerFrame, load);
        ++frames;
    }
    CHECK(loads == static_cast<int>(kThreads) - 1);
    CHECK(frames == static_cast<int>((kThreads - 1 + kDeepenPerFrame - 1) /
                                     kDeepenPerFrame));
    CHECK(b.index().coverage().complete());
    CHECK(b.deepen(kDeepenPerFrame, load) == 0);
    CHECK(loads == static_cast<int>(kThreads) - 1);

    // The thread whose in-memory copy was whole was never asked for.
    for (const auto& id : asked) CHECK(id != "t10");
    // The one whose copy was a tail was, and the longer disk copy replaced it.
    CHECK(b.index().query("quota", 600).size() == kThreads - 1);
}

// Deepening only ever deepens: a disk copy that is missing, or is no longer
// than the tail already indexed, leaves the thread with what it had. Without
// this a thread whose cache file is older than the live tail would get WORSE
// under a search that was supposed to improve it.
// Results come out newest-thread first, whatever order the adapter sent the
// list in. session_index.h's header said the caller added them newest-first;
// the caller iterated app.sessions unmodified, and only the MOCK adapter sorts
// -- HttpClient and AgentcloudClient push rows in wire order. docs/SEARCH.md
// S7.
static void test_results_come_out_newest_first() {
    std::printf("test_results_come_out_newest_first\n");
    using hanabi::search::CorpusBuilder;
    using hanabi::search::Row;

    const auto row = [](const char* id, std::int64_t at, const char* body) {
        Row r;
        r.id = id;
        r.title = id;
        r.held = body;
        r.has_held = true;
        r.updated_at = at;
        return r;
    };
    // Wire order: neither sorted nor reversed, which is what "whatever the
    // server sent" looks like.
    std::vector<Row> rows;
    rows.push_back(row("middle", 200, "the retry budget"));
    rows.push_back(row("oldest", 100, "the retry budget"));
    rows.push_back(row("newest", 300, "the retry budget"));

    CorpusBuilder b;
    b.begin(std::move(rows));
    const auto hits = b.index().query("retry", 10);
    CHECK(hits.size() == 3);
    if (hits.size() != 3) return;
    CHECK(hits[0].id == "newest");
    CHECK(hits[1].id == "middle");
    CHECK(hits[2].id == "oldest");

    // Ties break by id, so the order is total: two threads sharing a second --
    // and updated_at is a whole number of seconds, so a real catalog has
    // plenty -- cannot swap places between frames while the corpus deepens.
    std::vector<Row> tied;
    tied.push_back(row("b", 500, "quota"));
    tied.push_back(row("a", 500, "quota"));
    tied.push_back(row("c", 500, "quota"));
    CorpusBuilder t;
    t.begin(std::move(tied));
    const auto tiedHits = t.index().query("quota", 10);
    CHECK(tiedHits.size() == 3);
    if (tiedHits.size() != 3) return;
    CHECK(tiedHits[0].id == "a");
    CHECK(tiedHits[1].id == "b");
    CHECK(tiedHits[2].id == "c");
}

static void test_deepening_never_makes_a_thread_shallower() {
    std::printf("test_deepening_never_makes_a_thread_shallower\n");
    using hanabi::search::CorpusBuilder;
    using hanabi::search::Loaded;
    using hanabi::search::Row;

    std::vector<Row> rows;
    Row a;
    a.id = "a";
    a.title = "has a tail in memory";
    a.held = "the newest twenty messages, mentioning kestrel";
    a.has_held = true;
    a.held_is_tail = true;
    a.updated_at = 2;
    rows.push_back(std::move(a));
    Row b2;
    b2.id = "b";
    b2.title = "nothing in memory";
    b2.updated_at = 1;
    rows.push_back(std::move(b2));

    CorpusBuilder b;
    b.begin(std::move(rows));
    b.deepen(8, [](const std::string& id) -> std::optional<Loaded> {
        if (id == "a") {
            Loaded l;
            l.body = "stale short copy";  // shorter than the tail we hold
            return l;
        }
        return std::nullopt;  // no copy of b on this machine at all
    });
    CHECK(b.complete());
    // The tail survived...
    CHECK(b.index().query("kestrel", 10).size() == 1);
    CHECK(b.index().query("stale", 10).empty());
    // ...and the thread with no copy anywhere is title-and-preview only.
    CHECK(b.index().coverage().windowed == 1);
    CHECK(b.index().coverage().shallow() == 1);
    CHECK(b.index().coverage().full == 0);
}

static void test_a_snippet_is_one_readable_line() {
    std::printf("test_a_snippet_is_one_readable_line\n");
    const std::string body =
        "the first line\nand then we discussed memory management at length\n"
        "and a third line";
    const size_t off = body.find("memory");
    const std::string s = snippet_around(body, off, 6);
    CHECK(s.find("memory") != std::string::npos);
    CHECK(s.find('\n') == std::string::npos);
    // Cut on both sides, so it says so on both sides.
    CHECK(s.rfind("\xe2\x80\xa6", 0) == 0);
    CHECK(s.size() > 6);
    CHECK(s.substr(s.size() - 3) == "\xe2\x80\xa6");

    // A match at the very start is not cut at the start.
    const std::string head = snippet_around(body, 0, 3);
    CHECK(head.rfind("\xe2\x80\xa6", 0) != 0);
    // An offset past the end is a bug upstream, not a crash here.
    CHECK(snippet_around(body, body.size() + 5, 3).empty());
}

int main() {
    std::printf("=== test_session_index ===\n");
    test_a_body_hit_carries_the_line_it_was_found_in();
    test_matching_is_case_insensitive();
    test_a_thread_we_only_skimmed_is_marked();
    test_a_title_hit_still_shows_something();
    test_an_empty_query_is_not_everything();
    test_coverage_counts_what_was_actually_read();
    test_the_note_says_how_partial_the_answer_is();
    test_a_thread_read_only_to_its_tail_is_its_own_depth();
    test_opening_the_panel_reads_nothing_from_disk();
    test_deepening_never_makes_a_thread_shallower();
    test_results_come_out_newest_first();
    test_a_snippet_is_one_readable_line();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
