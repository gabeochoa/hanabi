#import <AppKit/AppKit.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "../../src/a11y_bridge.h"
#include "../../src/shortcuts.h"

static int failures = 0;

#define CHECK(value)                                                       \
    do {                                                                   \
        if (!(value)) {                                                    \
            std::printf("FAIL: %s line %d\n", #value, __LINE__);           \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static void check_platform_flags_match_appkit() {
    CHECK(hanabi::shortcuts::PlatformShift ==
          (unsigned long long)NSEventModifierFlagShift);
    CHECK(hanabi::shortcuts::PlatformControl ==
          (unsigned long long)NSEventModifierFlagControl);
    CHECK(hanabi::shortcuts::PlatformOption ==
          (unsigned long long)NSEventModifierFlagOption);
    CHECK(hanabi::shortcuts::PlatformCommand ==
          (unsigned long long)NSEventModifierFlagCommand);
    CHECK(hanabi::shortcuts::PlatformNumericPad ==
          (unsigned long long)NSEventModifierFlagNumericPad);
    CHECK(hanabi::shortcuts::PlatformFunction ==
          (unsigned long long)NSEventModifierFlagFunction);
}

static NSEvent* arrow_event(unsigned short keyCode, unichar ch,
                            NSEventModifierFlags extra) {
    NSString* chars = [NSString stringWithCharacters:&ch length:1];
    return [NSEvent keyEventWithType:NSEventTypeKeyDown
                            location:NSZeroPoint
                       modifierFlags:(NSEventModifierFlagFunction |
                                      NSEventModifierFlagNumericPad | extra)
                           timestamp:[NSProcessInfo processInfo].systemUptime
                        windowNumber:0
                             context:nil
                          characters:chars
         charactersIgnoringModifiers:chars
                           isARepeat:NO
                             keyCode:keyCode];
}

static void check_real_arrow_flags_are_not_a_chord() {
    struct Arrow {
        const char* name;
        unsigned short code;
        unichar ch;
    };
    const Arrow arrows[] = {
        {"up", 126, NSUpArrowFunctionKey},
        {"down", 125, NSDownArrowFunctionKey},
        {"left", 123, NSLeftArrowFunctionKey},
        {"right", 124, NSRightArrowFunctionKey},
    };
    for (const Arrow& a : arrows) {
        NSEvent* e = arrow_event(a.code, a.ch, 0);
        const unsigned long long flags = (unsigned long long)[e modifierFlags];
        CHECK((flags & NSEventModifierFlagFunction) != 0);
        CHECK((flags & NSEventModifierFlagNumericPad) != 0);
        const std::uint8_t mods =
            hanabi::shortcuts::modifiers_from_platform_flags(flags);
        if (mods != 0)
            std::printf("FAIL: real %s arrow read as chord mask %u\n", a.name,
                        mods);
        CHECK(mods == 0);
    }

    NSEvent* shifted =
        arrow_event(126, NSUpArrowFunctionKey, NSEventModifierFlagShift);
    CHECK(hanabi::shortcuts::modifiers_from_platform_flags(
              (unsigned long long)[shifted modifierFlags]) ==
          hanabi::shortcuts::ShiftModifier);

    NSEvent* cmdOpt = arrow_event(
        125, NSDownArrowFunctionKey,
        NSEventModifierFlagCommand | NSEventModifierFlagOption);
    CHECK(hanabi::shortcuts::modifiers_from_platform_flags(
              (unsigned long long)[cmdOpt modifierFlags]) ==
          (hanabi::shortcuts::CommandModifier |
           hanabi::shortcuts::OptionModifier));
}

static NSWindow* make_window() {
    NSWindow* w = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 1100, 760)
                  styleMask:NSWindowStyleMaskTitled
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [w setContentView:[[NSView alloc] initWithFrame:NSMakeRect(0, 0, 1100, 760)]];
    [w makeKeyAndOrderFront:nil];
    return w;
}

static std::string describe(const char* name) {
    char buf[512] = {};
    native_a11y_describe(name, buf, sizeof(buf));
    return std::string(buf);
}

static std::string parent_of(const char* name) {
    char buf[512] = {};
    native_a11y_parent_of(name, buf, sizeof(buf));
    return std::string(buf);
}

static std::string role_of(const char* name) {
    char buf[512] = {};
    native_a11y_role_of(name, buf, sizeof(buf));
    return std::string(buf);
}

static void check_bridge_publishes_semantics() {
    NSWindow* window = make_window();
    (void)window;

    NativeA11yNode nodes[4] = {};
    nodes[0].name = "CONVERSATION";
    nodes[0].role = "menu";
    nodes[0].parent = "";
    nodes[0].x = 120; nodes[0].y = 400; nodes[0].width = 184;
    nodes[0].height = 288;
    nodes[0].enabled = 1;

    nodes[1].name = "Open\u2026";
    nodes[1].role = "menuitem";
    nodes[1].parent = "CONVERSATION";
    nodes[1].x = 124; nodes[1].y = 432; nodes[1].width = 176;
    nodes[1].height = 36;
    nodes[1].enabled = 1;
    nodes[1].selected = 1;
    nodes[1].has_submenu = 1;
    nodes[1].expanded = 1;
    nodes[1].focused = 1;

    nodes[2].name = "Rename\u2026";
    nodes[2].role = "menuitem";
    nodes[2].parent = "CONVERSATION";
    nodes[2].x = 124; nodes[2].y = 468; nodes[2].width = 176;
    nodes[2].height = 36;
    nodes[2].enabled = 0;

    nodes[3].name = "Attach a file";
    nodes[3].role = "tooltip";
    nodes[3].parent = "composer_attach";
    nodes[3].x = 889; nodes[3].y = 696; nodes[3].width = 98;
    nodes[3].height = 24;
    nodes[3].enabled = 1;

    native_a11y_publish(nodes, 4);
    CHECK(native_a11y_published_count() == 4);

    CHECK(describe("CONVERSATION") == "CONVERSATION, menu");
    CHECK(parent_of("Open\u2026") == "CONVERSATION");
    CHECK(parent_of("Rename\u2026") == "CONVERSATION");
    CHECK(parent_of("CONVERSATION").empty());
    CHECK(native_a11y_child_count("CONVERSATION") == 2);
    CHECK(native_a11y_child_count("Rename\u2026") == 0);
    CHECK(role_of("CONVERSATION") == "AXMenu");
    CHECK(role_of("Open\u2026") == "AXMenuItem");
    CHECK(role_of("Attach a file") == "AXStaticText");
    CHECK(describe("Open\u2026") ==
          "Open\u2026, menuitem, selected, submenu expanded");
    CHECK(describe("Rename\u2026") == "Rename\u2026, menuitem, dimmed");
    CHECK(describe("Attach a file") == "Attach a file, tooltip");
    CHECK(describe("no such control").empty());

    NSView* view = nil;
    for (NSWindow* w in [NSApp windows])
        if ([w isVisible]) { view = [w contentView]; break; }
    CHECK(view != nil);
    if (view == nil) return;
    int menuItems = 0;
    bool sawRole = false;
    bool sawDisabled = false;
    bool sawSelected = false;
    bool sawFocused = false;
    bool sawParent = false;
    NSMutableArray* flat = [NSMutableArray array];
    NSMutableArray* queue =
        [NSMutableArray arrayWithArray:[view accessibilityChildren]];
    while ([queue count] > 0) {
        id node = [queue objectAtIndex:0];
        [queue removeObjectAtIndex:0];
        [flat addObject:node];
        NSArray* kids = [node accessibilityChildren];
        if (kids != nil) [queue addObjectsFromArray:kids];
    }
    CHECK([[view accessibilityChildren] count] == 2);

    for (id child in flat) {
        NSString* role = [child accessibilityRole];
        if ([role isEqualToString:NSAccessibilityMenuItemRole]) ++menuItems;
        if ([role isEqualToString:NSAccessibilityMenuRole]) sawRole = true;
        if ([[child accessibilityLabel] isEqualToString:@"Rename\u2026"]) {
            sawDisabled = ![child isAccessibilityEnabled];
            id realParent = [child accessibilityParent];
            sawParent =
                [realParent isKindOfClass:[NSAccessibilityElement class]] &&
                [[realParent accessibilityLabel]
                    isEqualToString:@"CONVERSATION"];
        }
        if ([[child accessibilityLabel] isEqualToString:@"Open\u2026"]) {
            sawSelected = [child isAccessibilitySelected];
            sawFocused = [child isAccessibilityFocused];
            CHECK([child isAccessibilityExpanded]);
        }
    }
    CHECK(menuItems == 2);
    CHECK(sawRole);
    CHECK(sawDisabled);
    CHECK(sawSelected);
    CHECK(sawFocused);
    CHECK(sawParent);

    // The frame is flipped into the view's own space, so a node at the top of
    // the UI is at the top for a screen reader too.
    for (id child in flat) {
        if (![[child accessibilityLabel] isEqualToString:@"CONVERSATION"])
            continue;
        const NSRect f = [child accessibilityFrameInParentSpace];
        CHECK(f.origin.x == 120);
        CHECK(f.origin.y == 760 - 400 - 288);
        CHECK(f.size.width == 184);
        CHECK(f.size.height == 288);
    }

    native_a11y_publish(nullptr, 0);
    CHECK(native_a11y_published_count() == 0);
    CHECK(describe("CONVERSATION").empty());
}

static void check_press_action_dispatch() {
    NativeA11yNode nodes[3] = {};
    nodes[0].name = "Archive";
    nodes[0].role = "menuitem";
    nodes[0].parent = "";
    nodes[0].entity = 4242;
    nodes[0].width = 176; nodes[0].height = 36;
    nodes[0].enabled = 1;

    nodes[1].name = "Rename\u2026";
    nodes[1].role = "menuitem";
    nodes[1].parent = "";
    nodes[1].entity = 777;
    nodes[1].width = 176; nodes[1].height = 36;
    nodes[1].enabled = 0;

    nodes[2].name = "CONVERSATION";
    nodes[2].role = "menu";
    nodes[2].parent = "";
    nodes[2].entity = 99;
    nodes[2].width = 184; nodes[2].height = 288;
    nodes[2].enabled = 1;

    native_a11y_publish(nodes, 3);
    CHECK(native_a11y_take_pressed() == 0);

    CHECK(native_a11y_perform_press("Archive") == 1);
    CHECK(native_a11y_take_pressed() == 4242);
    CHECK(native_a11y_take_pressed() == 0);

    CHECK(native_a11y_perform_press("Rename\u2026") == 0);
    CHECK(native_a11y_take_pressed() == 0);

    CHECK(native_a11y_perform_press("CONVERSATION") == 0);
    CHECK(native_a11y_take_pressed() == 0);

    CHECK(native_a11y_perform_press("no such control") == 0);
    CHECK(native_a11y_take_pressed() == 0);

    for (id child in [[[NSApp windows] firstObject] contentView]
             .accessibilityChildren) {
        if (![[child accessibilityLabel] isEqualToString:@"Archive"]) continue;
        CHECK([[child accessibilityActionNames]
            containsObject:NSAccessibilityPressAction]);
        CHECK([[child accessibilityActionDescription:NSAccessibilityPressAction]
            isEqualToString:@"press"]);
        [child accessibilityPerformAction:NSAccessibilityPressAction];
        CHECK(native_a11y_take_pressed() == 4242);
        CHECK([child accessibilityPerformPress]);
        CHECK(native_a11y_take_pressed() == 4242);
    }
    for (id child in [[[NSApp windows] firstObject] contentView]
             .accessibilityChildren) {
        if (![[child accessibilityLabel] isEqualToString:@"Rename\u2026"])
            continue;
        CHECK([[child accessibilityActionNames] count] == 0);
        CHECK(![child accessibilityPerformPress]);
        CHECK(native_a11y_take_pressed() == 0);
    }
}

int main() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        check_platform_flags_match_appkit();
        check_real_arrow_flags_are_not_a_chord();
        check_bridge_publishes_semantics();
        check_press_action_dispatch();
    }
    if (failures != 0) {
        std::printf("a11y bridge: %d FAILURES\n", failures);
        return 1;
    }
    std::printf("a11y bridge: ok\n");
    return 0;
}
