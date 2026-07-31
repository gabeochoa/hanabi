#include "settings.h"

#include <afterhours/src/plugins/files.h>

#include <filesystem>
#include <fstream>

#include "../vendor/nlohmann/json.hpp"

using json = nlohmann::json;

Settings::Settings() {}
Settings::~Settings() {}

std::string Settings::get_settings_path() const {
    auto configDir = afterhours::files::get_config_path();
    if (configDir.empty()) {
        configDir = std::filesystem::current_path();
    }
    configDir /= "hanabi";
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
