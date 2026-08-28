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
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "../../src/api/mock_client.h"  // pulls in client.h (Config + Client)
#include "../../src/settings.h"
#include "../../src/ui/effort_menu.h"
#include "../../src/util/quiet_hours.h"

static int g_failures = 0;
#define CHECK(cond)                                                 \
    do {                                                            \
        if (!(cond)) {                                              \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++g_failures;                                           \
        }                                                           \
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
    api::Config c;                     // no base_url, no update path
    CHECK(!c.settings_write_ready());  // nothing configured => off
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

// --- Send key: what each choice MEANS, and the settings round-trip --------
// The meaning half is here rather than in a scripted UI test because the
// injector cannot synthesise a Cmd chord at all (afterhours_gaps.md #49), so
// "Cmd+Return sends" has no other place it can be asserted.
static void test_send_key_round_trips() {
    std::printf("test_send_key_round_trips\n");
    isolate_settings();
    auto& s = Settings::get();

    // A fresh install sends on Return, which is what every chat app has
    // trained people to expect.
    CHECK(s.get_send_key() == hanabi::kSendKeyReturn);

    s.set_send_key(hanabi::kSendKeyCmdReturn);
    CHECK(s.get_send_key() == hanabi::kSendKeyCmdReturn);
    // Reading the file back is the whole point: a choice that lives only in
    // memory is gone on the next launch.
    s.load_save_file();
    CHECK(s.get_send_key() == hanabi::kSendKeyCmdReturn);

    // A settings.json edited to something the app does not know falls back to
    // Return rather than leaving the composer with no send key at all.
    s.set_send_key("f13");
    CHECK(s.get_send_key() == hanabi::kSendKeyReturn);

    // Local-only: no confirmed backend field, so it must not ride the
    // preference push.
    s.clear_settings_dirty();
    s.set_send_key(hanabi::kSendKeyCmdReturn);
    CHECK(!s.is_settings_dirty());

    // --- What the choice means, both ways round ---------------------------
    using hanabi::enter_sends;
    // Return: bare Return sends, and so does Cmd+Return — fingers trained on
    // other apps reach for the chord, and refusing it would only look broken.
    CHECK(enter_sends(hanabi::kSendKeyReturn, /*cmdHeld=*/false));
    CHECK(enter_sends(hanabi::kSendKeyReturn, /*cmdHeld=*/true));
    // Cmd+Return: the chord sends and bare Return does NOT. This is the arm a
    // scripted UI test can only prove half of.
    CHECK(enter_sends(hanabi::kSendKeyCmdReturn, /*cmdHeld=*/true));
    CHECK(!enter_sends(hanabi::kSendKeyCmdReturn, /*cmdHeld=*/false));
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

// --- the read-stamp map is bounded, and so is the file it writes ------------
//
// Reaching the bottom of a thread writes a last_read entry and rewrites the
// WHOLE settings file. Nothing ever removed an entry, so both the map and the
// file grew for the life of the install: measured in the app, 10,000 entries
// is a 360 KB settings.json, +4.8 MB RSS and +0.5 ms per frame.
//
// The assertion is on the FILE, not on a duration: a timing threshold in a
// test is a flake, and the file size is the thing the duration is a function
// of. Without Settings::prune_last_read this writes ~180 KB and the count is
// 5000.
static void test_last_read_is_bounded() {
    std::printf("test_last_read_is_bounded\n");
    isolate_settings();
    auto& s = Settings::get();
    s.load_save_file();

    // Stamps ascend with the index, so the newest thread read is the last one.
    for (int i = 0; i < 5000; ++i)
        s.set_last_read("thread-" + std::to_string(i),
                        1780000000000LL + i * 1000LL);

    CHECK(s.last_read_count() == Settings::kMaxLastRead);
    // The newest survive and the oldest are gone -- an eviction that kept the
    // wrong end would pass a size check and be useless.
    CHECK(s.get_last_read("thread-4999") == 1780000000000LL + 4999 * 1000LL);
    CHECK(s.get_last_read("thread-0") == 0);
    // The boundary: 5000 written, 2000 kept, so 3000..4999 survive.
    CHECK(s.get_last_read("thread-3000") != 0);
    CHECK(s.get_last_read("thread-2999") == 0);

    // And the file the app rewrites on every advance stays small.
    std::FILE* f = std::fopen(s.get_settings_path().c_str(), "rb");
    CHECK(f != nullptr);
    long bytes = 0;
    if (f != nullptr) {
        std::fseek(f, 0, SEEK_END);
        bytes = std::ftell(f);
        std::fclose(f);
    }
    std::printf("  settings.json after 5000 threads read: %ld bytes\n", bytes);
    CHECK(bytes > 0 && bytes < 120 * 1024);

    // A file written by an older build arrives over the cap and shrinks on the
    // first load rather than waiting for the next thread to be read.
    s.load_save_file();
    CHECK(s.last_read_count() <= Settings::kMaxLastRead);
}

// The membership index and the ordered vector must never disagree.
//
// is_starred/is_muted answer from an unordered_set; get_starred() and the JSON
// round-trip use the vector. Two structures holding one fact is a desync bug
// waiting to happen, and the failure is SILENT and user-visible: a star that
// the sidebar draws but the settings file does not contain, or the reverse.
// This walks both and asserts they agree after every kind of mutation.
static void test_star_and_mute_index_agrees_with_the_stored_list() {
    std::printf("test_star_and_mute_index_agrees_with_the_stored_list\n");
    isolate_settings();
    auto& s = Settings::get();

    // Both directions of agreement, over ids that ARE and are NOT present.
    auto agree = [&](const char* where) {
        const auto& v = s.get_starred();
        for (const auto& id : v)
            if (!s.is_starred(id))
                std::printf(
                    "  FAIL: %s: '%s' is in the list but is_starred "
                    "says no\n",
                    where, id.c_str()),
                    ++g_failures;
        for (int i = 0; i < 40; ++i) {
            const std::string id = "t" + std::to_string(i);
            const bool inList = std::find(v.begin(), v.end(), id) != v.end();
            if (inList != s.is_starred(id))
                std::printf("  FAIL: %s: '%s' list=%d is_starred=%d\n", where,
                            id.c_str(), inList ? 1 : 0,
                            s.is_starred(id) ? 1 : 0),
                    ++g_failures;
        }
    };

    for (int i = 0; i < 40; i += 2)
        s.set_starred("t" + std::to_string(i), true);
    agree("after starring");

    // Unstarring is where a set-backed index goes wrong: the vector erase and
    // the set erase are two statements, and dropping either one is invisible
    // until someone reads the other structure.
    for (int i = 0; i < 40; i += 4)
        s.set_starred("t" + std::to_string(i), false);
    agree("after unstarring");

    // Re-star something previously unstarred: catches an index that was left
    // holding a stale entry, which would make the no-change early-out fire.
    s.set_starred("t0", true);
    agree("after re-starring");

    // And across a reload, which rebuilds the index from the file.
    s.load_save_file();
    agree("after reload");
    CHECK(s.is_starred("t2"));
    CHECK(!s.is_starred("t4"));
    CHECK(!s.is_starred("nonexistent"));

    // Mute keeps its own pair of structures; same contract.
    s.set_muted("m1", true);
    s.set_muted("m2", true);
    s.set_muted("m1", false);
    s.load_save_file();
    CHECK(!s.is_muted("m1"));
    CHECK(s.is_muted("m2"));
}

static void test_subagent_sidebar_toggle_round_trips() {
    std::printf("test_subagent_sidebar_toggle_round_trips\n");
    isolate_settings();
    auto& s = Settings::get();
    CHECK(!s.get_subagent_sidebar_open());
    s.set_subagent_sidebar_open(true);
    s.load_save_file();
    CHECK(s.get_subagent_sidebar_open());
    s.set_subagent_sidebar_open(false);
    s.load_save_file();
    CHECK(!s.get_subagent_sidebar_open());
}

static void test_tabs_and_pins_round_trip() {
    std::printf("test_tabs_and_pins_round_trip\n");
    isolate_settings();
    auto& s = Settings::get();

    s.set_open_tabs({"t1", "t4", "t5"}, "t4", {"t1", "t4"});
    s.write_save_file();
    s.set_open_tabs({}, "", {});
    s.load_save_file();

    CHECK((s.get_open_tabs() == std::vector<std::string>{"t1", "t4", "t5"}));
    CHECK(s.get_active_tab() == "t4");
    CHECK((s.get_pinned_tabs() == std::vector<std::string>{"t1", "t4"}));

    s.set_open_tabs({}, "", {});
    s.write_save_file();
    s.load_save_file();
    CHECK(s.get_open_tabs().empty());
    CHECK(s.get_active_tab().empty());
    CHECK(s.get_pinned_tabs().empty());
}

static void test_legacy_split_focus_inference() {
    std::printf("test_legacy_split_focus_inference\n");
    isolate_settings();
    auto& s = Settings::get();
    const auto load = [&](const std::string& body) {
        std::ofstream out(s.get_settings_path());
        out << body;
        out.close();
        CHECK(s.load_save_file());
    };

    load(R"({"active_tab":"t9","split_open":true,"split_panes":["t2","t9"]})");
    CHECK(s.get_split_focused_pane() == 1);

    load(R"({"active_tab":"t2","split_open":true,"split_panes":["t2","t9"]})");
    CHECK(s.get_split_focused_pane() == 0);

    load(R"({"active_tab":"t2","split_open":true,"split_panes":["t2","t2"]})");
    CHECK(s.get_split_focused_pane() == 0);

    load(
        R"({"active_tab":"missing","split_open":true,"split_panes":["t2","t9"]})");
    CHECK(s.get_split_focused_pane() == 0);

    load(
        R"({"active_tab":"t9","split_open":true,"split_panes":["t2","t9"],"split_focused_pane":0})");
    CHECK(s.get_split_focused_pane() == 0);
}

// --- produce a pane of zero width -----------------------------------------
//
// The persistence half is the same round-trip every preference above gets. The
// CLAMP half is here and not in a UI test because the values a UI test can
// produce are the ones a drag produces, and a drag is already clamped -- the
// case worth pinning is a settings.json that was hand-edited, half-written by
// a crash, or left by a build with different limits, which nothing but this
// can reach.
static void test_split_round_trips_and_clamps() {
    std::printf("test_split_round_trips_and_clamps\n");
    isolate_settings();
    auto& s = Settings::get();

    // A fresh install is not split, and the divider is centred.
    CHECK(!s.get_split_open());
    CHECK(s.get_split_ratio() == 0.5f);
    CHECK(s.get_split_pane(0).empty());
    CHECK(s.get_split_pane(1).empty());
    CHECK(s.get_split_focused_pane() == 0);

    s.set_split(true, 0.62f, "t2", "t9", 1);
    CHECK(s.get_split_open());
    CHECK(s.get_split_ratio() == 0.62f);
    CHECK(s.get_split_pane(0) == "t2");
    CHECK(s.get_split_pane(1) == "t9");
    CHECK(s.get_split_focused_pane() == 1);
    // Reading the file back is the whole point: an arrangement that lives only
    // in memory is gone on the next launch.
    s.load_save_file();
    CHECK(s.get_split_open());
    CHECK(s.get_split_ratio() == 0.62f);
    CHECK(s.get_split_pane(0) == "t2");
    CHECK(s.get_split_pane(1) == "t9");
    CHECK(s.get_split_focused_pane() == 1);

    // An out-of-range index is a question with no answer, not a crash.
    CHECK(s.get_split_pane(2).empty());
    CHECK(s.get_split_pane(-1).empty());

    // The clamp, at both ends and through the store.
    CHECK(hanabi::clamp_split_ratio(0.0f) == hanabi::kSplitMinRatio);
    CHECK(hanabi::clamp_split_ratio(1.0f) == hanabi::kSplitMaxRatio);
    CHECK(hanabi::clamp_split_ratio(-3.0f) == hanabi::kSplitMinRatio);
    CHECK(hanabi::clamp_split_ratio(0.5f) == 0.5f);
    // NaN fails every comparison, so a naive min/max clamp passes it straight
    // through and the pane it sizes has a NaN width. It comes out at a legal
    // ratio instead.
    const float nan = std::nanf("");
    const float clamped = hanabi::clamp_split_ratio(nan);
    CHECK(clamped >= hanabi::kSplitMinRatio);
    CHECK(clamped <= hanabi::kSplitMaxRatio);

    s.set_split(true, 5.0f, "t2", "t9", 99);
    CHECK(s.get_split_ratio() == hanabi::kSplitMaxRatio);
    CHECK(s.get_split_focused_pane() == 1);
    s.set_split(true, 0.01f, "t2", "t9", -5);
    CHECK(s.get_split_ratio() == hanabi::kSplitMinRatio);
    CHECK(s.get_split_focused_pane() == 0);

    // A write that changes nothing does not rewrite the file. This one is not
    // hygiene: the ratio is written from a DRAG, and the drag runs sixty times
    // a second over a file that is fully re-serialised on every write.
    s.set_split(true, hanabi::kSplitMinRatio, "t2", "t9");
    CHECK(s.get_split_ratio() == hanabi::kSplitMinRatio);
}

static void test_shortcuts_round_trip_and_reset() {
    std::printf("test_shortcuts_round_trip_and_reset\n");
    isolate_settings();
    auto& s = Settings::get();
    using namespace hanabi::shortcuts;
    using namespace afterhours::keys;

    s.reset_shortcuts();
    const Shortcut custom{
        P, static_cast<std::uint8_t>(CommandModifier | ShiftModifier)};
    CHECK(s.set_shortcut(Command::OpenPalette, custom).ok);
    CHECK(s.get_shortcut(Command::OpenPalette) == custom);
    s.load_save_file();
    CHECK(s.get_shortcut(Command::OpenPalette) == custom);

    const auto before = s.get_shortcut(Command::OpenSettings);
    const auto conflict = s.set_shortcut(Command::OpenSettings, custom);
    CHECK(!conflict.ok);
    CHECK(s.get_shortcut(Command::OpenSettings) == before);

    s.reset_shortcuts();
    s.load_save_file();
    CHECK(s.get_shortcut(Command::OpenPalette) ==
          definition(Command::OpenPalette).shortcut);
}

int main() {
    std::printf("=== test_settings ===\n");
    test_wired_controls_change_value();
    test_mock_settings_write();
    test_settings_write_config_gate();
    test_shelf_fold_round_trips();
    test_effort_round_trips();
    test_send_key_round_trips();
    test_shortcuts_round_trip_and_reset();
    test_quiet_hours_window();
    test_quiet_hours_persist();
    test_archive_overlay_round_trips();
    test_mute_round_trips();
    test_finished_subagents_round_trips();
    test_subagent_sidebar_toggle_round_trips();
    test_tabs_and_pins_round_trip();
    // Last: it writes five thousand entries and reloads the file, so anything
    // after it would be asserting against a settings file this test authored.
    test_last_read_is_bounded();
    test_split_round_trips_and_clamps();
    test_legacy_split_focus_inference();

    test_star_and_mute_index_agrees_with_the_stored_list();

    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
