// What retirement promises, and the two ways it could quietly be wrong.
//
// src/ui/widget_epoch.h puts hanabi's `mk` in front of afterhours' and retires
// what nothing has built. Both halves have a failure mode that LOOKS fine:
//
//   * The wrapper must forward the caller's std::source_location. `imm::mk`
//     hashes the call site to decide which entity to hand back, so a wrapper
//     that let the default argument bind to its own body would give every
//     widget in the app one entity — a screen that renders as a single box,
//     or, worse, renders correctly until two call sites disagree about what
//     they were handed. `same_call_site_keeps_its_entity` and
//     `two_call_sites_get_two_entities` are that pair.
//
//   * Retiring is TWO operations: erase the call-site hash AND destroy the
//     entity. Do only the second and afterhours' id recycling hands the next
//     `mk()` at that call site a different widget's entity —
//     `the_map_never_points_at_a_dead_entity` is the assertion, and it is the
//     one that matters, because the symptom of getting it wrong is not a leak
//     but a widget wearing another widget's state.
//
// No graphics: the UI collection, `imm::mk` and the sweep are all header-only,
// so the frame loop below is the real one — advance the epoch, sweep, build,
// merge, clean up — with the drawing left out.

#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "../../src/ui/widget_epoch.h"

using afterhours::Entity;
using afterhours::EntityID;
namespace we = hanabi::widget_epoch;

static int g_failures = 0;
#define CHECK(cond)                                                 \
    do {                                                            \
        if (!(cond)) {                                              \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++g_failures;                                           \
        }                                                           \
    } while (0)

static afterhours::EntityCollection& ui() {
    return afterhours::ui::UICollectionHolder::get().collection;
}

// Two distinct call sites. They have to be distinct TEXTUALLY, not just
// logically: the hash is (parent id, index, file, line, column, function).
static EntityID build_a(Entity& parent) { return we::mk(parent).first.get().id; }
static EntityID build_b(Entity& parent) { return we::mk(parent).first.get().id; }

// One frame, in the order the app runs it: the epoch opens and the sweep runs
// before anything is built (ecs::WidgetRetireSystem), and afterhours merges and
// cleans up at the end of the update phase.
template <typename Build>
static void frame(unsigned grace, Build build) {
    we::begin_epoch();
    we::retire_stale(grace);
    build();
    ui().merge_entity_arrays();
    ui().cleanup();
}

static bool alive(EntityID id) {
    for (const auto& ptr : ui().get_entities())
        if (ptr && ptr->id == id) return true;
    return false;
}

static size_t map_entries_for(EntityID id) {
    size_t n = 0;
    for (const auto& [hash, entity] : afterhours::ui::imm::existing_ui_elements)
        if (entity == id) ++n;
    return n;
}

// --- the wrapper is transparent ---------------------------------------------
static void same_call_site_keeps_its_entity() {
    std::printf("same_call_site_keeps_its_entity\n");
    Entity& root = ui().createPermanentEntity();
    ui().merge_entity_arrays();

    EntityID first = -1;
    for (int i = 0; i < 200; ++i) {
        EntityID got = -1;
        frame(90, [&] { got = build_a(root); });
        if (i == 0) first = got;
        CHECK(got == first);
    }
    CHECK(alive(first));
}

static void two_call_sites_get_two_entities() {
    std::printf("two_call_sites_get_two_entities\n");
    Entity& root = ui().createPermanentEntity();
    ui().merge_entity_arrays();

    EntityID a = -1;
    EntityID b = -1;
    frame(90, [&] {
        a = build_a(root);
        b = build_b(root);
    });
    CHECK(a != b);
}

// --- what the sweep takes, and what it leaves --------------------------------
static void a_widget_still_being_built_is_never_retired() {
    std::printf("a_widget_still_being_built_is_never_retired\n");
    Entity& root = ui().createPermanentEntity();
    ui().merge_entity_arrays();

    EntityID a = -1;
    for (int i = 0; i < 300; ++i) frame(4, [&] { a = build_a(root); });
    CHECK(alive(a));
    CHECK(map_entries_for(a) == 1);
}

static void a_widget_nobody_builds_is_retired_after_the_grace() {
    std::printf("a_widget_nobody_builds_is_retired_after_the_grace\n");
    Entity& root = ui().createPermanentEntity();
    ui().merge_entity_arrays();

    EntityID a = -1;
    frame(4, [&] { a = build_a(root); });
    CHECK(alive(a));

    // Inside the grace: still here, still owned.
    for (int i = 0; i < 4; ++i) frame(4, [] {});
    CHECK(alive(a));
    CHECK(map_entries_for(a) == 1);

    // Past it: gone from the map and gone from the collection.
    frame(4, [] {});
    CHECK(!alive(a));
    CHECK(map_entries_for(a) == 0);
    CHECK(we::stamp_read(a) == 0u);
}

// The one that matters. afterhours recycles EntityIDs, so a hash left pointing
// at a destroyed entity is not a leak — it is the next `mk()` at that call site
// being handed a live widget that belongs to something else.
static void the_map_never_points_at_a_dead_entity() {
    std::printf("the_map_never_points_at_a_dead_entity\n");
    Entity& root = ui().createPermanentEntity();
    ui().merge_entity_arrays();

    EntityID gone = -1;
    frame(2, [&] { gone = build_a(root); });
    for (int i = 0; i < 4; ++i) frame(2, [] {});
    CHECK(!alive(gone));

    // Churn: enough new widgets that the recycled id is handed out again.
    std::set<EntityID> reissued;
    for (int i = 0; i < 8; ++i)
        frame(2, [&] { reissued.insert(build_b(root)); });

    for (const auto& [hash, id] : afterhours::ui::imm::existing_ui_elements)
        CHECK(alive(id));

    // And the call site that was retired builds itself a NEW entity rather
    // than being handed whatever now holds its old id.
    EntityID again = -1;
    frame(2, [&] { again = build_a(root); });
    CHECK(alive(again));
    CHECK(reissued.count(again) == 0 || again != *reissued.begin());
    for (const auto& [hash, id] : afterhours::ui::imm::existing_ui_elements)
        CHECK(alive(id));
}

// An entity the LIBRARY made for itself never came through hanabi's `mk`, is
// not in the map, and must survive any amount of sweeping.
static void an_entity_the_library_owns_is_never_touched() {
    std::printf("an_entity_the_library_owns_is_never_touched\n");
    Entity& theirs = ui().createEntity();
    const EntityID id = theirs.id;
    ui().merge_entity_arrays();

    for (int i = 0; i < 50; ++i) frame(1, [] {});
    CHECK(alive(id));
    CHECK(we::stamp_read(id) == 0u);
}

int main() {
    std::printf("=== widget retirement ===\n");
    same_call_site_keeps_its_entity();
    two_call_sites_get_two_entities();
    a_widget_still_being_built_is_never_retired();
    a_widget_nobody_builds_is_retired_after_the_grace();
    the_map_never_points_at_a_dead_entity();
    an_entity_the_library_owns_is_never_touched();

    if (g_failures == 0) {
        std::printf("all widget retirement checks passed\n");
        return 0;
    }
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
}
