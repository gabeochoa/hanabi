// A folder header says empty, how many want you, loading, or failed -- not a
// bare member count that reads the same in all five situations.
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/ecs/folder_state.h"
#include "../../src/ecs/sidebar_buckets.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

using api::SessionSummary;
using ecs::model::CatalogRead;
using ecs::model::FolderState;
using Members = std::vector<const SessionSummary*>;

static SessionSummary row(std::string id, api::ThreadState state,
                          api::ThreadTag tag, std::string folder = "/w") {
    SessionSummary s;
    s.id = std::move(id);
    s.title = s.id;
    s.state = state;
    s.tag = tag;
    s.folder = std::move(folder);
    return s;
}

static void test_a_failed_read_outranks_a_pending_one() {
    CHECK(ecs::model::catalog_read(false, "") == CatalogRead::Loading);
    CHECK(ecs::model::catalog_read(true, "") == CatalogRead::Fresh);
    CHECK(ecs::model::catalog_read(true, "timed out") == CatalogRead::Failed);
    CHECK(ecs::model::catalog_read(false, "timed out") == CatalogRead::Failed);
}

static void test_the_count_is_the_smart_views_rule() {
    const SessionSummary idle = row("a", api::ThreadState::Unknown,
                                    api::ThreadTag::None);
    const SessionSummary blocked = row("b", api::ThreadState::Attention,
                                       api::ThreadTag::Blocked);
    const SessionSummary waiting = row("c", api::ThreadState::Attention,
                                       api::ThreadTag::Waiting);
    const SessionSummary failed = row("d", api::ThreadState::Attention,
                                      api::ThreadTag::Failed);
    const SessionSummary ready = row("e", api::ThreadState::Ready,
                                     api::ThreadTag::Review);
    const SessionSummary running = row("f", api::ThreadState::Running,
                                       api::ThreadTag::None);
    const Members members{&idle, &blocked, &waiting, &failed, &ready, &running};
    const FolderState f =
        ecs::model::folder_state(members, 0, CatalogRead::Fresh);
    CHECK(f.total == 6);
    CHECK(f.attention == 4);
    int viewCount = 0;
    for (const SessionSummary* s : members)
        if (ecs::model::in_blocked_view(*s) ||
            s->state == api::ThreadState::Ready)
            ++viewCount;
    CHECK(f.attention == viewCount);
    CHECK(ecs::model::folder_count_label(f) == "4 need you \xc2\xb7 6");
}

static void test_every_state_reads_differently() {
    const SessionSummary idle = row("a", api::ThreadState::Unknown,
                                    api::ThreadTag::None);
    const Members four{&idle, &idle, &idle, &idle};
    const Members none;

    CHECK(ecs::model::folder_count_label(
              ecs::model::folder_state(four, 0, CatalogRead::Fresh)) == "4");
    CHECK(ecs::model::folder_count_label(
              ecs::model::folder_state(none, 0, CatalogRead::Fresh)) ==
          "empty");
    CHECK(ecs::model::folder_count_label(
              ecs::model::folder_state(none, 3, CatalogRead::Fresh)) ==
          "3 hidden");
    CHECK(ecs::model::folder_count_label(
              ecs::model::folder_state(four, 0, CatalogRead::Loading)) ==
          "loading\xe2\x80\xa6");
    CHECK(ecs::model::folder_count_label(
              ecs::model::folder_state(four, 0, CatalogRead::Failed)) ==
          "could not be read");
    // A stale count never masquerades as a fresh one: neither non-Fresh arm
    // prints the number.
    const std::string loading = ecs::model::folder_count_label(
        ecs::model::folder_state(four, 0, CatalogRead::Loading));
    const std::string failed = ecs::model::folder_count_label(
        ecs::model::folder_state(four, 0, CatalogRead::Failed));
    CHECK(loading.find('4') == std::string::npos);
    CHECK(failed.find('4') == std::string::npos);
}

static void test_the_buckets_count_what_the_filter_hid() {
    std::vector<SessionSummary> catalog;
    catalog.push_back(row("s1", api::ThreadState::Unknown,
                          api::ThreadTag::None, "/jobs"));
    catalog.back().title = "Schedule: nightly digest";
    catalog.push_back(row("s2", api::ThreadState::Unknown,
                          api::ThreadTag::None, "/jobs"));
    catalog.back().title = "kicker-tick";
    catalog.push_back(row("s3", api::ThreadState::Unknown,
                          api::ThreadTag::None, "/work"));

    ecs::model::SidebarBuckets buckets;
    const auto noContent = [](const std::string&, const std::string&) {
        return false;
    };
    buckets.rebuild(1, catalog, "", /*hideAutomated=*/true, noContent);
    CHECK(buckets.folders().size() == 2);
    CHECK(buckets.members("/jobs").empty());
    CHECK(buckets.hidden("/jobs") == 2);
    CHECK(buckets.hidden("/work") == 0);
    const FolderState jobs = ecs::model::folder_state(
        buckets.members("/jobs"), buckets.hidden("/jobs"), CatalogRead::Fresh);
    CHECK(ecs::model::folder_count_label(jobs) == "2 hidden");

    // Filter off: the same folder is two members and nothing hidden.
    buckets.rebuild(2, catalog, "", /*hideAutomated=*/false, noContent);
    CHECK(buckets.members("/jobs").size() == 2);
    CHECK(buckets.hidden("/jobs") == 0);
}

int main() {
    std::printf("=== test_folder_state ===\n");
    test_a_failed_read_outranks_a_pending_one();
    test_the_count_is_the_smart_views_rule();
    test_every_state_reads_differently();
    test_the_buckets_count_what_the_filter_hid();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
