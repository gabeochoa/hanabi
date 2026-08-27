#include <cstdio>
#include <string>
#include <vector>

#include "../../src/util/spotlight_catalog.h"

static int failures = 0;
#define CHECK(value)                                             \
    do {                                                         \
        if (!(value)) {                                          \
            std::printf("FAIL: %s line %d\n", #value, __LINE__); \
            ++failures;                                          \
        }                                                        \
    } while (0)

static api::SessionSummary session(std::string id, std::string title,
                                   std::string preview, int64_t updated) {
    api::SessionSummary value;
    value.id = std::move(id);
    value.title = std::move(title);
    value.preview = std::move(preview);
    value.updated_at = updated;
    return value;
}

static void test_recent_first_and_bounded() {
    std::vector<api::SessionSummary> sessions;
    for (std::size_t i = 0; i < hanabi::spotlight::kMaxCatalogItems + 4; ++i)
        sessions.push_back(session("id-" + std::to_string(i), "title",
                                   "preview", static_cast<int64_t>(i)));
    const auto items = hanabi::spotlight::make_catalog(sessions);
    CHECK(items.size() == hanabi::spotlight::kMaxCatalogItems);
    CHECK(items.front().id == "id-2003");
    CHECK(items.back().id == "id-4");
}

static void test_metadata_and_url() {
    const auto items = hanabi::spotlight::make_catalog(
        {session("space/id ?", "", "offline preview", 4)});
    CHECK(items.size() == 1);
    CHECK(items[0].title == "Untitled thread");
    CHECK(items[0].preview == "offline preview");
    CHECK(items[0].url == "hanabi://thread/space%2Fid%20%3F");
}

static void test_empty_and_duplicate_ids_are_removed() {
    const auto items = hanabi::spotlight::make_catalog(
        {session("", "empty", "", 9), session("same", "new", "", 8),
         session("same", "old", "", 1)});
    CHECK(items.size() == 1);
    CHECK(items[0].title == "new");
}

static void test_utf8_truncation_and_update_signature() {
    std::string long_preview(599, 'a');
    long_preview += "\xe2\x9c\x93";
    long_preview += "tail";
    const auto a = hanabi::spotlight::make_catalog(
        {session("id", "title", long_preview, 1)});
    CHECK(a[0].preview.size() <= hanabi::spotlight::kMaxPreviewBytes);
    CHECK(a[0].preview.back() == 'a');
    auto b = a;
    b[0].preview = "changed";
    CHECK(hanabi::spotlight::signature(a) != hanabi::spotlight::signature(b));
}

int main() {
    test_recent_first_and_bounded();
    test_metadata_and_url();
    test_empty_and_duplicate_ids_are_removed();
    test_utf8_truncation_and_update_signature();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
