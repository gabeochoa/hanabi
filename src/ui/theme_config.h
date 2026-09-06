#pragma once

// ---------------------------------------------------------------------------
// App-level afterhours theme configuration.
//
// afterhours' ThemeDefaults holds TWO Themes (upstream b6af466 "Scope a theme
// to the frame that set it"):
//
//   app_default -- what the app configured at startup. Persists.
//   theme       -- what is in effect THIS FRAME. The UI systems call
//                  ThemeDefaults::begin_frame() once per frame, which does
//                  `theme = app_default`.
//
// Everything that resolves a colour or a layout metric reads `theme`, so a
// write to `theme` alone is a per-FRAME override: it survives until the first
// begin_frame and then silently vanishes. That is the point of the split -- a
// screen can set its own theme without leaking into the next one -- and it is
// a trap for app configuration, which has to outlive the frame. Hanabi hit it
// on the pin bump: font_sizing set in preload was gone by frame one, so every
// FontSize::Large label fell back to the library's own tier and grew.
//
// afterhours' own setters (set_theme, set_theme_color, set_click_activation_
// mode, set_highlight_mode) already write both slots. The fields hanabi
// configures -- font_sizing, focus ring, roundness, arrows_tab, ui_scale --
// have no such setter, so they call this after writing `theme`.
//
// scripts/check_theme_config.py enforces the pairing.
// ---------------------------------------------------------------------------

#include <afterhours/src/plugins/ui/theme.h>

namespace hanabi::ui {

// Promote the configured frame theme to the app default, so it survives
// ThemeDefaults::begin_frame().
inline void publish_app_theme() {
    auto& defaults = afterhours::ui::imm::ThemeDefaults::get();
    defaults.app_default = defaults.theme;
}

}  // namespace hanabi::ui
