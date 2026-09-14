#pragma once

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <afterhours/src/plugins/ui/imm_components.h>
#include "accessibility.h"
#include "anchored_surface.h"
#include "control_state.h"
#include "div.h"
#include "menu_keys.h"
#include "../native_menu.h"
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
    // What picking this row MEANS, as a stable name ("archive", "mute",
    // "open:tab"), decided by the caller when it builds the item. A result
    // that arrives a frame or more after the menu opened (the native menu
    // closes on its own run-loop turn) is resolved by this id against the
    // snapshot the menu was opened with -- never by a row index into a
    // vector the caller may have rebuilt since. Empty = the row's index is
    // its only identity (the drawn menu, same frame).
    std::string action_id;
    // A group boundary, drawn as a hairline in a row slot of its own: no
    // label, no press, skipped by the keyboard (it is `disabled` to the
    // cursor), not announced. The reference's menus separate their groups
    // with one; its height there is the platform's, and hanabi's is a full
    // row slot until a capture says what it should be.
    bool separator = false;

    static MenuItem divider(std::string debug_name) {
        MenuItem m;
        m.debug_name = std::move(debug_name);
        m.disabled = true;
        m.separator = true;
        return m;
    }
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

    // A menu with no caption draws no caption row: the caller passes an
    // empty title and metrics.header_h = 0, the way a submenu already does.
    if (title != nullptr && *title != '\0')
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
        if (item.separator) {
            hanabi::ui::div(
                ctx, mk(root, baseKey + 3 + static_cast<int>(k)),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(metrics.row_width()),
                                             pixels(metrics.row_h)})
                    .with_absolute_position()
                    .with_translate(metrics.row_x(at.x), metrics.row_y(at.y, k))
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_render_layer(layer + 1)
                    .with_on_draw_fg([](RectangleType r) {
                        const float y = r.y + r.height * 0.5f;
                        afterhours::draw_rectangle(
                            RectangleType{r.x + 6.0f, y, r.width - 12.0f, 1.0f},
                            theme::border());
                    })
                    .with_debug_name(item.debug_name));
            continue;
        }
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

// ---- the native arm -------------------------------------------------------
//
// A menu that was OPENED as a native NSMenu (hanabi::native_menu) is not
// drawn by the app; the caller still runs every frame while its open-state
// says so, and this shim gives it the same MenuResult the drawn menu would:
// nothing until the native menu closes, then the pick or the dismissal, once.
// The caller keeps its snapshot (`NativeMenuOpen`) in its own open-state, so
// a result is applied to the items the menu was OPENED with, by action_id.
//
// While the native menu tracks, the app still draws the drawn menu's
// full-window transparent eater under it: afterhours has no notion of an
// external tracker owning the pointer (recorded in afterhours_gaps.md), so
// this is how nothing underneath registers hover or press. The eater's own
// press is ignored here -- AppKit decides dismissal, not the eater -- it
// only swallows.
// The caller's record of an open native menu (generation, scope, the
// snapshot of action ids by row); see native_menu.h.
using NativeMenuOpen = hanabi::native_menu::Open;

// Focus return on close (see native_menu::focus_to_restore): AppKit handing
// key status back to the view does not touch the UI's focus_id, and a press
// on the eater moves focus onto it -- so the element focused when the menu
// opened is restored once, if it still exists and no menu action moved
// focus elsewhere.
template <typename Ctx>
void native_menu_restore_focus(Ctx& ctx, const hanabi::native_menu::Open& open) {
    const long long now = static_cast<long long>(ctx.focus_id);
    const bool exists =
        open.focus_before >= 0 &&
        afterhours::ui::UICollectionHolder::getEntityForID(
            static_cast<afterhours::EntityID>(open.focus_before))
            .valid();
    const long long to = hanabi::native_menu::focus_to_restore(
        open.focus_before, open.eater_id, now, static_cast<long long>(ctx.ROOT), exists);
    if (to >= 0) ctx.set_focus(static_cast<afterhours::EntityID>(to));
}

