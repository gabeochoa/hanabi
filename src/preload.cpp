#include "preload.h"

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
    files::init("hanabi", resolve_resource_root());
    afterhours::graphics::set_exit_key(0);

    return *this;
}

Preload& Preload::make_singleton() {
    auto& sophie = EntityHelper::createEntity();
    {
        {
            // Input mappings for text input
            std::map<int, afterhours::input::ValidInputs> mapping;
            mapping[static_cast<int>(InputAction::TextBackspace)] = {
                afterhours::keys::BACKSPACE,
            };
            mapping[static_cast<int>(InputAction::TextDelete)] = {
                afterhours::keys::DELETE_KEY,
            };
            mapping[static_cast<int>(InputAction::TextHome)] = {
                afterhours::keys::HOME,
            };
            mapping[static_cast<int>(InputAction::TextEnd)] = {
                afterhours::keys::END,
            };
            mapping[static_cast<int>(InputAction::WidgetLeft)] = {
                afterhours::keys::LEFT,
            };
            mapping[static_cast<int>(InputAction::WidgetRight)] = {
                afterhours::keys::RIGHT,
            };
            mapping[static_cast<int>(InputAction::WidgetPress)] = {
                afterhours::keys::ENTER,
            };
            mapping[static_cast<int>(InputAction::MenuBack)] = {
                afterhours::keys::ESCAPE,
            };
            input::add_singleton_components(sophie, mapping);
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

        ui::imm::UIStylingDefaults::get().set_grid_snapping(true);
    }

    return *this;
}

Preload::~Preload() {
    if (afterhours::graphics::is_window_ready()) {
        afterhours::graphics::close_window();
    }
}
