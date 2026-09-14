#pragma once

// Hanabi color theme.
//
// All colors flow from a SINGLE swappable token set (`Tokens`). Two variants
// ship — a dark palette (the anchor / default) and a light palette — and the
// active set is chosen at runtime via theme::set_mode(). The token values
// mirror the design mock's CSS custom properties one-to-one.
//
// The historical inline color names (theme::SIDEBAR_BG etc.) are preserved as
// thin accessors that read the active token set, so existing call sites keep
// working while the whole palette becomes runtime-swappable.

#include "../util/atlas_guard.h"
#include "../util/prof.h"
#include "../util/text_cache.h"
#include "font_system.h"
#include <afterhours/src/drawing_helpers.h>

#include <bitset>
#include <cmath>
#include <iterator>
#include <string>

namespace theme {

using Color = afterhours::Color;

enum class Mode { Dark, Light };

// The complete token set. Adding a color means adding one field here and one
// value in each of the two palettes below — nothing else in the app hardcodes
// a color.
struct Tokens {
    // Window chrome
    Color window_bg;
    Color sidebar_bg;
    Color panel_bg;    // transcript / main
    Color panel_bg_2;  // search field, tab hover surfaces
    Color border;
    Color border_soft;
    // The sidebar's section-header strip (VIEWS / FOLDERS). A named surface
    // rather than a reuse of panel_bg_2, because the strip and the search field
    // are measurably different colours on the reference.
    Color section_header_bg;
    // The single hairline parting the sidebar from the main pane. Distinct from
    // `border`: the vertical rail divider is measurably darker than the
    // horizontal rules the rest of the app draws with border.
    Color divider;

    // Text
    Color text_primary;
    Color text_secondary;
    Color text_faint;
    Color empty_state_text;

    // Accent / interactive
    Color accent;
    Color accent_soft;
    // Behind a find-in-conversation match. Warm rather than accent-coloured so
    // a highlighted run cannot be mistaken for a link or a selected row.
    Color find_match;
    // Behind selected text. Blue-grey, the desktop convention, and distinct
    // from the warm find band so the two read differently when both are up.
    Color selection_bg;
    // A work-tracker id that opens somewhere. Brighter than accent so an id
    // reads as a link against body text rather than as emphasis, and it is the
    // only underlined run in the transcript.
    Color link;
    Color button_primary;
    Color button_secondary;
    Color hover_bg;
    Color selected_bg;
    Color row_separator;
    Color focus_ring;

    // Disabled
    Color disabled_bg;
    Color disabled_text;
    Color destructive;

    // Attention dot
    Color dot;

    // Tag chips (fg over soft bg)
    Color tag_blocked_fg, tag_blocked_bg;
    Color tag_ready_fg, tag_ready_bg;
    Color tag_done_fg, tag_done_bg;

    // On-BACKGROUND status hues (section headers, sidebar status glyphs) — a
    // saturated red / green that reads as its HUE on the pane/sidebar bg. These
    // are DISTINCT from tag_*_fg, which is the on-PILL chip-text color: on light
    // the chip text is a very dark maroon (for contrast on a pale-pink pill),
    // which reads as near-black when drawn on the light background instead. So a
    // header/glyph needs its own mid-saturation hue that carries "red/green =
    // urgent/ready" on both themes.
    Color status_blocked, status_review;

    // Message role accents (transcript)
    Color role_user, role_assistant, role_system, role_tool;
    Color bubble_user_bg, bubble_assistant_bg, bubble_other_bg;

    // Session-list status pips (legacy)
    Color status_active, status_idle, status_archived;

