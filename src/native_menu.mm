// See native_menu.h. AppKit side of the context-menu adapter.
#include "native_menu.h"

#import <AppKit/AppKit.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <unistd.h>

extern "C" int metal_drive_one_frame(void);             // sokol_impl.mm
extern "C" void metal_release_stuck_mouse_buttons(void);  // sokol_impl.mm

namespace hanabi::native_menu {
namespace {

// ---- state: written by the frame (request/pump/take) and by AppKit targets
// (tracking), both on the main thread; a mutex anyway so the slot reads are
// one word of truth regardless of which run-loop turn wrote them.
std::mutex g_mu;
std::uint64_t g_next_generation = 1;
std::optional<Request> g_queued;        // waiting for the pump
// Handed to the run loop by the pump, not yet on screen. The generation is
// STILL OWNED here: request() refuses a second menu, tracking() reports it,
// cancel() withdraws it (pop_up then finds nothing to show), and a scope
// replacement cancels and re-requests. Without this slot the moment between
// the pump and the dispatched block was owned by nothing.
std::uint64_t g_dispatched_generation = 0;
// The generation pop_up is showing (set before popUpMenuPositioningItem:,
// cleared when its result is delivered). OWNERSHIP, not proof of a menu.
std::uint64_t g_tracking_generation = 0;
// AppKit's own word: menuWillOpen: fired for this generation and
// menuDidClose: has not. The one flag that means "a menu is on screen".
std::uint64_t g_on_screen_generation = 0;
// Frames the heartbeat drove while the current/last menu tracked -- the
// runtime evidence that rendering continued, readable by a script.
std::uint64_t g_frames_while_tracking = 0;
std::optional<Result> g_result;           // the last close, until taken

NSWindow* the_window() {
    NSWindow* w = [NSApp mainWindow];
    if (w == nil) w = [NSApp keyWindow];
    if (w == nil)
        for (NSWindow* c in [NSApp windows])
            if ([c isVisible]) return c;
    return w;
}

bool window_owned_by_this_pid(NSWindow* win) {
    if (win == nil) return false;
    CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow,
                                                 static_cast<CGWindowID>([win windowNumber]));
    if (list == nullptr) return false;
    bool owned = false;
    if (CFArrayGetCount(list) == 1) {
        NSDictionary* info = (__bridge NSDictionary*)CFArrayGetValueAtIndex(list, 0);
        NSNumber* pid = info[(__bridge NSString*)kCGWindowOwnerPID];
        owned = pid != nil && [pid intValue] == getpid();
    }
    CFRelease(list);
    return owned;
}

bool log_on() {
    const char* v = std::getenv("HANABI_NATIVE_MENU_LOG");
    return v != nullptr && *v != '\0' && *v != '0';
}

void deliver(Result r) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (r.generation != g_tracking_generation) {
        if (log_on())
            std::fprintf(stderr, "[native-menu] stale result gen=%llu (tracking %llu) dropped\n",
                         (unsigned long long)r.generation,
                         (unsigned long long)g_tracking_generation);
        return;
    }
    g_result = r;
    g_tracking_generation = 0;
    g_on_screen_generation = 0;
    if (log_on())
        std::fprintf(stderr, "[native-menu] result gen=%llu row=%zu child=%zu dismissed=%d\n",
                     (unsigned long long)r.generation, r.row, r.child, r.dismissed ? 1 : 0);
}

}  // namespace

// AppKit's menuWillOpen: for the menu on screen. Only the generation pop_up
// owns can become on-screen; anything else is a stale delegate call.
void mark_on_screen(std::uint64_t generation) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (generation != g_tracking_generation) return;
    g_on_screen_generation = generation;
    if (log_on())
        std::fprintf(stderr, "[native-menu] on screen gen=%llu\n", (unsigned long long)generation);
}
}  // namespace hanabi::native_menu

// The NSMenuItem target and NSMenuDelegate. One object per pop-up; it owns
// nothing but the generation it was made for and a "picked" flag, so a
// menuDidClose after a pick is not a second result.
@interface HanabiNativeMenuBridge : NSObject <NSMenuDelegate>
@property(nonatomic) unsigned long long generation;
@property(nonatomic) BOOL picked;
- (void)pick:(NSMenuItem*)sender;
@end

