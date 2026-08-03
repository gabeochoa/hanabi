// src/api/textinput_filter.h
#pragma once
// Shared predicate: is this codepoint something the composer should TYPE into
// the field (vs a control code that should be handled as an action or ignored)?
//
// WHY THIS EXISTS: on macOS the sokol backend emits a CHAR event for keys whose
// NSEvent.characters is non-empty — including BACKSPACE (U+007F DEL), ENTER, and
// other control keys. afterhours' insert_char only rejects codepoints < 32, so
// 0x7F (127) slips through and gets inserted as a DEL glyph — the "backspace
// adds a space/box" bug. This predicate is the single source of truth for
// "typable char" and is applied wherever hanabi drains typed characters.
//
// Rule: accept printable Unicode (>= 0x20, i.e. space and up) EXCEPT the DEL
// control (0x7F). Tab (0x09) is allowed because insert_char allows it. Reject
// everything else (< 0x20 control codes, and 0x7F).

namespace hanabi {
inline bool is_typable_char(int codepoint) {
    if (codepoint == 0x09) return true;   // tab (insert_char permits it)
    if (codepoint < 0x20) return false;   // C0 control codes (incl. BS, CR, ESC)
    if (codepoint == 0x7F) return false;  // DEL — macOS backspace CHAR
    return true;                          // space (0x20) and up = printable
}
}  // namespace hanabi
