#pragma once

#include "../../vendor/afterhours/src/core/system.h"
#include "../ui/text_select.h"
#include "../ui/widget_epoch.h"

namespace ecs {

struct WidgetRetireSystem : afterhours::System<> {
    bool should_iterate() const override { return false; }

    void once(float) override {
        hanabi::widget_epoch::configure_retirement();
        const afterhours::EntityID owner = hanabi::text_select::state().owner;
        if (owner < 0) return;
        if (afterhours::ui::UICollectionHolder::getEntityForID(owner).valid())
            return;
        hanabi::text_select::clear();
    }
};

}  // namespace ecs
