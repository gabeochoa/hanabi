#pragma once

#include <string>
#include <string_view>

#include "../test_hooks.h"
#include "../ui/context_menu.h"
#include "../ui/control_state.h"
#include "components.h"
#include "ui_imports.h"

namespace ecs {

struct PointerState {
    afterhours::EntityID hot = -1;
    std::string hot_name;
    bool ever_moved = false;
};

inline PointerState& pointer_state() {
    static PointerState value;
    return value;
}

inline constexpr int kKeyPressFrames = 3;

inline hanabi::surface::MenuKeys take_menu_keys(AppComponent& app) {
    hanabi::surface::MenuKeys keys;
    keys.pointer_moved = pointer_state().ever_moved;
    if (app.escape == EscapeIntent::CloseContextMenu) {
        keys.key = hanabi::menu::Key::Cancel;
        app.escape = EscapeIntent::None;
    } else if (app.arrow == ArrowIntent::ContextMenu && app.arrowDelta != 0) {
        keys.key = app.arrowDelta > 0 ? hanabi::menu::Key::Down
                                      : hanabi::menu::Key::Up;
        app.arrow = ArrowIntent::None;
        app.arrowDelta = 0;
    } else if (app.menuLateral != 0) {
        keys.key = app.menuLateral > 0 ? hanabi::menu::Key::Right
                                       : hanabi::menu::Key::Left;
        app.menuLateral = 0;
    } else if (app.activate == ActivateIntent::ContextMenu) {
        keys.key = hanabi::menu::Key::Activate;
        app.activate = ActivateIntent::None;
    }
    return keys;
}

struct PointerStateSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>&, float) override {
        auto* ctx = afterhours::EntityHelper::get_singleton_cmp<
            afterhours::ui::UIContext<InputAction>>();
        if (ctx == nullptr) return;
        record_hot(*ctx);

        if (keyHeld_ > 0) --keyHeld_;
        auto* app = find_singleton<AppComponent>();
        if (app != nullptr && app->activate != ActivateIntent::None &&
            ctx->focus_id != ctx->ROOT && takes_key_press(ctx->focus_id)) {
            keyPressed_ = ctx->focus_id;
            keyHeld_ = kKeyPressFrames;
        }
        if (keyHeld_ == 0) keyPressed_ = -1;
        if (keyPressed_ != -1)
            hanabi::control::keyboard_pressed_id() = keyPressed_;
        else if (app == nullptr || app->menuCursor.press_frames == 0)
            hanabi::control::keyboard_pressed_id() = -1;
        if (app != nullptr) app->menuCursor.tick_press();

        const afterhours::EntityID want = pressed_this_frame(*ctx);
        if (want != ctx->ROOT) apply_press(want);
        if (keyPressed_ != -1 && keyPressed_ != want) apply_press(keyPressed_);
    }

   private:
    static void record_hot(const UIContext<InputAction>& ctx) {
        PointerState& state = pointer_state();
        state.ever_moved = state.ever_moved || ctx.mouse.moved_this_frame;
        state.hot = ctx.hot_id;
        state.hot_name.clear();
        if (ctx.hot_id == ctx.ROOT) return;
        auto opt = afterhours::ui::UICollectionHolder::getEntityForID(ctx.hot_id);
        if (!opt.valid()) return;
        const Entity& e = opt.asE();
        if (e.has<afterhours::ui::UIComponentDebug>())
            state.hot_name = e.get<afterhours::ui::UIComponentDebug>().name();
    }

    static void apply_press(afterhours::EntityID id) {
        auto opt = afterhours::ui::UICollectionHolder::getEntityForID(id);
        if (!opt.valid() || !opt.asE().has<afterhours::HasColor>()) return;
        Entity& e = opt.asE();
        auto& color = e.get<afterhours::HasColor>();
        const afterhours::Color pressed =
            hanabi::control::press_over(surface_under(e));
        color.set(pressed);
        color.hover_color = pressed;
    }

    static afterhours::Color surface_under(const Entity& e) {
        const afterhours::Color own =
            e.get<afterhours::HasColor>().color();
        if (own.a > 0)
            return hanabi::control::effective_backdrop(own, own);
        if (!e.has<afterhours::ui::UIComponent>()) return theme::panel_bg();
        afterhours::EntityID up = e.get<afterhours::ui::UIComponent>().parent;
        for (int hop = 0; hop < 8 && up != -1; ++hop) {
            auto opt = afterhours::ui::UICollectionHolder::getEntityForID(up);
            if (!opt.valid()) break;
            const Entity& parent = opt.asE();
            if (parent.has<afterhours::HasColor>() &&
                parent.get<afterhours::HasColor>().color().a > 0)
                return parent.get<afterhours::HasColor>().color();
            if (!parent.has<afterhours::ui::UIComponent>()) break;
            up = parent.get<afterhours::ui::UIComponent>().parent;
        }
        return theme::panel_bg();
    }

    static bool takes_key_press(afterhours::EntityID id) {
        auto opt = afterhours::ui::UICollectionHolder::getEntityForID(id);
        if (!opt.valid()) return false;
        const Entity& e = opt.asE();
        if (!e.has<afterhours::ui::HasClickListener>()) return false;
        if (e.has<afterhours::text_input::HasTextInputState>() ||
            e.has<afterhours::text_input::HasTextAreaState>())
            return false;
        return takes_press(e);
    }

    static bool takes_press(const Entity& e) {
        return e.has<afterhours::HasColor>();
    }

    static afterhours::EntityID forced() {
        const std::string_view name = hanabi::test_hooks::forced_press_name();
        if (name.empty()) return -1;
        for (const auto& e :
             afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
            if (!e || !e->has<afterhours::ui::UIComponentDebug>()) continue;
            if (e->get<afterhours::ui::UIComponentDebug>().name() != name)
                continue;
            return takes_press(*e) ? e->id : -1;
        }
        return -1;
    }

    static afterhours::EntityID pressed_this_frame(
        const UIContext<InputAction>& ctx) {
        if (const afterhours::EntityID force = forced(); force != -1)
            return force;
        if (!pointer_state().ever_moved) return ctx.ROOT;
        if (!ctx.mouse.left_down || ctx.active_id == ctx.ROOT) return ctx.ROOT;
        if (ctx.hot_id != ctx.active_id) return ctx.ROOT;
        auto opt =
            afterhours::ui::UICollectionHolder::getEntityForID(ctx.active_id);
        if (!opt.valid() || !takes_press(opt.asE())) return ctx.ROOT;
        return ctx.active_id;
    }

    afterhours::EntityID keyPressed_ = -1;
    int keyHeld_ = 0;
};

}  // namespace ecs
