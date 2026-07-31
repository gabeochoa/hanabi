// Perf regression micro-benchmarks (headless, in-process, deterministic).
//
// (1) Thread-switch latency: times switching among a few threads through the
//     REAL code path that runs today — the tabflow switch + a MockClient
//     get_session fetch (this is what LoaderSystem does on every open, since
//     there is NO transcript cache yet). We MEASURE and print the number and
//     assert a GENEROUS current-path ceiling. The strict sub-millisecond
//     cached-switch assertion is PENDING Phase X (transcript LRU cache) — see
//     docs/phased-plan.md; we don't fake it.
//
// Emits measured numbers to stdout so trends are visible in CI logs.

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#define AFTER_HOURS_ENTITY_HELPER
#define AFTER_HOURS_ENTITY_QUERY
#define AFTER_HOURS_SYSTEM
#include "../../vendor/afterhours/src/ecs.h"

#include "../../src/api/mock_client.h"
#include "../../src/ecs/components.h"
#include "../../src/ecs/tab_model.h"

// Generous ceiling for the CURRENT (uncached) in-process switch path. This is
// a regression guard, not the Phase-X target. When the LRU cache lands, add a
// separate sub-millisecond assertion for cache HITS and tighten this.
static constexpr double kSwitchCeilingMs = 5.0;

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_perf (thread-switch latency) ===\n");

    // Build a minimal headless world: app + strip, seeded from the mock.
    auto& appE = afterhours::EntityHelper::createEntity();
    auto& app = appE.addComponent<ecs::AppComponent>();
    api::MockClient client;
    app.sessions = client.list_sessions().value;
    auto& stripE = afterhours::EntityHelper::createEntity();
    stripE.addComponent<ecs::TabStripComponent>();
    auto stripQ = afterhours::EntityQuery({.force_merge = true})
                      .whereHasComponent<ecs::TabStripComponent>()
                      .gen();
    auto& strip = stripQ[0].get().get<ecs::TabStripComponent>();

    // Open 5 threads (the "recently-interacted" set the Phase-X cache targets).
    const std::vector<std::string> ids = {"t1", "t4", "t5", "t3", "t11"};
    for (const auto& id : ids)
        ecs::tabflow::open_session_in_tab(strip, app, id);

    // Warm once.
    for (const auto& id : ids) {
        ecs::tabflow::open_session_in_tab(strip, app, id);
        auto r = client.get_session(id);
        (void)r;
    }

    // Time N cycles of switching among the 5 threads through the current path:
    // tabflow focus (sets requestOpenId) + the fetch LoaderSystem would run.
    constexpr int kCycles = 2000;
    auto t0 = std::chrono::high_resolution_clock::now();
    size_t sink = 0;
    for (int c = 0; c < kCycles; ++c) {
        for (const auto& id : ids) {
            ecs::tabflow::open_session_in_tab(strip, app, id);  // focus existing
            auto r = client.get_session(id);  // uncached fetch (today's path)
            sink += r.value.messages.size();
        }
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double totalMs =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    int totalSwitches = kCycles * static_cast<int>(ids.size());
    double perSwitchMs = totalMs / totalSwitches;

    std::printf("  switches:        %d\n", totalSwitches);
    std::printf("  total:           %.2f ms\n", totalMs);
    std::printf("  per-switch (avg): %.4f ms  (current UNCACHED path)\n",
                perSwitchMs);
    std::printf("  (sink=%zu, prevents dead-code elimination)\n", sink);
    std::printf("  ceiling (regression guard): %.1f ms/switch\n",
                kSwitchCeilingMs);
    std::printf(
        "  PENDING Phase X: sub-millisecond CACHED-switch assertion once the "
        "transcript LRU cache lands (docs/phased-plan.md). Baseline recorded "
        "above.\n");

    CHECK(perSwitchMs < kSwitchCeilingMs);

    std::printf("----------------------------------------\n");
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
