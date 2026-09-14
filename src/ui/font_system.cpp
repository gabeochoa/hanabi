#include "font_system.h"
#include "font_plan.h"

#include <afterhours/src/plugins/files.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "../native_extras.h"
#include "../rl.h"
#include "../util/atlas_guard.h"
#include "../util/text_epoch.h"
#include "theme.h"
#include "theme_config.h"

namespace hanabi::fonts {
namespace {

// Real font files the manager can hold: preload adds three (the bundled
// regular, mono, hyperlegible); each applied family adds up to four (regular,
// medium, semibold, bold), de-duplicated by name -- three family switches in
// one run is the most the slot count allows.
static_assert(3 + 3 * 4 <= afterhours::graphics::metal_detail::MAX_FONTS);
static_assert(std::string_view(kDefaultFontName) ==
              std::string_view(afterhours::ui::UIComponent::DEFAULT_FONT));

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
    std::string default_source;
    std::vector<AliasReport> report;
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
    // HANABI_FONT_DEFAULT=bundled: the "default" family is the bundled files
    // and NOTHING the machine has -- the machine's faces (the reference's, the
    // platform's, and any installed heavier weights of the bundled family) are
    // kept out of it, whatever the headless allow flag says, so a test can
    // stand on a machine with no installed faces. Explicit families (system,
    // optimistic) still resolve from the catalog as usual.
    const char* pin = std::getenv("HANABI_FONT_DEFAULT");
    const bool pinBundled = pin != nullptr && std::strcmp(pin, "bundled") == 0;
    if (allow_native()) add_native_catalog();
    if (pinBundled) {
        if (Family* def = find_family("default")) {
            def->paths = {{"regular", regular}};
            def->point_scale = 1.17185f;
        }
        if (Family* hyp = find_family("hyperlegible")) hyp->paths = {{"regular", hyper}};
    }

