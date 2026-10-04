#include <cmath>
#include <cstdio>

#include "../../src/ui/image_markup.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace mk = hanabi::markup;

static bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

int main() {
    // Never enlarged: a 200x100 picture in a 900x600 canvas stays 200x100, centred.
    const mk::Rect small = mk::fitted_rect(200, 100, 900, 600);
    CHECK(near(small.w, 200) && near(small.h, 100) && near(small.x, 350) && near(small.y, 250));
    // Shrunk to fit: 2000x1000 in 1000x1000 -> 1000x500.
    const mk::Rect big = mk::fitted_rect(2000, 1000, 1000, 1000);
    CHECK(near(big.w, 1000) && near(big.h, 500) && near(big.y, 250));
    // A canvas point maps to image pixels and back.
    const mk::Point ip = mk::image_point({500, 500}, 2000, 1000, 1000, 1000);
    CHECK(near(ip.x, 1000) && near(ip.y, 500));
    const mk::Point cp = mk::canvas_point(ip, 2000, 1000, 1000, 1000);
    CHECK(near(cp.x, 500) && near(cp.y, 500));
    // Off the picture clamps rather than drops.
    const mk::Point off = mk::image_point({-50, 10}, 2000, 1000, 1000, 1000);
    CHECK(near(off.x, 0) && near(off.y, 0));
    CHECK(!mk::worth_keeping({0, 0}, {3, 4}) && mk::worth_keeping({0, 0}, {6, 0}));
    CHECK(near(mk::stroke_width(100, 100), 2) && near(mk::stroke_width(9000, 9000), 14) &&
          near(mk::stroke_width(1800, 900), 5));
    const auto head = mk::arrow_head({0, 0}, {100, 0}, 4);
    CHECK(head.size() == 3 && near(head[0].x, 100) && near(head[1].x, 82) && near(head[1].y, 8.1f) &&
          near(head[2].y, -8.1f));
    CHECK(mk::arrow_head({5, 5}, {5, 5}, 4).empty());
    CHECK(mk::arrow_head({0, 0}, {3, 0}, 4)[1].x <= 0.01f);  // head never longer than the arrow
    const mk::Rect b = mk::box({30, 40}, {10, 5});
    CHECK(near(b.x, 10) && near(b.y, 5) && near(b.w, 20) && near(b.h, 35));
    mk::Markup m;
    CHECK(m.empty() && !m.undo());
    m.add({mk::Tool::Box, {0, 0}, {1, 1}});
    m.add({mk::Tool::Arrow, {0, 0}, {9, 9}});
    CHECK(m.undo() && m.strokes.size() == 1 && m.strokes[0].tool == mk::Tool::Box);
    // Crop: the newest wins, in whole pixels inside the picture; under 2 px is none;
    // undo takes it back like a mark; a crop is not a painted mark.
    std::vector<mk::Stroke> st{{mk::Tool::Crop, {10.4f, 5.6f}, {50.2f, 40.1f}}};
    auto c = mk::effective_crop(st, 64, 64);
    CHECK(c && near(c->x, 10) && near(c->y, 5) && near(c->w, 41) && near(c->h, 36));
    st.push_back({mk::Tool::Crop, {80, 80}, {-5, 30}});  // dragged up-left, past the edges
    c = mk::effective_crop(st, 64, 64);
    CHECK(c && near(c->x, 0) && near(c->y, 30) && near(c->w, 64) && near(c->h, 34));
    CHECK(!mk::effective_crop({{mk::Tool::Crop, {3, 3}, {4, 40}}}, 64, 64));
    CHECK(!mk::effective_crop({{mk::Tool::Box, {3, 3}, {40, 40}}}, 64, 64));
    st.push_back({mk::Tool::Redact, {0, 0}, {10, 10}});
    CHECK(mk::painted_count(st) == 1);
    // A drag clamped to a line along an edge leaves nothing of area.
    CHECK(!mk::has_extent({mk::Tool::Redact, {0, 0}, {22, 0}}));
    CHECK(!mk::has_extent({mk::Tool::Crop, {5, 5}, {5, 40}}));
    CHECK(mk::has_extent({mk::Tool::Box, {5, 5}, {9, 9}}) && mk::has_extent({mk::Tool::Arrow, {0, 0}, {0, 3}}));
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    return 1;
}
