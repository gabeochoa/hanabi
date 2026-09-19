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
    fn();
    const double t0 = cpu_ms();
    for (int i = 0; i < iters; ++i) fn();
    return (cpu_ms() - t0) / iters;
}
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
    const double tc = measure(5, [] {
        auto n = api::disk_cache::trim_to_cap(1024ull * 1024 * 1024);
        asm volatile("" : : "r,m"(n) : "memory");
    });
    std::printf("  %-46s %8.3f ms\n",
                "trim_to_cap(1 GiB) — under cap, no-op", tc);

    {
        const std::uint64_t onDisk = api::disk_cache::total_bytes();
        const std::uint64_t tinyCap = onDisk / 4;
        const std::uint64_t freed = api::disk_cache::trim_to_cap(tinyCap);
        const std::uint64_t nowOnDisk = api::disk_cache::total_bytes();
        std::printf("  cap enforcement: %llu B on disk, cap %llu B -> freed "
                    "%llu B, now %llu B  [%s]\n",
                    (unsigned long long)onDisk, (unsigned long long)tinyCap,
                    (unsigned long long)freed,
                    (unsigned long long)nowOnDisk,
                    freed > 0 ? "evicted" : "DID NOT EVICT -- BUG");
        if (freed == 0) return 3;
    }

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