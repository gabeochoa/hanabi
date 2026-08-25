#pragma once

// ---------------------------------------------------------------------------
// Is this series going up? — the estimator, with no dependencies.
//
// Split out of soak.h so it can be tested without a Metal device, an ECS or a
// window. The soak probe is the only caller today; the stress driver's
// failure conditions (util/stress.h) are the second.
//
// WHAT IT REPLACED. The soak verdict used to be one subtraction: an early
// bucket against the last one. A two-point estimate of a trend carries the
// full noise of both its endpoints — one bucket that faulted in an extra page
// moves the whole verdict, and nothing in the reading says whether it did.
//
// THEIL-SEN: the median over all pairs (i<j) of (y_j - y_i) / (x_j - x_i).
// Two properties earn it here:
//
//   * A ~29% breakdown point. RSS is page-granular and arrives in jumps; a
//     run where the allocator faults 200 KB in at one bucket and gives it back
//     at the next drags an ordinary-least-squares line and does not move this
//     at all. OLS was written first and is strictly worse on this data.
//   * At two points it IS the two-point delta, exactly. A run too short to do
//     better is not refused and is not silently different — it is the old
//     behaviour, and `degraded` says so out loud.
//
// AND `rising`: the fraction of pairs whose slope is positive. A leak adds on
// every bucket, so essentially every pair increases and it reads 1.00; noise
// is a coin and reads about 0.50. One number cannot carry that, and it is the
// difference between a verdict a reader believes and one they re-run.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cstddef>
#include <vector>

namespace hanabi::trend {

struct Point {
    double x = 0.0;
    double y = 0.0;
};

struct Trend {
    // Median pairwise slope, in y's units per 1000 units of x.
    double per1k = 0.0;
    // Fraction of pairs with a strictly positive slope. Meaningless below
    // three points, where it can only read 0.00 or 1.00; `points` says so and
    // callers are expected to suppress the column rather than print a figure
    // that will be quoted.
    double rising = 0.0;
    int points = 0;
    long pairs = 0;
    // Two points is the old two-point delta wearing a new name.
    bool degraded = false;
    // False when there was nothing to fit. A caller must not read `per1k` as
    // "flat" in that case: it is "unmeasured", and the two have opposite
    // consequences for a gate.
    bool valid = false;
};

inline double median_of(std::vector<double>& xs) {
    const size_t n = xs.size();
    const size_t mid = n / 2;
    std::nth_element(xs.begin(), xs.begin() + static_cast<std::ptrdiff_t>(mid),
                     xs.end());
    const double hi = xs[mid];
    if (n % 2 == 1) return hi;
    // The lower of the two middles is the max of the left partition, which
    // nth_element has already put there — no second full sort.
    const double lo =
        *std::max_element(xs.begin(), xs.begin() + static_cast<std::ptrdiff_t>(mid));
    return (lo + hi) / 2.0;
}

inline Trend theil_sen(const std::vector<Point>& pts) {
    Trend t;
    t.points = static_cast<int>(pts.size());
    if (pts.size() < 2) return t;

    std::vector<double> slopes;
    slopes.reserve(pts.size() * (pts.size() - 1) / 2);
    long up = 0;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        for (size_t j = i + 1; j < pts.size(); ++j) {
            const double dx = pts[j].x - pts[i].x;
            // Two readings at the same x say nothing about a slope. Skipping
            // rather than dividing keeps one duplicated sample from producing
            // an infinity that then poisons the median.
            if (dx <= 0.0) continue;
            const double dy = pts[j].y - pts[i].y;
            slopes.push_back(dy / dx * 1000.0);
            if (dy > 0.0) ++up;
        }
    }
    if (slopes.empty()) return t;

    t.pairs = static_cast<long>(slopes.size());
    t.rising = static_cast<double>(up) / static_cast<double>(t.pairs);
    t.per1k = median_of(slopes);
    t.degraded = pts.size() < 3;
    t.valid = true;
    return t;
}

}  // namespace hanabi::trend