@implementation HanabiNativeMenuBridge
- (void)menuWillOpen:(NSMenu*)menu {
    (void)menu;
    hanabi::native_menu::mark_on_screen(self.generation);
}
- (void)pick:(NSMenuItem*)sender {
    // representedObject: @[row, child]; child == NSNotFound for a top row.
    NSArray* rc = sender.representedObject;
    hanabi::native_menu::Result r;
    r.generation = self.generation;
    r.row = (std::size_t)[rc[0] unsignedLongValue];
    const unsigned long child = [rc[1] unsignedLongValue];
    r.child = child == NSNotFound ? hanabi::native_menu::kNoRow : (std::size_t)child;
    self.picked = YES;
    hanabi::native_menu::deliver(r);
}
- (void)menuDidClose:(NSMenu*)menu {
    (void)menu;
    if (self.picked) return;
    // The pick's action fires AFTER menuDidClose on AppKit; defer the
    // "dismissed" verdict one run-loop turn so a pick that is about to land
    // wins. deliver() drops the second one as stale.
    unsigned long long gen = self.generation;
    __weak HanabiNativeMenuBridge* weakSelf = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        HanabiNativeMenuBridge* s = weakSelf;
        if (s != nil && s.picked) return;
        hanabi::native_menu::Result r;
        r.generation = gen;
        r.dismissed = true;
        hanabi::native_menu::deliver(r);
    });
}
@end

namespace hanabi::native_menu {
namespace {

// The bridge for the menu on screen; released when its result is delivered.
HanabiNativeMenuBridge* g_bridge = nil;
NSMenu* g_menu = nil;

// The most recent right-button press, kept so the menu can be opened WITH
// the event that opened it. `popUpContextMenu:withEvent:forView:` gives
// AppKit the gesture: a right button still held when the menu appears is
// the menu's own tracking press -- release on an item picks it, release
// where it began keeps the menu up, release elsewhere dismisses -- exactly
// the reference's (and every Mac app's) context menu. The programmatic
// `popUpMenuPositioningItem:` knows nothing of the press, so a right-UP
// arriving once it tracks reads as "released outside" and dismisses
// (measured: the tab menu closed itself ~20 frames in, no key). The
// monitor is a LOCAL NSEvent monitor -- it observes and passes the event
// through unchanged; sokol's view still receives it -- installed once.
id g_right_down_monitor = nil;
NSEvent* g_last_right_down = nil;
void install_right_down_monitor() {
    if (g_right_down_monitor != nil) return;
    g_right_down_monitor = [NSEvent
        addLocalMonitorForEventsMatchingMask:NSEventMaskRightMouseDown
                                     handler:^NSEvent*(NSEvent* e) {
                                         std::lock_guard<std::mutex> lock(g_mu);
                                         g_last_right_down = e;
                                         return e;
                                     }];
}
// The opening event for a request anchored at content (cx, cy): the last
// right-down if it happened in this window within the last second and
// within 8 pt of the anchor; otherwise nil (keyboard-opened, or the anchor
// is not where the button went down) and the programmatic pop-up is used.
NSEvent* opening_event_for(NSWindow* win, NSView* cv, NSPoint viewPt) {
    NSEvent* e = nil;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        e = g_last_right_down;
    }
    const auto reject = [&](const char* why, double a, double b) {
        if (log_on())
            std::fprintf(stderr, "[native-menu] opening press rejected: %s (%.1f, %.1f)\n", why, a, b);
        return (NSEvent*)nil;
    };
    if (e == nil) return reject("no right-down captured", 0, 0);
    // A posted event's `window` may be resolved by number rather than
    // identity; compare numbers.
    if (e.window == nil || e.window.windowNumber != win.windowNumber)
        return reject("other window", e.window != nil ? (double)e.window.windowNumber : -1.0,
                      (double)win.windowNumber);
    const double age = [[NSProcessInfo processInfo] systemUptime] - e.timestamp;
    if (age > 1.0) return reject("too old (s)", age, 1.0);
    // Both points in the VIEW's coordinate space: viewPt came through
    // content_to_view (flipped as the view is), the event through the
    // view's own convertPoint -- no separate flip arithmetic.
    const NSPoint inView = [cv convertPoint:e.locationInWindow fromView:nil];
    const double dx = std::fabs(inView.x - viewPt.x), dy = std::fabs(inView.y - viewPt.y);
    if (dx > 8.0 || dy > 8.0) return reject("too far from anchor (dx, dy)", dx, dy);
    return e;
}

NSMenuItem* make_item(const Item& leaf, std::size_t row, std::size_t child,
                      HanabiNativeMenuBridge* bridge) {
    NSString* title = [NSString stringWithUTF8String:leaf.label.c_str()];
    NSMenuItem* it = [[NSMenuItem alloc] initWithTitle:title action:@selector(pick:) keyEquivalent:@""];
    it.target = bridge;
    it.enabled = leaf.disabled ? NO : YES;
    it.representedObject = @[ @(row), @(child) ];
    if (leaf.destructive) {
        // The reference's destructive rows are red in the system's own
        // destructive colour; AppKit menus take an attributed title for that.
        NSDictionary* attrs = @{
            NSForegroundColorAttributeName : [NSColor systemRedColor],
            NSFontAttributeName : [NSFont menuFontOfSize:0]
        };
        it.attributedTitle = [[NSAttributedString alloc] initWithString:title attributes:attrs];
    }
    return it;
}

NSMenu* build_menu(const Request& req, HanabiNativeMenuBridge* bridge) {
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];
    menu.autoenablesItems = NO;  // enabled state is the app's, set per item
    menu.delegate = bridge;
    for (std::size_t i = 0; i < req.items.size(); ++i) {
        const Item& m = req.items[i];
        if (m.separator) {
            [menu addItem:[NSMenuItem separatorItem]];
            continue;
        }
        NSMenuItem* it = make_item(m, i, static_cast<std::size_t>(NSNotFound), bridge);
        if (!m.children.empty()) {
            // A row with children is a submenu; the row itself does not pick.
            it.action = nil;
            it.target = nil;
            NSMenu* sub = [[NSMenu alloc] initWithTitle:it.title];
            sub.autoenablesItems = NO;
            for (std::size_t c = 0; c < m.children.size(); ++c)
                [sub addItem:make_item(m.children[c], i, c, bridge)];
            it.submenu = sub;
        }
        [menu addItem:it];
    }
    return menu;
}

