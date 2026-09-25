#include <cmath>
#include <cstdio>
#include <limits>

#include "../../src/text_zoom.h"

namespace tz = hanabi::text_zoom;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main() {
    std::printf("=== test_text_zoom ===\n");
    CHECK(tz::kDefault == 1.0 && tz::kMin == 0.7 && tz::kMax == 2.0 && tz::kStep == 0.1);
    CHECK(near(tz::sanitize(1.0), 1.0));
    CHECK(near(tz::sanitize(0.0), 0.7));
    CHECK(near(tz::sanitize(-3.0), 0.7));
    CHECK(near(tz::sanitize(std::numeric_limits<double>::quiet_NaN()), 1.0));
    CHECK(near(tz::sanitize(std::numeric_limits<double>::infinity()), 1.0));
    CHECK(near(tz::sanitize(0.2), 0.7));
    CHECK(near(tz::sanitize(9.0), 2.0));
    CHECK(near(tz::sanitize(1.35), 1.35));
    CHECK(tz::sanitize(1.01) != tz::sanitize(1.0));
    CHECK(near(tz::sanitize(1.01), 1.01));

    double s = 1.0;
    for (int i = 0; i < 3; ++i) s = tz::zoomed_in(s);
    CHECK(near(s, 1.3));
    for (int i = 0; i < 20; ++i) s = tz::zoomed_in(s);
    CHECK(near(s, 2.0));
    CHECK(near(tz::zoomed_in(2.0), 2.0));
    for (int i = 0; i < 40; ++i) s = tz::zoomed_out(s);
    CHECK(near(s, 0.7));
    CHECK(near(tz::zoomed_out(0.7), 0.7));
    CHECK(near(tz::zoomed_out(1.0), 0.9));
    CHECK(near(tz::zoomed_in(0.9), 1.0));
    const auto on_grid = [](double v) {
        const double k = v / tz::kStep;
        return std::fabs(k - std::round(k)) < 1e-6 && v >= tz::kMin - 1e-9 && v <= tz::kMax + 1e-9;
    };
    CHECK(on_grid(tz::zoomed_in(1.35)) && tz::zoomed_in(1.35) > 1.35);
    CHECK(on_grid(tz::zoomed_out(1.35)) && tz::zoomed_out(1.35) < 1.35);
    double walk = tz::kMin;
    for (int i = 0; i < 13; ++i) {
        walk = tz::zoomed_in(walk);
        CHECK(on_grid(walk));
    }
    CHECK(near(walk, tz::kMax));
    CHECK(near(tz::zoomed_in(0.0), 0.8));
    CHECK(near(tz::snap(1.04), 1.0) && near(tz::snap(1.06), 1.1));
    CHECK(near(tz::snap(0.1), 0.7) && near(tz::snap(5.0), 2.0));
    {
        float face = 1.17185f;
        float body = 13.0f * face;
        const auto set = [&](float s) {
            face = s;
            body = 13.0f * s;
        };
        {
            tz::PointScaleScope scope(face, 1.3, set);
            CHECK(std::fabs(face - 1.17185f * 1.3f) < 1e-5f);
            CHECK(std::fabs(body - 13.0f * 1.17185f * 1.3f) < 1e-4f);
        }
        CHECK(std::fabs(face - 1.17185f) < 1e-6f);
        CHECK(std::fabs(body - 13.0f * 1.17185f) < 1e-5f);
        int sets = 0;
        {
            tz::PointScaleScope idle(face, 1.0, [&](float s) {
                ++sets;
                face = s;
            });
            CHECK(sets == 0);
        }
        CHECK(sets == 1 && std::fabs(face - 1.17185f) < 1e-6f);
        {
            tz::PointScaleScope tiny(face, 1.01, set);
            CHECK(std::fabs(face - 1.17185f * 1.01f) < 1e-5f);
        }
        CHECK(std::fabs(face - 1.17185f) < 1e-6f);
    }
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
