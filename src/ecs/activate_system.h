#pragma once

#include "../keys.h"
#include "../ui/activation.h"
#include "components.h"
#include "ui_imports.h"

namespace ecs {

inline hanabi::activate::Surfaces activate_surfaces(const AppComponent& app,
                                                    bool tabMenuOpen) {
    hanabi::activate::Surfaces out;
    out.palette = app.paletteOpen;
    out.sessionSearch = app.sessionSearchOpen;
    out.contextMenu = app.rowMenuOpen || tabMenuOpen;
    out.rename = app.renameOpen;
    out.recordingShortcut = app.shortcutRecording >= 0;
    out.shortcuts = app.showShortcuts;
    out.settings = app.showSettings;
    out.auth = app.showAuth;
    out.find = app.any_find_open();
    out.slashMenu = app.slashMenuOpen;
    out.picker = app.modelPopoverOpen || app.effortPopoverOpen ||
                 app.planPopoverOpen || app.foldPopoverOpen;
    out.askFocused = app.askFocused;
    return out;
}

struct ActivateSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>&, float) override {
        auto* app = find_singleton<AppComponent>();
        if (!app) return;

        app->activate = ActivateIntent::None;
        app->menuLateral = 0;
        const bool left = hanabi::keys::pressed(hanabi::keys::kLeft);
        const bool right = hanabi::keys::pressed(hanabi::keys::kRight);
        if (left != right) {
            const auto* lateralStrip = find_singleton<TabStripComponent>();
            if (app->rowMenuOpen ||
                (lateralStrip != nullptr && lateralStrip->menuOpen))
                app->menuLateral = right ? 1 : -1;
        }
        if (!hanabi::keys::pressed(hanabi::keys::kEnter)) return;

        const auto* strip = find_singleton<TabStripComponent>();
        const auto surfaces =
            activate_surfaces(*app, strip != nullptr && strip->menuOpen);
        switch (hanabi::activate::resolve(surfaces)) {
            case hanabi::activate::Intent::None:
                app->activate = ActivateIntent::None;
                break;
            case hanabi::activate::Intent::Palette:
                app->activate = ActivateIntent::Palette;
                break;
            case hanabi::activate::Intent::SessionSearch:
                app->activate = ActivateIntent::SessionSearch;
                break;
            case hanabi::activate::Intent::Settings:
                app->activate = ActivateIntent::Settings;
                break;
            case hanabi::activate::Intent::Ask:
                app->activate = ActivateIntent::Ask;
                break;
            case hanabi::activate::Intent::ListCursor:
                app->activate = ActivateIntent::ListCursor;
                break;
            case hanabi::activate::Intent::ContextMenu:
                app->activate = ActivateIntent::ContextMenu;
                break;
        }
    }
};

}  // namespace ecs
