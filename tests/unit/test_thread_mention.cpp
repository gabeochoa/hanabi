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
    s.title = "(untitled)";  // the agentcloud parse's placeholder
    CHECK(mn::row_title(s) == "profile the disk");
    s.preview.clear();
    CHECK(mn::row_title(s) == "Untitled thread");
}

static void test_a_thread_url_in_text_is_a_thread_link() {
    const std::string base = "https://web.test/chat";
    const std::string text = "see https://web.test/chat/r5 and https://web.test/chat/r6, "
                             "not https://web.test/chat/r7/files or xhttps://web.test/chat/r8";
    const auto links = mn::find_threads(text, base);
    CHECK(links.size() == 2);
    if (links.size() == 2) {
        CHECK(links[0].id == "r5" && text.substr(links[0].off, links[0].len) ==
                                                "https://web.test/chat/r5");
        CHECK(links[1].id == "r6");  // the comma ends it
    }
    CHECK(mn::find_threads(text, "").empty());
}

static void test_spaces_follow_threads_and_write_space_ids() {
    const std::vector<api::SessionSummary> cat{thread("t", "subs migration", 9)};
    const std::vector<api::spaces::Space> spaces{{"1593993452358360", "Subs", "\xf0\x9f\x90\xa6", true, 0, "", "", false, ""},
                                                 {"22", "Infra", "", false, 1, "", "", false, ""},
                                                 {"bad", "Subs bad id", "", false, 2, "", "", false, ""}};
    const auto none = [](const api::SessionSummary&) { return false; };
    const auto r = mn::rows(cat, "@sub", "https://web.test/chat", "", none, spaces);
    CHECK(r.size() == 2);
    if (r.size() == 2) {
        CHECK(!r[0].space && r[0].id == "t");
        CHECK(r[1].space && r[1].id == "1593993452358360" && r[1].title == "Subs");
    }
    CHECK(mn::completed("see @sub", mn::space_reference("1593993452358360")) ==
          "see space:1593993452358360 ");
    // Spaces need no web base; threads do.
    const auto noBase = mn::rows(cat, "@", "", "", none, spaces);
    CHECK(noBase.size() == 2 && noBase[0].space);
}

static void test_automation_born_threads_are_not_offered() {
    auto a = thread("auto", "nightly sweep", 9);
    a.origin_application = "metamate";
    auto mine = thread("mine", "nightly notes", 8);
    mine.origin_application = "metamate";
    mine.title_is_human = true;  // a person's own Metamate chat
    const std::vector<api::SessionSummary> cat{a, mine};
    const auto none = [](const api::SessionSummary&) { return false; };
    const auto r = mn::rows(cat, "@nightly", "https://web.test/chat", "", none);
    CHECK(r.size() == 1 && r[0].id == "mine");
    CHECK(api::is_automation_born(a) && !api::is_automation_born(mine));
    api::SessionSummary noOrigin;
    CHECK(!api::is_automation_born(noOrigin));
}

static void test_references_are_drawn_by_their_current_title() {
    const auto titleOf = [](const std::string& id) -> std::string {
        if (id == "r5") return "profiling the disk";
        return "";  // not held here: stays its URL
    };
    const auto t = mn::titled("see https://w.test/chat/r5 and https://w.test/chat/zz now",
                              "https://w.test/chat", titleOf);
    CHECK(t.text == "see @profiling the disk and https://w.test/chat/zz now");
    CHECK(t.labels.size() == 1 && t.labels[0].first == "@profiling the disk" && t.labels[0].second == "r5");
    CHECK(!t.signature.empty());
    const auto none = mn::titled("plain words", "https://w.test/chat", titleOf);
    CHECK(none.text == "plain words" && none.labels.empty() && none.signature.empty());
}

int main() {
    test_references_are_drawn_by_their_current_title();
    test_automation_born_threads_are_not_offered();
    test_spaces_follow_threads_and_write_space_ids();
    test_a_thread_url_in_text_is_a_thread_link();
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
