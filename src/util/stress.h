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
//   HANABI_STRESS=idle     nothing; the control arm
//   HANABI_STRESS=scroll   wheel the sidebar up and down
//   HANABI_STRESS=scrollall  the same, after asking for the WHOLE list
//   HANABI_STRESS=read     wheel the TRANSCRIPT up and down
//   HANABI_STRESS=threads  open every session in the catalog, in turn
//   HANABI_STRESS=tabs     open N PREVIEW tabs, then round-robin between them
//   HANABI_STRESS=search   type into the sidebar's search field
//   HANABI_STRESS=open     open every thread as a KEPT tab and never close one
//   HANABI_STRESS=resize   drag the window narrower and wider, forever
//   HANABI_STRESS=churn    open a thread, leave, close it, open the next
//   HANABI_STRESS=mixed    all of the above, interleaved, the way a person is
//
//   HANABI_STRESS_FRAMES=<n>   total frames (default 3000, ~50s at 60fps)
//   HANABI_STRESS_SETTLE=<n>   frames to run before measuring (default 120)
//   HANABI_STRESS_TABS=<n>     tabs, for the tabs scenario (default 8)
//   HANABI_STRESS_EVERY=<n>    frames between actions, where a scenario has
//                              a natural cadence (default per scenario)
//
// A NOTE ON `tabs`, AND WHY `open` HAD TO EXIST. `app.requestOpenTab` is the
// flag a sidebar row click raises, and TabFlowSystem consumes it with
// `keep=false` -- a PREVIEW, which reuses the one preview tab rather than
// making a new one. So the `tabs` arm, whose whole subject is "anything the
// tab strip or a per-tab cache holds on to", has never had more than one tab
// open: it re-pointed the same tab eight times. Measured, not inferred; see
// docs/perf/STRESS.md. `open` promotes each tab the way a second click does
// (tab_bar_system.h: "treat a click on the tab you are already reading as the
// second look that keeps it"), so the strip actually accumulates.
//
// Everything here is behind that one variable and is a hard no-op when unset,
// the same contract as `test_hooks.h`.
// ---------------------------------------------------------------------------

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <algorithm>
#include <vector>

#include "../ecs/components.h"
#include "../ecs/tab_model.h"
#include "../ecs/ui_imports.h"
#include "../../vendor/afterhours/src/plugins/window_manager.h"

