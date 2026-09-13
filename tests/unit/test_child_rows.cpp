// Children under their parent: who folds, what the glyph slot is for, and
// that expansion follows the THREAD rather than the row it happened to be on.
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/ecs/child_rows.h"

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);       \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

static api::SessionSummary child(const char* id, const char* parent) {
    api::SessionSummary s;
    s.id = id;
    s.parent_id = parent;
    return s;
}

int main() {
    namespace M = ecs::model;

    // The glyph slot: status alone without children; with them it folds, and
    // it keeps its mark when the thread wants something.
    CHECK(M::row_glyph_role(false, false) == M::RowGlyphRole::Status);
    CHECK(M::row_glyph_role(false, true) == M::RowGlyphRole::Status);
    CHECK(M::row_glyph_role(true, false) == M::RowGlyphRole::Fold);
    CHECK(M::row_glyph_role(true, true) == M::RowGlyphRole::MarkedFold);

    std::vector<api::SessionSummary> kids = {
        child("c1", "p1"), child("c2", "p1"), child("c3", "p2"),
        child("orphan", "")};
    M::ChildIndex index;
    index.sync(kids, 1);
    CHECK(index.count_for("p1") == 2);
    CHECK(index.count_for("p2") == 1);
    CHECK(index.count_for("p3") == 0);
    CHECK(index.find("p3") == nullptr);
    CHECK(index.find("p1") != nullptr && index.find("p1")->front()->id == "c1");
    // A child with no parent belongs to nobody rather than to everybody.
    CHECK(index.count_for("") == 0);

    // The same revision does not rebuild; a new one does.
    const std::size_t built = index.rebuilds();
    index.sync(kids, 1);
    CHECK(index.rebuilds() == built);
    index.sync(kids, 2);
    CHECK(index.rebuilds() == built + 1);

    // Expansion is by id, so a list that reorders cannot fold the wrong row.
    std::vector<std::string> expanded;
    CHECK(!M::children_shown(expanded, "p1"));
    M::toggle_children(expanded, "p1");
    CHECK(M::children_shown(expanded, "p1"));
    CHECK(!M::children_shown(expanded, "p2"));
    M::toggle_children(expanded, "p2");
    M::toggle_children(expanded, "p1");
    CHECK(!M::children_shown(expanded, "p1"));
    CHECK(M::children_shown(expanded, "p2"));

    if (failures == 0) std::printf("OK\n");
    return failures == 0 ? 0 : 1;
}
