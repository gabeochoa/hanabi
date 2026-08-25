// Unit tests for hanabi::heapwalk — the live-block metric the soak verdicts
// and scripts/scroll_gate.sh's memory arm are built on.
//
// WHY THIS EXISTS. The metric used to be the sum of
// `malloc_zone_statistics().blocks_in_use` over the process's zones, and that
// number is the allocator's own tally rather than a count of live blocks. It
// drifts. On the app it drifted by a thousand blocks over a run that allocated
// nothing net, which made scroll_gate.sh's block arm red about one run in five
// and got a retry bolted on to hide it.
//
// The case below is that drift, small enough to run in a second and with no
// Metal, no ECS and no window: churn a couple of hundred thousand small
// allocations and free every one of them. Nothing is retained, so a live-block
// count must come back to where it started. The tally does not — it reports
// several hundred blocks that are not there, the same number every round,
// which is why this is a test and not a flake.
//
// Measured on gabeochoa-mac-GRQ7Y259H4, three rounds of 200,000 x 48 bytes:
//
//   round   exact walk   zone tally
//   0             +2         +343
//   1             +2         +343
//   2             +2         +343
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "../../src/util/heap_walk.h"

static int g_failures = 0;
#define CHECK(cond)                                                 \
    do {                                                            \
        if (!(cond)) {                                              \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++g_failures;                                           \
        }                                                           \
    } while (0)

using hanabi::heapwalk::Live;
using hanabi::heapwalk::live;

// Churn that retains nothing. `volatile` on the sink so a release build cannot
// decide the whole loop is dead and delete the thing under test.
static void churn(int n, size_t sz) {
    std::vector<void*> v;
    v.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        void* p = std::malloc(sz);
        *static_cast<volatile char*>(p) = 1;
        v.push_back(p);
    }
    for (void* p : v) std::free(p);
}

// A walk of a heap nothing has changed reads the same both times.
static void test_walk_is_repeatable() {
    std::printf("test_walk_is_repeatable\n");
    std::vector<void*> keep;
    for (int i = 0; i < 500; ++i) keep.push_back(std::malloc(96));
    const Live a = live();
    const Live b = live();
    const int d = static_cast<int>(b.count) - static_cast<int>(a.count);
    CHECK(d >= -8 && d <= 8);
    CHECK(a.count > 0);
    for (void* p : keep) std::free(p);
}

// The metric moves by what was retained, and by that only.
static void test_counts_what_is_retained() {
    std::printf("test_counts_what_is_retained\n");
    const Live before = live();
    std::vector<void*> keep;
    keep.reserve(5000);
    for (int i = 0; i < 5000; ++i) keep.push_back(std::malloc(512));
    const Live held = live();
    const int grew = static_cast<int>(held.count) - static_cast<int>(before.count);
    CHECK(grew >= 5000);
    CHECK(grew <= 5100);
    CHECK(held.bytes >= before.bytes + 5000u * 512u);
    for (void* p : keep) std::free(p);
    const Live after = live();
    const int net = static_cast<int>(after.count) - static_cast<int>(before.count);
    CHECK(net >= -32 && net <= 32);
}

// THE ONE THIS FILE IS FOR. Free everything you allocate and the count comes
// back. The zone tally does not, by several hundred blocks, every round.
static void test_churn_does_not_drift() {
    std::printf("test_churn_does_not_drift\n");
    std::vector<void*> keep;
    for (int i = 0; i < 2000; ++i) keep.push_back(std::malloc(48));
    const Live base = live();

    for (int round = 0; round < 3; ++round) {
        churn(200000, 48);
        const Live now = live();
        const int walked =
            static_cast<int>(now.count) - static_cast<int>(base.count);
        const int tallied =
            static_cast<int>(now.approxCount) - static_cast<int>(base.approxCount);
        std::printf("  round %d: walk %+d, zone tally %+d\n", round, walked,
                    tallied);
        CHECK(walked >= -32 && walked <= 32);
    }
    for (void* p : keep) std::free(p);
}

// A zone with no usable enumerator must not read as "everything was freed".
static void test_never_reports_empty() {
    std::printf("test_never_reports_empty\n");
    const Live l = live();
    CHECK(l.count > 100);
    CHECK(l.bytes > 1024);
}

int main() {
    std::printf("=== heap_walk unit tests ===\n");
    test_walk_is_repeatable();
    test_counts_what_is_retained();
    test_churn_does_not_drift();
    test_never_reports_empty();
    if (g_failures == 0) {
        std::printf("All heap_walk tests passed.\n");
        return 0;
    }
    std::printf("%d heap_walk test(s) FAILED.\n", g_failures);
    return 1;
}