// The result the native menu has for `open`, if it closed; the eater is drawn
// so the pointer underneath is owned. `dismissed` also fires when the menu's
// scope no longer matches `expectScope` (the target changed under it): the
// caller cancels and closes rather than applying a pick to a new target.
template <typename Ctx>
MenuResult native_menu_frame(Ctx& ctx, afterhours::Entity& root, int baseKey,
                             const char* debugName, NativeMenuOpen& open,
                             std::string_view expectScope,
                             int layer = kContextMenuLayer) {
    using afterhours::ui::imm::button;
    using afterhours::ui::imm::mk;
    MenuResult out;
    if (!open.open()) return out;
    // Ownership of the pointer while AppKit tracks: swallow, never decide.
    auto eater = button(ctx, mk(root, baseKey),
           ComponentConfig{}
               .with_label(" ")
               .with_size(ComponentSize{pixels(ctx.screen_width), pixels(ctx.screen_height)})
               .with_absolute_position()
               .with_translate(0.0f, 0.0f)
               .with_transparent_bg()
               .with_custom_hover_bg(afterhours::Color{0, 0, 0, 0})
               .with_click_activation(ClickActivationMode::Press)
               .with_skip_tabbing(true)  // owns the pointer, never the keyboard focus
               .with_roundness(0.0f)
               .with_render_layer(layer - 1)
               .with_debug_name(std::string(debugName) + "_native_eater"));
    open.eater_id = eater.ent().id;
    ctx.set_hot(ctx.ROOT);  // nothing underneath is hot while the menu owns input

    if (open.scope != expectScope) {
        if (std::getenv("HANABI_NATIVE_MENU_LOG"))
            std::fprintf(stderr, "[native-menu] scope changed under %s (now %.*s): cancelling\n",
                         open.scope.c_str(), static_cast<int>(expectScope.size()),
                         expectScope.data());
        hanabi::native_menu::cancel(open.generation);
        out.dismissed = true;
        out.cancelled = true;
        native_menu_restore_focus(ctx, open);
        return out;
    }
    hanabi::native_menu::Result r;
    if (!hanabi::native_menu::take_result(open.generation, &r)) return out;
    if (r.dismissed) {
        out.dismissed = true;
        out.cancelled = true;
        native_menu_restore_focus(ctx, open);
        return out;
    }
    out.activated = r.row;
    out.activated_child = r.child;
    // A pick: the caller's action may set its own focus (rename opens a
    // modal, say) AFTER this returns, so the restore happens first and that
    // later request wins by ordering.
    native_menu_restore_focus(ctx, open);
    return out;
}

// Open a native menu for `items`, recording the snapshot in `open`. Returns
// false (and leaves `open` empty) when the native arm is unavailable or a
// menu is already up -- the caller then draws its own menu as before.
inline bool native_menu_open(NativeMenuOpen& open, std::string scope, const char* debugName,
                             const std::vector<MenuItem>& items, float contentX,
                             float contentY, long long focusBefore = -1) {
    if (!hanabi::native_menu::available()) return false;
    hanabi::native_menu::Request req;
    req.scope = scope;
    req.debug_name = debugName;
    req.items.reserve(items.size());
    for (const MenuItem& m : items) {  // copied, flattened to the adapter's type
        hanabi::native_menu::Item it{m.label, m.debug_name, m.disabled, m.destructive,
                                     m.separator, {}};
        for (const MenuLeaf& c : m.children)
            it.children.push_back({c.label, c.debug_name, c.disabled, c.destructive, false, {}});
        req.items.push_back(std::move(it));
    }
    req.content_x = contentX;
    req.content_y = contentY;
    const std::uint64_t gen = hanabi::native_menu::request(std::move(req));
    if (gen == 0) return false;
    open.generation = gen;
    open.scope = std::move(scope);
    open.action_ids.clear();
    for (const MenuItem& m : items) open.action_ids.push_back(m.action_id);
    open.focus_before = focusBefore;
    open.eater_id = -1;
    return true;
}

}  // namespace hanabi::surface
