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
#import <Carbon/Carbon.h>
#include <branding.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>

#include "menubar.h"
#include "settings.h"
#include "shortcuts.h"

// ---- action flags: set on the main thread by menu items, drained by C++ ----
static std::atomic<bool> g_want_show{false};
static std::atomic<bool> g_want_new_task{false};
static std::atomic<int> g_command{-1};
static std::atomic<int> g_recording_command{-1};
static std::atomic<int> g_recorded_key{0};
static std::atomic<unsigned char> g_recorded_modifiers{0};

// ---- the status item + its menu rows we mutate on count change -------------
static NSStatusItem* g_status_item = nil;
static NSMenuItem* g_status_row = nil;   // the live "N blocked on you" row
static int g_last_blocked = -1;          // change-guard for menubar_set_blocked
static NSMenu* g_main_menu = nil;
static std::array<NSMenuItem*, hanabi::shortcuts::kDefinitions.size()>
    g_command_items{};
static NSResponder* g_edit_bridge = nil;
struct StandardMenuBinding {
    NSMenuItem* item = nil;
    std::string key;
    NSEventModifierFlags modifiers = 0;
};
static std::vector<StandardMenuBinding> g_standard_bindings;

// A small firework/spark glyph for the ambient icon. Generic — no branding.
// U+2726 BLACK FOUR POINTED STAR reads as a spark/firework at menu-bar size.
static NSString* const kGlyph = @"\u2726";

// Target object for the menu items. Lives for the process lifetime.
@interface HanabiMenuTarget : NSObject
- (void)onShow:(id)sender;
- (void)onNewTask:(id)sender;
- (void)onQuit:(id)sender;
- (void)onCommand:(id)sender;
@end

@implementation HanabiMenuTarget
- (void)onShow:(id)sender {
    (void)sender;
    g_want_show.store(true);
}
- (void)onNewTask:(id)sender {
    (void)sender;
    g_want_show.store(true);
    g_command.store(static_cast<int>(hanabi::shortcuts::Command::NewTask));
}
- (void)onQuit:(id)sender {
    (void)sender;
    [NSApp terminate:nil];
}
- (void)onCommand:(id)sender {
    NSMenuItem* item = (NSMenuItem*)sender;
    const int command = static_cast<int>(item.tag);
    NSEvent* event = [NSApp currentEvent];
    if (g_recording_command.load() >= 0 && event != nil &&
        event.type == NSEventTypeKeyDown && command >= 0 &&
        command < static_cast<int>(hanabi::shortcuts::Command::Count)) {
        const auto shortcut = Settings::get().get_shortcut(
            static_cast<hanabi::shortcuts::Command>(command));
        g_recorded_key.store(shortcut.key);
        g_recorded_modifiers.store(shortcut.modifiers);
        return;
    }
    g_command.store(command);
}
@end

static HanabiMenuTarget* g_target = nil;

static bool bundled_app() {
    NSString* identifier = [[NSBundle mainBundle] bundleIdentifier];
    NSString* expected = [NSString
        stringWithUTF8String:product_branding::kBundleIdentifier];
    return identifier != nil && [identifier isEqualToString:expected];
}

static NSEventModifierFlags native_modifiers(std::uint8_t modifiers) {
    NSEventModifierFlags flags = 0;
    if (modifiers & hanabi::shortcuts::CommandModifier)
        flags |= NSEventModifierFlagCommand;
    if (modifiers & hanabi::shortcuts::ShiftModifier)
        flags |= NSEventModifierFlagShift;
    if (modifiers & hanabi::shortcuts::OptionModifier)
        flags |= NSEventModifierFlagOption;
    if (modifiers & hanabi::shortcuts::ControlModifier)
        flags |= NSEventModifierFlagControl;
    return flags;
}

static void replay_edit_key(unsigned short keyCode, NSString* characters,
                            NSEventModifierFlags modifiers) {
    NSWindow* window = [NSApp keyWindow];
    if (window == nil) window = [NSApp mainWindow];
    if (window == nil) return;
    NSEvent* down = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                     location:NSZeroPoint
                                modifierFlags:modifiers
                                    timestamp:[NSProcessInfo processInfo].systemUptime
                                 windowNumber:window.windowNumber
                                      context:nil
                                   characters:characters
                  charactersIgnoringModifiers:[characters lowercaseString]
                                    isARepeat:NO
                                      keyCode:keyCode];
    NSEvent* up = [NSEvent keyEventWithType:NSEventTypeKeyUp
                                   location:NSZeroPoint
                              modifierFlags:modifiers
                                  timestamp:[NSProcessInfo processInfo].systemUptime
                               windowNumber:window.windowNumber
                                    context:nil
                                 characters:characters
                charactersIgnoringModifiers:[characters lowercaseString]
                                  isARepeat:NO
                                    keyCode:keyCode];
    [window sendEvent:down];
    [window sendEvent:up];
}

