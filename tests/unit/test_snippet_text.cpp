// Cutting a match down to a sidebar row (src/ui/snippet_text.h).
//
// The highlighting is asserted on screen (tests/ui/sidebar_search_snippet.e2e);
// this is the arithmetic underneath it, where the interesting cases live — a
// match at the very start of the line, a line with no match in it at all, a
// window wider than the text. A scripted test reaches those by luck; here they
// are the point.
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

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
using hanabi::snippet_text::extract_into;
using hanabi::snippet_text::kContext;

static unsigned long long g_allocs = 0;
static bool g_counting = false;

void* operator new(std::size_t n) {
    if (g_counting) ++g_allocs;
    void* p = std::malloc(n == 0 ? 1 : n);
    if (p == nullptr) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t n) {
    if (g_counting) ++g_allocs;
    void* p = std::malloc(n == 0 ? 1 : n);
    if (p == nullptr) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

// The body extract_into replaced, restated. A copy on purpose — a shared
// implementation could not catch the two drifting.
static std::string reference_extract(const std::string& text,
                                     const std::string& query,
                                     size_t context = kContext) {
    if (text.empty() || query.empty()) return std::string();
    const std::string hay = fmtutil::to_lower(text);
    const std::string needle = fmtutil::to_lower(query);
    const size_t at = hay.find(needle);
    if (at == std::string::npos) return std::string();

    size_t begin = at > context ? at - context : 0;
    size_t end = at + needle.size() + context;
    if (end > text.size()) end = text.size();
    if (begin > 0) {
        const size_t sp = text.find_first_of(" \t\n", begin);
        if (sp != std::string::npos && sp < at) begin = sp + 1;
    }
    if (end < text.size()) {
        const size_t sp = text.find_last_of(" \t\n", end);
        if (sp != std::string::npos && sp > at + needle.size()) end = sp;
    }

    std::string out = text.substr(begin, end - begin);
    for (char& c : out)
        if (c == '\n' || c == '\t' || c == '\r') c = ' ';
    if (begin > 0) out.insert(0, "\xe2\x80\xa6");
    if (end < text.size()) out += "\xe2\x80\xa6";
    return out;
}

static std::vector<std::string> corpus_lines() {
    std::vector<std::string> lines;
    lines.reserve(512);
    for (int i = 0; i < 512; ++i) {
        std::string s = "session " + std::to_string(i) +
                        ": the worker reported a stale FLAGS ledger entry and "
                        "then retried the jittered backoff once more, i=" +
                        std::to_string(i);
        if (i % 7 == 0) s += "\nsecond line with flags in it too";
        if (i % 11 == 0) s = "flags lead this one, " + s;
        if (i % 13 == 0) s = "nothing of interest here, entry " +
                             std::to_string(i);
        lines.push_back(std::move(s));
    }
    return lines;
}

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

static void test_extract_into_answers_what_the_copying_spelling_did() {
    std::printf("test_extract_into_answers_what_the_copying_spelling_did\n");
    const std::vector<std::string> lines = corpus_lines();
    static const char* kQueries[] = {"flags", "FLAGS", "backoff", "ledger",
                                     "zzq",   "",      "session 3"};
    size_t pairs = 0, agree = 0;
    std::string out;
    for (const std::string& line : lines) {
        for (const char* q : kQueries) {
            const std::string want = reference_extract(line, q);
            const bool got =
                extract_into(out, line, fmtutil::to_lower(std::string(q)));
            ++pairs;
            if (got == !want.empty() && out == want) ++agree;
        }
    }
    std::printf("  differential: %zu of %zu (line, query) pairs agree\n", agree,
                pairs);
    CHECK(agree == pairs);
}

static void test_a_reused_buffer_cuts_without_allocating() {
    std::printf("test_a_reused_buffer_cuts_without_allocating\n");
    const std::vector<std::string> lines = corpus_lines();
    const std::string needle = "flags";
    std::string out;
    for (const std::string& line : lines) extract_into(out, line, needle);

    g_allocs = 0;
    g_counting = true;
    for (const std::string& line : lines) extract_into(out, line, needle);
    g_counting = false;
    const unsigned long long lean = g_allocs;

    g_allocs = 0;
    g_counting = true;
    unsigned long long sink = 0;
    for (const std::string& line : lines)
        sink += reference_extract(line, needle).size();
    g_counting = false;
    const unsigned long long copying = g_allocs;

    std::printf("  %zu lines: extract_into %llu allocations, the copying "
                "spelling %llu\n",
                lines.size(), lean, copying);
    CHECK(sink > 0);
    CHECK(lean == 0);
    CHECK(copying >= lines.size());
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
    test_extract_into_answers_what_the_copying_spelling_did();
    test_a_reused_buffer_cuts_without_allocating();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
