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
#include <string>

#include "menubar.h"
#include "ui/mm3_faces.h"
#include "ui/mm3_menubar.h"
#include <vector>
#include "resize_drive.h"
#include "settings.h"
#include "edit_verbs.h"
#include "quit_confirmation.h"
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
    if (!menubar_confirm_quit_if_asked()) return;
    [NSApp terminate:nil];
}
- (BOOL)validateMenuItem:(NSMenuItem*)item {
    if (item.action != @selector(onCommand:)) return YES;
    const int command = static_cast<int>(item.tag);
    if (command == static_cast<int>(hanabi::shortcuts::Command::CloseKeptTab) &&
        !hanabi_native_tab_host_is_key())
        return NO;
    return menubar_command_enabled(command) ? YES : NO;
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
    if (command == static_cast<int>(hanabi::shortcuts::Command::CloseKeptTab) &&
        !hanabi_native_tab_host_is_key())
        return;
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

@interface HanabiEditBridge : NSResponder
@end

@implementation HanabiEditBridge
- (void)undo:(id)sender {
    (void)sender;
    menubar_push_edit_verb(static_cast<int>(hanabi::EditVerb::Undo));
}
- (void)redo:(id)sender {
    (void)sender;
    menubar_push_edit_verb(static_cast<int>(hanabi::EditVerb::Redo));
}
- (void)cut:(id)sender {
    (void)sender;
    menubar_push_edit_verb(static_cast<int>(hanabi::EditVerb::Cut));
}
- (void)copy:(id)sender {
    (void)sender;
    menubar_push_edit_verb(static_cast<int>(hanabi::EditVerb::Copy));
}
- (void)paste:(id)sender {
    (void)sender;
    menubar_push_edit_verb(static_cast<int>(hanabi::EditVerb::Paste));
}
- (void)selectAll:(id)sender {
    (void)sender;
    menubar_push_edit_verb(static_cast<int>(hanabi::EditVerb::SelectAll));
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
        if (recording || !Settings::get().get_shortcut_enabled(def.command)) {
            menuItem.keyEquivalent = @"";
            continue;
        }
        const auto shortcut = Settings::get().get_shortcut(def.command);
        const std::string equivalent =
            hanabi::shortcuts::shifted_alias(def.command, shortcut)
                ? std::string("+")
                : hanabi::shortcuts::native_key_equivalent(shortcut);
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
    // NOT gated on the bundle any more. macOS delivers a Cmd chord through
    // -performKeyEquivalent:, and sokol's view answers NO to everything but
    // Tab (sokol_app.h), so a Cmd key never becomes a keyDown and the in-app
    // fallback in command_system.h -- keys::shortcut_pressed -- can never see
    // one. The MAIN MENU is the only thing that answers a key equivalent, and
    // `make run` launches output/hanabi.exe directly (scripts/run_app.sh), an
    // unbundled process: every Cmd shortcut in the app (Cmd+1..9 select tab,
    // Cmd+W close tab, Cmd+T new tab, Cmd+, settings) silently did nothing on
    // the path the app is actually developed and run on. An unbundled process
    // can hold a main menu perfectly well once it is a regular activation
    // policy app, which it is by the time this runs.
    if (g_main_menu != nil) return;

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
                          @selector(onQuit:), @"q", g_target)];
    appRoot.submenu = appMenu;
    [g_main_menu addItem:appRoot];

    NSMenuItem* fileRoot = item(@"File", nil, @"", nil);
    NSMenu* fileMenu = [[[NSMenu alloc] initWithTitle:@"File"] autorelease];
    [fileMenu addItem:command_item(hanabi::shortcuts::Command::NewTask)];
    [fileMenu addItem:command_item(hanabi::shortcuts::Command::NewTab)];
    [fileMenu addItem:command_item(hanabi::shortcuts::Command::ReopenClosedTab)];
    [fileMenu addItem:command_item(hanabi::shortcuts::Command::CloseTab)];
    [fileMenu addItem:command_item(hanabi::shortcuts::Command::CloseKeptTab)];
    [fileMenu addItem:[NSMenuItem separatorItem]];
    [fileMenu addItem:command_item(
                          hanabi::shortcuts::Command::ArchiveCurrentConversation)];
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
    [viewMenu addItem:[NSMenuItem separatorItem]];
    [viewMenu addItem:command_item(hanabi::shortcuts::Command::ZoomIn)];
    [viewMenu addItem:command_item(hanabi::shortcuts::Command::ZoomOut)];
    [viewMenu addItem:command_item(hanabi::shortcuts::Command::ZoomActual)];
    viewRoot.submenu = viewMenu;
    [g_main_menu addItem:viewRoot];

    NSMenuItem* windowRoot = item(@"Window", nil, @"", nil);
    NSMenu* windowMenu = [[[NSMenu alloc] initWithTitle:@"Window"] autorelease];
    [windowMenu addItem:item(@"Minimize", @selector(performMiniaturize:), @"m", nil)];
    [windowMenu addItem:item(@"Zoom", @selector(performZoom:), @"", nil)];
    [windowMenu addItem:[NSMenuItem separatorItem]];
    for (int slot = 1; slot <= hanabi::shortcuts::kTabSlots; ++slot)
        [windowMenu addItem:command_item(
                                hanabi::shortcuts::command_for_tab_slot(slot))];
    [windowMenu addItem:[NSMenuItem separatorItem]];
    // The reference's six rows, in its order: step (stops at the ends), then
    // the two wrapping cycles.
    for (const auto command : {hanabi::shortcuts::Command::ShowPreviousTab,
                               hanabi::shortcuts::Command::ShowNextTab,
                               hanabi::shortcuts::Command::CycleTabsBackward,
                               hanabi::shortcuts::Command::CycleTabsForward,
                               hanabi::shortcuts::Command::PreviousTab,
                               hanabi::shortcuts::Command::NextTab})
        [windowMenu addItem:command_item(command)];
    [windowMenu addItem:[NSMenuItem separatorItem]];
    [windowMenu addItem:item(@"Bring All to Front", @selector(arrangeInFront:), @"", nil)];
    windowRoot.submenu = windowMenu;
    [g_main_menu addItem:windowRoot];
    [NSApp setWindowsMenu:windowMenu];

    NSMenuItem* helpRoot = item(@"Help", nil, @"", nil);
    NSMenu* helpMenu = [[[NSMenu alloc] initWithTitle:@"Help"] autorelease];
    [helpMenu addItem:command_item(hanabi::shortcuts::Command::OpenShortcuts)];
    [helpMenu addItem:command_item(hanabi::shortcuts::Command::ReportBug)];
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

