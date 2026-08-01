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
