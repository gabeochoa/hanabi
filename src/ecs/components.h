#pragma once

#include <atomic>
#include <chrono>
#include <future>
#include <limits>
#include <map>
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

// What Esc means on THIS frame. Four systems used to poll the key
// independently, so one press closed the overlay AND dropped the transcript
// selection AND wiped the composer draft. EscapeSystem reads the key once and
// resolves it to a single intent (topmost thing wins); every other site reads
// the intent instead of the key.
enum class EscapeIntent {
    None,
    CloseComposer,
    CloseShortcuts,
    CloseSettings,
    CloseFind,
    ClearTranscript,
};

// Singleton: owns the API client and the whole app's data + view state.
struct AppComponent : public afterhours::BaseComponent {
    std::unique_ptr<api::Client> client;
    std::string backend_label;
    // Web/session URL base for the "Copy URL" tab action (from config
    // web_base_url / env HANABI_WEB_BASE_URL). Empty => host-neutral
    // navi://session/<id>. Never used as an API endpoint.
    std::string webBaseUrl;

    // Session list.
    std::vector<api::SessionSummary> sessions;
    LoadState listState = LoadState::Idle;
    std::string listError;

    // Selected session + transcript (the active tab's thread).
    std::string selectedId;
    std::optional<api::Session> openSession;
    LoadState transcriptState = LoadState::Idle;
    std::string transcriptError;

    // --- Split view (I2): an optional SECOND transcript shown to the RIGHT of
    // the primary one. splitSessionId is the right pane's thread; splitSession
    // is its transcript (served from the LRU cache on open, async-filled on a
    // miss via requestSplitOpen). Empty splitSessionId == not split (single
    // pane). Snapping a tab to the right edge sets splitSessionId; closing the
    // split clears it. The primary pane keeps using openSession as before, so
    // single-pane behavior is unchanged when splitSessionId is empty.
    std::string splitSessionId;
    std::optional<api::Session> splitSession;
    // One-shot: sidebar/tab asks to open a thread in the RIGHT split pane.
    std::string requestSplitOpen;
    // One-shot: close the split (back to single pane).
    bool requestSplitClose = false;
    std::future<api::Result<api::Session>> splitFuture;
    bool splitPending = false;
    std::string splitPendingId;

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
    // Scroll-anchor preservation for load-older: when older messages are
    // prepended, the content grows ABOVE the viewport, so the scroll offset
    // must be bumped by the added-above height to keep the user's view on the
    // same message (instead of snapping to the newly-loaded oldest). The loader
    // records the message COUNT + total content height at request time; the
    // render side, on the frame the new (larger) content is laid out, adds the
    // height delta to scroll_offset.y once, then clears the pending anchor.
    // anchorPending is the session id awaiting the offset bump (empty = none).
    std::string anchorPending;
    size_t anchorPrevMsgCount = 0;   // message count before the older load

    // --- Live events (SSE) — MULTI-thread background subscriptions --------
    // When the backend supports_events(), the loader keeps a POOL of live
    // subscriptions — one per OPEN TAB (not just the focused thread) — so every
    // open thread keeps live-reading in the background and its fresh transcript
    // is written straight to the disk cache. Switching to a tab then shows the
    // already-fresh content instantly instead of waiting to fetch once you land
    // on it. Each pool entry owns its subscription handle + its own atomic
    // dirty flag (flipped by that thread's SSE worker) + last-event stamp; the
    // loader polls the flags on the UI thread, debounces, and refetches the
    // dirty thread(s) on worker futures. Subscriptions are opened when a tab
    // appears and reaped (off the UI thread) when its tab closes.
    struct LiveSub {
        std::unique_ptr<api::EventSubscription> sub;
        std::shared_ptr<std::atomic<bool>> dirty =
            std::make_shared<std::atomic<bool>>(false);
        std::chrono::steady_clock::time_point lastRefetch{};
        bool pending = false;  // a background refetch for this id is in flight
        std::future<api::Result<api::Session>> future;
    };
    std::map<std::string, LiveSub> liveSubs;  // session id -> live subscription
    // Steady-clock ms of the last live event across ANY subscription (0=none),
    // set thread-safely by every sink; the status bar flashes a "live" dot from
    // it. Atomic so workers can write it.
    std::atomic<long long> lastEventMs{0};
    // Whether the OPEN thread currently has a live subscription (drives the
    // "live" status indicator's connected state). Recomputed each frame.
    bool openThreadLive = false;
    // Legacy single-sub fields kept for the load-older / live refetch of the
    // FOCUSED thread (the immediate-swap path). liveFuture/livePending drive a
    // fetch whose result swaps into openSession.
    std::future<api::Result<api::Session>> liveFuture;
    bool livePending = false;

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