bool menubar_edit_binding(const char* selector, unsigned short* keyCode,
                           unsigned long long* modifiers) {
    if (selector == nullptr || keyCode == nullptr || modifiers == nullptr)
        return false;
    struct Binding {
        const char* selector;
        unsigned short keyCode;
        NSEventModifierFlags modifiers;
    };
    static const Binding bindings[] = {
        {"undo:", kVK_ANSI_Z, NSEventModifierFlagCommand},
        {"redo:", kVK_ANSI_Z,
         NSEventModifierFlagCommand | NSEventModifierFlagShift},
        {"cut:", kVK_ANSI_X, NSEventModifierFlagCommand},
        {"copy:", kVK_ANSI_C, NSEventModifierFlagCommand},
        {"paste:", kVK_ANSI_V, NSEventModifierFlagCommand},
        {"selectAll:", kVK_ANSI_A, NSEventModifierFlagCommand},
    };
    for (const auto& binding : bindings) {
        if (std::strcmp(selector, binding.selector) != 0) continue;
        *keyCode = binding.keyCode;
        *modifiers = static_cast<unsigned long long>(binding.modifiers);
        return true;
    }
    return false;
}

static void replay_edit_action(const char* selector, NSString* characters) {
    unsigned short keyCode = 0;
    unsigned long long modifiers = 0;
    if (!menubar_edit_binding(selector, &keyCode, &modifiers)) return;
    replay_edit_key(keyCode, characters,
                    static_cast<NSEventModifierFlags>(modifiers));
}

@interface HanabiEditBridge : NSResponder
@end

@implementation HanabiEditBridge
- (void)undo:(id)sender {
    (void)sender;
    replay_edit_action("undo:", @"z");
}
- (void)redo:(id)sender {
    (void)sender;
    replay_edit_action("redo:", @"Z");
}
- (void)cut:(id)sender {
    (void)sender;
    replay_edit_action("cut:", @"x");
}
- (void)copy:(id)sender {
    (void)sender;
    replay_edit_action("copy:", @"c");
}
- (void)paste:(id)sender {
    (void)sender;
    replay_edit_action("paste:", @"v");
}
- (void)selectAll:(id)sender {
    (void)sender;
    replay_edit_action("selectAll:", @"a");
}
@end

static NSMenuItem* item(NSString* title, SEL action, NSString* key,
                        id target) {
    NSMenuItem* result = [[[NSMenuItem alloc] initWithTitle:title
                                                    action:action
                                             keyEquivalent:key] autorelease];
    result.target = target;
    return result;
}

static NSMenuItem* command_item(hanabi::shortcuts::Command command) {
    const auto& def = hanabi::shortcuts::definition(command);
    NSString* title = [NSString stringWithUTF8String:
        std::string(def.title).c_str()];
    NSMenuItem* result = item(title, @selector(onCommand:), @"", g_target);
    result.tag = static_cast<NSInteger>(command);
    g_command_items[hanabi::shortcuts::index(command)] = result;
    return result;
}

void menubar_refresh_shortcuts(void) {
    if (g_main_menu == nil) return;
    const bool recording = g_recording_command.load() >= 0;
    for (const auto& def : hanabi::shortcuts::kDefinitions) {
        NSMenuItem* menuItem =
            g_command_items[hanabi::shortcuts::index(def.command)];
        if (menuItem == nil) continue;
        if (recording) {
            menuItem.keyEquivalent = @"";
            continue;
        }
        const auto shortcut = Settings::get().get_shortcut(def.command);
        const std::string equivalent =
            hanabi::shortcuts::native_key_equivalent(shortcut);
        menuItem.keyEquivalent =
            [NSString stringWithUTF8String:equivalent.c_str()];
        menuItem.keyEquivalentModifierMask =
            native_modifiers(shortcut.modifiers);
    }
}

static void collect_standard_bindings(NSMenu* menu) {
    for (NSMenuItem* menuItem in menu.itemArray) {
        if (menuItem.target != g_target && menuItem.keyEquivalent.length > 0) {
            g_standard_bindings.push_back(
                {menuItem, std::string(menuItem.keyEquivalent.UTF8String),
                 menuItem.keyEquivalentModifierMask});
        }
        if (menuItem.submenu != nil)
            collect_standard_bindings(menuItem.submenu);
    }
}

