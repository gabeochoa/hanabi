#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "../../src/ui/md_spans.h"

static unsigned long long g_allocs = 0;
static bool g_counting = false;

void* operator new(std::size_t n) {
    if (g_counting) ++g_allocs;
    void* p = std::malloc(n == 0 ? 1 : n);
    if (p == nullptr) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

static int g_failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);     \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

namespace md = hanabi::md;

static const md::Palette kPalette{
    afterhours::Color{230, 230, 230, 255},
    afterhours::Color{120, 200, 255, 255},
    afterhours::Color{255, 255, 255, 255},
};

static md::Spans reference_spans(const std::string& line,
                                 const md::Palette& pal) {
    md::Spans p;
    const afterhours::Color base = pal.base;
    const afterhours::Color codeC = pal.code;
    const afterhours::Color strongC = pal.strong;
    auto push = [&](const std::string& t, afterhours::Color c) {
        if (t.empty()) return;
        p.visible += t;
        if (!p.spans.empty() && p.spans.back().color.r == c.r &&
            p.spans.back().color.g == c.g && p.spans.back().color.b == c.b &&
            p.spans.back().color.a == c.a)
            p.spans.back().text += t;
        else
            p.spans.push_back(afterhours::ui::TextSpan{t, c});
    };
    size_t i = 0;
    const size_t n = line.size();
    while (i < n) {
        if (line[i] == '`') {
            size_t close = line.find('`', i + 1);
            if (close != std::string::npos && close > i + 1) {
                push(line.substr(i + 1, close - i - 1), codeC);
                i = close + 1;
                continue;
            }
        }
        for (const char* d : {"**", "__"}) {
            if (line.compare(i, 2, d) == 0) {
                size_t close = line.find(d, i + 2);
                if (close != std::string::npos && close > i + 2) {
                    push(line.substr(i + 2, close - i - 2), strongC);
                    i = close + 2;
                    goto next;
                }
            }
        }
        for (char d : {'*', '_'}) {
            if (line[i] == d && i + 1 < n && line[i + 1] != ' ') {
                size_t close = line.find(d, i + 1);
                if (close != std::string::npos && close > i + 1) {
                    push(line.substr(i + 1, close - i - 1), strongC);
                    i = close + 1;
                    goto next;
                }
            }
        }
        push(std::string(1, line[i]), base);
        ++i;
    next:;
    }
    return p;
}

static bool same_spans(const md::Spans& a, const md::Spans& b) {
    if (a.visible != b.visible) return false;
    if (a.spans.size() != b.spans.size()) return false;
    for (size_t i = 0; i < a.spans.size(); ++i) {
        if (a.spans[i].text != b.spans[i].text) return false;
        if (a.spans[i].color.r != b.spans[i].color.r) return false;
        if (a.spans[i].color.g != b.spans[i].color.g) return false;
        if (a.spans[i].color.b != b.spans[i].color.b) return false;
        if (a.spans[i].color.a != b.spans[i].color.a) return false;
    }
    return true;
}

static std::vector<std::string> corpus() {
    const std::vector<std::string> fragments = {
        "",           " ",          "plain words here",
        "`",          "``",         "`code`",
        "`a`",        "*",          "**",
        "***",        "_",          "__",
        "**bold**",   "__bold__",   "*em*",
        "_em_",       "* ",         "_ ",
        "a*b",        "a_b_c",      "snake_case_name",
        "2 * 3 * 4",  "**unclosed", "`unclosed",
        "naïve",      "héllo `wörld`", "→ ✦ ✓",
        "path/to/_file_.txt",        "**a**`b`_c_",
        "trailing_",  "_leading",   "a `b` **c** _d_ e",
    };
    std::vector<std::string> out;
    for (const auto& a : fragments) {
        out.push_back(a);
        for (const auto& b : fragments) {
            out.push_back(a + b);
            out.push_back(a + " " + b);
        }
    }
    for (size_t i = 0; i < fragments.size(); ++i)
        for (size_t j = 0; j < fragments.size(); j += 3)
            for (size_t k = 0; k < fragments.size(); k += 7)
                out.push_back(fragments[i] + fragments[j] + " " +
                              fragments[k] + " tail text that is long enough "
                                             "to leave the small string buffer");
    return out;
}

static void test_matches_the_reference_over_the_corpus() {
    std::printf("-- differential against the per-character reference\n");
    const std::vector<std::string> lines = corpus();
    size_t mismatches = 0;
    std::string firstBad;
    for (const std::string& line : lines) {
        const md::Spans got = md::inline_spans(line, kPalette);
        const md::Spans want = reference_spans(line, kPalette);
        if (!same_spans(got, want)) {
            if (mismatches == 0) firstBad = line;
            ++mismatches;
        }
    }
    if (mismatches != 0)
        std::printf("  first mismatch on: \"%s\"\n", firstBad.c_str());
    std::printf("  %zu lines compared\n", lines.size());
    CHECK(lines.size() > 3000);
    CHECK(mismatches == 0);
}

