// Unit tests for the SETTINGS rework (wt/settings):
//   (1) each WIRED preference control changes the persisted Settings value
//       (call the set_ method, read it back, assert) — proves the control
//       actually does something.
//   (2) any preference change flips the sync-dirty flag.
//   (3) the mock's settings-WRITE path accepts + stores the pushed settings
//       and reports success (proves update_settings is invoked end-to-end).
//   (4) the http config write-gate is opt-in and OFF by default (the mock is
//       the zero-config default; a real endpoint only comes from local config).
//
// Pure logic only — no network, no graphics. Settings persists to a JSON file;
// we isolate it to a temp config dir so we never touch a real user settings
// file, and disable auto-save churn where it isn't under test.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

#include "../../src/settings.h"
#include "../../src/ui/effort_menu.h"
#include "../../src/util/quiet_hours.h"
#include "../../src/api/mock_client.h"  // pulls in client.h (Config + Client)

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

// Isolate Settings' on-disk file to a per-pid temp dir so the real user
// settings are never touched.
static void isolate_settings() {
    std::string dir = "/tmp/hanabi_test_cfg_" + std::to_string(::getpid());
    // afterhours get_config_path honors XDG_CONFIG_HOME on non-mac; on mac it
    // uses Application Support. Either way we point HOME/XDG at the temp dir so
    // create_directories lands somewhere disposable. Best-effort — the value
    // round-trips in-memory regardless of where the file lands.
    setenv("XDG_CONFIG_HOME", dir.c_str(), 1);
    setenv("HOME", dir.c_str(), 1);
}

// --- (1)+(2) each wired control changes the persisted value + marks dirty ---
static void test_wired_controls_change_value() {
    std::printf("test_wired_controls_change_value\n");
    isolate_settings();
    auto& s = Settings::get();

    // Yap level: default 2; set 0 -> reads back 0, dirty set.
    s.clear_settings_dirty();
    s.set_yap_level(0);
    CHECK(s.get_yap_level() == 0);
    CHECK(s.is_settings_dirty());

    // Auto-archive days.
    s.clear_settings_dirty();
    s.set_auto_archive_days(30);
    CHECK(s.get_auto_archive_days() == 30);
    CHECK(s.is_settings_dirty());

    // Notification sound (default true -> false).
    s.clear_settings_dirty();
    s.set_notification_sound(false);
    CHECK(s.get_notification_sound() == false);
    CHECK(s.is_settings_dirty());

    // Memory backend.
    s.clear_settings_dirty();
    s.set_memory_backend("hindsight");
    CHECK(s.get_memory_backend() == "hindsight");
    CHECK(s.is_settings_dirty());

    // Default model.
    s.clear_settings_dirty();
    s.set_default_model("reasoning");
    CHECK(s.get_default_model() == "reasoning");
    CHECK(s.is_settings_dirty());

    // Re-setting the SAME value is a no-op: no dirty flip, no churn.
    s.clear_settings_dirty();
    s.set_yap_level(0);  // already 0
    CHECK(!s.is_settings_dirty());
}

// --- (3) the mock accepts + stores a pushed settings snapshot ---------------
static void test_mock_settings_write() {
    std::printf("test_mock_settings_write\n");
    api::MockClient m;
    CHECK(m.supports_settings_write());
    CHECK(m.settings_write_count() == 0);

    api::UserSettings snap;
    snap.ok = true;
    snap.user_id = "someone@example.invalid";
    snap.raw_json =
        R"({"yapLevel":1,"autoArchiveDays":14,"notificationSound":false})";
    const bool ok = m.update_settings(snap);
    CHECK(ok);
    CHECK(m.settings_write_count() == 1);
    CHECK(m.last_written_settings().user_id == "someone@example.invalid");
    CHECK(m.last_written_settings().raw_json == snap.raw_json);

    // A second push increments the count (the loader pushes on each debounced
    // change).
    m.update_settings(snap);
    CHECK(m.settings_write_count() == 2);
}

// --- (4) http write-gate is opt-in + OFF by default -------------------------
static void test_settings_write_config_gate() {
    std::printf("test_settings_write_config_gate\n");
    api::Config c;                       // no base_url, no update path
    CHECK(!c.settings_write_ready());    // nothing configured => off
    c.base_url = "https://example.invalid/api";
    // Base URL alone is NOT enough: settings_update_path is EMPTY by default so
    // the write stays off (the mock is the zero-config default; a real endpoint
    // only comes from local config — never committed).
    CHECK(c.settings_update_path.empty());
    CHECK(!c.settings_write_ready());
    // Setting the path (as a user's local config would) activates it.
    c.settings_update_path = "/some/local/only/path";
    CHECK(c.settings_write_ready());
}

// --- Home shelf folds survive a reload ------------------------------------
static void test_shelf_fold_round_trips() {
    std::printf("test_shelf_fold_round_trips\n");
    isolate_settings();
    auto& s = Settings::get();

    CHECK(!s.is_shelf_collapsed("waiting"));
    s.set_shelf_collapsed("waiting", true);
    CHECK(s.is_shelf_collapsed("waiting"));

    // Reading the file back is the whole point: a fold that lives only in
    // memory is gone on the next launch.
    s.load_save_file();
    CHECK(s.is_shelf_collapsed("waiting"));
    CHECK(!s.is_shelf_collapsed("recent"));

    s.set_shelf_collapsed("waiting", false);
    s.load_save_file();
    CHECK(!s.is_shelf_collapsed("waiting"));
}

