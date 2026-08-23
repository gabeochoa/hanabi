#pragma once

// ---------------------------------------------------------------------------
// One owner for Up/Down.
//
// The same lesson as Esc (escape_system.h), one key later. afterhours' input
// is multi-reader — a press stays readable by everyone for the whole frame —
// so two sites that both poll Up both fire on one keystroke. Up already meant
// two things: walk the composer's history, and scroll the transcript. They
// stayed apart only because each one hand-checked the other's condition (the
// scroller skips itself while a text field is focused; the walk requires
// focus), and that arrangement holds exactly until a third reader is added
// without knowing about the first two.
//
// So the key is read once, here, and resolved into a single intent by what
// owns the keyboard: a focused text field first (the caret and the history
// walk live there), then an overlay (nothing behind a sheet may move while it
// is up), then the transcript when one is open, and a list otherwise.
//
// Registered ahead of every consumer, so the intent is already resolved by the
// time a UI system reads it.
// ---------------------------------------------------------------------------

#include "../keys.h"
#include "components.h"
#include "keyboard_focus.h"
#include "ui_imports.h"

namespace ecs {

struct ArrowSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>&, float) override {
        auto* app = find_singleton<AppComponent>();
        if (!app) return;

        app->arrow = ArrowIntent::None;
        app->arrowDelta = 0;

        const bool up = hanabi::keys::pressed(hanabi::keys::kUp);
        const bool down = hanabi::keys::pressed(hanabi::keys::kDown);
        // Both at once is a keyboard, not an intent.
        if (up == down) return;
        app->arrowDelta = down ? 1 : -1;

        // The palette's own field is focused while it is up, but Up/Down there
        // mean "move the selection", so it outranks the text field.
        if (app->paletteOpen)
            app->arrow = ArrowIntent::Palette;
        else if (app->sessionSearchOpen)
            app->arrow = ArrowIntent::SessionSearch;
        else if (any_text_field_focused())
            app->arrow = ArrowIntent::TextField;
        else if (app->renameOpen || app->composerOpen || app->showShortcuts ||
                 app->showSettings || app->showAuth)
            app->arrow = ArrowIntent::None;
        else if (app->view == SmartView::Chat && app->openSession)
            app->arrow = ArrowIntent::Transcript;
        else
            app->arrow = ArrowIntent::List;
    }
};

}  // namespace ecs
