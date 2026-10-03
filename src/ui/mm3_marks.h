#pragma once

// Which MM3 face a status wears, and whether it moves (the reference's
// IconTable MM3 column as landed: D123023215, D123162830, D123167028,
// D123182666; puffin_gaps.md D51).
//
//   working          the library's blink, playing calmly (a play, then a rest);
//                    a live run with no news for 3 minutes is THINKING instead
//                    (eyes up, three dots appearing; a cycle, no rest)
//   asking (Waiting) the library's whole angry animation, looping end to end
//                    (a row waiting on you draws the eye; owner, kt-p22h);
//                    still, it is the wave
//   blocked          the angry loop too; still, it is tired
//   failed           the angry face, still -- whole (kt-gsa2), so a run that
//                    failed is told from one blocked on you by motion AND shape
//   review (Done)    the library's wink, at the calm cadence: something is
//                    ready for you
//   idle             the resting smile (in the quiet ink); untouched for a
//                    day, the tired face, still
//   paused           asleep, a z rising (a cycle)
//   frozen           confused, the eyes swapping sizes (calm)
//
// And one moment: a row whose run really FINISHES (it was live, its run is now
// closed, and it settled asking nothing -- idle or ready for review) waves
// once, the library's one-handed wave, then wears its settled face. Asking,
// blocked and failed are not completions and never wave.
//
// Thinking, sleeping and confused are GENERATED faces in the library's grid
// (the reference says so: not from the library's sheet).
//
// Pure (graphics-free) so the mapping is unit-tested.

#include <cstdint>
#include <optional>

#include "../ecs/thread_model.h"
#include "mm3_anim.h"
#include "mm3_faces.h"

namespace hanabi::mm3 {

struct Mark {
    Face still = Face::Smile;
    std::optional<Animation> moves;
    bool urgent = false;  // loops with no rest, keeps moving behind another app
    bool once = false;    // plays a single time (the finish wave); onceT = seconds in
    double onceT = 0.0;
};

// The moment-only faces a ROW can wear (the sidebar decides; tabs do not).
// Eight bytes on purpose: every row's draw callback captures one, and a
// capture past std::function's small buffer is a heap allocation per row per
// frame (the allocation gate's home arm saw +75 a frame at 32 bytes).
struct Moment {
    float finishSince = 0.0f;  // seconds into a finish wave
    bool finishing = false;    // a finish wave is playing
    bool thinking = false;     // a live run quiet for kThinkingAfter
    bool stale = false;        // an idle row untouched for kStaleAfter
};
static_assert(sizeof(Moment) <= 8, "a row's draw callback captures this: keep it inside the small buffer");

inline constexpr double kThinkingAfter = 3 * 60;
inline constexpr double kStaleAfter = 24 * 60 * 60;

// Whether a row now in `g` has really finished: its run is closed and it
// settled asking nothing.
inline bool is_completion(ecs::model::StatusGlyph g, bool runOpen) {
    using G = ecs::model::StatusGlyph;
    return !runOpen && (g == G::Idle || g == G::Done);
}

// What the finish wave watches: the row's status AND whether its run is open
// (status alone misses a run that closes under the same face).
struct RunPhase {
    ecs::model::StatusGlyph glyph = ecs::model::StatusGlyph::Idle;
    bool runOpen = false;
    bool operator==(const RunPhase&) const = default;
};
inline bool finishes(const RunPhase& from, const RunPhase& to) {
    return (from.glyph == ecs::model::StatusGlyph::Running || from.runOpen) &&
           is_completion(to.glyph, to.runOpen);
}

// A live run's face once nothing has arrived for kThinkingAfter. Needs the
// row's OWN run open: the last event is the row's own clock.
inline bool thinking_now(ecs::model::StatusGlyph g, bool ownRunOpen, std::int64_t lastEventMs,
                         std::int64_t nowMs) {
    return g == ecs::model::StatusGlyph::Running && ownRunOpen && lastEventMs > 0 &&
           static_cast<double>(nowMs - lastEventMs) >= kThinkingAfter * 1000.0;
}
// An idle row's face once nothing has happened for kStaleAfter.
inline bool stale_now(ecs::model::StatusGlyph g, std::int64_t lastEventMs, std::int64_t nowMs) {
    return g == ecs::model::StatusGlyph::Idle && lastEventMs > 0 &&
           static_cast<double>(nowMs - lastEventMs) >= kStaleAfter * 1000.0;
}

inline std::optional<Mark> mark_for(ecs::model::StatusGlyph g, bool failed, const Moment& mo = {}) {
    using G = ecs::model::StatusGlyph;
    std::optional<Mark> base;
    switch (g) {
        case G::Running:
            base = mo.thinking ? Mark{Face::Neutral, Animation::Thinking} : Mark{Face::Blink, Animation::Blink};
            break;
        case G::Waiting: base = Mark{Face::Wave, Animation::Angry, true}; break;
        case G::Blocked:
            base = failed ? Mark{Face::Angry, std::nullopt} : Mark{Face::Tired, Animation::Angry, true};
            break;
        case G::Done: base = Mark{Face::Wink, Animation::Wink}; break;
        case G::Idle: base = mo.stale ? Mark{Face::Tired, std::nullopt} : Mark{Face::Smile, std::nullopt}; break;
        case G::Paused: base = Mark{Face::Sleeping, Animation::Sleeping}; break;
        case G::Frozen: base = Mark{Face::Confused, Animation::Confused}; break;
    }
    // The finish wave plays over the settled face, then gives way to it.
    if (base && mo.finishing && is_completion(g, false) &&
        mo.finishSince < anim(Animation::Wave).duration) {
        Mark wave = *base;
        wave.moves = Animation::Wave;
        wave.urgent = false;
        wave.once = true;
        wave.onceT = mo.finishSince;
        return wave;
    }
    return base;
}

// Whether a moving face plays this frame. Reduce Motion stills every face; a
// calm one (and a cycle) also stops while another app is in front; an urgent
// one, and a finish wave, stop only when the window cannot be seen.
inline bool plays(const Mark& m, bool reduceMotion, bool appActive, bool windowVisible) {
    if (!m.moves || reduceMotion || !windowVisible) return false;
    return m.urgent || m.once || appActive;
}

// The VIEWS list (chrome): Home's smile and Pinned's heart blink, calmly
// (D123182666). Nothing else on the shelf moves.
inline std::optional<Animation> shelf_motion(Face f) {
    if (f == Face::Smile) return Animation::SmileBlink;
    if (f == Face::Heart) return Animation::HeartBlink;
    return std::nullopt;
}

inline const char* name_of(Animation a) {
    switch (a) {
        case Animation::Blink: return "blink";
        case Animation::Angry: return "angry";
        case Animation::SmileBlink: return "smile_blink";
        case Animation::Wave: return "wave";
        case Animation::Tired: return "tired";
        case Animation::Wink: return "wink";
        case Animation::HeartBlink: return "heart_blink";
        case Animation::Thinking: return "thinking";
        case Animation::Sleeping: return "sleeping";
        case Animation::Confused: return "confused";
    }
    return "?";
}
inline const char* name_of(Face f) {
    switch (f) {
        case Face::Neutral: return "neutral";
        case Face::Blink: return "blink";
        case Face::Wink: return "wink";
        case Face::Wave: return "wave";
        case Face::Tired: return "tired";
        case Face::Angry: return "angry";
        case Face::Smile: return "smile";
        case Face::Heart: return "heart";
        case Face::Sleeping: return "sleeping";
        case Face::Confused: return "confused";
    }
    return "?";
}

}  // namespace hanabi::mm3
