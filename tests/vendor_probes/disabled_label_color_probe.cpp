// Red before 266, green after: an EXPLICIT label colour on a disabled widget
// must be drawn as the caller asked, not halved. The engine's own choices --
// auto-contrast and the theme font -- keep their dimming, because there the
// engine picked the colour and owns the adjustment.
#include <afterhours/tests/ui_test_harness.h>

using namespace afterhours;
using namespace afterhours::ui;
using namespace afterhours::ui::imm;

static bool same(Color a, Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

int main() {
    const Color asked{240, 240, 240, 255};

    ui_test::ImmTestHarness explicitCase;
    div(explicitCase.context(), mk(explicitCase.root(), 0),
        ComponentConfig{}
            .with_size(ComponentSize{pixels(200), pixels(40)})
            .with_label("disabled")
            .with_custom_text_color(asked)
            .with_disabled(true));
    explicitCase.render();
    const auto explicitTexts = explicitCase.drawn("text");
    if (explicitTexts.empty()) return 1;
    if (!same(explicitTexts[0].color, asked)) {
        std::printf("explicit disabled colour was rewritten: asked %d, drew %d\n",
                    asked.r, explicitTexts[0].color.r);
        return 1;
    }

    ui_test::ImmTestHarness themed;
    div(themed.context(), mk(themed.root(), 0),
        ComponentConfig{}
            .with_size(ComponentSize{pixels(200), pixels(40)})
            .with_label("disabled")
            .with_disabled(true));
    div(themed.context(), mk(themed.root(), 1),
        ComponentConfig{}
            .with_size(ComponentSize{pixels(200), pixels(40)})
            .with_label("enabled"));
    themed.render();
    const auto themedTexts = themed.drawn("text");
    if (themedTexts.size() < 2) return 1;
    if (same(themedTexts[0].color, themedTexts[1].color)) {
        std::printf("a theme-derived disabled label stopped dimming\n");
        return 1;
    }
    return 0;
}
