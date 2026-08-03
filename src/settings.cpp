#include "settings.h"

#include <afterhours/src/plugins/files.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "../vendor/nlohmann/json.hpp"

using json = nlohmann::json;

Settings::Settings() {}
Settings::~Settings() {}

std::string Settings::get_settings_path() const {
    // afterhours' get_config_path() already returns a per-app config dir
    // (it appends the app name, e.g. .../Application Support/hanabi). Only add
    // our own "hanabi" subdir when falling back to the bare cwd, otherwise we'd
    // nest as .../hanabi/hanabi and settings would silently never load.
    auto configDir = afterhours::files::get_config_path();
    if (configDir.empty()) {
        configDir = std::filesystem::current_path() / "hanabi";
    }
    std::error_code ec;
    std::filesystem::create_directories(configDir, ec);
    return (configDir / "settings.json").string();
}

bool Settings::load_save_file() {
    std::ifstream in(get_settings_path());
    if (!in.good()) return false;
    try {
        json j;
        in >> j;
        window_width_ = j.value("window_width", window_width_);
        window_height_ = j.value("window_height", window_height_);
        last_session_ = j.value("last_session", last_session_);
        active_tab_ = j.value("active_tab", active_tab_);
        theme_ = j.value("theme", theme_);
        font_choice_ = j.value("font", font_choice_);
        sidebar_collapsed_ = j.value("sidebar_collapsed", sidebar_collapsed_);
        cache_cap_bytes_ =
            j.value("cache_cap_bytes", cache_cap_bytes_);
        open_tabs_.clear();
        if (j.contains("open_tabs") && j["open_tabs"].is_array()) {
            for (const auto& e : j["open_tabs"])
                if (e.is_string()) open_tabs_.push_back(e.get<std::string>());
        }
        starred_ids_.clear();
        if (j.contains("starred") && j["starred"].is_array()) {
            for (const auto& e : j["starred"])
                if (e.is_string()) starred_ids_.push_back(e.get<std::string>());
        }
    } catch (...) {
        return false;
    }
    return true;
}

void Settings::write_save_file() {
    if (!auto_save_enabled) {
        // Explicit writes still go through; auto_save just gates callers.
    }
    json j;
    j["window_width"] = window_width_;
    j["window_height"] = window_height_;
    j["last_session"] = last_session_;
    j["open_tabs"] = open_tabs_;
    j["active_tab"] = active_tab_;
    j["theme"] = theme_;
    j["font"] = font_choice_;
    j["sidebar_collapsed"] = sidebar_collapsed_;
    j["cache_cap_bytes"] = cache_cap_bytes_;
    j["starred"] = starred_ids_;
    std::ofstream out(get_settings_path());
    if (out.good()) out << j.dump(2);
}

int Settings::get_window_width() const { return window_width_; }
int Settings::get_window_height() const { return window_height_; }
void Settings::set_window_geometry(int w, int h) {
    window_width_ = w;
    window_height_ = h;
}
const std::string& Settings::get_last_session() const { return last_session_; }
void Settings::set_last_session(const std::string& id) { last_session_ = id; }

const std::vector<std::string>& Settings::get_open_tabs() const {
    return open_tabs_;
}
const std::string& Settings::get_active_tab() const { return active_tab_; }
void Settings::set_open_tabs(std::vector<std::string> ids,
                             std::string activeId) {
    open_tabs_ = std::move(ids);
    active_tab_ = std::move(activeId);
}

const std::string& Settings::get_theme() const { return theme_; }
void Settings::set_theme(const std::string& mode) {
    theme_ = mode;
    // Persist immediately so a theme change survives relaunch without callers
    // having to remember to write_save_file() themselves.
    if (auto_save_enabled) write_save_file();
}

const std::string& Settings::get_font_choice() const { return font_choice_; }
void Settings::set_font_choice(const std::string& font) {
    if (font == font_choice_) return;  // no change — skip the write
    font_choice_ = font;
    // Persist immediately (mirrors set_theme) so the font choice survives
    // relaunch. Client-local only — never sent to the backend.
    if (auto_save_enabled) write_save_file();
}

bool Settings::get_sidebar_collapsed() const { return sidebar_collapsed_; }
void Settings::set_sidebar_collapsed(bool collapsed) {
    if (sidebar_collapsed_ == collapsed) return;  // no-op, no needless write
    sidebar_collapsed_ = collapsed;
    if (auto_save_enabled) write_save_file();
}

std::uint64_t Settings::get_cache_cap_bytes() const { return cache_cap_bytes_; }
void Settings::set_cache_cap_bytes(std::uint64_t bytes) {
    if (bytes == cache_cap_bytes_) return;  // no change — skip the write
    cache_cap_bytes_ = bytes;
    // Persist immediately (mirrors set_theme) so the cap survives relaunch.
    if (auto_save_enabled) write_save_file();
}

const std::vector<std::string>& Settings::get_starred() const {
    return starred_ids_;
}
bool Settings::is_starred(const std::string& id) const {
    for (const auto& s : starred_ids_)
        if (s == id) return true;
    return false;
}
void Settings::set_starred(const std::string& id, bool starred) {
    auto it = std::find(starred_ids_.begin(), starred_ids_.end(), id);
    const bool present = (it != starred_ids_.end());
    if (starred && !present) {
        starred_ids_.push_back(id);
    } else if (!starred && present) {
        starred_ids_.erase(it);
    } else {
        return;  // no change — skip the write
    }
    // Persist immediately (mirrors set_theme) so a star survives relaunch.
    if (auto_save_enabled) write_save_file();
}
