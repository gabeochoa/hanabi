// Perf regression micro-benchmarks (headless, in-process, deterministic).
//
// (1) Thread-switch latency: times switching among a few threads through the
//     REAL code path. Two measurements:
//       * UNCACHED baseline: tabflow switch + a MockClient get_session fetch
//         (what the loader ran before the cache). Regression guard only.
//       * CACHED switch (Phase X): tabflow switch + a TranscriptCache HIT
//         (what the loader runs today for a recently-seen thread) — served
//         synchronously, no fetch. This is the instant-switch path; we assert
//         a STRICT sub-millisecond ceiling on it.
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
#include "../../src/ecs/transcript_cache.h"

// Generous ceiling for the UNCACHED switch path (regression guard, not a
// target). The Phase-X cache HIT path has its own strict sub-ms assertion.
static constexpr double kSwitchCeilingMs = 5.0;
// Phase X: a cache HIT must be well under a millisecond in-process.
static constexpr double kCachedSwitchCeilingMs = 1.0;

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
    std::printf("  per-switch (avg): %.4f ms  (UNCACHED baseline path)\n",
                perSwitchMs);
    std::printf("  (sink=%zu, prevents dead-code elimination)\n", sink);
    std::printf("  ceiling (regression guard): %.1f ms/switch\n",
                kSwitchCeilingMs);

    CHECK(perSwitchMs < kSwitchCeilingMs);

    // --- Phase X: CACHED switch path (transcript LRU cache HIT) ---------------
    // Prime the cache with the 5 recently-interacted threads, then time
    // switching among them through the REAL cached path: tabflow focus + a
    // TranscriptCache HIT (served synchronously, no get_session fetch). This is
    // what the loader runs today for a recently-seen thread.
    ecs::AppComponent& capp = app;
    for (const auto& id : ids) {
        auto r = client.get_session(id);
        capp.transcriptCache.put(r.value);  // cap to 20, mark MRU
    }

    auto c0 = std::chrono::high_resolution_clock::now();
    size_t csink = 0;
    for (int c = 0; c < kCycles; ++c) {
        for (const auto& id : ids) {
            ecs::tabflow::open_session_in_tab(strip, capp, id);  // focus existing
            auto hit = capp.transcriptCache.get(id);  // cache HIT (no fetch)
            if (hit) csink += hit->messages.size();
        }
    }
    auto c1 = std::chrono::high_resolution_clock::now();
    double cachedTotalMs =
        std::chrono::duration<double, std::milli>(c1 - c0).count();
    double cachedPerSwitchMs = cachedTotalMs / totalSwitches;

    std::printf("  per-switch (avg): %.4f ms  (CACHED path, Phase X)\n",
                cachedPerSwitchMs);
    std::printf("  (csink=%zu)\n", csink);
    std::printf("  ceiling (cached, strict): %.1f ms/switch\n",
                kCachedSwitchCeilingMs);

    CHECK(cachedPerSwitchMs < kCachedSwitchCeilingMs);

    std::printf("----------------------------------------\n");
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
