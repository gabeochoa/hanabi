#include <cstdio>
#include <string>

#include "../../src/api/memory_wire.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace mem = api::memory;
using json = nlohmann::json;

static std::string envelope(const char* field, json payload) {
    return json{{"success", true}, {"result", {{"data", {{field, payload}}}}}}.dump();
}

static void test_paths_and_keys() {
    CHECK(mem::valid_path(""));
    CHECK(mem::valid_path("projects/infra"));
    CHECK(!mem::valid_path("projects//infra"));
    CHECK(!mem::valid_path("../up"));
    CHECK(!mem::valid_path("a\\b"));
    CHECK(mem::valid_key("notes.md"));
    CHECK(!mem::valid_key("a/b.md"));
    CHECK(!mem::valid_key(".memex_keep"));
    CHECK(!mem::valid_key(""));
}

static void test_bodies_carry_variables_as_a_string() {
    const json list = json::parse(mem::list_body("projects"));
    CHECK(list["query_text"].get<std::string>().find("memex_memory_namespace") != std::string::npos);
    CHECK(list["variables"].is_string());
    const json vars = json::parse(list["variables"].get<std::string>());
    CHECK(vars["input"]["usecase"] == "metamate_personal");
    CHECK(vars["input"]["sub_namespace"] == "projects");
    const json create = json::parse(json::parse(mem::write_body("", "n.md", "hi", ""))["variables"].get<std::string>());
    CHECK(create["input"]["mode"] == "CREATE_ONLY" && !create["input"].contains("expected_version"));
    const json edit = json::parse(json::parse(mem::write_body("", "n.md", "hi", "v3"))["variables"].get<std::string>());
    CHECK(edit["input"]["mode"] == "UPDATE_ONLY" && edit["input"]["expected_version"] == "v3");
}

static void test_a_listing_is_believed_only_for_the_folder_asked() {
    const json payload{{"usecase", "metamate_personal"},
                       {"root_namespace", "u123"},
                       {"sub_namespace", "projects"},
                       {"memories", json::array({{{"key", "a.md"}, {"content_length", 12}},
                                                 {{"key", "a.md"}},
                                                 {{"key", "b.md"}, {"is_link", true}}})},
                       {"sub_namespaces", json::array({"projects/x", "projects/a"})},
                       {"next_cursor", ""}};
    json v;
    std::string why;
    CHECK(mem::unwrap(envelope("memex_memory_namespace", payload), "memex_memory_namespace", &v, &why));
    const auto listing = mem::parse_listing(v, "projects");
    CHECK(listing.has_value());
    if (listing) {
        CHECK(listing->files.size() == 2 && listing->files[0].bytes == 12 && listing->files[1].linked);
        CHECK(listing->folders.size() == 2 && listing->folders[0] == "projects/a");
        CHECK(listing->root == "u123");
    }
    CHECK(!mem::parse_listing(v, "other").has_value());
    json bad = payload;
    bad["sub_namespaces"] = json::array({"elsewhere/x"});
    CHECK(!mem::parse_listing(bad, "projects").has_value());
    CHECK(!mem::unwrap("{\"success\":false}", "memex_memory_namespace", &v, &why));
    CHECK(!mem::unwrap("nope", "memex_memory_namespace", &v, &why));
}

static void test_a_document_must_be_the_key_asked() {
    const json payload{{"usecase", "metamate_personal"}, {"root_namespace", "u1"},
                       {"sub_namespace", ""}, {"key", "n.md"}, {"content", "hello"},
                       {"version", "v7"}};
    const auto d = mem::parse_document(payload, "", "n.md");
    CHECK(d && d->content == "hello" && d->version == "v7");
    CHECK(!mem::parse_document(payload, "", "other.md"));
    CHECK(mem::folder_name("a/b/c") == "c" && mem::parent_of("a/b/c") == "a/b" &&
          mem::parent_of("a").empty());
}

int main() {
    test_paths_and_keys();
    test_bodies_carry_variables_as_a_string();
    test_a_listing_is_believed_only_for_the_folder_asked();
    test_a_document_must_be_the_key_asked();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
