#pragma once

// ---------------------------------------------------------------------------
// One owner for Esc.
//
// Esc was polled in four places — the find bar, the transcript selection, the
// composer field, and each of the three overlays. afterhours' injector is
// multi-reader (a press stays readable for the whole frame), so they ALL fired
// on one keystroke: dismissing the settings sheet also dropped your selection
// and wiped the draft you had typed.
//
// So the key is read exactly once, here, and resolved into a single intent by
// what is on top: the rename modal draws over the modal composer, which draws
// over the shortcuts sheet, which draws over settings, which draws over the
// find bar, which sits over the composer's own menus (slash commands, the
// model picker, the effort picker), which sit over the transcript. The auth
// overlay is deliberately absent — login gates the app and Esc must not
// dismiss it.
//
// Registered ahead of every consumer, so the intent is already resolved by the
// time a UI system reads it.
// ---------------------------------------------------------------------------

#include "../keys.h"
#include "components.h"
#include "ui_imports.h"

namespace ecs {

struct EscapeSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>&, float) override {
        auto* app = find_singleton<AppComponent>();
        if (!app) return;

        app->escape = EscapeIntent::None;
        if (!hanabi::keys::pressed(hanabi::keys::kEscape)) return;

        if (app->renameOpen && !app->renamePending)
            app->escape = EscapeIntent::CloseRename;
        else if (app->composerOpen)
            app->escape = EscapeIntent::CloseComposer;
        else if (app->showShortcuts)
            app->escape = EscapeIntent::CloseShortcuts;
        else if (app->showSettings)
            app->escape = EscapeIntent::CloseSettings;
        else if (app->findOpen)
            app->escape = EscapeIntent::CloseFind;
        else if (app->slashMenuOpen)
            app->escape = EscapeIntent::CloseSlashMenu;
        else if (app->modelPopoverOpen)
            app->escape = EscapeIntent::CloseModelPicker;
        else if (app->effortPopoverOpen)
            app->escape = EscapeIntent::CloseEffortPicker;
        else
            app->escape = EscapeIntent::ClearTranscript;
    }
};

}  // namespace ecs