static std::array<std::atomic<int>, hanabi::shortcuts::kDefinitions.size()> g_command_enabled{};

static hanabi::EditVerbQueue g_edit_verbs;

void menubar_push_edit_verb(int verb) { g_edit_verbs.push(verb); }

bool menubar_take_edit_verb(int* verb) { return g_edit_verbs.take(verb); }

bool menubar_confirm_quit_if_asked(void) {
    namespace qc = hanabi::quit_confirmation;
    NSEvent* event = [NSApp currentEvent];
    const bool keyDown = event != nil && event.type == NSEventTypeKeyDown;
    const bool commandDown = event != nil && (event.modifierFlags & NSEventModifierFlagCommand) != 0;
    std::string chars;
    if (keyDown && event.charactersIgnoringModifiers != nil)
        chars = event.charactersIgnoringModifiers.UTF8String;
    const qc::Invocation invocation = qc::invocation(keyDown, commandDown, chars);
    if (!qc::should_ask(Settings::get().get_confirm_quit(), invocation)) return true;
    NSAlert* alert = [[[NSAlert alloc] init] autorelease];
    alert.messageText = [NSString stringWithUTF8String:qc::title(product_branding::kAppName).c_str()];
    alert.informativeText = [NSString stringWithUTF8String:qc::body(product_branding::kAppName).c_str()];
    [alert addButtonWithTitle:[NSString stringWithUTF8String:std::string(qc::kQuitButton).c_str()]];
    [alert addButtonWithTitle:[NSString stringWithUTF8String:std::string(qc::kCancelButton).c_str()]];
    alert.showsSuppressionButton = YES;
    alert.suppressionButton.title = [NSString stringWithUTF8String:std::string(qc::kSuppressionTitle).c_str()];
    const NSModalResponse response = [alert runModal];
    const qc::Answer answer =
        qc::answer_for_button(static_cast<int>(response - NSAlertFirstButtonReturn));
    if (qc::should_disable(answer, alert.suppressionButton.state == NSControlStateValueOn))
        Settings::get().set_confirm_quit(false);
    return answer == qc::Answer::Quit;
}

void menubar_set_command_enabled(int command, bool enabled) {
    if (command < 0 || command >= static_cast<int>(g_command_enabled.size())) return;
    g_command_enabled[static_cast<std::size_t>(command)].store(enabled ? 1 : 2);
}

bool menubar_command_enabled(int command) {
    if (command < 0 || command >= static_cast<int>(g_command_enabled.size())) return false;
    return g_command_enabled[static_cast<std::size_t>(command)].load() != 2;
}

