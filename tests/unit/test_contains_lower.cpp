#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "../../src/ecs/sidebar_buckets.h"
#include "../../src/util/format.h"

static int g_failures = 0;
#define REQUIRE(cond)                                               \
    do {                                                            \
        if (!(cond)) {                                              \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++g_failures;                                           \
        }                                                           \
    } while (0)

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

// The expression contains_lower replaced, restated. This is the reference the
// differential arm compares against; it is a copy on purpose, because a shared
// implementation could not catch the two drifting.
static bool reference_contains(const std::string& hay,
                               const std::string& lowerNeedle) {
    if (lowerNeedle.empty()) return true;
    return fmtutil::to_lower(hay).find(lowerNeedle) != std::string::npos;
}

// The body of ecs::model::title_matches before this change.
static bool reference_title_matches(const std::string& title,
                                    const std::string& q) {
    if (q.empty()) return true;
    return fmtutil::to_lower(title).find(q) != std::string::npos;
}

static std::vector<std::string> corpus_haystacks() {
    return {
        "",
        "a",
        "A",
        "recover the cohort",
        "RECOVER THE COHORT",
        "ReCoVeR tHe CoHoRt",
        "aaab",
        "AAAB",
        "abcabd",
        "AbCaBd",
        "rrrrrrrrrrrrrrrrrrrrrrrrrrrrr",
        "reconcile a cohort that will not converge",
        "reconcile a cohort \xe2\x80\x94 and reporting back on what moved",
        "\xe2\x80\x94\xe2\x80\x94\xe2\x80\x94",
        "Schedule: nightly export -tick",
        "   leading and trailing   ",
        "MiXeD\xc3\x89 accents stay bytes",
        "tab\tand\nnewline",
        std::string("embedded\0nul", 12),
        std::string(300, 'q') + "needle" + std::string(300, 'q'),
    };
}

static std::vector<std::string> corpus_needles() {
    return {
        "",       "a",      "A",     "r",      "re",    "rec",
        "reco",   "cohort", "COHORT",
        "ab",     "abd",    "aab",   "q",      "needle",
        "\xe2\x80\x94",     "-tick", "schedule:",
        "z",      "zzq",    "recover the cohort",
        "recover the cohort and then some",
        " ",      "\t",     std::string(1, '\0'),
    };
}

// 1. DIFFERENTIAL. contains_lower must answer exactly what the allocating
//    expression answered, for every pair in the corpus.
static void differential() {
    const auto hays = corpus_haystacks();
    const auto needles = corpus_needles();
    std::size_t pairs = 0;
    for (const std::string& hay : hays) {
        for (const std::string& raw : needles) {
            const std::string q = fmtutil::to_lower(raw);
            const bool got = fmtutil::contains_lower(hay, q);
            const bool want = reference_contains(hay, q);
            if (got != want)
                std::printf("  FAIL: contains_lower(\"%s\", \"%s\") = %d, "
                            "reference = %d\n",
                            hay.c_str(), q.c_str(), got, want);
            REQUIRE(got == want);
            ++pairs;
        }
    }
    std::printf("  differential: %zu (haystack, needle) pairs agree\n", pairs);
}

// 2. The call site the sidebar's filter actually uses, against ITS old body.
static void title_matches_differential() {
    const auto hays = corpus_haystacks();
    const auto needles = corpus_needles();
    std::size_t pairs = 0;
    for (const std::string& title : hays) {
        for (const std::string& raw : needles) {
            const std::string q = fmtutil::to_lower(raw);
            REQUIRE(ecs::model::title_matches(title, q) ==
                    reference_title_matches(title, q));
            ++pairs;
        }
    }
    std::printf("  title_matches: %zu pairs agree with the old body\n", pairs);
}

