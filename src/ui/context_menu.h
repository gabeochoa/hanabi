#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "../../vendor/afterhours/src/plugins/ui/imm_components.h"
#include "accessibility.h"
#include "anchored_surface.h"
#include "control_state.h"
#include "div.h"
#include "menu_keys.h"
#include "overlay_lifecycle.h"
#include "secondary_surface.h"
#include "theme.h"

namespace hanabi::surface {

inline constexpr float kContextMenuW = 184.0f;
inline constexpr int kContextMenuLayer = 30;

inline constexpr std::size_t kNoMenuRow = menu::kNoRow;

struct MenuLeaf {
    std::string label;
    std::string debug_name;
    bool destructive = false;
    bool disabled = false;
};

struct MenuItem {
    std::string label;
    std::string debug_name;
    bool destructive = false;
    bool disabled = false;
    std::vector<MenuLeaf> children;
};

struct MenuResult {
    std::size_t activated = kNoMenuRow;
    std::size_t activated_child = kNoMenuRow;
    bool dismissed = false;
    bool cancelled = false;
    Rect rect{};
};

struct MenuKeys {
    menu::Key key = menu::Key::None;
    bool pointer_moved = false;
};

namespace detail {
inline bool menu_row_enabled(std::size_t i, void* ctx) {
    const auto* items = static_cast<const std::vector<MenuItem>*>(ctx);
    return i < items->size() && !(*items)[i].disabled;
}
}  // namespace detail

inline void apply_menu_keys(menu::Cursor& cursor, const MenuKeys& keys,
                            const std::vector<MenuItem>& items,
                            MenuResult* out) {
    if (keys.key == menu::Key::None) return;
    menu::Shape shape;
    shape.rows = items.size();
    if (cursor.row != menu::kNoRow && cursor.row < items.size()) {
        shape.children_of_row = items[cursor.row].children.size();
        shape.row_has_children = !items[cursor.row].children.empty();
        shape.row_enabled = !items[cursor.row].disabled;
    }
    const menu::Step step = menu::advance(
        cursor, keys.key, shape, &detail::menu_row_enabled,
        const_cast<std::vector<MenuItem>*>(&items));
    const std::size_t from = cursor.row;
    const std::size_t fromChild = cursor.child;
    const bool wasInSubmenu = cursor.in_submenu();
    cursor = step.cursor;
    if (step.effect == menu::Effect::Activate)
        cursor.hold_press(from, wasInSubmenu ? fromChild : menu::kNoRow);
    if (step.effect == menu::Effect::OpenedSubmenu &&
        keys.key == menu::Key::Activate)
        cursor.hold_press(from);
    if (step.effect == menu::Effect::Activate && out != nullptr) {
        out->activated = cursor.row;
        out->activated_child = cursor.in_submenu() ? cursor.child : kNoMenuRow;
    }
    if (step.effect == menu::Effect::Close && out != nullptr)
        out->cancelled = true;
}

template <typename Ctx>
MenuResult context_menu(Ctx& ctx, afterhours::Entity& root, int baseKey,
                        const MenuMetrics& metrics, float wantX, float wantY,
                        const char* title, const char* debugName,
                        const std::vector<MenuItem>& items,
                        menu::Cursor& cursor, const MenuKeys& keys = {},
                        int layer = kContextMenuLayer,
                        std::string_view forced_submenu = {}) {
    using afterhours::ui::imm::button;
    using afterhours::ui::imm::mk;

    MenuResult out;
    apply_menu_keys(cursor, keys, items, &out);

    const float menuH = metrics.height_for(items.size());
    const Placed at = place_at(wantX, wantY, metrics.width, menuH,
                               ctx.screen_width, ctx.screen_height);
    out.rect = Rect{at.x, at.y, metrics.width, menuH};
    hanabi::overlay::publish_occluder(out.rect);

    auto eater = button(
        ctx, mk(root, baseKey),
        ComponentConfig{}
            .with_label(" ")
            .with_size(ComponentSize{pixels(ctx.screen_width),
                                     pixels(ctx.screen_height)})
            .with_absolute_position()
            .with_translate(0.0f, 0.0f)
            .with_transparent_bg()
            .with_custom_hover_bg(afterhours::Color{0, 0, 0, 0})
            .with_click_activation(ClickActivationMode::Press)
            .with_roundness(0.0f)
            .with_render_layer(layer - 1)
            .with_debug_name(std::string(debugName) + "_eater"));
    out.dismissed =
        out.dismissed ||
        hanabi::overlay::dismisses(static_cast<bool>(eater), ctx.mouse.pos.x,
                                   ctx.mouse.pos.y, out.rect);

    auto panel = menu(metrics.width, menuH, layer);
    panel.with_absolute_position()
        .with_translate(at.x, at.y)
        .with_debug_name(debugName);
    auto panelEl = hanabi::ui::div(ctx, mk(root, baseKey + 1), panel);
    hanabi::a11y::describe(panelEl.ent(),
                           {.value = title, .role = hanabi::a11y::Role::Menu});

    hanabi::ui::div(
        ctx, mk(root, baseKey + 2),
        ComponentConfig{}
            .with_label(title)
            .with_size(ComponentSize{pixels(metrics.width - 16.0f),
                                     pixels(metrics.header_h)})
            .with_absolute_position()
            .with_translate(at.x + 8.0f, at.y + metrics.pad)
            .with_transparent_bg()
            .with_custom_text_color(theme::text_faint())
            .with_font_size(theme::type::MICRO)
            .with_letter_spacing(0.8f)
            .with_alignment(TextAlignment::Left)
            .with_render_layer(layer + 1)
            .with_debug_name(std::string(debugName) + "_title"));

    for (std::size_t k = 0; k < items.size(); ++k) {
        const MenuItem& item = items[k];
        const bool selected = cursor.row == k && !cursor.in_submenu();
        const theme::Color base = item.destructive && !item.disabled
                                      ? destructive_surface()
                                      : theme::panel_bg();
        hanabi::control::State state;
        state.disabled = item.disabled;
        state.selected = selected;
        state.pressed = cursor.row_pressed(k);
        auto row = option_row(metrics.row_width(), metrics.row_h, state,
                              layer + 1, base);
        row.with_label(item.label)
            .with_absolute_position()
            .with_translate(metrics.row_x(at.x), metrics.row_y(at.y, k))
            .with_font_size(theme::type::ROW)
            .with_alignment(TextAlignment::Left)
            .with_padding(Padding{.left = pixels(10)})
            .with_custom_text_color(
                item.destructive && !item.disabled
                    ? theme::destructive()
                    : hanabi::control::ink_emphasized(state))
            .with_debug_name(item.debug_name);
        auto hit = button(ctx, mk(root, baseKey + 3 + static_cast<int>(k)), row);
        if (state.pressed)
            hanabi::control::keyboard_pressed_id() = hit.ent().id;
        hanabi::a11y::describe(
            hit.ent(),
            {.value = item.label,
             .role = hanabi::a11y::Role::MenuItem,
             .enabled = !item.disabled,
             .selected = cursor.row == k,
             .has_submenu = !item.children.empty(),
             .expanded = cursor.row == k && cursor.submenu_open,
             .parent = title});
        if (hit && !item.disabled && item.children.empty()) out.activated = k;
        if (hit && !item.disabled && !item.children.empty()) {
            cursor.row = k;
            cursor.submenu_open = true;
            cursor.child = 0;
        }

        if (item.children.empty()) continue;

        MenuMetrics sub = metrics;
        sub.header_h = 0.0f;
        const float subH = sub.height_for(item.children.size());
        const Placed subAt = place_submenu(out.rect, metrics.row_y(at.y, k),
                                           sub.width, subH, ctx.screen_width,
                                           ctx.screen_height);
        const Rect subRect{subAt.x, subAt.y, sub.width, subH};
        const Rect parentRow{metrics.row_x(at.x), metrics.row_y(at.y, k),
                             metrics.row_width(), metrics.row_h};
        const bool keyboardOpen = cursor.row == k && cursor.submenu_open;
        const bool pointerOpen =
            keys.pointer_moved &&
            (hanabi::overlay::inside(parentRow, ctx.mouse.pos.x,
                                     ctx.mouse.pos.y) ||
             hanabi::overlay::inside(subRect, ctx.mouse.pos.x,
                                     ctx.mouse.pos.y));
        const bool open =
            keyboardOpen || pointerOpen ||
            (!forced_submenu.empty() && forced_submenu == item.debug_name);
        if (!open) continue;

        hanabi::overlay::publish_occluder(subRect);
        auto subPanel = menu(sub.width, subH, layer + 2);
        subPanel.with_absolute_position()
            .with_translate(subAt.x, subAt.y)
            .with_debug_name(item.debug_name + "_submenu");
        hanabi::ui::div(
            ctx, mk(root, baseKey + 40 + static_cast<int>(k) * 8), subPanel);
        for (std::size_t c = 0; c < item.children.size(); ++c) {
            const MenuLeaf& leaf = item.children[c];
            const bool leafSelected =
                cursor.row == k && cursor.in_submenu() && cursor.child == c;
            hanabi::control::State leafState;
            leafState.disabled = leaf.disabled;
            leafState.selected = leafSelected;
            leafState.pressed = cursor.child_pressed(k, c);
            const theme::Color leafBase = leaf.destructive && !leaf.disabled
                                              ? destructive_surface()
                                              : theme::panel_bg();
            auto leafRow = option_row(sub.row_width(), sub.row_h, leafState,
                                      layer + 3, leafBase);
            leafRow.with_label(leaf.label)
                .with_absolute_position()
                .with_translate(sub.row_x(subAt.x), sub.row_y(subAt.y, c))
                .with_font_size(theme::type::ROW)
                .with_alignment(TextAlignment::Left)
                .with_padding(Padding{.left = pixels(10)})
                .with_custom_text_color(
                    leaf.destructive && !leaf.disabled
                        ? theme::destructive()
                        : hanabi::control::ink_emphasized(leafState))
                .with_debug_name(leaf.debug_name);
            auto leafHit =
                button(ctx, mk(root, baseKey + 41 + static_cast<int>(k) * 8 +
                                         static_cast<int>(c)),
                       leafRow);
            if (leafState.pressed)
                hanabi::control::keyboard_pressed_id() = leafHit.ent().id;
            hanabi::a11y::describe(leafHit.ent(),
                                   {.value = leaf.label,
                                    .role = hanabi::a11y::Role::MenuItem,
                                    .enabled = !leaf.disabled,
                                    .selected = leafSelected,
                                    .parent = item.label});
            if (leafHit && !leaf.disabled) {
                out.activated = k;
                out.activated_child = c;
            }
        }
        if (hanabi::overlay::inside(subRect, ctx.mouse.pos.x, ctx.mouse.pos.y))
            out.dismissed = false;
    }

    return out;
}

}  // namespace hanabi::surface
