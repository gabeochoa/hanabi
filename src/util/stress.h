#pragma once

// ---------------------------------------------------------------------------
// A launch that drives itself, so a profile is of the same work every time.
//
// WHY. The reported symptom was "open it, scroll the sidebar up and down until
// it breaks" — and nothing in this project could have caught that. `make test`
// renders 45 frames and asserts on the 45th. `measure_launch.sh` gates the
// FIRST frame and peak RSS of a process under a second old. Both are budgets
// on a young idle app, and a leak is a young idle app looking fine. What was
// missing is a measurement of the SLOPE, under the input a person actually
// gives it.
//
// Ported, deliberately, from Puffin's `Sources/App/StressDriver.swift`, which
// had already worked out the two decisions that make such a harness usable:
//
//   * **Every scenario terminates on a fixed COUNT, never a duration.** A
//     count is the same amount of work on a fast machine and a slow one, so
//     two runs are comparable; a wall-clock window measures how much a machine
//     got through, which is the question already being asked.
//   * **Settle before measuring.** Puffin's PERFORMANCE.md: "A first pass on a
//     fresh launch read 1.6-47.6% CPU and was the launch burst." Warm-up is
//     not the steady state, and averaging it in hides the thing being looked
//     for.
//
// It differs from Puffin's in one way, and the difference is the whole reason
// this exists: **it scrolls.** Puffin's driver deliberately does not, because
// reaching SwiftUI's ScrollViewProxy would mean an observed hook inside the
// transcript's body — the instrument would become part of the reading. hanabi
// is immediate-mode over an ECS: the wheel is an input value, so a scenario
// can drive it from outside the widget tree without changing the tree.
//
//   HANABI_STRESS=scroll   wheel the sidebar up and down
//   HANABI_STRESS=read     wheel the TRANSCRIPT up and down
//   HANABI_STRESS=threads  open every session in the catalog, in turn
//   HANABI_STRESS=tabs     open N tabs, then round-robin between them
//   HANABI_STRESS=idle     nothing; the control arm
//
//   HANABI_STRESS_FRAMES=<n>   total frames (default 3000, ~50s at 60fps)
//   HANABI_STRESS_SETTLE=<n>   frames to run before measuring (default 120)
//   HANABI_STRESS_TABS=<n>     tabs, for the tabs scenario (default 8)
//
// Everything here is behind that one variable and is a hard no-op when unset,
// the same contract as `test_hooks.h`.
// ---------------------------------------------------------------------------

#include <cstdlib>
#include <string>
#include <string_view>
#include <algorithm>
#include <vector>

#include "../ecs/components.h"

namespace hanabi::stress {

enum struct Scenario {
    None,
    Idle,
    Scroll,
    Read,
    Threads,
    Tabs,
};

inline Scenario scenario() {
    static const Scenario s = [] {
        const char* v = std::getenv("HANABI_STRESS");
        if (v == nullptr || *v == '\0') return Scenario::None;
        const std::string_view name{v};
        if (name == "idle") return Scenario::Idle;
        if (name == "scroll") return Scenario::Scroll;
        if (name == "read") return Scenario::Read;
        if (name == "threads") return Scenario::Threads;
        if (name == "tabs") return Scenario::Tabs;
        return Scenario::None;
    }();
    return s;
}

inline int env_int(const char* name, int fallback) {
    const char* v = std::getenv(name);
    if (v == nullptr || *v == '\0') return fallback;
    const int parsed = std::atoi(v);
    return parsed > 0 ? parsed : fallback;
}

inline int frames() { return env_int("HANABI_STRESS_FRAMES", 3000); }
inline int settle_frames() { return env_int("HANABI_STRESS_SETTLE", 120); }
inline int tab_count() { return env_int("HANABI_STRESS_TABS", 8); }

inline const char* name(Scenario s) {
    switch (s) {
        case Scenario::Idle: return "idle";
        case Scenario::Scroll: return "scroll";
        case Scenario::Read: return "read";
        case Scenario::Threads: return "threads";
        case Scenario::Tabs: return "tabs";
        case Scenario::None: return "none";
    }
    return "none";
}

// One frame's worth of driving. Called between `sm.run()` calls, so what it
// writes is read by the next frame exactly as a real input would be.
//
// `frame` counts from 0 at the START of the measured window, so a scenario's
// phase does not depend on how long the settle took.
struct Driver {
    Scenario mode = Scenario::None;
    int tabsOpened = 0;
    int threadCursor = 0;

