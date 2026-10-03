#include <cstdio>
#include <cmath>

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
    // As landed (D123167028, D123182666): review winks calmly; paused sleeps
    // (a cycle); frozen is confused (calm); idle smiles, still.
    const auto review = m::mark_for(G::Done, false);
    CHECK(review && review->still == m::Face::Wink && review->moves == m::Animation::Wink && !review->urgent);
    CHECK(m::mark_for(G::Idle, false)->still == m::Face::Smile && !m::mark_for(G::Idle, false)->moves);
    const auto paused = m::mark_for(G::Paused, false);
    CHECK(paused && paused->still == m::Face::Sleeping && paused->moves == m::Animation::Sleeping);
    CHECK(m::anim(m::Animation::Sleeping).loops && m::anim(m::Animation::Thinking).loops);
    CHECK(m::rest_for(m::anim(m::Animation::Sleeping), false) == 0.0);
    CHECK(m::rest_for(m::anim(m::Animation::Wink), false) == m::kRest);
    const auto frozen = m::mark_for(G::Frozen, false);
    CHECK(frozen && frozen->still == m::Face::Confused && frozen->moves == m::Animation::Confused);

    // Moments. Thinking: a live run quiet 3 minutes, its OWN run open.
    const std::int64_t now = 2'000'000'000'000;
    CHECK(m::thinking_now(G::Running, true, now - 181'000, now));
    CHECK(!m::thinking_now(G::Running, true, now - 179'000, now));
    CHECK(!m::thinking_now(G::Running, false, now - 600'000, now));  // a child's run, not its own
    CHECK(!m::thinking_now(G::Idle, true, now - 600'000, now));
    m::Moment think;
    think.thinking = true;
    const auto thinking = m::mark_for(G::Running, false, think);
    CHECK(thinking && thinking->moves == m::Animation::Thinking && thinking->still == m::Face::Neutral);
    // Stale: an idle row untouched a day turns tired, still.
    CHECK(m::stale_now(G::Idle, now - 86'401'000, now) && !m::stale_now(G::Idle, now - 86'399'000, now));
    CHECK(!m::stale_now(G::Done, now - 999'999'000, now));
    m::Moment stale;
    stale.stale = true;
    CHECK(m::mark_for(G::Idle, false, stale)->still == m::Face::Tired && !m::mark_for(G::Idle, false, stale)->moves);
    // The finish wave: only a real completion, once, then the settled face.
    using P = m::RunPhase;
    CHECK(m::finishes(P{G::Running, true}, P{G::Idle, false}));
    CHECK(m::finishes(P{G::Running, true}, P{G::Done, false}));
    CHECK(!m::finishes(P{G::Running, true}, P{G::Waiting, false}));  // asking
    CHECK(!m::finishes(P{G::Running, true}, P{G::Blocked, false}));  // blocked or failed
    CHECK(!m::finishes(P{G::Running, true}, P{G::Done, true}));      // the run is still open
    CHECK(!m::finishes(P{G::Idle, false}, P{G::Done, false}));       // never live
    m::Moment finish;
    finish.finishing = true;
    finish.finishSince = 0.5f;
    const auto wave = m::mark_for(G::Done, false, finish);
    CHECK(wave && wave->moves == m::Animation::Wave && wave->once && !wave->urgent && wave->still == m::Face::Wink);
    CHECK(m::plays(*wave, false, false, true));  // a finish waves behind another app too
    finish.finishSince = 2.01f;                   // past the wave: the settled face
    CHECK(m::mark_for(G::Done, false, finish)->moves == m::Animation::Wink);
    finish.finishSince = 0.5f;
    CHECK(m::mark_for(G::Waiting, false, finish)->moves == m::Animation::Angry);  // asking never waves
    const m::Anim waveAnim = m::anim(m::Animation::Wave);
    CHECK(m::frame_once(waveAnim, 5.0) == waveAnim.timeline[waveAnim.steps - 1]);  // holds the last frame
    // The shelf: Home and Pinned blink, in their own boxes.
    CHECK(m::shelf_motion(m::Face::Smile) == m::Animation::SmileBlink);
    CHECK(m::shelf_motion(m::Face::Heart) == m::Animation::HeartBlink);
    CHECK(!m::shelf_motion(m::Face::Tired) && !m::shelf_motion(m::Face::Wink));
    CHECK(m::anim(m::Animation::HeartBlink).box.y == m::kHeartY && m::anim(m::Animation::HeartBlink).box.h == m::kHeartH);

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
    // When the frame next changes: the loop wakes then, not at the display rate.
    CHECK(std::abs(m::next_change_in(angry, 0.0, m::kUrgentRest) - 0.04) < 1e-6);   // step 1 at 0.02*2s
    CHECK(std::abs(m::next_change_in(blink, 0.0, m::kRest) - 0.52) < 1e-6);        // 0.371429*1.4
    CHECK(std::abs(m::next_change_in(blink, 2.0, m::kRest) - 3.9) < 1e-6);         // resting: until the loop
    CHECK(m::next_change_in(angry, 1.999, m::kUrgentRest) >= 0.001);
    // Every frame of every row animation fits the row box whole -- the point
    // of the box.
    for (const m::Anim& a : {angry, blink, m::anim(m::Animation::Wink), m::anim(m::Animation::Wave),
                             m::anim(m::Animation::Thinking), m::anim(m::Animation::Sleeping),
                             m::anim(m::Animation::Confused)})
        for (std::size_t f = 0; f < a.frameCount; ++f) {
            const auto c = m::frame_cells(a, f);
            CHECK(c.size > 0);
            for (std::size_t i = 0; i < c.size; ++i) {
                CHECK(c.data[i].x >= m::kRowX && c.data[i].x + c.data[i].w <= m::kRowX + m::kRowW);
                CHECK(c.data[i].y >= m::kRowY && c.data[i].y + c.data[i].h <= m::kRowY + m::kRowH);
            }
        }
    for (m::Face f : {m::Face::Wave, m::Face::Tired, m::Face::Angry, m::Face::Smile, m::Face::Wink, m::Face::Blink,
                      m::Face::Sleeping, m::Face::Confused, m::Face::Neutral}) {
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
