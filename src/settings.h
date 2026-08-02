#pragma once

#include <afterhours/src/singleton.h>

#include <cstdint>
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

    // Disk-cache size cap in BYTES. 0 == Unlimited (no eviction). Default 1 GB.
    // The settings modal offers 100 MB / 1 GB / 10 GB / Unlimited; the disk
    // cache trims the oldest data to stay under this after a transcript save
    // (see api::disk_cache::trim_to_cap). Persisted so the choice survives
    // relaunch. Auto-persists (mirrors set_theme).
    std::uint64_t get_cache_cap_bytes() const;
    void set_cache_cap_bytes(std::uint64_t bytes);  // auto-persists

    // Starred session ids (Phase I star toggle). Persisted so a user's stars
    // survive relaunch — otherwise a star flipped in the sidebar is lost on the
    // next launch (it lived only in the in-memory SessionSummary). The set is
    // the source of truth; the loader applies it to freshly-fetched sessions.
    const std::vector<std::string>& get_starred() const;
    bool is_starred(const std::string& id) const;
    void set_starred(const std::string& id, bool starred);  // auto-persists

    std::string get_settings_path() const;

    bool auto_save_enabled = true;

  private:
    int window_width_ = 1100;
    int window_height_ = 760;
    std::string last_session_;
    std::vector<std::string> open_tabs_;
    std::string active_tab_;
    std::string theme_ = "dark";
    // 0 == unlimited; default 1 GiB. See get/set_cache_cap_bytes.
    std::uint64_t cache_cap_bytes_ = 1024ull * 1024 * 1024;
    std::vector<std::string> starred_ids_;
};
