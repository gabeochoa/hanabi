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

inline constexpr float kTabMinWidth = 40.0f;
inline constexpr float kTabMaxWidth = 220.0f;
inline constexpr float kTabGap = 4.0f;

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
    auto& pane = app.pane();
    pane.supersede_transcript_loads();
    if (auto* old = active_tab_entity()) old->removeComponent<ActiveTab>();
    newTab.addComponent<ActiveTab>();
    auto& tab = newTab.get<Tab>();
    pane.selectedId = tab.sessionId;
    pane.requestOpenId = tab.sessionId;
    app.view = SmartView::Chat;
}

// A tab's caption: the thread's title, or the raw id while the list has not
// caught up. A thread the user just started is created locally and only appears
// in the session list on the next refresh, so at open time there is no title
// and the tab used to be stuck reading its id ("new1") for the rest of the
// session. Resolved fresh on every render instead.
inline std::string_view tab_label_view_for(const AppComponent& app,
                                           const std::string& id) {
    const auto* sum = app.find_summary(id);
    if (sum && !sum->title.empty())
        return fmtutil::display_title_view(sum->title);
    for (const auto& pane : app.panes) {
        if (pane.openSession && pane.openSession->summary.id == id &&
            !pane.openSession->summary.title.empty())
            return fmtutil::display_title_view(pane.openSession->summary.title);
    }
    return id;
}

inline std::string tab_label_for(const AppComponent& app,
                                 const std::string& id) {
    return std::string(tab_label_view_for(app, id));
}

inline const std::string& refresh_tab_label(const AppComponent& app, Tab& tab) {
    const std::string_view title = tab_label_view_for(app, tab.sessionId);
    if (tab.label != title) {
        tab.label.assign(title);
        tab.accessibleLabel.clear();
        tab.closeAccessibleLabel.clear();
    }
    return tab.label;
}

inline const std::string& tab_close_accessible_label(Tab& tab) {
    if (tab.closeAccessibleLabel.empty()) {
        tab.closeAccessibleLabel.reserve(tab.label.size() + 11);
        tab.closeAccessibleLabel.append("Close tab: ").append(tab.label);
    }
    return tab.closeAccessibleLabel;
}

inline const std::string& tab_accessible_label(Tab& tab, bool active) {
    if (tab.accessibleLabel.empty() || tab.accessiblePinned != tab.pinned ||
        tab.accessibleActive != active) {
        tab.accessibleLabel.clear();
        tab.accessibleLabel.reserve(tab.label.size() + 24);
        tab.accessibleLabel.append("Tab: ").append(tab.label);
        if (tab.pinned) tab.accessibleLabel.append(", pinned");
        if (active) tab.accessibleLabel.append(", active");
        tab.accessiblePinned = tab.pinned;
        tab.accessibleActive = active;
    }
    return tab.accessibleLabel;
}

// The one tab currently showing a PREVIEW, or nullptr. There is never more than
// one: opening a second preview reuses this tab instead of adding another.
inline afterhours::Entity* preview_tab_entity(const TabStripComponent& strip) {
    // Merge first, then look up: a tab created earlier in the same frame is not
    // in the entity map yet, and finding exactly that tab is what this is for.
    // active_tab_entity's query forces the same merge.
    auto tabs = afterhours::EntityQuery({.force_merge = true})
                    .whereHasComponent<Tab>()
                    .gen();
    for (auto tabId : strip.tabOrder) {
        for (auto& ref : tabs) {
            afterhours::Entity& e = ref.get();
            if (e.id == tabId && !e.get<Tab>().keptOpen) return &e;
        }
    }
    return nullptr;
}

// Commit to a tab: it stops being a glance and becomes one the user is using.
// Idempotent, so "keep it" can be said on every path that means it without any
// caller having to check first.
inline void keep_tab(afterhours::Entity& tabEntity) {
    if (tabEntity.has<Tab>()) tabEntity.get<Tab>().keptOpen = true;
}

inline void set_tab_pinned(Tab& tab, bool pinned) {
    tab.pinned = pinned;
    tab.accessibleLabel.clear();
    if (pinned) tab.keptOpen = true;
}