// Content-space (top-left, points) -> the view's coordinate space, the same
// conversion the capture receipt and the native pointer use; no titlebar
// arithmetic.
NSPoint content_to_view(NSView* cv, float cx, float cy) {
    const NSRect b = [cv bounds];
    if ([cv isFlipped]) return NSMakePoint(b.origin.x + cx, b.origin.y + cy);
    return NSMakePoint(b.origin.x + cx, b.origin.y + b.size.height - cy);
}

void pop_up(Request req) {
    // Claim: this generation must still be the dispatched one. A cancel() or
    // a scope replacement between the pump and this turn cleared it (and
    // wrote the dismissal); then there is nothing to show.
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (g_dispatched_generation != req.generation) {
            if (log_on())
                std::fprintf(stderr, "[native-menu] popup gen=%llu withdrawn before showing\n",
                             (unsigned long long)req.generation);
            return;
        }
        g_dispatched_generation = 0;
        g_frames_while_tracking = 0;
        // From here the generation lives in g_tracking_generation (set below
        // before the pop-up, or by the no-window path).
    }
    NSWindow* win = the_window();
    if (win == nil || !window_owned_by_this_pid(win) || ![win isVisible]) {
        // The window went away between request and pump: the caller sees a
        // dismissal, not a hang.
        Result r;
        r.generation = req.generation;
        r.dismissed = true;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_tracking_generation = req.generation;
        }
        deliver(r);
        return;
    }
    HanabiNativeMenuBridge* bridge = [HanabiNativeMenuBridge new];
    bridge.generation = req.generation;
    bridge.picked = NO;
    NSMenu* menu = build_menu(req, bridge);
    // The APP's appearance, resolved at the UI boundary and carried in the
    // request: the reference's menus follow its theme, not the desktop's.
    // Unspecified (no theme known) falls back to the window's.
    switch (req.appearance) {
        case Appearance::Dark:
            menu.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
            break;
        case Appearance::Light:
            menu.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
            break;
        case Appearance::Unspecified:
            menu.appearance = win.effectiveAppearance;
            break;
    }
    NSView* cv = [win contentView];
    const NSPoint at = content_to_view(cv, req.content_x, req.content_y);
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_tracking_generation = req.generation;
        g_bridge = bridge;
        g_menu = menu;
    }
    if (log_on())
        std::fprintf(stderr, "[native-menu] popup gen=%llu scope=%s items=%zu appearance=%s at content=(%.1f,%.1f) view=(%.1f,%.1f)\n",
                     (unsigned long long)req.generation, req.scope.c_str(), req.items.size(),
                     req.appearance == Appearance::Dark    ? "dark"
                     : req.appearance == Appearance::Light ? "light"
                                                           : "window",
                     req.content_x, req.content_y, at.x, at.y);
    // The heartbeat. AppKit's menu tracking is a nested run loop in
    // NSEventTrackingRunLoopMode, and MTKView's display link is NOT serviced
    // in that mode -- measured on the first windowed run: the last frame
    // logged was the one that popped the menu, and nothing advanced until the
    // watchdog killed the process. So while the menu is up a timer scheduled
    // in the tracking mode drives one app frame per tick through the same
    // synchronous `[MTKView draw]` the live-resize path uses (guarded against
    // re-entry: a tick that lands inside a frame is skipped). Frames therefore
    // keep running -- the e2e runner ticks, `wait_frames` advances, timers
    // and folds run -- and the app's own hot/press stay swallowed by the
    // eater the shim draws. Invalidated the moment the pop-up returns.
    const std::uint64_t gen = req.generation;
    NSTimer* heartbeat = [NSTimer timerWithTimeInterval:(1.0 / 60.0)
                                                repeats:YES
                                                  block:^(NSTimer* t) {
        (void)t;
        if (metal_drive_one_frame() != 0) {
            std::lock_guard<std::mutex> lock(g_mu);
            if (g_tracking_generation == gen) ++g_frames_while_tracking;
        }
    }];
    NSRunLoop* loop = [NSRunLoop mainRunLoop];
    [loop addTimer:heartbeat forMode:NSEventTrackingRunLoopMode];
    [loop addTimer:heartbeat forMode:NSDefaultRunLoopMode];
    // Nested run loop: this returns when the menu closes. We are on a
    // dispatch_async turn of the main queue -- NOT inside drawRect. Opened
    // WITH the right-down that asked for it when there is one (see
    // install_right_down_monitor), so the held button is the menu's own
    // tracking press; the programmatic pop-up otherwise (a keyboard or
    // scripted opening with no press).
    NSEvent* opener = opening_event_for(win, cv, at);
    if (log_on())
        std::fprintf(stderr, "[native-menu] gen=%llu opening via %s\n", (unsigned long long)gen,
                     opener != nil ? "popUpContextMenu:withEvent: (the right-down)"
                                   : "popUpMenuPositioningItem: (no opening press)");
    if (opener != nil)
        [NSMenu popUpContextMenu:menu withEvent:opener forView:cv];
    else
        [menu popUpMenuPositioningItem:nil atLocation:at inView:cv];
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_last_right_down = nil;  // consumed: never reused for a later menu
    }
    [heartbeat invalidate];
    // The right-up that ended the gesture was AppKit's; sokol never saw it.
    metal_release_stuck_mouse_buttons();
    if (log_on()) {
        std::lock_guard<std::mutex> lock(g_mu);
        std::fprintf(stderr, "[native-menu] popup gen=%llu returned; frames driven while tracking=%llu\n",
                     (unsigned long long)gen, (unsigned long long)g_frames_while_tracking);
    }
    // Whatever happened, the tracking is over. The result (pick or the
    // deferred dismissal) is delivered by the bridge; here we only release.
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_bridge = nil;
        g_menu = nil;
    }
}

}  // namespace

