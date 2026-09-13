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

    // The list as drawn: parents in order, an unfolded parent's children
    // beneath it, a child that is also a member drawn once, and the drop
    // index counting parents only.
    api::SessionSummary p1; p1.id = "p1";
    api::SessionSummary p2; p2.id = "p2";
    api::SessionSummary c1m; c1m.id = "c1";  // c1 is ALSO a member row
    std::vector<const api::SessionSummary*> members = {&p1, &c1m, &p2};
    std::vector<std::string> open = {"p1", "p2"};
    std::vector<M::VisibleRow> vis;
    M::flatten_visible(members, 3, index, open, vis);
    // p1, (c1 skipped: it is a member), c2, c1(member), p2, c3
    CHECK(vis.size() == 5);
    CHECK(vis[0].s->id == "p1" && !vis[0].child);
    CHECK(vis[1].s->id == "c2" && vis[1].child && vis[1].parentIndex == 0);
    CHECK(vis[2].s->id == "c1" && !vis[2].child);
    CHECK(vis[3].s->id == "p2" && !vis[3].child && vis[3].parentIndex == 2);
    CHECK(vis[4].s->id == "c3" && vis[4].child);
    // Drop gaps: before p1 = 0; between p1 and its child c2 = 1; between c2
    // and c1 = 1 (a child adds nothing); after everything = 3 parents.
    CHECK(M::parent_drop_index(vis, 0) == 0);
    CHECK(M::parent_drop_index(vis, 1) == 1);
    CHECK(M::parent_drop_index(vis, 2) == 1);
    CHECK(M::parent_drop_index(vis, 5) == 3);
    CHECK(M::parent_drop_index(vis, 99) == 3);
    // A limit shorter than the members cuts parents AND their children.
    M::flatten_visible(members, 1, index, open, vis);
    CHECK(vis.size() == 3);  // p1, c1 (not a member within the limit), c2

    if (failures == 0) std::printf("OK\n");
    return failures == 0 ? 0 : 1;
}
