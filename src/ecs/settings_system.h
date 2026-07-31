#pragma once

// Settings overlay (Phase K). Renders a settings sheet when
// AppComponent::showSettings is true: theme light/dark/system toggle wired to
// Settings::set_theme + theme::set_mode (live), closes on gear/Esc/outside.
// STUB — to be implemented in Phase K. Owns this file only.

#include "ui_imports.h"

namespace ecs {

struct SettingsSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* app = find_singleton<AppComponent>();
        if (!app || !app->showSettings) return;
        // TODO(Phase K): render the settings overlay + theme toggle.
        (void)ctx;
    }
};

}  // namespace ecs
