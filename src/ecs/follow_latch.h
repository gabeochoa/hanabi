#pragma once

// ---------------------------------------------------------------------------
// The transcript's follow-the-bottom latch, as arithmetic.
//
// WHAT THE LATCH IS FOR. A chat transcript pins itself to the newest message
// while you are reading the newest message, and lets go the moment you scroll
// back to read something older. Recomputing "am I at the bottom?" from geometry
// every frame does not work: when a token lands, the content grows while the
// offset stays put, so a pure geometry test flips FALSE the instant the thing
// it is supposed to follow arrives. The latch exists because content GROWTH and
// a user SCROLL-UP both widen (contentH - offset - viewH) and only one of them
// means "the reader left the bottom".
//
// WHY IT IS A SEPARATE HEADER. The latch used to be nine lines inline in
// main_pane_system.h, and those nine lines silently ate every mouse wheel event
// in the app (see below). The bug is a two-line ordering mistake between two
// conditions, and no screenshot and no text assertion can see it -- the pane
// looks exactly the same whether the wheel was applied and reverted or never
// arrived at all. It is a state machine, so it is tested as a state machine.
//
// THE BUG THIS HEADER WAS EXTRACTED TO FIX (2026-08-26, reported as "can you
// make sure that scrolling with the scroll wheel works in threads").
//
// The old body, in order:
//
//     nearEnd = (offset + viewH >= contentH - 24);
//     if (prevOffset >= 0 && offset < prevOffset - 2) follow = false;
//     if (nearEnd) follow = true;              // <-- undoes the line above
//
// afterhours' wheel handler (plugins/ui/systems.h HandleScrollInput) adds the
// notch to `scroll_target`, NOT to `scroll_offset`; `ease_scroll` then walks the
// offset toward the target by `scroll_smoothing` of the remaining distance per
// 60fps frame, and hanabi sets that to 0.28 (util/scroll_prefs.h). So the first
// frame after a notch the offset has moved 0.28 x 20 = 5.6 px, and 5.6 px is
// inside the 24 px `nearEnd` band. The latch broke on line two and re-armed on
// line three, the pane pinned itself back to the end -- writing BOTH the offset
// and the target, so the notch was erased, not merely outrun -- and the next
// frame started over from the bottom. The offset could never leave the band, so
// the wheel moved the transcript exactly zero pixels, forever, measured:
//
//     eight notches over the transcript, transcript_bottom_pad y=646 before,
//     y=646 after.
//
// It was not a dead event and not a missed hit-test: the same eight notches
// after one PAGE_UP moved the pad 646 -> 1209 -> 1689. The wheel was arriving
// and being reverted, which is the version of this bug that looks like nothing
// is wired up at all.
//
// THE FIX, IN ONE SENTENCE. Read the user's intent off `scroll_target`, which is
// the field the wheel writes, instead of off `scroll_offset`, which is the field
// the easing writes -- so a notch is legible on the frame it lands rather than
// 28% of it being legible one frame later.
//
// Three signals, and why each is the shape it is:
//
//   BREAK on a target that DECREASED since last frame. A decrease is
//   unambiguous: content growth only ever raises the end, and the pin only ever
//   writes the end, so nothing but the reader moves the target up. This fires on
//   the whole notch (20 px) the frame it arrives, not on the 5.6 px the easing
//   has delivered.
//
//   BREAK on an offset that decreased, kept from the old code, because the
//   minimap drag and the soak driver (util/soak.h scroll_named) write the offset
//   directly and are entitled to break the latch too.
//
//   RE-ARM when the view has arrived at the end AND nothing is still pulling it
//   away. "Still pulling away" is exactly `target < offset`: during an upward
//   glide the target sits above (numerically below) the offset the easing is
//   walking. That single extra clause is what stops the re-arm from undoing the
//   break on the frame the notch lands, which was the whole bug.
//
// And one guard: content that SHRINKS (switching threads, a find that filters,
// a message removed) drags the target down through `clamp_scroll`, which is not
// the reader scrolling up. A frame whose max-scroll fell is not allowed to break
// the latch.
//
// Pure and graphics-free, like thread_model.h and digest_layout.h: no UIContext,
// no afterhours draw, so a unit test drives it headlessly
// (tests/unit/test_follow_latch.cpp).
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cmath>

namespace ecs::model {

// Within this much of the content end counts as "at the bottom". Unchanged from
// the inline version: it is the band a newly-pinned view sits in once rounding
// and the trailing spacer have had their say.
inline constexpr float kFollowRearmPx = 24.0f;

// Movement below this is clamp wobble and sub-pixel jitter, not a gesture.
inline constexpr float kFollowJitterPx = 2.0f;

// What the latch remembers between frames. Lives in PaneState, one per thread,
// so split-view's two transcripts cannot clobber each other.
struct FollowMemory {
    bool follow = true;        // armed on a fresh open: threads open at the end
    float prevOffset = -1.0f;  // -1 = no previous frame yet
    float prevTarget = -1.0f;
    float prevMaxScroll = -1.0f;
};

// This frame's scroll state, read off HasScrollView.
struct FollowInput {
    float offset = 0.0f;   // scroll_offset.y  -- where the view IS
    float target = 0.0f;   // scroll_target.y  -- where the wheel asked it to be
    float viewH = 0.0f;
    float contentH = 0.0f;
};

struct FollowVerdict {
    bool follow = true;   // pin to the end this frame
    bool nearEnd = true;  // the view is sitting at the end, latch aside
};

// The scroll offset that shows the end of the content.
inline float follow_end(const FollowInput& in) {
    return std::max(0.0f, in.contentH - in.viewH);
}

// Advance the latch one frame. Mutates `mem` (the latch and its previous-frame
// record) and returns what the pane should do.
inline FollowVerdict step_follow_latch(FollowMemory& mem,
                                       const FollowInput& in) {
    const float end = follow_end(in);

    FollowVerdict v;
    v.nearEnd = (in.offset + in.viewH >= in.contentH - kFollowRearmPx);

    // Content that got shorter pulled the target down with it via clamp_scroll.
    // That is the layout moving, not the reader.
    const bool shrank = mem.prevMaxScroll >= 0.0f &&
                        end < mem.prevMaxScroll - kFollowJitterPx;

    if (!shrank) {
        // The wheel writes the target, so the target is where a notch is
        // legible on the frame it lands.
        if (mem.prevTarget >= 0.0f && in.target < mem.prevTarget - kFollowJitterPx)
            mem.follow = false;
        // The minimap drag and the soak driver write the offset directly.
        if (mem.prevOffset >= 0.0f && in.offset < mem.prevOffset - kFollowJitterPx)
            mem.follow = false;
    }

    // Arriving back at the end re-arms -- but only when nothing is still
    // gliding the view away from it.
    const bool pullingUp = in.target < in.offset - 1.0f;
    if (v.nearEnd && !pullingUp) mem.follow = true;

    mem.prevOffset = in.offset;
    mem.prevTarget = in.target;
    mem.prevMaxScroll = end;

    v.follow = mem.follow;
    return v;
}

// The pane pinned itself to the end this frame and wrote both fields. Record
// that as the new previous frame, so the pin is not read back as a scroll.
inline void note_follow_pinned(FollowMemory& mem, float offset, float target) {
    mem.prevOffset = offset;
    mem.prevTarget = target;
}

}  // namespace ecs::model
