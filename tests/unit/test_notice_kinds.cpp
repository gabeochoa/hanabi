// The notice kinds the server added most recently each read as themselves;
// a kind this build predates is still the generic platform notice, one row.
#include <cstdio>
#include <string>

#include "../../src/api/notice_kinds.h"
#include "../../src/api/tool_kinds.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

using api::notice_kinds::Kind;
using api::notice_kinds::Severity;
using api::notice_kinds::display_line;
using api::notice_kinds::from_wire;
using api::notice_kinds::is_known;
using api::notice_kinds::severity;
using api::notice_kinds::title;
using api::notice_kinds::wire_name;

static void test_the_new_kinds_are_known_words() {
    // The six the reference ruled on in 2026-11-16, plus the two generic
    // kinds that followed. Each must round-trip, or the row falls back to
    // the generic notice and the reader loses which thing happened.
    const char* const wires[] = {
        "conversation_lost", "ancestor_halted", "mcp_roster_updated",
        "memory_not_loaded", "settings_switch", "plugin_hook",
        "delivery_failures_parked", "directory_context_unavailable",
    };
    for (const char* w : wires) {
        CHECK(is_known(w));
        CHECK(std::string(wire_name(from_wire(w))) == w);
    }
    CHECK(from_wire("conversation_lost") == Kind::ConversationLost);
    CHECK(from_wire("ancestor_halted") == Kind::AncestorHalted);
    CHECK(from_wire("mcp_roster_updated") == Kind::McpRosterUpdated);
    CHECK(from_wire("memory_not_loaded") == Kind::MemoryNotLoaded);
    CHECK(from_wire("settings_switch") == Kind::SettingsSwitch);
    CHECK(from_wire("plugin_hook") == Kind::PluginHook);
    CHECK(from_wire("delivery_failures_parked") == Kind::DeliveryFailuresParked);
    CHECK(from_wire("directory_context_unavailable") == Kind::DirectoryContextUnavailable);
}

static void test_the_new_kinds_carry_their_rulings() {
    CHECK(std::string(title(Kind::ConversationLost)) == "Conversation lost");
    CHECK(severity(Kind::ConversationLost) == Severity::Attention);
    CHECK(std::string(title(Kind::AncestorHalted)) == "Ancestor halted");
    CHECK(severity(Kind::AncestorHalted) == Severity::Attention);
    CHECK(std::string(title(Kind::McpRosterUpdated)) == "Tool roster updated");
    CHECK(severity(Kind::McpRosterUpdated) == Severity::Informational);
    CHECK(std::string(title(Kind::MemoryNotLoaded)) == "Memory not loaded");
    CHECK(severity(Kind::MemoryNotLoaded) == Severity::Attention);
    CHECK(std::string(title(Kind::SettingsSwitch)) == "Skills from settings");
    CHECK(severity(Kind::SettingsSwitch) == Severity::Informational);
    CHECK(std::string(title(Kind::PluginHook)) == "Plugin hook");
    CHECK(severity(Kind::PluginHook) == Severity::Attention);
    // The two the reference draws generically keep the generic heading.
    CHECK(std::string(title(Kind::DeliveryFailuresParked)) == "Platform notice");
    CHECK(std::string(title(Kind::DirectoryContextUnavailable)) == "Platform notice");
}

static void test_the_older_kinds_keep_their_rulings() {
    CHECK(from_wire("refusal") == Kind::Refusal);
    CHECK(severity(Kind::Refusal) == Severity::Alert);
    CHECK(std::string(title(Kind::Refusal)) == "Refused");
    CHECK(severity(Kind::BudgetBreached) == Severity::Alert);
    CHECK(severity(Kind::ApprovalTimeoutsParked) == Severity::Alert);
    CHECK(std::string(title(Kind::ModelRetry)) == "Retrying");
}

static void test_a_notice_line_names_its_kind_then_the_message() {
    CHECK(display_line("conversation_lost", "the earlier conversation is gone") ==
          "Conversation lost \xe2\x80\x94 the earlier conversation is gone");
    CHECK(display_line("ancestor_halted", "queued behind a halt") ==
          "Ancestor halted \xe2\x80\x94 queued behind a halt");
    CHECK(display_line("settings_switch", "skills come from settings") ==
          "Skills from settings \xe2\x80\x94 skills come from settings");
    CHECK(display_line("plugin_hook", "check the path") ==
          "Plugin hook \xe2\x80\x94 check the path");
    // No message: the heading is the row, not an empty stamp.
    CHECK(display_line("memory_not_loaded", "") == "Memory not loaded");
    CHECK(display_line("mcp_roster_updated", "") == "Tool roster updated");
}

static void test_an_unruled_kind_is_the_generic_notice() {
    // The generic kinds, and one this build has never seen: the message is
    // the row, exactly as before kinds were ruled on. Nothing is dropped
    // and no heading is invented from the wire word.
    CHECK(display_line("delivery_failures_parked", "deliveries are parked") ==
          "deliveries are parked");
    CHECK(display_line("directory_context_unavailable", "no directory context") ==
          "no directory context");
    CHECK(display_line("kind_from_next_quarter", "something new happened") ==
          "something new happened");
    CHECK(from_wire("kind_from_next_quarter") == Kind::Unknown);
    CHECK(!is_known("kind_from_next_quarter"));
    CHECK(severity(Kind::Unknown) == Severity::Attention);
    CHECK(display_line("kind_from_next_quarter", "") == "kind_from_next_quarter");
    CHECK(display_line("", "") == "Platform notice");
}

static void test_the_reserve_tool_reads_as_itself() {
    // The node reserve tool the reference added to its catalog: hanabi has
    // no tool catalog, a call draws through the generic tool row, and its
    // name must still read as words rather than as the wire spelling.
    CHECK(api::tool_kinds::classify("node__reserve") == api::tool_kinds::Kind::Generic);
    CHECK(api::tool_kinds::display_name("node__reserve") == "node reserve");
    CHECK(api::tool_kinds::display_name("node__reserve").find("__") == std::string::npos);
}

int main() {
    std::printf("=== test_notice_kinds ===\n");
    test_the_new_kinds_are_known_words();
    test_the_new_kinds_carry_their_rulings();
    test_the_older_kinds_keep_their_rulings();
    test_a_notice_line_names_its_kind_then_the_message();
    test_an_unruled_kind_is_the_generic_notice();
    test_the_reserve_tool_reads_as_itself();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
