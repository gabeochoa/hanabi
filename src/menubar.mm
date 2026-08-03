// menubar.mm
// Native macOS menu-bar extra (NSStatusItem) for hanabi. Obj-C++ behind the
// extern "C" seam declared in menubar.h — same pattern as metal_activate_app()
// in sokol_impl.mm (an AppKit function the C++ core calls without knowing any
// Obj-C). This is an ambient affordance: a small glyph in the system menu bar
// whose title reflects "N blocked on you", with a quick-action dropdown.
//
// Threading: install + updates must run on the main thread. The app's frame
// loop runs on the main thread, so the direct calls from app_frame are safe.
//
// Action routing: the menu items are immediate UI on the AppKit side; they set
// file-static atomic flags here, and the C++ frame loop polls + clears them via
// menubar_take_*(). This keeps the immediate-mode C++ core the single owner of
// app state (no cross-thread mutation of ECS components from a menu callback).

#import <AppKit/AppKit.h>
#include <atomic>

#include "menubar.h"

// ---- action flags: set on the main thread by menu items, drained by C++ ----
static std::atomic<bool> g_want_show{false};
static std::atomic<bool> g_want_new_task{false};

// ---- the status item + its menu rows we mutate on count change -------------
static NSStatusItem* g_status_item = nil;
static NSMenuItem* g_status_row = nil;   // the live "N blocked on you" row
static int g_last_blocked = -1;          // change-guard for menubar_set_blocked

// A small firework/spark glyph for the ambient icon. Generic — no branding.
// U+2726 BLACK FOUR POINTED STAR reads as a spark/firework at menu-bar size.
static NSString* const kGlyph = @"\u2726";

// Target object for the menu items. Lives for the process lifetime.
@interface HanabiMenuTarget : NSObject
- (void)onShow:(id)sender;
- (void)onNewTask:(id)sender;
- (void)onQuit:(id)sender;
@end

@implementation HanabiMenuTarget
- (void)onShow:(id)sender {
    (void)sender;
    g_want_show.store(true);
}
- (void)onNewTask:(id)sender {
    (void)sender;
    // Bring the app forward too, so the composer is visible when it opens.
    g_want_show.store(true);
    g_want_new_task.store(true);
}
- (void)onQuit:(id)sender {
    (void)sender;
    [NSApp terminate:nil];
}
@end

static HanabiMenuTarget* g_target = nil;

// Compose the menu-bar title from the blocked count: glyph + count when there
// is attention, glyph alone when calm.
static NSString* title_for_blocked(int n) {
    if (n > 0) return [NSString stringWithFormat:@"%@ %d", kGlyph, n];
    return kGlyph;
}

// Compose the live status-row label.
static NSString* status_for_blocked(int n) {
    if (n > 0) return [NSString stringWithFormat:@"%d blocked on you", n];
    return @"All caught up";
}

void menubar_install(void) {
    @autoreleasepool {
        // Idempotent: only ever create one status item.
        if (g_status_item != nil) return;
        // NSApp must exist (the windowed run creates it). Guard defensively so
        // a mis-timed call is a no-op rather than a crash; the caller retries
        // next frame.
        if (NSApp == nil) return;

        g_target = [[HanabiMenuTarget alloc] init];

        g_status_item = [[NSStatusBar systemStatusBar]
            statusItemWithLength:NSVariableStatusItemLength];
        // Retain across autorelease pool — status items are otherwise released.
        [g_status_item retain];

        g_status_item.button.title = title_for_blocked(0);

        NSMenu* menu = [[NSMenu alloc] init];
        menu.autoenablesItems = NO;  // we control enabled state explicitly

        // Disabled header row.
        NSMenuItem* header = [[NSMenuItem alloc] initWithTitle:@"hanabi"
                                                        action:nil
                                                 keyEquivalent:@""];
        [header setEnabled:NO];
        [menu addItem:header];

        // Live status row (disabled — it's a label, not an action).
        g_status_row = [[NSMenuItem alloc] initWithTitle:status_for_blocked(0)
                                                  action:nil
                                           keyEquivalent:@""];
        [g_status_row setEnabled:NO];
        [menu addItem:g_status_row];
        [g_status_row retain];

        [menu addItem:[NSMenuItem separatorItem]];

        NSMenuItem* show = [[NSMenuItem alloc] initWithTitle:@"Show hanabi"
                                                      action:@selector(onShow:)
                                               keyEquivalent:@""];
        [show setTarget:g_target];
        [menu addItem:show];

        NSMenuItem* newTask = [[NSMenuItem alloc]
            initWithTitle:@"New task\u2026"
                   action:@selector(onNewTask:)
            keyEquivalent:@""];
        [newTask setTarget:g_target];
        [menu addItem:newTask];

        [menu addItem:[NSMenuItem separatorItem]];

        NSMenuItem* quit = [[NSMenuItem alloc] initWithTitle:@"Quit hanabi"
                                                      action:@selector(onQuit:)
                                               keyEquivalent:@""];
        [quit setTarget:g_target];
        [menu addItem:quit];

        g_status_item.menu = menu;

        g_last_blocked = 0;
        NSLog(@"menubar: installed, title=%@", g_status_item.button.title);
    }
}

void menubar_set_blocked(int n) {
    // Change-guard: skip all AppKit work when the count is unchanged. Called
    // every frame, so this is the common path.
    if (n == g_last_blocked) return;
    @autoreleasepool {
        if (g_status_item == nil) return;
        g_last_blocked = n;
        g_status_item.button.title = title_for_blocked(n);
        if (g_status_row != nil) g_status_row.title = status_for_blocked(n);
        // Chatty (fires on every blocked-count change) — silent unless
        // HANABI_NATIVE_LOG=1 (Gabe: turn off the working-as-expected logging).
        if (const char* v = getenv("HANABI_NATIVE_LOG"); v && v[0] && v[0] != '0')
            NSLog(@"menubar: blocked=%d title=%@", n, g_status_item.button.title);
    }
}

bool menubar_take_show(void) {
    return g_want_show.exchange(false);
}

bool menubar_take_new_task(void) {
    return g_want_new_task.exchange(false);
}
