#include <cstdio>
#include <set>

#include "../../src/ui/mm3_menubar.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace mb = hanabi::mm3::menubar;
namespace m = hanabi::mm3;

int main() {
    // The owner's map, and the connection outranks the rows.
    CHECK(mb::mood_of(0, 0, false) == mb::Mood::Idle);
    CHECK(mb::mood_of(0, 2, false) == mb::Mood::Working);
    CHECK(mb::mood_of(1, 2, false) == mb::Mood::NeedsYou);
    CHECK(mb::mood_of(3, 0, true) == mb::Mood::Offline);
    CHECK(mb::plan_for(mb::Mood::NeedsYou).animation == m::Animation::Wave && mb::plan_for(mb::Mood::NeedsYou).repeats());
    CHECK(!mb::plan_for(mb::Mood::Offline).repeats());
    CHECK(mb::plan_for(mb::Mood::Working).rest == m::Face::Neutral);

    // When it moves: the idle blink only in front; never hidden or under Reduce Motion.
    CHECK(mb::moves(mb::Mood::Working, true, false, false));
    CHECK(!mb::moves(mb::Mood::Idle, true, false, false));
    CHECK(mb::moves(mb::Mood::Idle, true, false, true));
    CHECK(!mb::moves(mb::Mood::NeedsYou, false, false, true));
    CHECK(!mb::moves(mb::Mood::NeedsYou, true, true, true));

    // The step walk covers the whole play, merging runs of one frame, and the
    // delays add up to the animation's length.
    for (m::Animation which : {m::Animation::SmileBlink, m::Animation::Blink, m::Animation::Wave,
                               m::Animation::Tired, m::Animation::Angry}) {
        const m::Anim a = m::anim(which);
        double total = 0;
        std::size_t step = 0, swaps = 0;
        while (true) {
            const mb::Step s = mb::step_at(a, step);
            if (s.done) break;
            CHECK(s.last >= step && s.frame < a.frameCount);
            total += s.seconds;
            ++swaps;
            step = s.last + 1;
        }
        CHECK(swaps <= a.steps);
        CHECK(std::abs(total - a.duration * (1.0 - a.keyTimes[0])) < 0.05 * a.steps);
    }
    const m::Anim angry = m::anim(m::Animation::Angry);
    CHECK(mb::step_at(angry, 39).frame == 36 && mb::step_at(angry, 40).done);

    // The pixel key: a face differs from a different face, equals itself, and
    // some library frames collapse at menu-bar size (the point of the dedupe).
    const auto nc = m::cells(m::Face::Neutral);
    const auto tc = m::cells(m::Face::Tired);
    const auto kn = mb::pixel_key(nc.data, nc.size, mb::frame_box(), 2.0);
    CHECK(kn == mb::pixel_key(nc.data, nc.size, mb::frame_box(), 2.0));
    CHECK(kn != mb::pixel_key(tc.data, tc.size, mb::frame_box(), 2.0));
    std::set<std::vector<std::uint64_t>> distinct;
    const m::Anim wave = m::anim(m::Animation::Wave);
    for (std::size_t f = 0; f < wave.frameCount; ++f) {
        const auto c = m::frame_cells(wave, f);
        distinct.insert(mb::pixel_key(c.data, c.size, mb::frame_box(), 1.0));
    }
    CHECK(distinct.size() < wave.frameCount);
    std::printf("wave: %zu frames, %zu distinct at 1x\n", wave.frameCount, distinct.size());
    CHECK(mb::image_width() == 19.0);
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    return 1;
}