// 3. Named edge cases, spelled out so a failure names the property.
static void edges() {
    REQUIRE(fmtutil::contains_lower("anything", "") == true);
    REQUIRE(fmtutil::contains_lower("", "") == true);
    REQUIRE(fmtutil::contains_lower("", "a") == false);
    REQUIRE(fmtutil::contains_lower("ab", "abc") == false);
    REQUIRE(fmtutil::contains_lower("abc", "abc") == true);
    REQUIRE(fmtutil::contains_lower("ABC", "abc") == true);
    REQUIRE(fmtutil::contains_lower("xABCx", "abc") == true);
    REQUIRE(fmtutil::contains_lower("ABCx", "abc") == true);
    REQUIRE(fmtutil::contains_lower("xABC", "abc") == true);
    // A false start must not consume the position that matches.
    REQUIRE(fmtutil::contains_lower("aaab", "aab") == true);
    REQUIRE(fmtutil::contains_lower("abcabd", "abd") == true);
    // Folding and a false start in the SAME input: the restart has to re-fold
    // from i+1, not compare the raw byte it already folded once.
    REQUIRE(fmtutil::contains_lower("AAAB", "aab") == true);
    REQUIRE(fmtutil::contains_lower("AbCaBd", "abd") == true);
    REQUIRE(fmtutil::contains_lower("AAAC", "aab") == false);
    // ASCII-only folding, the same rule to_lower has: bytes outside A-Z are
    // compared as they are.
    REQUIRE(fmtutil::contains_lower("\xc3\x89", "\xc3\xa9") == false);
    REQUIRE(fmtutil::contains_lower("caf\xc3\xa9", "\xc3\xa9") == true);
    // The needle is taken as already lowered: an upper-case needle cannot
    // match, which is the contract every caller upholds by lowering once.
    REQUIRE(fmtutil::contains_lower("abc", "ABC") == false);
}

// 4. THE OPTIMIZATION ITSELF. A scan over long titles must allocate NOTHING.
//    The second half runs the allocating expression this replaced over the
//    same input, so a zero on the first line is evidence rather than a test
//    that cannot fail.
static void allocates_nothing() {
    std::vector<std::string> titles;
    titles.reserve(512);
    for (int i = 0; i < 512; ++i)
        titles.push_back("Reconcile a cohort that will not converge " +
                         std::to_string(i));
    const std::string q = "converge";

    int hits = 0;
    g_allocs = 0;
    g_counting = true;
    for (const std::string& t : titles) hits += fmtutil::contains_lower(t, q);
    g_counting = false;
    const unsigned long long lean = g_allocs;

    int refHits = 0;
    g_allocs = 0;
    g_counting = true;
    for (const std::string& t : titles) refHits += reference_contains(t, q);
    g_counting = false;
    const unsigned long long allocating = g_allocs;

    std::printf("  %zu titles: contains_lower %llu allocations, "
                "to_lower().find() %llu\n",
                titles.size(), lean, allocating);
    REQUIRE(hits == 512);
    REQUIRE(refHits == hits);
    REQUIRE(lean == 0);
    // If this ever reads 0 the arm above proves nothing: it would mean the
    // reference stopped allocating too, and the comparison is vacuous.
    REQUIRE(allocating >= titles.size());
}

// 5. The same pin through the shipped call site, which is what regresses.
static void title_matches_allocates_nothing() {
    std::vector<api::SessionSummary> sessions;
    sessions.reserve(400);
    for (int i = 0; i < 400; ++i) {
        api::SessionSummary s;
        s.id = "s" + std::to_string(i);
        s.title = "Reconcile a cohort that will not converge " +
                  std::to_string(i);
        sessions.push_back(std::move(s));
    }
    const std::string q = "cohort";

    int hits = 0;
    g_allocs = 0;
    g_counting = true;
    for (const api::SessionSummary& s : sessions)
        hits += ecs::model::title_matches(s.title, q);
    g_counting = false;

    std::printf("  %zu titles through title_matches: %llu allocations\n",
                sessions.size(), g_allocs);
    REQUIRE(hits == 400);
    REQUIRE(g_allocs == 0);
}

int main() {
    std::printf("=== contains_lower ===\n");
    differential();
    title_matches_differential();
    edges();
    allocates_nothing();
    title_matches_allocates_nothing();
    if (g_failures == 0) {
        std::printf("test_contains_lower: PASS\n");
        return 0;
    }
    std::printf("test_contains_lower: FAIL (%d)\n", g_failures);
    return 1;
}