    // Set to a session id ONLY when a thread is opened for the FIRST time (a
    // new tab) — NOT when switching to an already-open tab. The transcript
    // render pins the scroll to the bottom (newest message) while this matches
    // the open thread, then clears it once the content is laid out and pinned,
    // so a freshly-opened thread lands at the bottom exactly once. Switching
    // back to an existing tab leaves this empty, so the user's scroll position
    // in that tab is preserved.
    std::string scrollBottomPending;

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
    // One-time guard: folders start COLLAPSED by default (Gabe — subthreads
    // hidden until you expand a folder). The first render that sees folders
    // seeds every folder key into collapsedFolders, then sets this so the user's
    // subsequent expand/collapse choices are respected for the session.
    bool foldersDefaultCollapsedSeeded = false;
    bool foldAllFolders = false;
    std::string searchQuery;

    // Phase I: request to toggle a thread's starred flag (set by sidebar row,
    // consumed by whichever system owns the summary mutation).
    std::string requestToggleStar;

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
    // The keyboard-shortcut reference (Cmd+/). Every binding in this app is
    // otherwise invisible.
    bool showShortcuts = false;

    // ==== Find in conversation (Cmd+F) ===================================
    // A long thread is unsearchable without this: the sidebar's search finds
    // THREADS, and nothing finds a line inside the one you are reading.
    bool findOpen = false;
    // Resolved by EscapeSystem at the top of the frame; read by whichever site
    // owns that intent. Reset to None every frame.
    EscapeIntent escape = EscapeIntent::None;
    std::string findQuery;
    int findIndex = 0;    // which match is current, 0-based
    int findCount = 0;    // matches on the last rendered frame (for "3 of 12")
    // Set when the current match changes; the transcript scrolls it into view
    // on the next frame it lays out, then clears this.
    bool findScrollPending = false;

    // Test/screenshot only: a run of text to pre-select in the transcript, so
    // the selection band can be captured without a live drag. Empty normally.
    std::string selectDemo;
    std::string themeChoice = "dark";
    bool composerOpen = false;
    std::string composerDraft;

    // Phase G (menu-bar): set by the frame loop when the menu-bar "New task…"
    // action fires; serviced there by opening the composer. A one-shot request
    // flag (mirrors requestOpenTab/requestToggleStar) — cleared on consume.
    bool requestNewTask = false;
    // Optional text a welcome-screen suggestion chip seeds into the new-task
    // composer draft (consumed once by render_composer). Empty = no seed.
    std::string welcomeSeed;

    // Phase SEND: two one-shot send request flags (mirror requestNewTask),
    // serviced by LoaderSystem via the same std::async + poll pattern as
    // list/transcript. Set by the composer (kickoff) / transcript composer
    // (reply); cleared on consume.
    std::string requestKickoffPrompt;  // start a NEW session from this prompt
    std::string requestSendPrompt;     // reply into the OPEN session
    // What Enter in the composer submitted, before it has been routed. The
    // text-input listener is attached once and lives for the entity's whole
    // life, so it cannot be the thing that decides between kickoff and reply —
    // it would decide with whatever was true on the frame it was attached.
    // It parks the text here and the composer, which recomputes the mode every
    // frame, routes it exactly as the Send button does.
    std::string composerSubmit;
    // The prompt currently being sent, for a "sending…" hint while in flight.
    std::string sendingPrompt;

