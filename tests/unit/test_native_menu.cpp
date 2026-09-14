// The native context-menu adapter's ROUTING, without AppKit ever opening a
// menu: request -> generation -> result slot -> take, with the stale, foreign,
// cancelled and re-entrant cases. This is evidence about the app's plumbing
// only. What AppKit does when the menu is on screen -- tracking, keyboard,
// appearance, anti-aliasing -- is proven by the windowed scripts and by
// nothing here.
//
// Runs with no window: `available()` is false and the shim's
// `native_menu_open` refuses, which is itself the first assertion (the drawn
// menu stays the menu headless). The routing is then driven through
// `native_menu::request` directly and results are delivered through
// `inject_result_for_test`, the same slot an NSMenuItem target writes.
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/native_menu.h"

static int failures = 0;
#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

namespace nm = hanabi::native_menu;

// The adapter's heartbeat drives frames through sokol_impl.mm's
// metal_drive_one_frame; this test links the adapter alone and never shows a
// menu, so the hook is a stub that records it was asked (it must never be,
// here: no menu is ever on screen).
static int g_frames_asked = 0;
extern "C" int metal_drive_one_frame(void) {
    ++g_frames_asked;
    return 0;
}
static int g_releases_asked = 0;
extern "C" void metal_release_stuck_mouse_buttons(void) { ++g_releases_asked; }

static nm::Request req(const char* scope, std::size_t rows) {
    nm::Request r;
    r.scope = scope;
    r.debug_name = "t";
    for (std::size_t i = 0; i < rows; ++i)
        r.items.push_back({"row " + std::to_string(i), "t_" + std::to_string(i), false,
                           false, false, {}});
    r.content_x = 10;
    r.content_y = 20;
    return r;
}

static void test_headless_is_unavailable() {
    CHECK(!nm::available());
    CHECK(!nm::busy() && !nm::tracking());
    CHECK(nm::current_generation() == 0);
}

static void test_request_result_take_once() {
    const auto g = nm::request(req("session:t2", 3));
    CHECK(g != 0);
    CHECK(nm::busy());
    CHECK(!nm::tracking());  // owned, not on screen: nothing has opened
    CHECK(nm::current_generation() == g);
    // Nothing yet.
    nm::Result out;
    CHECK(!nm::take_result(g, &out));
    // The pick lands (as an NSMenuItem target would deliver it).
    nm::Result r;
    r.generation = g;
    r.row = 1;
    nm::inject_result_for_test(r);
    CHECK(!nm::busy() && !nm::tracking());
    CHECK(nm::take_result(g, &out));
    CHECK(out.row == 1 && out.child == nm::kNoRow && !out.dismissed);
    // Once.
    CHECK(!nm::take_result(g, &out));
}

static void test_no_reentry_while_open() {
    const auto g1 = nm::request(req("session:a", 2));
    CHECK(g1 != 0);
    const auto g2 = nm::request(req("session:b", 2));
    CHECK(g2 == 0);  // refused: one menu at a time
    CHECK(nm::current_generation() == g1);
    nm::cancel(g1);
    nm::Result out;
    CHECK(nm::take_result(g1, &out) && out.dismissed);
    CHECK(!nm::busy());
}

static void test_stale_and_foreign_results_are_dropped() {
    const auto g = nm::request(req("view:v-1", 2));
    CHECK(g != 0);
    // A result for a generation that is not the open one: dropped, the menu
    // stays open and nothing is takeable under either generation.
    nm::Result stale;
    stale.generation = g + 100;
    stale.row = 0;
    nm::inject_result_for_test(stale);
    CHECK(nm::busy());
    nm::Result out;
    CHECK(!nm::take_result(g, &out));
    CHECK(!nm::take_result(g + 100, &out));
    // The real one.
    nm::Result r;
    r.generation = g;
    r.dismissed = true;
    nm::inject_result_for_test(r);
    CHECK(nm::take_result(g, &out) && out.dismissed);
    // Taking with the wrong generation never yields the slot.
    const auto g2 = nm::request(req("tab:7", 1));
    nm::Result r2;
    r2.generation = g2;
    r2.row = 0;
    nm::inject_result_for_test(r2);
    CHECK(!nm::take_result(g, &out));   // old generation: no
    CHECK(nm::take_result(g2, &out));   // its own: yes
}

static void test_cancel_of_a_queued_menu_is_a_dismissal() {
    const auto g = nm::request(req("session:z", 1));
    CHECK(g != 0);
    nm::cancel(g);
    nm::Result out;
    CHECK(nm::take_result(g, &out));
    CHECK(out.dismissed && out.row == nm::kNoRow);
    CHECK(!nm::busy());
    // Cancel of nothing is safe.
    nm::cancel(g);
    nm::cancel(0);
}

static void test_pump_is_a_noop_with_nothing_queued() {
    CHECK(!nm::busy());
    nm::pump_after_frame();
    nm::pump_after_frame();
    CHECK(!nm::busy());
}

static void test_submenu_child_rides_the_result() {
    const auto g = nm::request(req("session:s", 1));
    nm::Result r;
    r.generation = g;
    r.row = 0;
    r.child = 1;
    nm::inject_result_for_test(r);
    nm::Result out;
    CHECK(nm::take_result(g, &out) && out.row == 0 && out.child == 1);
}

