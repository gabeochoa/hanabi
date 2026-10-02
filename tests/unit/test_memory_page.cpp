#include <cstdio>
#include <memory>
#include <string>

#include "../../src/api/mock_client.h"
#include "../../src/ecs/memory_page.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

// One request in, run to its answer.
static void settle(ecs::MemoryPage& m, const std::shared_ptr<api::Client>& c) {
    ecs::service_memory(m, c);        // starts it
    ecs::service_memory(m, c, true);  // lands it
}

static void test_list_open_edit_save() {
    std::shared_ptr<api::Client> c = std::make_shared<api::MockClient>();
    ecs::MemoryPage m;
    m.requestList = true;
    settle(m, c);
    CHECK(m.listed && m.files.size() == 2 && m.folders.size() == 1 && m.error.empty());
    m.requestOpenKey = "core_memory.md";
    settle(m, c);
    CHECK(m.doc && m.doc->version == "v3" && m.draft == m.doc->content);
    m.draft += "- Likes tests.\n";
    CHECK(m.dirty() && m.can_save());
    // A dirty draft holds the page: no listing, no other file.
    m.requestList = true;
    settle(m, c);
    CHECK(m.error == "Save or discard this edit first." && m.doc && m.dirty());
    m.requestSave = true;
    settle(m, c);
    CHECK(m.notice == "Saved." && m.doc->version == "v4" && !m.dirty());
}

static void test_a_stale_save_keeps_the_edit_and_says_so() {
    std::shared_ptr<api::Client> c = std::make_shared<api::MockClient>();
    ecs::MemoryPage m;
    m.requestList = true;
    settle(m, c);
    m.requestOpenKey = "working_preferences.md";
    settle(m, c);
    // Someone else saves first.
    CHECK(c->memory_write("", "working_preferences.md", "theirs", m.doc->version).ok);
    m.draft = "mine";
    m.requestSave = true;
    settle(m, c);
    CHECK(m.error.find("Your edits are still here") != std::string::npos);
    CHECK(m.draft == "mine" && m.dirty());
}

static void test_create_adds_the_file_and_opens_it() {
    std::shared_ptr<api::Client> c = std::make_shared<api::MockClient>();
    ecs::MemoryPage m;
    m.requestList = true;
    settle(m, c);
    m.requestCreateKey = "bad/name";
    settle(m, c);
    CHECK(m.error.find("file name") != std::string::npos);
    m.requestCreateKey = "fresh.md";
    settle(m, c);
    CHECK(m.notice == "Created." && m.doc && m.doc->key == "fresh.md" && m.files.size() == 3);
    // Creating it again is refused by CREATE_ONLY.
    m.requestCreateKey = "fresh.md";
    settle(m, c);
    CHECK(m.error.find("Couldn't create") != std::string::npos);
}

int main() {
    test_list_open_edit_save();
    test_a_stale_save_keeps_the_edit_and_says_so();
    test_create_adds_the_file_and_opens_it();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
