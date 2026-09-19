#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unistd.h>
#include <vector>

#include "../../src/api/disk_cache.h"
#include "../../src/api/types.h"

using api::SessionSummary;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

static SessionSummary row(const char* id, std::optional<std::int64_t> event, std::optional<std::int64_t> run) {
    SessionSummary s;
    s.id = id;
    s.title = id;
    s.updated_at = 1781524800;
    s.last_event_unix_ms = event;
    s.last_run_complete_unix_ms = run;
    return s;
}

static const SessionSummary* find(const std::vector<SessionSummary>& rows, const char* id) {
    for (const SessionSummary& s : rows)
        if (s.id == id) return &s;
    return nullptr;
}

static std::filesystem::path sessions_file() {
    return std::filesystem::path(api::disk_cache::cache_dir()) / "sessions.json";
}

static void test_both_clocks_round_trip_and_absent_stays_absent() {
    api::disk_cache::save_sessions({row("both", 1781524800123, 1781524700000), row("event", 0, std::nullopt),
                                    row("run", std::nullopt, 5), row("none", std::nullopt, std::nullopt)});
    const auto loaded = api::disk_cache::load_sessions();
    CHECK(loaded.has_value() && loaded->size() == 4);
    if (!loaded) return;
    const SessionSummary* both = find(*loaded, "both");
    CHECK(both && both->last_event_unix_ms == std::optional<std::int64_t>{1781524800123} &&
          both->last_run_complete_unix_ms == std::optional<std::int64_t>{1781524700000});
    const SessionSummary* event = find(*loaded, "event");
    CHECK(event && event->last_event_unix_ms == std::optional<std::int64_t>{0} && !event->last_run_complete_unix_ms);
    const SessionSummary* run = find(*loaded, "run");
    CHECK(run && !run->last_event_unix_ms && run->last_run_complete_unix_ms == std::optional<std::int64_t>{5});
    const SessionSummary* none = find(*loaded, "none");
    CHECK(none && !none->last_event_unix_ms && !none->last_run_complete_unix_ms);
    std::ifstream in(sessions_file());
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.find("\"last_event_unix_ms\":1781524800123") != std::string::npos);
    CHECK(text.find("\"last_event_unix_ms\":0") != std::string::npos);
    CHECK(text.find("\"last_run_complete_unix_ms\":5") != std::string::npos);
    CHECK(text.find("\"last_event_unix_ms\":null") == std::string::npos);
    CHECK(text.find("\"last_run_complete_unix_ms\":null") == std::string::npos);
}

static void write_sessions_file(const std::string& body) {
    std::error_code ec;
    std::filesystem::create_directories(sessions_file().parent_path(), ec);
    std::ofstream out(sessions_file(), std::ios::trunc);
    out << body;
}

static void test_a_cache_written_before_the_clocks_loads_with_them_absent() {
    write_sessions_file(
        "{\"sessions\":[{\"id\":\"old\",\"title\":\"old\",\"updated_at\":1,\"status\":\"\",\"preview\":\"\","
        "\"state\":0,\"tag\":0,\"folder\":\"\",\"starred\":false}]}");
    const auto loaded = api::disk_cache::load_sessions();
    CHECK(loaded.has_value() && loaded->size() == 1);
    if (!loaded) return;
    CHECK(!loaded->front().last_event_unix_ms && !loaded->front().last_run_complete_unix_ms);
    CHECK(loaded->front().updated_at == 1);
}

static void test_malformed_clock_values_in_the_cache_load_as_absent() {
    write_sessions_file(
        "{\"sessions\":[{\"id\":\"bad\",\"title\":\"bad\",\"updated_at\":1,\"last_event_unix_ms\":-5,"
        "\"last_run_complete_unix_ms\":true},"
        "{\"id\":\"str\",\"title\":\"str\",\"updated_at\":1,\"last_event_unix_ms\":\"12\","
        "\"last_run_complete_unix_ms\":1.5},"
        "{\"id\":\"big\",\"title\":\"big\",\"updated_at\":1,\"last_event_unix_ms\":18446744073709551615,"
        "\"last_run_complete_unix_ms\":9223372036854775807},"
        "{\"id\":\"nul\",\"title\":\"nul\",\"updated_at\":1,\"last_event_unix_ms\":null,"
        "\"last_run_complete_unix_ms\":null}]}");
    const auto loaded = api::disk_cache::load_sessions();
    CHECK(loaded.has_value() && loaded->size() == 4);
    if (!loaded) return;
    for (const char* id : {"bad", "str", "nul"}) {
        const SessionSummary* s = find(*loaded, id);
        CHECK(s && !s->last_event_unix_ms && !s->last_run_complete_unix_ms);
    }
    const SessionSummary* big = find(*loaded, "big");
    CHECK(big && !big->last_event_unix_ms &&
          big->last_run_complete_unix_ms == std::optional<std::int64_t>{INT64_MAX});
}

int main() {
    std::printf("=== test_session_cache_clocks ===\n");
    char tmpl[] = "/tmp/hanabi_session_cache_clocks.XXXXXX";
    const char* dir = mkdtemp(tmpl);
    if (dir == nullptr) {
        std::printf("  FAIL: could not make a temp cache dir\n");
        return 1;
    }
    setenv("HANABI_CACHE_DIR", dir, 1);
    test_both_clocks_round_trip_and_absent_stays_absent();
    test_a_cache_written_before_the_clocks_loads_with_them_absent();
    test_malformed_clock_values_in_the_cache_load_as_absent();
    unsetenv("HANABI_CACHE_DIR");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
