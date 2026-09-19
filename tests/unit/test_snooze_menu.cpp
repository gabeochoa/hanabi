#include <cstdio>
#include <ctime>
#include <string>

#include "../../src/ui/snooze_menu.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace sm = hanabi::snooze_menu;
namespace sp = hanabi::snooze_presets;

static std::tm utc_local(std::int64_t at) {
    std::tm out{};
    const std::time_t t = static_cast<std::time_t>(at);
    gmtime_r(&t, &out);
    return out;
}
static std::int64_t utc_from_local(std::tm local) { return static_cast<std::int64_t>(timegm(&local)); }

int main() {
    std::printf("=== test_snooze_menu ===\n");
    const sp::LocalCalendar utc{utc_local, utc_from_local};
    const std::int64_t now = 1789700000;

    CHECK(sm::parse("unsnooze") && !sm::parse("unsnooze")->until);
    CHECK(sm::parse("snooze:1789703600") && *sm::parse("snooze:1789703600")->until == 1789703600);
    CHECK(!sm::parse("snooze:"));
    CHECK(!sm::parse("snooze:12a"));
    CHECK(!sm::parse("snooze"));
    CHECK(!sm::parse("mute"));
    CHECK(!sm::parse(""));
    CHECK(sm::leaf_id(42) == "snooze:42");

    {
        const auto m = sm::item(std::nullopt, now, "row_menu_snooze", false, false, utc);
        CHECK(m.label == "Snooze" && m.action_id == "snooze" && !m.disabled);
        const auto opts = sp::options(now, utc);
        CHECK(!opts.empty() && m.children.size() == opts.size());
        for (std::size_t i = 0; i < opts.size(); ++i) {
            CHECK(m.children[i].action_id == sm::leaf_id(opts[i].until));
            CHECK(m.children[i].label == sp::option_text(opts[i], now, utc));
            CHECK(m.children[i].debug_name == std::string("row_menu_snooze_") + sp::raw_name(opts[i].choice));
            CHECK(!m.children[i].disabled);
            const auto pick = sm::parse(m.children[i].action_id);
            CHECK(pick && pick->until && *pick->until == opts[i].until);
        }
        const auto later = sm::item(std::nullopt, now + 3600, "row_menu_snooze", false, false, utc);
        CHECK(later.children.front().action_id != m.children.front().action_id);
    }
    {
        const auto m = sm::item(std::nullopt, now, "tab_menu_snooze", true, false, utc);
        CHECK(m.disabled);
        for (const auto& leaf : m.children) CHECK(leaf.disabled);
    }
    {
        const auto m = sm::item(now + 7200, now, "row_menu_snooze", false, false, utc);
        CHECK(m.children.empty());
        CHECK(m.action_id == "unsnooze");
        CHECK(m.label == sp::snoozed_until_text(now + 7200, now, utc));
        CHECK(m.label.rfind("Snoozed until ", 0) == 0);
        CHECK(sm::parse(m.action_id) && !sm::parse(m.action_id)->until);
    }
    {
        CHECK(!sm::active_until(std::nullopt, now));
        CHECK(!sm::active_until(std::optional<std::int64_t>(now - 60), now));
        CHECK(!sm::active_until(std::optional<std::int64_t>(now), now));
        CHECK(sm::active_until(std::optional<std::int64_t>(now + 1), now) &&
              *sm::active_until(std::optional<std::int64_t>(now + 1), now) == now + 1);
        const auto woke = sm::item(std::optional<std::int64_t>(now - 60), now, "row_menu_snooze", false, false, utc);
        CHECK(woke.label == "Snooze" && woke.action_id == "snooze" && !woke.children.empty());
        const auto due = sm::item(std::optional<std::int64_t>(now), now, "row_menu_snooze", false, false, utc);
        CHECK(due.label == "Snooze" && !due.children.empty());
        const auto pending = sm::item(std::optional<std::int64_t>(now + 1), now, "row_menu_snooze", false, false, utc);
        CHECK(pending.label.rfind("Snoozed until ", 0) == 0 && pending.children.empty() && pending.action_id == "unsnooze");
    }

    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
