// Perf regression micro-benchmarks (headless, in-process, deterministic).
//
// (1) Thread-switch latency: times switching among a few threads through the
//     REAL code path. Two measurements:
//       * UNCACHED baseline: model tab switch + a MockClient get_session fetch
//         (what the loader ran before the cache). Regression guard only.
//       * CACHED switch (Phase X): model tab switch + a TranscriptCache HIT
//         (what the loader runs today for a recently-seen thread) — served
//         synchronously, no fetch. This is the instant-switch path; we assert
//         a STRICT sub-millisecond ceiling on it.
//
// Emits measured numbers to stdout so trends are visible in CI logs.

#include <chrono>
#include <cstdio>
#include <future>
#include <optional>
#include <string>
#include <unistd.h>
#include <vector>

#define AFTER_HOURS_ENTITY_HELPER
#define AFTER_HOURS_ENTITY_QUERY
#define AFTER_HOURS_SYSTEM
#include "../../vendor/afterhours/src/ecs.h"

#include "../../src/api/disk_cache.h"
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
        ecs::model::open_session_in_tab(strip, app, id);

    // Warm once.
    for (const auto& id : ids) {
        ecs::model::open_session_in_tab(strip, app, id);
        auto r = client.get_session(id);
        (void)r;
    }

    // Time N cycles of switching among the 5 threads through the current path:
    // model tab focus (sets requestOpenId) + the fetch LoaderSystem would run.
    constexpr int kCycles = 2000;
    auto t0 = std::chrono::high_resolution_clock::now();
    size_t sink = 0;
    for (int c = 0; c < kCycles; ++c) {
        for (const auto& id : ids) {
            ecs::model::open_session_in_tab(strip, app, id);  // focus existing
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
    // switching among them through the REAL cached path: model tab focus + a
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
            ecs::model::open_session_in_tab(strip, capp, id);  // focus existing
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

    // --- FEATURE #1: UI-thread cost of a MISS-path switch (async disk read) --
    // The beachball Gabe hit was the disk-cache read + JSON parse of a big
    // transcript running SYNCHRONOUSLY on the UI thread during a switch. We now
    // do that on a worker thread. This benchmark measures BOTH so the win is
    // visible in the log:
    //   * OLD (synchronous): the UI thread pays the full disk read + parse.
    //   * NEW (async dispatch): the UI thread only LAUNCHES std::async — the
    //     read+parse happen on the worker. We assert the UI-thread portion is
    //     well under 2ms (the perf gate), independent of transcript size.
    {
        // Build a big transcript and persist it to an isolated /tmp cache so
        // load_transcript() has a real, large file to open+parse.
        setenv("HANABI_CACHE_DIR",
               ("/tmp/hanabi_perf_cache_" + std::to_string(::getpid())).c_str(),
               1);
        api::disk_cache::set_namespace("");
        api::disk_cache::wipe_all();
        setenv("HANABI_BIG_TRANSCRIPT", "1", 1);
        api::MockClient big;
        auto bigTx = big.get_session("rbig");  // ~200 messages
        api::disk_cache::save_transcript(bigTx.value);
        const std::string bigId = bigTx.value.summary.id;

        constexpr int kIters = 200;

        // OLD path: synchronous disk read + parse on the (would-be) UI thread.
        auto o0 = std::chrono::high_resolution_clock::now();
        size_t osink = 0;
        for (int i = 0; i < kIters; ++i) {
            auto s = api::disk_cache::load_transcript(bigId);  // BLOCKS
            if (s) osink += s->messages.size();
        }
        auto o1 = std::chrono::high_resolution_clock::now();
        double oldPerSwitchMs =
            std::chrono::duration<double, std::milli>(o1 - o0).count() / kIters;

        // NEW path: the UI thread only LAUNCHES the async read (what the loader
        // now does on switch). We time ONLY the launch; the worker does the
        // read+parse. Keep the futures alive so the launch isn't optimized out.
        std::vector<std::future<std::optional<api::Session>>> futs;
        futs.reserve(kIters);
        auto n0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < kIters; ++i) {
            futs.push_back(std::async(std::launch::async, [bigId] {
                return api::disk_cache::load_transcript(bigId);
            }));
        }
        auto n1 = std::chrono::high_resolution_clock::now();
        double newPerSwitchMs =
            std::chrono::duration<double, std::milli>(n1 - n0).count() / kIters;
        size_t nsink = 0;
        for (auto& f : futs)
            if (auto s = f.get()) nsink += s->messages.size();

        std::printf("----------------------------------------\n");
        std::printf("=== FEATURE #1: switch UI-thread cost (big transcript) ===\n");
        std::printf("  transcript messages: %zu\n", bigTx.value.messages.size());
        std::printf("  OLD (sync disk read+parse on UI): %.4f ms/switch\n",
                    oldPerSwitchMs);
        std::printf("  NEW (async dispatch on UI):       %.4f ms/switch\n",
                    newPerSwitchMs);
        std::printf("  (osink=%zu nsink=%zu)\n", osink, nsink);
        // The UI-thread portion of the NEW path must be well under the 1-2ms
        // budget regardless of transcript size (the heavy work is on a worker).
        CHECK(newPerSwitchMs < 2.0);
        api::disk_cache::wipe_all();
        unsetenv("HANABI_BIG_TRANSCRIPT");
        unsetenv("HANABI_CACHE_DIR");
    }

    // ---------------------------------------------------------------------
    // (3) Component lookup: dynamic_cast vs the type-id bitset.
    //
    // A profile of an idle frame put ~16% of the main thread in C++ runtime
    // type machinery, with strcmp the hottest non-font function. The chain is
    // System::for_each_derived -> HasAllComponents -> Entity::has_child_of<T>
    // -> child_of<T> -> dynamic_cast -> type_info::operator== -> strcmp: on
    // Apple's libc++abi, comparing two type_infos from the same image can fall
    // through to comparing their mangled NAMES.
    //
    // has_child_of walks the whole componentArray (max_num_components slots)
    // and dynamic_casts each one, so the cost is per (entity x system x
    // required component), every frame. Entity::has<T>() answers the same
    // question through a bitset and never appears in the profile at all.
    //
    // This times the two against each other on a realistic entity, so the gap
    // has a number attached rather than a percentage of a flame graph.
    // Reported for afterhours (gap #43); not gated, since it measures the
    // library rather than hanabi.
    {
        struct BenchA : afterhours::BaseComponent {};
        struct BenchB : afterhours::BaseComponent {};
        struct BenchC : afterhours::BaseComponent {};
        struct Absent : afterhours::BaseComponent {};

        auto& e = afterhours::EntityHelper::createEntity();
        e.addComponent<BenchA>();
        e.addComponent<BenchB>();
        e.addComponent<BenchC>();

        constexpr int kIters = 200000;
        volatile size_t sink = 0;

        // The path the system runner actually takes.
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < kIters; ++i) {
            sink += e.has_child_of<BenchA>() ? 1u : 0u;
            sink += e.has_child_of<Absent>() ? 1u : 0u;
        }
        auto t1 = std::chrono::high_resolution_clock::now();

        // The same question, asked of the bitset.
        for (int i = 0; i < kIters; ++i) {
            sink += e.has<BenchA>() ? 1u : 0u;
            sink += e.has<Absent>() ? 1u : 0u;
        }
        auto t2 = std::chrono::high_resolution_clock::now();

        const double castNs =
            std::chrono::duration<double, std::nano>(t1 - t0).count() /
            (kIters * 2.0);
        const double bitsNs =
            std::chrono::duration<double, std::nano>(t2 - t1).count() /
            (kIters * 2.0);

        std::printf("----------------------------------------\n");
        std::printf("=== afterhours #43: component lookup ===\n");
        std::printf("  has_child_of<T> (dynamic_cast): %8.1f ns/call\n", castNs);
        std::printf("  has<T>          (bitset):       %8.1f ns/call\n", bitsNs);
        if (bitsNs > 0.0)
            std::printf("  ratio:                          %8.0fx\n",
                        castNs / bitsNs);
        std::printf("  (a present component and an absent one, averaged)\n");
        std::printf("  (sink=%zu)\n", static_cast<size_t>(sink));
    }

    std::printf("----------------------------------------\n");
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