// The async boundary: after the pump has handed the request to the run loop
// but before the pop-up runs, the generation must still be owned.
static void test_dispatched_but_not_shown_is_still_owned() {
    const auto g = nm::request(req("session:d", 2));
    CHECK(g != 0);
    nm::pump_after_frame();  // queued -> dispatched (block pending on the main queue)
    CHECK(nm::busy());
    CHECK(!nm::tracking());  // dispatched is NOT on screen: no menuWillOpen: yet
    CHECK(nm::current_generation() == g);
    // A second request in this window is refused.
    CHECK(nm::request(req("session:e", 1)) == 0);
    // Cancel while dispatched: withdrawn, answered as a dismissal.
    nm::cancel(g);
    nm::Result out;
    CHECK(nm::take_result(g, &out) && out.dismissed);
    CHECK(!nm::busy());
    // The block now runs: pop_up must find the generation withdrawn and show
    // nothing -- there is no window here, and had it "shown" it would have
    // written a SECOND dismissal for g (the no-window path).
    nm::drain_main_queue_for_test();
    CHECK(!nm::take_result(g, &out));  // no second result
    CHECK(!nm::busy() && !nm::tracking());
    // And the slot is free for a new menu (scope replacement's re-request).
    const auto g2 = nm::request(req("session:e", 1));
    CHECK(g2 != 0 && g2 != g);
    nm::cancel(g2);
    CHECK(nm::take_result(g2, &out) && out.dismissed);
}

static void test_dispatched_then_shown_without_a_window_dismisses_once() {
    const auto g = nm::request(req("session:w", 1));
    nm::pump_after_frame();
    CHECK(nm::busy() && !nm::tracking());
    nm::drain_main_queue_for_test();  // pop_up runs: no window -> dismissal for g
    nm::Result out;
    CHECK(nm::take_result(g, &out) && out.dismissed);
    CHECK(!nm::busy() && !nm::tracking());
    CHECK(!nm::take_result(g, &out));
}

static void test_pump_moves_exactly_one_request() {
    const auto g = nm::request(req("session:p", 1));
    nm::pump_after_frame();
    nm::pump_after_frame();  // nothing queued now: no-op, still owned by dispatch
    CHECK(nm::current_generation() == g);
    nm::cancel(g);
    nm::drain_main_queue_for_test();
    nm::Result out;
    CHECK(nm::take_result(g, &out) && out.dismissed);
}

// tracking() is AppKit's word alone: only the menu-open event makes it true,
// and only for the generation on screen; a close clears it.
static void test_tracking_is_set_by_the_open_event_only() {
    const auto g = nm::request(req("session:o", 1));
    CHECK(nm::busy() && !nm::tracking());
    nm::mark_tracking_for_test(g + 5);  // a stale delegate call: ignored
    CHECK(!nm::tracking());
    nm::mark_tracking_for_test(g);      // menuWillOpen: for OUR generation
    CHECK(nm::tracking() && nm::busy());
    nm::Result r;
    r.generation = g;
    r.dismissed = true;
    nm::inject_result_for_test(r);      // menuDidClose:
    CHECK(!nm::tracking() && !nm::busy());
    nm::Result out;
    CHECK(nm::take_result(g, &out) && out.dismissed);
}

// The permutation AppKit actually produces (measured on the first pick run):
// menuDidClose: fires BEFORE the picked item's action. The bridge defers the
// dismissal one run-loop turn; the pick lands first and the deferred close
// arrives as a stale result for a generation no longer tracking -- dropped.
// Exactly ONE terminal result, and it is the pick.
static void test_close_before_action_publishes_the_pick_not_a_dismissal() {
    const auto g = nm::request(req("session:c", 9));
    nm::mark_tracking_for_test(g);  // on screen
    // 1. menuDidClose: (deferred): nothing published yet -- modelled by NOT
    //    delivering anything on this turn.
    nm::Result out;
    CHECK(!nm::take_result(g, &out));
    // 2. the item's action: the pick publishes and ends tracking.
    nm::Result pick;
    pick.generation = g;
    pick.row = 8;
    nm::inject_result_for_test(pick);
    CHECK(!nm::tracking());
    // 3. the deferred dismissal turn arrives for the same generation: stale.
    nm::Result late;
    late.generation = g;
    late.dismissed = true;
    nm::inject_result_for_test(late);
    // The caller takes exactly the pick, once; nothing lingers after.
    CHECK(nm::take_result(g, &out) && !out.dismissed && out.row == 8);
    CHECK(!nm::take_result(g, &out));
    CHECK(!nm::has_pending_result());
    // And the other order (action never fires -- Escape): only the dismissal.
    const auto g2 = nm::request(req("session:c2", 1));
    nm::mark_tracking_for_test(g2);
    nm::Result esc;
    esc.generation = g2;
    esc.dismissed = true;
    nm::inject_result_for_test(esc);
    CHECK(nm::take_result(g2, &out) && out.dismissed);
    CHECK(!nm::has_pending_result());
}

static void test_no_heartbeat_without_a_shown_menu() {
    // Everything above ran without a window: no menu was ever popped, so the
    // heartbeat timer never existed and the frame hook was never asked.
    CHECK(g_frames_asked == 0);
    CHECK(g_releases_asked == 0);  // the stuck-button release runs only after a real pop-up
    CHECK(nm::frames_while_tracking() == 0);
}

int main() {
    test_tracking_is_set_by_the_open_event_only();
    test_dispatched_but_not_shown_is_still_owned();
    test_dispatched_then_shown_without_a_window_dismisses_once();
    test_pump_moves_exactly_one_request();
    test_headless_is_unavailable();
    test_request_result_take_once();
    test_no_reentry_while_open();
    test_stale_and_foreign_results_are_dropped();
    test_cancel_of_a_queued_menu_is_a_dismissal();
    test_pump_is_a_noop_with_nothing_queued();
    test_submenu_child_rides_the_result();
    test_close_before_action_publishes_the_pick_not_a_dismissal();
    test_no_heartbeat_without_a_shown_menu();
    if (failures == 0) std::printf("test_native_menu: OK\n");
    return failures == 0 ? 0 : 1;
}