namespace hanabi::stress {

enum struct Scenario {
    None,
    Idle,
    Scroll,
    ScrollAll,
    Read,
    Threads,
    Tabs,
    Search,
    Open,
    Resize,
    Churn,
    Mixed,
};

inline Scenario scenario() {
    static const Scenario s = [] {
        const char* v = std::getenv("HANABI_STRESS");
        if (v == nullptr || *v == '\0') return Scenario::None;
        const std::string_view name{v};
        if (name == "idle") return Scenario::Idle;
        if (name == "scroll") return Scenario::Scroll;
        if (name == "scrollall") return Scenario::ScrollAll;
        if (name == "read") return Scenario::Read;
        if (name == "threads") return Scenario::Threads;
        if (name == "tabs") return Scenario::Tabs;
        if (name == "search") return Scenario::Search;
        if (name == "open") return Scenario::Open;
        if (name == "resize") return Scenario::Resize;
        if (name == "churn") return Scenario::Churn;
        if (name == "mixed") return Scenario::Mixed;
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
inline int every(int fallback) { return env_int("HANABI_STRESS_EVERY", fallback); }

inline const char* name(Scenario s) {
    switch (s) {
        case Scenario::Idle: return "idle";
        case Scenario::Scroll: return "scroll";
        case Scenario::ScrollAll: return "scrollall";
        case Scenario::Read: return "read";
        case Scenario::Threads: return "threads";
        case Scenario::Tabs: return "tabs";
        case Scenario::Search: return "search";
        case Scenario::Open: return "open";
        case Scenario::Resize: return "resize";
        case Scenario::Churn: return "churn";
        case Scenario::Mixed: return "mixed";
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
    // `open`: how many threads have been promoted to a KEPT tab. This is the
    // number the failure report is in terms of -- "it broke at 43 tabs" is a
    // fact somebody can act on; "it broke at frame 1720" is not.
    int keptTabs = 0;
    // `churn`: tabs opened and closed again, the counterpart quantity.
    int churnCycles = 0;
    // `resize`: the size the app started at, captured on the first action so
    // the sweep is relative to whatever window the run was given.
    int baseW = 0;
    int baseH = 0;
    int resizes = 0;
    std::string moreKeyScratch;

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
        if (mode != Scenario::Scroll && mode != Scenario::Read &&
            mode != Scenario::ScrollAll)
            return 0.0f;
        // `scrollall` sweeps a list that is the whole catalog rather than two
        // viewports, so it gets a longer period and a faster step -- 96 px is
        // three rows a frame, a flick rather than a nudge. The product,
        // 96 x 600 = 57,600 px, is 1800 rows: at the 2000-session catalog one
        // half-period walks nearly the entire list, so a row that is only
        // expensive the first time it is reached is reached.
        if (mode == Scenario::ScrollAll) {
            const int period = 1200;
            const int phase = frame % period;
            return phase < period / 2 ? 96.0f : -96.0f;
        }
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

            case Scenario::ScrollAll: {
                // Ask for the whole list, once, on the first measured frame --
                // the same write the "Show N more..." row at the bottom of the
                // sidebar performs when it is clicked, through the same
                // sentinel (ecs::more_key), so this is the user's own gesture
                // and not a back door.
                //
                // WHY THIS SCENARIO EXISTS. `scroll` scrolls a list the
                // sidebar has already capped at two viewports, so scrolling it
                // moves a clip rectangle over rows that were going to be built
                // anyway: measured at a 2000-session catalog over 2000 frames,
                // `scroll` and `idle` allocate 7,422,153 and 7,422,071 times
                // -- a difference of 82 allocations in four hundred million
                // bytes, which is to say the arm was a second idle arm. The
                // list a person scrolls is the one they asked to see all of,
                // and that is the list this drives.
                if (frame != 0) break;
                app.collapsedFolders.insert(
                    ecs::more_key("recent", moreKeyScratch));
                break;
            }

            case Scenario::Search: {
                // Type a query in, hold it, clear it, repeat.
                //
                // The hold is the point. A filter that re-derives itself every
                // frame costs the same whether the query changed or not, and
                // the frames where nothing is being typed are most of the
                // frames a person spends looking at their own search results.
                // A scenario that only ever typed would measure the keystroke
                // and miss the pause, which is the expensive part.
                //
                // Written straight onto app.searchQuery, which is exactly what
                // the text field writes (sidebar_system.h binds it), so this
                // is the same value flowing through the same filter.
                //
                // "re" is deliberately a substring of many synthetic titles
                // and of none of the hand-written twenty's leading words --
                // a query that matches nothing exits the filter early on
                // every row and measures the cheap path.
                static const char* kTyped[] = {"r", "re", "rec", "reco"};
                const int period = 240;
                const int phase = frame % period;
                if (phase < 4)
                    app.searchQuery = kTyped[phase];
                else if (phase == period - 20)
                    app.searchQuery.clear();
                break;
            }

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

            case Scenario::Open: {
                // OPEN EVERY THREAD UNTIL IT BREAKS -- the scenario the brief
                // asked for by name, and the one `tabs` was mistaken for.
                //
                // Two frames per thread, because a tab is KEPT by the second
                // look: `requestOpenTab` always opens a PREVIEW (TabFlowSystem
                // passes keep=false), and a preview reuses the one preview
                // slot. Raising the same id again finds the tab already in
                // tabOrder and promotes it -- which is exactly what
                // tab_bar_system.h does on "a click on the tab you are already
                // reading". So the strip really does grow by one, through the
                // same code path a person's second click takes.
                //
                // The cadence is deliberately faster than `threads`' 30
                // frames: this arm is about the strip's arithmetic and the
                // accumulated per-tab state, not about a transcript fetch
                // landing, and 20 frames is still a third of a second.
                if (app.sessions.empty()) break;
                const int cadence = every(20);
                const int phase = frame % cadence;
                if (phase != 0 && phase != 1) break;
                const size_t i =
                    static_cast<size_t>(threadCursor) % app.sessions.size();
                app.requestOpenTab = app.sessions[i].id;
                if (phase == 1) {
                    ++threadCursor;
                    ++keptTabs;
                }
                break;
            }

            case Scenario::Resize: {
                // A WINDOW DRAG, one step a frame, which is what a drag is.
                //
                // Nothing in this project has ever resized while watching
                // memory -- GATES.md says so in as many words ("no gate here
                // presses a key, opens a menu, or resizes a window"). The
                // reason to care is one layer down: the headless backend
                // honours a resize by destroying and recreating the offscreen
                // render target (sokol/backend.h), and its own comment says
                // that is fine because resizes are rare. A drag makes them
                // sixty a second, and a render target is GPU memory, which is
                // the one thing the malloc counters cannot see. RSS can.
                //
                // A triangle wave rather than random sizes: constant work per
                // frame, so a frame that is slower is slower for a reason
                // other than how far the edge moved.
                if (baseW == 0) {
                    baseW = afterhours::graphics::get_screen_width();
                    baseH = afterhours::graphics::get_screen_height();
                    if (baseW <= 0 || baseH <= 0) break;
                }
                const int period = 120;
                const int phase = frame % period;
                const int half = period / 2;
                // Narrow to 60% and back. 60% is past the width at which the
                // sidebar and the transcript both re-wrap rather than merely
                // re-position, which is the expensive half of a resize.
                const float travel =
                    phase < half ? static_cast<float>(phase) / half
                                 : static_cast<float>(period - phase) / half;
                const int w = baseW - static_cast<int>(0.4f * travel *
                                                       static_cast<float>(baseW));
                const int h = baseH;
                set_window_size_now(w, h);
                ++resizes;
                break;
            }

            case Scenario::Churn: {
                // RAPID NAVIGATION CHURN: open a thread, walk away to a smart
                // view, close the tab, open the next. The shape of somebody
                // triaging an inbox, and the shape that exercises every
                // per-session map in the app -- open populates them, close is
                // supposed to let them go.
                //
                // docs/perf/MEMORY.md entry 1 is five maps keyed by session id
                // that nothing ever erased, found by exactly this motion, done
                // by hand. Nothing automated has driven it since.
                if (app.sessions.empty()) break;
                const int cadence = every(12);
                const int phase = frame % cadence;
                if (phase == 0) {
                    const size_t i =
                        static_cast<size_t>(threadCursor) % app.sessions.size();
                    app.requestOpenTab = app.sessions[i].id;
                    ++threadCursor;
                } else if (phase == cadence / 3) {
                    // Leave the thread for a list view. The view cycles so the
                    // filter, the grouping and the empty state all get a turn.
                    static const ecs::SmartView kViews[] = {
                        ecs::SmartView::Home, ecs::SmartView::Blocked,
                        ecs::SmartView::Review, ecs::SmartView::Starred,
                        ecs::SmartView::Archived};
                    app.view = kViews[static_cast<size_t>(churnCycles) % 5];
                } else if (phase == (2 * cadence) / 3) {
                    // Count the close, not the attempt. `churn_cycles=100` on
                    // a run that closed nothing is the report lying, which is
                    // the whole failure mode work_done() exists to prevent.
                    if (close_newest_tab(app)) ++churnCycles;
                }
                break;
            }

            case Scenario::Mixed: {
                // EVERYTHING AT ONCE, which is the only arm that resembles
                // use. Every other scenario holds the app still on every axis
                // but one, and a leak that needs two things to be true at the
                // same time -- a cache keyed on (thread, width), a per-tab
                // subscription reaped only on the frame the tab is visible --
                // is invisible to all of them.
                //
                // Its weakness is the mirror of that strength: when it goes
                // red it does not say which axis did it, and the single-axis
                // arms are how you find out. It is a detector, not a
                // diagnosis, and the failure text says so.
                Driver sub;
                sub.mode = Scenario::Threads;
                sub.threadCursor = threadCursor;
                sub.act(frame, app);
                threadCursor = sub.threadCursor;

                if (frame % 90 < 8) {
                    static const char* kTyped[] = {"r",  "re", "rec", "reco",
                                                   "rec", "re", "r",  ""};
                    app.searchQuery = kTyped[frame % 90];
                }
                if (frame % 150 == 0) {
                    if (baseW == 0) {
                        baseW = afterhours::graphics::get_screen_width();
                        baseH = afterhours::graphics::get_screen_height();
                    }
                    if (baseW > 0) {
                        const bool narrow = (frame / 150) % 2 == 0;
                        set_window_size_now(
                            narrow ? baseW - baseW / 4 : baseW, baseH);
                        ++resizes;
                    }
                }
                if (frame % 200 == 100 && close_newest_tab(app)) ++churnCycles;
                break;
            }
        }
    }

    // A resize, applied the way afterhours' own e2e `resize` command applies
    // one: the ECS resolution singleton is the authoritative source for
    // layout, and the backend call is what re-sizes the render target. Doing
    // only the second leaves every widget laid out at the old size, so the
    // scenario would resize a texture and measure nothing.
    // Whether a resize also re-sizes the RENDER TARGET, or only the layout.
    //
    // OFF BY DEFAULT, AND THE REASON IS AN UPSTREAM LEAK — afterhours_gaps.h
    // #200. The headless backend honours a resize by destroying and
    // recreating the offscreen render texture, and
    // `load_render_texture` -> `sgl_make_context` creates five Metal render
    // pipelines that `sgl_destroy_context` does not release. Named with
    // MallocStackLogging + malloc_history over ~8100 resizes:
    //
    //   40511 live calls, 12963520 bytes  _sg_init_pipeline -> MTLVertexDescriptor
    //   40511 live calls, 12963520 bytes  _sg_init_pipeline -> MTLVertexBufferLayout
    //  162040 live calls,  7777920 bytes  _sg_init_pipeline -> MTLVertexAttribute
    //    8103 live calls                  _sg_init_image  (= one per resize)
    //
    // 4.8 MB of RSS per 1000 frames of resizing, rising 1.00 -- larger than
    // the Metal autorelease leak that started this whole project. It is not
    // hanabi's and hanabi cannot reach it: vendor/afterhours is read-only and
    // there is no seam between window_manager::set_window_size and the
    // backend. It is also NOT what a user hits, because the WINDOWED path
    // resizes an NSWindow (metal_set_window_size) and never touches the
    // offscreen target -- this is a headless-harness leak.
    //
    // So the default arm resizes the LAYOUT only: ProvidesCurrentResolution is
    // what every widget is laid out against and what viewport::width() feeds,
    // so the wrap, the clip and every width-keyed cache in hanabi all see the
    // new size. That is hanabi's own resize cost, it is gateable, and it is
    // the half hanabi can fix. Set HANABI_STRESS_RESIZE_BACKEND=1 to include
    // the render target and reproduce #200 in one run.
    static bool resize_backend() {
        static const bool on = [] {
            const char* v = std::getenv("HANABI_STRESS_RESIZE_BACKEND");
            return v != nullptr && *v != '\0' && std::string(v) != "0";
        }();
        return on;
    }

    static void set_window_size_now(int w, int h) {
        if (w <= 0 || h <= 0) return;
        if (auto* pcr = afterhours::EntityHelper::get_singleton_cmp<
                afterhours::window_manager::ProvidesCurrentResolution>()) {
            pcr->current_resolution.width = w;
            pcr->current_resolution.height = h;
            // Otherwise CollectCurrentResolution puts the old size straight
            // back on the next frame and the sweep is a no-op.
            pcr->should_refetch = false;
        }
        if (resize_backend()) afterhours::window_manager::set_window_size(w, h);
    }

    // Close the most recently opened tab, which is what Cmd-W does to the one
    // you are looking at. Reaches into ecs::model directly because there is no
    // request flag for a close -- `requestOpenTab` has no counterpart, so an
    // out-of-tree driver has no seam. Filed as an annoyance in
    // docs/perf/STRESS.md rather than papered over here.
    static bool close_newest_tab(ecs::AppComponent& app) {
        // find_singleton, not EntityHelper::get_singleton_cmp: the tab strip
        // is a component on an ordinary entity, not a registered singleton,
        // and the registered-singleton lookup returns null for it -- silently,
        // which is exactly how the first version of this driver reported
        // "churn_cycles=100" for a run that closed no tabs at all.
        auto* strip = ecs::find_singleton<ecs::TabStripComponent>();
        if (strip == nullptr || strip->tabOrder.empty()) return false;
        const size_t index = strip->tabOrder.size() - 1;
        const afterhours::EntityID tabId = strip->tabOrder[index];
        auto opt = afterhours::EntityHelper::getEntityForID(tabId);
        const bool wasActive = opt.valid() && opt->has<ecs::ActiveTab>();
        ecs::model::close_tab(*strip, app, tabId, index, wasActive);
        return true;
    }

    // How many tabs are actually open right now. Not a count the driver kept:
    // the strip's own, which is the only number that can contradict the
    // driver's belief about what it did.
    static int live_tab_count() {
        auto* strip = ecs::find_singleton<ecs::TabStripComponent>();
        return strip == nullptr ? -1 : static_cast<int>(strip->tabOrder.size());
    }

    // WHAT THIS RUN DID. Ported from Puffin's StressDriver.reportLines, whose
    // comment is the reason it exists: a stress run that measured nothing
    // "produced them SILENTLY: the same defect as kt-4ond, where a probe with
    // no call site answered 'clean' to a question it had not measured. A run
    // that measured nothing has to say so louder than a run that measured
    // something."
    //
    // Every scenario here can silently do nothing -- an empty catalog, a
    // scroll view that is not on screen, a strip that never grew -- and a soak
    // arm that drove nothing reads as the flattest run anybody ever took.
    // Counts are the only thing that can tell those two apart, so they are
    // printed on every run, pass or fail.
    [[nodiscard]] std::string work_done() const {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "threads_opened=%d kept_tabs=%d churn_cycles=%d "
                      "resizes=%d tabs_now=%d",
                      threadCursor, keptTabs, churnCycles, resizes,
                      live_tab_count());
        return std::string(buf);
    }

    // A scenario that names work it did not do. Returns an empty string when
    // the run looks honest.
    [[nodiscard]] std::string did_nothing_reason() const {
        switch (mode) {
            case Scenario::Open:
                if (keptTabs == 0) return "no thread was promoted to a tab";
                if (live_tab_count() <= 1)
                    return "the strip never grew past one tab, so this "
                           "measured one tab being re-pointed";
                return "";
            case Scenario::Churn:
                return churnCycles == 0 ? "no tab was ever closed" : "";
            case Scenario::Resize:
                return resizes == 0 ? "the window was never resized" : "";
            case Scenario::Threads:
            case Scenario::Tabs:
                return threadCursor == 0 ? "no thread was ever opened" : "";
            case Scenario::Mixed:
                return (threadCursor == 0 && resizes == 0)
                           ? "neither threads nor resizes happened"
                           : "";
            default:
                return "";
        }
    }
};

}  // namespace hanabi::stress
