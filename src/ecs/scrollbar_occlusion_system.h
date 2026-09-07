#pragma once

#include <vector>

#include "../ui/overlay_lifecycle.h"
#include "components.h"
#include "ui_imports.h"

namespace ecs {

inline std::vector<afterhours::EntityID>& scrollbars_suppressed() {
    static std::vector<afterhours::EntityID> value;
    return value;
}

struct ScrollbarOcclusionResetSystem
    : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>&, float) override {
        for (afterhours::EntityID id : scrollbars_suppressed()) {
            auto opt = afterhours::ui::UICollectionHolder::getEntityForID(id);
            if (opt.valid() && opt.asE().has<afterhours::ui::HasScrollView>())
                opt.asE().get<afterhours::ui::HasScrollView>().vertical_enabled =
                    true;
        }
        scrollbars_suppressed().clear();
        hanabi::overlay::clear_occluders();
    }
};

struct ScrollbarOcclusionSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>&, float) override {
        if (hanabi::overlay::occluders().empty()) return;

        for (const auto& e :
             afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
            if (!e || !e->has<afterhours::ui::HasScrollView>()) continue;
            if (!e->has<afterhours::ui::UIComponent>()) continue;
            auto& scroll = e->get<afterhours::ui::HasScrollView>();
            if (!scroll.vertical_enabled) continue;
            const auto& uic = e->get<afterhours::ui::UIComponent>();
            if (!uic.was_rendered_to_screen || uic.should_hide) continue;
            const auto r = uic.rect();
            if (!hanabi::overlay::occluded(
                    hanabi::surface::Rect{r.x, r.y, r.width, r.height}))
                continue;
            scroll.vertical_enabled = false;
            scrollbars_suppressed().push_back(e->id);
        }
    }
};

}  // namespace ecs
