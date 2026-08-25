// tools/bench_startup_data.cpp — CPU-time benchmark for the once-per-launch and
// once-per-keystroke costs: the mock fixture builder, the settings overlays,
// and the settings write path.
//
// WHY CPU TIME, NOT WALL CLOCK. This box runs several agents' builds at once;
// load average hit 29 while this was written, and an A/B under that load came
// out with the FASTER binary reading 50% slower. CLOCK_THREAD_CPUTIME_ID counts
// only cycles this thread was actually given, so a neighbouring build steals
// throughput but not the measurement. Allocation COUNTS are reported next to
// it because they are load-invariant entirely: a count that drops from 1.4M to
// 700 is a result no scheduler can argue with.
//
// Build (from the repo root):
//   clang++ -std=c++23 -O2 -isystem vendor/ -I. \
//       tools/bench_startup_data.cpp src/settings.cpp -o output/bench_sd \
//       -fobjc-arc -framework Foundation
// Run: output/bench_sd [sessions] [starred]

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <malloc/malloc.h>
#include <string>
#include <vector>

#include <afterhours/src/plugins/files.h>

#include "src/api/mock_client.h"
#include "src/search/session_index.h"
#include "src/settings.h"

// ── measurement primitives ──────────────────────────────────────────────────

static double cpu_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

// Live malloc bytes for this process, via the default zone's statistics. Paired
// before/after a phase this is "what did that phase retain"; the allocation
// COUNT delta over a phase that frees everything is "how much churn".
static size_t live_bytes() {
    malloc_statistics_t st{};
    malloc_zone_statistics(malloc_default_zone(), &st);
    return st.size_in_use;
}
static size_t total_allocs() {
    malloc_statistics_t st{};
    malloc_zone_statistics(malloc_default_zone(), &st);
    return st.blocks_in_use;
}

struct Sample {
    double cpu_ms = 0;
    long long retained = 0;
};

template <typename Fn>
static Sample measure(int iters, Fn&& fn) {
    // One untimed warm pass so first-touch page faults and any lazy statics are
    // not attributed to the phase under test.
    fn();
    const size_t b0 = live_bytes();
    const double t0 = cpu_ms();
    for (int i = 0; i < iters; ++i) fn();
    const double t1 = cpu_ms();
    const size_t b1 = live_bytes();
    return Sample{(t1 - t0) / iters,
                  static_cast<long long>(b1) - static_cast<long long>(b0)};
}

static void row(const char* name, const Sample& s, const char* unit = "call") {
    std::printf("  %-42s %9.3f ms/%s   retained %+lld B\n", name, s.cpu_ms,
                unit, s.retained);
}

// ── the phases under test ───────────────────────────────────────────────────

