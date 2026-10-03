// The background transcript writer (api/disk_cache.h): coalesced, ordered per
// thread, read-through, clear-safe, flushed.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

#include "../../src/api/disk_cache.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace dc = api::disk_cache;
namespace fs = std::filesystem;

static api::Session make(const std::string& id, const std::string& text) {
    api::Session s;
    s.summary.id = id;
    s.summary.title = "t " + id;
    s.messages.push_back(api::Message("1", api::Role::User, text, 1, ""));
    return s;
}

static std::string text_of(const std::optional<api::Session>& s) {
    return s && !s->messages.empty() ? s->messages.back().text : std::string("<none>");
}

int main() {
    char tmpl[] = "/tmp/hanabi_writer_XXXXXX";
    const char* dir = mkdtemp(tmpl);
    setenv("HANABI_CACHE_DIR", dir, 1);

    // Coalesced: three snapshots of one thread before its write starts are
    // written once, the newest.
    dc::transcript_writer_pause(true);
    dc::save_transcript_async(make("a", "v1"), 0);
    dc::save_transcript_async(make("a", "v2"), 0);
    dc::save_transcript_async(make("b", "b1"), 0);
    dc::save_transcript_async(make("a", "v3"), 0);
    // Read-through: the newest snapshot answers before any file exists.
    CHECK(text_of(dc::load_transcript("a")) == "v3");
    CHECK(text_of(dc::load_transcript("b")) == "b1");
    dc::transcript_writer_pause(false);
    CHECK(dc::flush_transcript_writes());
    auto st = dc::transcript_writer_stats();
    CHECK(st.enqueued == 4 && st.coalesced == 2 && st.written == 2);
    CHECK(text_of(dc::load_transcript("a")) == "v3");  // now from the file
    CHECK(text_of(dc::load_transcript("b")) == "b1");

    // Ordered per thread: a newer snapshot queued after an older one was
    // written lands last.
    dc::save_transcript_async(make("a", "v4"), 0);
    CHECK(dc::flush_transcript_writes());
    dc::save_transcript_async(make("a", "v5"), 0);
    CHECK(dc::flush_transcript_writes());
    CHECK(text_of(dc::load_transcript("a")) == "v5");

    // Clear-safe: a queued write does not resurrect a cleared cache.
    dc::transcript_writer_pause(true);
    dc::save_transcript_async(make("c", "c1"), 0);
    dc::wipe_all_report();
    dc::transcript_writer_pause(false);
    CHECK(dc::flush_transcript_writes());
    CHECK(!dc::load_transcript("c").has_value());
    CHECK(!dc::load_transcript("a").has_value());
    CHECK(dc::transcript_writer_stats().discarded >= 1);

    // Bounded flush: a held writer times out instead of hanging.
    dc::transcript_writer_pause(true);
    dc::save_transcript_async(make("d", "d1"), 0);
    CHECK(!dc::flush_transcript_writes(std::chrono::milliseconds(50)));
    dc::transcript_writer_pause(false);
    CHECK(dc::flush_transcript_writes());
    CHECK(text_of(dc::load_transcript("d")) == "d1");

    std::error_code ec;
    fs::remove_all(dir, ec);
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    return 1;
}
