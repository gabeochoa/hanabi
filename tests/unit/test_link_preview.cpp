#include <cstdio>
#include <string>

#include "../../src/api/link_preview.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace lp = api::link_preview;
using json = nlohmann::json;

static std::vector<std::string> ids(const std::string& text) {
    std::vector<std::string> out;
    for (const auto& r : lp::refs_in(text)) out.push_back(r.id);
    return out;
}

static void test_refs() {
    CHECK(ids("Landed D117423523; see T275363905 and D117423523 again.") ==
          (std::vector<std::string>{"D117423523", "T275363905"}));
    CHECK(ids("Filed kt-lgsh in agentcloud/puffin; it supersedes kt-7kzn.") ==
          (std::vector<std::string>{"kt-lgsh", "kt-7kzn"}));
    CHECK(ids("plain prose with a D and a T").empty());
    CHECK(ids("D123 is too short, T12 too").empty());
    CHECK(ids("D12345678901 is too long").empty());
    CHECK(ids("[D1234567](https://x) and /diff/D1234567").empty());
    CHECK(ids("AD1234567 or D1234567x").empty());
    CHECK(ids("https://knots.internalmeta.com/agentcloud/puffin/issues/kt-lgsh").empty());
    CHECK(ids("markt-abcd kt-abcde kt-ABCD kt-ab kt-ab-c").empty());
    CHECK(ids("(kt-ab12).") == (std::vector<std::string>{"kt-ab12"}));
    CHECK(lp::refs_in("D1234567")[0].kind == lp::Kind::Diff);
    CHECK(lp::refs_in("T1234567")[0].kind == lp::Kind::Task);
    CHECK(!lp::may_hold_reference("Nothing Doing Today") && lp::may_hold_reference("see kt-ab12"));
}

static json field_for(const json& data) {
    return json{{"success", true}, {"data", data.dump()}, {"error", nullptr}};
}

static void test_decode() {
    const lp::Ref d{lp::Kind::Diff, "D117423523"};
    const json body{{"success", true},
                    {"link_type", "diff"},
                    {"ttl_seconds", 3600},
                    {"common_data",
                     {{"title", "[puffin] Cmd+click opens the web"},
                      {"url", "https://www.internalfb.com/diff/D117423523"},
                      {"authorName", "Gabe Ochoa"},
                      {"creationTime", 1790900000}}},
                    {"type_data", {{"status", "NEEDS_REVIEW"}}}};
    const auto c = lp::decode(field_for(body), d);
    CHECK(c && c->title == "[puffin] Cmd+click opens the web" && c->owner == "Gabe Ochoa");
    CHECK(c && lp::status_label(*c) == "Needs Review" && lp::status_tone(*c) == lp::Tone::Active);
    CHECK(lp::ttl_seconds(field_for(body), 1) == 3600);
    // A preview of another kind, a refusal, no title, garbage: no card.
    json other = body;
    other["link_type"] = "task";
    CHECK(!lp::decode(field_for(other), d));
    CHECK(!lp::decode(json{{"success", false}, {"data", nullptr}}, d));
    json untitled = body;
    untitled["common_data"]["title"] = "";
    CHECK(!lp::decode(field_for(untitled), d));
    CHECK(!lp::decode(json{{"success", true}, {"data", "not json"}}, d));
    // The request: the URL is built here, the command is fixed.
    const json req = json::parse(lp::request_body(d));
    CHECK(req["query_text"].get<std::string>().find("getLinkPreview") != std::string::npos);
    CHECK(json::parse(json::parse(req["variables"].get<std::string>())["params"].get<std::string>())["url"] ==
          "https://www.internalfb.com/diff/D117423523");
    CHECK((lp::Ref{lp::Kind::Task, "T275363905"}).url() ==
          "https://www.internalfb.com/intern/tasks/?t=275363905");
}

static void test_knot() {
    const lp::Ref k{lp::Kind::Knot, "kt-lgsh"};
    const std::string locate =
        R"({"id":"kt-lgsh","namespace":"agentcloud/puffin","url":"https://knots.internalmeta.com/agentcloud/puffin/issues/kt-lgsh"})";
    const std::string issue =
        R"({"id":"kt-lgsh","title":"Render knots as cards in the transcript","status":"in_progress","priority":"1","type":"feature"})";
    const auto c = lp::decode_knot(locate, issue, k);
    CHECK(c && c->title == "Render knots as cards in the transcript");
    CHECK(c && c->url == "https://knots.internalmeta.com/agentcloud/puffin/issues/kt-lgsh");
    CHECK(c && c->ns == "agentcloud/puffin" && c->issue_type == "feature");
    CHECK(c && lp::status_label(*c) == "In Progress" && lp::status_tone(*c) == lp::Tone::Active);
    CHECK(c && lp::priority_label(*c) == "P1" && lp::priority_tone(*c) == lp::Tone::Wrong);
    CHECK(lp::knot_namespace(locate) == "agentcloud/puffin");
    CHECK(!lp::decode_knot(locate, R"({"id":"kt-zzzz","title":"x"})", k));
    CHECK(!lp::decode_knot("{}", R"({"id":"kt-lgsh"})", k));
    CHECK(lp::knot_get_args(k, "agentcloud/puffin")[2] == "--namespace=agentcloud/puffin");
}

static void test_words() {
    CHECK(lp::diff_tone("LANDED") == lp::Tone::Settled && lp::diff_tone("NEEDS_REVISION") == lp::Tone::Wrong);
    CHECK(lp::task_status("no-progress") == "Open" && lp::task_priority("hi-pri") == "High");
    CHECK(lp::task_priority("unbreak-now") == "UBN!" && lp::task_priority("none").empty());
    CHECK(lp::knot_priority("7").empty() && lp::knot_priority("p0") == "P0");
    CHECK(lp::age(1000, 1000 + 3 * 86400 + 5) == "3d ago" && lp::age(0, 5).empty());
    lp::Card c;
    c.kind = lp::Kind::Knot;
    c.ns = "agentcloud/puffin";
    c.issue_type = "feature";
    CHECK(lp::footer(c, 0) == "agentcloud/puffin \xc2\xb7 feature");
    lp::Card t = c;
    t.kind = lp::Kind::Task;
    CHECK(lp::count_label({c, c}) == "2 knots" && lp::count_label({c, t}) == "2 references" &&
          lp::count_label({t}) == "1 task");
}

static void test_cache() {
    lp::Card good;
    good.id = "D1234";
    good.title = "x";
    const lp::Entry first = lp::merged(std::nullopt, good, 100, 3600);
    CHECK(first.card && first.fresh(100 + 3599) && !first.fresh(100 + 3600));
    const lp::Entry blip = lp::merged(first, std::nullopt, 5000, 3600);
    CHECK(blip.card && blip.ttl == lp::kFailureTtl);  // keeps the card, short clock
    const lp::Entry refused = lp::merged(std::nullopt, std::nullopt, 5000, 3600);
    CHECK(!refused.card && refused.ttl == lp::kFailureTtl);
}

int main() {
    test_refs();
    test_decode();
    test_knot();
    test_words();
    test_cache();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    return 1;
}
