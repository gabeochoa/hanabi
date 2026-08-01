#pragma once

#include <future>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "../../vendor/afterhours/src/core/base_component.h"
#include "../api/client.h"
#include "transcript_cache.h"

namespace ecs {

// Which pane the transcript view is showing.
enum class LoadState {
    Idle,
    Loading,
    Loaded,
    Error,
};

// Which smart view / main pane is active. Home/Blocked/Review/Starred are
// digest-style views over the thread set; Chat means a session tab is open
// and the transcript is showing.
enum class SmartView {
    Home,
    Blocked,
    Review,
    Starred,
    Chat,
};

// Singleton: owns the API client and the whole app's data + view state.
struct AppComponent : public afterhours::BaseComponent {
    std::unique_ptr<api::Client> client;
    std::string backend_label;

    // Session list.
    std::vector<api::SessionSummary> sessions;
    LoadState listState = LoadState::Idle;
    std::string listError;

    // Selected session + transcript (the active tab's thread).
    std::string selectedId;
    std::optional<api::Session> openSession;
    LoadState transcriptState = LoadState::Idle;
    std::string transcriptError;

    // Which main-pane view is showing.
    SmartView view = SmartView::Home;

    // Async fetches in flight (polled by the loader system).
    std::future<api::Result<std::vector<api::SessionSummary>>> listFuture;
    bool listPending = false;

    std::future<api::Result<api::Session>> transcriptFuture;
    bool transcriptPending = false;
    std::string transcriptPendingId;

    // Phase X: LRU transcript cache (last 20 msgs x last 5 threads). On a
    // cache HIT the loader sets openSession synchronously (no fetch, no Loading
    // flash); on a MISS it takes the async get_session path and inserts the
    // result. Bounds RAM: the 20x5 cap is the only growth point.
    model::TranscriptCache transcriptCache;

    // Set by the list system to request a (re)load; consumed by loader.
    bool requestListRefresh = true;
    // Set by the sidebar when a row is clicked (a thread to open in a tab);
    // consumed by TabFlowSystem which opens/focuses the tab.
    std::string requestOpenTab;
    // Set by the tab system when a transcript needs fetching; consumed by
    // the loader.
    std::string requestOpenId;

    // Look up a summary by id (for tab labels, row rendering).
    const api::SessionSummary* find_summary(const std::string& id) const {
        for (const auto& s : sessions)
            if (s.id == id) return &s;
        return nullptr;
    }

    // Tab set to restore once the session list has loaded (from Settings).
    std::vector<std::string> restoreTabIds;
    std::string restoreActiveId;
    bool restoreDone = false;

    // ---- Interaction state (Phases I/J/K) — pre-staged here so each system
    // owns only its own file and never races on this shared component. ----

    // Phase I (sidebar): per-folder collapse state (folder key -> collapsed),
    // a global fold-all flag, and the live search query. The sidebar system
    // reads/writes these; empty query = show everything.
    std::set<std::string> collapsedFolders;
    bool foldAllFolders = false;
    std::string searchQuery;

    // Phase I: request to toggle a thread's starred flag (set by sidebar row,
    // consumed by whichever system owns the summary mutation).
    std::string requestToggleStar;

    // Phase J (transcript): which sub-agent rows are expanded in the sub-agent
    // panel (child session id -> expanded). Sub-agent viz lives in the
    // transcript only, never the sidebar.
    std::set<std::string> expandedSubAgents;

    // Phase K (settings/composer): settings overlay visibility + the theme
    // currently selected in the panel ("dark"/"light"/"system"); composer
    // open state + its draft text for kicking off a new task.
    bool showSettings = false;
    std::string themeChoice = "dark";
    bool composerOpen = false;
    std::string composerDraft;
};

// Layout rectangles recomputed each frame from the window size.
struct LayoutComponent : public afterhours::BaseComponent {
    struct Rect { float x = 0, y = 0, width = 0, height = 0; };
    Rect sidebar;
    Rect tabStrip;    // tab strip across the top of the main pane
    Rect main;        // transcript / smart-view content (below the tab strip)
    Rect statusBar;

    // Sidebar collapse model. `collapsed` picks the thin rail width; the
    // animated width is `sidebarAnimWidth`, tweened toward the target each
    // frame with a smoothstep ease (mirrors floatinghotel's approach).
    bool sidebarCollapsed = false;
    float sidebarWidth = 280.0f;      // full, unfolded
    float sidebarRailWidth = 52.0f;   // folded thin rail
    float sidebarAnimWidth = 280.0f;  // current tweened width
    float sidebarAnimT = 1.0f;        // 0..1 progress of the current tween
    float sidebarAnimFrom = 280.0f;
    float sidebarAnimTarget = 280.0f;

    float tabStripHeight = 38.0f;
    float statusBarHeight = 26.0f;
};

// ---- Tab components (VS Code-style closable content tabs) ----
// Each open thread is a Tab entity; the focused one has an ActiveTab marker.
struct Tab : public afterhours::BaseComponent {
    std::string sessionId;   // which thread this tab shows
    std::string label;       // display title
};

struct ActiveTab : public afterhours::BaseComponent {};

// Singleton: ordered list of open tab entities.
struct TabStripComponent : public afterhours::BaseComponent {
    std::vector<afterhours::EntityID> tabOrder;
};

}  // namespace ecs