// Open `id` in a tab: focus if already open, else create a new tab.
// `keep` false means this is a PREVIEW — a sidebar row clicked once. A preview
// REUSES the existing preview tab rather than opening another, so browsing a
// list never leaves a trail of tabs behind. Asking for a thread that is already
// open always keeps it: the second look is the commitment.
// `pinned` applies to a NEWLY created tab only. It is a parameter rather than
// something the caller sets afterwards because an entity created here cannot be
// found by EntityHelper::getEntityForID until the ECS merges at the end of the
// frame — a restore loop that opened tabs and then looked them up by id to
// stamp state on them silently stamped nothing (see FRICTION_LOG).
inline void open_session_in_tab(TabStripComponent& strip, AppComponent& app,
                                const std::string& id, bool keep = true,
                                bool pinned = false) {
    for (auto tabId : strip.tabOrder) {
        auto opt = afterhours::EntityHelper::getEntityForID(tabId);
        if (opt.valid() && opt->has<Tab>() && opt->get<Tab>().sessionId == id) {
            keep_tab(opt.asE());
            switch_to_tab(app, opt.asE());
            return;
        }
    }
    // New tab.
    if (auto* old = active_tab_entity()) old->removeComponent<ActiveTab>();

    afterhours::Entity* e = nullptr;
    if (!keep) {
        // Reuse the tab already holding a preview: same slot in tabOrder, same
        // entity, new thread — which is what makes clicking down a list feel
        // like one pane changing rather than tabs accumulating.
        e = preview_tab_entity(strip);
    }
    if (e == nullptr) {
        e = &afterhours::EntityHelper::createEntity();
        e->addComponent<Tab>();
        strip.tabOrder.push_back(e->id);
    }
    auto& tab = e->get<Tab>();
    const std::string nextLabel = tab_label_for(app, id);
    const bool identityChanged = tab.sessionId != id || tab.label != nextLabel;
    tab.sessionId = id;
    tab.label = nextLabel;
    if (identityChanged) {
        tab.accessibleLabel.clear();
        tab.closeAccessibleLabel.clear();
    }
    tab.keptOpen = keep;
    tab.pinned = pinned;
    e->addComponentIfMissing<ActiveTab>();

    auto& pane = app.pane();
    pane.supersede_transcript_loads();
    pane.selectedId = id;
    pane.requestOpenId = id;  // loader fetches the transcript
    // First time this thread is opened (a NEW tab) -> land at the bottom
    // (newest message). Switching to an already-open tab returns above and
    // never sets this, so its scroll position is preserved.
    pane.scrollBottomPending = id;
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
// ---------------------------------------------------------------------------
// Chrome-style overflow width (pure, unit-tested).
//
// Chrome keeps every open tab visible by SHRINKING them to share the strip,
// down to a comfortable floor. Below that floor it stops shrinking (tabs would
// become illegible) and instead lets the strip SCROLL. We model exactly that:
//
//   compute_tab_width(stripW, count, minW, maxW, gap):
//     * 0 tabs            -> maxW (unused, but well-defined).
//     * fits at <= maxW   -> uniform share (stripW split evenly), capped at
//                            maxW so a couple of tabs don't stretch huge.
//     * would go < minW   -> CLAMP at minW; the tabs now overflow and the
//                            strip must scroll (see compute_max_scroll).
//   The returned width is the SAME for every tab (uniform), which keeps the
//   existing drag-reorder slot math (slotStride) valid.
// ---------------------------------------------------------------------------
inline float compute_tab_width(float stripW, size_t count, float minW,
                               float maxW, float gap) {
    if (count == 0) return maxW;
    float totalGap = gap * static_cast<float>(count - 1);
    float perTab = (stripW - totalGap) / static_cast<float>(count);
    return std::clamp(perTab, minW, maxW);
}

// Total pixel width the tabs+gaps occupy at width `tabW`. (count-1 gaps.)
inline float compute_content_width(size_t count, float tabW, float gap) {
    if (count == 0) return 0.0f;
    return static_cast<float>(count) * tabW +
           gap * static_cast<float>(count - 1);
}

// How far the strip can scroll horizontally: content width beyond the visible
// strip, floored at 0 (never scroll when everything fits).
inline float compute_max_scroll(float stripW, size_t count, float tabW,
                                float gap) {
    float content = compute_content_width(count, tabW, gap);
    return std::max(0.0f, content - stripW);
}

// Clamp a proposed scroll offset into [0, maxScroll].
inline float clamp_scroll(float offset, float maxScroll) {
    if (maxScroll <= 0.0f) return 0.0f;
    return std::clamp(offset, 0.0f, maxScroll);
}

inline float horizontal_scroll_delta(float wheelX, float wheelY, bool overStrip,
                                     bool shiftDown) {
    if (overStrip && wheelX != 0.0f) return wheelX;
    if ((overStrip || shiftDown) && wheelY != 0.0f) return wheelY;
    return 0.0f;
}

// Given the currently-active tab's index, return a scroll offset that makes
// that tab FULLY visible (Chrome scrolls a freshly-selected off-screen tab
// into view). If the tab's left edge is left of the viewport, scroll so its
// left aligns to the strip's left; if its right edge is past the viewport,
// scroll so its right aligns to the strip's right. Otherwise keep `offset`.
// slotStride == tabW + gap. Result is clamped to [0, maxScroll].
inline float scroll_to_show(size_t activeIndex, float offset, float stripW,
                            float tabW, float gap, size_t count) {
    if (count == 0) return 0.0f;
    float slotStride = tabW + gap;
    float tabLeft = slotStride * static_cast<float>(activeIndex);
    float tabRight = tabLeft + tabW;
    float maxScroll = compute_max_scroll(stripW, count, tabW, gap);
    float out = offset;
    if (tabLeft < offset) {
        out = tabLeft;  // scroll left to reveal
    } else if (tabRight > offset + stripW) {
        out = tabRight - stripW;  // scroll right to reveal
    }
    return clamp_scroll(out, maxScroll);
}

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

inline bool session_is_open(const TabStripComponent& strip,
                            const std::string& sessionId) {
    if (sessionId.empty()) return false;
    for (auto tabId : strip.tabOrder) {
        auto opt = afterhours::EntityHelper::getEntityForID(tabId);
        if (opt.valid() && opt->has<Tab>() &&
            opt->get<Tab>().sessionId == sessionId)
            return true;
    }
    return false;
}

inline void retarget_split_pane(Pane& target, const std::string& id) {
    target.supersede_transcript_loads();
    target.selectedId = id;
    target.requestOpenId = id;
    target.scrollBottomPending = id;
}

inline void reset_pane_to(Pane& pane, const std::string& sessionId) {
    pane.supersede_transcript_loads();
    pane.selectedId = sessionId;
    pane.requestOpenId = sessionId;
    pane.openSession.reset();
    pane.transcriptState = LoadState::Idle;
    pane.transcriptError.clear();
    pane.transcriptLoadingId.clear();
    pane.scrollBottomPending.clear();
    pane.hasMoreOlder = false;
    pane.requestLoadOlder = false;
    pane.loadingOlder = false;
    pane.anchorPending.clear();
    pane.findOpen = false;
    pane.findQuery.clear();
    pane.findIndex = 0;
    pane.findCount = 0;
    pane.findScrollPending = false;
}

inline void reconcile_panes_with_tabs(const TabStripComponent& strip,
                                      AppComponent& app,
                                      const std::string& fallbackId) {
    for (auto& pane : app.panes) {
        if (!pane.requestOpenId.empty() &&
            !session_is_open(strip, pane.requestOpenId))
            pane.requestOpenId.clear();
        if (!pane.scrollBottomPending.empty() &&
            !session_is_open(strip, pane.scrollBottomPending))
            pane.scrollBottomPending.clear();
        if (!pane.selectedId.empty() &&
            !session_is_open(strip, pane.selectedId))
            reset_pane_to(pane, fallbackId);
    }
    if (strip.tabOrder.empty()) {
        app.splitOpen = false;
        app.focusedPane = 0;
        app.view = SmartView::Home;
    }
}

inline void close_tab(TabStripComponent& strip, AppComponent& app,
                      afterhours::EntityID tabId, size_t index,
                      bool wasActive) {
    auto opt = afterhours::EntityHelper::getEntityForID(tabId);
    if (index < strip.tabOrder.size())
        strip.tabOrder.erase(strip.tabOrder.begin() + static_cast<long>(index));
    if (opt.valid()) opt.asE().cleanup = true;

    std::string fallbackId;
    afterhours::Entity* fallback = nullptr;
    if (!strip.tabOrder.empty()) {
        size_t ni = std::min(index, strip.tabOrder.size() - 1);
        auto no = afterhours::EntityHelper::getEntityForID(strip.tabOrder[ni]);
        if (no.valid() && no->has<Tab>()) {
            fallback = &no.asE();
            fallbackId = no->get<Tab>().sessionId;
        }
    }
    if (wasActive && fallback != nullptr) switch_to_tab(app, *fallback);
    reconcile_panes_with_tabs(strip, app, fallbackId);
}

inline void close_others(TabStripComponent& strip, AppComponent& app,
                         const std::string& keepId) {
    afterhours::Entity* keep = nullptr;
    for (auto tabId : strip.tabOrder) {
        auto opt = afterhours::EntityHelper::getEntityForID(tabId);
        if (opt.valid() && opt->has<Tab>() &&
            opt->get<Tab>().sessionId == keepId) {
            keep = &opt.asE();
            break;
        }
    }
    if (!keep) return;

    size_t write = 0;
    for (auto tabId : strip.tabOrder) {
        auto opt = afterhours::EntityHelper::getEntityForID(tabId);
        const bool preserve =
            tabId == keep->id ||
            (opt.valid() && opt->has<Tab>() && opt->get<Tab>().pinned);
        if (preserve) {
            strip.tabOrder[write++] = tabId;
        } else if (opt.valid()) {
            opt.asE().cleanup = true;
        }
    }
    strip.tabOrder.resize(write);
    switch_to_tab(app, *keep);
    reconcile_panes_with_tabs(strip, app, keepId);
}

// The web session URL for a thread — used by the tab context menu's "Copy Navi
// URL" action. Kept here (pure, testable) so the exact URL shape is asserted by
// a unit test rather than only formed inline at the call site. The base comes
// from config (web_base_url / env HANABI_WEB_BASE_URL); when unset we emit a
// host-neutral navi://session/<id> scheme so NO web host is hardcoded here.
inline std::string navi_url_for(const std::string& webBase,
                                const std::string& sessionId) {
    if (webBase.empty()) return "navi://session/" + sessionId;
    // Join without doubling a trailing slash.
    if (!webBase.empty() && webBase.back() == '/') return webBase + sessionId;
    return webBase + "/" + sessionId;
}

}  // namespace ecs::model