static void install_main_menu() {
    if (g_main_menu != nil || !bundled_app()) return;

    NSString* appName =
        [NSString stringWithUTF8String:product_branding::kAppName];
    g_main_menu = [[NSMenu alloc] initWithTitle:@""];

    NSMenuItem* appRoot = item(appName, nil, @"", nil);
    NSMenu* appMenu = [[[NSMenu alloc] initWithTitle:appName] autorelease];
    [appMenu addItem:item([NSString stringWithFormat:@"About %@", appName],
                          @selector(orderFrontStandardAboutPanel:), @"", nil)];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItem:command_item(hanabi::shortcuts::Command::OpenSettings)];
    [appMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* servicesRoot = item(@"Services", nil, @"", nil);
    NSMenu* servicesMenu = [[[NSMenu alloc] initWithTitle:@"Services"] autorelease];
    servicesRoot.submenu = servicesMenu;
    [appMenu addItem:servicesRoot];
    [NSApp setServicesMenu:servicesMenu];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItem:item([NSString stringWithFormat:@"Hide %@", appName],
                          @selector(hide:), @"h", nil)];
    NSMenuItem* hideOthers = item(@"Hide Others", @selector(hideOtherApplications:),
                                  @"h", nil);
    hideOthers.keyEquivalentModifierMask =
        NSEventModifierFlagCommand | NSEventModifierFlagOption;
    [appMenu addItem:hideOthers];
    [appMenu addItem:item(@"Show All", @selector(unhideAllApplications:), @"", nil)];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItem:item([NSString stringWithFormat:@"Quit %@", appName],
                          @selector(terminate:), @"q", nil)];
    appRoot.submenu = appMenu;
    [g_main_menu addItem:appRoot];

    NSMenuItem* fileRoot = item(@"File", nil, @"", nil);
    NSMenu* fileMenu = [[[NSMenu alloc] initWithTitle:@"File"] autorelease];
    [fileMenu addItem:command_item(hanabi::shortcuts::Command::NewTask)];
    [fileMenu addItem:command_item(hanabi::shortcuts::Command::CloseTab)];
    fileRoot.submenu = fileMenu;
    [g_main_menu addItem:fileRoot];

    NSMenuItem* editRoot = item(@"Edit", nil, @"", nil);
    NSMenu* editMenu = [[[NSMenu alloc] initWithTitle:@"Edit"] autorelease];
    [editMenu addItem:item(@"Undo", @selector(undo:), @"z", nil)];
    NSMenuItem* redo = item(@"Redo", @selector(redo:), @"Z", nil);
    redo.keyEquivalentModifierMask =
        NSEventModifierFlagCommand | NSEventModifierFlagShift;
    [editMenu addItem:redo];
    [editMenu addItem:[NSMenuItem separatorItem]];
    [editMenu addItem:item(@"Cut", @selector(cut:), @"x", nil)];
    [editMenu addItem:item(@"Copy", @selector(copy:), @"c", nil)];
    [editMenu addItem:item(@"Paste", @selector(paste:), @"v", nil)];
    [editMenu addItem:item(@"Select All", @selector(selectAll:), @"a", nil)];
    editRoot.submenu = editMenu;
    [g_main_menu addItem:editRoot];

    NSMenuItem* viewRoot = item(@"View", nil, @"", nil);
    NSMenu* viewMenu = [[[NSMenu alloc] initWithTitle:@"View"] autorelease];
    [viewMenu addItem:command_item(hanabi::shortcuts::Command::FindInThread)];
    [viewMenu addItem:command_item(hanabi::shortcuts::Command::FindNext)];
    [viewMenu addItem:command_item(hanabi::shortcuts::Command::FindPrevious)];
    [viewMenu addItem:[NSMenuItem separatorItem]];
    [viewMenu addItem:command_item(hanabi::shortcuts::Command::SearchThreads)];
    [viewMenu addItem:command_item(hanabi::shortcuts::Command::OpenPalette)];
    [viewMenu addItem:[NSMenuItem separatorItem]];
    [viewMenu addItem:command_item(hanabi::shortcuts::Command::ToggleSidebar)];
    [viewMenu addItem:command_item(hanabi::shortcuts::Command::ToggleSplit)];
    viewRoot.submenu = viewMenu;
    [g_main_menu addItem:viewRoot];

    NSMenuItem* windowRoot = item(@"Window", nil, @"", nil);
    NSMenu* windowMenu = [[[NSMenu alloc] initWithTitle:@"Window"] autorelease];
    [windowMenu addItem:item(@"Minimize", @selector(performMiniaturize:), @"m", nil)];
    [windowMenu addItem:item(@"Zoom", @selector(performZoom:), @"", nil)];
    [windowMenu addItem:[NSMenuItem separatorItem]];
    [windowMenu addItem:item(@"Bring All to Front", @selector(arrangeInFront:), @"", nil)];
    windowRoot.submenu = windowMenu;
    [g_main_menu addItem:windowRoot];
    [NSApp setWindowsMenu:windowMenu];

    NSMenuItem* helpRoot = item(@"Help", nil, @"", nil);
    NSMenu* helpMenu = [[[NSMenu alloc] initWithTitle:@"Help"] autorelease];
    [helpMenu addItem:command_item(hanabi::shortcuts::Command::OpenShortcuts)];
    helpRoot.submenu = helpMenu;
    [g_main_menu addItem:helpRoot];
    [NSApp setHelpMenu:helpMenu];

    [NSApp setMainMenu:g_main_menu];
    g_standard_bindings.clear();
    collect_standard_bindings(g_main_menu);
    g_edit_bridge = [[HanabiEditBridge alloc] init];
    NSView* content = [NSApp keyWindow].contentView;
    if (content != nil) {
        g_edit_bridge.nextResponder = content.nextResponder;
        content.nextResponder = g_edit_bridge;
    }
    menubar_refresh_shortcuts();
}

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
        install_main_menu();

        g_status_item = [[NSStatusBar systemStatusBar]
            statusItemWithLength:NSVariableStatusItemLength];
        // Retain across autorelease pool — status items are otherwise released.
        [g_status_item retain];

        g_status_item.button.title = title_for_blocked(0);

        NSMenu* menu = [[NSMenu alloc] init];
        menu.autoenablesItems = NO;  // we control enabled state explicitly

        // Disabled header row.
        NSString* app_name =
            [NSString stringWithUTF8String:product_branding::kAppName];
        NSMenuItem* header = [[NSMenuItem alloc] initWithTitle:app_name
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

        NSString* show_title = [NSString stringWithFormat:@"Show %@", app_name];
        NSMenuItem* show = [[NSMenuItem alloc] initWithTitle:show_title
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

        NSString* quit_title = [NSString stringWithFormat:@"Quit %@", app_name];
        NSMenuItem* quit = [[NSMenuItem alloc] initWithTitle:quit_title
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

void menubar_set_shortcut_recording(int command) {
    const int previous = g_recording_command.exchange(command);
    if (previous == command || g_main_menu == nil) return;
    if (command >= 0) {
        for (const auto& binding : g_standard_bindings)
            binding.item.keyEquivalent = @"";
        menubar_refresh_shortcuts();
        return;
    }
    for (const auto& binding : g_standard_bindings) {
        binding.item.keyEquivalent =
            [NSString stringWithUTF8String:binding.key.c_str()];
        binding.item.keyEquivalentModifierMask = binding.modifiers;
    }
    menubar_refresh_shortcuts();
}

bool menubar_take_command(int* command) {
    const int value = g_command.exchange(-1);
    if (value < 0 || command == nullptr) return false;
    *command = value;
    return true;
}

void menubar_simulate_command(int command) {
    if (command < 0 ||
        command >= static_cast<int>(hanabi::shortcuts::Command::Count))
        return;
    g_command.store(command);
}

bool menubar_take_recorded_shortcut(int* key, unsigned char* modifiers) {
    const int value = g_recorded_key.exchange(0);
    if (value == 0 || key == nullptr || modifiers == nullptr) return false;
    *key = value;
    *modifiers = g_recorded_modifiers.exchange(0);
    return true;
}

void menubar_diagnostics(char* out, int cap) {
    if (out == nullptr || cap <= 0) return;
    std::snprintf(out, static_cast<std::size_t>(cap),
                  "bundled=%s main_menu=%s command_items=%zu edit_bridge=%s recording=%d key_equivalents=%s",
                  bundled_app() ? "true" : "false",
                  g_main_menu != nil ? "installed" : "absent",
                  hanabi::shortcuts::kDefinitions.size(),
                  g_edit_bridge != nil ? "installed" : "absent",
                  g_recording_command.load(),
                  g_recording_command.load() >= 0 ? "suspended" : "active");
    out[cap - 1] = '\0';
}
