#include "font_system.h"

#include <afterhours/src/plugins/files.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "../native_extras.h"
#include "../rl.h"
#include "../util/atlas_guard.h"
#include "../util/text_epoch.h"
#include "theme.h"

namespace hanabi::fonts {
namespace {

static_assert(14 <= afterhours::graphics::metal_detail::MAX_FONTS);

const std::array<Choice, 4> kFamilyChoices{{
    {"default", "Standard"},
    {"hyperlegible", "Hyperlegible"},
    {"system", "System"},
    {"optimistic", "Optimistic"},
}};

const std::array<Choice, 4> kWeightChoices{{
    {"regular", "Regular"},
    {"medium", "Medium"},
    {"semibold", "Semibold"},
    {"bold", "Bold"},
}};

struct Family {
    Choice choice;
    std::map<std::string, std::string, std::less<>> paths;
    float point_scale = 1.0f;
};

struct Runtime {
    bool ready = false;
    std::vector<Family> catalog;
    std::vector<Choice> visible_families;
    std::string applied_family;
    std::string applied_emphasis;
};

Runtime& runtime() {
    static Runtime value;
    return value;
}

Family* find_family(std::string_view key) {
    for (auto& family : runtime().catalog)
        if (family.choice.key == key) return &family;
    return nullptr;
}

bool allow_native() {
    if (!afterhours::graphics::is_headless()) return true;
    const char* value = std::getenv("HANABI_ALLOW_SYSTEM_FONTS_HEADLESS");
    return value != nullptr && *value != '\0' && std::strcmp(value, "0") != 0;
}

std::string registered_name(std::string_view family, std::string_view weight) {
    return "hanabi." + std::string(family) + "." + std::string(weight);
}

afterhours::Font load_face(afterhours::ui::FontManager& manager,
                           std::string_view family, std::string_view weight,
                           const std::string& path) {
    const std::string key = registered_name(family, weight);
    if (!manager.fonts.contains(key)) manager.load_font(key, path.c_str());
    const auto it = manager.fonts.find(key);
    if (it == manager.fonts.end() || !afterhours::is_font_loaded(it->second))
        return afterhours::Font{};
    return it->second;
}

void add_native_catalog() {
    std::array<NativeFontFace, 32> faces{};
    const int count =
        native_font_faces(faces.data(), static_cast<int>(faces.size()));
    for (int i = 0; i < count && i < static_cast<int>(faces.size()); ++i) {
        const NativeFontFace& face = faces[static_cast<std::size_t>(i)];
        Family* family = find_family(face.family);
        if (family == nullptr) {
            const auto choice = std::find_if(
                kFamilyChoices.begin(), kFamilyChoices.end(),
                [&](const Choice& c) { return c.key == face.family; });
            if (choice == kFamilyChoices.end()) continue;
            runtime().catalog.push_back(Family{*choice, {}, 1.0f});
            family = &runtime().catalog.back();
        }
        family->paths[face.weight] = face.path;
        if (std::strcmp(face.weight, "regular") == 0)
            family->point_scale = face.point_scale;
    }
}

}  // namespace

void preload(afterhours::ui::FontManager& manager) {
    Runtime& state = runtime();
    state = Runtime{};

    const std::string regular =
        afterhours::files::get_resource_path("fonts", "Roboto-Regular.ttf")
            .string();
    const std::string hyper = afterhours::files::get_resource_path(
                                  "fonts", "AtkinsonHyperlegible-Regular.ttf")
                                  .string();
    const std::string mono = afterhours::files::get_resource_path(
                                 "fonts", "JetBrainsMono-Regular.ttf")
                                 .string();

    state.catalog.push_back(
        Family{kFamilyChoices[0], {{"regular", regular}}, 1.17185f});
    state.catalog.push_back(
        Family{kFamilyChoices[1], {{"regular", hyper}}, 1.24000f});
    if (allow_native()) add_native_catalog();

    manager.load_font(afterhours::ui::UIComponent::DEFAULT_FONT,
                      regular.c_str());
    manager.load_font(
        afterhours::ui::UIComponent::SYMBOL_FONT,
        manager.get_font(afterhours::ui::UIComponent::DEFAULT_FONT));
    manager.load_font("mono", mono.c_str());
    manager.load_font(
        registered_name("default", "regular"),
        manager.get_font(afterhours::ui::UIComponent::DEFAULT_FONT));
    manager.load_font(registered_name("hyperlegible", "regular"),
                      hyper.c_str());

    for (const auto& choice : kFamilyChoices)
        if (const Family* family = find_family(choice.key);
            family != nullptr && family->paths.contains("regular"))
            state.visible_families.push_back(choice);
    state.ready = true;
}

const std::vector<Choice>& families() { return runtime().visible_families; }

const std::vector<Choice>& weights() {
    static const std::vector<Choice> values(kWeightChoices.begin(),
                                            kWeightChoices.end());
    return values;
}

bool family_available(std::string_view key) {
    const Family* family = find_family(key);
    return family != nullptr && family->paths.contains("regular");
}

bool weight_available(std::string_view family, std::string_view weight) {
    const Family* found = find_family(family);
    return found != nullptr && found->paths.contains(weight);
}

std::string effective_family(std::string_view requested) {
    return family_available(requested) ? std::string(requested) : "default";
}

std::string effective_weight(std::string_view family,
                             std::string_view requested) {
    const std::string resolved_family = effective_family(family);
    return weight_available(resolved_family, requested) ? std::string(requested)
                                                        : "regular";
}

void apply(afterhours::ui::FontManager& manager, std::string_view family,
           std::string_view emphasis) {
    Runtime& state = runtime();
    if (!state.ready) preload(manager);

    const std::string selected_family = effective_family(family);
    const std::string selected_emphasis =
        effective_weight(selected_family, emphasis);
    if (state.applied_family == selected_family &&
        state.applied_emphasis == selected_emphasis)
        return;

    Family* selected = find_family(selected_family);
    Family* fallback = find_family("default");
    if (selected == nullptr || fallback == nullptr) return;

    afterhours::Font regular = load_face(manager, selected_family, "regular",
                                         selected->paths.at("regular"));
    if (!afterhours::is_font_loaded(regular)) {
        regular = load_face(manager, "default", "regular",
                            fallback->paths.at("regular"));
    }

    afterhours::Font emphasized = regular;
    if (selected_emphasis != "regular") {
        emphasized = load_face(manager, selected_family, selected_emphasis,
                               selected->paths.at(selected_emphasis));
        if (!afterhours::is_font_loaded(emphasized)) emphasized = regular;
    }

    const std::string base = afterhours::ui::UIComponent::DEFAULT_FONT;
    manager.load_font(base, regular);
    manager.load_font(base + "@light", regular);
    manager.load_font(base + "@medium", emphasized);
    manager.load_font(base + "@semibold", emphasized);
    manager.load_font(base + "@bold", emphasized);
    manager.set_active(base);

    theme::type::set_point_scale(selected->point_scale);
    auto& sizing = afterhours::ui::imm::ThemeDefaults::get().theme.font_sizing;
    sizing.small = 10.0f * selected->point_scale;
    sizing.medium = 12.0f * selected->point_scale;
    sizing.large = 14.0f * selected->point_scale;
    sizing.xl = 17.0f * selected->point_scale;

    state.applied_family = selected_family;
    state.applied_emphasis = selected_emphasis;
    hanabi::text::bump_font_epoch();
    if (const char* audit = std::getenv("HANABI_FONT_AUDIT");
        audit != nullptr && *audit != '\0' && std::strcmp(audit, "0") != 0) {
        const char* sample = "Agentcloud Wg 0123456789";
        const float sampleSize = 13.0f * selected->point_scale;
        const auto regularBox =
            afterhours::measure_text(regular, sample, sampleSize, 1.0f);
        const auto emphasisBox =
            afterhours::measure_text(emphasized, sample, sampleSize, 1.0f);
        const float regularAdvance = measure_advance(
            sample, sampleSize, afterhours::colors::FontWeight::Regular);
        const float emphasisAdvance = measure_advance(
            sample, sampleSize, afterhours::colors::FontWeight::SemiBold);
        std::fprintf(stderr,
                     "[font] requested=%.*s/%.*s effective=%s/%s "
                     "regular_id=%d emphasis_id=%d epoch=%u scale=%.5f dpi=%.2f "
                     "regular_advance=%.3f regular_ink=%.3f line=%.3f "
                     "emphasis_advance=%.3f emphasis_ink=%.3f line=%.3f\n",
                     static_cast<int>(family.size()), family.data(),
                     static_cast<int>(emphasis.size()), emphasis.data(),
                     selected_family.c_str(), selected_emphasis.c_str(),
                     regular.id, emphasized.id, hanabi::text::font_epoch(),
                     selected->point_scale,
                     afterhours::graphics::metal_detail::dpi_scale(), regularAdvance,
                     regularBox.x, regularBox.y, emphasisAdvance, emphasisBox.x,
                     emphasisBox.y);
    }
    if (auto* cache = afterhours::EntityHelper::get_singleton_cmp<
            afterhours::ui::TextMeasureCache>())
        cache->clear();
}

float measure_advance(const char* text, float size,
                      afterhours::colors::FontWeight weight) {
    if (text == nullptr || *text == '\0') return 0.0f;
    auto* manager = afterhours::EntityHelper::get_singleton_cmp<
        afterhours::ui::FontManager>();
    auto* ctx = afterhours::graphics::metal_detail::g_fons_ctx;
    if (manager == nullptr || ctx == nullptr) return 0.0f;
    const std::string name = manager->resolve_weighted(
        afterhours::ui::UIComponent::DEFAULT_FONT, weight);
    const auto it = manager->fonts.find(name);
    if (it == manager->fonts.end() || !afterhours::is_font_loaded(it->second))
        return 0.0f;
    fonsSetFont(ctx, it->second.id);
    const float dpi = afterhours::graphics::metal_detail::dpi_scale();
    fonsSetSize(ctx, size * dpi);
    fonsSetAlign(ctx, FONS_ALIGN_LEFT | FONS_ALIGN_TOP);
    return fonsTextBounds(ctx, 0.0f, 0.0f, text, nullptr, nullptr) / dpi;
}

}  // namespace hanabi::fonts