// ---- The MM3 face, moving (ui/mm3_menubar.h; the reference's MM3MenuBar) ----
// One NSTimer at most, and only while a frame or a play is due; none at rest,
// under Reduce Motion, while the screens sleep or the session is switched
// away, or under the Normal set. A generation number stops a stale timer from
// drawing over a newer state. A frame whose pixels at the screen's scale match
// the one showing is not handed to the status item.
static int g_mm3_state = -1;
namespace {
namespace mb = hanabi::mm3::menubar;
struct Mm3MenuBar {
    bool active = false;
    mb::Mood mood = mb::Mood::Idle;
    bool screensAwake = true, sessionActive = true, appActive = true;
    NSTimer* timer = nil;
    unsigned generation = 0;
    std::vector<std::uint64_t> shownKey;
    std::string shownName;
    unsigned long swaps = 0, timersArmed = 0;
    NSMutableDictionary<NSString*, NSImage*>* images = nil;
};
Mm3MenuBar g_mm3;

bool native_log() {
    const char* v = getenv("HANABI_NATIVE_LOG");
    return v && v[0] && v[0] != '0';
}

NSImage* mm3_image(const hanabi::mm3::Cell* cells, std::size_t n, hanabi::mm3::Box b, NSString* name) {
    if (g_mm3.images == nil) g_mm3.images = [[NSMutableDictionary alloc] init];
    if (NSImage* hit = g_mm3.images[name]) return hit;
    const CGFloat side = mb::kSide;
    const CGFloat scale = (side * 0.8) / b.h;
    const NSSize size = NSMakeSize(mb::image_width(), side);
    std::vector<hanabi::mm3::Cell> copy(cells, cells + n);
    NSImage* image = [NSImage imageWithSize:size
                                    flipped:YES
                             drawingHandler:^BOOL(NSRect) {
                                 [[NSColor blackColor] setFill];
                                 const CGFloat dy = side * 0.1;
                                 for (const auto& k : copy)
                                     NSRectFill(NSMakeRect((k.x - b.x) * scale, dy + (k.y - b.y) * scale,
                                                           k.w * scale, k.h * scale));
                                 return YES;
                             }];
    [image setTemplate:YES];
    g_mm3.images[name] = image;
    return image;
}

void mm3_show(const hanabi::mm3::Cell* cells, std::size_t n, hanabi::mm3::Box b, const std::string& name) {
    if (name == g_mm3.shownName || g_status_item == nil) return;
    g_mm3.shownName = name;
    const double scale = NSScreen.mainScreen != nil ? NSScreen.mainScreen.backingScaleFactor : 2.0;
    auto key = mb::pixel_key(cells, n, b, scale);
    if (key == g_mm3.shownKey) return;  // the same pixels: no status-bar redraw
    g_mm3.shownKey = std::move(key);
    ++g_mm3.swaps;
    g_status_item.button.image = mm3_image(cells, n, b, [NSString stringWithUTF8String:name.c_str()]);
    if (native_log()) NSLog(@"menubar: mm3 frame %s (swap %lu)", name.c_str(), g_mm3.swaps);
}

void mm3_show_rest() {
    const auto face = mb::plan_for(g_mm3.mood).rest;
    const auto cells = hanabi::mm3::cells(face);
    mm3_show(cells.data, cells.size, hanabi::mm3::box_of(face), "rest-" + std::to_string(static_cast<int>(face)));
}

void mm3_play(std::size_t step);

void mm3_schedule(double seconds, std::size_t step) {
    const unsigned gen = g_mm3.generation;
    [g_mm3.timer invalidate];
    ++g_mm3.timersArmed;
    g_mm3.timer = [NSTimer scheduledTimerWithTimeInterval:std::max(0.01, seconds)
                                                  repeats:NO
                                                    block:^(NSTimer*) {
                                                        g_mm3.timer = nil;
                                                        if (!g_mm3.active || g_mm3.generation != gen) return;
                                                        mm3_play(step);
                                                    }];
}

double mm3_every(const mb::Plan& p) {
    return p.everyLo + (p.everyHi - p.everyLo) * (static_cast<double>(arc4random_uniform(1000)) / 1000.0);
}

void mm3_play(std::size_t step) {
    const mb::Plan plan = mb::plan_for(g_mm3.mood);
    const hanabi::mm3::Anim a = hanabi::mm3::anim(plan.animation);
    const mb::Step s = mb::step_at(a, step);
    if (s.done) {
        mm3_show_rest();
        if (plan.repeats()) mm3_schedule(mm3_every(plan), 0);  // the next play
        return;
    }
    const auto cells = hanabi::mm3::frame_cells(a, s.frame);
    mm3_show(cells.data, cells.size, mb::frame_box(),
             "anim-" + std::to_string(static_cast<int>(plan.animation)) + "-" + std::to_string(s.frame));
    mm3_schedule(s.seconds, s.last + 1);
}

void mm3_restart() {
    [g_mm3.timer invalidate];
    g_mm3.timer = nil;
    ++g_mm3.generation;
    g_mm3.shownName.clear();
    g_mm3.shownKey.clear();
    if (!g_mm3.active) return;
    mm3_show_rest();
    const bool reduce = [NSWorkspace sharedWorkspace].accessibilityDisplayShouldReduceMotion;
    if (!mb::moves(g_mm3.mood, g_mm3.screensAwake && g_mm3.sessionActive, reduce, g_mm3.appActive)) return;
    const mb::Plan plan = mb::plan_for(g_mm3.mood);
    // HANABI_TEST_MM3_MENUBAR_SOON: the first play starts at once (a smoke run).
    const bool soon = getenv("HANABI_TEST_MM3_MENUBAR_SOON") != nullptr;
    mm3_schedule(soon || !plan.repeats() ? 0.2 : mm3_every(plan), 0);
}

void mm3_observe_once() {
    static bool done = false;
    if (done) return;
    done = true;
    NSNotificationCenter* ws = [NSWorkspace sharedWorkspace].notificationCenter;
    NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
    const auto on = [](NSNotificationCenter* c, NSNotificationName name, void (^body)(void)) {
        [c addObserverForName:name object:nil queue:[NSOperationQueue mainQueue]
                   usingBlock:^(NSNotification*) { body(); }];
    };
    on(ws, NSWorkspaceScreensDidSleepNotification, ^{ g_mm3.screensAwake = false; mm3_restart(); });
    on(ws, NSWorkspaceScreensDidWakeNotification, ^{ g_mm3.screensAwake = true; mm3_restart(); });
    on(ws, NSWorkspaceSessionDidResignActiveNotification, ^{ g_mm3.sessionActive = false; mm3_restart(); });
    on(ws, NSWorkspaceSessionDidBecomeActiveNotification, ^{ g_mm3.sessionActive = true; mm3_restart(); });
    on(ws, NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification, ^{ mm3_restart(); });
    on(nc, NSApplicationDidBecomeActiveNotification, ^{ g_mm3.appActive = true; mm3_restart(); });
    on(nc, NSApplicationDidResignActiveNotification, ^{ g_mm3.appActive = false; mm3_restart(); });
}
}  // namespace

