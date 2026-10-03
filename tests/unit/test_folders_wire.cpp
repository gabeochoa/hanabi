#include <cstdio>
#include <string>

#include "../../src/api/folders_wire.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace fo = api::folders;
using json = nlohmann::json;

int main() {
    const auto folders = fo::parse_folders(
        R"({"folders":[{"id":"f2","name":"Beta","position":1},{"id":"f1","name":"Alpha","position":1},{"id":"f0","name":"First","position":0},{"id":"","name":"x"}]})");
    CHECK(folders.has_value() && folders->size() == 3);
    const auto members = fo::parse_membership(
        R"({"overlays":[{"sessionId":"s1","folderId":"f1"},{"sessionId":"s2","folderId":null},{"sessionId":"s3","folderId":"gone"}]})");
    CHECK(members.has_value() && members->size() == 2);
    const fo::Carve c = fo::carve(*folders, *members);
    CHECK(c.folders[0].id == "f0" && c.folders[1].id == "f1" && c.folders[2].id == "f2");
    CHECK(c.membership.size() == 1 && c.membership.at("s1") == "f1");  // "gone" dropped
    CHECK(!fo::parse_folders("{}").has_value() && !fo::parse_membership("nope").has_value());

    const auto made = fo::parse_created(R"({"folder":{"id":"f9","name":"New","position":3}})");
    CHECK(made && made->id == "f9" && made->position == 3);
    CHECK(fo::validate_name("  Work  ").name == "Work" && fo::validate_name("  ").error != "");
    CHECK(fo::validate_name(std::string(101, 'x')).error != "");
    CHECK(fo::refusal(R"({"error":"Folder name already exists"})", "x") == "Folder name already exists");
    CHECK(fo::refusal("<html>", "fallback") == "fallback");
    CHECK(json::parse(fo::file_body("s1", ""))["folderId"].is_null());
    CHECK(json::parse(fo::file_body("s1", "f1"))["folderId"] == "f1");
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
