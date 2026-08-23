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
        yap_level_ = j.value("yap_level", yap_level_);
        auto_archive_days_ = j.value("auto_archive_days", auto_archive_days_);
        notification_sound_ = j.value("notification_sound", notification_sound_);
        show_timestamps_ = j.value("show_timestamps", show_timestamps_);
        quiet_start_minutes_ =
            j.value("quiet_start_minutes", quiet_start_minutes_);
        quiet_end_minutes_ = j.value("quiet_end_minutes", quiet_end_minutes_);
        memory_backend_ = j.value("memory_backend", memory_backend_);
        default_model_ = j.value("default_model", default_model_);
        open_tabs_.clear();
        if (j.contains("open_tabs") && j["open_tabs"].is_array()) {
            for (const auto& e : j["open_tabs"])
                if (e.is_string()) open_tabs_.push_back(e.get<std::string>());
        }
        last_read_.clear();
        if (j.contains("last_read") && j["last_read"].is_object()) {
            for (const auto& [k, v] : j["last_read"].items())
                if (v.is_number_integer())
                    last_read_[k] = v.get<int64_t>();
        }
        starred_ids_.clear();
        if (j.contains("starred") && j["starred"].is_array()) {
            for (const auto& e : j["starred"])
                if (e.is_string()) starred_ids_.push_back(e.get<std::string>());
        }
        collapsed_shelves_.clear();
        if (j.contains("collapsed_shelves") &&
            j["collapsed_shelves"].is_array()) {
            for (const auto& e : j["collapsed_shelves"])
                if (e.is_string())
                    collapsed_shelves_.push_back(e.get<std::string>());
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
    j["yap_level"] = yap_level_;
    j["auto_archive_days"] = auto_archive_days_;
    j["notification_sound"] = notification_sound_;
    j["show_timestamps"] = show_timestamps_;
    j["quiet_start_minutes"] = quiet_start_minutes_;
    j["quiet_end_minutes"] = quiet_end_minutes_;
    j["memory_backend"] = memory_backend_;
    j["default_model"] = default_model_;
    j["starred"] = starred_ids_;
    j["collapsed_shelves"] = collapsed_shelves_;
    j["last_read"] = last_read_;
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

const std::vector<std::string>& Settings::get_collapsed_shelves() const {
    return collapsed_shelves_;
}
bool Settings::is_shelf_collapsed(const std::string& key) const {
    for (const auto& s : collapsed_shelves_)
        if (s == key) return true;
    return false;
}
void Settings::set_shelf_collapsed(const std::string& key, bool collapsed) {
    auto it = std::find(collapsed_shelves_.begin(), collapsed_shelves_.end(),
                        key);
    const bool present = (it != collapsed_shelves_.end());
    if (collapsed && !present) {
        collapsed_shelves_.push_back(key);
    } else if (!collapsed && present) {
        collapsed_shelves_.erase(it);
    } else {
        return;
    }
    if (auto_save_enabled) write_save_file();
}

int64_t Settings::get_last_read(const std::string& id) const {
    auto it = last_read_.find(id);
    return it == last_read_.end() ? 0 : it->second;
}
void Settings::set_last_read(const std::string& id, int64_t stamp) {
    if (id.empty() || stamp <= 0) return;
    auto it = last_read_.find(id);
    if (it != last_read_.end() && it->second == stamp) return;  // no churn
    last_read_[id] = stamp;
    if (auto_save_enabled) write_save_file();
}

// ── Preference slots. Each auto-persists (mirrors set_theme) AND marks the
// sync-dirty flag so the loader can push the change to the backend. No-op
// writes are skipped so re-selecting the current value doesn't churn.
int Settings::get_yap_level() const { return yap_level_; }
void Settings::set_yap_level(int level) {
    if (level == yap_level_) return;
    yap_level_ = level;
    settings_dirty_ = true;
    if (auto_save_enabled) write_save_file();
}

int Settings::get_auto_archive_days() const { return auto_archive_days_; }
void Settings::set_auto_archive_days(int days) {
    if (days == auto_archive_days_) return;
    auto_archive_days_ = days;
    settings_dirty_ = true;
    if (auto_save_enabled) write_save_file();
}

bool Settings::get_notification_sound() const { return notification_sound_; }
int Settings::get_quiet_start_minutes() const { return quiet_start_minutes_; }
int Settings::get_quiet_end_minutes() const { return quiet_end_minutes_; }
void Settings::set_quiet_window(int startMinutes, int endMinutes) {
    if (startMinutes == quiet_start_minutes_ &&
        endMinutes == quiet_end_minutes_)
        return;
    quiet_start_minutes_ = startMinutes;
    quiet_end_minutes_ = endMinutes;
    settings_dirty_ = true;
    if (auto_save_enabled) write_save_file();
}
void Settings::set_notification_sound(bool on) {
    if (on == notification_sound_) return;
    notification_sound_ = on;
    settings_dirty_ = true;
    if (auto_save_enabled) write_save_file();
}

bool Settings::get_show_timestamps() const { return show_timestamps_; }
void Settings::set_show_timestamps(bool on) {
    if (on == show_timestamps_) return;
    show_timestamps_ = on;
    if (auto_save_enabled) write_save_file();
}

const std::string& Settings::get_memory_backend() const {
    return memory_backend_;
}
void Settings::set_memory_backend(const std::string& backend) {
    if (backend == memory_backend_) return;
    memory_backend_ = backend;
    settings_dirty_ = true;
    if (auto_save_enabled) write_save_file();
}

const std::string& Settings::get_default_model() const { return default_model_; }
void Settings::set_default_model(const std::string& model) {
    if (model == default_model_) return;
    default_model_ = model;
    settings_dirty_ = true;
    if (auto_save_enabled) write_save_file();
}

bool Settings::is_settings_dirty() const { return settings_dirty_; }
void Settings::mark_settings_dirty() { settings_dirty_ = true; }
void Settings::clear_settings_dirty() { settings_dirty_ = false; }
