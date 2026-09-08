#pragma once

// The frame around ecs/focus_routing.h's rules: read who owns the keyboard,
// serve the pane a click left pending, and park the characters typed at a
// transcript so the composer built later THIS frame adopts them. Parking is
// what keeps the first letter: the field does not have the caret yet on the
// frame the first character arrives.

#include <algorithm>
#include <string>

#include "../keys.h"
#include "components.h"
#include "focus_routing.h"
#include "keyboard_focus.h"
#include "ui_imports.h"

namespace ecs {

struct FocusRoutingSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>&, float) override {
        auto* app = find_singleton<AppComponent>();
        if (app == nullptr) return;
        auto* strip = find_singleton<TabStripComponent>();

        model::FocusRouting in;
        in.typingLive =
            composer_typing_live(*app, strip != nullptr && strip->menuOpen);
        in.textFieldFocused = any_text_field_focused();
        in.chatView = app->view == SmartView::Chat;
        in.modifierHeld =
            hanabi::keys::cmd_or_ctrl_down() || hanabi::keys::option_down();

        const int pending = app->paneFocusPending;
        app->paneFocusPending = -1;
        if (pending >= 0 && pending == std::clamp(app->focusedPane, 0, 1) &&
            model::pane_click_takes_caret(in))
            app->request_composer_focus();

        if (!model::composer_takes_typing(in)) return;

        model::TypedRun run;
        for (int c = afterhours::input::get_char_pressed(); c > 0;
             c = afterhours::input::get_char_pressed())
            run.offer(c);
        if (run.text.empty()) return;

        app->typedSeed += run.text;
        app->request_composer_focus();
    }
};

struct KeyboardClaimSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* app = find_singleton<AppComponent>();
        if (app == nullptr) return;
        auto* strip = find_singleton<TabStripComponent>();
        app->keyboardClaim.observe(
            keyboard_surfaces_up(*app, strip != nullptr && strip->menuOpen),
            caret_in_composer(ctx.focus_id),
            ctx.focus_source == afterhours::ui::FocusSource::Explicit);
    }
};

}  // namespace ecs
