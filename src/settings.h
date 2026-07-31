#pragma once

#include <afterhours/src/singleton.h>

#include <string>
#include <vector>

// Minimal persisted settings: window geometry, theme mode, and the open-tab
// set (session ids + which one is active) so a launch restores exactly where
// the user left off. Stored as JSON next to the platform config dir.
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

    // Open tabs (ordered session ids) + which one was active.
    const std::vector<std::string>& get_open_tabs() const;
    const std::string& get_active_tab() const;
    void set_open_tabs(std::vector<std::string> ids, std::string activeId);

    // Theme mode: "dark" (default) or "light".
    const std::string& get_theme() const;
    void set_theme(const std::string& mode);

    std::string get_settings_path() const;

    bool auto_save_enabled = true;

  private:
    int window_width_ = 1100;
    int window_height_ = 760;
    std::string last_session_;
    std::vector<std::string> open_tabs_;
    std::string active_tab_;
    std::string theme_ = "dark";
};
