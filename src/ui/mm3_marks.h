#pragma once

// Which MM3 face a status wears, and whether it moves (the reference's
// IconTable MM3 column at drafts D123023215 + D123162830; puffin_gaps.md D51).
//
//   working          the library's blink, playing calmly (a play, then a rest)
//   asking (Waiting) the library's whole angry animation, looping end to end
//                    (a row waiting on you draws the eye; owner, kt-p22h);
//                    still, it is the wave
//   blocked          the angry loop too; still, it is tired
//   failed           the angry face, still -- whole (kt-gsa2), so a run that
//                    failed is told from one blocked on you by motion AND shape
//   review (Done)    the wink
//   idle             the resting smile (in the quiet ink)
//   frozen, paused   no face: the conventions, read without being decoded
//
// Pure (graphics-free) so the mapping is unit-tested.

#include <optional>

#include "../ecs/thread_model.h"
#include "mm3_anim.h"
#include "mm3_faces.h"

namespace hanabi::mm3 {

struct Mark {
    Face still = Face::Smile;
    std::optional<Animation> moves;
    bool urgent = false;  // loops with no rest, keeps moving behind another app
};

inline std::optional<Mark> mark_for(ecs::model::StatusGlyph g, bool failed) {
    using G = ecs::model::StatusGlyph;
    switch (g) {
        case G::Running: return Mark{Face::Blink, Animation::Blink, false};
        case G::Waiting: return Mark{Face::Wave, Animation::Angry, true};
        case G::Blocked:
            if (failed) return Mark{Face::Angry, std::nullopt, false};
            return Mark{Face::Tired, Animation::Angry, true};
        case G::Done: return Mark{Face::Wink, std::nullopt, false};
        case G::Idle: return Mark{Face::Smile, std::nullopt, false};
        case G::Frozen:
        case G::Paused: break;
    }
    return std::nullopt;
}

// Whether a moving face plays this frame. Reduce Motion stills every face; a
// calm one also stops while another app is in front; an urgent one stops only
// when the window cannot be seen (hidden, covered, the screens asleep).
inline bool plays(const Mark& m, bool reduceMotion, bool appActive, bool windowVisible) {
    if (!m.moves || reduceMotion || !windowVisible) return false;
    return m.urgent || appActive;
}

}  // namespace hanabi::mm3
