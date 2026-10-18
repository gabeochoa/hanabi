#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../../src/api/mock_client.h"
#include "../../src/api/mock_scale.h"
#include "../../src/ecs/transcript_reconcile.h"
#include "../../src/ui/open_fetch.h"

namespace of = hanabi::model;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_open_fetch ===\n");
    CHECK(of::open_fetch_plan(false, false) == of::OpenFetch::Full);
    CHECK(of::open_fetch_plan(false, true) == of::OpenFetch::Full);
    CHECK(of::open_fetch_plan(true, false) == of::OpenFetch::Full);
    CHECK(of::open_fetch_plan(true, true) == of::OpenFetch::WaitForDisk);
    CHECK(!of::after_disk_read(false, 9).delta);
    CHECK(!of::after_disk_read(true, 0).delta);
    CHECK(of::after_disk_read(true, 9).delta && of::after_disk_read(true, 9).since == 9);

    setenv("HANABI_MOCK_SCALE", "3", 1);
    api::MockClient client;
    auto full = client.get_session("t2x1", 0);
    CHECK(full.ok && full.value.messages.size() == 9);
    api::Session cached = full.value;
    const std::uint64_t cachedNewest = ecs::model::newest_seq(cached.messages);
    CHECK(cachedNewest == 9);

    cached.messages.pop_back();
    cached.messages.pop_back();
    const std::uint64_t since = ecs::model::newest_seq(cached.messages);
    CHECK(since == 7);
    auto delta = client.get_session_since("t2x1", since, 50);
    CHECK(delta.ok && delta.value.messages.size() == 3 && delta.value.messages.front().id == "7");
    const auto out = ecs::model::reconcile_transcript(cached.messages, std::move(delta.value.messages));
    CHECK(cached.messages.size() == 9 && cached.messages.back().id == "9");
    CHECK(cached.messages.front().id == "1");
    bool ordered = true;
    for (std::size_t i = 0; i < cached.messages.size(); ++i)
        if (cached.messages[i].id != std::to_string(i + 1)) ordered = false;
    CHECK(ordered);

    api::Session base = client.get_session("t2", 0).value;
    CHECK(ecs::model::newest_seq(base.messages) == 0);
    CHECK(!of::after_disk_read(true, ecs::model::newest_seq(base.messages)).delta);
    (void)out;
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
