// Where a session started, whether a machine started it, and what the
// sidebar does with both (api/session_origin.h, ecs/sidebar_buckets.h):
// the reference's AutomationOrigin precedence and its Group by Origin.
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/api/session_origin.h"
#include "../../src/ecs/sidebar_buckets.h"
#include "../../src/ui/slash_commands.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace o = api::origin;
using api::SessionSummary;

static SessionSummary row(std::string id, std::string title, std::string origin = {},
                          bool human = false, bool pinned = false) {
    SessionSummary s;
    s.id = std::move(id);
    s.title = std::move(title);
    s.origin_application = std::move(origin);
    s.title_is_human = human;
    s.starred = pinned;
    return s;
}

static void test_machine_run_names_are_anchored_and_exact() {
    std::printf("test_machine_run_names_are_anchored_and_exact\n");
    const std::string hex32 = "0123456789abcdef0123456789abcdef";
    CHECK(o::is_machine_run_name("medi_mission_" + hex32));
    CHECK(o::is_machine_run_name("  medi_mission_" + hex32 + "\n"));  // outer whitespace
    CHECK(!o::is_machine_run_name("medi_mission_" + hex32.substr(1)));  // 31 digits
    CHECK(!o::is_machine_run_name("medi_mission_" + hex32 + "0"));      // 33 digits
    CHECK(!o::is_machine_run_name("medi_mission_0123456789ABCDEF0123456789abcdef"));  // upper
    CHECK(!o::is_machine_run_name("medi_mission_" + hex32 + " notes"));
    CHECK(!o::is_machine_run_name("re: medi_mission_" + hex32));  // never a prefix match
    CHECK(o::is_machine_run_name("Automation TaskAttempt 42"));
    CHECK(o::is_machine_run_name("Automation TaskAttempt 0"));
    CHECK(!o::is_machine_run_name("Automation TaskAttempt "));
    CHECK(!o::is_machine_run_name("Automation TaskAttempt 4a"));
    CHECK(!o::is_machine_run_name("automation taskattempt 4"));
    CHECK(!o::is_machine_run_name("Automation TaskAttempt 4 retry"));
    CHECK(!o::is_machine_run_name(""));
    CHECK(!o::is_machine_run_name("Fix the build"));
}

static void test_automation_precedence_is_the_references() {
    std::printf("test_automation_precedence_is_the_references\n");
    const std::string mission = "medi_mission_ffffffffffffffffffffffffffffffff";
    // 1. a reserved name decides whatever else says -- even a human flag and no origin.
    CHECK(o::is_automation_born("", mission, true));
    CHECK(o::is_automation_born("gchat", "Automation TaskAttempt 7", false));
    // 2. a human-set title exempts a metamate row.
    CHECK(!o::is_automation_born("metamate", "My Space chat", true));
    // 3. only then the origin: metamate and its canonical spelling.
    CHECK(o::is_automation_born("metamate", "Nightly digest", false));
    CHECK(o::is_automation_born("metamate_automation", "Nightly digest", false));
    CHECK(!o::is_automation_born("gchat", "Nightly digest", false));
    // No recorded origin is never an automation.
    CHECK(!o::is_automation_born("", "Nightly digest", false));
    CHECK(api::is_automation_born(row("a", mission)));
}

static void test_origin_terms_are_total() {
    std::printf("test_origin_terms_are_total\n");
    CHECK(o::term_of("") == "web");
    CHECK(o::term_of("agentcloud_web") == "web");
    CHECK(o::term_of("gchat") == "chat");
    CHECK(o::term_of("agentcloud_cli") == "cli");
    CHECK(o::term_of("zoom") == "vc" && o::term_of("google_meet") == "vc");
    CHECK(o::term_of("metamate_automation") == "metamate");
    CHECK(o::term_of("tasks") == "tasks");
    // An unregistered name answers its own word, never "web".
    CHECK(o::term_of("brand_new_surface") == "brand_new_surface");
}

static bool none(const std::string&, const std::string&) { return false; }

static bool has(const std::vector<const SessionSummary*>& v, const std::string& id) {
    for (const auto* s : v)
        if (s->id == id) return true;
    return false;
}

