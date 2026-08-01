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

    // Text
    Color text_primary;
    Color text_secondary;
    Color text_faint;
    Color empty_state_text;

    // Accent / interactive
    Color accent;
    Color accent_soft;
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

    // Message role accents (transcript)
    Color role_user, role_assistant, role_system, role_tool;
    Color bubble_user_bg, bubble_assistant_bg, bubble_other_bg;

    // Session-list status pips (legacy)
    Color status_active, status_idle, status_archived;
};

// -------- Dark palette (anchor / default) --------
//
// ELEVATION SYSTEM (dark). Surfaces read as a subtle 3-step stack so the eye
// can parse structure without harsh contrast (this is a pro tool, not a
// high-contrast theme). Each step is a distinct, measurable value:
//   L0 base    (window_bg)  {24,24,28}  — the deepest recessed plane
//   L0 sidebar (sidebar_bg) {19,19,23}  — chrome rail, a touch below base
//   L1 raised  (panel_bg)   {33,33,39}  — cards / panels / active tab
//   L2 overlay (panel_bg_2) {42,42,49}  — search field, hover surfaces, chips
// The gap between neighbouring layers is ~8-9 in luma (vs the old ~6 that made
// everything blend), and the border is bumped so dividers are actually seen.
inline const Tokens kDark = {
    /*window_bg*/ {24, 24, 28, 255},
    /*sidebar_bg*/ {19, 19, 23, 255},
    /*panel_bg*/ {33, 33, 39, 255},
    /*panel_bg_2*/ {42, 42, 49, 255},
    /*border*/ {62, 62, 72, 255},
    /*border_soft*/ {255, 255, 255, 20},

    /*text_primary*/ {224, 224, 230, 255},
    /*text_secondary*/ {142, 142, 154, 255},
    /*text_faint*/ {100, 100, 112, 255},
    /*empty_state_text*/ {112, 112, 124, 255},

    /*accent*/ {90, 128, 255, 255},
    /*accent_soft*/ {90, 128, 255, 38},
    /*button_primary*/ {90, 128, 255, 255},
    /*button_secondary*/ {58, 58, 66, 255},
    /*hover_bg*/ {255, 255, 255, 16},
    /*selected_bg*/ {52, 72, 128, 255},
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

    /*role_user*/ {90, 128, 255, 255},
    /*role_assistant*/ {126, 200, 140, 255},
    /*role_system*/ {180, 150, 90, 255},
    /*role_tool*/ {150, 130, 200, 255},
    /*bubble_user_bg*/ {44, 58, 92, 255},
    /*bubble_assistant_bg*/ {34, 46, 40, 255},
    /*bubble_other_bg*/ {44, 44, 52, 255},

    /*status_active*/ {126, 200, 140, 255},
    /*status_idle*/ {180, 150, 90, 255},
    /*status_archived*/ {110, 110, 122, 255},
};

// -------- Light palette --------
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
    /*window_bg*/ {238, 238, 242, 255},
    /*sidebar_bg*/ {230, 230, 235, 255},
    /*panel_bg*/ {255, 255, 255, 255},
    /*panel_bg_2*/ {247, 247, 250, 255},
    /*border*/ {176, 176, 190, 255},
    /*border_soft*/ {0, 0, 0, 40},

    /*text_primary*/ {33, 33, 43, 255},
    /*text_secondary*/ {85, 85, 95, 255},
    /*text_faint*/ {134, 134, 143, 255},
    /*empty_state_text*/ {122, 122, 134, 255},

    /*accent*/ {46, 90, 236, 255},
    /*accent_soft*/ {46, 90, 236, 34},
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

    /*role_user*/ {46, 90, 236, 255},
    /*role_assistant*/ {40, 138, 72, 255},
    /*role_system*/ {150, 112, 30, 255},
    /*role_tool*/ {114, 84, 184, 255},
    /*bubble_user_bg*/ {224, 232, 255, 255},
    /*bubble_assistant_bg*/ {224, 242, 230, 255},
    /*bubble_other_bg*/ {238, 238, 244, 255},

    /*status_active*/ {40, 138, 72, 255},
    /*status_idle*/ {150, 112, 30, 255},
    /*status_archived*/ {134, 134, 146, 255},
};

// Active token set (mutable, swapped at runtime). Defaults to dark.
inline Mode g_mode = Mode::Dark;
inline Tokens t = kDark;

inline void set_mode(Mode m) {
    g_mode = m;
    t = (m == Mode::Light) ? kLight : kDark;
}
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
inline Color text_primary() { return t.text_primary; }
inline Color text_secondary() { return t.text_secondary; }
inline Color text_faint() { return t.text_faint; }
inline Color empty_state_text() { return t.empty_state_text; }
inline Color accent() { return t.accent; }
inline Color accent_soft() { return t.accent_soft; }
inline Color button_primary() { return t.button_primary; }
inline Color button_secondary() { return t.button_secondary; }
inline Color hover_bg() { return t.hover_bg; }
inline Color selected_bg() { return t.selected_bg; }
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
inline Color status_active() { return t.status_active; }
inline Color status_idle() { return t.status_idle; }
inline Color status_archived() { return t.status_archived; }

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
