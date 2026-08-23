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

#include <afterhours/src/drawing_helpers.h>

#include <bitset>
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
};

// -------- Dark palette (anchor / default) --------
//
// ONE BACKGROUND. Every value below with a hex beside it was read off a real
// Puffin window with ~/w/vis/probe.py, not chosen. Puffin does not run a
// 3-step elevation stack: the sidebar, the main pane, the tab strip, the
// composer plane and even the composer's own input are all the SAME #171723,
// and structure is carried by hairlines and by two named surfaces rather than
// by tinting each plane a different value. The three-step ladder that used to
// live here (base 24 / sidebar 19 / raised 33 / overlay 42) is what made every
// side-by-side screenshot read as a different app at a glance.
//
//   flat plane   window_bg / sidebar_bg / panel_bg  #171723  everything
//   header strip section_header_bg                  #22222D  VIEWS / FOLDERS
//   selection    selected_bg                        #2E3A58  row + active tab
//   rail hairline divider                           #2A2A39  sidebar -> main
//   recessed     panel_bg_2                         #242430  search field
//
// panel_bg is now the same value as window_bg on purpose. It keeps its own
// token because it means "the content plane" and a future palette may part the
// two again; raised surfaces (cards, chips, the search pill) use panel_bg_2,
// which is what actually separates them from the plane now.
inline const Tokens kDark = {
    /*window_bg*/ {23, 23, 35, 255},   // #171723
    /*sidebar_bg*/ {23, 23, 35, 255},  // #171723 — no separate rail tint
    /*panel_bg*/ {23, 23, 35, 255},    // #171723 — the main pane is the window
    /*panel_bg_2*/ {36, 36, 48, 255},  // #242430 — search fill
    /*border*/ {62, 62, 72, 255},
    /*border_soft*/ {255, 255, 255, 20},
    /*section_header_bg*/ {34, 34, 45, 255},  // #22222D
    /*divider*/ {42, 42, 57, 255},            // #2A2A39

    /*text_primary*/ {224, 224, 230, 255},
    /*text_secondary*/ {142, 142, 154, 255},
    /*text_faint*/ {100, 100, 112, 255},
    /*empty_state_text*/ {112, 112, 124, 255},

    /*accent*/ {90, 128, 255, 255},
    /*accent_soft*/ {90, 128, 255, 38},
    /*find_match*/ {235, 180, 60, 90},
    /*selection_bg*/ {90, 128, 255, 96},
    /*link*/ {126, 166, 255, 255},
    /*button_primary*/ {90, 128, 255, 255},
    /*button_secondary*/ {58, 58, 66, 255},
    /*hover_bg*/ {255, 255, 255, 16},
    /*selected_bg*/ {46, 58, 88, 255},  // #2E3A58 — selected row + active tab
    /*row_separator*/ {46, 46, 54, 255},
    /*focus_ring*/ {90, 128, 255, 255},

    /*disabled_bg*/ {44, 44, 50, 255},
    /*disabled_text*/ {120, 120, 128, 255},
    /*destructive*/ {224, 88, 84, 255},

    /*dot*/ {90, 128, 255, 255},

    /*tag_blocked_fg*/ {255, 120, 120, 255},
    /*tag_blocked_bg*/ {255, 90, 90, 36},
    /*tag_ready_fg*/ {126, 210, 150, 255},
    /*tag_ready_bg*/ {120, 210, 140, 36},
    // DONE is a SETTLED/closed state, not an active/info one — so it reads as a
    // muted neutral slate, deliberately NOT the blue accent (which the eye maps
    // to "active/link"). Cool desaturated grey: present but visually receded.
    /*tag_done_fg*/ {158, 164, 178, 255},
    /*tag_done_bg*/ {150, 158, 176, 34},

    // On-background status hues (dark): the chip fg already reads well on dark,
    // so these match it — a bright red / green on the dark pane + sidebar.
    /*status_blocked*/ {255, 120, 120, 255},
    /*status_review*/ {126, 210, 150, 255},

    /*role_user*/ {90, 128, 255, 255},
    /*role_assistant*/ {126, 200, 140, 255},
    /*role_system*/ {180, 150, 90, 255},
    /*role_tool*/ {150, 130, 200, 255},
    // User bubble = a MUTED NEUTRAL grey (not a saturated blue tint) so it reads
    // as "your message" without competing with the ONE accent (transcript-only
    // token — the Navi-web-chat look: quiet grey right-aligned bubble).
    /*bubble_user_bg*/ {46, 46, 48, 255},
    /*bubble_assistant_bg*/ {34, 46, 40, 255},
    /*bubble_other_bg*/ {44, 44, 52, 255},

    /*status_active*/ {126, 200, 140, 255},
    /*status_idle*/ {180, 150, 90, 255},
    /*status_archived*/ {110, 110, 122, 255},

    // Syntax (dark): six hues that stay apart from each other on the sunken
    // window_bg the code block fills with, and clear of the blue accent so a
    // keyword never reads as a link.
    /*syntax_keyword*/ {198, 149, 234, 255},
    /*syntax_type*/ {126, 200, 200, 255},
    /*syntax_string*/ {152, 195, 121, 255},
    /*syntax_comment*/ {110, 112, 126, 255},
    /*syntax_number*/ {216, 168, 108, 255},
    /*syntax_punct*/ {150, 152, 166, 255},
};

