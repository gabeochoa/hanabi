#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>
#include <afterhours/src/plugins/files.h>
#include "src/api/disk_cache.h"
#include "src/search/session_corpus.h"

namespace fs = std::filesystem;
static double cpu_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}
static std::string body_line(int k) {
    return "Here's the breakdown for step " + std::to_string(k) +
           ":\n1. Pulled the trace and diffed it against the baseline.\n"
           "2. The hot path is handle_request calling into "
           "parser_cache.entries on every event.\n"
           "3. Under load that's ~40k calls/sec, each allocating.\n"
           "4. The fix caps the cache and hashes the key.";
}
int main(int argc, char** argv) {
    const int threads = argc > 1 ? std::atoi(argv[1]) : 400;
    const int msgs = argc > 2 ? std::atoi(argv[2]) : 40;
    std::printf("bench_search_index: %d threads x %d messages\n", threads,
                msgs);
    std::printf("  clock: CLOCK_THREAD_CPUTIME_ID (load-invariant)\n\n");

    const std::string dir = "/tmp/hanabi_bench_si_cache";
    fs::remove_all(dir);
    setenv("HANABI_CACHE_DIR", dir.c_str(), 1);
    api::disk_cache::set_namespace("");
    std::vector<std::string> ids;
    ids.reserve(static_cast<std::size_t>(threads));
    for (int t = 0; t < threads; ++t) {
        api::Session s;
        s.summary.id = "th" + std::to_string(t);
        s.summary.title = "thread " + std::to_string(t);
        s.summary.preview = "the last thing said in it";
        for (int m = 0; m < msgs; ++m) {
            api::Message x;
            x.id = "m" + std::to_string(m);
            x.role = (m % 2) ? api::Role::Assistant : api::Role::User;
            x.text = (m % 2) ? body_line(m)
                             : "Follow-up question #" + std::to_string(m);
            s.messages.push_back(std::move(x));
        }
        api::disk_cache::save_transcript(s);
        ids.push_back(s.summary.id);
    }
    std::printf("  cache on disk: %llu bytes\n\n",
                static_cast<unsigned long long>(api::disk_cache::total_bytes()));
    {
        const double t0 = cpu_ms();
        std::size_t bytes = 0;
        for (const auto& id : ids)
            if (auto s = api::disk_cache::load_transcript(id))
                for (const auto& m : s->messages) bytes += m.text.size();
        const double dt = cpu_ms() - t0;
        std::printf("  whole cache, one frame   %8.3f ms  (%.4f ms/thread)\n",
                    dt, dt / threads);
        std::printf("    text indexed           %8zu bytes\n\n", bytes);
    }
    std::vector<hanabi::search::Row> rows;
    rows.reserve(ids.size());
    for (int t = 0; t < threads; ++t) {
        hanabi::search::Row r;
        r.id = ids[static_cast<std::size_t>(t)];
        r.title = "thread " + std::to_string(t);
        r.preview = "the last thing said in it";
        r.updated_at = t;
        rows.push_back(std::move(r));
    }
    const auto load = [](const std::string& id)
        -> std::optional<hanabi::search::Loaded> {
        auto s = api::disk_cache::load_transcript(id);
        if (!s) return std::nullopt;
        hanabi::search::Loaded out;
        for (const auto& m : s->messages) {
            if (m.role != api::Role::User && m.role != api::Role::Assistant)
                continue;
            out.body += m.text;
            out.body.push_back('\n');
        }
        out.windowed = s->has_more_older;
        return out;
    };
    hanabi::search::CorpusBuilder b;
    const double t0 = cpu_ms();
    b.begin(std::move(rows));
    const double tOpen = cpu_ms() - t0;
    const double t1 = cpu_ms();
    const std::size_t read =
        b.deepen(hanabi::search::kDeepenPerFrame, load);
    const double tSlice = cpu_ms() - t1;
    std::printf("  open the panel           %8.3f ms  (0 disk reads)\n", tOpen);
    std::printf("  one deepening slice      %8.3f ms  (%zu disk reads)\n",
                tSlice, read);
    int frames = 1;
    const double t2 = cpu_ms();
    while (!b.complete()) {
        b.deepen(hanabi::search::kDeepenPerFrame, load);
        ++frames;
    }
    std::printf("  to full coverage         %8.3f ms over %d frames\n\n",
                cpu_ms() - t2 + tSlice, frames);

    {
        const double t0 = cpu_ms();
        int hits = 0;
        for (const auto& id : ids)
            if (api::disk_cache::content_matches(id, "parser_cache")) ++hits;
        const double dt = cpu_ms() - t0;
        std::printf("  sidebar filter, cold     %8.3f ms  (%.4f ms/thread, "
                    "%d hits)\n", dt, dt / threads, hits);
        const double t1 = cpu_ms();
        for (const auto& id : ids) api::disk_cache::content_matches(id, "parser_cache");
        std::printf("  sidebar filter, memoized %8.3f ms\n\n", cpu_ms() - t1);
    }
    fs::remove_all(dir);
    return 0;
}