// --- Effort level: the ladder, and the settings round-trip ----------------
static void test_effort_round_trips() {
    std::printf("test_effort_round_trips\n");
    isolate_settings();
    auto& s = Settings::get();
    namespace ef = hanabi::effort;

    // A fresh install sits on the ladder's own default rather than an
    // invented one.
    CHECK(s.get_default_effort() == ef::default_id());
    CHECK(ef::display_name("high") == "High");

    s.set_default_effort("xhigh");
    CHECK(s.get_default_effort() == "xhigh");
    // Reading the file back is the whole point: a level that lives only in
    // memory is gone on the next launch.
    s.load_save_file();
    CHECK(s.get_default_effort() == "xhigh");

    // It stays OUT of the backend preference push — there is no confirmed
    // field for it, and a guessed key would ride every other preference.
    s.clear_settings_dirty();
    s.set_default_effort("low");
    CHECK(!s.is_settings_dirty());

    // The tokens are the server's own; anything else is shown as itself
    // rather than quietly redrawn as a level it is not.
    CHECK(ef::index_of("max") < ef::all().size());
    CHECK(ef::index_of("maximum") == ef::all().size());
    CHECK(ef::display_name("maximum") == "maximum");
}

// --- Quiet hours: the window, and the settings round-trip -----------------
static void test_quiet_hours_window() {
    std::printf("test_quiet_hours_window\n");
    using hanabi::quiet::in_window;

    // A daytime window, both ends on the same day.
    CHECK(in_window(13 * 60, 9 * 60, 17 * 60));
    CHECK(!in_window(8 * 60, 9 * 60, 17 * 60));
    // Half-open: quiet AT the start, noisy again AT the end.
    CHECK(in_window(9 * 60, 9 * 60, 17 * 60));
    CHECK(!in_window(17 * 60, 9 * 60, 17 * 60));

    // The one that matters: 10pm-8am crosses midnight.
    CHECK(in_window(22 * 60, 22 * 60, 8 * 60));
    CHECK(in_window(23 * 60 + 59, 22 * 60, 8 * 60));
    CHECK(in_window(0, 22 * 60, 8 * 60));
    CHECK(in_window(3 * 60, 22 * 60, 8 * 60));
    CHECK(!in_window(8 * 60, 22 * 60, 8 * 60));
    CHECK(!in_window(12 * 60, 22 * 60, 8 * 60));

    // Equal ends mean no window at all, never an all-day silence.
    CHECK(!in_window(8 * 60, 8 * 60, 8 * 60));
    CHECK(!in_window(0, 0, 0));
}

static void test_quiet_hours_persist() {
    std::printf("test_quiet_hours_persist\n");
    isolate_settings();
    auto& s = Settings::get();

    CHECK(s.get_quiet_start_minutes() == s.get_quiet_end_minutes());  // off
    s.set_quiet_window(22 * 60, 8 * 60);
    s.load_save_file();
    CHECK(s.get_quiet_start_minutes() == 22 * 60);
    CHECK(s.get_quiet_end_minutes() == 8 * 60);
}

// --- The archive overlay round-trips, and can say false as well as true ---
static void test_archive_overlay_round_trips() {
    std::printf("test_archive_overlay_round_trips\n");
    isolate_settings();
    auto& s = Settings::get();

    // Nothing said about a thread yet: the caller is meant to fall back to
    // whatever the backend reported, so the answer is "no opinion", not false.
    CHECK(!s.get_archived("r4").has_value());

    s.set_archived("r4", true);
    CHECK(s.get_archived("r4").value_or(false));

    // Reading the file back is the whole point: an archive that lives only in
    // memory is gone on the next launch.
    s.load_save_file();
    CHECK(s.get_archived("r4").value_or(false));
    CHECK(!s.get_archived("r5").has_value());

    // A stored false is a real answer and must survive as one — it is what
    // unarchiving a thread the backend itself calls archived comes down to.
    s.set_archived("r4", false);
    s.load_save_file();
    CHECK(s.get_archived("r4").has_value());
    CHECK(!s.get_archived("r4").value_or(true));
}

// --- A mute survives a reload, and unmuting removes it -------------------
static void test_mute_round_trips() {
    std::printf("test_mute_round_trips\n");
    isolate_settings();
    auto& s = Settings::get();

    CHECK(!s.is_muted("r4"));
    s.set_muted("r4", true);
    CHECK(s.is_muted("r4"));

    // Reading the file back is the whole point: a mute that lives only in
    // memory is gone on the next launch, and the thread starts shouting again.
    s.load_save_file();
    CHECK(s.is_muted("r4"));
    CHECK(!s.is_muted("r5"));

    s.set_muted("r4", false);
    s.load_save_file();
    CHECK(!s.is_muted("r4"));
}

static void test_finished_subagents_round_trips() {
    std::printf("test_finished_subagents_round_trips\n");
    isolate_settings();
    auto& s = Settings::get();

    // Off by default: a thread that spawned a dozen helpers should open showing
    // the ones still working, not the ten that are done.
    CHECK(!s.get_show_finished_subagents());

    s.set_show_finished_subagents(true);
    CHECK(s.get_show_finished_subagents());
    // It is a standing preference, so it has to survive the launch that set it.
    s.load_save_file();
    CHECK(s.get_show_finished_subagents());

    s.set_show_finished_subagents(false);
    s.load_save_file();
    CHECK(!s.get_show_finished_subagents());
}

int main() {
    std::printf("=== test_settings ===\n");
    test_wired_controls_change_value();
    test_mock_settings_write();
    test_settings_write_config_gate();
    test_shelf_fold_round_trips();
    test_effort_round_trips();
    test_quiet_hours_window();
    test_quiet_hours_persist();
    test_archive_overlay_round_trips();
    test_mute_round_trips();
    test_finished_subagents_round_trips();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
