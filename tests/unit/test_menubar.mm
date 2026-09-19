#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>

#include <cstdio>

#include "../../src/menubar.h"
#include "../../src/palette_rows.h"
#include "../../src/shortcuts.h"

static int failures = 0;
#define CHECK(value)                                                        \
    do {                                                                    \
        if (!(value)) {                                                     \
            std::printf("FAIL: %s line %d\n", #value, __LINE__);           \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

static int g_hostKey = 0;
extern "C" int hanabi_native_tab_host_is_key(void) { return g_hostKey; }

static void test_command_enabled_table() {
    const int keptTab = static_cast<int>(hanabi::shortcuts::Command::CloseKeptTab);
    const int closeTab = static_cast<int>(hanabi::shortcuts::Command::CloseTab);
    CHECK(menubar_command_enabled(closeTab));
    CHECK(menubar_command_enabled(keptTab));
    menubar_set_command_enabled(keptTab, false);
    CHECK(!menubar_command_enabled(keptTab));
    CHECK(menubar_command_enabled(closeTab));
    menubar_set_command_enabled(keptTab, true);
    CHECK(menubar_command_enabled(keptTab));
    menubar_set_command_enabled(-1, false);
    menubar_set_command_enabled(static_cast<int>(hanabi::shortcuts::Command::Count), false);
    CHECK(!menubar_command_enabled(-1));
    CHECK(!menubar_command_enabled(static_cast<int>(hanabi::shortcuts::Command::Count)));
    g_hostKey = 0;
    CHECK(hanabi_native_tab_host_is_key() == 0);
    g_hostKey = 1;
    CHECK(hanabi_native_tab_host_is_key() == 1);
    const auto& kept = hanabi::shortcuts::definition(hanabi::shortcuts::Command::CloseKeptTab);
    const auto& open = hanabi::shortcuts::definition(hanabi::shortcuts::Command::OpenShortcuts);
    CHECK(kept.in_palette && open.in_palette);
    menubar_set_command_enabled(keptTab, false);
    CHECK(!hanabi::palette_rows::lists(kept) && hanabi::palette_rows::lists(open));
    menubar_set_command_enabled(keptTab, true);
    CHECK(hanabi::palette_rows::lists(kept));
}

int main() {
    test_command_enabled_table();
    struct Expected {
        const char* selector;
        unsigned short keyCode;
        bool shift;
    };
    const Expected expected[] = {
        {"undo:", kVK_ANSI_Z, false},
        {"redo:", kVK_ANSI_Z, true},
        {"cut:", kVK_ANSI_X, false},
        {"copy:", kVK_ANSI_C, false},
        {"paste:", kVK_ANSI_V, false},
        {"selectAll:", kVK_ANSI_A, false},
    };
    for (const auto& item : expected) {
        unsigned short keyCode = 0;
        unsigned long long modifiers = 0;
        CHECK(menubar_edit_binding(item.selector, &keyCode, &modifiers));
        CHECK(keyCode == item.keyCode);
        CHECK((modifiers & NSEventModifierFlagCommand) != 0);
        CHECK(((modifiers & NSEventModifierFlagShift) != 0) == item.shift);
    }
    int command = -1;
    CHECK(!menubar_command_pending());
    CHECK(!menubar_take_command(&command));
    menubar_simulate_command(6);
    CHECK(menubar_command_pending());
    CHECK(menubar_take_command(&command));
    CHECK(command == 6);
    CHECK(!menubar_command_pending());
    CHECK(!menubar_take_command(&command));
    menubar_simulate_command(-1);
    CHECK(!menubar_take_command(&command));

    unsigned short keyCode = 0;
    unsigned long long modifiers = 0;
    CHECK(!menubar_edit_binding("deleteBackward:", &keyCode, &modifiers));
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
