#pragma once

#include <cstddef>

namespace hanabi::menu {

inline constexpr std::size_t kNoRow = static_cast<std::size_t>(-1);

enum class Key { None, Up, Down, Left, Right, Activate, Cancel };

enum class Effect { None, Moved, OpenedSubmenu, ClosedSubmenu, Activate, Close };

inline constexpr int kPressFrames = 3;

struct Cursor {
    std::size_t row = kNoRow;
    std::size_t child = kNoRow;
    bool submenu_open = false;
    std::size_t pressed_row = kNoRow;
    std::size_t pressed_child = kNoRow;
    int press_frames = 0;

    bool in_submenu() const { return submenu_open && child != kNoRow; }

    bool row_pressed(std::size_t at) const {
        return press_frames > 0 && pressed_row == at &&
               pressed_child == kNoRow;
    }

    bool child_pressed(std::size_t row_at, std::size_t child_at) const {
        return press_frames > 0 && pressed_row == row_at &&
               pressed_child == child_at;
    }

    void hold_press(std::size_t at, std::size_t child_at = kNoRow) {
        pressed_row = at;
        pressed_child = child_at;
        press_frames = kPressFrames;
    }

    void tick_press() {
        if (press_frames > 0 && --press_frames == 0) {
            pressed_row = kNoRow;
            pressed_child = kNoRow;
        }
    }
};

struct Shape {
    std::size_t rows = 0;
    std::size_t children_of_row = 0;
    bool row_has_children = false;
    bool row_enabled = true;
};

struct Step {
    Cursor cursor;
    Effect effect = Effect::None;
};

inline std::size_t next_enabled(std::size_t from, int delta, std::size_t count,
                                bool (*enabled)(std::size_t, void*),
                                void* ctx) {
    if (count == 0) return kNoRow;
    long i = static_cast<long>(from);
    for (std::size_t tried = 0; tried < count; ++tried) {
        i += delta;
        if (i < 0) i = 0;
        if (i >= static_cast<long>(count)) i = static_cast<long>(count) - 1;
        const std::size_t at = static_cast<std::size_t>(i);
        if (enabled == nullptr || enabled(at, ctx)) return at;
        if ((delta < 0 && i == 0) ||
            (delta > 0 && i == static_cast<long>(count) - 1))
            break;
    }
    return from == kNoRow ? kNoRow : from;
}

inline Step advance(Cursor cursor, Key key, const Shape& shape,
                    bool (*row_enabled)(std::size_t, void*) = nullptr,
                    void* ctx = nullptr) {
    Step out{cursor, Effect::None};
    if (shape.rows == 0) return out;

    if (cursor.in_submenu()) {
        switch (key) {
            case Key::Up:
            case Key::Down: {
                if (shape.children_of_row == 0) return out;
                const std::size_t moved = next_enabled(
                    cursor.child, key == Key::Down ? 1 : -1,
                    shape.children_of_row, nullptr, nullptr);
                if (moved == cursor.child) return out;
                out.cursor.child = moved;
                out.effect = Effect::Moved;
                return out;
            }
            case Key::Left:
            case Key::Cancel:
                out.cursor.child = kNoRow;
                out.cursor.submenu_open = false;
                out.effect = key == Key::Left ? Effect::ClosedSubmenu
                                              : Effect::ClosedSubmenu;
                return out;
            case Key::Activate:
                out.effect = Effect::Activate;
                return out;
            case Key::Right:
            case Key::None:
                return out;
        }
        return out;
    }

    switch (key) {
        case Key::Up:
        case Key::Down: {
            const std::size_t start =
                cursor.row == kNoRow
                    ? (key == Key::Down ? kNoRow : shape.rows)
                    : cursor.row;
            std::size_t moved;
            if (cursor.row == kNoRow) {
                moved = key == Key::Down ? 0 : shape.rows - 1;
                if (row_enabled != nullptr && !row_enabled(moved, ctx))
                    moved = next_enabled(moved, key == Key::Down ? 1 : -1,
                                         shape.rows, row_enabled, ctx);
            } else {
                moved = next_enabled(start, key == Key::Down ? 1 : -1,
                                     shape.rows, row_enabled, ctx);
            }
            if (moved == kNoRow || moved == cursor.row) return out;
            out.cursor.row = moved;
            out.cursor.child = kNoRow;
            out.cursor.submenu_open = false;
            out.effect = Effect::Moved;
            return out;
        }
        case Key::Right:
            if (!shape.row_has_children || shape.children_of_row == 0)
                return out;
            out.cursor.submenu_open = true;
            out.cursor.child = 0;
            out.effect = Effect::OpenedSubmenu;
            return out;
        case Key::Activate:
            if (cursor.row == kNoRow || !shape.row_enabled) return out;
            if (shape.row_has_children && shape.children_of_row > 0) {
                out.cursor.submenu_open = true;
                out.cursor.child = 0;
                out.effect = Effect::OpenedSubmenu;
                return out;
            }
            out.effect = Effect::Activate;
            return out;
        case Key::Cancel:
            out.effect = Effect::Close;
            return out;
        case Key::Left:
        case Key::None:
            return out;
    }
    return out;
}

}  // namespace hanabi::menu