// -------- Light palette --------
//
// LEFT ALONE, deliberately. Puffin has no light theme to measure and no light
// mode to switch into: it does not follow the system appearance at all. Its
// palette is one of ten NAMED themes (`LinkTheme` in
// fbobjc/Apps/Internal/Puffin/Sources/Models/Models.swift), chosen by the
// reader and stored under the `theme` default as a NAME — an unrecognised
// value there resolves to Hyrule, so `defaults write com.meta.puffin theme
// -string light` would not produce a light Puffin, it would produce Hyrule.
// The window chrome reads that same theme (`PuffinTheme.Chrome`), so every
// number in the dark palette above is the "Navi" preset's chrome, not a dark
// mode. Three of the ten presets are pale (Light World, Daylight, Puffin Day),
// but picking one of them as "the light Puffin" would be a choice, not a
// measurement — so nothing below changed. See the report / friction log.
//
// Independently tuned (NOT an inversion of dark). ELEVATION in light mode runs
// the other way: a faintly grey base with progressively WHITER raised surfaces.
//   L0 base    (window_bg)  {238,238,242} — recessed plane
//   L0 sidebar (sidebar_bg) {230,230,235} — chrome rail, slightly below base
//   L1 raised  (panel_bg)   {255,255,255} — cards / panels / active tab (pure white)
//   L2 overlay (panel_bg_2) {247,247,250} — search field, hover surfaces
// Text is tuned for real contrast on these surfaces (WCAG on white / #FFF):
//   text_primary   #21212B on #FFF  = 15.3:1   (title bar & body — was fine, kept)
//   text_secondary #55555F on #FFF  =  7.3:1    (was #78788 4 ≈ 3.7:1, too faint)
//   text_faint     #86868F on #FFF  =  3.4:1    (>=3:1 large/secondary target)
// Borders/dividers are darkened so they're visible on light surfaces. The
// digest cards fill with panel_bg_2 (247,247,250) — only ~8 levels below the
// white pane — so the 1px border is what actually defines each card edge. At
// the old {200,200,208} the border sat at ~1.66:1 vs the pane and read as
// near-invisible (defect #8); {176,176,190} lifts it to ~2.1:1 vs pane and
// ~2.0:1 vs the card fill so every card gets a crisp edge without looking heavy.
inline const Tokens kLight = {
    // True macOS light theme (was a muddy mid-grey that read as "dark with the
    // lights half-on", audit #2). Sidebar = near-white with a hairline divider;
    // canvas = a very light neutral (#f6f6f7); main pane = pure white; cards
    // sit on white with a crisp 1px border. This gives real light/dark polarity
    // instead of two greys.
    /*window_bg*/ {246, 246, 247, 255},   // canvas behind panels
    /*sidebar_bg*/ {251, 251, 252, 255},  // near-white sidebar
    /*panel_bg*/ {255, 255, 255, 255},    // main / transcript = pure white
    /*panel_bg_2*/ {242, 242, 245, 255},  // search field / recessed surfaces
    /*border*/ {209, 209, 216, 255},      // crisp hairline (was too dark 176)
    /*border_soft*/ {0, 0, 0, 22},
    // The two surfaces the dark palette gained for Puffin parity. These are
    // NOT derived from the dark values and NOT an inversion of them: Puffin has
    // no light theme to measure (see below), so each takes the light palette's
    // OWN existing value for the same role — the recessed surface for the
    // header strip, the hairline for the rail divider. No existing light token
    // moved.
    /*section_header_bg*/ {242, 242, 245, 255},
    /*divider*/ {209, 209, 216, 255},

    /*text_primary*/ {33, 33, 43, 255},
    /*text_secondary*/ {85, 85, 95, 255},
    // text_faint renders on BOTH the white pane and the recessed card fill
    // (panel_bg_2 = 242). The old {134,134,143} was 3.6:1 on white and only
    // 3.2:1 on the card — below the 4.5:1 small-text bar (defect: faint text
    // fails contrast on light). {110,110,122} lifts it to 5.0:1 (white) /
    // 4.5:1 (card) while staying clearly lighter than text_secondary (7.4:1).
    /*text_faint*/ {110, 110, 122, 255},
    // empty-state copy is large & centered but was 4.2:1 on white; nudged to
    // 4.9:1 so it clears 4.5:1 without going as dark as body text.
    /*empty_state_text*/ {112, 112, 124, 255},

    /*accent*/ {46, 90, 236, 255},
    /*accent_soft*/ {46, 90, 236, 34},
    /*find_match*/ {250, 205, 90, 170},
    /*selection_bg*/ {46, 90, 236, 70},
    /*link*/ {24, 70, 210, 255},
    /*button_primary*/ {46, 90, 236, 255},
    /*button_secondary*/ {224, 224, 230, 255},
    /*hover_bg*/ {0, 0, 0, 14},
    /*selected_bg*/ {205, 220, 255, 255},
    /*row_separator*/ {216, 216, 224, 255},
    /*focus_ring*/ {46, 90, 236, 255},

    /*disabled_bg*/ {228, 228, 232, 255},
    /*disabled_text*/ {158, 158, 168, 255},
    /*destructive*/ {196, 40, 40, 255},

    /*dot*/ {46, 90, 236, 255},

    // Chips: fg is a strong hue for legible labels, bg is a mid-alpha tint
    // pre-blended by over() onto the CARD surface (panel_bg_2, ~white). The
    // earlier low alphas (36-46) rendered the pills as near-white washes on the
    // light card (BLOCKED pale-pink, DONE pale-lavender) — the hue didn't read
    // and the small chip text landed at ~3.9:1 (defect #6). Deepened: alpha
    // ~115-135 so the pill carries a clear urgent-red / blue / green cast (still
    // a tint, not a solid block — pill vs card ~2:1), and the fg is darkened so
    // the small AA'd label clears 4.5:1 on its own pill. On the white card the
    // resulting pure fg-on-pill CR is BLOCKED 6.2 / DONE 6.2 / READY 5.4 — deep
    // enough that the tiny AA'd glyphs still clear 4.5:1 at their darkest.
    /*tag_blocked_fg*/ {96, 0, 0, 255},
    /*tag_blocked_bg*/ {210, 44, 44, 120},
    /*tag_ready_fg*/ {4, 60, 26, 255},
    /*tag_ready_bg*/ {30, 128, 60, 140},
    // DONE = settled/closed → muted neutral slate (NOT the blue accent). Deep
    // enough on the white card that the small AA'd label still clears ~4.5:1.
    /*tag_done_fg*/ {58, 66, 82, 255},
    /*tag_done_bg*/ {96, 104, 124, 120},

    // On-background status hues (light): tag_*_fg above is tuned as chip text on
    // a pale pill (a near-black maroon / deep green), which reads as ~black when
    // drawn straight on the light pane. These are a saturated mid-dark red /
    // green that keep their HUE on the ~white pane/sidebar bg while still
    // clearing 4.5:1 (red ~5.0, green ~4.7 on #F-ish).
    /*status_blocked*/ {193, 30, 30, 255},
    /*status_review*/ {22, 118, 56, 255},

    /*role_user*/ {46, 90, 236, 255},
    /*role_assistant*/ {34, 124, 62, 255},
    /*role_system*/ {150, 112, 30, 255},
    /*role_tool*/ {114, 84, 184, 255},
    // User bubble = a MUTED NEUTRAL grey on light (transcript-only) — quiet,
    // not a saturated blue tint. Sits a touch below the white pane.
    /*bubble_user_bg*/ {232, 233, 238, 255},
    /*bubble_assistant_bg*/ {224, 242, 230, 255},
    /*bubble_other_bg*/ {238, 238, 244, 255},

    /*status_active*/ {34, 124, 62, 255},
    /*status_idle*/ {150, 112, 30, 255},
    /*status_archived*/ {110, 110, 122, 255},

    // Syntax (light): the same six roles, darkened for a near-white code
    // surface — each one clears 4.5:1 on it, which the dark palette's hues
    // would not.
    /*syntax_keyword*/ {138, 44, 178, 255},
    /*syntax_type*/ {18, 108, 116, 255},
    /*syntax_string*/ {32, 116, 48, 255},
    /*syntax_comment*/ {124, 128, 140, 255},
    /*syntax_number*/ {160, 92, 12, 255},
    /*syntax_punct*/ {94, 98, 112, 255},
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
// Typography — the ALLOWED font-size scale (docs/spec-metrics.md "Type scale").
// Every with_font_size(<px>) in the systems must use one of these named sizes,
// so a size is defined once here instead of repeated as a raw float literal and
// so we never "invent" an off-scale size. Values match the spec exactly.
namespace theme {
namespace type {
constexpr float H1 = 20.0f;          // smart-view h1
constexpr float SPOTLIGHT = 17.0f;   // spotlight/kickoff input
constexpr float LG = 14.0f;          // transcript h2 / large labels
constexpr float TITLE = 13.5f;       // digest card title
constexpr float BODY = 13.0f;        // smart-view row label
constexpr float ROW = 12.5f;         // thread-row title, tab label
constexpr float MD = 12.0f;          // folder name, section body
constexpr float SUBROW = 11.5f;      // sub-agent sub-row title
constexpr float SM = 11.0f;          // counts, status bar, sub-labels
constexpr float LABEL = 10.5f;       // section labels (VIEWS/FOLDERS)
constexpr float XS = 10.0f;          // smallest body
constexpr float CHIP = 9.5f;         // tag chip text
constexpr float MICRO = 9.0f;        // glyph-adjacent micro text
}  // namespace type

// Real rendered width (logical px) of `s` at font size `px`, measured against
// the SAME active font draw_text uses (fontstash bounds). Replaces the
// per-glyph width ESTIMATES that left trailing gaps — e.g. a fixed-width
// right-aligned time column meant the star sat flush to the COLUMN edge, not
// the text, and the status activity dot floated in the gutter left of the
// count. Falls back to a conservative per-glyph estimate only if the font
// context isn't ready yet (very first frame / headless before font load).
inline float text_px(const char* s, float px) {
    if (!s || !*s) return 0.0f;
    float w = afterhours::measure_text_internal(s, px);
    if (w > 0.0f) return w;
    // Fallback: ~0.5em per glyph (only hit before the font context exists).
    return static_cast<float>(std::char_traits<char>::length(s)) * px * 0.5f;
}
inline float text_px(const std::string& s, float px) {
    return text_px(s.c_str(), px);
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
