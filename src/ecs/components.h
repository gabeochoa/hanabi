#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "../../vendor/afterhours/src/core/base_component.h"
#include "../api/auth.h"
#include "../api/client.h"
#include "../api/outbox.h"
#include "../settings.h"
#include "../search/find_memo.h"
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
    ClosePalette,
    CloseSessionSearch,
    CloseRename,
    CloseComposer,
    CloseShortcuts,
    CloseSettings,
    CloseFind,
    CloseSlashMenu,
    CloseModelPicker,
    CloseEffortPicker,
    CloseFoldPicker,
    ClearTranscript,
};

// What Up/Down mean on THIS frame. Read once by ArrowSystem (arrow_system.h)
// and resolved by what owns the keyboard, so one keystroke moves one thing:
// the caret/history in a focused field, the transcript's scroll, or a list's
// selection. None = nothing may move (an overlay is up, or no arrow).
enum class ArrowIntent {
    None,
    Palette,
    SessionSearch,
    TextField,
    Transcript,
    List,
};

// The `collapsedFolders` sentinel under which a group's "Show N more" opt-in
// is recorded. Presence = the user asked for the whole list.
//
// It lives next to the set it is a key of, and not in the sidebar that builds
// it, because two things now write it: the sidebar's expander row, and the
// stress driver's `scrollall` scenario, which exists precisely to measure what
// that expander costs. A second spelling of the same string in the driver
// would be a scenario that silently stops expanding anything the day the
// sentinel changes -- the failure #147 describes, one field over.
//
// Written into a caller-owned buffer rather than returned by value: the
// sidebar asks this on the render path of every frame, and a std::set lookup
// that allocates its key first is a malloc per frame for a string that is the
// same string it was last frame.
inline const std::string& more_key(std::string_view key, std::string& scratch) {
    scratch.assign("__more_").append(key).append("__");
    return scratch;
}

// One transcript view: everything a pane knows about the thread it is showing.
//
// WHAT THIS REPLACES. Every field below used to sit directly on AppComponent,
// written as if there could only ever be one of it -- because for most of this
// app's life there could. Split view then arrived as a SECOND transcript
// rendered from the SAME fields, by moving `openSession` out to a local,
// moving `splitSession` in, rendering, and moving both back
// (main_pane_system.h, "SWAP app.openSession<->app.splitSession"). That worked
// only for as long as the second pane wanted nothing of its own: one find bar,
// one scroll-to-bottom latch, one load-older anchor, one in-flight fetch,
// shared between two panes looking at two different places.
//
// So the state is a VALUE, and a pane owns one. The single-pane app is
// `panes[0]` and nothing about it changed; the second pane is a second value,
// not a second code path. The swap is gone, and with it the class of bug where
// the pane you were not looking at wrote into the pane you were.
//
// WHAT IS NOT HERE, AND WHY. Three kinds of state stayed on AppComponent:
//
//   * The session LIST, the tab strip, the sidebar, the overlays. One of each
//     exists, and the tab strip is deliberately global -- one strip, and it
//     opens into the focused pane.
//   * Fold state (`expandedPiles`, `expandedMsgs`, `expandedThinking`). Keyed
//     by MESSAGE id, not by pane: "I opened this tool call" is a fact about
//     the message, and two panes showing one thread agreeing about it is the
//     behaviour you want rather than a bug.
//   * The composer's draft, its history and the scroll/follow latches. Already
//     per THREAD, in the bounded LRU at ecs/pane_state.h -- which is keyed by
//     pane AND thread, so two panes on one thread keep their own.
struct Pane {
    // The thread this pane is showing, and its transcript.
    std::string selectedId;
    std::optional<api::Session> openSession;
    LoadState transcriptState = LoadState::Idle;
    std::string transcriptError;

    // Set by the tab system (or a split-open request) when a transcript
    // needs fetching; consumed by the loader.
    std::string requestOpenId;

    // Set to a session id ONLY when a thread is opened for the FIRST time (a
    // new tab) — NOT when switching to an already-open tab. The transcript
    // render pins the scroll to the bottom (newest message) while this matches
    // the open thread, then clears it once the content is laid out and pinned,
    // so a freshly-opened thread lands at the bottom exactly once. Switching
    // back to an existing tab leaves this empty, so the user's scroll position
    // in that tab is preserved.
    std::string scrollBottomPending;