    // Fenced code blocks. A syntax palette is not the UI palette: it has to
    // separate six roles from each other AND stay legible on the sunken code
    // surface, so these are their own tokens rather than reused accents. Plain
    // code keeps text_secondary, so an uncoloured language looks exactly as it
    // did before.
    Color syntax_keyword, syntax_type, syntax_string, syntax_comment,
        syntax_number, syntax_punct;
    // The fence's own surface. It is NOT window_bg, which is what the block
    // used to fill with: inside a bubble that fills with something else, a
    // window-coloured fence punches a hole through the bubble and reads as two
    // bubbles with a gap. Puffin gives the fence its own token
    // (PuffinTheme.Color.codeBackground -> the theme's syntax.background) and
    // draws it a step DARKER than whatever it sits on.
    Color code_bg;
};

// -------- Dark palette (anchor / default) --------
//
// The reference's DEFAULT theme, the reference's default dark theme: the theme a fresh install
// wears (`ThemeManager.init` reads the `theme` default and falls back to that
// name). Read from the theme table at the pinned source (fb84b1d4,
// Sources/Models/Models.swift, `the theme table's dark default`), and confirmed against
// the reference capture: the harness PNGs carry the display's ICC profile, and
// converted to sRGB their two dominant surfaces are #0C141D and #171F2A --
// `nightSky` and `headerBg` to the byte.
//
// The reference declares ELEVEN colours per theme and derives everything else
// (its theme file): hairline = mutedText at 20 %, a selected row = accent at
// 25 %, hover = text at 9 %, a tinted pill = a hue at 16 % -- so every derived
// token below is that formula pre-blended over the surface it sits on (the
// renderer cannot blend, gap #13), and the ones that stay translucent keep the
// reference's alpha.
//
//   nightSky  #0D141D  window_bg / panel_bg -- the window and the transcript
//   headerBg  #171F2A  sidebar_bg / section_header_bg -- the rail, the settings
//                      pane list, the tab strip; panel_bg_2 (raised: the search
//                      field's ground) is text at 6 % over it (SearchFieldChrome)
//   naviBlue  #3B9EDB  accent (and everything that takes the accent hue)
//   lightText #F7F9FC  text_primary
//   mutedText #8B9BA5  text_secondary; text_faint is the same ink, and the
//                      hairline is 20 % of it
//   errorRed  #F14E53 / faroreGreen #3FBF7F / triforceGold #FC6C05 -- the
//                      status hues, as the reference draws them on the plane
//   sheikahSlate #1E2733 agent bubble, kokiriGreen #16323F user bubble
//   syntax.background #04070C  code_bg
//
// Every byte below is the reference's declared value or its exact recipe.
// Where a recipe lands under the 4.5:1 small-text bar (the reference's own
// tinted pill puts danger text at 3.9:1 on the dark rail; on the light default
// the pills land at 4.1-4.5:1), test_theme_contrast REPORTS it as a diagnostic
// and does not fail: matching the reference is the accepted target, and a
// colour changed to satisfy an older bar would be a colour the reference does
// not have. docs/sidebar-parity.md lists those readings.
inline const Tokens kDark = {
    /*window_bg*/ {13, 20, 29, 255},   // #0D141D  nightSky
    /*sidebar_bg*/ {23, 31, 42, 255},  // #171F2A  headerBg
    /*panel_bg*/ {13, 20, 29, 255},    // #0D141D  the transcript is the window
    /*panel_bg_2*/ {36, 44, 55, 255},  // #242C37  text 6 % on headerBg: the search field's ground (capture #232A35)
    /*border*/ {38, 47, 56, 255},      // #262F38  hairline: mutedText 20 % on nightSky
    /*border_soft*/ {139, 155, 165, 51},  // the same hairline, kept translucent
    /*section_header_bg*/ {23, 31, 42, 255},  // #171F2A  headerBg
    /*divider*/ {38, 47, 56, 255},            // #262F38  the rail's hairline

    /*text_primary*/ {247, 249, 252, 255},   // #F7F9FC  lightText
    /*text_secondary*/ {139, 155, 165, 255}, // #8B9BA5  mutedText
    /*text_faint*/ {139, 155, 165, 255},     // the reference has one muted ink
    /*empty_state_text*/ {139, 155, 165, 255},

    /*accent*/ {59, 158, 219, 255},       // #3B9EDB  naviBlue
    /*accent_soft*/ {59, 158, 219, 41},   // 16 % -- the reference's tinted pill
    /*find_match*/ {252, 108, 5, 90},     // triforceGold band, hanabi's alpha
    /*selection_bg*/ {59, 158, 219, 96},
    /*link*/ {59, 158, 219, 255},         // the accent IS the link colour
    /*button_primary*/ {59, 158, 219, 255},
    /*button_secondary*/ {30, 39, 51, 255},  // #1E2733  sheikahSlate
    /*hover_bg*/ {247, 249, 252, 23},     // 9 %  -- HoverHighlight.hoverOpacity
    /*selected_bg*/ {32, 63, 86, 255},    // #203F56  accent 25 % on headerBg (capture: #223F56)
    /*row_separator*/ {38, 47, 56, 255},  // the hairline
    /*focus_ring*/ {59, 158, 219, 255},

    /*disabled_bg*/ {30, 39, 51, 255},
    /*disabled_text*/ {139, 155, 165, 160},
    /*destructive*/ {241, 78, 83, 255},   // #F14E53  errorRed

    /*dot*/ {59, 158, 219, 255},

    // A tinted pill, the reference's recipe exactly: the hue at 16 %
    // (ToggleChip.Tint.onFill) under the hue as its text. Contrast on the
    // pill is a diagnostic the tests REPORT, not a reason to change the byte.
    /*tag_blocked_fg*/ {241, 78, 83, 255},    // errorRed
    /*tag_blocked_bg*/ {241, 78, 83, 41},
    /*tag_ready_fg*/ {63, 191, 127, 255},     // faroreGreen
    /*tag_ready_bg*/ {63, 191, 127, 41},
    // DONE is a SETTLED state: the muted ink, not a hue.
    /*tag_done_fg*/ {139, 155, 165, 255},     // mutedText
    /*tag_done_bg*/ {139, 155, 165, 41},

    /*status_blocked*/ {241, 78, 83, 255},    // errorRed  (danger)
    /*status_review*/ {63, 191, 127, 255},    // faroreGreen (success)

    /*role_user*/ {59, 158, 219, 255},
    /*role_assistant*/ {63, 191, 127, 255},
    /*role_system*/ {252, 108, 5, 255},       // triforceGold (highlight)
    /*role_tool*/ {139, 155, 165, 255},
    /*bubble_user_bg*/ {22, 50, 63, 255},       // #16323F  kokiriGreen
    /*bubble_assistant_bg*/ {30, 39, 51, 255},  // #1E2733  sheikahSlate
    /*bubble_other_bg*/ {23, 31, 42, 255},      // headerBg

    /*status_active*/ {63, 191, 127, 255},
    /*status_idle*/ {252, 108, 5, 255},
    /*status_archived*/ {139, 155, 165, 255},

    // Syntax: the reference's declared `.night` palette (SyntaxPalette.night),
    // which its derivation keeps when it already clears the code surface.
    /*syntax_keyword*/ {199, 146, 234, 255},  // #C792EA
    /*syntax_type*/ {92, 198, 240, 255},      // #5CC6F0
    /*syntax_string*/ {127, 217, 140, 255},   // #7FD98C
    /*syntax_comment*/ {139, 155, 165, 255},  // #8B9BA5
    /*syntax_number*/ {245, 147, 86, 255},    // #F59356
    /*syntax_punct*/ {139, 155, 165, 255},
    // LAST: positional initializer, see the field order in Tokens.
    /*code_bg*/ {4, 7, 12, 255},              // #04070C  syntax.background
};

// -------- Light palette --------
//
// The reference's light default, the reference's default light theme (`the theme table's light default`, the
// second of its ten named themes and the pale twin of Night). Same eleven
// colours, same derivations, read from the same pinned source. The reference
// does not follow the system appearance -- a reader picks a theme by name --
// so hanabi's Light MODE wears Day the way Dark wears Night.
//
//   nightSky  #F7F9FC  window_bg / panel_bg
//   headerBg  #FFFFFF  sidebar_bg / section_header_bg; panel_bg_2 = text 6 % over it
//   naviBlue  #0B5C8A  accent
//   lightText #121417  text_primary;  mutedText #5C6B73  text_secondary
//   errorRed  #C1272D / faroreGreen #1E7A4C / triforceGold #8A5A00
//   sheikahSlate #ECEFF3 agent bubble, kokiriGreen #DCEEF8 user bubble
//   syntax.background #D8DFE8  code_bg
inline const Tokens kLight = {
    /*window_bg*/ {247, 249, 252, 255},   // #F7F9FC  nightSky
    /*sidebar_bg*/ {255, 255, 255, 255},  // #FFFFFF  headerBg
    /*panel_bg*/ {247, 249, 252, 255},    // #F7F9FC
    /*panel_bg_2*/ {241, 241, 241, 255},  // #F1F1F1  text 6 % on headerBg: the search field's ground
    /*border*/ {216, 221, 225, 255},      // #D8DDE1  mutedText 20 % on nightSky
    /*border_soft*/ {92, 107, 115, 51},
    /*section_header_bg*/ {255, 255, 255, 255},
    /*divider*/ {216, 221, 225, 255},

    /*text_primary*/ {18, 20, 23, 255},      // #121417  lightText
    /*text_secondary*/ {92, 107, 115, 255},  // #5C6B73  mutedText
    /*text_faint*/ {92, 107, 115, 255},
    /*empty_state_text*/ {92, 107, 115, 255},

    /*accent*/ {11, 92, 138, 255},        // #0B5C8A  naviBlue
    /*accent_soft*/ {11, 92, 138, 41},
    /*find_match*/ {138, 90, 0, 120},     // triforceGold band
    /*selection_bg*/ {11, 92, 138, 70},
    /*link*/ {11, 92, 138, 255},
    /*button_primary*/ {11, 92, 138, 255},
    /*button_secondary*/ {236, 239, 243, 255},  // #ECEFF3  sheikahSlate
    /*hover_bg*/ {18, 20, 23, 23},        // 9 % of the text
    /*selected_bg*/ {194, 214, 226, 255}, // #C2D6E2  accent 25 % on headerBg
    /*row_separator*/ {216, 221, 225, 255},
    /*focus_ring*/ {11, 92, 138, 255},

    /*disabled_bg*/ {236, 239, 243, 255},
    /*disabled_text*/ {92, 107, 115, 160},
    /*destructive*/ {193, 39, 45, 255},   // #C1272D  errorRed

    /*dot*/ {11, 92, 138, 255},

    // Tinted pills, the reference's recipe: the hue at 16 % under the hue.
    /*tag_blocked_fg*/ {193, 39, 45, 255},    // errorRed
    /*tag_blocked_bg*/ {193, 39, 45, 41},
    /*tag_ready_fg*/ {30, 122, 76, 255},      // faroreGreen
    /*tag_ready_bg*/ {30, 122, 76, 41},
    /*tag_done_fg*/ {92, 107, 115, 255},      // mutedText
    /*tag_done_bg*/ {92, 107, 115, 41},

    /*status_blocked*/ {193, 39, 45, 255},   // errorRed
    /*status_review*/ {30, 122, 76, 255},    // faroreGreen

    /*role_user*/ {11, 92, 138, 255},
    /*role_assistant*/ {30, 122, 76, 255},
    /*role_system*/ {138, 90, 0, 255},       // triforceGold
    /*role_tool*/ {92, 107, 115, 255},
    /*bubble_user_bg*/ {220, 238, 248, 255},       // #DCEEF8  kokiriGreen
    /*bubble_assistant_bg*/ {236, 239, 243, 255},  // #ECEFF3  sheikahSlate
    /*bubble_other_bg*/ {255, 255, 255, 255},

    /*status_active*/ {30, 122, 76, 255},
    /*status_idle*/ {138, 90, 0, 255},
    /*status_archived*/ {92, 107, 115, 255},

    // Syntax: the reference's declared `.day` palette.
    /*syntax_keyword*/ {122, 33, 168, 255},   // #7A21A8
    /*syntax_type*/ {11, 92, 138, 255},       // #0B5C8A
    /*syntax_string*/ {26, 99, 56, 255},      // #1A6338
    /*syntax_comment*/ {74, 88, 96, 255},     // #4A5860
    /*syntax_number*/ {158, 54, 0, 255},      // #9E3600
    /*syntax_punct*/ {92, 107, 115, 255},
    // LAST -- see the dark palette's note.
    /*code_bg*/ {216, 223, 232, 255},         // #D8DFE8  syntax.background
};

// Active token set (mutable, swapped at runtime). Defaults to dark.
inline Mode g_mode = Mode::Dark;
inline Tokens t = kDark;

// ---------------------------------------------------------------------------
// Custom colours — the user's edits to a small set of NAMED tokens, layered
// over whichever palette is active.
//
// A token is edited by picking a named swatch, not by typing a hex value, and
// every swatch carries TWO colours: one for dark, one for light. That is the
// whole reason this is not a hex field. The shipped accent is already two
// different blues — {90,128,255} on dark, the deeper {46,90,236} on light —
// because one colour that reads well on a near-black pane is washed out on
// white. Letting a single RGB win on both palettes is exactly how the light
// theme shipped muddy the first time.
//
// Only tokens that DECORATE are editable: the accent family and the find
// highlight. Surfaces and text are not on offer, so no choice here can leave a
// pane without contrast.
//
// The edits live in this pair of choice keys rather than in a copy of the
// Tokens struct, so a swatch added or corrected in a later build reaches
// people who already made a choice, instead of freezing whatever hex their
// settings file happened to capture.
// ---------------------------------------------------------------------------
struct Swatch {
    const char* key;
    const char* label;
    Color dark;
    Color light;
};

// Accent: buttons, the focus ring, links, the attention dot.
inline constexpr Swatch kAccentSwatches[] = {
    {"violet", "Violet", {166, 128, 255, 255}, {104, 58, 214, 255}},
    {"green", "Green", {96, 200, 140, 255}, {22, 118, 72, 255}},
    {"amber", "Amber", {235, 170, 66, 255}, {166, 102, 8, 255}},
};

// Find highlight: the band behind a find-in-conversation match. Warm by
// default, and every alternative stays a BAND — the palette's own alpha is
// kept, only the hue is replaced, so a match can never turn into a solid block
// that hides the text under it.
inline constexpr Swatch kHighlightSwatches[] = {
    {"green", "Green", {120, 210, 140, 255}, {96, 200, 130, 255}},
    {"pink", "Pink", {240, 130, 190, 255}, {240, 140, 190, 255}},
    {"blue", "Blue", {110, 170, 255, 255}, {120, 175, 255, 255}},
};

// "default" is the absence of an edit rather than a swatch of its own: the
// palette's own colour is left alone, so correcting a palette reaches everyone
// who never chose anything.
inline constexpr const char* kDefaultChoice = "default";

inline std::string g_accent_choice = kDefaultChoice;
inline std::string g_highlight_choice = kDefaultChoice;

inline const Swatch* find_swatch(const Swatch* list, size_t n,
                                 const std::string& key) {
    for (size_t i = 0; i < n; ++i)
        if (key == list[i].key) return &list[i];
    // Unknown (a hand-edited settings file, or a swatch dropped in a later
    // build) reads as no edit at all rather than as a crash or a black UI.
    return nullptr;
}

inline const Swatch* accent_swatch() {
    return find_swatch(kAccentSwatches, std::size(kAccentSwatches),
                       g_accent_choice);
}
inline const Swatch* highlight_swatch() {
    return find_swatch(kHighlightSwatches, std::size(kHighlightSwatches),
                       g_highlight_choice);
}

// Replace a token's HUE and keep its own alpha: accent_soft is a 38/255 wash
// and find_match a 90/255 band, and those alphas are what make them washes
// rather than slabs.
inline void set_hue(Color& dst, Color hue) {
    dst = Color{hue.r, hue.g, hue.b, dst.a};
}

// Re-layer the custom colours over the freshly-loaded palette. Called by
// set_mode, so a theme switch never drops someone's accent.
inline void apply_custom() {
    // Start from the palette every time: "default" for the accent or the
    // highlight means the palette's own hue, not whatever swatch was last
    // chosen. Without this reset a swatch's tint survived a return to
    // default until the next mode switch (the theme-token unit caught it).
    const Tokens& base = (g_mode == Mode::Light) ? kLight : kDark;
    t.accent = base.accent;
    t.accent_soft = base.accent_soft;
    t.button_primary = base.button_primary;
    t.focus_ring = base.focus_ring;
    t.dot = base.dot;
    t.find_match = base.find_match;
    if (const Swatch* a = accent_swatch()) {
        const Color ac = (g_mode == Mode::Light) ? a->light : a->dark;
        set_hue(t.accent, ac);
        set_hue(t.accent_soft, ac);
        set_hue(t.button_primary, ac);
        set_hue(t.focus_ring, ac);
        set_hue(t.dot, ac);
    }
    if (const Swatch* h = highlight_swatch())
        set_hue(t.find_match, (g_mode == Mode::Light) ? h->light : h->dark);
}

inline void set_mode(Mode m) {
    g_mode = m;
    t = (m == Mode::Light) ? kLight : kDark;
    apply_custom();
}

inline void set_accent_choice(const std::string& key) {
    g_accent_choice = key;
    apply_custom();
}
inline void set_highlight_choice(const std::string& key) {
    g_highlight_choice = key;
    apply_custom();
}
inline const std::string& accent_choice() { return g_accent_choice; }
inline const std::string& highlight_choice() { return g_highlight_choice; }

inline Mode mode() { return g_mode; }
inline void toggle_mode() {
    set_mode(g_mode == Mode::Dark ? Mode::Light : Mode::Dark);
}

// ---------------------------------------------------------------------------
// Named accessors. Every color used by the app is exposed as a function that
// reads the ACTIVE token set, so a runtime theme swap is reflected everywhere
// without touching any call site. Call as `theme::sidebar_bg()`.
// ---------------------------------------------------------------------------
// Alpha-composite `fg` (which may be translucent) OVER opaque `bg`, returning
// an OPAQUE color. Needed because the sokol_gl default pipeline used for UI
// rect fills has alpha blending disabled (afterhours gap #13): a low-alpha
// custom_background renders fully opaque instead of tinting. Pre-blending here
// gives us the color that real src-over blending would have produced, so a
// "soft tint" chip reads as a subtle pill (not a saturated solid block).
inline Color over(Color fg, Color bg) {
    const float a = fg.a / 255.0f;
    auto mix = [a](unsigned char f, unsigned char b) -> unsigned char {
        float v = f * a + b * (1.0f - a);
        if (v < 0.0f) v = 0.0f;
        if (v > 255.0f) v = 255.0f;
        return static_cast<unsigned char>(v + 0.5f);
    };
    return Color{mix(fg.r, bg.r), mix(fg.g, bg.g), mix(fg.b, bg.b), 255};
}

inline Color window_bg() { return t.window_bg; }
inline Color sidebar_bg() { return t.sidebar_bg; }
inline Color panel_bg() { return t.panel_bg; }
inline Color panel_bg_2() { return t.panel_bg_2; }
inline Color border() { return t.border; }
inline Color border_soft() { return t.border_soft; }
inline Color section_header_bg() { return t.section_header_bg; }
inline Color divider() { return t.divider; }
inline Color text_primary() { return t.text_primary; }
inline Color text_secondary() { return t.text_secondary; }
inline Color text_faint() { return t.text_faint; }

inline bool is_dark() {
    const Color bg = t.window_bg;
    return (static_cast<int>(bg.r) + bg.g + bg.b) < 384;
}

inline Color ask_action_fill() { return t.panel_bg_2; }

inline Color ask_action_disabled_fill() {
    return is_dark() ? t.panel_bg_2 : Color{214, 214, 221, 255};
}

inline Color ask_action_disabled_ink() {
    return is_dark() ? Color{150, 150, 162, 255} : Color{72, 72, 82, 255};
}

inline Color ask_action_enabled_ink() {
    return is_dark() ? t.text_primary : t.text_secondary;
}

inline Color ask_caveat_ink() { return t.text_secondary; }
inline Color empty_state_text() { return t.empty_state_text; }
inline Color accent() { return t.accent; }
inline Color accent_soft() { return t.accent_soft; }
inline Color find_match() { return t.find_match; }
inline Color selection_bg() { return t.selection_bg; }
inline Color link() { return t.link; }
inline Color button_primary() { return t.button_primary; }
inline Color button_secondary() { return t.button_secondary; }
inline Color hover_bg() { return t.hover_bg; }
inline Color selected_bg() { return t.selected_bg; }

// Hover fill PRE-COMPOSITED over a given backdrop. The hover_bg token is a
// low-alpha wash (e.g. white @ a=16), but the UI rect fill can't alpha-blend
// (afterhours gap #13) — passing the raw token makes the sgl default pipeline
// render it as a harsh near-opaque flash instead of a subtle highlight. So a
// hovered surface must pass hover_over(<its own background>) to get the
// intended subtle lift. Every hoverable surface should use this with the SAME
// color it fills with normally (sidebar rows → sidebar_bg, digest cards →
// panel_bg_2, etc.) so the hover reads as a gentle brighten of that surface.
inline Color hover_over(Color backdrop) { return over(hover_bg(), backdrop); }
inline Color row_separator() { return t.row_separator; }
inline Color focus_ring() { return t.focus_ring; }
inline Color disabled_bg() { return t.disabled_bg; }
inline Color disabled_text() { return t.disabled_text; }
inline Color destructive() { return t.destructive; }
inline Color dot() { return t.dot; }
inline Color role_user() { return t.role_user; }
inline Color role_assistant() { return t.role_assistant; }
inline Color role_system() { return t.role_system; }
inline Color role_tool() { return t.role_tool; }
inline Color bubble_user_bg() { return t.bubble_user_bg; }
inline Color bubble_assistant_bg() { return t.bubble_assistant_bg; }
inline Color bubble_other_bg() { return t.bubble_other_bg; }
inline Color code_bg() { return t.code_bg; }
inline Color syntax_keyword() { return t.syntax_keyword; }
inline Color syntax_type() { return t.syntax_type; }
inline Color syntax_string() { return t.syntax_string; }
inline Color syntax_comment() { return t.syntax_comment; }
inline Color syntax_number() { return t.syntax_number; }
inline Color syntax_punct() { return t.syntax_punct; }
inline Color status_active() { return t.status_active; }
inline Color status_idle() { return t.status_idle; }
inline Color status_archived() { return t.status_archived; }
inline Color status_blocked() { return t.status_blocked; }
inline Color status_review() { return t.status_review; }

// Tag chip colors.
inline Color tag_blocked_fg() { return t.tag_blocked_fg; }
inline Color tag_blocked_bg() { return t.tag_blocked_bg; }
inline Color tag_ready_fg() { return t.tag_ready_fg; }
inline Color tag_ready_bg() { return t.tag_ready_bg; }
inline Color tag_done_fg() { return t.tag_done_fg; }
inline Color tag_done_bg() { return t.tag_done_bg; }

// Modal/overlay scrim — the dim wash drawn behind settings/composer sheets.
// Pre-blend it over the backdrop with over() since the fill can't alpha-blend
// (gap #13): e.g. with_custom_background(theme::over(theme::scrim(), bg)).
inline Color scrim() { return Color{0, 0, 0, 140}; }

}  // namespace theme

