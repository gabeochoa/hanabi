#pragma once

#include "../vendor/afterhours/src/plugins/ui.h"
#include "../vendor/afterhours/src/plugins/window_manager.h"
#include "../vendor/afterhours/src/plugins/toast.h"
#include "../vendor/afterhours/src/plugins/modal.h"
#include "rl.h"
#include "input_mapping.h"

namespace ui_imm {

using InputAction = ::InputAction;
using UIContextType = afterhours::ui::UIContext<InputAction>;

// Initialize the UI context with screen dimensions and dark theme
inline void initUIContext(int screenWidth, int screenHeight) {
    using namespace afterhours;

    auto* resProv = EntityHelper::get_singleton_cmp<
        window_manager::ProvidesCurrentResolution>();
    if (resProv) {
        resProv->current_resolution = {screenWidth, screenHeight};
    }
}

// Get the root entity for parenting UI elements
inline afterhours::Entity& getUIRootEntity() {
    auto roots = afterhours::EntityQuery({.force_merge = true})
                     .whereHasComponent<afterhours::ui::AutoLayoutRoot>()
                     .gen();
    if (roots.empty()) {
        throw std::runtime_error("No UI root found");
    }
    return roots[0].get();
}

inline void registerUIPreLayoutSystems(
    afterhours::SystemManager& manager) {
    afterhours::ui::register_before_ui_updates<InputAction>(manager);
}

inline void registerUIPostLayoutSystems(
    afterhours::SystemManager& manager) {
    afterhours::ui::register_after_ui_updates<InputAction>(manager);
}

inline void registerUIRenderSystems(
    afterhours::SystemManager& manager) {
    afterhours::ui::register_render_systems<InputAction>(manager);
}

inline void registerToastSystems(
    afterhours::SystemManager& manager) {
    afterhours::toast::enforce_singletons(manager);
    afterhours::toast::register_update_systems(manager);
    afterhours::toast::register_layout_systems<InputAction>(manager);
}

inline void registerModalSystems(
    afterhours::SystemManager& manager) {
    afterhours::modal::enforce_singletons(manager);
    afterhours::modal::register_update_systems<InputAction>(manager);
}

inline void registerModalRenderSystems(
    afterhours::SystemManager& manager) {
    afterhours::modal::register_render_systems<InputAction>(manager);
}

}  // namespace ui_imm
