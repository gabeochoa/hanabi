#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "../../src/ecs/subagent_parent_index.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

static api::SessionSummary summary(std::string id, std::string title) {
    api::SessionSummary out;
    out.id = std::move(id);
    out.title = std::move(title);
    return out;
}

int main() {
    std::vector<api::SessionSummary> roots{summary("parent", "Root wins"),
                                           summary("other", "Other")};
    std::vector<api::SessionSummary> children{
        summary("parent", "Duplicate child"), summary("child", "Child")};

    ecs::model::SubagentParentIndex index;
    CHECK(index.update(1, 1, roots, children));
    CHECK(index.rebuilds() == 1);
    CHECK(index.find("parent") == &roots[0]);
    CHECK(index.find("child") == &children[1]);
    CHECK(index.find("missing") == nullptr);

    CHECK(!index.update(1, 1, roots, children));
    CHECK(index.rebuilds() == 1);

    std::vector<api::SessionSummary> replacement{
        summary("parent", "Replacement root")};
    CHECK(index.update(2, 1, replacement, children));
    CHECK(index.rebuilds() == 2);
    CHECK(index.find("parent") == &replacement[0]);
    CHECK(index.find("parent")->title == "Replacement root");
    roots.clear();
    roots.shrink_to_fit();
    CHECK(index.find("parent") == &replacement[0]);

    std::vector<api::SessionSummary> replacementChildren{
        summary("new-child", "New child")};
    CHECK(index.update(2, 2, replacement, replacementChildren));
    CHECK(index.rebuilds() == 3);
    CHECK(index.find("child") == nullptr);
    CHECK(index.find("new-child") == &replacementChildren[0]);

    const auto max = std::numeric_limits<std::uint64_t>::max();
    CHECK(index.update(max, max, replacement, replacementChildren));
    CHECK(index.update(1, 1, replacement, replacementChildren));
    CHECK(!index.update(1, 1, replacement, replacementChildren));

    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
