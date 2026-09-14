#pragma once
// The native context menu, as an ADAPTER over the app's existing menu model.
//
// On macOS with a real window, a context menu is an NSMenu -- AppKit's
// tracking, keyboard, anti-aliasing and appearance, not a reproduction of
// them in sokol. Everything else about the menu stays the app's: the items
// (`surface::MenuItem`: label, debug name, disabled, destructive, children,
// separator), the result (`surface::MenuResult`: which row / child, or
// dismissed), and the intents the callers write on a result. This adapter
// is the one place the two meet, and it has three rules:
//
//   1. NEVER inside a frame. `[NSMenu popUpMenuPositioningItem:...]` runs a
//      NESTED run loop until the menu closes; sokol's frame runs from
//      MTKView drawRect on the same thread. So a system only QUEUES a copied
//      request (`request`), and `pump_after_frame` -- called once per frame
//      after rendering, from main.cpp beside resize_drive::frame_end -- hands
//      it to the main run loop with dispatch_async, where the pop-up executes
//      on the NEXT run-loop turn, outside drawRect. Re-entry is refused: one
//      menu at a time. And while the menu tracks the app must keep
//      rendering: AppKit's tracking mode does NOT service MTKView's display
//      link (measured: the app froze until a watchdog), so the adapter runs
//      a timer in NSEventTrackingRunLoopMode that drives one frame per tick
//      through the same synchronous `[MTKView draw]` the live-resize path
//      uses; `frames_while_tracking()` is the count.
//   2. Callbacks only QUEUE. An NSMenuItem target fires during tracking; it
//      writes a typed result (`generation`, row, child) into a slot and
//      touches nothing else. The consuming system reads the slot next frame
//      with `take_result`, and only if the generation is the one it opened.
//      A close without a pick (Escape, click outside, another menu) arrives
//      through NSMenuDelegate menuDidClose: as `dismissed`.
//   3. Copy everything. Labels, children, flags are copied into the request
//      and into NSMenuItems; the adapter holds no pointer into a frame's
//      data. The request carries a `scope` string (the target session / view
//      / tab the caller opened it FOR) so a result can be matched to the
//      snapshot, and the caller decides an action by a stable
//      `MenuItem::action_id`, never by a row index into a vector it may have
//      rebuilt since.
//
// Availability: `available()` is true only on macOS with a visible window
// owned by this process (the same gate the native_* e2e commands use);
// headless runs, other platforms, and a hidden window keep the drawn menu.
// Nothing here is a proof of AppKit's behaviour -- that is verified only by
// a windowed script that opens the real menu and drives it with real input.
//
// Afterhours limitation (recorded, not hidden -- no vendor edit): the UI
// plugin has no notion of an external tracker owning the pointer. While the
// NSMenu tracks, the UI still sees last frame's hover/press. The callers
// keep the drawn menu's full-window transparent eater up for the menu's
// lifetime, so nothing underneath registers a press, and clear hot/active
// on close. See afterhours_gaps.md.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hanabi::native_menu {

// The adapter's own copy of a row: a plain value type, so this header pulls
// in no UI and the UI's context_menu.h can include it. The shim in
// context_menu.h converts surface::MenuItem into these (labels and flags
// copied; a child leaf is an Item with no children).
struct Item {
    std::string label;
    std::string debug_name;
    bool disabled = false;
    bool destructive = false;
    bool separator = false;
    std::vector<Item> children;
};
inline constexpr std::size_t kNoRow = static_cast<std::size_t>(-1);


struct Request {
    std::uint64_t generation = 0;
    std::string scope;   // what the menu is FOR: "session:<id>", "view:<id>", "tab:<n>"
    std::string debug_name;
    std::vector<Item> items;  // copied
    float content_x = 0.0f;  // anchor, content-space points, top-left origin
    float content_y = 0.0f;
};

struct Result {
    std::uint64_t generation = 0;
    std::size_t row = kNoRow;
    std::size_t child = kNoRow;
    bool dismissed = false;  // closed with no pick
};

