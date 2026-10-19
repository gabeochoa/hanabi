#include <cstdio>
#include <string>
#include <vector>

#include "../../src/ui/work_stretch.h"

namespace ws = hanabi::stretch;
using K = ws::Kind;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

static ws::Block block(const std::vector<K>& v, std::size_t lo) {
    return ws::block_at(lo, v.size(), [&](std::size_t i) { return v[i]; });
}

int main() {
    std::printf("=== test_work_stretch ===\n");
    // Two runs with nothing between them fold; a lone run does not.
    {
        const std::vector<K> v{K::Step, K::Step, K::Step, K::Step, K::Boundary};
        const ws::Block b = block(v, 0);
        CHECK(b.lo == 0 && b.hi == 4 && b.runs == 1 && b.items == 1);
        CHECK(!ws::folds(b, v.size(), false));
    }
    {
        const std::vector<K> v{K::Step, K::Step, K::Done, K::Step, K::Boundary, K::Step};
        const ws::Block b = block(v, 0);
        CHECK(b.hi == 4 && b.runs == 2 && b.items == 3);
        CHECK(ws::folds(b, v.size(), false));
        CHECK(block(v, 5).runs == 1 && !ws::folds(block(v, 5), v.size(), false));
    }
    // A wake and a run are two items; a wake alone, or thinking alone, is not a stretch.
    {
        const std::vector<K> v{K::Wake, K::Step, K::Boundary};
        CHECK(ws::folds(block(v, 0), v.size(), false));
        const std::vector<K> w{K::Wake, K::Thinking, K::Boundary};
        CHECK(!ws::folds(block(w, 0), w.size(), false));
    }
    // A boundary row starts no block; the walk stops at one.
    {
        const std::vector<K> v{K::Boundary, K::Step, K::Wake, K::Step};
        CHECK(block(v, 0).empty());
        const ws::Block b = block(v, 1);
        CHECK(b.lo == 1 && b.hi == 4 && b.runs == 2 && b.items == 3);
    }
    // The run the server is working on stays out: a block that reaches the
    // end of a live thread does not fold; the same block folds once it ends.
    {
        const std::vector<K> v{K::Boundary, K::Wake, K::Step, K::Step, K::Done, K::Step};
        const ws::Block b = block(v, 1);
        CHECK(b.hi == 6);
        CHECK(!ws::folds(b, v.size(), true));
        CHECK(ws::folds(b, v.size(), false));
        const std::vector<K> w{K::Boundary, K::Wake, K::Step, K::Done, K::Step, K::Boundary};
        CHECK(ws::folds(block(w, 1), w.size(), true));
    }
    // Durations and the line.
    CHECK(ws::duration_label(0) == "0s");
    CHECK(ws::duration_label(42'000) == "42s");
    CHECK(ws::duration_label(252'000) == "4m 12s");
    CHECK(ws::duration_label(44'100'000) == "12h 15m");
    CHECK(ws::duration_label(3'600'000) == "1h");
    {
        ws::Facts f;
        f.runs = 15; f.steps = 61; f.wakes = 14; f.active_ms = 252'000; f.span_ms = 44'100'000;
        CHECK(ws::summary(f) == "Worked for 4m 12s across 12h 15m \xe2\x80\x94 14 wakes \xc2\xb7 15 runs \xc2\xb7 61 steps");
        f.errors = 1;
        CHECK(ws::summary(f).ends_with("61 steps \xc2\xb7 1 error"));
        ws::Facts g;
        g.runs = 1; g.steps = 1; g.span_ms = 5'000;
        CHECK(ws::summary(g) == "Worked for 5s \xe2\x80\x94 1 run \xc2\xb7 1 step");
        ws::Facts h;
        h.runs = 2; h.steps = 3;
        CHECK(ws::summary(h) == "Worked \xe2\x80\x94 2 runs \xc2\xb7 3 steps");
        ws::Facts onlyWakes;
        onlyWakes.wakes = 3;
        CHECK(ws::line(onlyWakes) == "Woke up 3 times");
        CHECK(ws::line(f) == ws::summary(f));
    }

    if (failures == 0) std::printf("OK\n");
    else std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
