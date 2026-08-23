// Cutting a match down to a sidebar row (src/ui/snippet_text.h).
//
// The highlighting is asserted on screen (tests/ui/sidebar_search_snippet.e2e);
// this is the arithmetic underneath it, where the interesting cases live — a
// match at the very start of the line, a line with no match in it at all, a
// window wider than the text. A scripted test reaches those by luck; here they
// are the point.
#include <cstdio>
#include <string>

#include "../../src/ui/snippet_text.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

using hanabi::snippet_text::extract;

static const char* kEllipsis = "\xe2\x80\xa6";

static void test_it_keeps_the_match_and_its_neighbours() {
    std::printf("test_it_keeps_the_match_and_its_neighbours\n");
    const std::string line =
        "we looked at the jittered backoff, one edge case left";
    const std::string s = extract(line, "backoff");
    CHECK(s.find("backoff") != std::string::npos);
    CHECK(s.find("jittered") != std::string::npos);
}

static void test_a_line_that_fits_is_not_cut() {
    std::printf("test_a_line_that_fits_is_not_cut\n");
    const std::string s = extract("scanning for stale flags", "flags");
    CHECK(s == "scanning for stale flags");
    CHECK(s.find(kEllipsis) == std::string::npos);
}

static void test_a_cut_says_where_it_cut() {
    std::printf("test_a_cut_says_where_it_cut\n");
    const std::string line =
        "aaaa bbbb cccc dddd eeee ffff gggg needle hhhh iiii jjjj kkkk llll";
    const std::string s = extract(line, "needle");
    CHECK(s.find("needle") != std::string::npos);
    CHECK(s.rfind(kEllipsis, 0) == 0);
    CHECK(s.substr(s.size() - 3) == kEllipsis);
    // Cut on word boundaries: what survives is a whole run of the line, and
    // the character before it in the line is a space — never a half-word.
    const std::string body = s.substr(3, s.size() - 6);  // strip both ellipses
    const size_t at = line.find(body);
    CHECK(at != std::string::npos);
    CHECK(at > 0 && line[at - 1] == ' ');
    CHECK(line[at + body.size()] == ' ');
}

static void test_a_match_at_the_start_is_not_cut_at_the_start() {
    std::printf("test_a_match_at_the_start_is_not_cut_at_the_start\n");
    const std::string s =
        extract("flags are the subject of this whole long sentence about "
                "cleanup work", "flags");
    CHECK(s.rfind(kEllipsis, 0) != 0);
    CHECK(s.rfind("flags", 0) == 0);
}

static void test_case_does_not_matter() {
    std::printf("test_case_does_not_matter\n");
    // The query comes from a text field; the text comes from a backend.
    CHECK(!extract("Stale Flags everywhere", "flags").empty());
    CHECK(!extract("stale flags everywhere", "FLAGS").empty());
    // ...and the snippet keeps the ORIGINAL casing, not the lowered copy.
    CHECK(extract("Stale Flags everywhere", "flags").find("Flags") !=
          std::string::npos);
}

static void test_no_match_is_empty_not_the_whole_line() {
    std::printf("test_no_match_is_empty_not_the_whole_line\n");
    // The caller distinguishes "here is the line it matched on" from "here is
    // a preview, nothing lit" by this being EMPTY. Returning the line would
    // make every row look like a content hit.
    CHECK(extract("scanning for stale flags", "ledger").empty());
    CHECK(extract("", "flags").empty());
    CHECK(extract("something", "").empty());
}

static void test_a_snippet_is_one_line() {
    std::printf("test_a_snippet_is_one_line\n");
    const std::string s = extract("first line\nsecond has flags\nthird line",
                                  "flags");
    CHECK(s.find("flags") != std::string::npos);
    CHECK(s.find('\n') == std::string::npos);
}

int main() {
    std::printf("=== test_snippet_text ===\n");
    test_it_keeps_the_match_and_its_neighbours();
    test_a_line_that_fits_is_not_cut();
    test_a_cut_says_where_it_cut();
    test_a_match_at_the_start_is_not_cut_at_the_start();
    test_case_does_not_matter();
    test_no_match_is_empty_not_the_whole_line();
    test_a_snippet_is_one_line();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
