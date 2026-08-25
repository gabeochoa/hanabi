// tools/bench_data_layer.cpp — CPU-time benchmark for the two data-layer costs
// that scale with something the user controls: the disk cache's directory scan
// (which runs after every transcript save) and the websocket stream's
// per-frame classification (which runs at token rate).
//
// Build (from the repo root):
//   clang++ -std=c++23 -O2 -DFMT_HEADER_ONLY -isystem vendor/ \
//       -isystem vendor/afterhours/vendor/ -I. tools/bench_data_layer.cpp \
//       src/api/disk_cache.cpp vendor/afterhours/src/plugins/files.cpp \
//       -o output/bench_dl -fobjc-arc -framework Foundation
// Run: output/bench_dl [cache_files] [stream_frames]

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <afterhours/src/plugins/files.h>
#include <nlohmann/json.hpp>

#include "src/api/disk_cache.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

static double cpu_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

template <typename Fn>
static double measure(int iters, Fn&& fn) {
    fn();  // warm
    const double t0 = cpu_ms();
    for (int i = 0; i < iters; ++i) fn();
    return (cpu_ms() - t0) / iters;
}

// A realistic live text-append frame: the shape that arrives once per token.
static json make_frame(int i) {
    return json{{"type", "frame"},
                {"sub", 1},
                {"event",
                 {{"type", "block_delta"},
                  {"key", "blk_" + std::to_string(i % 4)},
                  {"delta", {{"delta", "append"}, {"text", " token"}}}}}};
}

int main(int argc, char** argv) {
    const int files = argc > 1 ? std::atoi(argv[1]) : 2000;
    const int frames = argc > 2 ? std::atoi(argv[2]) : 5000;

    std::printf("bench_data_layer: %d cache files, %d stream frames\n", files,
                frames);
    std::printf("  clock: CLOCK_THREAD_CPUTIME_ID (load-invariant)\n\n");

    // ── 1. the disk cache's directory scan ──────────────────────────────────
    // trim_to_cap() calls total_bytes() FIRST, before it knows whether anything
    // needs evicting, and total_bytes() walks the whole cache directory with a
    // file_size() stat per entry. The default cap is 1 GiB, so on almost every
    // save the answer is "under cap, nothing to do" -- and the walk was pure
    // cost. This measures the walk at a realistic cache size.
    // disk_cache::cache_dir() honours HANABI_CACHE_DIR first, and that is the
    // ONLY reliable way to point it somewhere isolated -- the fallback is
    // $HOME/.config/hanabi/cache, NOT the macOS Application Support path, and
    // the first cut of this bench wrote its fixture files to the wrong
    // directory and measured an empty one. It reported 0.003 ms for a 2000-file
    // scan, which is 2000 stat() calls in three microseconds: impossible, and
    // exactly the kind of too-good number that gets believed.
    const std::string home = "/tmp/hanabi_bench_dl_home";
    fs::remove_all(home);
    const fs::path cdir = fs::path(home) / "cache";
    fs::create_directories(cdir);
    setenv("HANABI_CACHE_DIR", cdir.c_str(), 1);
    afterhours::files::init("hanabi", "resources");
    if (api::disk_cache::cache_dir() != cdir.string()) {
        std::fprintf(stderr,
                     "bench: cache_dir() is '%s', expected '%s' -- refusing to "
                     "report a number for a directory the fixture is not in\n",
                     api::disk_cache::cache_dir().c_str(), cdir.c_str());
        return 2;
    }

    // Each transcript is a plausible size: a summary plus a few dozen messages.
    json tx;
    tx["summary"] = {{"id", "x"}, {"title", "a cached thread"}, {"state", 0}};
    tx["messages"] = json::array();
    for (int m = 0; m < 40; ++m)
        tx["messages"].push_back(
            {{"id", "m" + std::to_string(m)},
             {"role", m % 2},
             {"text", "a line of transcript text that is about this long, "
                      "which is typical for a real turn in a conversation"}});
    const std::string txBody = tx.dump();
    for (int i = 0; i < files; ++i) {
        std::ofstream o(cdir / ("tx_bench" + std::to_string(i) + ".json"));
        o << txBody;
    }
    std::printf("[1] disk cache (%d files, %.1f MB on disk)\n", files,
                static_cast<double>(files * txBody.size()) / 1e6);

    const double tb = measure(5, [] {
        auto n = api::disk_cache::total_bytes();
        asm volatile("" : : "r,m"(n) : "memory");
    });
    std::printf("  %-46s %8.3f ms\n",
                "total_bytes() — runs on EVERY save", tb);

    // The real call: cap far above the cache, so this is the common
    // "nothing to evict" path and everything it costs is the scan.
    const double tc = measure(5, [] {
        auto n = api::disk_cache::trim_to_cap(1024ull * 1024 * 1024);
        asm volatile("" : : "r,m"(n) : "memory");
    });
    std::printf("  %-46s %8.3f ms\n",
                "trim_to_cap(1 GiB) — under cap, no-op", tc);

    // ── 2. the stream's per-frame classification ────────────────────────────
    // The websocket loop has already parsed each frame into a json object, then
    // calls classify_live_frame(msg.dump()) -- serialising the whole object
    // back to a string so the classifier can parse it a second time. Every
    // frame is parse -> dump -> parse. Frames arrive at token rate.
    std::printf("\n[2] websocket stream (%d frames)\n", frames);
    std::vector<json> msgs;
    msgs.reserve(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i) msgs.push_back(make_frame(i));

    const double roundTrip = measure(3, [&] {
        for (const auto& m : msgs) {
            const std::string s = m.dump();
            json again = json::parse(s, nullptr, false);
            asm volatile("" : : "r,m"(again.is_discarded()) : "memory");
        }
    });
    std::printf("  %-46s %8.3f ms  (%.1f us/frame)\n",
                "dump() + re-parse, per stream burst", roundTrip,
                roundTrip * 1000.0 / frames);

    const double direct = measure(3, [&] {
        for (const auto& m : msgs) {
            // What passing the already-parsed object costs instead: a lookup.
            const auto it = m.find("event");
            asm volatile("" : : "r,m"(it != m.end()) : "memory");
        }
    });
    std::printf("  %-46s %8.3f ms  (%.1f us/frame)\n",
                "use the object already parsed", direct,
                direct * 1000.0 / frames);
    std::printf("  ---> the round trip is %.0fx the direct read\n",
                direct > 0 ? roundTrip / direct : 0.0);

    fs::remove_all(home);
    return 0;
}
