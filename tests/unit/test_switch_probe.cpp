#include <cstdio>
#include <string>

#include "../../src/util/switch_probe.h"

namespace sp = hanabi::switch_probe;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_switch_probe ===\n");
    sp::Probe p;
    CHECK(!p.armed());
    CHECK(!p.end("t1", 1000, 1, 5, false));
    p.begin("t1", 1000, 10);
    CHECK(p.armed());
    CHECK(!p.end("t2", 2000, 11, 5, false));
    CHECK(!p.end("t1", 2000, 11, 0, false));
    CHECK(p.end("t1", 13500, 12, 40, false));
    CHECK(!p.armed() && p.opens().size() == 1);
    CHECK(p.opens()[0].ms == 12.5 && p.opens()[0].frames == 2 && p.opens()[0].rows == 40);
    p.begin("t3", 20000, 20);
    p.begin("t4", 21000, 21);
    CHECK(!p.end("t3", 22000, 22, 3, true));
    CHECK(p.end("t4", 22000, 22, 3, true));
    p.begin("t5", 30000, 30);
    CHECK(p.end("t5", 31000, 30, 1, true));
    const auto s = p.summary();
    CHECK(s.count == 3 && s.cached == 2);
    CHECK(s.min_ms == 1.0 && s.max_ms == 12.5);
    CHECK(s.p50_ms == 1.0);
    CHECK(s.p95_ms > 11.0 && s.p95_ms < 12.5);
    CHECK(s.p50_frames == 1.0);
    CHECK(sp::percentile({}, 0.5) == 0.0);
    CHECK(sp::percentile({3.0, 1.0, 2.0}, 0.5) == 2.0);
    CHECK(sp::percentile({1.0, 2.0}, 0.5) == 1.5);
    CHECK(sp::summarize({}).count == 0);
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