    std::future<api::Result<api::Session>> transcriptFuture;
    bool transcriptPending = false;
    std::string transcriptPendingId;

    // ==== Feature #1: never-beachball thread switch ======================
    // Per pane: two panes can each be mid-switch, and one spinner flag
    // between them showed the wrong pane's spinner.
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

    // ==== Find in conversation (Cmd+F) ===================================
    // A long thread is unsearchable without this: the sidebar's search finds
    // THREADS, and nothing finds a line inside the one you are reading.
    bool findOpen = false;
    //
    // Per pane, not per app: the two panes are two places in (possibly) two
    // conversations, and one tally counting both would name a match the reader
    // cannot see.
    std::string findQuery;
    int findIndex = 0;    // which match is current, 0-based
    int findCount = 0;    // matches on the last rendered frame (for "3 of 12")
    // Set when the current match changes; the transcript scrolls it into view
    // on the next frame it lays out, then clears this.
    bool findScrollPending = false;
    std::uint64_t transcriptVersion = 1;
    hanabi::find_memo::Memo findMemo;

    void note_transcript_change() {
        ++transcriptVersion;
        if (transcriptVersion == 0) transcriptVersion = 1;
    }

    // Is this pane showing a thread at all?
    bool has_thread() const { return openSession.has_value(); }
};

// Singleton: owns the API client and the whole app's data + view state.
struct AppComponent : public afterhours::BaseComponent {
    std::unique_ptr<api::Client> client;
    std::string backend_label;
    // Web/session URL base for the "Copy URL" tab action (from config
    // web_base_url / env HANABI_WEB_BASE_URL). Empty => host-neutral
    // navi://session/<id>. Never used as an API endpoint.
    std::string webBaseUrl;
    // Where a work-tracker id in a message points (from config
    // tracker_base_url / env HANABI_TRACKER_BASE_URL). Empty => ids in the
    // transcript are prose, not links.
    std::string trackerBaseUrl;

    // Session list.
    std::vector<api::SessionSummary> sessions;
    LoadState listState = LoadState::Idle;
    std::string listError;

    // ==== The panes ========================================================
    // Two transcript views. `panes[0]` is the one that has always existed;
    // `panes[1]` is the right-hand pane and is live only while `splitOpen`.
    // `focusedPane` is the one the keyboard, the composer and the tab strip act
    // on -- exactly one, always a valid index.
    //
    // An ARRAY rather than two named members, so every site that services a
    // pane is a loop and cannot service one and forget the other. That
    // asymmetry is exactly what the old split block was: a second, simpler,
    // subtly different copy of the first pane's plumbing.
    std::array<Pane, 2> panes;
    int focusedPane = 0;
    // Is the second pane showing? Closed == single pane, and `panes[1]` is left
    // alone -- its thread is remembered, so reopening the split is free and it
    // is what gets persisted.
    bool splitOpen = false;
    // Where the divider sits, as the LEFT pane's share of the pane width.
    // Clamped to [kSplitMinRatio, kSplitMaxRatio] wherever it is written.
    float splitRatio = 0.5f;
    // A divider drag in flight. On the app rather than in a render-local static
    // for the reason every gesture in this app is: the widget is rebuilt every
    // frame, so the gesture cannot live in it.
    bool splitDragging = false;

    // One-shot: open this thread in the pane that is NOT focused, splitting if
    // it is not already split. Empty = nothing asked.
    std::string requestSplitOpen;
    // One-shot: close the split (back to single pane).
    bool requestSplitClose = false;
    // One-shot: toggle the split. The chord, the palette row and the menu item
    // all set this rather than each deciding for itself what a toggle means.
    bool requestSplitToggle = false;

    // The divider's limits live in settings.h next to clamp_split_ratio: the
    // drag clamps as it writes and the restore clamps as it reads, and two
    // copies of a limit drift.