    // "default" (Standard) is the reference's default face where the machine
    // has it -- the reference names its faces by family and falls back to the
    // platform UI face where that family is not installed -- and the bundled
    // face only where neither is. An explicit family choice (system,
    // optimistic, hyperlegible) is never touched by this.
    {
        const Family* reference = pinBundled ? nullptr : find_family("optimistic");
        const Family* platform = pinBundled ? nullptr : find_family("system");
        state.default_source = pick_default_source(
            reference != nullptr && reference->paths.contains("regular"),
            platform != nullptr && platform->paths.contains("regular"));
        Family* def = find_family("default");
        const Family* from = state.default_source == "optimistic" ? reference
                             : state.default_source == "system"  ? platform
                                                                  : nullptr;
        if (def != nullptr && from != nullptr) {
            def->paths = from->paths;
            def->point_scale = from->point_scale;
        }
    }

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
    // Until apply() runs, every weight alias draws the bundled regular and the
    // report says so (fallback): a component asking for a weight before the
    // preference is applied gets text, not a missing-font warning.
    {
        const std::string base = afterhours::ui::UIComponent::DEFAULT_FONT;
        const afterhours::Font bundled = manager.get_font(base);
        state.report.clear();
        for (const char* w : {"light", "medium", "semibold", "bold"}) {
            const std::string alias = base + "@" + w;
            manager.load_font(alias, bundled);
            state.report.push_back(
                {AliasPlan{alias, w, "regular", regular, true}, "default",
                 afterhours::is_font_loaded(bundled)});
        }
    }

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

std::string face_name(std::string_view family, std::string_view weight) {
    const std::string resolved = effective_family(family);
    const std::string w = effective_weight(resolved, weight);
    const Family* found = find_family(resolved);
    if (found == nullptr || !found->paths.contains(w)) return {};
    return registered_name(resolved, w);
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
    std::string regular_family = selected_family;
    if (!afterhours::is_font_loaded(regular)) {
        regular = load_face(manager, "default", "regular",
                            fallback->paths.at("regular"));
        regular_family = "default";
    }
    const Family* faces_of = find_family(regular_family);

    // One real face per alias, from the family's own files; a weight the
    // family lacks falls down its ladder and the report says so. The
    // registration IS the plan: nothing is registered that the report does
    // not list, and nothing listed that was not registered.
    std::vector<std::pair<std::string, std::string>> have;
    for (const auto& [w, path] : faces_of->paths) have.emplace_back(w, path);
    // The REQUESTED emphasis goes to the plan, not the availability-clamped
    // one: a chosen Semibold on a family with no such face is a fallback the
    // report must call a fallback, not "emphasis off by choice".
    const std::vector<AliasPlan> plan = plan_aliases(have, emphasis);
    state.report.clear();
    afterhours::Font emphasized = regular;
    for (const AliasPlan& row : plan) {
        afterhours::Font face =
            row.weight == "regular"
                ? regular
                : load_face(manager, regular_family, row.weight, row.path);
        bool loaded = afterhours::is_font_loaded(face);
        AliasPlan registered = row;
        if (!loaded) {
            // The file named a face the loader could not open: honest
            // fallback to the regular face, recorded as such.
            face = regular;
            registered.weight = "regular";
            registered.path = faces_of->paths.at("regular");
            registered.fallback = true;
            loaded = afterhours::is_font_loaded(face);
        }
        manager.load_font(row.alias, face);
        if (row.alias == afterhours::ui::UIComponent::DEFAULT_FONT + std::string("@semibold"))
            emphasized = face;
        state.report.push_back({registered, regular_family, loaded});
    }
    const std::string base = afterhours::ui::UIComponent::DEFAULT_FONT;
    manager.set_active(base);

    theme::type::set_point_scale(selected->point_scale);
    auto& sizing = afterhours::ui::imm::ThemeDefaults::get().theme.font_sizing;
    sizing.small = 10.0f * selected->point_scale;
    sizing.medium = 12.0f * selected->point_scale;
    sizing.large = 14.0f * selected->point_scale;
    sizing.xl = 17.0f * selected->point_scale;
    // A font swap is app configuration, not a one-frame override, so it has to
    // reach the slot begin_frame restores from (ui/theme_config.h).
    hanabi::ui::publish_app_theme();

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
        for (const AliasReport& r : state.report)
            std::fprintf(stderr, "[font] alias=%s wanted=%s got=%s%s family=%s file=%s%s\n",
                         r.plan.alias.c_str(), r.plan.wanted.c_str(),
                         r.plan.weight.c_str(), r.plan.fallback ? " (fallback)" : "",
                         r.family.c_str(), r.plan.path.c_str(),
                         r.loaded ? "" : " NOT LOADED");
        std::fprintf(stderr, "[font] default_source=%s\n", state.default_source.c_str());
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

const std::vector<AliasReport>& report() { return runtime().report; }
const std::string& default_source() { return runtime().default_source; }

namespace {
float advance_in_registered(const std::string& name, const char* text, float size) {
    if (text == nullptr || *text == '\0') return 0.0f;
    auto* manager = afterhours::EntityHelper::get_singleton_cmp<
        afterhours::ui::FontManager>();
    auto* ctx = afterhours::graphics::metal_detail::g_fons_ctx;
    if (manager == nullptr || ctx == nullptr) return 0.0f;
    const auto it = manager->fonts.find(name);
    if (it == manager->fonts.end() || !afterhours::is_font_loaded(it->second))
        return 0.0f;
    fonsSetFont(ctx, it->second.id);
    const float dpi = afterhours::graphics::metal_detail::dpi_scale();
    fonsSetSize(ctx, size * dpi);
    fonsSetAlign(ctx, FONS_ALIGN_LEFT | FONS_ALIGN_TOP);
    return fonsTextBounds(ctx, 0.0f, 0.0f, text, nullptr, nullptr) / dpi;
}
}  // namespace

float measure_advance(const char* text, float size,
                      afterhours::colors::FontWeight weight) {
    auto* manager = afterhours::EntityHelper::get_singleton_cmp<
        afterhours::ui::FontManager>();
    if (manager == nullptr) return 0.0f;
    return advance_in_registered(
        manager->resolve_weighted(afterhours::ui::UIComponent::DEFAULT_FONT, weight), text,
        size);
}

float measure_advance_in(const char* font_name, const char* text, float size) {
    if (font_name == nullptr || *font_name == '\0') return 0.0f;
    return advance_in_registered(font_name, text, size);
}

}  // namespace hanabi::fonts