    // Kickoff async state (create_session).
    std::future<api::Result<std::string>> kickoffFuture;
    bool kickoffPending = false;

    // ==== Agent steering (Phase STEER) ====================================
    // When a message is sent into the OPEN thread while that thread's agent is
    // CURRENTLY RUNNING, it should STEER (interrupt/redirect) the in-flight
    // turn instead of starting a fresh one. This is the single decision point
    // the loader (and the composer's Send-vs-Steer relabel) reads: true iff
    //   (a) the backend can steer (client->supports_steer()), AND
    //   (b) the open thread's state is Running.
    // When false the loader takes the normal send/stream path (no behavior
    // change). Kept as a method (not a stored flag) so it always reflects the
    // live client + open-session state with no staleness.
    bool should_steer_open() const {
        if (!client || !client->supports_steer()) return false;
        if (!openSession) return false;
        return openSession->summary.state == api::ThreadState::Running;
    }

    // Steer async state (steer() into selectedId). Parallels the reply
    // (send_message) path: one future + a pending flag + the target id.
    std::future<api::Result<api::Message>> steerFuture;
    bool steerPending = false;
    std::string steerSessionId;  // which session the in-flight steer targets

    // Reply async state (send_message into selectedId).
    std::future<api::Result<api::Message>> sendFuture;
    bool sendPending = false;
    std::string sendSessionId;  // which session the in-flight reply targets
    // The id of the OPTIMISTIC user bubble appended at send-dispatch (with
    // sync=Persisting). The resolver flips its SyncState to Synced/Failed by
    // matching this id. Empty when no optimistic send is in flight.
    std::string optimisticSendId;

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
    // Wall-clock (seconds) when the current stream/thinking turn began, so the
    // live "thinking" indicator can show an elapsed timer ("Thinking… · 32s").
    // 0 = not started. Set when a stream/steer kicks off; cleared on Done/Idle.
    int64_t streamStartedAt = 0;
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

    // ==== Feature #1: never-beachball thread switch ======================
    // The id of the thread whose transcript is CURRENTLY loading (async fetch
    // and/or async disk-cache read in flight), or empty when nothing is
    // loading. The RENDER side reads this to show a per-thread spinner: show
    // the spinner when transcriptState == Loading (already the signal) AND/OR
    // when transcriptLoadingId == the tab's session id. It is set IMMEDIATELY
    // (synchronously, ~0 cost) the instant a switch is requested, BEFORE any
    // heavy work, so the pane can paint a spinner on the very next frame while
    // the worker thread does the disk read + JSON parse + windowing off the UI
    // thread. Cleared when the fetch/disk result is applied (or dropped because
    // the user switched away again).
    std::string transcriptLoadingId;

    // A worker-thread disk-cache read of a transcript, polled like
    // transcriptFuture. On a MISS the loader USED to call
    // disk_cache::load_transcript() synchronously on the UI thread — opening +
    // JSON-parsing an up-to-690-message file, the exact beachball Gabe hit.
    // Now that read runs on this future's worker thread; the loader polls it
    // (non-blocking wait_for(0)) and paints the stale copy when it lands, still
    // ahead of (or alongside) the network revalidate. std::optional inside the
    // Result: nullopt == cache miss (no stale copy to paint).
    std::future<std::optional<api::Session>> diskReadFuture;
    bool diskReadPending = false;
    std::string diskReadId;  // the thread the disk read targets