    Pane& pane() { return panes[static_cast<size_t>(focusedPane)]; }
    const Pane& pane() const {
        return panes[static_cast<size_t>(focusedPane)];
    }
    Pane& other_pane() { return panes[focusedPane == 0 ? 1 : 0]; }
    // How many panes are actually showing: one, or two when split.
    size_t active_pane_count() const { return splitOpen ? 2u : 1u; }

    // Which main-pane view is showing.
    SmartView view = SmartView::Home;

    // Async fetches in flight (polled by the loader system).
    std::future<api::Result<std::vector<api::SessionSummary>>> listFuture;
    bool listPending = false;

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
    // Look up a summary by id (for tab labels, row rendering).
    const api::SessionSummary* find_summary(const std::string& id) const {
        for (const auto& s : sessions)
            if (s.id == id) return &s;
        return nullptr;
    }

    // Tab set to restore once the session list has loaded (from Settings).
    std::vector<std::string> restoreTabIds;
    // What each pane was showing at quit. Serviced by the same restore pass as
    // the tabs, and for the same reason: a thread has to be in the session
    // list before a pane can be told to open it.
    std::array<std::string, 2> restoreSplitIds;
    std::vector<std::string> restorePinnedIds;
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

    // Request to flip a thread's machine-local archive overlay, set by the row
    // menu and applied by the sidebar — the same one-writer arrangement the
    // star toggle uses, so the sessions vector still has exactly one mutator.
    std::string requestToggleArchive;
    // Request to silence (or un-silence) a thread on this machine. Same
    // one-writer arrangement as the star: the sidebar owns the sessions vector.
    std::string requestToggleMute;

    // ==== Sidebar manual row order (drag-to-reorder) ======================
    // Folder key -> that folder's PINNED PREFIX of session ids. What the order
    // MEANS, and why it is not a re-sort, is written up on
    // ecs::model::apply_row_order. Seeded from Settings on the first render and
    // written straight back on every drop, so it is machine-local and durable
    // in the same way mute and star are.
    std::map<std::string, std::vector<std::string>> rowOrder;
    bool rowOrderSeeded = false;
    // One-shot: forget a folder's manual order (the row menu's "Reset order").
    std::string requestResetRowOrder;

    // The row drag in flight. Written only by the sidebar.
    struct RowDrag {
        std::string sessionId;   // empty == nothing is being dragged
        std::string folderKey;   // a drag never leaves its own folder
        size_t fromIndex = 0;    // where the row sits in the rendered band
        size_t dropIndex = 0;    // where it would land, recomputed each frame
        // Screen y of the drop line, taken from the rendered band's geometry
        // so the line the user sees and the slot they get are one number.
        float lineY = 0.0f;
        bool live = false;       // past the press-drag threshold
        // The group's rows in rendered order, captured while they render; the
        // drop rewrites this list rather than re-deriving it. Bounded by the
        // group's render cap, not by the folder's size.
        std::vector<std::string> visibleIds;
    };
    RowDrag rowDrag;

    // Transcript: which TOOL PILES are expanded. Consecutive tool-role messages
    // collapse into one "N tool calls" summary row (like the navi website); the
    // set holds the pile keys (first tool msg id) the user has expanded. Default
    // collapsed — keeps a tool-heavy thread scannable and bounds render cost.
    std::set<std::string> expandedPiles;

    // Transcript: which tool rows the reader has explicitly CLOSED, against a
    // fold mode that would otherwise open them. The two sets are overrides on
    // top of the per-session mode (src/ui/fold_menu.h): closed wins, then
    // opened, then the mode's own answer. Picking a mode clears both, so
    // "Expand all" means all and not "all except the four I closed".
    std::set<std::string> collapsedPiles;

    // Transcript: which long assistant messages the user has expanded. A very
    // tall body is capped at a fold height with a "Show N more lines" toggle
    // (keeps a huge pasted log/diff from dominating the pane AND bounds the
    // rendered vertex/entity count — a long message no longer blows up RAM).
    // The set holds the message ids that are expanded; default folded.
    std::set<std::string> expandedMsgs;
    std::uint64_t findFoldVersion = 1;

