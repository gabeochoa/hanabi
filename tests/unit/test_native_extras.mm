#include <branding.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "../../src/native_extras.h"
#include "../../src/global_hotkeys.h"

static int failures = 0;
#define CHECK(value)                                                        \
    do {                                                                    \
        if (!(value)) {                                                     \
            std::printf("FAIL: %s line %d\n", #value, __LINE__);           \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

static GlobalHotkeyRequest chord_for(hanabi::globals::Slot slot, bool on) {
    const auto shortcut = hanabi::globals::definition(slot).shortcut;
    return GlobalHotkeyRequest{shortcut.key, shortcut.modifiers, on};
}

static constexpr unsigned kNewTaskBit = 1u;
static constexpr unsigned kPaletteBit = 2u;

static void test_re_enabling_one_chord_after_both_were_off_registers_it() {
    native_hotkey_test_begin();
    native_hotkey_test_set_active(true);
    CHECK(native_hotkey_test_registered_mask() == (kNewTaskBit | kPaletteBit));

    const auto newTaskOff = chord_for(hanabi::globals::Slot::NewTask, false);
    const auto paletteOff = chord_for(hanabi::globals::Slot::Palette, false);
    CHECK(native_set_global_hotkeys(newTaskOff, paletteOff));
    CHECK(native_hotkey_test_registered_mask() == 0u);

    const auto newTaskOn = chord_for(hanabi::globals::Slot::NewTask, true);
    CHECK(native_set_global_hotkeys(newTaskOn, paletteOff));
    CHECK(native_hotkey_test_registered_mask() == kNewTaskBit);

    const auto paletteOn = chord_for(hanabi::globals::Slot::Palette, true);
    CHECK(native_set_global_hotkeys(newTaskOn, paletteOn));
    CHECK(native_hotkey_test_registered_mask() == (kNewTaskBit | kPaletteBit));
}

static void test_a_reset_after_a_disable_brings_both_chords_back() {
    native_hotkey_test_begin();
    native_hotkey_test_set_active(true);
    CHECK(native_set_global_hotkeys(
        chord_for(hanabi::globals::Slot::NewTask, false),
        chord_for(hanabi::globals::Slot::Palette, false)));
    CHECK(native_hotkey_test_registered_mask() == 0u);

    const auto restored = hanabi::globals::defaults();
    const auto& nt =
        restored[hanabi::globals::index(hanabi::globals::Slot::NewTask)];
    const auto& pl =
        restored[hanabi::globals::index(hanabi::globals::Slot::Palette)];
    CHECK(nt.enabled);
    CHECK(pl.enabled);
    CHECK(native_set_global_hotkeys(
        GlobalHotkeyRequest{nt.shortcut.key, nt.shortcut.modifiers,
                            nt.enabled},
        GlobalHotkeyRequest{pl.shortcut.key, pl.shortcut.modifiers,
                            pl.enabled}));
    CHECK(native_hotkey_test_registered_mask() == (kNewTaskBit | kPaletteBit));
}

static void test_a_switched_off_slot_keeps_its_chord_and_stays_unregistered() {
    native_hotkey_test_begin();
    native_hotkey_test_set_active(true);
    const auto off = chord_for(hanabi::globals::Slot::NewTask, false);
    CHECK(off.key == hanabi::globals::definition(hanabi::globals::Slot::NewTask)
                         .shortcut.key);
    CHECK(off.modifiers != 0);
    CHECK(native_set_global_hotkeys(
        off, chord_for(hanabi::globals::Slot::Palette, true)));
    CHECK(native_hotkey_test_registered_mask() == kPaletteBit);
}

static void test_a_background_app_registers_nothing_and_catches_up_on_focus() {
    native_hotkey_test_begin();
    CHECK(native_hotkey_test_registered_mask() == 0u);
    CHECK(native_set_global_hotkeys(
        chord_for(hanabi::globals::Slot::NewTask, true),
        chord_for(hanabi::globals::Slot::Palette, true)));
    CHECK(native_hotkey_test_registered_mask() == 0u);

    native_hotkey_test_set_active(true);
    CHECK(native_hotkey_test_registered_mask() == (kNewTaskBit | kPaletteBit));
    native_hotkey_test_set_active(false);
    CHECK(native_hotkey_test_registered_mask() == 0u);
}

int main() {
    char thread[128] = {};
    CHECK(!native_take_open_thread(thread, sizeof(thread)));
    native_simulate_notification_click("thread/from-notification");
    CHECK(native_take_open_thread(thread, sizeof(thread)));
    CHECK(std::strcmp(thread, "thread/from-notification") == 0);
    CHECK(!native_take_open_thread(thread, sizeof(thread)));
    const std::string url = std::string(product_branding::kUrlScheme) +
                            "://thread/space%2Fid%20one?source=spotlight";
    native_simulate_open_url(url.c_str());
    CHECK(native_take_open_thread(thread, sizeof(thread)));
    CHECK(std::strcmp(thread, "space/id one") == 0);
    native_simulate_open_url("https://example.invalid/thread/nope");
    CHECK(!native_take_open_thread(thread, sizeof(thread)));

    char dropped[256] = {};
    CHECK(!native_dropped_image_pending());
    native_simulate_file_drop("/tmp/idle-wake.png");
    CHECK(native_dropped_image_pending());
    CHECK(native_take_dropped_image(dropped, sizeof(dropped)));
    CHECK(std::strcmp(dropped, "/tmp/idle-wake.png") == 0);
    CHECK(!native_dropped_image_pending());

    char status[512] = {};
    native_integration_status(status, sizeof(status));
    CHECK(std::strstr(status, "bundle=") != nullptr);
    CHECK(std::strstr(status, "notifications=") != nullptr);
    CHECK(std::strstr(status, "spotlight=") != nullptr);

    native_notify("headless", "must not post", "thread", true);
    native_spotlight_sync(nullptr, 0);
    native_integration_status(status, sizeof(status));
    CHECK(std::strstr(status, "notifications=non-bundled") != nullptr);
    CHECK(std::strstr(status, "spotlight=non-bundled") != nullptr);

    const int face_count = native_font_faces(nullptr, 0);
    CHECK(face_count > 0);
    std::vector<NativeFontFace> faces(static_cast<std::size_t>(face_count));
    CHECK(native_font_faces(faces.data(), face_count) == face_count);
    std::set<std::string> keys;
    bool system_regular = false;
    bool system_bold = false;
    for (const auto& face : faces) {
        CHECK(face.family[0] != '\0');
        CHECK(face.weight[0] != '\0');
        CHECK(face.path[0] == '/');
        CHECK(face.point_scale > 1.0f && face.point_scale < 2.0f);
        CHECK(std::filesystem::is_regular_file(face.path));
        CHECK(keys.insert(std::string(face.family) + "/" + face.weight).second);
        if (std::strcmp(face.family, "system") == 0 &&
            std::strcmp(face.weight, "regular") == 0)
            system_regular = true;
        if (std::strcmp(face.family, "system") == 0 &&
            std::strcmp(face.weight, "bold") == 0)
            system_bold = true;
    }
    CHECK(system_regular);
    CHECK(system_bold);

    test_re_enabling_one_chord_after_both_were_off_registers_it();
    test_a_reset_after_a_disable_brings_both_chords_back();
    test_a_switched_off_slot_keeps_its_chord_and_stays_unregistered();
    test_a_background_app_registers_nothing_and_catches_up_on_focus();

    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
