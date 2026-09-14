#pragma once

// Test-only: what the pointer pipeline DECIDED on the last press frame,
// read after HandleClicks ran (registered right after the UI post-layout
// systems), so a script can ask "did the press dispatch, and to whom"
// instead of inferring it from a rect. An app-owned observer: the library
// is not instrumented.

#include <string>

#include "../ecs/components.h"
#include "../ecs/ui_imports.h"

namespace ecs {

struct LastPress {
    bool seen = false;
    int frame = 0;
    float x = 0.0f, y = 0.0f;
    afterhours::EntityID hot = -1;
    afterhours::EntityID active = -1;
    afterhours::EntityID focus = -1;
    std::string hotName, activeName, focusName;
    std::string downNames;  // every entity whose HasClickListener.down was true
};

inline LastPress& last_press() {
    static LastPress v;
    return v;
}

struct ClickObserverSystem : afterhours::System<> {
    int frame_ = 0;
    static std::string name_of(afterhours::EntityID id) {
        if (id < 0) return "-";
        auto opt = afterhours::ui::UICollectionHolder::getEntityForID(id);
        if (!opt.valid() || !opt->has<afterhours::ui::UIComponentDebug>())
            return "#" + std::to_string(id);
        return opt->get<afterhours::ui::UIComponentDebug>().name();
    }
    void once(float) override {
        ++frame_;
        auto* ctx = afterhours::EntityHelper::get_singleton_cmp<UIContext<InputAction>>();
        if (ctx == nullptr || !ctx->mouse.just_pressed) return;
        LastPress& lp = last_press();
        lp.seen = true;
        lp.frame = frame_;
        lp.x = ctx->mouse.pos.x;
        lp.y = ctx->mouse.pos.y;
        lp.hot = ctx->hot_id;
        lp.active = ctx->active_id;
        lp.focus = ctx->focus_id;
        lp.hotName = name_of(lp.hot);
        lp.activeName = name_of(lp.active);
        lp.focusName = name_of(lp.focus);
        lp.downNames.clear();
        for (const auto& e : afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
            if (!e || !e->has<afterhours::ui::HasClickListener>()) continue;
            if (!e->get<afterhours::ui::HasClickListener>().down) continue;
            if (!lp.downNames.empty()) lp.downNames += ",";
            lp.downNames += name_of(e->id);
        }
    }
};

}  // namespace ecs
