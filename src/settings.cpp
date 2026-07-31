#include "settings.h"

#include <afterhours/src/plugins/files.h>

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
        open_tabs_.clear();
        if (j.contains("open_tabs") && j["open_tabs"].is_array()) {
            for (const auto& e : j["open_tabs"])
                if (e.is_string()) open_tabs_.push_back(e.get<std::string>());
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