bool available() {
    @autoreleasepool {
        NSWindow* win = the_window();
        return win != nil && [win isVisible] && window_owned_by_this_pid(win);
    }
}

std::uint64_t request(Request req) {
    std::lock_guard<std::mutex> lock(g_mu);
    // No re-entry in any of the three lives: queued, dispatched, tracking.
    if (g_queued.has_value() || g_dispatched_generation != 0 || g_tracking_generation != 0)
        return 0;
    req.generation = g_next_generation++;
    g_result.reset();
    g_queued = std::move(req);
    return g_queued->generation;
}

bool busy() {
    std::lock_guard<std::mutex> lock(g_mu);
    return g_queued.has_value() || g_dispatched_generation != 0 || g_tracking_generation != 0;
}

bool tracking() {
    std::lock_guard<std::mutex> lock(g_mu);
    return g_on_screen_generation != 0;
}

void mark_tracking_for_test(std::uint64_t generation) {
    // As menuWillOpen: would: only the generation pop_up owns. For a routing
    // script the "owner" is the injected path (inject_result_for_test moves a
    // queued/dispatched generation to tracking), so allow that too.
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_queued.has_value() && g_queued->generation == generation) {
        g_queued.reset();
        g_tracking_generation = generation;
    } else if (g_dispatched_generation == generation) {
        g_dispatched_generation = 0;
        g_tracking_generation = generation;
    }
    if (g_tracking_generation == generation) g_on_screen_generation = generation;
}

