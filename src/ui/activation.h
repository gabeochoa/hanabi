#pragma once

namespace hanabi::activate {

enum class Intent {
    None,
    Palette,
    SessionSearch,
    Settings,
    Ask,
    ListCursor,
    ContextMenu,
};

struct Surfaces {
    bool palette = false;
    bool sessionSearch = false;
    bool contextMenu = false;
    bool rename = false;
    bool recordingShortcut = false;
    bool shortcuts = false;
    bool settings = false;
    bool auth = false;
    bool find = false;
    bool slashMenu = false;
    bool picker = false;
    bool askFocused = false;
};

inline Intent resolve(const Surfaces& s) {
    if (s.palette) return Intent::Palette;
    if (s.sessionSearch) return Intent::SessionSearch;
    if (s.contextMenu) return Intent::ContextMenu;
    if (s.rename) return Intent::None;
    if (s.recordingShortcut) return Intent::None;
    if (s.shortcuts) return Intent::None;
    if (s.auth) return Intent::None;
    if (s.settings) return Intent::Settings;
    if (s.find) return Intent::None;
    if (s.slashMenu) return Intent::None;
    if (s.picker) return Intent::None;
    if (s.askFocused) return Intent::Ask;
    return Intent::ListCursor;
}

inline const char* intent_name(Intent i) {
    switch (i) {
        case Intent::None: return "none";
        case Intent::Palette: return "palette";
        case Intent::SessionSearch: return "session_search";
        case Intent::Settings: return "settings";
        case Intent::Ask: return "ask";
        case Intent::ListCursor: return "list_cursor";
        case Intent::ContextMenu: return "context_menu";
    }
    return "none";
}

}  // namespace hanabi::activate
