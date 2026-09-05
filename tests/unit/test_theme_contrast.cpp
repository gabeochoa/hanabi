#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "../../src/ui/theme.h"

static int failures = 0;

static double channel(unsigned char value) {
    const double v = static_cast<double>(value) / 255.0;
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

static double luminance(theme::Color color) {
    return 0.2126 * channel(color.r) + 0.7152 * channel(color.g) +
           0.0722 * channel(color.b);
}

static double contrast(theme::Color a, theme::Color b) {
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

static void check_pair(const char* name, theme::Color fg, theme::Color bg) {
    const double ratio = contrast(fg, theme::over(bg, theme::panel_bg_2()));
    std::printf("%s %.2f:1\n", name, ratio);
    if (ratio < 4.5) ++failures;
}

static void check_direct(const char* name, theme::Color fg, theme::Color bg) {
    const double ratio = contrast(fg, bg);
    std::printf("%s %.2f:1\n", name, ratio);
    if (ratio < 4.5) ++failures;
}

static void check_mode(theme::Mode mode, const char* name) {
    theme::set_mode(mode);
    check_pair((std::string(name) + " blocked").c_str(), theme::tag_blocked_fg(),
               theme::tag_blocked_bg());
    check_pair((std::string(name) + " ready").c_str(), theme::tag_ready_fg(),
               theme::tag_ready_bg());
    check_pair((std::string(name) + " done").c_str(), theme::tag_done_fg(),
               theme::tag_done_bg());
    check_direct((std::string(name) + " panel primary").c_str(),
                 theme::text_primary(), theme::panel_bg());
    check_direct((std::string(name) + " panel secondary").c_str(),
                 theme::text_secondary(), theme::panel_bg());
    // The ask card's arity hints ("Pick one", "Pick any") and its free-text
    // label ("Or write your own answer · Other") sit on the recessed card
    // fill. They were text_faint, which is 2.63:1 on the dark card and 4.50:1
    // on the light one -- one fails outright and the other clears the floor by
    // 0.002, so a single rounding of the card fill would drop it too. They are
    // the labels that say whether a question takes one answer or several, so
    // they are not decoration.
    check_direct((std::string(name) + " ask arity").c_str(),
                 theme::text_secondary(), theme::panel_bg_2());
    check_direct((std::string(name) + " ask other label").c_str(),
                 theme::text_secondary(), theme::panel_bg_2());
    // The two sentences that say a file will not be sent: one on the card
    // fill, one on the composer strip -- different backdrops, so they are
    // two measurements, not one.
    check_direct((std::string(name) + " ask file caveat").c_str(),
                 theme::ask_caveat_ink(), theme::panel_bg_2());
    check_direct((std::string(name) + " composer image caveat").c_str(),
                 theme::ask_caveat_ink(), theme::panel_bg());
    theme::Color dangerTint = theme::destructive();
    dangerTint.a = mode == theme::Mode::Light ? 34 : 16;
    check_direct((std::string(name) + " destructive surface").c_str(),
                 theme::destructive(),
                 theme::over(dangerTint, theme::panel_bg()));
    const auto titlebar = theme::chrome::titlebar();
    const auto sidebar = theme::chrome::sidebar();
    const auto content = theme::chrome::content();
    const auto raised = theme::chrome::raised();
    if (!(luminance(titlebar) < luminance(sidebar))) ++failures;
    if (!(luminance(sidebar) < luminance(content))) ++failures;
    if (!(luminance(content) < luminance(raised))) ++failures;
    const auto selected = theme::chrome::selected_on(sidebar);
    if (selected.r == sidebar.r && selected.g == sidebar.g &&
        selected.b == sidebar.b)
        ++failures;
    if (theme::chrome::ROW < theme::chrome::HIT) ++failures;
    if (theme::chrome::RADIUS > theme::chrome::ROW * 0.25f) ++failures;
}

int main() {
    check_mode(theme::Mode::Dark, "dark");
    check_mode(theme::Mode::Light, "light");
    theme::set_mode(theme::Mode::Dark);
    if (failures == 0) {
        std::puts("status pill contrast: PASS");
        return 0;
    }
    std::printf("status pill contrast: %d failure(s)\n", failures);
    return 1;
}
