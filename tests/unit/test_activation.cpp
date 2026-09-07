#include "../../src/ui/activation.h"

#include <cassert>
#include <iostream>

int main() {
    using hanabi::activate::Intent;
    using hanabi::activate::Surfaces;
    using hanabi::activate::resolve;

    assert(resolve(Surfaces{}) == Intent::ListCursor);

    Surfaces palette;
    palette.palette = true;
    palette.askFocused = true;
    palette.settings = true;
    assert(resolve(palette) == Intent::Palette);

    Surfaces search;
    search.sessionSearch = true;
    search.askFocused = true;
    assert(resolve(search) == Intent::SessionSearch);

    Surfaces bothOverlays;
    bothOverlays.palette = true;
    bothOverlays.sessionSearch = true;
    assert(resolve(bothOverlays) == Intent::Palette);

    Surfaces rowMenu;
    rowMenu.contextMenu = true;
    assert(resolve(rowMenu) == Intent::ContextMenu);

    Surfaces rowMenuOverList;
    rowMenuOverList.contextMenu = true;
    rowMenuOverList.askFocused = true;
    assert(resolve(rowMenuOverList) == Intent::ContextMenu);

    Surfaces renaming;
    renaming.rename = true;
    renaming.askFocused = true;
    assert(resolve(renaming) == Intent::None);

    Surfaces recording;
    recording.recordingShortcut = true;
    assert(resolve(recording) == Intent::None);

    Surfaces sheet;
    sheet.shortcuts = true;
    assert(resolve(sheet) == Intent::None);

    Surfaces login;
    login.auth = true;
    login.settings = true;
    login.askFocused = true;
    assert(resolve(login) == Intent::None);

    Surfaces settings;
    settings.settings = true;
    settings.askFocused = true;
    assert(resolve(settings) == Intent::Settings);

    Surfaces finding;
    finding.find = true;
    finding.askFocused = true;
    assert(resolve(finding) == Intent::None);

    Surfaces slash;
    slash.slashMenu = true;
    slash.askFocused = true;
    assert(resolve(slash) == Intent::None);

    Surfaces picker;
    picker.picker = true;
    picker.askFocused = true;
    assert(resolve(picker) == Intent::None);

    Surfaces ask;
    ask.askFocused = true;
    assert(resolve(ask) == Intent::Ask);

    Surfaces everyOverlayShut;
    everyOverlayShut.askFocused = false;
    assert(resolve(everyOverlayShut) == Intent::ListCursor);

    assert(std::string_view(hanabi::activate::intent_name(Intent::Palette)) ==
           "palette");
    assert(std::string_view(hanabi::activate::intent_name(Intent::None)) ==
           "none");
    assert(std::string_view(hanabi::activate::intent_name(
               Intent::ContextMenu)) == "context_menu");

    std::cout << "activation ladder: ok\n";
    return 0;
}