static void test_visible_text_drops_only_matched_markers() {
    std::printf("-- visible text\n");
    CHECK(md::inline_spans("`code`", kPalette).visible == "code");
    CHECK(md::inline_spans("**bold**", kPalette).visible == "bold");
    CHECK(md::inline_spans("__bold__", kPalette).visible == "bold");
    CHECK(md::inline_spans("*em*", kPalette).visible == "em");
    CHECK(md::inline_spans("_em_", kPalette).visible == "em");
    CHECK(md::inline_spans("a * b", kPalette).visible == "a * b");
    CHECK(md::inline_spans("**unclosed", kPalette).visible == "**unclosed");
    CHECK(md::inline_spans("``", kPalette).visible == "``");
    CHECK(md::inline_spans("snake_case_name", kPalette).visible == "snakecasename");
}

static void test_runs_carry_the_palette() {
    std::printf("-- colour runs\n");
    const md::Spans s = md::inline_spans("a `b` c", kPalette);
    CHECK(s.visible == "a b c");
    CHECK(s.spans.size() == 3);
    if (s.spans.size() == 3) {
        CHECK(s.spans[0].text == "a ");
        CHECK(s.spans[0].color.r == kPalette.base.r);
        CHECK(s.spans[1].text == "b");
        CHECK(s.spans[1].color.r == kPalette.code.r);
        CHECK(s.spans[2].text == " c");
        CHECK(s.spans[2].color.r == kPalette.base.r);
    }
    const md::Spans plain = md::inline_spans("no markup at all here", kPalette);
    CHECK(plain.spans.size() == 1);
    const md::Spans split = md::inline_spans("a**b**c", kPalette);
    CHECK(split.visible == "abc");
    CHECK(split.spans.size() == 3);
    const md::Palette flat{kPalette.base, kPalette.code, kPalette.base};
    const md::Spans merged = md::inline_spans("a**b**c", flat);
    CHECK(merged.visible == "abc");
    CHECK(merged.spans.size() == 1);
}

template <typename Fn>
static unsigned long long allocations_for(const std::vector<std::string>& lines,
                                          Fn parse) {
    g_allocs = 0;
    g_counting = true;
    for (const std::string& line : lines) {
        const md::Spans s = parse(line);
        if (s.visible.size() == 0xFFFFFFFF) std::abort();
    }
    g_counting = false;
    return g_allocs;
}

static std::vector<std::string> long_plain_lines() {
    std::vector<std::string> out;
    for (int variant = 0; variant < 4; ++variant) {
        std::string line;
        while (line.size() < 1000u * (1u + static_cast<unsigned>(variant)))
            line += "the quick brown fox jumps over the lazy dog ";
        out.push_back(line);
    }
    return out;
}

static void test_allocates_per_run_not_per_character() {
    std::printf("-- allocation ceiling\n");
    const std::vector<std::string> mixed = corpus();
    const auto mine = [](const std::string& l) {
        return md::inline_spans(l, kPalette);
    };
    const auto ref = [](const std::string& l) {
        return reference_spans(l, kPalette);
    };

    const double mixedMine =
        static_cast<double>(allocations_for(mixed, mine)) /
        static_cast<double>(mixed.size());
    const double mixedRef = static_cast<double>(allocations_for(mixed, ref)) /
                            static_cast<double>(mixed.size());
    std::printf("  mixed corpus:  %.2f allocations/line "
                "(per-character reference: %.2f)\n", mixedMine, mixedRef);
    CHECK(mixedMine < mixedRef);

    const std::vector<std::string> longs = long_plain_lines();
    const double longMine = static_cast<double>(allocations_for(longs, mine)) /
                            static_cast<double>(longs.size());
    const double longRef = static_cast<double>(allocations_for(longs, ref)) /
                           static_cast<double>(longs.size());
    std::printf("  1-4 KB plain:  %.2f allocations/line "
                "(per-character reference: %.2f)\n", longMine, longRef);
    CHECK(longMine <= 6.0);
    CHECK(longRef >= 12.0);
}

int main() {
    std::printf("=== inline markdown span tests ===\n");
    test_matches_the_reference_over_the_corpus();
    test_visible_text_drops_only_matched_markers();
    test_runs_carry_the_palette();
    test_allocates_per_run_not_per_character();
    if (g_failures == 0) {
        std::printf("ALL MD SPAN TESTS PASSED\n");
        return 0;
    }
    std::printf("%d MD SPAN TEST(S) FAILED\n", g_failures);
    return 1;
}