    // ==== Feature #3: composer message queue =============================
    // When the user sends a message into a session that ALREADY has a reply /
    // stream in flight, we QUEUE it (FIFO) instead of dropping or interleaving.
    // The loader drains ONE queued send per session as soon as that session's
    // current send/stream completes, preserving order. The composer (render)
    // side: (a) pushes onto this via enqueue_send() instead of setting
    // requestSendPrompt directly when a send is already pending for the target;
    // (b) reads pending_send_count(id) to show "N queued" + sending_for(id) for
    // a per-session "sending…" indicator. Keyed by session id so each thread's
    // queue is independent and drafts-per-session are unaffected.
    struct PendingSend {
        std::string sessionId;
        std::string prompt;
    };
    std::vector<PendingSend> pendingSendQueue;

    // True while ANY send/stream is in flight for `id` (so the composer can
    // disable/queue and show a spinner). Covers the synchronous reply path
    // (sendPending), the collect+drain stream path (streamCollecting /
    // streamActive), all scoped to the matching session id.
    bool sending_for(const std::string& id) const {
        return (sendPending && sendSessionId == id) ||
               (steerPending && steerSessionId == id) ||
               (streamCollecting && streamPendingSession == id) ||
               (streamActive && streamSessionId == id);
    }

    // How many messages are queued (not yet sent) for `id`.
    size_t pending_send_count(const std::string& id) const {
        size_t n = 0;
        for (const auto& p : pendingSendQueue)
            if (p.sessionId == id) ++n;
        return n;
    }

    // Enqueue a user send for `id`. The composer calls this for EVERY send; the
    // loader decides whether to dispatch immediately (nothing in flight) or
    // hold it in the queue. Ordered per session (push_back = FIFO).
    void enqueue_send(const std::string& id, const std::string& prompt) {
        pendingSendQueue.push_back(PendingSend{id, prompt});
    }

    // ==== Feature #4: settings read from the API =========================
    // The user/account settings fetched from the backend so the app can verify
    // it is set up correctly. requestSettings kicks the async fetch (loader);
    // settings holds the last result; settingsState tracks the fetch. The
    // settings screen (render, separate stream) reads settings + settingsState.
    api::UserSettings settings;
    LoadState settingsState = LoadState::Idle;
    std::string settingsError;
    bool requestSettings = false;  // one-shot: set to trigger a fetch
    std::future<api::Result<api::UserSettings>> settingsFuture;
    bool settingsPending = false;
};

// Layout rectangles recomputed each frame from the window size.
struct LayoutComponent : public afterhours::BaseComponent {
    struct Rect { float x = 0, y = 0, width = 0, height = 0; };
    Rect sidebar;
    Rect tabStrip;    // tab strip across the top of the main pane
    Rect main;        // transcript / smart-view content (below the tab strip)
    Rect composer;    // chat input strip, pinned above the status bar
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
    float composerHeight = 92.0f;  // chat input strip height (0 hides it)
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

    // ---- Chrome-style overflow scroll (set/read only by TabBarSystem) -----
    // When more tabs are open than fit at their min width, the strip stops
    // shrinking the tabs and scrolls instead. scrollX shifts every tab's
    // x-position left; it's clamped each frame to [0, maxScroll] by the
    // pure model (model::clamp_scroll / compute_max_scroll). A horizontal
    // wheel / shift+wheel over the strip adjusts it; selecting an off-screen
    // tab scrolls it into view (model::scroll_to_show).
    float scrollX = 0.0f;

    // ---- Right-click context menu state (set/read only by TabBarSystem) ---
    // A right-click on a tab opens a small overlay menu anchored at the cursor
    // with per-tab actions ("Copy Navi URL", "Close others"). Dismissed on
    // click-away or after an action. menuTabId is the tab the menu acts on.
    bool menuOpen = false;
    afterhours::EntityID menuTabId =
        std::numeric_limits<afterhours::EntityID>::max();
    float menuX = 0.0f;         // cursor x at right-click (menu top-left)
    float menuY = 0.0f;         // cursor y at right-click
    void close_menu() {
        menuOpen = false;
        menuTabId = std::numeric_limits<afterhours::EntityID>::max();
    }
};

}  // namespace ecs
