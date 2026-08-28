#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>

#include <cstdio>

#include "../../src/menubar.h"

static int failures = 0;
#define CHECK(value)                                                        \
    do {                                                                    \
        if (!(value)) {                                                     \
            std::printf("FAIL: %s line %d\n", #value, __LINE__);           \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

int main() {
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
    CHECK(!menubar_take_command(&command));
    menubar_simulate_command(6);
    CHECK(menubar_take_command(&command));
    CHECK(command == 6);
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
