#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "../../vendor/nlohmann/json.hpp"

namespace hanabi::settings_transfer {

inline constexpr const char* kFileName = "hanabi-settings.json";
inline constexpr int kFormat = 1;
inline constexpr const char* kApp = "hanabi";

inline constexpr std::array<std::string_view, 63> kAllowed{
    "theme", "font", "font_weight", "theme_accent", "theme_highlight", "theme_rotate_secs", "icon_set",
    "text_scale", "user_text_scale", "assistant_text_scale", "user_font", "assistant_font", "line_spacing",
    "transcript_width_choice", "show_timestamps", "show_date_dividers", "show_reasoning", "show_minimap",
    "minimap_hidden_marks", "show_finished_subagents", "fold_long_messages", "jump_to_latest",
    "find_newest_first", "context_detail", "disclosure_chips_open", "diff_cards", "changed_file_rows",
    "send_chords", "send_key", "native_diff_companion", "yap_level", "restore_tabs", "confirm_quit",
    "escape_stops", "inbox_triage", "triage_bindings", "session_grouping", "session_sort_order",
    "only_ungrouped", "notifications_enabled", "notify_finished", "notify_blocked", "notify_waiting",
    "notify_subagents", "notify_updates", "run_chime", "quiet_start_minutes", "quiet_end_minutes", "global_shortcuts",
    "global_shortcuts_off", "shortcuts", "shortcuts_off", "capture_hotkey", "composer_templates",
    "default_model", "default_effort", "cache_cap_bytes", "thread_colours", "session_theme",
    "background", "background_dim",
    "sound_override", "send_usage_data"};

inline bool is_allowed(std::string_view key) {
    for (std::string_view k : kAllowed)
        if (k == key) return true;
    return false;
}

inline std::string export_path(std::string_view dir) {
    std::string out(dir);
    if (!out.empty() && out.back() != '/') out += '/';
    out += kFileName;
    return out;
}

inline nlohmann::json envelope(const nlohmann::json& settingsFile, std::string_view exportedAt) {
    nlohmann::json settings = nlohmann::json::object();
    if (settingsFile.is_object())
        for (const auto& [k, v] : settingsFile.items())
            if (is_allowed(k)) settings[k] = v;
    return nlohmann::json{{"format", kFormat}, {"app", kApp}, {"exported_at", std::string(exportedAt)},
                          {"settings", settings}};
}

enum class Verdict { Ok, Empty, NotJsonObject, NotSettings, WrongFormat };

struct Extract {
    Verdict verdict = Verdict::NotSettings;
    nlohmann::json settings = nlohmann::json::object();
    std::vector<std::string> keys;
};

inline Extract extract(std::string_view text) {
    Extract out;
    std::size_t i = 0;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\n' || text[i] == '\r' || text[i] == '\t')) ++i;
    if (i >= text.size()) {
        out.verdict = Verdict::Empty;
        return out;
    }
    const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        out.verdict = Verdict::NotJsonObject;
        return out;
    }
    const nlohmann::json* body = &root;
    if (root.contains("settings") && root["settings"].is_object()) {
        if (root.contains("format") && (!root["format"].is_number_integer() || root["format"].get<int>() != kFormat)) {
            out.verdict = Verdict::WrongFormat;
            return out;
        }
        if (root.contains("app") && (!root["app"].is_string() || root["app"].get<std::string>() != kApp)) {
            out.verdict = Verdict::NotSettings;
            return out;
        }
        body = &root["settings"];
    }
    for (const auto& [k, v] : body->items())
        if (is_allowed(k)) {
            out.settings[k] = v;
            out.keys.push_back(k);
        }
    out.verdict = out.keys.empty() ? Verdict::NotSettings : Verdict::Ok;
    return out;
}

inline std::string_view reason(Verdict v) {
    switch (v) {
        case Verdict::Ok: return "";
        case Verdict::Empty: return "the file is empty";
        case Verdict::NotJsonObject: return "not a JSON object";
        case Verdict::NotSettings: return "no Hanabi settings in it";
        case Verdict::WrongFormat: return "a newer settings format than this build reads";
    }
    return "";
}

inline std::string preview_line(const std::vector<std::string>& keys, std::string_view fileLabel) {
    std::string out = std::to_string(keys.size()) + (keys.size() == 1 ? " setting" : " settings") + " from " +
                      std::string(fileLabel) + ": ";
    for (std::size_t i = 0; i < keys.size() && i < 4; ++i) {
        if (i > 0) out += ", ";
        out += keys[i];
    }
    if (keys.size() > 4) out += " and " + std::to_string(keys.size() - 4) + " more";
    out += ". Apply replaces those; everything else stays.";
    return out;
}

inline nlohmann::json merge(nlohmann::json current, const nlohmann::json& incoming) {
    if (!current.is_object()) current = nlohmann::json::object();
    for (const auto& [k, v] : incoming.items())
        if (is_allowed(k)) current[k] = v;
    return current;
}

}
