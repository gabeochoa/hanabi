#pragma once

// Composer (Phase K). A "New task" affordance (Cmd+N / title-bar +) opens a
// compose row (AppComponent::composerOpen / composerDraft) to kick off a new
// thread via the client. Read-only browse otherwise; reply-inline deferred.
// STUB — to be implemented in Phase K. Owns this file only.

#include "ui_imports.h"

namespace ecs {

struct ComposerSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* app = find_singleton<AppComponent>();
        if (!app || !app->composerOpen) return;
        // TODO(Phase K): render the composer + kickoff.
        (void)ctx;
    }
};

}  // namespace ecs