    // Transcript: which THINKING blocks the reader has opened. Reasoning is
    // real content but it is not the answer, so it arrives folded and is
    // keyed by message id (index as a fallback for an id-less message).
    std::set<std::string> expandedThinking;

    // Home: which shelves are folded shut, by shelf KEY (not label). Seeded
    // from settings at startup and written back on every toggle, so a folded
    // shelf survives relaunch.
    std::set<std::string> collapsedShelves;

    // Phase K (settings/composer): settings overlay visibility + the theme
    // currently selected in the panel ("dark"/"light"/"system"); composer
    // open state + its draft text for kicking off a new task.
    bool showSettings = false;
    // The keyboard-shortcut reference (Cmd+/). Every binding in this app is
    // otherwise invisible.
    bool showShortcuts = false;

    // The command palette (Cmd+K): a query and a cursor over the rows it
    // ranks. Its state lives here rather than in the system's own locals
    // because Esc and the arrows each have exactly one owner, and both have
    // to rank the palette against everything else that is open.
    bool paletteOpen = false;
    std::string paletteQuery;
    int paletteIndex = 0;

    // Search across threads (Cmd+Shift+F). Same shape as the palette, over a
    // different corpus: the sessions' titles and previews plus whatever
    // transcripts this machine holds (session_search_system.h).
    bool sessionSearchOpen = false;
    std::string sessionSearchQuery;
    int sessionSearchIndex = 0;

    // One-shot: put the caret back in the composer next frame. The find bar
    // owns a text field of its own, so closing it left focus on an element
    // that no longer exists and the composer's own keys (Esc to clear, the
    // history walk) did nothing until you clicked back in.
    bool refocusComposer = false;
    // Resolved by EscapeSystem at the top of the frame; read by whichever site
    // owns that intent. Reset to None every frame.
    EscapeIntent escape = EscapeIntent::None;
    // This frame's arrow intent + its direction (-1 up, +1 down, 0 none).
    ArrowIntent arrow = ArrowIntent::None;
    int arrowDelta = 0;
    // The list row the keyboard is on (a session id), for the digest lists in
    // Home and the smart views. Empty = no cursor; the first arrow press puts
    // one on the nearest end of the list.
    std::string listCursorId;
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
    // Whether Cmd was held when that Enter landed. A FACT the listener
    // observed, not a decision it took: whether the chord sends depends on the
    // send-key setting, which the user can change between one Enter and the
    // next, so only the per-frame router may read the setting (see
    // hanabi::enter_sends in settings.h).
    bool composerSubmitWithCmd = false;
    // The prompt currently being sent, for a "sending…" hint while in flight.
    std::string sendingPrompt;

    // Composer history (Up/Down walk) and the half-typed draft now live
    // TOGETHER in ecs::model::pane_states() — one bounded LRU keyed by session
    // id, instead of this map plus four function-local statics in
    // main_pane_system.h, none of which was ever pruned. See ecs/pane_state.h
    // for what the bound is and what it costs.

    // Images pasted or dropped onto the composer, in the order they arrived.
    // A PATH each, never bytes: the transcript's inline-image cache turns a
    // path into a texture, so the chip's thumbnail costs nothing new.
    //
    // NOT keyed by thread, unlike the draft above. hanabi cannot SEND an image
    // on any backend it speaks (see the note over the chips row in
    // main_pane_system.h), so an attachment never moves with a message and
    // there is no per-thread lifetime to respect yet. When a send path exists
    // this becomes a per-thread slot the way the draft is.
    struct Attachment {
        // Absolute path to the image on this machine.
        std::string path;
        // What the chip calls it — the file's base name.
        std::string name;
    };
    std::vector<Attachment> composerAttachments;
    // The most images the composer will hold. Not an arbitrary number: it is
    // the cap the orchestrator's own message route enforces (five per
    // message), so hanabi never accumulates a set that could not be sent even
    // once a send path exists.
    static constexpr size_t kMaxAttachments = 5;

