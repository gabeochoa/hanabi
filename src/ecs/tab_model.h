#pragma once

// Pure, graphics-free tab-flow logic (open / focus / close), extracted from
// TabBarSystem so it can be exercised headlessly in tests with the real
// afterhours entity system but no window / UIContext / graphics backend.
//
// The rendering system (tab_bar_system.h) delegates to these, so the tested
// logic IS the shipped logic. Behavior (verbatim from the prior inline impl):
//   * open: focus the tab if the id is already open, else create a new tab
//           entity (Tab + ActiveTab), push to tabOrder, set selected/view.
//   * switch: move the ActiveTab marker, set selected/requestOpenId/view.
//   * close: erase from tabOrder, mark the entity for cleanup; if it was the
//            active tab, fall back to the neighbor (min(index, size-1)); if no
//            tabs remain, drop back to the Home digest.

#include <algorithm>
#include <cmath>
#include <string>

#include "../../vendor/afterhours/src/core/entity_helper.h"
#include "../../vendor/afterhours/src/core/entity_query.h"
#include "../util/format.h"
#include "components.h"

namespace ecs::model {

// Graphics-free equivalent of ui_imports.h's find_singleton_entity<Tab,
// ActiveTab>() — the render header keeps its own; this one has no UI deps.
inline afterhours::Entity* active_tab_entity() {
    auto results = afterhours::EntityQuery({.force_merge = true})
                       .whereHasComponent<Tab>()
                       .whereHasComponent<ActiveTab>()
                       .gen();
    if (results.empty()) return nullptr;
    return &results[0].get();
}

inline void switch_to_tab(AppComponent& app, afterhours::Entity& newTab) {
    if (auto* old = active_tab_entity()) old->removeComponent<ActiveTab>();
    newTab.addComponent<ActiveTab>();
    auto& tab = newTab.get<Tab>();
    app.selectedId = tab.sessionId;
    app.requestOpenId = tab.sessionId;
    app.view = SmartView::Chat;
}

// Open `id` in a tab: focus if already open, else create a new tab.
inline void open_session_in_tab(TabStripComponent& strip, AppComponent& app,
                                const std::string& id) {
    for (auto tabId : strip.tabOrder) {
        auto opt = afterhours::EntityHelper::getEntityForID(tabId);
        if (opt.valid() && opt->has<Tab>() &&
            opt->get<Tab>().sessionId == id) {
            switch_to_tab(app, opt.asE());
            return;
        }
    }
    // New tab.
    if (auto* old = active_tab_entity()) old->removeComponent<ActiveTab>();

    auto& e = afterhours::EntityHelper::createEntity();
    auto& tab = e.addComponent<Tab>();
    tab.sessionId = id;
    const auto* sum = app.find_summary(id);
    tab.label = sum ? (sum->title.empty() ? id : fmtutil::display_title(sum->title)) : id;
    e.addComponent<ActiveTab>();
    strip.tabOrder.push_back(e.id);

    app.selectedId = id;
    app.requestOpenId = id;  // loader fetches the transcript
    app.view = SmartView::Chat;
}

// ---------------------------------------------------------------------------
// Drag-to-reorder (pure logic, unit-tested headlessly).
//
// The tab strip lays tabs out left-to-right in uniform-width slots. While a
// tab is dragged, its "center" follows the cursor; the target drop index is
// the slot whose center the dragged center has passed. We compute that purely
// from geometry so the reorder decision is testable without a live mouse:
//   * stripX / slotStride: the x of the first slot and the per-tab advance
//     (uniform tab width + inter-tab gap).
//   * draggedCenterX: the current center-x of the dragged tab.
//   * count: number of tabs.
// Returns a clamped insertion index in [0, count-1].
// ---------------------------------------------------------------------------
inline size_t compute_drop_index(float draggedCenterX, float stripX,
                                 float slotStride, size_t count) {
    if (count <= 1 || slotStride <= 0.0f) return 0;
    // Which slot does the dragged center sit over? slot i's center is at
    // stripX + slotStride*i + slotStride/2, i.e. index = round to nearest slot
    // by (center - stripX) / stride.
    float rel = (draggedCenterX - stripX) / slotStride;
    long idx = static_cast<long>(std::floor(rel));
    if (idx < 0) idx = 0;
    if (idx > static_cast<long>(count) - 1) idx = static_cast<long>(count) - 1;
    return static_cast<size_t>(idx);
}

// Move the tab currently at `from` to sit at `to` (stable erase+insert). No-op
// if indices are equal, out of range, or fewer than 2 tabs. Order-only: it
// never touches the ActiveTab marker or any app selection, so the active tab
// and its content stay exactly as they were — only the visual order changes.
inline void reorder_tab(TabStripComponent& strip, size_t from, size_t to) {
    const size_t n = strip.tabOrder.size();
    if (n < 2 || from >= n || to >= n || from == to) return;
    auto id = strip.tabOrder[from];
    strip.tabOrder.erase(strip.tabOrder.begin() + static_cast<long>(from));
    // After erasing, indices > from shift left by one; `to` was computed
    // against the pre-erase layout's slot centers, so it already refers to the
    // desired final position. Clamp to the post-erase size and insert.
    size_t insertAt = to;
    if (insertAt > strip.tabOrder.size()) insertAt = strip.tabOrder.size();
    strip.tabOrder.insert(strip.tabOrder.begin() + static_cast<long>(insertAt),
                          id);
}

inline void close_tab(TabStripComponent& strip, AppComponent& app,
                      afterhours::EntityID tabId, size_t index,
                      bool wasActive) {
    if (index < strip.tabOrder.size())
        strip.tabOrder.erase(strip.tabOrder.begin() +
                             static_cast<long>(index));
    auto opt = afterhours::EntityHelper::getEntityForID(tabId);
    if (opt.valid()) opt.asE().cleanup = true;

    if (wasActive) {
        if (!strip.tabOrder.empty()) {
            size_t ni = std::min(index, strip.tabOrder.size() - 1);
            auto no = afterhours::EntityHelper::getEntityForID(strip.tabOrder[ni]);
            if (no.valid() && no->has<Tab>()) switch_to_tab(app, no.asE());
        } else {
            // No tabs left -> back to Home digest, clear open transcript.
            app.selectedId.clear();
            app.openSession.reset();
            app.view = SmartView::Home;
        }
    }
}

}  // namespace ecs::model
