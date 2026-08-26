#pragma once

// ---------------------------------------------------------------------------
// The editing chords afterhours has no action for.
//
// The key table (src/input_mapping.h) covers everything the widget itself can
// carry out: word motion, word delete, line ends, select-all. macOS has one
// more that the library has no InputAction for at all — Cmd+Backspace, delete
// from the caret back to the start of the line — and vendor/afterhours is
// read-only here, so it is hanabi's to do (afterhours_gaps.md #257).
//
// HOW IT DOES IT, AND WHY NOT THE OBVIOUS WAY. The obvious way is to erase the
// text out of the field's state directly, the way the library's own
// delete_word_before_cursor does. That does not survive the frame. The widget
// is immediate-mode and re-seeds itself from the std::string it is bound to on
// every call:
//
//     if (s.text() != text) { s.storage.clear(); s.storage.insert(0, text); }
//
// An outside system that erases from the state and not from the bound string
// leaves the two disagreeing, and that line puts the deleted text straight
// back — with the caret thrown to the end for good measure. It is why
// escape-to-clear in main_pane_system.h has to clear BOTH, and why doing this
// generically that way would mean naming every bound string in the app.
//
// So this sets the SELECTION and lets the widget do the deleting. A selection
// is not text, so the re-seed above never fires on it; the widget's own
// TextBackspace sees a selection and deletes it, pushes the undo snapshot, and
// writes the result back to the bound string. One consequence worth stating:
// this must run BEFORE the pane builds its UI, since the widget consumes the
// same keypress later in the same frame.
//
// Cmd+Backspace is deliberately NOT in the key table. An explicit chord on
// BACKSPACE would claim the key (suppress_permissive_duplicates in
// input_system.h) and suppress the plain TextBackspace binding — the very one
// this relies on to perform the delete. So the modifier is read off the key
// state instead, and reads Ctrl as well as Cmd for the reason keys.h gives.
// ---------------------------------------------------------------------------

#include <string>

#include "../keys.h"
#include "keyboard_focus.h"
#include "ui_imports.h"

namespace ecs {

struct TextEditChordsSystem : afterhours::System<> {
    bool should_iterate() const override { return false; }

    void once(float) override {
        if (!hanabi::keys::cmd_or_ctrl_down()) return;
        if (!hanabi::keys::pressed(hanabi::keys::kBackspace)) return;

        auto* st = focused_text_field();
        if (!st) return;
        // A selection already stands in for the range to delete; macOS deletes
        // it and nothing more.
        if (st->has_selection()) return;

        const std::string text = st->text();
        const size_t caret = std::min(st->cursor_position, text.size());
        if (caret == 0) return;

        // Back to the start of the line, not the start of the field. The two
        // are the same in a single-line composer; written this way so it stays
        // right if the field ever grows a second line.
        const size_t nl = text.rfind('\n', caret - 1);
        const size_t line_start = (nl == std::string::npos) ? 0 : nl + 1;
        if (line_start >= caret) return;

        st->selection_anchor = line_start;
        st->cursor_position = caret;
    }
};

}  // namespace ecs
