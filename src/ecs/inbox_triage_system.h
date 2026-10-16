#pragma once

#include <string>

#include "../keys.h"
#include "../settings.h"
#include "../ui/inbox_triage.h"
#include "components.h"
#include "keyboard_focus.h"
#include "ui_imports.h"

namespace ecs {

struct InboxTriageSystem : afterhours::System<UIContext<InputAction>> {
    std::string pendingPrefix_;
    int pendingFrames_ = 0;
    static constexpr int kPrefixFrames = 90;

    static std::string key_name_pressed() {
        namespace ah = afterhours::keys;
        for (int k = ah::A; k <= ah::Z; ++k)
            if (hanabi::keys::pressed(k)) return std::string(1, static_cast<char>('a' + (k - ah::A)));
        if (hanabi::keys::pressed(hanabi::keys::kSlash)) return "/";
        return {};
    }

    void for_each_with(Entity&, UIContext<InputAction>&, float) override {
        auto* app = find_singleton<AppComponent>();
        if (!app) return;
        if (!Settings::get().get_inbox_triage()) {
            pendingPrefix_.clear();
            return;
        }
        if (!pendingPrefix_.empty() && ++pendingFrames_ > kPrefixFrames) pendingPrefix_.clear();
        if (app->arrow != ArrowIntent::List) {
            pendingPrefix_.clear();
            return;
        }
        if (any_text_field_focused() || hanabi::keys::cmd_or_ctrl_down() ||
            hanabi::keys::option_down() || app->rowMenuOpen || app->messageMenuOpen ||
            app->paletteOpen || app->sessionSearchOpen || app->showSettings ||
            app->showShortcuts || app->renameOpen)
            return;
        if (app->view == SmartView::Chat) return;
        const std::string key = key_name_pressed();
        if (key.empty()) return;
        const auto binding_of = [](hanabi::triage::Command c) {
            return hanabi::triage::parse_binding(
                Settings::get().get_triage_binding(std::string(hanabi::triage::definition(c).key)));
        };
        const auto r = hanabi::triage::resolve(key, pendingPrefix_, binding_of);
        pendingPrefix_.clear();
        if (r.pending_prefix) {
            pendingPrefix_ = key;
            pendingFrames_ = 0;
            return;
        }
        if (!r.matched()) return;
        using hanabi::triage::Command;
        switch (r.command) {
            case Command::Next:
                app->arrow = ArrowIntent::List;
                app->arrowDelta = 1;
                break;
            case Command::Previous:
                app->arrow = ArrowIntent::List;
                app->arrowDelta = -1;
                break;
            case Command::Open:
                if (!app->listCursorId.empty()) app->requestOpenTab = app->listCursorId;
                break;
            case Command::Archive:
                if (!app->listCursorId.empty()) app->requestToggleArchive = app->listCursorId;
                break;
            case Command::Pin:
                if (!app->listCursorId.empty()) app->requestToggleStar = app->listCursorId;
                break;
            case Command::Search:
                app->sessionSearchOpen = true;
                app->sessionSearchQuery.clear();
                app->sessionSearchIndex = 0;
                break;
            case Command::GoHome:
                app->view = SmartView::Home;
                app->savedViewId.clear();
                Settings::get().set_selected_view("");
                break;
            case Command::GoPinned:
                app->view = SmartView::Starred;
                app->savedViewId.clear();
                Settings::get().set_selected_view("");
                break;
            case Command::GoArchived:
                app->view = SmartView::Archived;
                app->savedViewId.clear();
                Settings::get().set_selected_view("");
                break;
            case Command::Count:
                break;
        }
    }
};

}