static void test_the_sidebar_keeps_automations_off_unless_pinned_or_shown() {
    std::printf("test_the_sidebar_keeps_automations_off_unless_pinned_or_shown\n");
    std::vector<SessionSummary> cat = {
        row("mine", "Fix the build", "agentcloud_web"),
        row("cron", "Nightly digest", "metamate"),
        row("medi", "medi_mission_0123456789abcdef0123456789abcdef"),
        row("pinned", "Automation TaskAttempt 3", "", false, true),
        row("spacechat", "Ask about Q4", "metamate", true),
    };
    cat.push_back(row("filed", "Automation TaskAttempt 9"));
    cat.back().folder = "ops";
    ecs::model::SidebarBuckets b;
    b.rebuild(1, cat, "", false, none);
    const auto& recent = b.recent();
    CHECK(has(recent, "mine") && has(recent, "spacechat") && has(recent, "pinned"));
    CHECK(!has(recent, "cron") && !has(recent, "medi"));
    // Counted for the switch: unpinned automation-born rows, filed ones too.
    CHECK(b.automation_count() == 3);
    // A folder whose only member is hidden still lists, and says it is hidden.
    CHECK(b.hidden("ops") == 1 && b.members("ops").empty());
    // A search does not reach them either.
    b.rebuild(1, cat, "digest", false, none);
    CHECK(!has(b.recent(), "cron"));
    // The switch brings them back without a catalog change.
    b.set_show_automation(true);
    b.rebuild(1, cat, "", false, none);
    CHECK(has(b.recent(), "cron") && has(b.recent(), "medi"));
    CHECK(has(b.members("ops"), "filed") && b.hidden("ops") == 0);
    CHECK(b.automation_count() == 3);
}

static void test_group_by_origin_sections_every_thread() {
    std::printf("test_group_by_origin_sections_every_thread\n");
    std::vector<SessionSummary> cat = {
        row("w", "Web thread", ""),
        row("c", "Chat thread", "gchat"),
        row("l", "CLI thread", "agentcloud_cli"),
        row("u", "New surface", "brand_new_surface"),
        row("m", "Space chat", "metamate", true),
    };
    cat[0].folder = "ops";  // a folder does not section under Origin
    ecs::model::SidebarBuckets b;
    b.set_grouping(ecs::model::Grouping::Origin, true);  // only-ungrouped is meaningless here
    b.rebuild(1, cat, "", false, none);
    CHECK(b.recent().empty());
    CHECK(has(b.members("origin:web"), "w"));
    CHECK(has(b.members("origin:chat"), "c"));
    CHECK(has(b.members("origin:cli"), "l"));
    CHECK(has(b.members("origin:brand_new_surface"), "u"));
    CHECK(has(b.members("origin:metamate"), "m"));
    CHECK(b.members("ops").empty());
    CHECK(ecs::model::grouping_from("origin") == ecs::model::Grouping::Origin);
    CHECK(std::string(ecs::model::grouping_name(ecs::model::Grouping::Origin)) == "origin");
    CHECK(!ecs::model::grouping_has_ungrouped(ecs::model::Grouping::Origin));
}

static void test_goal_and_plan_go_to_the_session_as_text() {
    std::printf("test_goal_and_plan_go_to_the_session_as_text\n");
    namespace sl = hanabi::slash;
    // Listed, /goal before /plan, both on the slash menu's first letters.
    const auto g = sl::filter("/g");
    CHECK(!g.empty() && g.front()->name == "goal");
    const auto p = sl::filter("/pl");
    CHECK(p.size() == 1 && p.front()->name == "plan");
    int goalAt = -1, planAt = -1, i = 0;
    for (const auto& c : sl::all()) {
        if (c.name == "goal") goalAt = i;
        if (c.name == "plan") planAt = i;
        ++i;
    }
    CHECK(goalAt >= 0 && planAt == goalAt + 1);
    // Choosing completes the verb with a space for its argument.
    CHECK(sl::completion(*sl::find("goal")) == "/goal ");
    // Once the argument has begun, the draft is a message, menu or not.
    CHECK(sl::sends_as_text("/goal ship the migration by Friday", true));
    CHECK(sl::sends_as_text("/plan ", false));
    // Bare: sent when no menu is up to choose from, chosen when one is.
    CHECK(sl::sends_as_text("/goal", false));
    CHECK(!sl::sends_as_text("/goal", true));
    // The client's own verbs stay the client's.
    CHECK(!sl::sends_as_text("/btw why", false));
    CHECK(!sl::sends_as_text("/model", false));
    CHECK(!sl::sends_as_text("/nope x", false));
    CHECK(!sl::sends_as_text("goal x", false));
}

int main() {
    std::printf("=== test_session_origin ===\n");
    test_machine_run_names_are_anchored_and_exact();
    test_automation_precedence_is_the_references();
    test_origin_terms_are_total();
    test_the_sidebar_keeps_automations_off_unless_pinned_or_shown();
    test_group_by_origin_sections_every_thread();
    test_goal_and_plan_go_to_the_session_as_text();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
