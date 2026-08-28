#pragma once

#include <limits>

#include "../keys.h"
#include "../menubar.h"
#include "../settings.h"
#include "components.h"
#include "ui_imports.h"

namespace ecs::commands {

inline void dispatch(hanabi::shortcuts::Command command, AppComponent& app,
                     LayoutComponent* layout) {
    using hanabi::shortcuts::Command;
    switch (command) {
        case Command::NewTask:
            app.composerOpen = !app.composerOpen;
            break;
        case Command::CloseTab:
            app.requestCloseActiveTab = true;
            break;
        case Command::ToggleSidebar:
            if (layout != nullptr) {
                layout->sidebarCollapsed = !layout->sidebarCollapsed;
                Settings::get().set_sidebar_collapsed(layout->sidebarCollapsed);
            }
            break;
        case Command::ToggleSplit:
            app.requestSplitToggle = true;
            break;
        case Command::OpenSettings:
            app.showSettings = !app.showSettings;
            break;
        case Command::OpenShortcuts:
            app.showShortcuts = !app.showShortcuts;
            if (!app.showShortcuts) app.shortcutRecording = -1;
            break;
        case Command::OpenPalette:
            app.paletteOpen = !app.paletteOpen;
            app.paletteQuery.clear();
            app.paletteIndex = 0;
            break;
        case Command::FindInThread:
            if (app.pane().openSession) {
                app.pane().findOpen = !app.pane().findOpen;
                if (!app.pane().findOpen) app.pane().findQuery.clear();
            }
            break;
        case Command::FindNext:
            if (app.pane().findOpen) ++app.requestFindStep;
            break;
        case Command::FindPrevious:
            if (app.pane().findOpen) --app.requestFindStep;
            break;
        case Command::SearchThreads:
            app.sessionSearchOpen = !app.sessionSearchOpen;
            app.sessionSearchQuery.clear();
            app.sessionSearchIndex = 0;
            break;
        case Command::Count:
            break;
    }
}

struct System : afterhours::System<> {
    bool should_iterate() const override { return false; }

    void once(float) override {
        auto* app = find_singleton<AppComponent>();
        auto* layout = find_singleton<LayoutComponent>();
        if (app == nullptr) return;

        menubar_set_shortcut_recording(app->shortcutRecording);
        const auto revision = Settings::get().shortcut_revision();
        if (revision != shortcutRevision_) {
            shortcutRevision_ = revision;
            menubar_refresh_shortcuts();
        }

        int nativeCommand = -1;
        if (menubar_take_command(&nativeCommand) && nativeCommand >= 0 &&
            nativeCommand < static_cast<int>(hanabi::shortcuts::Command::Count))
            dispatch(static_cast<hanabi::shortcuts::Command>(nativeCommand),
                     *app, layout);

        if (app->shortcutRecording >= 0) return;
        for (const auto& item : hanabi::shortcuts::kDefinitions) {
            if (hanabi::keys::shortcut_pressed(
                    Settings::get().get_shortcut(item.command))) {
                dispatch(item.command, *app, layout);
                break;
            }
        }
    }

   private:
    std::uint64_t shortcutRevision_ = std::numeric_limits<std::uint64_t>::max();
};

}  // namespace ecs::commands
