#include <cstdio>
#include <string>

#include "../../src/api/spaces_wire.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace sp = api::spaces;
using json = nlohmann::json;

static json node(const char* id, const char* name, bool pinned, json rank,
                 const char* type = "PROJECT") {
    return json{{"node", {{"id", id}, {"name", name}, {"emoji", "\xf0\x9f\x90\xa6"},
                          {"project_type", type}, {"if_viewer_has_pinned", pinned},
                          {"viewer_rank", rank}}}};
}

static void test_parse_orders_and_drops_named_agents() {
    const json data{{"viewer_intern_user",
                     {{"metamate_projects",
                       {{"edges", json::array({node("3", "Zeta", false, nullptr),
                                               node("2", "Beta", false, 1),
                                               node("1", "Alpha", true, 9),
                                               node("4", "Agent", false, 0, "NAMED_AGENT"),
                                               node("x9", "Bad id", false, 0)})}}}}}};
    const auto list = sp::parse(data);
    CHECK(list.has_value());
    if (list) {
        CHECK(list->size() == 3);
        CHECK((*list)[0].id == "1");  // pinned first
        CHECK((*list)[1].id == "2");  // then ranked
        CHECK((*list)[2].id == "3");  // unranked last
    }
    CHECK(!sp::parse(json::object()).has_value());
    CHECK(sp::valid_id("1593993452358360") && !sp::valid_id("12a") && !sp::valid_id(""));
    CHECK(json::parse(sp::body())["query_text"].get<std::string>().find("metamate_projects") !=
          std::string::npos);
}

int main() {
    test_parse_orders_and_drops_named_agents();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
