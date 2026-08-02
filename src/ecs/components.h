#pragma once

#include <atomic>
#include <chrono>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "../../vendor/afterhours/src/core/base_component.h"
#include "../api/auth.h"
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
    Archived,
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

    // --- Memory-light transcript window (newest-N) ------------------------
    // Opening a thread fetches only the NEWEST N messages (see LoaderSystem's
    // kMessagesWindow) so you land at the bottom and the memory footprint stays
    // small; older messages load on demand. hasMoreOlder mirrors the loaded
    // Session's has_more_older so the RENDER side can show a "load older"
    // affordance at the top of the transcript. requestLoadOlder is a one-shot
    // flag the render side sets when the user scrolls to the top: the loader
    // services it by re-fetching the FULL transcript (no limit) and replacing
    // openSession->messages, preserving the open session. INTERIM: since the
    // backend has no working backward cursor yet, "load older" = fetch the full
    // transcript once (documented in loader_system.h).
    bool hasMoreOlder = false;
    bool requestLoadOlder = false;
    // True while a full-transcript ("load older") fetch is in flight, so the
    // render side can show a spinner and the loader doesn't double-fire.
    bool loadingOlder = false;

    // --- Live events (SSE) ------------------------------------------------
    // A subscription to the OPEN session's live activity stream. When the
    // backend supports_events(), the loader opens a subscription on thread-open
    // and tears it down on switch/close (never leaks a worker/socket). The
    // subscription's worker-thread callback ONLY flips eventRefetch (an atomic
    // "something changed, refetch newest-N") — it never touches the ECS. The
    // loader polls eventRefetch on the UI thread, debounces (kEventDebounce),
    // and re-fetches the open session's newest-N + refreshes the session list
    // so the sidebar reflects new activity. subscribedId tracks which session
    // the live handle is bound to so the loader knows when to re-subscribe.
    std::unique_ptr<api::EventSubscription> eventSub;
    std::string subscribedId;               // session eventSub is bound to
    std::atomic<bool> eventRefetch{false};  // set by worker, polled by loader
    // Timestamp of the last live refetch, for debouncing a burst of events.
    std::chrono::steady_clock::time_point lastEventRefetch{};
    // A live refetch (newest-N) in flight, polled like transcriptFuture.
    std::future<api::Result<api::Session>> liveFuture;
    bool livePending = false;
    std::string livePendingId;

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

    // Transcript: which TOOL PILES are expanded. Consecutive tool-role messages
    // collapse into one "N tool calls" summary row (like the navi website); the
    // set holds the pile keys (first tool msg id) the user has expanded. Default
    // collapsed — keeps a tool-heavy thread scannable and bounds render cost.
    std::set<std::string> expandedPiles;

    // Transcript: which long assistant messages the user has expanded. A very
    // tall body is capped at a fold height with a "Show N more lines" toggle
    // (keeps a huge pasted log/diff from dominating the pane AND bounds the
    // rendered vertex/entity count — a long message no longer blows up RAM).
    // The set holds the message ids that are expanded; default folded.
    std::set<std::string> expandedMsgs;

    // Phase K (settings/composer): settings overlay visibility + the theme
    // currently selected in the panel ("dark"/"light"/"system"); composer
    // open state + its draft text for kicking off a new task.
    bool showSettings = false;
    std::string themeChoice = "dark";
    bool composerOpen = false;
    std::string composerDraft;

    // Phase G (menu-bar): set by the frame loop when the menu-bar "New task…"
    // action fires; serviced there by opening the composer. A one-shot request
    // flag (mirrors requestOpenTab/requestToggleStar) — cleared on consume.
    bool requestNewTask = false;

    // Phase SEND: two one-shot send request flags (mirror requestNewTask),
    // serviced by LoaderSystem via the same std::async + poll pattern as
    // list/transcript. Set by the composer (kickoff) / transcript composer
    // (reply); cleared on consume.
    std::string requestKickoffPrompt;  // start a NEW session from this prompt
    std::string requestSendPrompt;     // reply into the OPEN session
    // The prompt currently being sent, for a "sending…" hint while in flight.
    std::string sendingPrompt;

    // Kickoff async state (create_session).
    std::future<api::Result<std::string>> kickoffFuture;
    bool kickoffPending = false;

    // Reply async state (send_message into selectedId).
    std::future<api::Result<api::Message>> sendFuture;
    bool sendPending = false;
    std::string sendSessionId;  // which session the in-flight reply targets

    // Phase STREAM: live token-by-token replies. When the active backend
    // supports_stream(), the transcript composer routes Send through here
    // instead of the synchronous sendFuture path above (the two are mutually
    // exclusive per turn). One-shot request flag (mirrors requestSendPrompt),
    // serviced by LoaderSystem. On start the loader appends a User bubble + an
    // empty Assistant bubble, seeds the token queue from the mock's stream
    // plan, then drains a few tokens PER FRAME into streamBuffer and rewrites
    // the live Assistant message's text. On done it finalizes + refreshes the
    // cache. Deterministic + offline for the mock: no worker thread, no timers.
    enum class StreamPhase { Idle, Thinking, Streaming, Done };
    std::string requestStreamPrompt;   // reply into the OPEN session, streamed.
    bool streamActive = false;         // a stream is in flight.
    std::string streamSessionId;       // which session the stream targets.
    StreamPhase streamPhase = StreamPhase::Idle;
    std::string streamBuffer;          // the in-progress assistant text so far.
    std::vector<std::string> streamQueue;  // remaining ordered text chunks.
    size_t streamCursor = 0;           // index of the next chunk to drain.
    // Index of the live (in-progress) Assistant message inside
    // openSession->messages, so the loader can rewrite its text each frame.
    size_t streamMsgIndex = 0;
    // The fully-assembled final Message, remembered so the loader can stamp its
    // final id/created_at when the drain completes.
    api::Message streamFinal;
    // The streamed reply is COLLECTED on a worker thread (not the UI thread) so
    // a slow-network send never beach-balls the app. The future yields the
    // ordered text chunks + the final Message; the loader polls it per frame
    // (non-blocking wait_for(0)) and only begins the visible drain once it is
    // ready. Mirrors the sendFuture/transcriptFuture async pattern.
    struct StreamCollected {
        std::vector<std::string> chunks;
        api::Message finalMsg;
        std::string error;
    };
    std::future<StreamCollected> streamCollectFuture;
    bool streamCollecting = false;      // a worker is gathering the reply.
    std::string streamPendingPrompt;    // prompt being collected (for the User bubble).
    std::string streamPendingSession;   // session the collection targets.


    // Phase AUTH (device-code login). The flow lives here as an optional so
    // the whole app is unchanged when auth is not configured (authFlow stays
    // empty, showAuth false, no overlay appears). main.cpp constructs the flow
    // with the real transport when cfg.auth_ready() && no persisted token,
    // drives poll_step each frame, and on Success persists + rebuilds the
    // client to the http backend. The overlay (auth_system.h) only renders
    // while showAuth is true.
    std::shared_ptr<api::DeviceCodeFlow> authFlow;
    bool showAuth = false;
    // The Config the flow was started with, remembered so main.cpp can rebuild
    // the live client (with the freshly-acquired token) on Success.
    api::Config authConfig;
    // Set by the overlay's "Use offline (mock)" / Cancel escape; main.cpp
    // consumes it to dismiss the overlay and keep the current (mock) client.
    bool requestAuthCancel = false;
    // Launch-perf: DeviceCodeFlow::begin() does a BLOCKING network POST (device
    // code request). Calling it in setup_app_state (pre-first-frame) put a full
    // auth-server round-trip on the windowed launch critical path — the single
    // biggest OURS cost when auth is configured but no token exists (a real
    // https server adds ~hundreds of ms to ~1s; an unreachable one blocks for
    // the whole connect timeout, ~5s). We now DEFER begin(): setup_app_state
    // sets authNeedsBegin=true (window paints immediately, overlay shows a
    // "requesting code…" state) and LoaderSystem kicks begin() on a worker via
    // authBeginFuture, exactly like the async list fetch. Nothing about the
    // flow's behavior changes — only WHEN the first request fires (off the
    // launch path).
    bool authNeedsBegin = false;
    std::future<void> authBeginFuture;
    bool authBeginPending = false;
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

    // ---- Drag-to-reorder state (set/read only by TabBarSystem) ------------
    // A press over a tab records it as a *candidate* drag (dragCandidate) with
    // the press-time cursor X and the tab's index; only once the cursor moves
    // past DRAG_THRESHOLD_PX do we promote it to an actual drag (dragging=true)
    // — so a plain click (no movement) still falls through to switch_to_tab.
    static constexpr float DRAG_THRESHOLD_PX = 4.0f;
    // invalid == "no candidate / not dragging". We use max() as the sentinel;
    // real EntityIDs start small so this never collides.
    afterhours::EntityID dragCandidate =
        std::numeric_limits<afterhours::EntityID>::max();
    bool dragging = false;      // promoted past the threshold this gesture
    float dragStartX = 0.0f;    // cursor X at press (for threshold + delta)
    float dragCurX = 0.0f;      // current cursor X while held
    size_t dragFromIndex = 0;   // tabOrder index of the tab being dragged

    bool has_drag_candidate() const {
        return dragCandidate !=
               std::numeric_limits<afterhours::EntityID>::max();
    }
    void clear_drag() {
        dragCandidate = std::numeric_limits<afterhours::EntityID>::max();
        dragging = false;
        dragStartX = dragCurX = 0.0f;
        dragFromIndex = 0;
    }
};

}  // namespace ecs