    // Slash-command menu (the dropdown a "/" draft raises over the composer).
    // Its open state lives on the app rather than in the composer's own
    // locals because Esc has exactly one owner, and that owner has to rank
    // this menu against every other dismissable thing (escape_system.h).
    bool slashMenuOpen = false;
    int slashMenuIndex = 0;
    // The draft Esc dismissed the menu for. The menu stays shut until the
    // draft changes, so Esc is not undone by the very next frame re-deriving
    // "this text starts with a slash".
    std::string slashDismissedFor;
    // What the router said about the last command it was handed — shown in
    // the composer strip. Cleared when the field is typed in again.
    std::string slashNotice;
    // The composer strip's model picker. One flag: the popover is a list of
    // models and a click, with no request in flight behind it (choosing
    // writes the default-model preference the settings sheet also writes).
    bool modelPopoverOpen = false;
    // The composer strip's effort picker. One flag: the popover is a list of
    // levels and a click, with nothing in flight behind it.
    bool effortPopoverOpen = false;
    // The composer strip's tool-fold picker (Fold all / Expand all / Auto).
    bool foldPopoverOpen = false;

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
        const Pane& p = pane();
        if (!p.openSession) return false;
        return p.openSession->summary.state == api::ThreadState::Running;
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

    // ==== The local-first OUTBOX's read side =============================
    // Every send is written to disk before it goes out (disk_cache::outbox_add)
    // so a crash cannot lose the user's words. For a long time nothing read
    // that back: outbox_list had no caller, so a failed send stayed on disk
    // forever and a crash mid-send restored nothing (docs/COMMIT_AUDIT.md CB3).
    //
    // This is the state the loader's drive_outbox() needs. The POLICY -- which
    // entry next, how long to wait -- is api::outbox::Retry, which is pure and
    // unit-tested; what lives here is only which retry is currently occupying
    // the one send slot, and the flag that stops a retry writing a SECOND copy
    // of the same prompt into the store it came out of.
    api::outbox::Retry outboxRetry;
    bool outboxRestored = false;      // the startup enumeration has run
    bool outboxSuppressAdd = false;   // this dispatch came FROM the outbox
    std::string outboxRetryId;        // the retry occupying the send slot
    std::string outboxRetryPrompt;

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
    // Test-only: hold the live turn in whatever phase it was put in, instead
    // of letting the loader complete it. A demo stream has no chunks, so the
    // loader declares it Done on the very next tick -- which is why the
    // "thinking" state was unphotographable and unscriptable for months. Set
    // only by the headless knob in main.cpp; false in every real run.
    bool streamDemoHold = false;
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

    // ==== Session rename (durable echo, never local optimism) =============
    // A right-click on a sidebar row opens rowMenu at the cursor; picking
    // "Rename…" opens the modal on renameSessionId with the current title in
    // renameDraft. Confirming parks the one-shot request for the loader, which
    // sends it and waits for the server's `session_renamed` echo — the title in
    // the sidebar and on the tab changes only when that echo lands. A refusal
    // comes back in renameError and the modal stays open with the text intact.
    bool rowMenuOpen = false;
    std::string rowMenuSessionId;
    float rowMenuX = 0.0f;
    float rowMenuY = 0.0f;
    void close_row_menu() {
        rowMenuOpen = false;
        rowMenuSessionId.clear();
    }

    // ==== Toast (a transient bar with one action) ==========================
    // Archive raises one so the action can be taken back: filing a thread away
    // is easy to do by accident, and the Archived view is not where the user is
    // looking. Never persisted — a toast that outlived the launch that raised
    // it would offer to undo something already forgotten.
    //
    // Star and mute raise one for the same reason from the other direction:
    // both are one small click on a hovered row, neither moves the thread
    // anywhere the eye would notice, and a mute in particular is invisible
    // until the notification it swallowed never arrives.
    //
    // Which toggle Undo re-runs is carried HERE rather than inferred from the
    // message text: the bar knew only how to unarchive, so a mute toast wired
    // to the same button would have archived the thread instead of unmuting it.
    enum class ToastUndo { None, Archive, Mute, Star };
    static constexpr float kToastSeconds = 10.0f;
    std::string toastMessage;
    std::string toastUndoSessionId;  // empty = no Undo affordance
    ToastUndo toastUndoKind = ToastUndo::None;
    float toastSecondsLeft = 0.0f;
    void raise_toast(std::string message, std::string undoSessionId,
                     ToastUndo kind) {
        toastMessage = std::move(message);
        toastUndoSessionId = std::move(undoSessionId);
        // An id with no action behind it would paint an Undo button that does
        // nothing, so the two travel together or not at all.
        toastUndoKind = toastUndoSessionId.empty() ? ToastUndo::None : kind;
        if (toastUndoKind == ToastUndo::None) toastUndoSessionId.clear();
        toastSecondsLeft = kToastSeconds;
    }
    void dismiss_toast() {
        toastMessage.clear();
        toastUndoSessionId.clear();
        toastUndoKind = ToastUndo::None;
        toastSecondsLeft = 0.0f;
    }

