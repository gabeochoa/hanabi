#pragma once

#include <string>
#include <string_view>

#include "../keys.h"
#include "../test_hooks.h"
#include "../ui/anchored_surface.h"
#include "../ui/reply_quote.h"
#include "../ui/secondary_surface.h"
#include "../ui/accessibility.h"
#include "../ui/tooltip.h"
#include "components.h"
#include "pointer_state_system.h"
#include "ui_imports.h"

namespace ecs {

inline constexpr int kTooltipLayer = 40;

inline bool control_says_words(const Entity& e) {
    if (e.has<afterhours::ui::HasLabel>() &&
        hanabi::reply_quote::has_ink(e.get<afterhours::ui::HasLabel>().label))
        return true;
    if (!e.has<afterhours::ui::UIComponent>()) return false;
    for (const afterhours::EntityID child :
         e.get<afterhours::ui::UIComponent>().children) {
        auto opt = afterhours::ui::UICollectionHolder::getEntityForID(child);
        if (!opt.valid()) continue;
        if (control_says_words(opt.asE())) return true;
    }
    return false;
}

inline std::string tooltip_text_for(const std::string& control,
                                    const Entity& e) {
    const std::string_view curated = hanabi::tip::text_for(control);
    if (!curated.empty()) return std::string(curated);
    if (!e.has<hanabi::a11y::AccessibleName>()) return {};
    const auto& named = e.get<hanabi::a11y::AccessibleName>();
    if (named.role != hanabi::a11y::Role::Button) return {};
    if (control_says_words(e)) return {};
    return named.value;
}

inline bool& pending_reveal() {
    static bool value = false;
    return value;
}

struct TooltipSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx,
                       float dt) override {
        std::string control;
        std::string resolved;
        hanabi::surface::Rect anchor{};
        resolve_hovered(ctx, &control, &resolved, &anchor);

        const std::string_view text = resolved;
        const bool forced = !hanabi::test_hooks::forced_tip_name().empty();
        if (text.empty())
            timer_.leave();
        else if (forced)
            timer_.force_show(control);
        else
            timer_.hold(control, hanabi::test_hooks::tip_fixed_clock()
                                     ? 1.0f / 60.0f
                                     : dt);

        if (!forced && (ctx.mouse.just_pressed || ctx.mouse.left_down ||
                        hanabi::keys::pressed(hanabi::keys::kEscape)))
            timer_.interrupt();

        pending_reveal() = timer_.awaiting_reveal();
        if (!timer_.visible()) return;

        const float w = hanabi::tip::width_for(text);
        const auto at = hanabi::surface::place_tooltip(
            anchor, w, hanabi::tip::kTipHeight, ctx.screen_width,
            ctx.screen_height);

        Entity& uiRoot = ui_imm::getUIRootEntity();
        auto panel = hanabi::surface::menu(w, hanabi::tip::kTipHeight,
                                           kTooltipLayer);
        panel.with_absolute_position()
            .with_translate(at.x, at.y)
            .with_debug_name("tooltip");
        auto tip = hanabi::ui::div(ctx, mk(uiRoot, 8600), panel);
        hanabi::a11y::describe(tip.ent(),
                               {.value = text,
                                .role = hanabi::a11y::Role::Tooltip,
                                .parent = pointer_state().hot_name});
        hanabi::ui::div(
            ctx, mk(uiRoot, 8601),
            ComponentConfig{}
                .with_label(std::string(text))
                .with_size(ComponentSize{pixels(w - hanabi::tip::kTipPadH),
                                         pixels(hanabi::tip::kTipHeight)})
                .with_absolute_position()
                .with_translate(at.x + hanabi::tip::kTipPadH, at.y)
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_render_layer(kTooltipLayer + 1)
                .with_debug_name("tooltip_text"));
    }

   private:
    static void resolve_hovered(const UIContext<InputAction>& ctx,
                                std::string* name, std::string* text,
                                hanabi::surface::Rect* rect) {
        const std::string_view forced = hanabi::test_hooks::forced_tip_name();
        if (forced.empty() && !pointer_state().ever_moved) return;
        const afterhours::EntityID from =
            forced.empty() ? pointer_state().hot : named_entity(forced);
        afterhours::EntityID walk = from;
        for (int depth = 0; depth < 6 && walk != ctx.ROOT && walk != -1;
             ++depth) {
            auto opt = afterhours::ui::UICollectionHolder::getEntityForID(walk);
            if (!opt.valid()) return;
            const Entity& e = opt.asE();
            if (!e.has<afterhours::ui::UIComponent>()) return;
            const auto& uic = e.get<afterhours::ui::UIComponent>();
            if (e.has<afterhours::ui::UIComponentDebug>()) {
                const std::string named =
                    e.get<afterhours::ui::UIComponentDebug>().name();
                const std::string said = tooltip_text_for(named, e);
                if (!said.empty()) {
                    const auto r = uic.rect();
                    *name = named;
                    *text = said;
                    *rect = hanabi::surface::Rect{r.x, r.y, r.width, r.height};
                    return;
                }
            }
            walk = uic.parent;
        }
    }

    static afterhours::EntityID named_entity(std::string_view name) {
        for (const auto& e :
             afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
            if (!e || !e->has<afterhours::ui::UIComponentDebug>()) continue;
            if (e->get<afterhours::ui::UIComponentDebug>().name() == name)
                return e->id;
        }
        return -1;
    }

    hanabi::tip::Timer timer_;
};

}  // namespace ecs
