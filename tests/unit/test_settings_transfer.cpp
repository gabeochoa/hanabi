#include <cstdio>
#include <string>

#include "../../src/ui/settings_transfer.h"

namespace st = hanabi::settings_transfer;
using json = nlohmann::json;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_settings_transfer ===\n");
    CHECK(st::export_path("/Users/x/Desktop") == "/Users/x/Desktop/hanabi-settings.json");
    CHECK(st::export_path("/Users/x/Desktop/") == "/Users/x/Desktop/hanabi-settings.json");
    CHECK(st::is_allowed("theme") && st::is_allowed("sound_override") && st::is_allowed("session_theme"));
    for (const char* state : {"open_tabs", "active_tab", "last_session", "last_read", "read_seq", "archived", "muted",
                              "starred", "starred_turns", "window_width", "window_height", "split_open", "split_ratio",
                              "split_panes", "split_focused_pane", "sidebar_collapsed", "export_dir",
                              "pins_migrated_to_web", "acknowledged_blocked", "expanded_parents", "tool_fold",
                              "row_order", "selected_view", "settings_pane", "account_autocompact"})
        CHECK(!st::is_allowed(state));

    const json file = {{"theme", "dark"}, {"font", "mono"}, {"open_tabs", json::array({"t1"})},
                       {"window_width", 1100}, {"sound_override", {{"t6", -1}}}};
    const json env = st::envelope(file, "2026-10-18T00:00:00Z");
    CHECK(env["format"] == 1 && env["app"] == "hanabi" && env["exported_at"] == "2026-10-18T00:00:00Z");
    CHECK(env["settings"].size() == 3);
    CHECK(env["settings"]["theme"] == "dark" && !env["settings"].contains("open_tabs") &&
          !env["settings"].contains("window_width"));

    const auto ok = st::extract(env.dump());
    CHECK(ok.verdict == st::Verdict::Ok && ok.keys.size() == 3 && ok.settings["font"] == "mono");
    const auto raw = st::extract(R"({"theme":"light","open_tabs":["t1"]})");
    CHECK(raw.verdict == st::Verdict::Ok && raw.keys.size() == 1 && raw.keys[0] == "theme");
    CHECK(st::extract("").verdict == st::Verdict::Empty);
    CHECK(st::extract("   \n").verdict == st::Verdict::Empty);
    CHECK(st::extract("[1]").verdict == st::Verdict::NotJsonObject);
    CHECK(st::extract("nope").verdict == st::Verdict::NotJsonObject);
    CHECK(st::extract(R"({"open_tabs":["t1"]})").verdict == st::Verdict::NotSettings);
    CHECK(st::extract(R"({"format":2,"app":"hanabi","settings":{"theme":"dark"}})").verdict == st::Verdict::WrongFormat);
    CHECK(st::extract(R"({"format":1,"app":"puffin","settings":{"theme":"dark"}})").verdict == st::Verdict::NotSettings);
    CHECK(st::reason(st::Verdict::WrongFormat) == "a newer settings format than this build reads");
    CHECK(st::reason(st::Verdict::NotSettings) == "no Hanabi settings in it");

    const std::string line = st::preview_line({"theme", "font", "run_chime", "shortcuts", "yap_level", "send_key"}, "~/x.json");
    CHECK(line == "6 settings from ~/x.json: theme, font, run_chime, shortcuts and 2 more. Apply replaces those; everything else stays.");
    CHECK(st::preview_line({"theme"}, "f") == "1 setting from f: theme. Apply replaces those; everything else stays.");

    const json merged = st::merge(json{{"theme", "dark"}, {"open_tabs", json::array({"t1"})}, {"font", "sans"}},
                                  json{{"theme", "light"}, {"open_tabs", json::array()}, {"yap_level", 2}});
    CHECK(merged["theme"] == "light" && merged["font"] == "sans" && merged["yap_level"] == 2);
    CHECK(merged["open_tabs"].size() == 1);
    CHECK(st::merge(json(), json{{"theme", "dark"}})["theme"] == "dark");
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
