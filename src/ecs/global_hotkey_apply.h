#pragma once

// Push the desktop-global chords Settings holds at the native registration.
// Lives here rather than in settings.cpp so the model layer keeps no native
// dependency.

#include "../global_hotkeys.h"
#include "../native_extras.h"
#include "../settings.h"

namespace ecs {

// False means a chord was refused by the system.
[[nodiscard]] inline bool apply_global_hotkeys_from_settings() {
    const auto reqs = Settings::get().get_global_requests();
    const auto& nt =
        reqs[hanabi::globals::index(hanabi::globals::Slot::NewTask)];
    const auto& pl =
        reqs[hanabi::globals::index(hanabi::globals::Slot::Palette)];
    return native_set_global_hotkeys(
        GlobalHotkeyRequest{nt.shortcut.key, nt.shortcut.modifiers,
                            nt.enabled},
        GlobalHotkeyRequest{pl.shortcut.key, pl.shortcut.modifiers,
                            pl.enabled});
}

}  // namespace ecs
