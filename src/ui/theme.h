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
inline const Tokens kDark = {
    /*window_bg*/ {28, 28, 32, 255},
    /*sidebar_bg*/ {22, 22, 26, 255},
    /*panel_bg*/ {28, 28, 32, 255},
    /*panel_bg_2*/ {34, 34, 40, 255},
    /*border*/ {52, 52, 60, 255},
    /*border_soft*/ {255, 255, 255, 13},

    /*text_primary*/ {222, 222, 228, 255},
    /*text_secondary*/ {138, 138, 150, 255},
    /*text_faint*/ {96, 96, 108, 255},
    /*empty_state_text*/ {110, 110, 122, 255},

    /*accent*/ {90, 128, 255, 255},
    /*accent_soft*/ {90, 128, 255, 38},
    /*button_primary*/ {90, 128, 255, 255},
    /*button_secondary*/ {56, 56, 64, 255},
    /*hover_bg*/ {255, 255, 255, 12},
    /*selected_bg*/ {48, 66, 120, 255},
    /*row_separator*/ {40, 40, 48, 255},
    /*focus_ring*/ {90, 128, 255, 255},

    /*disabled_bg*/ {44, 44, 50, 255},
    /*disabled_text*/ {120, 120, 128, 255},
    /*destructive*/ {224, 88, 84, 255},

    /*dot*/ {90, 128, 255, 255},

    /*tag_blocked_fg*/ {255, 120, 120, 255},
    /*tag_blocked_bg*/ {255, 90, 90, 36},
    /*tag_ready_fg*/ {126, 210, 150, 255},
    /*tag_ready_bg*/ {120, 210, 140, 36},
    /*tag_done_fg*/ {120, 160, 255, 255},
    /*tag_done_bg*/ {90, 128, 255, 41},

    /*role_user*/ {90, 128, 255, 255},
    /*role_assistant*/ {126, 200, 140, 255},
    /*role_system*/ {180, 150, 90, 255},
    /*role_tool*/ {150, 130, 200, 255},
    /*bubble_user_bg*/ {40, 52, 84, 255},
    /*bubble_assistant_bg*/ {32, 40, 36, 255},
    /*bubble_other_bg*/ {40, 40, 48, 255},

    /*status_active*/ {126, 200, 140, 255},
    /*status_idle*/ {180, 150, 90, 255},
    /*status_archived*/ {110, 110, 122, 255},
};

// -------- Light palette --------
inline const Tokens kLight = {
    /*window_bg*/ {244, 244, 247, 255},
    /*sidebar_bg*/ {236, 236, 240, 255},
    /*panel_bg*/ {252, 252, 253, 255},
    /*panel_bg_2*/ {246, 246, 249, 255},
    /*border*/ {216, 216, 222, 255},
    /*border_soft*/ {0, 0, 0, 13},

    /*text_primary*/ {34, 34, 42, 255},
    /*text_secondary*/ {120, 120, 132, 255},
    /*text_faint*/ {168, 168, 178, 255},
    /*empty_state_text*/ {150, 150, 162, 255},

    /*accent*/ {58, 102, 248, 255},
    /*accent_soft*/ {58, 102, 248, 31},
    /*button_primary*/ {58, 102, 248, 255},
    /*button_secondary*/ {224, 224, 230, 255},
    /*hover_bg*/ {0, 0, 0, 10},
    /*selected_bg*/ {214, 226, 255, 255},
    /*row_separator*/ {224, 224, 230, 255},
    /*focus_ring*/ {58, 102, 248, 255},

    /*disabled_bg*/ {228, 228, 232, 255},
    /*disabled_text*/ {170, 170, 178, 255},
    /*destructive*/ {200, 44, 44, 255},

    /*dot*/ {58, 102, 248, 255},

    /*tag_blocked_fg*/ {200, 44, 44, 255},
    /*tag_blocked_bg*/ {220, 60, 60, 31},
    /*tag_ready_fg*/ {30, 140, 66, 255},
    /*tag_ready_bg*/ {40, 160, 80, 36},
    /*tag_done_fg*/ {48, 92, 224, 255},
    /*tag_done_bg*/ {58, 102, 248, 31},

    /*role_user*/ {58, 102, 248, 255},
    /*role_assistant*/ {46, 150, 80, 255},
    /*role_system*/ {168, 128, 40, 255},
    /*role_tool*/ {126, 96, 196, 255},
    /*bubble_user_bg*/ {224, 232, 255, 255},
    /*bubble_assistant_bg*/ {228, 244, 232, 255},
    /*bubble_other_bg*/ {236, 236, 242, 255},

    /*status_active*/ {46, 150, 80, 255},
    /*status_idle*/ {168, 128, 40, 255},
    /*status_archived*/ {150, 150, 162, 255},
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
