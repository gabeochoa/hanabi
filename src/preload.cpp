#include "preload.h"
#include <branding.h>

#include <afterhours/src/plugins/files.h>
#include <afterhours/src/plugins/ui/theme.h>

#include <cassert>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

#include "input_mapping.h"
#include "ui/focus_visible.h"
#include "util/atlas_guard.h"
#include "rl.h"

#include <afterhours/src/core/key_codes.h>

using namespace afterhours;

namespace {

std::filesystem::path get_exe_dir() {
#ifdef __APPLE__
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) == 0) {
        return std::filesystem::canonical(buf).parent_path();
    }
#elif defined(__linux__)
    return std::filesystem::read_symlink("/proc/self/exe").parent_path();
#elif defined(_WIN32)
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
#endif
    return std::filesystem::current_path();
}

std::string resolve_resource_root() {
    auto exe_dir = get_exe_dir();

    // 1. Next to the executable (output/resources/)
    auto candidate = exe_dir / "resources";
    if (std::filesystem::is_directory(candidate))
        return candidate.string();

    // 2. macOS .app bundle (Contents/MacOS/../Resources)
    candidate = exe_dir / ".." / "Resources";
    if (std::filesystem::is_directory(candidate))
        return std::filesystem::canonical(candidate).string();

    // 3. Fallback: relative to CWD (current behavior, for dev workflow)
    return "resources";
}

} // namespace

Preload::Preload() {}

Preload& Preload::init(const char* /*title*/) {
    files::init(product_branding::kStorageName, resolve_resource_root());
    afterhours::graphics::set_exit_key(0);

    return *this;
}

Preload& Preload::make_singleton() {
    auto& sophie = EntityHelper::createEntity();
    {
        {
            // The chords themselves live in input_mapping.h, next to the
            // enum they are keyed by: the two only work as a pair, and a unit
            // test asserts the same table this line installs.
            input::add_singleton_components(sophie, hanabi::input::key_mapping());
        }
        {
            window_manager::add_singleton_components(sophie, 200);
        }
        {
            ui::init_ui_plugin<InputAction>();
        }
    }

    // Load fonts
    std::string ui_font_path =
        files::get_resource_path("fonts", "Roboto-Regular.ttf").string();
    std::string mono_font_path =
        files::get_resource_path("fonts", "JetBrainsMono-Regular.ttf").string();
    // Atkinson Hyperlegible: an alternate UI font offered in Settings
    // (Appearance -> Font). Loaded under a stable name here; the settings
    // control / startup restore swaps it into DEFAULT_FONT live via
    // FontManager.load_font. Client-local choice only (no API field).
    std::string hyperlegible_font_path =
        files::get_resource_path("fonts", "AtkinsonHyperlegible-Regular.ttf")
            .string();

    auto& fontMgr =
        EntityHelper::get_singleton_cmp_enforce<ui::FontManager>();

    fontMgr.load_font(ui::UIComponent::DEFAULT_FONT,
                      ui_font_path.c_str());
    fontMgr.load_font(ui::UIComponent::SYMBOL_FONT,
                      ui_font_path.c_str());
    fontMgr.load_font("mono", mono_font_path.c_str());
    fontMgr.load_font("hyperlegible", hyperlegible_font_path.c_str());

    // From here a zero-width measurement is a FAULT, not a not-ready. Before
    // this line measure_text_internal legitimately returns 0 (no font context,
    // no active face) for the whole of launch, and counting that would drown
    // the signal src/util/atlas_guard.h exists to raise.
    hanabi::atlas::arm();

    // Dark theme setup
    {
        ui::imm::ThemeDefaults::get()
            .set_theme_color(ui::Theme::Usage::Primary,
                             afterhours::Color{0, 122, 204, 255})
            .set_theme_color(ui::Theme::Usage::Error,
                             afterhours::Color{220, 76, 71, 255})
            .set_theme_color(ui::Theme::Usage::Font,
                             afterhours::Color{204, 204, 204, 255})
            .set_theme_color(ui::Theme::Usage::DarkFont,
                             afterhours::Color{30, 30, 30, 255})
            .set_theme_color(ui::Theme::Usage::Background,
                             afterhours::Color{30, 30, 30, 255})
            .set_theme_color(ui::Theme::Usage::Surface,
                             afterhours::Color{37, 37, 38, 255})
            .set_theme_color(ui::Theme::Usage::Secondary,
                             afterhours::Color{58, 58, 58, 255})
            .set_theme_color(ui::Theme::Usage::Accent,
                             afterhours::Color{0, 122, 204, 255});

        // Configure font sizing tiers (values are h720 reference pixels)
        // Small=10 (badges, hashes, secondary info), Medium=12 (row content),
        // Large=14 (section headers, toolbar/tabs), XL=17 (menu bar)
        auto& theme = ui::imm::ThemeDefaults::get().theme;
        theme.font_sizing.small = 10.0f;
        theme.font_sizing.medium = 12.0f;
        theme.font_sizing.large = 14.0f;
        theme.font_sizing.xl = 17.0f;

        // Focus ring: ONE hairline, flush with the element. The default draws
        // an outline plus `thickness` concentric rounded-line rects, each with
        // the same roundness FRACTION — and radius = min(w,h) * 0.5 * roundness,
        // so every ring out from the element has a larger corner radius than the
        // one inside it. On a 34px-tall rounded field the four rings fan apart
        // at the corners and read as bracket marks hanging off each end
        // (afterhours_gaps.md #46). One ring at offset 0 has nothing to diverge
        // from, and it is the desktop convention anyway.
        theme.focus_ring_thickness = 1.0f;
        theme.focus_ring_offset = 0.0f;

        // Square by default, which is what this app is: 220 widgets pass
        // with_roundness(0.0f) and afterhours defaults to 0.5, half the short
        // side. The default only ever reached a widget that states no
        // roundness of its own — a transparent chip, a bare label — and those
        // are exactly the widgets with no visible shape for a rounded ring to
        // agree with. See ui/focus_visible.h kRingRoundness for why zero also
        // stops the ring's three concentric outlines fanning at the corners.
        // Both frozen parity captures are byte-identical across this change.
        theme.roundness = hanabi::ui::focus_visible::kRingRoundness;

        // Arrows stay hanabi's. theme.arrows_tab defaults true, which makes
        // process_tabbing treat Up/Down as Tab/Shift+Tab — and hanabi already
        // owns the arrow keys twice over: ArrowSystem walks the sidebar's list
        // cursor with them and text_input walks the caret with them. Left as
        // true, binding Tab above would also have handed afterhours every
        // arrow keystroke in the app, so a caret move would jump focus.
        theme.arrows_tab = false;

        // afterhours_gaps.md #71 — the snap unit is round(4 * window_height /
        // 720) and it quantizes child POSITIONS, not just sizes, so a 32px row
        // pitch becomes 30 at a 949-tall window and drifts 2px per row.
        // skip_grid_snap opts a widget's SIZE out; nothing opts a position out.
        ui::imm::UIStylingDefaults::get().set_grid_snapping(false);
    }

    return *this;
}

Preload::~Preload() {
    if (afterhours::graphics::is_window_ready()) {
        afterhours::graphics::close_window();
    }
}
