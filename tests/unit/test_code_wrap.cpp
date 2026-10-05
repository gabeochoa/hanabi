#include <cstdio>
#include <string>

#include "../../src/ui/code_wrap.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace cw = hanabi::code_wrap;

static std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& p : v) s += p;
    return s;
}

int main() {
    // Fits: one piece.
    CHECK(cw::wrap("x = 1", 20).size() == 1);
    // Breaks between tokens, never inside an identifier.
    const std::string line = "total = compute_total(alpha, beta)";
    const auto w = cw::wrap(line, 16);
    CHECK(join(w) == line);
    for (const auto& p : w) CHECK(p.size() <= 16);
    CHECK(w[0] == "total = ");
    CHECK(w[1] == "compute_total(");
    CHECK(w[2] == "alpha, beta)");
    // A token longer than the line is cut at the limit, and nothing is lost.
    const auto l = cw::wrap("abcdefghijklmnop", 5);
    CHECK(l.size() == 4 && l[0] == "abcde" && join(l) == "abcdefghijklmnop");
    // Code points, not bytes: a multibyte word counts by characters and is
    // not split mid-sequence.
    const std::string u = "s = \"\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\"";
    const auto uw = cw::wrap(u, 6);
    CHECK(join(uw) == u);
    for (const auto& p : uw)
        CHECK(p.empty() || (static_cast<unsigned char>(p[0]) & 0xC0) != 0x80);
    CHECK(cw::count("", 10) == 1);
    // A row never starts with the space that separated it from the last one.
    const auto sp = cw::wrap("call(a, bb, cc)", 9);
    CHECK(join(sp) == "call(a, bb, cc)");
    for (const auto& p : sp) CHECK(p.empty() || p[0] != ' ');
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    return 1;
}
