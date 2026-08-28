#pragma once

#include <afterhours/src/plugins/color.h>

#include <string>
#include <string_view>
#include <vector>

namespace afterhours::ui {
struct FontManager;
}

namespace hanabi::fonts {

struct Choice {
    std::string key;
    std::string label;
};

void preload(afterhours::ui::FontManager& manager);
void apply(afterhours::ui::FontManager& manager, std::string_view family,
           std::string_view emphasis);
const std::vector<Choice>& families();
const std::vector<Choice>& weights();
bool family_available(std::string_view key);
bool weight_available(std::string_view family, std::string_view weight);
std::string effective_family(std::string_view requested);
std::string effective_weight(std::string_view family,
                             std::string_view requested);
float measure_advance(const char* text, float size,
                      afterhours::colors::FontWeight weight =
                          afterhours::colors::FontWeight::Regular);

}  // namespace hanabi::fonts
