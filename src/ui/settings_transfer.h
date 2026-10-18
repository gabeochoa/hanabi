#pragma once

#include <array>
#include <string>
#include <string_view>

namespace hanabi::settings_transfer {

inline constexpr const char* kFileName = "hanabi-settings.json";

inline constexpr std::array<std::string_view, 6> kMarkers{"theme", "window_width", "font",
                                                          "sound_override", "restore_tabs",
                                                          "settings_pane"};

inline std::string export_path(std::string_view dir) {
    std::string out(dir);
    if (!out.empty() && out.back() != '/') out += '/';
    out += kFileName;
    return out;
}

enum class Verdict { Ok, Empty, NotJsonObject, NotSettings };

inline Verdict inspect(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\n' || text[i] == '\r' || text[i] == '\t')) ++i;
    if (i >= text.size()) return Verdict::Empty;
    if (text[i] != '{') return Verdict::NotJsonObject;
    for (std::string_view m : kMarkers) {
        std::string key = "\"" + std::string(m) + "\"";
        if (text.find(key) != std::string_view::npos) return Verdict::Ok;
    }
    return Verdict::NotSettings;
}

inline std::string_view reason(Verdict v) {
    switch (v) {
        case Verdict::Ok: return "";
        case Verdict::Empty: return "the file is empty";
        case Verdict::NotJsonObject: return "not a JSON object";
        case Verdict::NotSettings: return "no Hanabi settings in it";
    }
    return "";
}

}
