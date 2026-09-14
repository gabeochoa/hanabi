// The palette IS the reference's: every semantic token reads the value the
// pinned reference's default themes declare (its dark default for Dark, its
// light default for Light -- the theme table in the pinned export at
// fb84b1d4), or the value the reference DERIVES from them (its theme file:
// hairline = mutedText at 20 %, a selected row = accent at 25 %, hover = text
// at 9 %). A token that drifts from its source fails here by name, so a
// palette edit is a change to this file too, on purpose.
//
// Also: the custom accent / highlight swatches re-layer over the fresh palette
// on every mode switch, and only the tokens that carry the accent hue move --
// a reader's chosen accent survives a Dark <-> Light switch, and the rest of
// the theme does not follow it.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../../src/ui/theme.h"

static int failures = 0;

static void expect_rgb(const char* name, theme::Color got, unsigned r, unsigned g,
                       unsigned b) {
    if (got.r != r || got.g != g || got.b != b) {
        std::printf("FAIL %s: got #%02X%02X%02X want #%02X%02X%02X\n", name, got.r,
                    got.g, got.b, r, g, b);
        ++failures;
    }
}

static void expect_rgba(const char* name, theme::Color got, unsigned r, unsigned g,
                        unsigned b, unsigned a) {
    expect_rgb(name, got, r, g, b);
    if (got.a != a) {
        std::printf("FAIL %s: alpha %u want %u\n", name, got.a, a);
        ++failures;
    }
}

// The reference's `tinted(color, on: surface, opacity:)`, rounded the way
// theme::over rounds -- what a translucent wash lands as on an opaque plane.
static theme::Color blend(theme::Color hue, unsigned alpha, theme::Color surface) {
    theme::Color c = hue;
    c.a = static_cast<unsigned char>(alpha);
    return theme::over(c, surface);
}

// -- the reference's dark default --------------------------------------------------------------
static void test_dark_is_the_reference_dark_default() {
    std::printf("test_dark_is_the_reference_dark_default\n");
    theme::set_accent_choice(theme::kDefaultChoice);
    theme::set_highlight_choice(theme::kDefaultChoice);
    theme::set_mode(theme::Mode::Dark);

    // The eleven declared colours, on the tokens that carry them.
    expect_rgb("window_bg = nightSky", theme::window_bg(), 0x0D, 0x14, 0x1D);
    expect_rgb("panel_bg = nightSky", theme::panel_bg(), 0x0D, 0x14, 0x1D);
    expect_rgb("sidebar_bg = headerBg", theme::sidebar_bg(), 0x17, 0x1F, 0x2A);
    expect_rgb("section_header_bg = headerBg", theme::section_header_bg(), 0x17, 0x1F, 0x2A);
    // The search field's ground: text at 6 % over headerBg (SearchFieldChrome).
    expect_rgb("panel_bg_2 = text 6% on headerBg", theme::panel_bg_2(), 0x24, 0x2C, 0x37);
    expect_rgb("accent = naviBlue", theme::accent(), 0x3B, 0x9E, 0xDB);
    expect_rgb("link = naviBlue", theme::link(), 0x3B, 0x9E, 0xDB);
    expect_rgb("focus_ring = naviBlue", theme::focus_ring(), 0x3B, 0x9E, 0xDB);
    expect_rgb("text_primary = lightText", theme::text_primary(), 0xF7, 0xF9, 0xFC);
    expect_rgb("text_secondary = mutedText", theme::text_secondary(), 0x8B, 0x9B, 0xA5);
    expect_rgb("destructive = errorRed", theme::destructive(), 0xF1, 0x4E, 0x53);
    expect_rgb("status_blocked = errorRed", theme::status_blocked(), 0xF1, 0x4E, 0x53);
    expect_rgb("status_review = faroreGreen", theme::status_review(), 0x3F, 0xBF, 0x7F);
    expect_rgb("role_system = triforceGold", theme::role_system(), 0xFC, 0x6C, 0x05);
    expect_rgb("bubble_user_bg = kokiriGreen", theme::bubble_user_bg(), 0x16, 0x32, 0x3F);
    expect_rgb("bubble_assistant_bg = sheikahSlate", theme::bubble_assistant_bg(), 0x1E, 0x27, 0x33);
    expect_rgb("code_bg = syntax.background", theme::code_bg(), 0x04, 0x07, 0x0C);
    expect_rgb("syntax_keyword = night.keyword", theme::syntax_keyword(), 0xC7, 0x92, 0xEA);
    expect_rgb("syntax_string = night.string", theme::syntax_string(), 0x7F, 0xD9, 0x8C);
    expect_rgb("syntax_type = night.type", theme::syntax_type(), 0x5C, 0xC6, 0xF0);
    expect_rgb("syntax_number = night.number", theme::syntax_number(), 0xF5, 0x93, 0x56);

    // The derived ones, by the reference's formula over the surface they sit on.
    expect_rgb("border = mutedText 20% on nightSky", theme::border(),
               blend(theme::text_secondary(), 51, theme::window_bg()).r,
               blend(theme::text_secondary(), 51, theme::window_bg()).g,
               blend(theme::text_secondary(), 51, theme::window_bg()).b);
    expect_rgb("divider = border", theme::divider(), theme::border().r, theme::border().g,
               theme::border().b);
    expect_rgb("selected_bg = accent 25% on headerBg", theme::selected_bg(),
               blend(theme::accent(), 64, theme::sidebar_bg()).r,
               blend(theme::accent(), 64, theme::sidebar_bg()).g,
               blend(theme::accent(), 64, theme::sidebar_bg()).b);
    // And the capture agrees: the reference's selected row, ICC-converted to
    // sRGB, read #223F56; the formula gives #203F56.
    expect_rgb("selected_bg literal", theme::selected_bg(), 0x20, 0x3F, 0x56);
    expect_rgba("hover_bg = text at 9%", theme::hover_bg(), 0xF7, 0xF9, 0xFC, 23);
    expect_rgba("accent_soft = accent at 16%", theme::accent_soft(), 0x3B, 0x9E, 0xDB, 41);

    // Tinted pills: the hue at 16 % under the hue itself (ToggleChip.Tint).
    expect_rgb("blocked pill text = errorRed", theme::tag_blocked_fg(), 0xF1, 0x4E, 0x53);
    expect_rgba("blocked pill = errorRed at 16%", theme::tag_blocked_bg(), 0xF1, 0x4E, 0x53, 41);
    expect_rgba("ready pill = faroreGreen at 16%", theme::tag_ready_bg(), 0x3F, 0xBF, 0x7F, 41);
    expect_rgba("done pill = mutedText at 16%", theme::tag_done_bg(), 0x8B, 0x9B, 0xA5, 41);

    // The shelf badge: attention DEEPENED for white digits. The reference's
    // #FC6C05 does not carry white (2.88:1), so the fill dims to the brightest
    // value on which white reads 4.6:1 -- and the pinned capture's rail badge,
    // converted to sRGB, is that byte.
    expect_rgb("attention_badge = deepened attention", theme::attention_badge(), 0xC3, 0x53, 0x04);
    if (theme::contrast_ratio(theme::attention_badge(), theme::Color{255, 255, 255, 255}) < 4.5) {
        std::puts("FAIL white does not read on the badge");
        ++failures;
    }
    // The deepening leaves a fill alone when white already reads on it.
    const theme::Color already{0x8A, 0x5A, 0x00, 255};
    expect_rgb("deepening is identity on a dark fill", theme::deepened_for_white_text(already),
               0x8A, 0x5A, 0x00);
}