std::uint64_t current_generation() {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_queued.has_value()) return g_queued->generation;
    if (g_dispatched_generation != 0) return g_dispatched_generation;
    return g_tracking_generation;
}

void pump_after_frame() {
    // The right-down monitor must exist BEFORE the press that opens a menu.
    // The row menu is release-activated, so the request arrives the frame
    // after the down; installed from request() the monitor missed the very
    // press it was for -- every open fell to the programmatic pop-up
    // (measured: five of five runs "no opening press"). Installed here, on
    // the first frame, idempotent.
    install_right_down_monitor();
    std::optional<Request> take;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (!g_queued.has_value() || g_dispatched_generation != 0) return;
        take = std::move(g_queued);
        g_queued.reset();
        g_dispatched_generation = take->generation;  // owned until pop_up claims or drops it
    }
    // Copy into the block; the pop-up runs on the NEXT run-loop turn.
    Request copy = std::move(*take);
    dispatch_async(dispatch_get_main_queue(), ^{
        @autoreleasepool {
            pop_up(copy);
        }
    });
}

bool take_result(std::uint64_t generation, Result* out) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_result.has_value() || g_result->generation != generation) return false;
    if (out != nullptr) *out = *g_result;
    g_result.reset();
    return true;
}

std::uint64_t frames_while_tracking() {
    std::lock_guard<std::mutex> lock(g_mu);
    return g_frames_while_tracking;
}

bool has_pending_result() {
    std::lock_guard<std::mutex> lock(g_mu);
    return g_result.has_value();
}

void cancel(std::uint64_t generation) {
    NSMenu* menu = nil;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        // Queued or dispatched-not-yet-shown: withdraw it and answer with a
        // dismissal; pop_up, when its turn comes, finds it withdrawn.
        const bool queued = g_queued.has_value() && g_queued->generation == generation;
        const bool dispatched = g_dispatched_generation == generation;
        if (queued || dispatched) {
            if (queued) g_queued.reset();
            if (dispatched) g_dispatched_generation = 0;
            Result r;
            r.generation = generation;
            r.dismissed = true;
            g_result = r;
            return;
        }
        if (g_tracking_generation == generation) menu = g_menu;
    }
    if (menu != nil) [menu cancelTracking];  // its close arrives via the bridge
}

int drain_main_queue_for_test() {
    // Run the main run loop's queued sources until nothing is left this turn
    // (dispatch_async blocks for the main queue are delivered here when this
    // thread is the main thread). Bounded.
    int turns = 0;
    @autoreleasepool {
        while (turns < 8 &&
               CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true) == kCFRunLoopRunHandledSource)
            ++turns;
    }
    return turns;
}

void inject_result_for_test(Result r) {
    // Exactly the path an NSMenuItem target takes: a typed result for the
    // menu that is tracking. If the injected generation is not the tracking
    // one it is dropped, the same as a stale callback.
    {
        std::lock_guard<std::mutex> lock(g_mu);
        // A queued-but-not-yet-popped menu is treated as tracking for the
        // injection, so a headless routing script can prove the route
        // without AppKit. It is never a claim about AppKit.
        if (g_queued.has_value() && g_queued->generation == r.generation) {
            g_queued.reset();
            g_tracking_generation = r.generation;
        } else if (g_dispatched_generation == r.generation) {
            g_dispatched_generation = 0;
            g_tracking_generation = r.generation;
        }
    }
    deliver(r);
    NSMenu* menu = nil;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        menu = g_menu;
    }
    if (menu != nil) [menu cancelTracking];
}

}  // namespace hanabi::native_menu
