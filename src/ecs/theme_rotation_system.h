#pragma once

// ---------------------------------------------------------------------------
// Theme rotation — flip the palette on a timer.
//
// The Light/Dark/System picker (settings_system.h, render_theme_row) already
// ships; what did not exist is the "and keep moving" half of it: a mode that
// cycles the theme by itself so the app follows the day without anyone opening
// the sheet or the OS setting.
//
// The clock is SIMULATED time — the dt the system manager is run with — not
// wall time. In the app that is the real frame time, and in the headless
// harness it is a fixed 1/60, so a test can advance the rotation
// deterministically in a couple of hundred frames instead of waiting minutes.
//
// A rotation deliberately does NOT persist the palette it lands on: at a
// fifteen-minute interval that would rewrite settings.json all day, and a
// relaunch should come up in the theme the USER chose rather than wherever the
// cycle happened to stop. The interval itself is what persists.
// ---------------------------------------------------------------------------

#include "../settings.h"
#include "../ui/theme.h"
#include "components.h"

namespace ecs {

namespace theme_rotation {

// Seconds of simulated time since the last flip. Lives here rather than inside
// the system so the settings sheet can restart it: picking Dark by hand should
// give you Dark for a whole interval, not for whatever was left on the clock.
inline float elapsed = 0.0f;

inline void restart() { elapsed = 0.0f; }

}  // namespace theme_rotation

struct ThemeRotationSystem : afterhours::System<AppComponent> {
    void for_each_with(Entity&, AppComponent&, float dt) override {
        const int secs = Settings::get().get_theme_rotate_secs();
        if (secs <= 0) {
            // Turning rotation off leaves the current palette up and puts the
            // clock back to zero, so turning it on again gives a full interval.
            theme_rotation::restart();
            return;
        }
        theme_rotation::elapsed += dt;
        if (theme_rotation::elapsed < static_cast<float>(secs)) return;
        theme_rotation::restart();
        theme::toggle_mode();
    }
};

}  // namespace ecs
