#pragma once

#include <afterhours/src/singleton.h>

#include "shortcuts.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

// Minimal persisted settings: window geometry, theme mode, and the open-tab
// set (session ids + which one is active) so a launch restores exactly where
// the user left off. Stored as JSON next to the platform config dir.

namespace hanabi {

// The two send-key choices, as they are written into settings.json.
inline constexpr const char* kSendKeyReturn = "return";
inline constexpr const char* kSendKeyCmdReturn = "cmd-return";

inline std::string normalize_font_choice(std::string_view value) {
    if (value == "hyperlegible" || value == "system" || value == "optimistic")
        return std::string(value);
    return "default";
}

inline std::string normalize_font_weight(std::string_view value) {
    if (value == "medium" || value == "semibold" || value == "bold")
        return std::string(value);
    return "regular";
}

// What an Enter keypress MEANS, given the configured send key and whether Cmd
// was held at the moment of the press. This is the whole of the decision, in
// one pure function, deliberately away from the composer: the composer's
// submit listener is attached with addComponentIfMissing, so whatever it
// decides is frozen on the frame the field was born and every later Enter
// re-runs a stale answer (that bug shipped once, and the ENTER-TO-SEND comment
// in render_composer is its scar tissue). The listener reports the two facts;
// this decides; and because it is pure it can be asserted directly in
// tests/unit/test_settings.cpp, where a UI test cannot go — the harness cannot
// synthesise a Cmd chord at all (afterhours_gaps.md #49).
//
// Under "return" Cmd+Return sends as well: fingers trained on other apps reach
// for it, and refusing it would only look broken.
inline bool enter_sends(const std::string& sendKey, bool cmdHeld) {
    return sendKey == kSendKeyCmdReturn ? cmdHeld : true;
}

// ── The split divider's limits ──────────────────────────────────────────
// Here rather than beside the pane state, because BOTH sides need them and
// two copies of a limit drift: the drag clamps as it writes, and the loader
// clamps again as it reads a file that may have been hand-edited, truncated
// by a crash, or written by a future build with different limits. One
// definition, both call sites, and a pure function a test can drive.
inline constexpr float kSplitMinRatio = 0.2f;
inline constexpr float kSplitMaxRatio = 0.8f;
inline float clamp_split_ratio(float r) {
    // NaN fails both comparisons and falls through to the midpoint, which is
    // the answer that cannot produce a zero-width pane.
    if (!(r >= kSplitMinRatio))
        return r > kSplitMaxRatio ? kSplitMaxRatio : kSplitMinRatio;
    if (r > kSplitMaxRatio) return kSplitMaxRatio;
    return r;
}

}  // namespace hanabi

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
    // Which of the open tabs are pinned (a subset of get_open_tabs()).
    const std::vector<std::string>& get_pinned_tabs() const;
    void set_open_tabs(std::vector<std::string> ids, std::string activeId,
                       std::vector<std::string> pinnedIds = {});

    // ── Split view ──────────────────────────────────────────────────────
    // Whether the main pane is split, where the divider sits, and which
    // thread each pane was showing. Machine-local, exactly like the open-tab
    // set beside it and for the same reason: it says how THIS window was
    // arranged, and no server field carries it.
    //
    // The ratio is stored as the LEFT pane's share and clamped on read as
    // well as on write, so a hand-edited or truncated file cannot produce a
    // pane of zero width. A remembered pane thread that no longer exists is
    // no different from a remembered TAB that no longer exists -- the loader
    // fails its fetch and the pane shows the error, which is the behaviour
    // that already exists for tabs.
    //
    // Bounded by construction: two ids, a ratio, a focus index and a boolean,
    // whatever the user does. Unlike last_read (kMaxLastRead), there is nothing
    // here to prune.
    bool get_split_open() const;
    float get_split_ratio() const;
    int get_split_focused_pane() const;
    // Which thread each pane held, index 0 (left) and 1 (right). Empty means
    // that pane had nothing open.
    const std::string& get_split_pane(int index) const;
    void set_split(bool open, float ratio, const std::string& left,
                   const std::string& right,
                   int focusedPane = 0);  // auto-persists

    // Theme mode: "dark" (default) or "light".
    const std::string& get_theme() const;
    void set_theme(const std::string& mode);

    // UI font family and emphasis weight. Client-local only; persisted like
    // theme so both survive relaunch.
    const std::string& get_font_choice() const;
    void set_font_choice(const std::string& font);  // auto-persists
    const std::string& get_font_weight() const;
    void set_font_weight(const std::string& weight);

    // Custom colours: which NAMED swatch the accent family and the find
    // highlight use ("default" = the palette's own colour). A key, not a hex
    // value — each swatch carries a dark and a light colour, so one choice
    // stays readable on both palettes (src/ui/theme.h). Client-local like
    // theme and font; the web preferences schema has no colour field.
    const std::string& get_accent_choice() const;
    void set_accent_choice(const std::string& key);  // auto-persists
    const std::string& get_highlight_choice() const;
    void set_highlight_choice(const std::string& key);  // auto-persists
    // ── Export destination (Settings -> Data -> Export) ─────────────────
    // Where "Export all" writes the owned Markdown copies. Empty means the
    // built-in default (api::disk_cache::export_dir(), ~/hanabi/threads);
    // a chosen folder is remembered, because picking one every time is the
    // kind of friction that stops people keeping their own copies.
    // Auto-persists (mirrors set_theme). Client-local; never sent anywhere.
    const std::string& get_export_dir() const;
    void set_export_dir(const std::string& dir);  // auto-persists

    // Sidebar collapsed (thin icon rail) vs expanded (full 280px). Persisted so
    // a user who folds the sidebar and quits gets it folded on relaunch (the
    // toggle otherwise lived only in the in-memory LayoutComponent).
    // Auto-persists.
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
    // whatever the backend reported" — see
    // api::SessionSummary::archive_override for why this has to be able to say
    // false as well as true. Machine-local: the per-viewer route that would
    // sync it is not reachable from here.
    std::optional<bool> get_archived(const std::string& id) const;
    void set_archived(const std::string& id, bool archived);  // auto-persists
    // Muted session ids. Machine-local by design (see SessionSummary::muted),
    // so this is the only place the state lives — there is no server copy to
    // reconcile against, and an id that no longer exists simply never matches.
    bool is_muted(const std::string& id) const;
    void set_muted(const std::string& id, bool muted);  // auto-persists

    // Manual sidebar row order, per folder key: the ids the user has arranged,
    // in the order they left them (see ecs::model::apply_row_order for what a
    // manual order means). Machine-local like mute — it says how THIS list
    // should read here, and no server field carries it. The caller bounds each
    // list at ecs::model::kRowOrderMax, so this never grows with the session
    // count. Auto-persists.
    const std::vector<std::string>& get_row_order(
        const std::string& folder) const;
    const std::map<std::string, std::vector<std::string>>& get_all_row_order()
        const;
    void set_row_order(const std::string& folder,
                       std::vector<std::string> ids);  // auto-persists
    // How much of a tool call a thread shows by default (hanabi::fold::Mode as
    // an int — see src/ui/fold_menu.h). Per session, because "show me every
    // tool result" is a property of the thread you are reading, not of the
    // app: a debugging thread wants them open and a long chat does not. An
    // absent id means the default (folded). Auto-persists.
    int get_tool_fold(const std::string& id) const;
    void set_tool_fold(const std::string& id, int mode);  // auto-persists

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

    // The read-stamp map is the ONE per-thread record this app writes without
    // being asked. Star, mute, archive and tool-fold are all a deliberate act
    // on one thread, so they grow with intent and are correctly unbounded.
    // Reaching the bottom of a thread is not an act -- it is what reading is
    // -- and it wrote an entry that nothing ever removed, in a file that is
    // fully re-serialised and rewritten on every single advance.
    //
    // MEASURED, with the app driven through 1500 frames opening threads
    // (HANABI_SOAK=1500 HANABI_STRESS=threads):
    //
    //   seeded entries   settings.json   RSS       frame time
    //             none         113 B     47.5 MB   4.17 ms
    //            2,000        72 KB      48.3 MB   4.24 ms
    //           10,000       360 KB      52.3 MB   4.64 ms
    //
    // Five megabytes of resident memory and half a millisecond a frame, from a
    // file, and both keep growing for as long as the app is used. So: keep the
    // most recently READ threads and drop the rest.
    //
    // What dropping one costs: the transcript only computes an unread divider
    // when the stamp is > 0, so a thread whose stamp was dropped opens with no
    // "new since you were last here" line. It does not open marked unread and
    // it does not lose anything else. Two thousand threads is far past any
    // catalog a person navigates by hand, and the entries dropped are the ones
    // read longest ago.
    static constexpr std::size_t kMaxLastRead = 2000;

    // Entries currently held (tests / instrumentation).
    std::size_t last_read_count() const;

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
    // List sub-agents that have FINISHED in the transcript's sub-agent rollup.
    // Global (not per session or per thread) and off by default: a finished
    // sub-agent has nothing left to say, and on a thread that spawned dozens it
    // is the finished ones that bury the two still working. The rollup's
    // headline still counts every sub-agent whatever this says, so turning it
    // off hides chips, never the fact that the work happened. Auto-persists.
    bool get_show_finished_subagents() const;
    void set_show_finished_subagents(bool on);  // auto-persists
    bool get_subagent_sidebar_open() const;
    void set_subagent_sidebar_open(bool on);
    // The day row above the first message of each new local calendar day.
    // Default true. A reader who works one thread all day sees a divider that
    // never tells them anything, so it is theirs to turn off.
    bool get_show_date_dividers() const;
    void set_show_date_dividers(bool on);  // auto-persists

    // Render the model's reasoning blocks (the folded "Thought for a moment"
    // rows) at all. Default true. Off drops them from the transcript
    // entirely — reasoning is not the answer, and some readers want only the
    // answer.
    bool get_show_reasoning() const;
    void set_show_reasoning(bool on);  // auto-persists

    // Fold a very long message behind a "Show more" button. Default true.
    // Off renders every message at full length: slower on a huge paste, but
    // it is the reader's call whether the transcript may hide text from them.
    bool get_fold_long_messages() const;
    void set_fold_long_messages(bool on);  // auto-persists

    // Memory backend: "traditional" (default) or "hindsight".
    const std::string& get_memory_backend() const;
    void set_memory_backend(
        const std::string& backend);  // auto-persists + dirty

    // Default model id, e.g. "default" (server default) / a named model.
    const std::string& get_default_model() const;
    void set_default_model(const std::string& model);  // auto-persists + dirty

    // Effort level for new work: one of the server's own tokens (low, medium,
    // high, xhigh, max — src/ui/effort_menu.h). Local-only: it is NOT in the
    // backend preference payload, because hanabi has no confirmed field for
    // it. Persists across restarts like every other preference here.
    const std::string& get_default_effort() const;
    void set_default_effort(const std::string& effort);  // auto-persists

    // Which keystroke in the composer sends: hanabi::kSendKeyReturn (plain
    // Return, the default every chat app trains people to expect) or
    // hanabi::kSendKeyCmdReturn (Cmd+Return, for people who want Return free).
    // Local-only, like the effort token: the backend preference payload has no
    // field for it. See hanabi::enter_sends for what the choice MEANS.
    const std::string& get_send_key() const;
    void set_send_key(const std::string& key);  // auto-persists

    hanabi::shortcuts::Shortcut get_shortcut(
        hanabi::shortcuts::Command command) const;
    hanabi::shortcuts::Bindings get_shortcuts() const;
    hanabi::shortcuts::Validation set_shortcut(
        hanabi::shortcuts::Command command,
        hanabi::shortcuts::Shortcut shortcut);
    void reset_shortcuts();
    std::uint64_t shortcut_revision() const;
    std::uint64_t font_revision() const;

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
    std::vector<std::string> pinned_tabs_;
    std::string active_tab_;
    std::string theme_ = "dark";
    bool split_open_ = false;
    float split_ratio_ = 0.5f;
    int split_focused_pane_ = 0;
    std::string split_panes_[2];
    std::string font_choice_ = "default";
    std::string font_weight_ = "regular";
    std::string accent_choice_ = "default";
    std::string highlight_choice_ = "default";
    std::string export_dir_;  // empty = the built-in default
    bool sidebar_collapsed_ = false;
    // 0 == unlimited; default 1 GiB. See get/set_cache_cap_bytes.
    std::uint64_t cache_cap_bytes_ = 1024ull * 1024 * 1024;
    std::vector<std::string> starred_ids_;
    std::map<std::string, bool> archived_;
    std::vector<std::string> muted_ids_;
    // Membership indexes over the two vectors above. The vectors stay the
    // source of truth because they carry ORDER and are what round-trips
    // through JSON; these only answer is_starred / is_muted, which
    // apply_local_overlays asks once per session on every list fetch --
    // O(sessions x starred) with a linear scan. Rebuilt wholesale in
    // load_save_file and maintained by set_starred / set_muted; nothing else
    // may touch the vectors without touching these.
    std::unordered_set<std::string> starred_set_;
    std::unordered_set<std::string> muted_set_;
    std::map<std::string, std::vector<std::string>> row_order_;
    std::vector<std::string> collapsed_shelves_;
    std::map<std::string, int64_t> last_read_;
    // Drop the oldest stamps down to kMaxLastRead. Called after an insert and
    // after a load, so a file written by an older build shrinks on first run.
    void prune_last_read();
    std::map<std::string, int> tool_fold_;
    // Preference slots (see getters). Defaults mirror the web defaults.
    int yap_level_ = 2;
    int auto_archive_days_ = 5;
    bool notification_sound_ = true;
    bool show_timestamps_ = true;
    int theme_rotate_secs_ = 0;
    bool show_finished_subagents_ = false;
    bool subagent_sidebar_open_ = false;
    bool show_date_dividers_ = true;
    bool show_reasoning_ = true;
    bool fold_long_messages_ = true;
    int quiet_start_minutes_ = 0;
    int quiet_end_minutes_ = 0;
    std::string memory_backend_ = "traditional";
    std::string default_model_ = "default";
    std::string default_effort_ = "high";
    std::string send_key_ = "return";
    std::array<std::optional<hanabi::shortcuts::Shortcut>,
               hanabi::shortcuts::kDefinitions.size()>
        custom_shortcuts_{};
    std::uint64_t shortcut_revision_ = 0;
    std::uint64_t font_revision_ = 0;
    // In-memory only: set on any preference change, cleared by the loader
    // after a successful (or best-effort) backend push.
    bool settings_dirty_ = false;
};