    bool renameOpen = false;
    std::string renameSessionId;
    std::string renameDraft;
    std::string renameError;
    // Set by Return in the field (the listener cannot decide anything — see the
    // composerSubmit note above); routed by the modal on the next frame.
    bool renameSubmit = false;
    std::string requestRenameId;     // one-shot: what the loader should send
    std::string requestRenameTitle;
    bool renamePending = false;      // in flight; the modal shows a spinner
    std::string renameInFlightId;
    std::future<api::Result<std::string>> renameFuture;

    // Apply a settled title everywhere it shows: the session list (sidebar
    // rows) and the open transcript. Tab captions are derived from the summary
    // every frame, so they follow with no work here.
    void apply_renamed_title(const std::string& id, const std::string& title) {
        for (auto& s : sessions)
            if (s.id == id) s.title = title;
        for (Pane& p : panes)
            if (p.openSession && p.openSession->summary.id == id)
                p.openSession->summary.title = title;
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

    // The declared compaction budget (api::Config::context_budget_tokens), for
    // a backend that reports none per session. Zero = undeclared, so the
    // composer shows a plain token figure instead of a bar.
    int64_t configuredContextBudget = 0;
};

// Layout rectangles recomputed each frame from the window size.
struct LayoutComponent : public afterhours::BaseComponent {
    struct Rect { float x = 0, y = 0, width = 0, height = 0; };
    Rect sidebar;
    Rect tabStrip;    // tab strip across the top of the main pane
    Rect main;        // transcript / smart-view content (below the tab strip)
    Rect composer;    // chat input strip, pinned at the main pane's floor

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

    // The strip is taller than the tabs it holds: the tabs sit at its BOTTOM
    // edge and the band above them is the window-drag / traffic-light zone the
    // frameless window leaves clear (Puffin: tabs occupy y=32..65 of a 67px
    // strip, and the content plane starts at 67). tabStripTabHeight is the tab
    // itself; the difference is the clear band.
    float tabStripHeight = 67.0f;
    float tabStripTabHeight = 34.0f;
    float composerHeight = 98.0f;  // chat input strip height (0 hides it)
};

// ---- Tab components (VS Code-style closable content tabs) ----
// Each open thread is a Tab entity; the focused one has an ActiveTab marker.
struct Tab : public afterhours::BaseComponent {
    std::string sessionId;   // which thread this tab shows
    std::string label;       // display title
    // A KEPT tab is one the user committed to; a PREVIEW tab is a look. There
    // is at most one preview tab at a time — clicking another sidebar row
    // reuses it rather than piling a tab up per glance — and only a second
    // click on the same thread keeps it. A preview tab holds no live
    // subscription (so its transcript is frozen at the moment it was opened)
    // and is not restored on the next launch. Default true: anything that opens
    // a tab without saying otherwise is an explicit act, not a glance.
    bool keptOpen = true;
    // A PINNED tab survives "close others", restores on launch, and carries a
    // pin glyph before its title. Pinning is a deliberate act from the tab's
    // context menu; nothing pins a tab implicitly.
    bool pinned = false;
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

// Is a modal sheet covering the app? Keyboard navigation behind one moves
// something the reader cannot see, so every key owner asks this first.
inline bool overlay_up(const AppComponent& app) {
    return app.renameOpen || app.composerOpen || app.showShortcuts ||
           app.showSettings || app.showAuth;
}

}  // namespace ecs
