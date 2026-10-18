#include <cstdio>
#include <string>
#include <vector>

#include "../../src/api/mock_scale.h"

namespace ms = api::mock_scale;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

static api::Session seed(const std::string& id, int messages) {
    api::Session s;
    s.summary.id = id;
    s.summary.title = "thread " + id;
    s.summary.updated_at = 1'000'000;
    s.summary.starred = true;
    for (int i = 0; i < messages; ++i) {
        api::Message m{"m" + std::to_string(i), i % 3 == 2 ? api::Role::Tool : api::Role::User,
                       "text " + std::to_string(i), 900'000 + i, ""};
        if (i % 4 == 1) {
            api::Attachment a;
            a.name = "shot.png";
            a.file_id = "f" + std::to_string(i);
            m.attachments.push_back(a);
        }
        s.messages.push_back(std::move(m));
    }
    return s;
}

int main() {
    std::printf("=== test_mock_scale ===\n");
    CHECK(ms::parse(nullptr) == 1 && ms::parse("") == 1 && ms::parse("0") == 1 && ms::parse("-3") == 1);
    CHECK(ms::parse("10") == 10 && ms::parse("100") == 100 && ms::parse("5000") == ms::kMax);
    CHECK(ms::clone_id("t2", 3) == "t2x3");

    std::vector<api::Session> v{seed("t1", 6), seed("t2", 4)};
    const auto before = ms::totals(v);
    CHECK(before.sessions == 2 && before.messages == 10 && before.tool_rows == 3 && before.attachments == 3);

    std::vector<api::Session> one = v;
    ms::apply(one, 1, 2'000'000);
    CHECK(one.size() == 2 && one[0].messages[0].id == "m0");

    ms::apply(v, 10, 2'000'000);
    const auto after = ms::totals(v);
    CHECK(after.sessions == 20);
    CHECK(after.messages == 10 + 9 * (6 * 10 + 4 * 10));
    CHECK(after.tool_rows == 3 + 9 * (2 * 10 + 1 * 10));
    CHECK(after.attachments == 3 + 9 * (2 * 10 + 1 * 10));
    CHECK(v[0].messages[0].id == "m0" && v[0].summary.starred);
    const api::Session& c = v[2];
    CHECK(c.summary.id == "t1x1" && !c.summary.starred && c.summary.title == "thread t1 \xc3\x97" "1");
    CHECK(c.messages.size() == 60 && c.messages.front().id == "1" && c.messages.back().id == "60");
    CHECK(c.summary.last_seq == 60);
    CHECK(c.messages.back().created_at == 2'000'000 && c.messages.front().created_at < c.messages.back().created_at);
    bool monotone = true;
    for (std::size_t i = 1; i < c.messages.size(); ++i)
        if (!(std::stoll(c.messages[i].id) == std::stoll(c.messages[i - 1].id) + 1 &&
              c.messages[i].created_at >= c.messages[i - 1].created_at))
            monotone = false;
    CHECK(monotone);
    CHECK(c.messages[1].attachments.size() == 1 && c.messages[1].attachments[0].file_id == "f1-1-0");
    CHECK(c.messages[7].attachments[0].file_id == "f1-1-1");
    CHECK(v[3].summary.id == "t2x1" && v[4].summary.id == "t1x2");
    CHECK(v[2].summary.updated_at < v[0].summary.updated_at && v[4].summary.updated_at < v[2].summary.updated_at);
    std::vector<api::Session> again{seed("t1", 6), seed("t2", 4)};
    ms::apply(again, 10, 2'000'000);
    CHECK(again[7].summary.id == v[7].summary.id && again[7].messages[13].id == v[7].messages[13].id &&
          again[7].messages[13].created_at == v[7].messages[13].created_at);
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
