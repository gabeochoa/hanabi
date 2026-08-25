#pragma once

// ---------------------------------------------------------------------------
// Which widgets did this frame actually build?
//
// afterhours' `imm::mk()` keeps a permanent map from call-site hash to entity
// and hands back the same entity forever. That is a cache for a screen you
// come back to and a leak for one you do not: nothing marks an entity as "not
// built this frame" and nothing ever retires one, while every UI system walks
// the WHOLE collection once per system per frame. So the set every pass walks
// is the union of every screen the app has ever shown. Measured at a
// 2000-session catalog: 461 entities sitting on Home, 2577 after visiting five
// screens -- of which 2401 were not built that frame (afterhours_gaps.md
// #115).
//
// The library cannot be changed (vendor/afterhours is read-only, ~20 projects
// vendor it), and #115's own list of escapes rules out the obvious app-side
// ones. This is the one it does not: hanabi imports `mk` through a SINGLE
// using-declaration (src/ecs/ui_imports.h), so hanabi can put its own `mk` in
// front of the library's and stamp what it hands back. Every widget the app
// builds then carries the frame that built it, which is the fact the library
// has and does not record.
//
// The stamp on its own retires nothing -- it makes the divergence between
// "live" and "built this frame" visible. src/ecs/widget_retire_system.h is
// what acts on it.
//
// TWO THINGS HERE ARE NOT STYLE, THEY ARE MEASURED:
//
//   * The source_location MUST be forwarded. `imm::mk` hashes the call site to
//     find the entity, so a wrapper that let the default argument bind to
//     itself would give every widget in the app the same hash -- one entity
//     for the whole tree.
//
//   * The stamp is a DENSE SIDE TABLE, not a component. The obvious ECS shape
//     is `entity.addComponentIfMissing<BuiltAt>().epoch = ...`, and it costs
//     0.108 ms/frame at a 2000-session catalog against 0.030 for the vector
//     (idle, interleaved A/B, median of 5). An entity's component array is
//     `std::array<unique_ptr, 128>` INLINE in the entity -- a kilobyte -- so a
//     component slot is 500-odd bytes past the entity header and the component
//     itself is a separate allocation: two cache misses per widget per frame,
//     to write four bytes. The vector is 4 bytes per live EntityID, contiguous,
//     and stays in L1. See afterhours_gaps.md #160.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdlib>
#include <source_location>
#include <vector>

#include "../../vendor/afterhours/src/core/entity.h"
#include "../../vendor/afterhours/src/plugins/ui/entity_management.h"
#include "../../vendor/afterhours/src/plugins/ui/ui_collection.h"

