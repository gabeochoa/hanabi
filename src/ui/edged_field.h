#pragma once

// A text_input whose focused edge is a complete rectangle.
//
// text_input sets its own `Border::all(accent, 2px)` when focused, and since
// pin 9ff9079 that border renders with its TOP ROW missing -- measured on
// 18d_palette_empty_dark: 8 accent pixels on y=160 where 506 belong, with the
// bottom row and both verticals intact. Nothing hanabi draws at the field's
// own top edge survives, a foreground stroke included, so it is a clip rather
// than an overpaint. Ruled out by experiment: hanabi's own `.with_border()`,
// the theme ring (`focus_ring_offset`), the field's opaque fill, and a
// post-hoc `HasOnDraw::fg`. afterhours_gaps.md #375 has the measurements.
//
// A WRAPPER owns its own rect, so its border is not inside the field's clip.
// That is why the composer's edge has always been complete: it draws on an
// app-built wrap (`field_chrome::apply_focus_edge`). This puts every other
// field on that same footing -- the wrap carries the chrome the field used to
// carry, and the field fills it transparently.

#include <afterhours/src/plugins/ui/text_input/component.h>
#include "../ecs/ui_imports.h"
#include "field_chrome.h"
#include "secondary_surface.h"
#include "theme.h"

namespace hanabi::ui {

// `chrome` is the config the field used to be built with: it carries the
// size, background, border and radius, and is applied to the WRAP. The field
// inside fills it and paints nothing of its own.
template <typename Ctx>
inline afterhours::ui::imm::ElementResult edged_text_input(
    Ctx& ctx, afterhours::ui::imm::EntityParent ep, std::string& value,
    afterhours::ui::imm::ComponentConfig chrome, const std::string& name,
    float fontPx) {
    using namespace afterhours::ui;
    using afterhours::ui::imm::ComponentConfig;

    // `disabled` belongs to the FIELD, not the wrap: it is what stops the
    // widget from accepting keystrokes. Left on the wrap it is decorative, and
    // an overlay that owns the keyboard would let text leak into the field
    // behind it -- which tests/ui/an_overlay_owns_the_typing_too.e2e catches.
    const bool disabled = chrome.is_disabled();
    auto wrap = hanabi::ui::div(ctx, ep,
                                std::move(chrome.with_debug_name(name + "_wrap")));

    auto field = afterhours::ui::imm::text_input(
        ctx, afterhours::ui::imm::mk(wrap.ent(), 1), value,
        ComponentConfig{}
            .with_size(ComponentSize{percent(1.0f), percent(1.0f)})
            .with_transparent_bg()
            .with_custom_text_color(theme::text_primary())
            .with_font_size(pixels(fontPx))
            .with_alignment(TextAlignment::Left)
            .with_disabled(disabled)
            .with_debug_name(name));

    const bool focused =
        field.ent().template has<afterhours::text_input::HasTextInputState>() &&
        field.ent()
            .template get<afterhours::text_input::HasTextInputState>()
            .is_focused;
    // ONE border, in the library's own accent. The wrapper reproduces exactly
    // what text_input would have drawn -- `ctx.theme.accent`, not hanabi's
    // theme::accent(), which is a different blue -- and the field's own copy
    // is cleared, or the two sit adjacent as concentric rings.
    hanabi::ui::field_chrome::clear_focus_border(field.ent().id);
    hanabi::ui::field_chrome::apply_focus_edge(wrap.ent().id, focused,
                                               ctx.theme.accent);
    return field;
}

}  // namespace hanabi::ui
