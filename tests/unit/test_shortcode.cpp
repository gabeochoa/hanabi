#include <cstdio>
#include <string>

#include "../../src/api/shortcode.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace sc = api::shortcode;
using nlohmann::json;

int main() {
    CHECK(sc::normalized("ACS1042") == "ACS1042");
    CHECK(sc::normalized("acs1042") == "ACS1042");
    CHECK(sc::normalized(" ACS1042 ") == "ACS1042");
    CHECK(sc::normalized("ACS1234567890123456789") == "ACS1234567890123456789");
    CHECK(!sc::normalized("ACS12345678901234567890"));  // twenty digits
    CHECK(!sc::normalized("ACS"));
    CHECK(!sc::normalized("ACS10x2"));
    CHECK(!sc::normalized("D1042"));
    CHECK(!sc::normalized("7bd3c487-ba80-4618-8eee-1f4c2221852b"));
    const json b = json::parse(sc::body("ACS1042"));
    CHECK(b["query_text"].get<std::string>().find("xfb_agentcloud_catalog_session_from_reference(reference: $reference)") !=
          std::string::npos);
    CHECK(json::parse(b["variables"].get<std::string>())["reference"] == "ACS1042");
    CHECK(sc::session_id(json{{"xfb_agentcloud_catalog_session_from_reference", {{"session_id", "abc"}}}}) == "abc");
    CHECK(sc::session_id(json{{"xfb_agentcloud_catalog_session_from_reference", nullptr}}).empty());
    CHECK(sc::session_id(json()).empty());
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    return 1;
}
