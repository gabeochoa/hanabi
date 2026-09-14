#pragma once
// The pure part of the font loader: which real face each weight alias draws,
// and which installed family "default" means. Header-only and free of the
// graphics backend so a unit test can drive exactly the rule apply() registers.
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hanabi::fonts {

// afterhours::ui::UIComponent::DEFAULT_FONT, spelled here so this header needs
// no UI include; font_system.cpp asserts the two agree.
inline constexpr const char* kDefaultFontName = "__default";

// -- The plan: which real face each weight alias draws -----------------------
// Afterhours resolves a component's weight by exact name: `__default@semibold`
// must be REGISTERED, or the text draws `__default` at its normal weight
// (FontManager::resolve_weighted). Nothing is synthesized, so every alias has
// to name a real face file. `plan_aliases` decides which, from the faces a
// family actually has (weight -> path) and the reader's Emphasis preference:
//   * `__default`  is always the family's regular face.
//   * "regular" as the Emphasis preference means NO emphasis: every alias is
//     the regular face, by choice (reported as the preference, not a fallback).
//   * otherwise `@semibold` -- the app's EMPHASIS token -- is the chosen weight,
//     `@medium` the medium face and `@bold` the bold face, each falling down its
//     ladder (semibold -> medium -> regular; bold -> semibold -> regular;
//     medium -> regular) to the nearest REAL face the family has, and saying
//     so: `fallback` is true whenever the alias did not get the weight it names.
// Pure, so the rule is unit-tested against fake catalogs; `apply` registers
// exactly what it returns and `report()` says what was registered.
struct AliasPlan {
    std::string alias;   // "__default", "__default@semibold", ...
    std::string wanted;  // the weight the alias names
    std::string weight;  // the weight of the face it actually got
    std::string path;    // the face file registered under the alias
    bool fallback = false;
    bool operator==(const AliasPlan&) const = default;
};

// Which installed family the "default" (Standard) choice resolves to: the
// reference's own default face where it is installed, the platform UI face
// where only that is, the bundled face otherwise. Pure; `default_source()`
// reports the one preload picked.


inline std::vector<AliasPlan> plan_aliases(
    const std::vector<std::pair<std::string, std::string>>& faces,
    std::string_view emphasis) {
    const auto path_of = [&](std::string_view w) -> const std::string* {
        for (const auto& [weight, path] : faces)
            if (weight == w) return &path;
        return nullptr;
    };
    const std::string base = kDefaultFontName;
    const std::string* regular = path_of("regular");
    const std::string regularPath = regular != nullptr ? *regular : std::string();
    // The ladder each alias falls down: the weight it names first, then the
    // nearest lighter real face, regular last.
    const auto ladder = [](std::string_view w) -> std::vector<std::string> {
        if (w == "bold") return {"bold", "semibold", "medium", "regular"};
        if (w == "semibold") return {"semibold", "medium", "regular"};
        if (w == "medium") return {"medium", "regular"};
        return {"regular"};
    };
    const auto resolve = [&](std::string_view alias_suffix, std::string_view wanted,
                             std::string_view target) {
        AliasPlan row;
        row.alias = base + std::string(alias_suffix);
        row.wanted = std::string(wanted);
        for (const std::string& step : ladder(target)) {
            if (const std::string* p = path_of(step)) {
                row.weight = step;
                row.path = *p;
                row.fallback = step != wanted;
                return row;
            }
        }
        row.weight = "regular";
        row.path = regularPath;
        row.fallback = wanted != "regular";
        return row;
    };
    std::vector<AliasPlan> out;
    out.push_back(resolve("", "regular", "regular"));
    // "light" has no face in any catalog hanabi knows; it is the regular face.
    out.push_back(resolve("@light", "light", "regular"));
    if (emphasis == "regular") {
        // Emphasis off, by preference: every weight alias is the regular face,
        // and that is the choice, not a fallback.
        for (const char* w : {"medium", "semibold", "bold"}) {
            AliasPlan row = resolve(std::string("@") + w, w, "regular");
            row.fallback = false;
            out.push_back(row);
        }
        return out;
    }
    out.push_back(resolve("@medium", "medium", "medium"));
    // The app's EMPHASIS token draws the CHOSEN weight -- what the Emphasis
    // setting means -- so its fallback flag is against that choice.
    {
        AliasPlan row = resolve("@semibold", emphasis, emphasis);
        row.wanted = "semibold";
        row.fallback = row.weight != emphasis;
        out.push_back(row);
    }
    out.push_back(resolve("@bold", "bold", "bold"));
    return out;
}

inline std::string pick_default_source(bool has_reference_face, bool has_platform_face) {
    if (has_reference_face) return "optimistic";
    if (has_platform_face) return "system";
    return "bundled";
}


}  // namespace hanabi::fonts