void menubar_set_mm3(bool on) {
    if ((on ? 1 : 0) == g_mm3_state) return;
    @autoreleasepool {
        if (g_status_item == nil) return;
        g_mm3_state = on ? 1 : 0;
        g_mm3.active = on;
        if (!on) {
            mm3_restart();  // stops the timer
            g_status_item.button.image = nil;
            g_status_item.button.title = title_for_blocked(g_last_blocked < 0 ? 0 : g_last_blocked);
            return;
        }
        mm3_observe_once();
        g_mm3.appActive = [NSApp isActive];
        g_status_item.button.imagePosition = NSImageLeft;
        const int n = g_last_blocked < 0 ? 0 : g_last_blocked;
        g_status_item.button.title = n > 0 ? [NSString stringWithFormat:@" %d", n] : @"";
        mm3_restart();
        if (native_log())
            NSLog(@"menubar: mm3 face %.1fx%.1f template=%d", g_status_item.button.image.size.width,
                  g_status_item.button.image.size.height, (int)g_status_item.button.image.isTemplate);
    }
}

void menubar_set_mm3_mood(int needsYou, int working, bool offline) {
    const mb::Mood m = mb::mood_of(needsYou, working, offline);
    if (m == g_mm3.mood) return;
    g_mm3.mood = m;
    if (native_log()) NSLog(@"menubar: mm3 mood %d", static_cast<int>(m));
    if (g_mm3.active) mm3_restart();
}

void menubar_set_blocked(int n) {
    // Change-guard: skip all AppKit work when the count is unchanged. Called
    // every frame, so this is the common path.
    if (n == g_last_blocked) return;
    @autoreleasepool {
        if (g_status_item == nil) return;
        g_last_blocked = n;
        g_status_item.button.title = g_mm3_state == 1 ? (n > 0 ? [NSString stringWithFormat:@" %d", n] : @"")
                                                      : title_for_blocked(n);
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

bool menubar_command_pending(void) {
    return g_command.load() >= 0;
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

bool menubar_recorded_shortcut_pending(void) {
    return g_recorded_key.load() != 0;
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
