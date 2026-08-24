#pragma once

// ---------------------------------------------------------------------------
// The LOGICAL viewport — the size hanabi must do its own layout arithmetic in.
//
// hanabi runs afterhours in Adaptive scaling mode (main.cpp), where every
// `pixels()` value in the tree — sizes, paddings, absolute translate, and
// explicit font sizes — is multiplied by `theme.ui_scale` before it reaches
// the renderer. So a rectangle hanabi computes for itself from the DEVICE
// resolution and then hands back as `pixels(...)` is scaled a SECOND time:
// at ui_scale 2.0 the composer's y doubles and the strip leaves the window,
// the sidebar's full-height column becomes twice the window tall and takes
// its footer off the bottom edge, and the transcript is laid out in a pane
// twice as wide as the one it is drawn into.
//
// afterhours already draws this distinction — `LayoutInfo::make` divides the
// screen size by ui_scale for exactly this reason (layout_types.h) — but it
// only exposes it to code that asks for a LayoutInfo. `graphics::get_screen_*`
// is, and has to be, the real framebuffer.
//
// So: every place hanabi measures the window in order to size or position a
// widget reads these, not `graphics::get_screen_*`. At the default ui_scale of
// 1.0 the divisor is exactly 1.0f and the result is bit-identical to the raw
// call, so this is a hard no-op for every windowed run and the whole scripted
// UI suite.
// ---------------------------------------------------------------------------

#include <afterhours/src/plugins/ui/theme.h>

#include "../rl.h"

namespace hanabi::viewport {

// The active UI scale, guarded against a zero/garbage theme value.
inline float scale() {
    const float s = afterhours::ui::imm::ThemeDefaults::get().theme.ui_scale;
    return s > 0.0f ? s : 1.0f;
}

// Window width in logical pixels — the units `pixels()` speaks in.
inline float width() {
    return static_cast<float>(afterhours::graphics::get_screen_width()) /
           scale();
}

// Window height in logical pixels.
inline float height() {
    return static_cast<float>(afterhours::graphics::get_screen_height()) /
           scale();
}

// A hand-computed pixel length, in DEVICE pixels.
//
// The counterpart of width()/height(): those convert device -> logical for
// code that feeds `pixels()` back into the layout, this one converts logical
// -> device for code that draws primitives ITSELF. `on_draw_fg` and the
// immediate-mode glyph helpers are handed a widget rect that afterhours has
// already scaled, but every radius, stroke width and sprite size inside them
// is a literal that afterhours never sees — so at ui_scale 2 the row marks,
// the nav icons and the pin stay 1x inside a 2x frame. Wrap the literal.
//
// Exactly v at the default ui_scale of 1.0 (a multiply by 1.0f), so this is a
// hard no-op for every windowed run and the whole scripted UI suite.
inline float px(float v) { return v * scale(); }

}  // namespace hanabi::viewport
