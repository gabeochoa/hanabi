#include <cstdio>
#include <string>

#include "../../src/api/drafts_wire.h"

namespace dw = api::drafts;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_drafts_wire ===\n");
    CHECK(std::string(dw::kPath) == "/api/inbox/drafts");
    const auto page = dw::parse(
        R"({"drafts":[{"id":"d1","body":"hello\nworld","sessionId":null,"createdAt":"a","updatedAt":"b"},)"
        R"({"id":"d2","body":"second","sessionId":"t2"},{"id":"","body":"no id"},{"id":"d4","body":""}],"limit":50})");
    CHECK(page && page->limit == 50 && page->drafts.size() == 2);
    CHECK(page->drafts[0].id == "d1" && page->drafts[0].body == "hello\nworld" && page->drafts[0].session_id.empty() &&
          page->drafts[0].updated_at == "b");
    CHECK(page->drafts[1].session_id == "t2");
    CHECK(!dw::parse("not json"));
    CHECK(!dw::parse(R"({"error":"nope"})"));
    CHECK(dw::parse(R"({"drafts":[]})") && dw::parse(R"({"drafts":[]})")->drafts.empty());
    CHECK(dw::first_line("  hello\nworld") == "hello");
    CHECK(dw::first_line("one line") == "one line");
    CHECK(dw::first_line(std::string(100, 'x'), 10) == std::string(10, 'x') + "\xe2\x80\xa6");
    CHECK(dw::first_line("caf\xc3\xa9\xc3\xa9", 5) == "caf\xc3\xa9\xe2\x80\xa6");
    CHECK(dw::status_note(200).empty());
    CHECK(dw::status_note(401) == "the web app did not accept this Mac's sign-in");
    CHECK(dw::status_note(404) == "the web app has no saved drafts route");
    CHECK(dw::status_note(500) == "the web app answered 500");
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
