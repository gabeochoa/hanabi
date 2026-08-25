// Unit tests for hanabi::trend — the estimator the soak verdict is built on.
//
// WHY THIS EXISTS AS A UNIT TEST. The soak gate's whole job is to answer "is
// this number going up", and until this file the answer came from a
// subtraction of two buckets that nothing tested. A leak detector that is
// itself wrong is worse than none: it reports a clean tree as leaking, gets
// disabled, and then the real leak ships. Every case below is a shape the
// probe has actually produced on this machine.
//
// The estimator is deliberately dependency-free (no Metal, no ECS, no window)
// precisely so this file can exist.
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "../../src/util/trend.h"

static int g_failures = 0;
#define CHECK(cond)                                                 \
    do {                                                            \
        if (!(cond)) {                                              \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++g_failures;                                           \
        }                                                           \
    } while (0)

using hanabi::trend::Point;
using hanabi::trend::Trend;
using hanabi::trend::theil_sen;

static bool near(double a, double b, double tol) {
    const double d = a - b;
    return (d < 0 ? -d : d) <= tol;
}

// Buckets the way the soak probe produces them: one reading every `every`
// frames, starting at `every`.
static std::vector<Point> buckets(std::initializer_list<double> ys,
                                  double every = 250.0) {
    std::vector<Point> out;
    double x = every;
    for (double y : ys) {
        out.push_back({x, y});
        x += every;
    }
    return out;
}

static void test_a_flat_series_has_no_slope() {
    const Trend t = theil_sen(buckets({38128, 38128, 38128, 38128, 38128, 38128}));
    CHECK(t.valid);
    CHECK(t.points == 6);
    CHECK(t.pairs == 15);
    CHECK(near(t.per1k, 0.0, 1e-9));
    // Nothing rose, so nothing rose. The column has to be able to say zero.
    CHECK(near(t.rising, 0.0, 1e-9));
}

static void test_a_steady_leak_reads_its_own_rate() {
    // The Metal leak, to scale: +704 KB every 250-frame bucket is
    // +2816 KB per 1000 frames, which is what the defective binary measured.
    const Trend t = theil_sen(
        buckets({38128, 38832, 39536, 40240, 40944, 41648}));
    CHECK(near(t.per1k, 2816.0, 0.5));
    // A leak adds on every single bucket. That is what makes it a leak, and
    // it is the signal that separates one from a busy machine.
    CHECK(near(t.rising, 1.0, 1e-9));
    CHECK(!t.degraded);
}

// THE ONE THIS WAS WRITTEN FOR.
//
// A page fault at one bucket, given back at the next: the exact shape
// docs/perf/MEMORY.md records ("that +192 KB sample came with a heap delta of
// MINUS 0.8 KB — pages faulted in by something other than the app's own
// allocations"). The series is flat except for one bucket.
//
// The OLD estimator subtracted an early bucket from the LAST one. Put the
// spike on the last bucket and that subtraction reports the whole spike as
// the run's growth; put it on the early anchor and it reports the negative of
// it. Both are verdicts about a tree with no leak in it.
static void test_one_spiked_bucket_does_not_move_the_verdict() {
    // Flat at 38128 with a single +2048 KB bucket at the END.
    const std::vector<Point> spikedLast =
        buckets({38128, 38128, 38128, 38128, 38128, 40176});
    const Trend t = theil_sen(spikedLast);

    // What the two-point estimator this replaced would have said: last minus
    // the second bucket, over the frames between them.
    const double twoPoint = (spikedLast.back().y - spikedLast[1].y) /
                            (spikedLast.back().x - spikedLast[1].x) * 1000.0;
    CHECK(near(twoPoint, 2048.0, 0.5));      // it fails a 512 KB budget
    CHECK(t.per1k <= 512.0);                 // the median does not
    CHECK(near(t.per1k, 0.0, 1e-9));

    // And the confidence column says why the reader should not be alarmed:
    // only the pairs that end on the spike rose — 5 of 15.
    CHECK(near(t.rising, 5.0 / 15.0, 1e-9));

    // The same spike on the early anchor, which is the other half of the
    // trap: the old estimator reports the tree as IMPROVING by 2 MB.
    const std::vector<Point> spikedEarly =
        buckets({38128, 40176, 38128, 38128, 38128, 38128});
    const double twoPointEarly = (spikedEarly.back().y - spikedEarly[1].y) /
                                 (spikedEarly.back().x - spikedEarly[1].x) *
                                 1000.0;
    CHECK(twoPointEarly < -400.0);
    CHECK(near(theil_sen(spikedEarly).per1k, 0.0, 1e-9));
}

// A real leak must still fail even with a spike in it, or the robustness
// above would have been bought by going blind.
static void test_a_leak_with_a_spike_in_it_still_reads_as_a_leak() {
    const Trend t = theil_sen(
        buckets({38128, 38832, 41000, 40240, 40944, 41648}));
    CHECK(t.per1k > 2000.0);
    CHECK(t.rising > 0.8);
}

static void test_two_points_is_the_old_two_point_delta() {
    // Not a regression to apologise for: a run too short to do better should
    // get the previous behaviour and be TOLD, rather than be refused.
    const std::vector<Point> pts = buckets({38128, 38832});
    const Trend t = theil_sen(pts);
    CHECK(t.valid);
    CHECK(t.degraded);
    CHECK(near(t.per1k, (38832.0 - 38128.0) / 250.0 * 1000.0, 1e-9));
}

static void test_too_few_points_is_not_flat() {
    // The distinction the whole third outcome rests on. `per1k` is 0.0 in
    // both of these, and reading that as "flat" is how a gate stops gating.
    const Trend none = theil_sen({});
    CHECK(!none.valid);
    CHECK(none.points == 0);
    const Trend one = theil_sen(buckets({38128}));
    CHECK(!one.valid);
    CHECK(one.points == 1);
}

static void test_duplicate_x_cannot_produce_an_infinity() {
    // Two readings at the same frame say nothing about a slope. Dividing by
    // the zero gap would put an inf into the median and poison every column
    // downstream of it.
    std::vector<Point> pts = {{250, 100}, {250, 900}, {500, 100}, {750, 100}};
    const Trend t = theil_sen(pts);
    CHECK(t.valid);
    CHECK(t.per1k > -1e9 && t.per1k < 1e9);
}

static void test_the_median_is_a_real_median_at_even_counts() {
    // Three points give three pairs (odd); four give six (even), and the even
    // case takes the mean of the two middles. Written down because an
    // off-by-one here biases every verdict low or high by half a step and
    // would never be noticed.
    const Trend odd = theil_sen(buckets({0, 100, 200}));
    CHECK(near(odd.per1k, 400.0, 1e-9));
    const Trend even = theil_sen(buckets({0, 100, 200, 300}));
    CHECK(near(even.per1k, 400.0, 1e-9));
}

int main() {
    std::printf("test_trend:\n");
    test_a_flat_series_has_no_slope();
    test_a_steady_leak_reads_its_own_rate();
    test_one_spiked_bucket_does_not_move_the_verdict();
    test_a_leak_with_a_spike_in_it_still_reads_as_a_leak();
    test_two_points_is_the_old_two_point_delta();
    test_too_few_points_is_not_flat();
    test_duplicate_x_cannot_produce_an_infinity();
    test_the_median_is_a_real_median_at_even_counts();
    if (g_failures == 0) {
        std::printf("  all trend tests passed\n");
        return 0;
    }
    std::printf("  %d FAILURES\n", g_failures);
    return 1;
}
