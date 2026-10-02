#include <cstdio>
#include <string>
#include <vector>

#include "../../src/ui/thread_mention.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace mn = hanabi::mention;

static api::SessionSummary thread(std::string id, std::string title, int64_t at,
                                  std::string parent = "") {
    api::SessionSummary s;
    s.id = std::move(id);
    s.title = std::move(title);
    s.updated_at = at;
    s.parent_id = std::move(parent);
    return s;
}

static void test_the_at_must_open_a_word() {
    CHECK(mn::query("@") == std::string());
    CHECK(mn::query("see @todo") == std::string("todo"));
    CHECK(mn::query("see @todo pipeline") == std::string("todo pipeline"));
    CHECK(!mn::query("gabe@meta.com"));
    CHECK(!mn::query("pay me @ 5pm"));
    CHECK(!mn::query("no mention here"));
    CHECK(!mn::query("@" + std::string(65, 'x')));
}

static void test_ranking_is_starts_then_word_then_contains_freshest_first() {
    const std::vector<api::SessionSummary> cat{
        thread("a", "Chat collector for subtodos", 50),
        thread("b", "Take on all 33 TODO rows", 40),
        thread("c", "TODO rows - dispatched work", 10),
        thread("d", "todo sweep", 20),
        thread("e", "unrelated", 99),
        thread("f", "todo child", 99, "d"),
    };
    const auto none = [](const api::SessionSummary&) { return false; };
    const auto rows = mn::rows(cat, "see @todo", "https://web.test/chat", "", none);
    CHECK(rows.size() == 4);
    CHECK(rows[0].id == "d" && rows[1].id == "c");  // starts-with, freshest first
    CHECK(rows[2].id == "b");                        // a word starts with it
    CHECK(rows[3].id == "a");                        // contains
    // Bare @: freshest root threads, never this one, never a sub-agent.
    const auto bare = mn::rows(cat, "@", "https://web.test/chat", "e", none);
    CHECK(!bare.empty() && bare[0].id == "a");
    for (const auto& r : bare) CHECK(r.id != "e" && r.id != "f");
    // A query nothing answers offers nothing, which closes the picker.
    CHECK(mn::rows(cat, "@zzz", "https://web.test/chat", "", none).empty());
    // No web base: nothing to write, no picker.
    CHECK(mn::rows(cat, "@todo", "", "", none).empty());
}

static void test_a_pick_writes_the_web_url_and_marks_the_row() {
    const std::string link = mn::link_for("https://web.test/chat/", "c");
    CHECK(link == "https://web.test/chat/c");
    CHECK(mn::completed("ask @tod", link) == "ask https://web.test/chat/c ");
    CHECK(mn::completed("no span", link) == "no span");
    CHECK(mn::names("see https://web.test/chat/c and @", link));
    CHECK(!mn::names("see https://web.test/chat/cd", link));
    const std::vector<api::SessionSummary> cat{thread("c", "TODO rows", 10)};
    const auto rows = mn::rows(cat, "see https://web.test/chat/c and @",
                               "https://web.test/chat", "",
                               [](const api::SessionSummary&) { return false; });
    CHECK(rows.size() == 1 && rows[0].alreadyNamed);
}

static void test_an_untitled_thread_is_named_from_what_it_carries() {
    api::SessionSummary s = thread("u", "", 5);
    s.preview = "profile the disk\nand more";
    CHECK(mn::row_title(s) == "profile the disk");
    s.preview.clear();
    CHECK(mn::row_title(s) == "Untitled thread");
}

int main() {
    test_the_at_must_open_a_word();
    test_ranking_is_starts_then_word_then_contains_freshest_first();
    test_a_pick_writes_the_web_url_and_marks_the_row();
    test_an_untitled_thread_is_named_from_what_it_carries();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