// ---------------------------------------------------------------------------
// Typography — the allowed native point-size ramp. The role values match the
// product spec; set_point_scale converts them to the selected face's fontstash
// pixel height without changing layout geometry.
namespace theme {
namespace type {
constexpr float H1_PT = 20.0f;
constexpr float SPOTLIGHT_PT = 17.0f;
constexpr float LG_PT = 14.0f;
constexpr float TITLE_PT = 13.5f;
constexpr float BODY_PT = 13.0f;
constexpr float ROW_PT = 12.5f;
constexpr float LIST_ROW_PT = 13.0f;
constexpr float MD_PT = 12.0f;
constexpr float SUBROW_PT = 11.5f;
constexpr float SM_PT = 11.0f;
constexpr float LABEL_PT = 10.5f;
constexpr float XS_PT = 10.0f;
constexpr float CHIP_PT = 9.5f;
constexpr float MICRO_PT = 9.0f;
// The reference's "nano" face (PuffinTheme.Size.nano = 8): capsule words,
// the resolved value beside a `default` row, footnotes.
constexpr float NANO_PT = 8.0f;
inline float H1 = H1_PT;
inline float SPOTLIGHT = SPOTLIGHT_PT;
inline float LG = LG_PT;
inline float TITLE = TITLE_PT;
inline float BODY = BODY_PT;
inline float ROW = ROW_PT;
inline float LIST_ROW = LIST_ROW_PT;
inline float MD = MD_PT;
inline float SUBROW = SUBROW_PT;
inline float SM = SM_PT;
inline float LABEL = LABEL_PT;
inline float XS = XS_PT;
inline float CHIP = CHIP_PT;
inline float MICRO = MICRO_PT;
inline float NANO = NANO_PT;
constexpr auto EMPHASIS = afterhours::colors::FontWeight::SemiBold;
inline void set_point_scale(float scale) {
    H1 = H1_PT * scale;
    SPOTLIGHT = SPOTLIGHT_PT * scale;
    LG = LG_PT * scale;
    TITLE = TITLE_PT * scale;
    BODY = BODY_PT * scale;
    ROW = ROW_PT * scale;
    LIST_ROW = LIST_ROW_PT * scale;
    MD = MD_PT * scale;
    SUBROW = SUBROW_PT * scale;
    SM = SM_PT * scale;
    LABEL = LABEL_PT * scale;
    XS = XS_PT * scale;
    CHIP = CHIP_PT * scale;
    MICRO = MICRO_PT * scale;
    NANO = NANO_PT * scale;
}
}  // namespace type

namespace chrome {
constexpr float SPACE_1 = 4.0f;
constexpr float SPACE_2 = 8.0f;
constexpr float SPACE_3 = 12.0f;
constexpr float SPACE_4 = 16.0f;
constexpr float SPACE_6 = 24.0f;
constexpr float HIT = 28.0f;
constexpr float ROW = 32.0f;
constexpr float RADIUS = 6.0f;
constexpr float HAIRLINE = 1.0f;

// The window's surfaces, BY ROLE, read from the active palette -- one source
// of colour, so a theme, a mode switch or a reader's accent reaches the rail,
// the tab strip and the settings list the same frame it reaches the transcript.
// These used to be a second, hardcoded ladder (titlebar < sidebar < content <
// raised in luminance); the reference has no such ladder: its rail (headerBg)
// is LIGHTER than its window (nightSky) in the dark default, and structure is
// carried by the hairline and the two named surfaces.
inline Color titlebar() { return window_bg(); }
inline Color sidebar() { return sidebar_bg(); }
inline Color content() { return panel_bg(); }
inline Color raised() { return panel_bg_2(); }
inline Color divider() { return theme::divider(); }

// A selected row: the accent at 25 % over whatever it sits on -- the
// reference's `HoverHighlight.selectedOpacity`, one number for both themes.
inline Color selected_on(Color backdrop) {
    Color tint = accent();
    tint.a = 64;
    return over(tint, backdrop);
}

inline Color attention_fill(Color backdrop) {
    Color tint = destructive();
    tint.a = mode() == Mode::Dark ? 28 : 20;
    return over(tint, backdrop);
}
}  // namespace chrome

// Real rendered width (logical px) of `s` at font size `px`, measured against
// the SAME active font draw_text uses (fontstash bounds). Replaces the
// per-glyph width ESTIMATES that left trailing gaps — e.g. a fixed-width
// right-aligned time column meant the star sat flush to the COLUMN edge, not
// the text, and the status activity dot floated in the gutter left of the
// count. Falls back to a conservative per-glyph estimate only if the font
// context isn't ready yet (very first frame / headless before font load).
// WCAG relative luminance and contrast, the reference's own definitions
// (PuffinTheme.luminance / contrast): sRGB channels linearised, 0.2126 /
// 0.7152 / 0.0722, (L1 + 0.05) / (L2 + 0.05).
inline double srgb_channel(unsigned char v8) {
    const double v = static_cast<double>(v8) / 255.0;
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}
inline double relative_luminance(Color c) {
    return 0.2126 * srgb_channel(c.r) + 0.7152 * srgb_channel(c.g) +
           0.0722 * srgb_channel(c.b);
}
inline double contrast_ratio(Color a, Color b) {
    const double la = relative_luminance(a);
    const double lb = relative_luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// A colour as a fill under WHITE text: the same hue, dimmed only as far as
// white needs -- the reference's `deepenedForWhiteText`. Its recipe exactly:
// leave the colour alone when white already clears 4.5:1 on it; otherwise
// scale all three channels by one factor (brightness = the peak channel) and
// binary-search that peak, 24 steps, for the brightest fill on which white
// reads 4.6:1 (aimed a hair past the bar because the fill is drawn in 8 bits).
// One function for every filled pill, so a theme whose attention colour is
// already dark enough is left exactly as declared.
inline Color deepened_for_white_text(Color color) {
    constexpr double kReadable = 4.5;
    const Color white{255, 255, 255, 255};
    if (contrast_ratio(color, white) >= kReadable) return color;
    const double aim = kReadable + 0.1;
    const double peak = static_cast<double>(std::max(color.r, std::max(color.g, color.b)));
    if (peak <= 0.0) return color;
    const auto dimmed = [&](double brightness) {
        const double k = brightness / peak;
        const auto ch = [k](unsigned char v) {
            return static_cast<unsigned char>(std::lround(static_cast<double>(v) * k));
        };
        return Color{ch(color.r), ch(color.g), ch(color.b), color.a};
    };
    double low = 0.0, high = peak;
    for (int i = 0; i < 24; ++i) {
        const double mid = (low + high) / 2.0;
        if (contrast_ratio(dimmed(mid), white) >= aim) low = mid; else high = mid;
    }
    return dimmed(low);
}

// The one colour a COUNT is badged on, whichever shelf row it sits on: the
// theme's attention colour, deepened for white digits -- the reference's shelf
// badge exactly (SmartViewSidebar `badgeView` -> `filledPill(Chrome.attention)`
// -> `deepenedForWhiteText`). Derived, not stored: the dark default's #FC6C05
// lands on #C35304 (the pinned capture's rail badge, ICC-converted, reads
// exactly that); the light default's #8A5A00 already clears white and is left
// as declared.
inline Color attention_badge() { return deepened_for_white_text(t.role_system); }

// Ink for text sitting ON a filled swatch: white unless the fill is light
// enough that white would wash out, then the darkest ink. Rec. 709 luma,
// the same rule the reference's filled pill follows (white on a deepened
// fill).
inline Color on_fill(Color fill) {
    const float luma = (0.2126f * static_cast<float>(fill.r) +
                        0.7152f * static_cast<float>(fill.g) +
                        0.0722f * static_cast<float>(fill.b)) /
                       255.0f;
    return luma > 0.62f ? Color{16, 16, 20, 255} : Color{255, 255, 255, 255};
}

inline float text_px(
    const char* s, float px,
    afterhours::colors::FontWeight weight =
        afterhours::colors::FontWeight::Regular) {
    if (!s || !*s) return 0.0f;
    constexpr std::size_t kAdvanceEntries = 1024;
    static hanabi::text::TextKeyCache<float> memo(kAdvanceEntries);
    const float weightKey = static_cast<float>(static_cast<int>(weight));
    if (const float* hit = memo.find(s, px, weightKey)) {
        hanabi::prof::tick("cache.advance_hit");
        return *hit;
    }
    hanabi::prof::tick("text.text_px_uncached");
    float w = hanabi::atlas::check(
        s, px, hanabi::fonts::measure_advance(s, px, weight));
    if (w > 0.0f) {
        memo.put(s, px, weightKey, w);
        hanabi::prof::gauge("cache.advance_entries", memo.size());
        return w;
    }
    return static_cast<float>(std::char_traits<char>::length(s)) * px * 0.5f;
}
inline float text_px(
    const std::string& s, float px,
    afterhours::colors::FontWeight weight =
        afterhours::colors::FontWeight::Regular) {
    return text_px(s.c_str(), px, weight);
}

// The one shared corner radius for chat surfaces (user prompt bubble + tool
// card) so they always match -- Gabe: "corners of my prompt must match the tool
// call". A pixel value, handed straight to with_corner_radius.
//
// This used to need a roundness_for_px() helper, because with_roundness takes a
// FRACTION of the short side: the same fraction gives a different corner on a
// 28px tool row than on a tall bubble, so matching them meant computing a
// fraction per element from its own dimensions -- dimensions the caller had to
// know, and sometimes guessed (one site passed `bodyH + 17.0f`). afterhours
// takes pixels directly now and resolves against the real rect, so the helper
// and the guessing are gone.
constexpr float kChatCorner = 5.0f;   // px -- subtle, modern, NOT a stadium

}  // namespace theme

// Layout constants referenced by the design-system presets.
namespace theme {
namespace layout {

// Rounded corners (all four).
inline const std::bitset<4> ROUNDED_CORNERS = std::bitset<4>(0b1111);

// Roundness (0.0 = square, 1.0 = pill).
constexpr float ROUNDNESS_BUTTON = 0.4f;
constexpr float ROUNDNESS_BADGE = 0.6f;
constexpr float ROUNDNESS_BOX = 0.3f;

// Row sizing.
constexpr int FILE_ROW_HEIGHT = 24;

}  // namespace layout
}  // namespace theme