// A caller's record of the native menu it opened: the generation to match
// results against, the scope (what the menu was FOR) and the SNAPSHOT of
// action ids, one per row as the menu was built. A result's row index is
// looked up here -- never in the caller's current item vector, which it may
// have rebuilt since -- and the caller then resolves that id against its
// current items, refusing an id that is no longer there (a toggle whose
// direction changed). Pure data; the callers keep one per menu.
struct Open {
    std::uint64_t generation = 0;
    std::string scope;                     // "session:<id>" / "view:<id>" / "tab:<n>"
    std::vector<std::string> action_ids;   // snapshot, by row
    // The UI element that had keyboard focus when the menu opened (the
    // UI's own id; -1 = none). AppKit returning key status to the view does
    // not touch the UI's focus_id, and a press on the eater can move it
    // there -- so the caller restores this ONCE when the menu closes, only
    // if focus is still on the eater or on nothing (a menu action that set
    // a new focus target is not overridden), and only if the element is
    // still built.
    long long focus_before = -1;
    long long eater_id = -1;               // the eater the shim drew, once known
    bool open() const { return generation != 0; }
    void clear() {
        generation = 0;
        scope.clear();
        action_ids.clear();
        focus_before = -1;
        eater_id = -1;
    }
    const std::string& action_of(std::size_t row) const {
        static const std::string kNone;
        return row < action_ids.size() ? action_ids[row] : kNone;
    }
};

// True when a native menu can be shown: macOS, a visible window this process
// owns. Never true headless.
bool available();

// Queue a menu. Returns its generation (> 0), or 0 when refused: not
// available, or a menu is already queued or tracking (no re-entry).
std::uint64_t request(Request req);

// BUSY: a generation is owned -- queued, dispatched-not-yet-shown, or on
// screen. What request() refuses on, what cancel() acts on.
bool busy();

// TRACKING: AppKit has actually opened the menu (NSMenuDelegate
// menuWillOpen: fired) and it has not closed yet. This is the only state
// that says a menu is on screen; a queued or dispatched request that never
// appears is busy but never tracking. Scripts' "open" and heartbeat
// brackets read this, never busy().
bool tracking();

// Generation of the menu currently queued/tracking, 0 when none.
std::uint64_t current_generation();

// Per frame, AFTER rendering, on the main thread: hands a queued request to
// the run loop. No-op when nothing is queued, when headless, or on other
// platforms. Never opens the menu synchronously.
void pump_after_frame();

// Take the result the last menu produced, if any. `generation` must match a
// menu the caller opened; a stale or foreign generation is not taken.
bool take_result(std::uint64_t generation, Result* out);

// Frames the adapter's heartbeat drove while the current (or, after it
// closed, the last) menu was tracking. AppKit's tracking run loop does not
// service the display link, so the adapter drives frames itself from a
// timer in that mode; this count is the runtime evidence that it did. Reset
// when a menu is shown.
std::uint64_t frames_while_tracking();

// True when a result is sitting in the slot untaken (for any generation).
// After a pick has been consumed there must be NO trailing dismissal here.
bool has_pending_result();

// Internal: AppKit's menuWillOpen: for the owned generation (called by the
// adapter's own delegate). Not for callers.
void mark_on_screen(std::uint64_t generation);

// Test seam: mark the owned generation as on screen, as menuWillOpen: would
// -- for a routing script that has no AppKit menu. Never used by the app.
void mark_tracking_for_test(std::uint64_t generation);

// Cancel a queued/tracking menu (the caller's target went away). Safe to call
// when nothing is open. A tracking NSMenu is cancelled via -cancelTracking;
// its close arrives as `dismissed`.
void cancel(std::uint64_t generation);

// The focus-return rule, as a pure function so it can be unit-tested apart
// from the UI: given the focus the menu opened with, the eater's id, the
// focus at close, and whether the remembered element still exists, what
// should focus be after the close? `-1` = leave focus where it is.
//   * focus moved somewhere real during the menu (a menu action asked for
//     it) -> leave it (-1)
//   * focus is on the eater, or on nothing (ROOT/-1) -> the remembered
//     element if it still exists, else -1
//   * nothing was remembered -> -1
inline long long focus_to_restore(long long focus_before, long long eater_id,
                                  long long focus_now, long long root_id,
                                  bool before_still_exists) {
    if (focus_before < 0 || focus_before == root_id || !before_still_exists) return -1;
    const bool onEater = eater_id >= 0 && focus_now == eater_id;
    const bool onNothing = focus_now < 0 || focus_now == root_id;
    if (!onEater && !onNothing) return -1;  // a menu action's own focus request wins
    return focus_before;
}

// Test seam: run the main queue's pending blocks once (the dispatched pop-up
// among them) without a window -- so a unit test can drive queued ->
// dispatched -> (cancel | show) through the real pump. Returns how many
// turns ran. Never used by the app.
int drain_main_queue_for_test();

// Test seam: deliver a typed result into the slot exactly as an NSMenuItem
// target would, for scripts proving the request -> result -> intent ROUTE.
// This is not evidence of AppKit behaviour and the scripts using it say so.
void inject_result_for_test(Result r);

}  // namespace hanabi::native_menu
