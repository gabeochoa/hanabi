#include "../../src/ui/menu_keys.h"

#include <cassert>
#include <iostream>
#include <vector>

using hanabi::menu::Cursor;
using hanabi::menu::Effect;
using hanabi::menu::Key;
using hanabi::menu::Shape;
using hanabi::menu::kPressFrames;
using hanabi::menu::advance;
using hanabi::menu::kNoRow;

static std::vector<bool> g_enabled;

static bool enabled_at(std::size_t i, void*) {
    return i < g_enabled.size() && g_enabled[i];
}

int main() {
    Shape flat;
    flat.rows = 5;

    Cursor c;
    auto step = advance(c, Key::Down, flat);
    assert(step.cursor.row == 0);
    assert(step.effect == Effect::Moved);

    step = advance(step.cursor, Key::Down, flat);
    assert(step.cursor.row == 1);

    step = advance(step.cursor, Key::Up, flat);
    assert(step.cursor.row == 0);

    step = advance(step.cursor, Key::Up, flat);
    assert(step.cursor.row == 0);
    assert(step.effect == Effect::None);

    Cursor last;
    last.row = 4;
    step = advance(last, Key::Down, flat);
    assert(step.cursor.row == 4);
    assert(step.effect == Effect::None);

    Cursor fromNothing;
    step = advance(fromNothing, Key::Up, flat);
    assert(step.cursor.row == 4);

    g_enabled = {true, false, false, true, true};
    Cursor skip;
    step = advance(skip, Key::Down, flat, &enabled_at, nullptr);
    assert(step.cursor.row == 0);
    step = advance(step.cursor, Key::Down, flat, &enabled_at, nullptr);
    assert(step.cursor.row == 3);
    step = advance(step.cursor, Key::Up, flat, &enabled_at, nullptr);
    assert(step.cursor.row == 0);

    g_enabled = {false, false, true};
    Shape three;
    three.rows = 3;
    Cursor firstDisabled;
    step = advance(firstDisabled, Key::Down, three, &enabled_at, nullptr);
    assert(step.cursor.row == 2);

    Shape parent;
    parent.rows = 5;
    parent.children_of_row = 2;
    parent.row_has_children = true;
    Cursor onParent;
    onParent.row = 0;
    step = advance(onParent, Key::Right, parent);
    assert(step.effect == Effect::OpenedSubmenu);
    assert(step.cursor.submenu_open);
    assert(step.cursor.child == 0);
    assert(step.cursor.in_submenu());

    Cursor inSub = step.cursor;
    step = advance(inSub, Key::Down, parent);
    assert(step.cursor.child == 1);
    assert(step.effect == Effect::Moved);

    step = advance(step.cursor, Key::Down, parent);
    assert(step.cursor.child == 1);
    assert(step.effect == Effect::None);

    step = advance(step.cursor, Key::Activate, parent);
    assert(step.effect == Effect::Activate);
    assert(step.cursor.in_submenu());

    step = advance(inSub, Key::Left, parent);
    assert(step.effect == Effect::ClosedSubmenu);
    assert(!step.cursor.submenu_open);
    assert(step.cursor.child == kNoRow);
    assert(step.cursor.row == 0);

    step = advance(inSub, Key::Cancel, parent);
    assert(step.effect == Effect::ClosedSubmenu);
    assert(!step.cursor.submenu_open);

    step = advance(onParent, Key::Activate, parent);
    assert(step.effect == Effect::OpenedSubmenu);

    Shape leaf;
    leaf.rows = 5;
    Cursor onLeaf;
    onLeaf.row = 2;
    step = advance(onLeaf, Key::Activate, leaf);
    assert(step.effect == Effect::Activate);

    Cursor none;
    step = advance(none, Key::Activate, leaf);
    assert(step.effect == Effect::None);

    Shape disabledRow;
    disabledRow.rows = 5;
    disabledRow.row_enabled = false;
    step = advance(onLeaf, Key::Activate, disabledRow);
    assert(step.effect == Effect::None);

    step = advance(onLeaf, Key::Cancel, leaf);
    assert(step.effect == Effect::Close);

    step = advance(onLeaf, Key::Right, leaf);
    assert(step.effect == Effect::None);

    step = advance(onLeaf, Key::Left, leaf);
    assert(step.effect == Effect::None);

    Shape empty;
    step = advance(none, Key::Down, empty);
    assert(step.effect == Effect::None);
    assert(step.cursor.row == kNoRow);

    Cursor movingOut;
    movingOut.row = 1;
    movingOut.submenu_open = true;
    movingOut.child = 0;
    step = advance(movingOut, Key::Down, flat);
    assert(step.cursor.child == 0);

    Cursor held;
    held.row = 2;
    assert(!held.row_pressed(2));
    held.hold_press(2);
    assert(held.row_pressed(2));
    assert(!held.row_pressed(1));
    assert(!held.child_pressed(2, 0));
    for (int i = 0; i < kPressFrames - 1; ++i) {
        held.tick_press();
        assert(held.row_pressed(2));
    }
    held.tick_press();
    assert(!held.row_pressed(2));
    assert(held.pressed_row == kNoRow);

    Cursor heldLeaf;
    heldLeaf.row = 1;
    heldLeaf.submenu_open = true;
    heldLeaf.child = 1;
    heldLeaf.hold_press(1, 1);
    assert(heldLeaf.child_pressed(1, 1));
    assert(!heldLeaf.row_pressed(1));
    assert(!heldLeaf.child_pressed(1, 0));

    Cursor viaModel;
    viaModel.row = 0;
    Shape leafShape;
    leafShape.rows = 3;
    assert(advance(viaModel, Key::Activate, leafShape).effect ==
           Effect::Activate);

    std::cout << "menu keys: ok\n";
    return 0;
}
