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
    test_a_snippet_is_one_readable_line();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