// -- the reference's light default ----------------------------------------------------------------
static void test_light_is_the_reference_light_default() {
    std::printf("test_light_is_the_reference_light_default\n");
    theme::set_accent_choice(theme::kDefaultChoice);
    theme::set_highlight_choice(theme::kDefaultChoice);
    theme::set_mode(theme::Mode::Light);

    expect_rgb("window_bg = nightSky", theme::window_bg(), 0xF7, 0xF9, 0xFC);
    expect_rgb("panel_bg = nightSky", theme::panel_bg(), 0xF7, 0xF9, 0xFC);
    expect_rgb("sidebar_bg = headerBg", theme::sidebar_bg(), 0xFF, 0xFF, 0xFF);
    expect_rgb("panel_bg_2 = text 6% on headerBg", theme::panel_bg_2(), 0xF1, 0xF1, 0xF1);
    expect_rgb("accent = naviBlue", theme::accent(), 0x0B, 0x5C, 0x8A);
    expect_rgb("text_primary = lightText", theme::text_primary(), 0x12, 0x14, 0x17);
    expect_rgb("text_secondary = mutedText", theme::text_secondary(), 0x5C, 0x6B, 0x73);
    expect_rgb("destructive = errorRed", theme::destructive(), 0xC1, 0x27, 0x2D);
    expect_rgb("status_blocked = errorRed", theme::status_blocked(), 0xC1, 0x27, 0x2D);
    expect_rgb("status_review = faroreGreen", theme::status_review(), 0x1E, 0x7A, 0x4C);
    expect_rgb("role_system = triforceGold", theme::role_system(), 0x8A, 0x5A, 0x00);
    expect_rgb("bubble_user_bg = kokiriGreen", theme::bubble_user_bg(), 0xDC, 0xEE, 0xF8);
    expect_rgb("bubble_assistant_bg = sheikahSlate", theme::bubble_assistant_bg(), 0xEC, 0xEF, 0xF3);
    expect_rgb("code_bg = day syntax.background", theme::code_bg(), 0xD8, 0xDF, 0xE8);
    expect_rgb("syntax_keyword = day.keyword", theme::syntax_keyword(), 0x7A, 0x21, 0xA8);
    expect_rgb("border = mutedText 20% on nightSky", theme::border(),
               blend(theme::text_secondary(), 51, theme::window_bg()).r,
               blend(theme::text_secondary(), 51, theme::window_bg()).g,
               blend(theme::text_secondary(), 51, theme::window_bg()).b);
    expect_rgb("selected_bg = accent 25% on headerBg", theme::selected_bg(),
               blend(theme::accent(), 64, theme::sidebar_bg()).r,
               blend(theme::accent(), 64, theme::sidebar_bg()).g,
               blend(theme::accent(), 64, theme::sidebar_bg()).b);
    expect_rgba("hover_bg = text at 9%", theme::hover_bg(), 0x12, 0x14, 0x17, 23);
    expect_rgba("blocked pill = errorRed at 16%", theme::tag_blocked_bg(), 0xC1, 0x27, 0x2D, 41);
    expect_rgb("blocked pill text = errorRed", theme::tag_blocked_fg(), 0xC1, 0x27, 0x2D);
    // Light attention (#8A5A00) already carries white: the badge IS the
    // declared colour.
    expect_rgb("attention_badge = attention, undimmed", theme::attention_badge(), 0x8A, 0x5A, 0x00);
}

