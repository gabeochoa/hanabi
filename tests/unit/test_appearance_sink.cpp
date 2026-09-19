#include <cstdio>
#include <vector>

#include "../../src/ui/appearance_sink.h"

namespace ap = hanabi::appearance;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

static std::vector<ap::Scheme> g_seen;
static void record(ap::Scheme s) { g_seen.push_back(s); }

static void test_publish_without_a_sink_reaches_nothing_and_remembers() {
    ap::reset_for_test();
    g_seen.clear();
    ap::publish(ap::Scheme::Light);
    CHECK(g_seen.empty());
    CHECK(ap::published_once() && ap::last_published() == ap::Scheme::Light);
}

static void test_installing_the_sink_replays_the_last_published_scheme() {
    ap::reset_for_test();
    g_seen.clear();
    ap::publish(ap::Scheme::Dark);
    ap::publish(ap::Scheme::Light);
    ap::install_sink(&record);
    CHECK(g_seen.size() == 1 && g_seen[0] == ap::Scheme::Light);
}

static void test_installing_before_any_publish_replays_nothing() {
    ap::reset_for_test();
    g_seen.clear();
    ap::install_sink(&record);
    CHECK(g_seen.empty());
    ap::publish(ap::Scheme::Dark);
    CHECK(g_seen.size() == 1 && g_seen[0] == ap::Scheme::Dark);
}

static void test_every_later_change_reaches_the_sink_in_order() {
    ap::reset_for_test();
    g_seen.clear();
    ap::install_sink(&record);
    ap::publish(ap::Scheme::Dark);
    ap::publish(ap::Scheme::Light);
    ap::publish(ap::Scheme::Light);
    ap::publish(ap::Scheme::Dark);
    CHECK(g_seen.size() == 4);
    CHECK(g_seen[0] == ap::Scheme::Dark && g_seen[1] == ap::Scheme::Light && g_seen[2] == ap::Scheme::Light &&
          g_seen[3] == ap::Scheme::Dark);
}

static void test_removing_the_sink_stops_delivery_but_keeps_the_memory() {
    ap::reset_for_test();
    g_seen.clear();
    ap::install_sink(&record);
    ap::publish(ap::Scheme::Light);
    ap::install_sink(nullptr);
    ap::publish(ap::Scheme::Dark);
    CHECK(g_seen.size() == 1);
    CHECK(ap::last_published() == ap::Scheme::Dark);
    ap::install_sink(&record);
    CHECK(g_seen.size() == 2 && g_seen[1] == ap::Scheme::Dark);
}

int main() {
    std::printf("=== test_appearance_sink ===\n");
    test_publish_without_a_sink_reaches_nothing_and_remembers();
    test_installing_the_sink_replays_the_last_published_scheme();
    test_installing_before_any_publish_replays_nothing();
    test_every_later_change_reaches_the_sink_in_order();
    test_removing_the_sink_stops_delivery_but_keeps_the_memory();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
