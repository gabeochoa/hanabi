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
#include "../ui/send_chords.h"
#include "../settings.h"
#include "keycap_row.h"
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
    // A named button OR checkbox says what it does; a row or a tab does
    // not carry a tooltip.
    if (named.role != hanabi::a11y::Role::Button &&
        named.role != hanabi::a11y::Role::Checkbox)
        return {};
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

        if (Settings::get().get_send_chords() &&
            (control == "composer_send" || control == "composer_send_2")) {
            draw_send_hint_card(ctx, anchor, control);
            return;
        }

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
    static constexpr float kHintRowH = 22.0f;
    static constexpr float kHintW = 300.0f;
    static constexpr float kHintChordW = 120.0f;

    static void draw_send_hint_card(UIContext<InputAction>& ctx,
                                    const hanabi::surface::Rect& anchor,
                                    const std::string& control) {
        auto* app = find_singleton<AppComponent>();
        const bool running = app != nullptr && app->should_steer_open();
        const auto rows = hanabi::send_chords::hint_rows(running, false);
        const float h = kHintRowH * static_cast<float>(rows.size()) + 8.0f;
        const auto at = hanabi::surface::place_tooltip(anchor, kHintW, h, ctx.screen_width,
                                                       ctx.screen_height);
        Entity& uiRoot = ui_imm::getUIRootEntity();
        auto panel = hanabi::surface::menu(kHintW, h, kTooltipLayer);
        panel.with_absolute_position()
            .with_translate(at.x, at.y)
            .with_flex_direction(FlexDirection::Column)
            .with_flex_wrap(FlexWrap::NoWrap)
            .with_padding(Padding{.top = pixels(4), .right = pixels(8), .bottom = pixels(4),
                                  .left = pixels(8)})
            .with_debug_name("send_hint_card");
        auto card = hanabi::ui::div(ctx, mk(uiRoot, 8600), panel);
        hanabi::a11y::describe(card.ent(), {.value = "What Return does, and the chords",
                                            .role = hanabi::a11y::Role::Tooltip,
                                            .parent = control});
        int id = 1;
        for (const auto& r : rows) {
            auto line = hanabi::ui::div(ctx, mk(card.ent(), id++),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(1.0f), pixels(kHintRowH)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_align_items(AlignItems::Center)
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_render_layer(kTooltipLayer + 1)
                    .with_debug_name("send_hint_row"));
            auto chordCol = hanabi::ui::div(ctx, mk(line.ent(), 1),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(kHintChordW), pixels(keycap_row::kCapH)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_align_items(AlignItems::Center)
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_render_layer(kTooltipLayer + 1)
                    .with_debug_name("send_hint_chord"));
            keycap_row::render(ctx, chordCol.ent(), 1, r.chord,
                               "send_hint_" + std::to_string(id - 2), false,
                               kTooltipLayer + 1);
            hanabi::ui::div(ctx, mk(line.ent(), 2),
                ComponentConfig{}
                    .with_label(r.meaning)
                    .with_size(ComponentSize{pixels(kHintW - kHintChordW - 20.0f),
                                             pixels(kHintRowH)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_primary())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_render_layer(kTooltipLayer + 1)
                    .with_debug_name("send_hint_meaning_" + std::to_string(id - 2)));
        }
    }

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
