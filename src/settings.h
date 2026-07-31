#pragma once

#include <afterhours/src/singleton.h>

#include <string>

// Minimal persisted settings: window geometry + which session was last open.
// Stored as JSON next to the platform config dir.
SINGLETON_FWD(Settings)
struct Settings {
    SINGLETON(Settings)

    Settings();
    ~Settings();

    Settings(const Settings&) = delete;
    void operator=(const Settings&) = delete;

    bool load_save_file();
    void write_save_file();

    int get_window_width() const;
    int get_window_height() const;
    void set_window_geometry(int w, int h);

    const std::string& get_last_session() const;
    void set_last_session(const std::string& id);

    std::string get_settings_path() const;

    bool auto_save_enabled = true;

  private:
    int window_width_ = 1100;
    int window_height_ = 760;
    std::string last_session_;
};
