#include <cstdio>
#include <string>

#include "../../src/api/draft_key.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

using nlohmann::json;
namespace dk = api::draft_key;

static void test_a_thread_key_ignores_the_pane() {
    std::printf("test_a_thread_key_ignores_the_pane\n");
    CHECK(dk::thread_key("t1") == std::string("t\x1ft1"));
    CHECK(dk::is_pseudo("") && dk::is_pseudo("__kickoff__") && !dk::is_pseudo("t1"));
}

static void test_a_legacy_reply_key_names_its_modern_key() {
    std::printf("test_a_legacy_reply_key_names_its_modern_key\n");
    CHECK(*dk::modern_of_legacy(std::string("reply:0\x1ft1")) == std::string("reply:t\x1ft1"));
    CHECK(*dk::modern_of_legacy(std::string("reply:1\x1ft1")) == std::string("reply:t\x1ft1"));
    CHECK(*dk::modern_of_legacy(std::string("reply:12\x1fr8")) == std::string("reply:t\x1fr8"));
    CHECK(!dk::modern_of_legacy(std::string("reply:0\x1f__kickoff__")));
    CHECK(!dk::modern_of_legacy(std::string("reply:0\x1f")));
    CHECK(!dk::modern_of_legacy(std::string("reply:t\x1ft1")));
    CHECK(!dk::modern_of_legacy("ob:t1"));
    CHECK(!dk::modern_of_legacy("t1"));
    CHECK(!dk::modern_of_legacy(std::string("reply:\x1ft1")));
}

static void test_a_saved_draft_moves_to_the_thread_key_once() {
    std::printf("test_a_saved_draft_moves_to_the_thread_key_once\n");
    json drafts = json::object();
    drafts[std::string("reply:0\x1ft1")] = {{"text", "left"}};
    drafts[std::string("reply:1\x1ft2")] = {{"text", "right"}};
    drafts[std::string("reply:0\x1f__kickoff__")] = {{"text", "new task"}};
    drafts["ob:t1"] = {{"queue", json::array({"p"})}};
    CHECK(dk::migrate_legacy(drafts) == 2);
    CHECK(drafts[std::string("reply:t\x1ft1")]["text"] == "left");
    CHECK(drafts[std::string("reply:t\x1ft2")]["text"] == "right");
    CHECK(!drafts.contains(std::string("reply:0\x1ft1")));
    CHECK(drafts[std::string("reply:0\x1f__kickoff__")]["text"] == "new task");
    CHECK(drafts.contains("ob:t1"));
    CHECK(dk::migrate_legacy(drafts) == 0);
}

static void test_a_modern_draft_wins_over_a_legacy_one() {
    std::printf("test_a_modern_draft_wins_over_a_legacy_one\n");
    json drafts = json::object();
    drafts[std::string("reply:t\x1ft1")] = {{"text", "modern"}};
    drafts[std::string("reply:0\x1ft1")] = {{"text", "old"}};
    CHECK(dk::migrate_legacy(drafts) == 0);
    CHECK(drafts[std::string("reply:t\x1ft1")]["text"] == "modern");
    CHECK(!drafts.contains(std::string("reply:0\x1ft1")));
    json notObject = json::array();
    CHECK(dk::migrate_legacy(notObject) == 0);
}

int main() {
    test_a_thread_key_ignores_the_pane();
    test_a_legacy_reply_key_names_its_modern_key();
    test_a_saved_draft_moves_to_the_thread_key_once();
    test_a_modern_draft_wins_over_a_legacy_one();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all passed\n");
    return 0;
}
