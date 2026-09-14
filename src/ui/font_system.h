#pragma once

#include <afterhours/src/plugins/color.h>
#include "font_plan.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace afterhours::ui {
struct FontManager;
}

namespace hanabi::fonts {

struct Choice {
    std::string key;
    std::string label;
};

// What preload/apply actually registered: one row per alias, in plan order,
// with whether the face file loaded. For HANABI_FONT_AUDIT and the e2e probe.
struct AliasReport {
    AliasPlan plan;
    std::string family;
    bool loaded = false;
};
const std::vector<AliasReport>& report();
const std::string& default_source();

void preload(afterhours::ui::FontManager& manager);
void apply(afterhours::ui::FontManager& manager, std::string_view family,
           std::string_view emphasis);
const std::vector<Choice>& families();
const std::vector<Choice>& weights();
bool family_available(std::string_view key);
bool weight_available(std::string_view family, std::string_view weight);
std::string effective_family(std::string_view requested);
// The registered font name for a family+weight, which is what a widget's
// with_font() takes. Empty when the pair was never loaded, so a caller can
// fall back to the app default rather than asking for a face that is not
// there.
std::string face_name(std::string_view family, std::string_view weight);
std::string effective_weight(std::string_view family,
                             std::string_view requested);
float measure_advance(const char* text, float size,
                      afterhours::colors::FontWeight weight =
                          afterhours::colors::FontWeight::Regular);
// The same advance, in a NAMED registered face ("mono", ...) rather than the
// app face -- for a chip sized around glyphs it draws in that face. 0 when the
// face is not registered or not loaded.
float measure_advance_in(const char* font_name, const char* text, float size);

}  // namespace hanabi::fonts
