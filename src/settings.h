#pragma once

#include <afterhours/src/singleton.h>

#include <cstdint>
#include <map>
#include <optional>
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

    // UI font choice: "default" (Roboto, default) or "hyperlegible" (Atkinson
    // Hyperlegible). Client-local only — NOT an API preference; the web app has
    // no font field in PUT /api/user/preferences (theme + font are both purely
    // client-side). Persisted like theme so a choice survives relaunch.
    // Auto-persists (mirrors set_theme).
    const std::string& get_font_choice() const;
    void set_font_choice(const std::string& font);  // auto-persists

    // Sidebar collapsed (thin icon rail) vs expanded (full 280px). Persisted so
    // a user who folds the sidebar and quits gets it folded on relaunch (the
    // toggle otherwise lived only in the in-memory LayoutComponent). Auto-persists.
    bool get_sidebar_collapsed() const;
    void set_sidebar_collapsed(bool collapsed);  // auto-persists

    // Which Home shelves ("Waiting on you", "Recent", ...) are folded shut.
    // Keyed by a stable shelf key, not by the visible label, so renaming a
    // heading cannot silently unfold everyone's Home. Auto-persists.
    const std::vector<std::string>& get_collapsed_shelves() const;
    bool is_shelf_collapsed(const std::string& key) const;
    void set_shelf_collapsed(const std::string& key, bool collapsed);

    // Quiet hours: minutes since local midnight, half-open [start, end).
    // Equal ends mean "no quiet window" (see util/quiet_hours.h). Persisted as
    // minutes rather than a preset index so a real time picker can replace the
    // presets without migrating anyone's settings. Auto-persists.
    int get_quiet_start_minutes() const;
    int get_quiet_end_minutes() const;
    void set_quiet_window(int startMinutes, int endMinutes);

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

    // Per-session archive overlay. An id is present only once the user has
    // said something about that thread here, so an absent id means "defer to
    // whatever the backend reported" — see api::SessionSummary::archive_override
    // for why this has to be able to say false as well as true. Machine-local:
    // the per-viewer route that would sync it is not reachable from here.
    std::optional<bool> get_archived(const std::string& id) const;
    void set_archived(const std::string& id, bool archived);  // auto-persists
    // Muted session ids. Machine-local by design (see SessionSummary::muted),
    // so this is the only place the state lives — there is no server copy to
    // reconcile against, and an id that no longer exists simply never matches.
    bool is_muted(const std::string& id) const;
    void set_muted(const std::string& id, bool muted);  // auto-persists

    // How far a thread had been READ, as the timestamp of its newest message
    // at the moment it was last open. Persisted so reopening a conversation
    // can say what arrived while you were away — without it, a thread that
    // gained twelve messages overnight looks exactly like one that gained
    // none. 0 means never opened (so nothing is marked new).
    // A plain store: it records whatever it is given. "Never go backwards" is
    // the transcript's rule, not this one's — it only advances the stamp when
    // the reader has actually reached the end — and keeping the policy at the
    // call site is what lets a test place the boundary anywhere it likes.
    int64_t get_last_read(const std::string& id) const;
    void set_last_read(const std::string& id, int64_t stamp);  // auto-persists

    // ── Preference slots (settings modal). Each auto-persists (mirrors
    // set_theme) AND marks the sync-dirty flag so the loader can push the
    // change to the backend when a write path is configured. These map onto
    // the web PUT-preferences schema (yapLevel / autoArchiveDays /
    // notificationSound / memoryBackend / defaultModelId) but persist locally
    // FIRST — the app is fully usable offline; the server just gets a copy.

    // Yap / verbosity level: 0 = No yapping, 1 = A little, 2 = Full. Default 2.
    int get_yap_level() const;
    void set_yap_level(int level);  // auto-persists + marks dirty

    // Auto-archive threads after N days. 0 = never. Default 5.
    int get_auto_archive_days() const;
    void set_auto_archive_days(int days);  // auto-persists + marks dirty

    // Notification sound on/off. Default true (Ping).
    bool get_notification_sound() const;
    void set_notification_sound(bool on);  // auto-persists + marks dirty

    // ── Local display preferences. Persisted, but NOT part of the web
    // preferences schema and never marked dirty: how this machine draws the
    // transcript is not something another device wants pushed onto it.

    // Show the time on transcript rows. Default true (the long-standing
    // behaviour).
    bool get_show_timestamps() const;
    void set_show_timestamps(bool on);  // auto-persists

    // Rotate the theme automatically every N seconds; 0 (the default) is off.
    // ONE number rather than an enabled flag plus an interval, so "off" can
    // never disagree with "every 15 minutes". Seconds, though the sheet only
    // offers minute presets: the presets can change without migrating anyone's
    // settings, and a test can rotate in a second.
    int get_theme_rotate_secs() const;
    void set_theme_rotate_secs(int secs);  // auto-persists

    // Memory backend: "traditional" (default) or "hindsight".
    const std::string& get_memory_backend() const;
    void set_memory_backend(const std::string& backend);  // auto-persists + dirty

    // Default model id, e.g. "default" (server default) / a named model.
    const std::string& get_default_model() const;
    void set_default_model(const std::string& model);  // auto-persists + dirty

    // Effort level for new work: one of the server's own tokens (low, medium,
    // high, xhigh, max — src/ui/effort_menu.h). Local-only: it is NOT in the
    // backend preference payload, because hanabi has no confirmed field for
    // it. Persists across restarts like every other preference here.
    const std::string& get_default_effort() const;
    void set_default_effort(const std::string& effort);  // auto-persists

    // ── Backend-sync bookkeeping. Any preference change flips settings_dirty_;
    // the loader debounces + pushes to the backend (best-effort) via
    // ApiClient::update_settings, then clears the flag. Purely in-memory (NOT
    // persisted): a relaunch starts clean and a real change re-flips it.
    bool is_settings_dirty() const;
    void mark_settings_dirty();
    void clear_settings_dirty();

    std::string get_settings_path() const;

    bool auto_save_enabled = true;

  private:
    int window_width_ = 1100;
    int window_height_ = 760;
    std::string last_session_;
    std::vector<std::string> open_tabs_;
    std::string active_tab_;
    std::string theme_ = "dark";
    std::string font_choice_ = "default";
    bool sidebar_collapsed_ = false;
    // 0 == unlimited; default 1 GiB. See get/set_cache_cap_bytes.
    std::uint64_t cache_cap_bytes_ = 1024ull * 1024 * 1024;
    std::vector<std::string> starred_ids_;
    std::map<std::string, bool> archived_;
    std::vector<std::string> muted_ids_;
    std::vector<std::string> collapsed_shelves_;
    std::map<std::string, int64_t> last_read_;
    // Preference slots (see getters). Defaults mirror the web defaults.
    int yap_level_ = 2;
    int auto_archive_days_ = 5;
    bool notification_sound_ = true;
    bool show_timestamps_ = true;
    int theme_rotate_secs_ = 0;
    int quiet_start_minutes_ = 0;
    int quiet_end_minutes_ = 0;
    std::string memory_backend_ = "traditional";
    std::string default_model_ = "default";
    std::string default_effort_ = "high";
    // In-memory only: set on any preference change, cleared by the loader
    // after a successful (or best-effort) backend push.
    bool settings_dirty_ = false;
};