int main(int argc, char** argv) {
    const int sessions = argc > 1 ? std::atoi(argv[1]) : 2000;
    const int starred = argc > 2 ? std::atoi(argv[2]) : 50;

    // An isolated settings file, and the files plugin initialised so
    // get_settings_path() resolves. WITHOUT this the path comes back empty,
    // the ofstream never opens, and write_save_file() measures the JSON build
    // with the I/O silently missing — a fake number that reads as a fast one.
    setenv("HOME", "/tmp/hanabi_bench_home", 1);
    system("mkdir -p '/tmp/hanabi_bench_home/Library/Application Support/hanabi'");
    afterhours::files::init("hanabi", "resources");

    // The fixture size is read from the environment by seed() itself, so set it
    // here rather than asking the caller to keep two numbers in step.
    setenv("HANABI_STRESS_SESSIONS", std::to_string(sessions).c_str(), 1);

    std::printf("bench_startup_data: %d synthetic sessions (+20 hand-written), "
                "%d starred\n",
                sessions, starred);
    std::printf("  clock: CLOCK_THREAD_CPUTIME_ID (load-invariant)\n\n");

    api::MockClient client;

    // --- 1. the fixture builder itself -------------------------------------
    std::printf("[1] mock fixture\n");
    {
        // seed() is private, so it is measured through the call site that does
        // nothing else: get_settings() builds the entire catalog and reads
        // .size() off it. That row IS the cost of one seed().
        auto lst = measure(3, [&] {
            auto r = client.list_sessions();
            asm volatile("" : : "r,m"(r.value.size()) : "memory");
        });
        row("list_sessions()", lst);

        // The id of the LAST synthetic row: the worst case for a linear scan,
        // and the honest one for "open a thread from the bottom of the list".
        const std::string lastId = "s" + std::to_string(sessions - 1);
        auto gs = measure(3, [&] {
            auto r = client.get_session(lastId);
            asm volatile("" : : "r,m"(r.ok) : "memory");
        });
        row("get_session(one id)", gs);

        auto st = measure(3, [&] {
            auto r = client.get_settings();
            asm volatile("" : : "r,m"(r.value.session_count) : "memory");
        });
        row("get_settings() — wants only a COUNT", st);
    }

    // --- 2. persistence -----------------------------------------------------
    std::printf("\n[2] persistence (settings)\n");
    {
        // An isolated settings file: never touch the developer's real one.
        // (HOME + files::init are set at the top of main.)
        Settings& cfg = Settings::get();
        cfg.auto_save_enabled = false;  // measure the pieces separately

        auto ids = std::vector<std::string>();
        ids.reserve(static_cast<size_t>(sessions));
        for (int i = 0; i < sessions; ++i)
            ids.push_back("s" + std::to_string(i));

        // Star a spread across the catalog so the linear scan's average hit
        // position is realistic (not all at the front, not all absent).
        for (int i = 0; i < starred && i < sessions; ++i)
            cfg.set_starred(ids[static_cast<size_t>((i * 37) % sessions)], true);

        auto listRes = client.list_sessions();
        std::vector<api::SessionSummary> summaries = listRes.value;
        std::printf("  (catalog: %zu rows, starred set: %zu)\n",
                    summaries.size(), cfg.get_starred().size());

        auto ov = measure(20, [&] {
            for (auto& s : summaries) {
                if (cfg.is_starred(s.id)) s.starred = true;
                if (cfg.is_muted(s.id)) s.muted = true;
                s.archive_override = cfg.get_archived(s.id);
            }
        });
        row("apply_local_overlays(whole list)", ov, "fetch");

        auto wr = measure(20, [&] { cfg.write_save_file(); });
        row("write_save_file()", wr);

        auto tog = measure(20, [&] {
            cfg.auto_save_enabled = true;
            cfg.set_starred(ids[0], true);
            cfg.set_starred(ids[0], false);
            cfg.auto_save_enabled = false;
        });
        row("set_starred() x2 (star + unstar)", tog, "pair");
    }

    // --- 3. the search index -----------------------------------------------
    std::printf("\n[3] search index\n");
    {
        auto listRes = client.list_sessions();
        hanabi::search::Index ix;
        auto build = measure(3, [&] {
            hanabi::search::Index tmp;
            for (const auto& s : listRes.value) {
                hanabi::search::Doc d;
                d.id = s.id;
                d.title = s.title;
                d.preview = s.preview;
                tmp.add(std::move(d));
            }
            asm volatile("" : : "r,m"(tmp.size()) : "memory");
        });
        row("build_index() titles+previews only", build, "open");

        for (const auto& s : listRes.value) {
            hanabi::search::Doc d;
            d.id = s.id;
            d.title = s.title;
            d.preview = s.preview;
            ix.add(std::move(d));
        }
        auto q = measure(50, [&] {
            auto hits = ix.query("quota", 12);
            asm volatile("" : : "r,m"(hits.size()) : "memory");
        });
        row("index.query() — runs EVERY FRAME while open", q, "frame");
    }

    std::printf("\n  live blocks at exit: %zu (%zu KB)\n", total_allocs(),
                live_bytes() / 1024);
    return 0;
}
