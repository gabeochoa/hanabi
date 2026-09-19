#include <cmath>
#include <cstdio>

#include "../../src/ui/pin_geometry.h"

namespace pg = hanabi::pin_geometry;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 0.001f; }

static pg::Box bounds(const std::array<pg::Box, 5>& r) {
    pg::Box b = r[0];
    float right = b.x + b.w;
    float bottom = b.y + b.h;
    for (const pg::Box& q : r) {
        if (q.x < b.x) b.x = q.x;
        if (q.y < b.y) b.y = q.y;
        if (q.x + q.w > right) right = q.x + q.w;
        if (q.y + q.h > bottom) bottom = q.y + q.h;
    }
    b.w = right - b.x;
    b.h = bottom - b.y;
    return b;
}

int main() {
    std::printf("=== test_pin_geometry ===\n");
    const pg::Box slot{100.0f, 40.0f, 10.0f, 12.0f};
    const auto rest = pg::rects(slot, 1.0f, 1.0f);
    CHECK(near(rest[0].x, 101.0f) && near(rest[0].y, 41.0f) && near(rest[0].w, 6.0f) && near(rest[0].h, 2.0f));
    CHECK(near(rest[1].x, 102.0f) && near(rest[1].y, 43.0f) && near(rest[1].w, 4.0f) && near(rest[1].h, 3.0f));
    CHECK(near(rest[2].x, 101.0f) && near(rest[2].y, 46.0f) && near(rest[2].w, 6.0f) && near(rest[2].h, 2.0f));
    CHECK(near(rest[3].x, 102.0f) && near(rest[3].y, 48.0f) && near(rest[3].w, 4.0f) && near(rest[3].h, 1.0f));
    CHECK(near(rest[4].x, 103.0f) && near(rest[4].y, 49.0f) && near(rest[4].w, 2.0f) && near(rest[4].h, 2.0f));
    const pg::Box rb = bounds(rest);
    CHECK(near(rb.x, 101.0f) && near(rb.y, 41.0f) && near(rb.w, 6.0f) && near(rb.h, 10.0f));

    const auto lit = pg::rects(slot, 1.0f, 1.6f);
    const pg::Box lb = bounds(lit);
    CHECK(near(lb.w, 9.6f) && near(lb.h, 16.0f));
    CHECK(near(lb.x + lb.w * 0.5f, rb.x + rb.w * 0.5f));
    CHECK(near(lb.y + lb.h * 0.5f, rb.y + rb.h * 0.5f));
    CHECK(near(lb.x, 104.0f - 4.8f) && near(lb.y, 46.0f - 8.0f));

    const auto dev = pg::rects(pg::Box{200.0f, 80.0f, 20.0f, 24.0f}, 2.0f, 1.0f);
    const pg::Box db = bounds(dev);
    CHECK(near(db.x, 202.0f) && near(db.y, 82.0f) && near(db.w, 12.0f) && near(db.h, 20.0f));
    const auto devLit = pg::rects(pg::Box{200.0f, 80.0f, 20.0f, 24.0f}, 2.0f, 1.6f);
    const pg::Box dlb = bounds(devLit);
    CHECK(near(dlb.w, 19.2f) && near(dlb.h, 32.0f));
    CHECK(near(dlb.x + dlb.w * 0.5f, db.x + db.w * 0.5f) && near(dlb.y + dlb.h * 0.5f, db.y + db.h * 0.5f));

    const auto mid = pg::rects(slot, 1.0f, 1.3f);
    const pg::Box mb = bounds(mid);
    CHECK(near(mb.w, 7.8f) && near(mb.h, 13.0f) && near(mb.x + mb.w * 0.5f, rb.x + rb.w * 0.5f));

    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
