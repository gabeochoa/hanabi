#include <cstdio>

#include "../../src/ui/mm3_marks.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace m = hanabi::mm3;
using G = ecs::model::StatusGlyph;

int main() {
    // The draft's mapping (D123162830): the rows waiting on you loop angry
    // and keep their own faces still; a failed run is the still angry face.
    const auto asking = m::mark_for(G::Waiting, false);
    CHECK(asking && asking->moves == m::Animation::Angry && asking->urgent && asking->still == m::Face::Wave);
    const auto blocked = m::mark_for(G::Blocked, false);
    CHECK(blocked && blocked->moves == m::Animation::Angry && blocked->urgent && blocked->still == m::Face::Tired);
    const auto failed = m::mark_for(G::Blocked, true);
    CHECK(failed && !failed->moves && failed->still == m::Face::Angry);
    const auto working = m::mark_for(G::Running, false);
    CHECK(working && working->moves == m::Animation::Blink && !working->urgent);
    CHECK(m::mark_for(G::Done, false)->still == m::Face::Wink);
    CHECK(m::mark_for(G::Idle, false)->still == m::Face::Smile && !m::mark_for(G::Idle, false)->moves);
    CHECK(!m::mark_for(G::Frozen, false) && !m::mark_for(G::Paused, false));

    // Motion gates: urgent keeps moving behind another app, calm does not;
    // hidden or Reduce Motion stills both.
    CHECK(m::plays(*asking, false, false, true));
    CHECK(!m::plays(*working, false, false, true));
    CHECK(m::plays(*working, false, true, true));
    CHECK(!m::plays(*asking, false, true, false));
    CHECK(!m::plays(*asking, true, true, true));
    CHECK(!m::plays(*failed, false, true, true));

    // Timing: angry loops end to end (2 s, no rest); blink plays 1.4 s then
    // rests on its first frame for 4.5 s.
    const m::Anim angry = m::anim(m::Animation::Angry);
    CHECK(angry.frameCount == 39 && angry.steps == 40 && angry.duration == 2.0);
    CHECK(m::frame_at(angry, 0.0, m::kUrgentRest) == 0);
    CHECK(m::frame_at(angry, 0.09, m::kUrgentRest) == 2);  // 0.045 of 2 s -> key 0.04, step 2
    CHECK(m::frame_at(angry, 1.99, m::kUrgentRest) == 36);  // the last step repeats frame 36
    CHECK(m::frame_at(angry, 2.09, m::kUrgentRest) == 2);  // and loops at once
    const m::Anim blink = m::anim(m::Animation::Blink);
    CHECK(blink.frameCount == 6 && blink.duration == 1.4);
    CHECK(m::frame_at(blink, 0.56, m::kRest) == 2);  // 0.4 of 1.4 s
    CHECK(m::frame_at(blink, 3.0, m::kRest) == 0);   // resting
    // Every frame of both fits the row box whole -- the point of the box.
    for (const m::Anim& a : {angry, blink})
        for (std::size_t f = 0; f < a.frameCount; ++f) {
            const auto c = m::frame_cells(a, f);
            CHECK(c.size > 0);
            for (std::size_t i = 0; i < c.size; ++i) {
                CHECK(c.data[i].x >= m::kRowX && c.data[i].x + c.data[i].w <= m::kRowX + m::kRowW);
                CHECK(c.data[i].y >= m::kRowY && c.data[i].y + c.data[i].h <= m::kRowY + m::kRowH);
            }
        }
    for (m::Face f : {m::Face::Wave, m::Face::Tired, m::Face::Angry, m::Face::Smile, m::Face::Wink, m::Face::Blink}) {
        const auto c = m::cells(f);
        for (std::size_t i = 0; i < c.size; ++i)
            CHECK(c.data[i].y >= m::kRowY && c.data[i].y + c.data[i].h <= m::kRowY + m::kRowH);
    }
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    return 1;
}