// -- A reader's own colours survive the palette ------------------------------
static void test_custom_accent_survives_a_mode_switch_and_moves_only_its_tokens() {
    std::printf("test_custom_accent_survives_a_mode_switch_and_moves_only_its_tokens\n");
    theme::set_highlight_choice(theme::kDefaultChoice);
    theme::set_mode(theme::Mode::Dark);
    const theme::Color plane = theme::window_bg();
    const theme::Color ink = theme::text_primary();
    const theme::Color selected = theme::selected_bg();

    const theme::Swatch& pick = theme::kAccentSwatches[1];
    theme::set_accent_choice(pick.key);
    expect_rgb("dark accent takes the swatch", theme::accent(), pick.dark.r, pick.dark.g, pick.dark.b);
    expect_rgb("dark focus ring follows", theme::focus_ring(), pick.dark.r, pick.dark.g, pick.dark.b);
    expect_rgb("dark dot follows", theme::dot(), pick.dark.r, pick.dark.g, pick.dark.b);
    // The wash keeps its OWN alpha: a hue swap, never a slab.
    if (theme::accent_soft().a != 41) {
        std::printf("FAIL accent_soft alpha moved: %u\n", theme::accent_soft().a);
        ++failures;
    }
    // Nothing else moved.
    expect_rgb("plane untouched by the accent", theme::window_bg(), plane.r, plane.g, plane.b);
    expect_rgb("ink untouched by the accent", theme::text_primary(), ink.r, ink.g, ink.b);
    expect_rgb("selected row untouched by the accent", theme::selected_bg(), selected.r,
               selected.g, selected.b);

    // Switch to Day: the palette reloads, the choice re-layers in its light arm.
    theme::set_mode(theme::Mode::Light);
    expect_rgb("light accent takes the swatch's light arm", theme::accent(), pick.light.r,
               pick.light.g, pick.light.b);
    expect_rgb("light plane is Day", theme::window_bg(), 0xF7, 0xF9, 0xFC);
    // And back, still chosen.
    theme::set_mode(theme::Mode::Dark);
    expect_rgb("dark accent still the swatch", theme::accent(), pick.dark.r, pick.dark.g, pick.dark.b);

    // Clearing the choice returns the reference's accent exactly.
    theme::set_accent_choice(theme::kDefaultChoice);
    expect_rgb("default restores naviBlue", theme::accent(), 0x3B, 0x9E, 0xDB);

    // The highlight swatch reaches find_match alone, keeping the band's alpha.
    const theme::Swatch& band = theme::kHighlightSwatches[0];
    theme::set_highlight_choice(band.key);
    expect_rgba("find_match takes the highlight hue at the band alpha", theme::find_match(),
                band.dark.r, band.dark.g, band.dark.b, 90);
    expect_rgb("accent untouched by the highlight", theme::accent(), 0x3B, 0x9E, 0xDB);
    theme::set_highlight_choice(theme::kDefaultChoice);
    expect_rgba("default find_match is the gold band", theme::find_match(), 0xFC, 0x6C, 0x05, 90);
}

// -- The mode toggle is a round trip -------------------------------------------
static void test_toggle_round_trips() {
    std::printf("test_toggle_round_trips\n");
    theme::set_accent_choice(theme::kDefaultChoice);
    theme::set_mode(theme::Mode::Dark);
    theme::toggle_mode();
    if (theme::mode() != theme::Mode::Light) { std::puts("FAIL toggle -> light"); ++failures; }
    expect_rgb("toggled plane is Day", theme::window_bg(), 0xF7, 0xF9, 0xFC);
    theme::toggle_mode();
    if (theme::mode() != theme::Mode::Dark) { std::puts("FAIL toggle -> dark"); ++failures; }
    expect_rgb("toggled plane is Night", theme::window_bg(), 0x0D, 0x14, 0x1D);
}

int main() {
    test_dark_is_the_reference_dark_default();
    test_light_is_the_reference_light_default();
    test_custom_accent_survives_a_mode_switch_and_moves_only_its_tokens();
    test_toggle_round_trips();
    theme::set_mode(theme::Mode::Dark);
    if (failures == 0) {
        std::puts("theme tokens: PASS");
        return 0;
    }
    std::printf("theme tokens: %d failure(s)\n", failures);
    return 1;
}