    // The scroll, in pixels per frame. Sixty frames down, sixty up: a full
    // sweep of the list and back, which is what the report described.
    //
    // A triangle wave, not a sine: a constant speed makes each frame's work
    // the same, so a frame that is slower is slower for a reason other than
    // how far the view moved that frame.
    //
    // Written onto `HasScrollView::scroll_target` rather than injected as a
    // wheel event, for two reasons. The injector lives behind
    // AFTER_HOURS_ENABLE_E2E_TESTING and so is absent from the binary a person
    // actually runs -- which is the binary the report is about. And the target
    // is precisely what the wheel handler computes: writing it exercises the
    // same clamp, the same ease, the same layout and the same clip, one step
    // closer to the thing under test.
    [[nodiscard]] float scroll_step(int frame) const {
        if (mode != Scenario::Scroll && mode != Scenario::Read) return 0.0f;
        const int period = 120;
        const int phase = frame % period;
        // The transcript opens pinned to its BOTTOM, so it sweeps up first and
        // then back down; the sidebar opens at its top and sweeps the other
        // way. Same triangle, opposite phase, so each starts by moving into
        // content that exists rather than pushing against a clamp for half a
        // period and measuring nothing.
        // 12 px/frame for the sidebar, 40 for the transcript, and the
        // difference is not taste. The transcript re-arms its follow-latch
        // whenever the viewport is within 24 px of the content end
        // (`nearEnd`), and then pins itself back to the bottom. A 12 px step
        // never leaves that band, so the pane clawed the scroll back every
        // frame and a scrolling run produced counters byte-identical to an
        // idle one -- wrap_text 55584 in both, to the call. 40 clears the band
        // on the first frame, which is also what a real wheel notch does.
        const float mag = mode == Scenario::Read ? 40.0f : 12.0f;
        const float down = phase < period / 2 ? mag : -mag;
        return mode == Scenario::Read ? -down : down;
    }

    // Which scroll view the step drives. `scroll` has always meant the
    // sidebar, and the transcript -- the pane with the genuinely large
    // content, and the one the report was about ("scroll until it breaks") --
    // had no scenario at all, so its scrolled cost had never been measured.
    [[nodiscard]] const char* scroll_target_name() const {
        return mode == Scenario::Read ? "transcript_scroll" : "sidebar_scroll";
    }

    // Drive the app's own request flags, the same ones a click sets.
    void act(int frame, ecs::AppComponent& app) {
        switch (mode) {
            case Scenario::None:
            case Scenario::Idle:
            case Scenario::Scroll:
            case Scenario::Read:
                break;

            case Scenario::Threads: {
                // One open every 30 frames -- half a second, which is longer
                // than a transcript fetch from the mock and shorter than a
                // person's dwell. Opening every frame would measure the
                // fetches landing on top of each other rather than the steady
                // state of opening a thread.
                if (frame % 30 != 0) break;
                if (app.sessions.empty()) break;
                const size_t i =
                    static_cast<size_t>(threadCursor) % app.sessions.size();
                app.requestOpenTab = app.sessions[i].id;
                ++threadCursor;
                break;
            }

            case Scenario::Tabs: {
                // Open the first N, then round-robin between them. Ids come
                // from the CATALOG, never invented: a made-up id opens a tab
                // whose fetch fails, which measures an error path and an empty
                // pane instead of the layout of a real one. (Puffin's driver
                // makes the same point in its own comment.)
                if (app.sessions.empty()) break;
                const int want =
                    std::min<int>(tab_count(),
                                  static_cast<int>(app.sessions.size()));
                if (tabsOpened < want) {
                    if (frame % 20 != 0) break;
                    app.requestOpenTab =
                        app.sessions[static_cast<size_t>(tabsOpened)].id;
                    ++tabsOpened;
                    break;
                }
                // The switch goes through requestOpenTab too: opening a thread
                // that is already a tab FOCUSES it rather than duplicating it
                // (tab_model.h), so this is the same seam a click on the strip
                // uses and needs no second code path.
                if (frame % 15 != 0) break;
                const size_t k =
                    static_cast<size_t>(threadCursor) % static_cast<size_t>(want);
                app.requestOpenTab = app.sessions[k].id;
                ++threadCursor;
                break;
            }
        }
    }
};

}  // namespace hanabi::stress
