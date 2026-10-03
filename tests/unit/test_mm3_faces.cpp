#include <cstdio>

#include "../../src/ui/mm3_faces.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace m = hanabi::mm3;

int main() {
    // Every face has cells, every cell has area, and every face but the
    // wave (whose raised hand reaches above the shared box, as in the source)
    // sits inside the shared box.
    for (m::Face f : {m::Face::Neutral, m::Face::Blink, m::Face::Wink, m::Face::Wave, m::Face::Tired,
                      m::Face::Angry}) {
        const auto c = m::cells(f);
        CHECK(c.size >= 11);
        for (std::size_t i = 0; i < c.size; ++i) {
            const auto& k = c.data[i];
            CHECK(k.w > 0 && k.h > 0);
            CHECK(k.x >= m::kBoxX && k.x + k.w <= m::kBoxX + m::kBoxW + 0.5f);
            if (f != m::Face::Wave) CHECK(k.y >= m::kBoxY && k.y + k.h <= m::kBoxY + m::kBoxH + 0.5f);
        }
    }
    CHECK(m::cells(m::Face::Wave).size == 63 && m::cells(m::Face::Neutral).size == 11);
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    return 1;
}