namespace hanabi::widget_epoch {

using afterhours::Entity;
using afterhours::EntityID;

// Frame counter. Starts at 1 so that 0 means "no widget of ours has ever had
// this EntityID", which is what makes the table safe to index by a recycled
// id: an entity hanabi's `mk` never handed out reads 0 and is invisible to
// everything below.
//
// Inline VARIABLES, not function-local statics: `mk` is the hottest path the
// app has (every widget, every frame) and a function-local static costs a
// guard check on every read.
inline unsigned g_epoch = 1;

// How many `mk` calls this epoch has seen. The per-frame "built" count that
// #115 asks the library for and does not get: compare it with the live entity
// count and the gap is the dead weight.
inline size_t g_built_this_epoch = 0;

// EntityID -> the epoch that last built it. 0 == not ours.
inline std::vector<unsigned> g_stamps;

inline unsigned epoch() { return g_epoch; }
inline size_t built_this_epoch() { return g_built_this_epoch; }

inline unsigned& stamp_of(EntityID id) {
    const size_t idx = static_cast<size_t>(id);
    if (idx >= g_stamps.size()) g_stamps.resize(idx + 64, 0u);
    return g_stamps[idx];
}

inline unsigned stamp_read(EntityID id) {
    const size_t idx = static_cast<size_t>(id);
    return idx < g_stamps.size() ? g_stamps[idx] : 0u;
}

inline void begin_epoch() {
    ++g_epoch;
    g_built_this_epoch = 0;
}

inline afterhours::ui::imm::EntityParent mk(
    Entity& parent, EntityID otherID = -1,
    const std::source_location location = std::source_location::current()) {
    afterhours::ui::imm::EntityParent pair =
        afterhours::ui::imm::mk(parent, otherID, location);
    stamp_of(pair.first.get().id) = g_epoch;
    ++g_built_this_epoch;
    return pair;
}

// The divergence, counted. #115's weaker ask -- "expose the collection size
// and a per-frame built count so an app can see the two diverge" -- answered
// from the app side.
//
// `unstamped` is the honest blind spot: entities the LIBRARY made for itself
// (the UI root, a scrollbar, a drag spacer) never came through hanabi's `mk`,
// carry no stamp, and can never be retired by anything hanabi writes. It is
// reported next to the rest so the number is visible rather than assumed.
struct Tally {
    size_t live = 0;
    size_t stamped = 0;
    size_t built_this_frame = 0;
    size_t stale = 0;
    size_t unstamped = 0;
};

inline Tally tally() {
    Tally out;
    for (const auto& ptr :
         afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
        if (!ptr) continue;
        ++out.live;
        const unsigned stamp = stamp_read(ptr->id);
        if (stamp == 0u) {
            ++out.unstamped;
            continue;
        }
        ++out.stamped;
        if (stamp == g_epoch)
            ++out.built_this_frame;
        else
            ++out.stale;
    }
    return out;
}

// ---------------------------------------------------------------------------
// The sweep.
//
// A widget is retired when nothing has built it for `grace` frames. Retiring
// is TWO operations, and they must happen together:
//
//   1. Erase the call-site hash that points at it. `imm::existing_ui_elements`
//      is a hash -> EntityID map that lives forever, and afterhours RECYCLES
//      EntityIDs -- so an entry left pointing at a destroyed entity is not a
//      slow leak, it is the next `mk()` from that call site being handed a
//      different widget's entity. This is the dangerous half.
//   2. Mark the entity for cleanup. afterhours' own post-update bridge calls
//      `cleanup()` at the end of the update phase, so a widget retired at the
//      top of a frame is destroyed before that frame renders. Nothing draws a
//      half-dead widget, and nothing hanabi wrote has to reach into the
//      library's destruction order.
//
// The loop is over THE MAP, not over the entity collection, and that is what
// makes it safe rather than merely correct: the map is by definition the set
// of entities `mk` currently owns. An entity that has a stale stamp but is no
// longer in the map (or never was) is not ours to destroy, and this cannot
// touch it. It also costs O(live widgets) instead of O(all entities), and only
// on the frames that sweep.
//
// The map is scanned by VALUE rather than reverse-indexed by hash on purpose:
// hanabi never sees the hash `mk()` computed, and re-deriving it would mean
// copying the library's private string format and silently mismatching the day
// it changes.
// ---------------------------------------------------------------------------

inline bool env_flag(const char* name, bool fallback) {
    const char* v = std::getenv(name);
    if (v == nullptr || *v == '\0') return fallback;
    return *v != '0';
}

inline unsigned env_uint(const char* name, unsigned fallback) {
    const char* v = std::getenv(name);
    if (v == nullptr || *v == '\0') return fallback;
    const int parsed = std::atoi(v);
    return parsed > 0 ? static_cast<unsigned>(parsed) : fallback;
}

// An off switch, because a fix with no off switch cannot be A/B'd and cannot
// be bisected past. scripts/retire_gate.sh runs the same binary both ways.
inline bool retire_enabled() {
    static const bool on = env_flag("HANABI_RETIRE", true);
    return on;
}

// Frames a widget may go unbuilt before it is retired. 90 is a second and a
// half: long enough that a popover toggled off and on, or a tab flicked to and
// back, keeps its entity and its state; short enough that a screen you have
// left stops costing anything while you are still looking at the next one.
inline unsigned grace_frames() {
    static const unsigned n = env_uint("HANABI_RETIRE_GRACE", 90);
    return n;
}

// Frames between sweeps. The sweep is cheap but not free, and retiring is
// never urgent -- a widget that has been dead for 90 frames can be dead for 15
// more. This divides the sweep's own cost by 15 and costs 15 frames of
// latency.
inline unsigned sweep_every() {
    static const unsigned n = env_uint("HANABI_RETIRE_EVERY", 15);
    return n;
}

struct SweepResult {
    size_t considered = 0;
    size_t retired = 0;
};

inline size_t g_retired_total = 0;

inline SweepResult retire_stale(unsigned grace) {
    SweepResult out;
    auto& owned = afterhours::ui::imm::existing_ui_elements;
    auto& collection = afterhours::ui::UICollectionHolder::get().collection;
    out.considered = owned.size();
    for (auto it = owned.begin(); it != owned.end();) {
        const EntityID id = it->second;
        const unsigned stamp = stamp_read(id);
        if (stamp == 0u || g_epoch - stamp <= grace) {
            ++it;
            continue;
        }
        stamp_of(id) = 0u;
        collection.markIDForCleanup(id);
        it = owned.erase(it);
        ++out.retired;
    }
    g_retired_total += out.retired;
    return out;
}

}  // namespace hanabi::widget_epoch
