#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <afterhours/src/plugins/ui/text_input/state.h>
#include <afterhours/src/plugins/ui/text_input/text_area_state.h>
#include <afterhours/src/plugins/ui/text_input/utils.h>

#include "../edit_verbs.h"
#include "../util/clipboard.h"

namespace ecs::edit_actions {

using hanabi::EditVerb;
enum class EditOwner { None, Field, Transcript, Image };

using ClipboardRead = std::string (*)();
using ClipboardWrite = void (*)(std::string_view);

struct Clipboard {
    ClipboardRead read;
    ClipboardWrite write;
};

inline Clipboard app_clipboard() {
    return Clipboard{[] { return hanabi::clipboard::get_text(); },
                     [](std::string_view text) { hanabi::clipboard::set_text(text); }};
}

template <typename State>
bool undo(State& s) {
    if (s.readonly || !s.undo()) return false;
    afterhours::text_input::reset_blink(s);
    return true;
}

template <typename State>
bool redo(State& s) {
    if (s.readonly || !s.redo()) return false;
    afterhours::text_input::reset_blink(s);
    return true;
}

template <typename State>
bool copy(State& s, const Clipboard& clip) {
    if (!s.has_selection()) return false;
    clip.write(s.selected_text());
    return true;
}

template <typename State>
bool cut(State& s, const Clipboard& clip) {
    if (s.readonly || !s.has_selection()) return false;
    s.push_undo_snapshot();
    clip.write(s.selected_text());
    afterhours::text_input::delete_selection(s);
    afterhours::text_input::reset_blink(s);
    return true;
}

template <typename State>
bool paste_text(State& s, const Clipboard& clip) {
    if (s.readonly) return false;
    const std::string text = clip.read();
    if (text.empty()) return false;
    s.push_undo_snapshot();
    if (s.has_selection()) afterhours::text_input::delete_selection(s);
    for (std::size_t i = 0; i < text.size();) {
        const int cp = afterhours::text_input::utf8_to_codepoint(text, i);
        if (cp == '\n') afterhours::text_input::insert_newline_if_multiline(s);
        else afterhours::text_input::insert_char(s, cp);
        const std::size_t len = afterhours::ui::utf8_char_length(text, i);
        i += len > 0 ? len : 1;
    }
    s.clear_selection();
    afterhours::text_input::reset_blink(s);
    return true;
}

struct Focus {
    bool composer = false;
    bool area = false;
    bool field = false;
};

inline EditOwner owner_for(EditVerb verb, Focus focus, bool imageOnPasteboard) {
    if (verb == EditVerb::Paste && focus.composer && imageOnPasteboard) return EditOwner::Image;
    if (focus.area || focus.field) return EditOwner::Field;
    if (verb == EditVerb::Copy) return EditOwner::Transcript;
    return EditOwner::None;
}

template <typename State>
bool select_all(State& s) {
    s.selection_anchor = 0;
    s.cursor_position = s.text_size();
    afterhours::text_input::reset_blink(s);
    return true;
}

template <typename State>
bool perform_on(State& s, EditVerb verb, const Clipboard& clip) {
    switch (verb) {
        case EditVerb::Undo: return undo(s);
        case EditVerb::Redo: return redo(s);
        case EditVerb::Cut: return cut(s, clip);
        case EditVerb::Copy: return copy(s, clip);
        case EditVerb::Paste: return paste_text(s, clip);
        case EditVerb::SelectAll: return select_all(s);
        case EditVerb::Count: break;
    }
    return false;
}

}
