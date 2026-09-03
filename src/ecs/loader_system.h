#pragma once

// Drives async data loading. Reads request flags on AppComponent, launches
// std::async fetches against the Client, and polls the futures each frame,
// writing results back into AppComponent. Keeps the UI thread responsive.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <future>

#include "../settings.h"
#include "../api/disk_cache.h"
#include "load_older_model.h"
#include "ui_imports.h"

namespace ecs {

struct LoaderSystem : afterhours::System<AppComponent> {
    // MEMORY-LIGHT window: opening a thread fetches only the newest N messages
    // (you land at the bottom; older ones load on demand). ~40 keeps the
    // transcript's RAM/vertex cost bounded while still showing plenty of recent
    // context. Passed to get_session(id, N); the mock + http adapter both honor
    // it (http appends "?limit=N", mock returns the last N).
    static constexpr int kMessagesWindow = 40;

    // Debounce for LIVE (SSE) refetches: coalesce a burst of events into at
    // most one newest-N refetch per interval so we don't hammer the backend.
    static constexpr std::chrono::milliseconds kEventDebounce{1000};

    // The on-disk cache is ONLY for a real (network) backend. The mock is
    // already instant + deterministic, and its sample data must never be
    // polluted by — or leak into — a real backend's cache (or vice-versa). So
    // every disk-cache read/write is gated on the http backend being active.
    // Any real backend, not a named list -- the mock is synthetic and
    // regenerated each run, so persisting it would cache fiction.
    static bool disk_cache_enabled(const AppComponent& app) {
        return app.backend_label != "mock";
    }

    // Lay the user's machine-local per-session state back over a freshly
    // fetched (or cached) list. Settings is the durable source of truth for
    // both: a backend seeds its own starred flags on every list fetch and
    // knows nothing at all about this client's archive overlay, so without
    // this a star or an archive flipped in a prior launch is simply lost.
    static void apply_local_overlay(api::SessionSummary& s) {
        if (Settings::get().is_starred(s.id)) s.starred = true;
        if (Settings::get().is_muted(s.id)) s.muted = true;
        s.archive_override = Settings::get().get_archived(s.id);
    }

    static void apply_local_overlays(std::vector<api::SessionSummary>& out) {
        for (auto& s : out) apply_local_overlay(s);
    }

    // Every route a transcript reaches a pane by -- LRU hit, cached disk read,
    // network attach -- runs through here, so the attach-learned brakes reach
    // the CATALOG summary whichever one wins the race. That matters because
    // everything deciding what the UI does reads `find_summary`, not the
    // pane's own copy.
    //
    // An attach is AUTHORITATIVE: a thaw or a resume sends the key absent and
    // the brake must lift. A CACHED restore is not -- it is a copy that
    // predates the answer -- so it may only speak for `replies_paused`, which
    // no catalog row can contradict. Its FREEZE is dropped: the live row
    // beside it is fresher, and a row with no `frozen` key is the server
    // saying the thread is not frozen.
    static void adopt_attach_brakes(AppComponent& app, const api::Session& s,
                                    bool authoritative) {
        AppComponent::AttachBrakes brakes;
        brakes.replies_paused = s.summary.replies_paused;
        if (authoritative) {
            brakes.frozen = s.summary.frozen;
            brakes.frozen_by = s.summary.frozen_by;
            brakes.frozen_reason = s.summary.frozen_reason;
            app.apply_attach_brakes(s.summary.id, brakes);
            return;
        }
        if (!brakes.replies_paused) return;
        // Keep whatever the live catalog already says about the freeze rather
        // than clearing it with a stale copy's silence.
        if (const auto* known = app.find_summary(s.summary.id)) {
            brakes.frozen = known->frozen;
            brakes.frozen_by = known->frozen_by;
            brakes.frozen_reason = known->frozen_reason;
        }
        app.apply_attach_brakes(s.summary.id, brakes);
    }



    static void adopt_turn_asks(AppComponent& app, const std::string& id,
                                const std::string& asksJson) {
        if (asksJson.empty()) return;
        if (!app.askState.busyId.empty() &&
            app.ask_session_of(app.askState.busyId) == id)
            return;
        const auto parsed = nlohmann::json::parse(asksJson, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_array()) return;
        nlohmann::json state = nlohmann::json::object();
        state["pending_elicitations"] = parsed;
        app.apply_attach_asks(id, api::elicitation::asks_from_state(state, id));
    }

    static void adopt_attach_asks(AppComponent& app, const api::Session& s,
                                  bool authoritative) {
        if (!authoritative) return;
        if (!app.askState.busyId.empty() &&
            app.ask_session_of(app.askState.busyId) == s.summary.id)
            return;
        app.apply_attach_asks(s.summary.id, s.pending_asks);
    }

    // Persist a freshly-fetched transcript AND enforce the user's cache cap.
    // Trimming right after a save is the natural "cache grew" trigger; the cap
    // comes from Settings (0 = Unlimited => trim_to_cap is a no-op). Archived +
    // least-recently-opened threads are evicted first (see disk_cache).
    static void save_and_trim(const AppComponent& app, const api::Session& s) {
        if (!disk_cache_enabled(app)) return;
        api::disk_cache::save_transcript(s);
        api::disk_cache::trim_to_cap(Settings::get().get_cache_cap_bytes());
    }

    // Flip the optimistic user bubble's sync badge (LocalOnly/Persisting ->
    // Synced/Failed). Finds it by app.optimisticSendId in the open transcript;
    // no-op if the id is empty or the thread was switched away.
    static void mark_optimistic(AppComponent& app, api::SyncState st) {
        if (app.optimisticSendId.empty() || !app.pane().openSession) return;
        for (auto it = app.pane().openSession->messages.rbegin();
             it != app.pane().openSession->messages.rend(); ++it) {
            if (it->id == app.optimisticSendId) {
                it->sync = st;
                const std::size_t index =
                    app.pane().openSession->messages.size() - 1 -
                    static_cast<std::size_t>(std::distance(
                        app.pane().openSession->messages.rbegin(), it));
                app.pane().note_transcript_update(index);
                return;
            }
        }
    }

    // Carry still-PENDING local messages forward when a server transcript
    // replaces the open session. A just-sent optimistic user bubble
    // (sync == LocalOnly/Persisting/Failed) can vanish if the server refetch
    // lands before the backend has materialized that turn — the swap would
    // replace the transcript with a version that doesn't contain it yet (Gabe:
    // "shows two checkmarks then disappears until the server responds"). Before
    // swapping, collect any pending-local messages from the CURRENT open
    // session that the incoming `fresh` transcript does NOT already contain (by
    // id AND by text, since the server may assign a new id), and append them so
    // they stay visible until a later refetch includes the real turn.
    static void reconcile_optimistic(const Pane& pane, api::Session& fresh) {
        if (!pane.openSession) return;
        for (const auto& m : pane.openSession->messages) {
            // Any LOCALLY-ORIGINATED message (sync != None) is a candidate: a
            // just-sent bubble is LocalOnly/Persisting/Failed, and even after
            // the server ACKs it (flipped to Synced) the backend may take up to
            // ~30s to include the turn in a refetch — dropping it here made it
            // vanish for that whole window (Gabe: "disappears after 30 seconds
            // before the server sends it back"). Carry it forward until the
            // server transcript ACTUALLY contains it (matched by id OR
            // role+text).
            if (m.sync == api::SyncState::None) continue;
            bool already = false;
            for (const auto& f : fresh.messages) {
                if ((!m.id.empty() && f.id == m.id) ||
                    (f.role == m.role && !m.text.empty() && f.text == m.text)) {
                    already = true;
                    break;
                }
            }
            if (!already) fresh.messages.push_back(m);
        }
    }

    // ---- One pane's transcript, from request to painted ---------------------
    // Everything a pane needs done for it each frame: service its open
    // request, poll its disk read, poll its network fetch. It takes the PANE,
    // so the second pane is this same code with a different value rather than
    // a second, simpler, subtly different copy of it.
    //
    // That copy existed. The split pane used to be serviced by a forty-line
    // block below this one that had a cache hit and a network fetch and
    // nothing else: no disk-cache read, so a thread that was only on disk
    // opened blank in the right pane; no stale-while-revalidate, so it never
    // showed anything while it fetched; no error state, so a failed fetch was
    // a permanent "Loading...". Every one of those is a thing this function
    // does and that one did not.
    void service_pane(AppComponent& app, Pane& pane) {
        pane.reap_superseded_loads();
        // --- Transcript ---
        if (!pane.requestOpenId.empty() && !pane.transcriptPending) {
            std::string id = pane.requestOpenId;
            pane.requestOpenId.clear();
            pane.selectedId = id;
            // Refresh this thread's disk-cache recency (mtime) so the cache-cap
            // eviction's LRU ordering reflects OPENS, not just saves — the
            // least-recently-OPENED thread is trimmed first when over cap.
            if (disk_cache_enabled(app)) api::disk_cache::touch_transcript(id);

            // Phase X fast path: cache HIT -> render synchronously, no async
            // round-trip, no Loading flash. Marks the thread most-recently-used
            // so "last 5 interacted with" stays accurate on every open/switch.
            if (auto hit = app.transcriptCache.get(id)) {
                apply_local_overlay(hit->summary);
                adopt_attach_brakes(app, *hit, /*authoritative=*/false);
                adopt_attach_asks(app, *hit, /*authoritative=*/false);
                pane.openSession = std::move(*hit);
                pane.note_transcript_reset();
                pane.transcriptState = LoadState::Loaded;
                pane.transcriptError.clear();
                pane.transcriptLoadingId.clear();  // nothing loading now
                pane.hasMoreOlder = pane.openSession->has_more_older;
                // Mock is static -> cache is authoritative (no revalidation).
                // SEAM: a live backend would kick a background revalidate here
                // and swap in fresh data if it changed. Not built for the mock.
                // Live subscriptions are managed by sync_subscriptions()
                // each frame (one per open tab), so no per-open binding here.
            } else {
                // MISS in the in-memory LRU. FEATURE #1 (never beachball): the
                // disk-cache read USED to happen synchronously RIGHT HERE, on
                // the UI thread — open + JSON-parse of an up-to-690-message
                // transcript file. On a big thread that blocked the UI thread
                // for tens of ms (the beachball). Now we:
                //   (a) set transcriptState=Loading + transcriptLoadingId
                //       IMMEDIATELY (trivially cheap) so the pane can paint a
                //       spinner on the very next frame, and
                //   (b) launch the disk read on a WORKER THREAD
                //   (diskReadFuture)
                //       and the network revalidate on ANOTHER worker
                //       (transcriptFuture) — the UI thread does NEITHER the
                //       disk read/parse NOR the network. Whichever lands first
                //       (disk = stale paint, network = fresh) is applied by the
                //       pollers below.
                // The heavy parse/window work is entirely off the UI thread.
                pane.transcriptState = LoadState::Loading;
                pane.transcriptLoadingId = id;

                // (a) Disk-cache read on a worker (stale-while-revalidate).
                if (disk_cache_enabled(app)) {
                    pane.diskReadPending = true;
                    pane.diskReadId = id;
                    pane.diskReadEpoch = api::disk_cache::epoch();
                    pane.diskReadFuture = std::async(std::launch::async, [id] {
                        return api::disk_cache::load_transcript(id);
                    });
                }
                // (b) Network revalidate / first-ever load, newest-N only.
                pane.transcriptPending = true;
                pane.transcriptPendingId = id;
                api::Client* c = app.client.get();
                pane.transcriptFuture = std::async(std::launch::async, [c, id] {
                    return c->get_session(id, kMessagesWindow);
                });
                // Live subscriptions are managed by sync_subscriptions()
                // each frame (one per open tab), so no per-open binding here.
            }
        }
        // Poll the worker-thread disk-cache read (stale paint). Lands ahead of
        // — or alongside — the network fetch; applied only if this is still the
        // thread the user wants AND the network hasn't already painted it.
        if (pane.diskReadPending && pane.diskReadFuture.valid() &&
            pane.diskReadFuture.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready) {
            const std::string completedId = pane.diskReadId;
            auto disk = pane.diskReadFuture.get();
            pane.diskReadPending = false;
            pane.diskReadId.clear();
            const std::uint64_t completedEpoch = pane.diskReadEpoch;
            pane.diskReadEpoch = 0;
            if (disk && pane.accepts_disk_read(
                            completedId, completedEpoch,
                            api::disk_cache::epoch()) &&
                disk->summary.id == completedId &&
                // Don't clobber a fresh network result that already landed.
                pane.transcriptState != LoadState::Loaded) {
                app.transcriptCache.put(*disk);
                apply_local_overlay(disk->summary);
                adopt_attach_brakes(app, *disk, /*authoritative=*/false);
                adopt_attach_asks(app, *disk, /*authoritative=*/false);
                pane.openSession = std::move(*disk);
                pane.note_transcript_reset();
                pane.transcriptState = LoadState::Loaded;  // show stale now
                pane.transcriptError.clear();
                pane.hasMoreOlder = pane.openSession->has_more_older;
                // A stale paint clears the spinner for THIS thread; the network
                // revalidate still runs in the background (no Loading flash).
                if (pane.transcriptLoadingId == completedId)
                    pane.transcriptLoadingId.clear();
            }
        }
        if (pane.transcriptPending && pane.transcriptFuture.valid()) {
            if (pane.transcriptFuture.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready) {
                const std::string completedId = pane.transcriptPendingId;
                auto r = pane.transcriptFuture.get();
                pane.transcriptPending = false;
                pane.transcriptPendingId.clear();
                if (r.ok) {
                    // Insert into the cache (capped to the last 20 msgs) and
                    // mark most-recently-used, then render. Also persist to
                    // disk for the next session's instant (stale) paint.
                    app.transcriptCache.put(r.value);
                    save_and_trim(app, r.value);
                    // Only swap into the view if this is still the open thread
                    // (the user may have switched tabs during a slow fetch).
                    if (pane.selectedId == completedId &&
                        r.value.summary.id == completedId) {
                        apply_local_overlay(r.value.summary);
                        adopt_attach_brakes(app, r.value, /*authoritative=*/true);
                        adopt_attach_asks(app, r.value, /*authoritative=*/true);
                        pane.openSession = std::move(r.value);
                        pane.note_transcript_reset();
                        pane.transcriptState = LoadState::Loaded;
                        pane.transcriptError.clear();
                        pane.hasMoreOlder = pane.openSession->has_more_older;
                        // Fresh data landed — clear the "loading this thread"
                        // spinner flag for this id.
                        if (pane.transcriptLoadingId == completedId)
                            pane.transcriptLoadingId.clear();
                    }
                } else if (pane.selectedId == completedId) {
                    // Network fetch failed. If we already painted a stale copy
                    // from disk/LRU, KEEP it rather than blanking the pane on a
                    // transient slow-network error; only surface the error when
                    // there's nothing to show.
                    pane.transcriptError = r.error;
                    if (pane.openSession &&
                        pane.openSession->summary.id == completedId) {
                        pane.transcriptState = LoadState::Loaded;  // keep stale
                    } else {
                        pane.openSession.reset();
                        pane.transcriptState = LoadState::Error;
                    }
                    // Fetch resolved (success or fail) for this thread — stop
                    // showing the spinner. A pending disk read (if any) may
                    // still paint a stale copy afterwards.
                    if (pane.transcriptLoadingId == completedId &&
                        !pane.diskReadPending)
                        pane.transcriptLoadingId.clear();
                }
            }
        }
    }

    void for_each_with(Entity&, AppComponent& app, float) override {
        if (!app.client) return;

        // --- Auth: deferred device-code begin() (launch-perf) ---
        // begin() does a BLOCKING network POST; main.cpp deferred it off the
        // launch path by setting authNeedsBegin. Kick it on a worker exactly
        // like the list fetch so the window paints immediately. The flow is not
        // thread-safe, so while the begin() future is in flight nothing else
        // touches app.authFlow (app_frame's poll_step is guarded on
        // authBeginPending). When it resolves the flow is in
        // AwaitingUser/Failed and the normal frame-driven poll takes over.
        if (app.authNeedsBegin && !app.authBeginPending && app.authFlow) {
            app.authNeedsBegin = false;
            app.authBeginPending = true;
            auto flow = app.authFlow;
            const int64_t now =
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
            app.authBeginFuture = std::async(std::launch::async,
                                             [flow, now] { flow->begin(now); });
        }
        if (app.authBeginPending && app.authBeginFuture.valid()) {
            if (app.authBeginFuture.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready) {
                app.authBeginFuture.get();
                app.authBeginPending = false;  // flow is now safe to poll again
            }
        }

        // FEATURE #3 (message queuing) — INTERCEPT. The composer (a render
        // file we don't own) sets requestSendPrompt / requestStreamPrompt
        // directly. If a reply/stream is ALREADY in flight for the open thread,
        // servicing that flag now would either be dropped (a THIRD rapid send
        // overwrites the held second) or interleave. So when the target session
        // is busy, MOVE the pending prompt into the ordered per-session queue
        // and let drive_send_queue() dispatch it FIFO once the current turn
        // finishes. When the session is IDLE the flag falls through untouched
        // to the existing immediate START below (no behavior change for the
        // common single-send case). drive_send_queue only ever re-sets the flag
        // when the session is free, so this intercept never re-captures it.
        if (!app.pane().selectedId.empty() && app.sending_for(app.pane().selectedId)) {
            if (!app.requestStreamPrompt.empty()) {
                app.enqueue_send(app.pane().selectedId,
                                 std::move(app.requestStreamPrompt));
                app.requestStreamPrompt.clear();
            }
            if (!app.requestSendPrompt.empty()) {
                app.enqueue_send(app.pane().selectedId,
                                 std::move(app.requestSendPrompt));
                app.requestSendPrompt.clear();
            }
        }

        // --- Session list ---
        if (app.requestListRefresh && !app.listPending) {
            app.requestListRefresh = false;
            app.listPending = true;
            // STALE-WHILE-REVALIDATE: if we don't have a list yet, paint the
            // last-known list from disk IMMEDIATELY so a slow-network launch
            // shows the sidebar + Home instantly instead of a "Loading…" wall.
            // The async fetch below still runs and swaps in fresh data when it
            // arrives. Only prime once (when the in-memory list is empty).
            if (app.sessions.empty()) {
                if (auto cached = disk_cache_enabled(app)
                                      ? api::disk_cache::load_sessions()
                                      : std::nullopt;
                    cached && !cached->empty()) {
                    auto sessions = std::move(*cached);
                    apply_local_overlays(sessions);
                    app.seed_attach_brakes_from(sessions);
                    app.overlay_attach_brakes(sessions);
                    app.replace_sessions(std::move(sessions));
                    app.listState = LoadState::Loaded;  // show stale now
                    // sessions is provably non-empty here (loaded from a
                    // !cached->empty() cache), so no re-check needed.
                    if (app.pane().selectedId.empty())
                        app.pane().requestOpenId = app.sessions.front().id;
                } else {
                    app.listState = LoadState::Loading;
                }
            }
            api::Client* c = app.client.get();
            app.listFuture = std::async(std::launch::async,
                                        [c] { return c->list_sessions(); });
        }
        if (app.listPending && app.listFuture.valid()) {
            if (app.listFuture.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready) {
                auto r = app.listFuture.get();
                app.listPending = false;
                if (r.ok) {
                    apply_local_overlays(r.value);
                    app.overlay_attach_brakes(r.value);
                    app.replace_sessions(std::move(r.value));
                    app.listState = LoadState::Loaded;
                    app.listError.clear();
                    // Persist the fresh list for the next launch's instant paint.
                    if (disk_cache_enabled(app))
                        api::disk_cache::save_sessions(app.sessions);
                    // Auto-open the first session if nothing selected yet.
                    if (app.pane().selectedId.empty() && !app.sessions.empty())
                        app.pane().requestOpenId = app.sessions.front().id;
                    // Screenshot affordance: HANABI_STREAM_DEMO forces the
                    // first thread OPEN in the Chat transcript so a headless
                    // capture has an openSession for the composer's stream demo
                    // to fire into (main.cpp's wait/capture path doesn't open a
                    // tab on its own). Mirrors HANABI_VIEW; ignored when unset;
                    // no network (the mock resolves the transcript from cache).
                    if (const char* d = std::getenv("HANABI_STREAM_DEMO");
                        d && *d && !app.sessions.empty()) {
                        app.view = SmartView::Chat;
                        app.pane().selectedId = app.sessions.front().id;
                        app.pane().requestOpenId = app.sessions.front().id;
                    }
                } else {
                    // The network fetch failed (e.g. slow-network timeout). If
                    // we already painted a stale list from disk, KEEP it — a
                    // transient failure shouldn't blank the sidebar. Only show
                    // the error state when we have nothing to show.
                    app.listError = r.error;
                    if (app.sessions.empty())
                        app.listState = LoadState::Error;
                    else
                        app.listState = LoadState::Loaded;  // keep stale data
                }
            }
        }

        if (!app.subagentSidebarOpen) {
            app.requestSubagentRefresh = false;
            app.clear_subagent_sessions();
            app.subagentListState = LoadState::Idle;
            app.subagentListError.clear();
        } else if (app.requestSubagentRefresh && !app.subagentListPending) {
            app.requestSubagentRefresh = false;
            if (!app.client->supports_subagents()) {
                app.subagentListState = LoadState::Error;
                app.subagentListError =
                    "This backend does not expose sub-agent sessions";
            } else {
                app.subagentListPending = true;
                app.subagentListState = LoadState::Loading;
                api::Client* c = app.client.get();
                app.subagentListFuture = std::async(std::launch::async, [c] {
                    return c->list_subagents(
                        AppComponent::kMaxSubagentSessions);
                });
            }
        }
        if (app.subagentListPending && app.subagentListFuture.valid() &&
            app.subagentListFuture.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready) {
            auto r = app.subagentListFuture.get();
            app.subagentListPending = false;
            if (!app.subagentSidebarOpen) {
                app.clear_subagent_sessions();
                app.subagentListState = LoadState::Idle;
            } else if (r.ok) {
                if (r.value.size() > AppComponent::kMaxSubagentSessions)
                    r.value.resize(AppComponent::kMaxSubagentSessions);
                app.replace_subagent_sessions(std::move(r.value));
                app.subagentListState = LoadState::Loaded;
                app.subagentListError.clear();
            } else {
                app.subagentListState = LoadState::Error;
                app.subagentListError = r.error;
            }
        }

        // --- Transcripts: every pane that is showing ---
        for (size_t i = 0; i < app.active_pane_count(); ++i)
            service_pane(app, app.panes[i]);
        for (size_t i = app.active_pane_count(); i < app.panes.size(); ++i)
            app.panes[i].reap_superseded_loads();

        // Open a thread in the pane that is not focused, splitting if
        // it is not already. The pane then loads it through the SAME
        // service_pane path above on the next frame.
        if (app.requestSplitToggle) {
            app.requestSplitToggle = false;
            if (app.splitOpen) {
                app.requestSplitClose = true;
            } else if (app.pane().openSession) {
                // Splitting shows the SAME thread in both panes: it is
                // the cheapest useful default (two places in one long
                // transcript) and the second pane is one tab click from
                // becoming anything else.
                app.requestSplitOpen = app.pane().selectedId;
            }
        }
        if (app.requestSplitClose) {
            app.requestSplitClose = false;
            app.splitOpen = false;
            app.focusedPane = 0;
            // Let go of the closed pane's transcript. Its ID is kept -- that
            // is what gets persisted and what makes reopening the split
            // instant off the LRU -- but the messages are the largest thing
            // this app holds, and a pane nobody can see holding a 690-message
            // transcript for the rest of the session is a leak with a
            // plausible excuse. Anything in flight for it is dropped the same
            // way a tab switch drops a slow fetch.
            Pane& closed = app.panes[1];
            closed.supersede_transcript_loads();
            closed.openSession.reset();
            closed.transcriptState = LoadState::Idle;
            closed.transcriptError.clear();
            closed.transcriptLoadingId.clear();
            closed.requestOpenId.clear();
            closed.findOpen = false;
            closed.findQuery.clear();
            // ...and reopening must actually re-open it, so the pane asks for
            // its thread again rather than sitting empty.
            if (!closed.selectedId.empty())
                closed.scrollBottomPending = closed.selectedId;
        }
        if (!app.requestSplitOpen.empty()) {
            const std::string id = app.requestSplitOpen;
            app.requestSplitOpen.clear();
            app.splitOpen = true;
            Pane& target = app.other_pane();
            // `|| !target.openSession` is not belt and braces: closing a split
            // drops the pane's transcript and keeps its id, so a pane that is
            // already "on" this thread has nothing to show until it asks
            // again. Without it, splitting -> closing -> splitting left the
            // second pane permanently blank.
            if (target.selectedId != id || !target.openSession)
                model::retarget_split_pane(target, id);
            app.view = SmartView::Chat;
        }

        //
        // DECISION POINT: the composer sets requestSendPrompt (synchronous
        // backends) or requestStreamPrompt (streaming backends, incl. the mock)
        // for the OPEN thread exactly as for a normal reply. If that open
        // thread's agent is CURRENTLY RUNNING and the backend can steer
        // (app.should_steer_open()), we CONSUME that flag HERE and dispatch
        // client->steer() on a worker thread — BEFORE the normal reply/stream
        // paths below/downstream get a chance to service it. When the thread is
        // idle (or steering is unsupported), this block is inert and the flags
        // fall through to the existing send/stream plumbing unchanged.
        //
        // We only steer the OPEN thread (steer targets a specific session and
        // reuses the open transcript for the appended turn). A steer already in
        // flight for this session holds off a second dispatch (steerPending).
        if (app.should_steer_open() && !app.steerPending &&
            !app.pane().selectedId.empty() && app.pane().openSession &&
            app.pane().openSession->summary.id == app.pane().selectedId &&
            (!app.requestSendPrompt.empty() ||
             !app.requestStreamPrompt.empty())) {
            std::string prompt = !app.requestStreamPrompt.empty()
                                     ? app.requestStreamPrompt
                                     : app.requestSendPrompt;
            // Consume BOTH so the downstream send/stream paths don't also fire.
            app.requestStreamPrompt.clear();
            app.requestSendPrompt.clear();
            std::string id = app.pane().selectedId;
            app.steerPending = true;
            app.steerSessionId = id;
            app.sendingPrompt = prompt;  // reuse the "…" in-flight hint
            api::Client* c = app.client.get();
            // Opt-in debug trace (HANABI_DUMP), mirroring http_client.cpp's
            // gated fprintf — proves the steer-vs-send routing fired without
            // spamming a normal run.
            if (std::getenv("HANABI_DUMP"))
                fprintf(stderr,
                        "[HANABI_DUMP] steer: routing into RUNNING session %s "
                        "(state==Running, supports_steer)\n",
                        id.c_str());
            app.steerFuture = std::async(std::launch::async, [c, id, prompt] {
                return c->steer(id, prompt);
            });
        }
        if (app.steerPending && app.steerFuture.valid()) {
            if (app.steerFuture.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready) {
                auto r = app.steerFuture.get();
                app.steerPending = false;
                std::string userText = app.sendingPrompt;
                app.sendingPrompt.clear();
                if (r.ok) {
                    // Append the user's steering message + the returned reply to
                    // the open transcript, mirroring the reply path so the
                    // transcript reads as a full turn, then refresh the cache.
                    if (app.pane().openSession &&
                        app.pane().openSession->summary.id == app.steerSessionId) {
                        api::Message um;
                        um.role = api::Role::User;
                        um.id = app.steerSessionId + "-u" +
                                std::to_string(app.pane().openSession->messages.size());
                        um.text = userText;
                        um.created_at = r.value.created_at;
                        const std::size_t first =
                            app.pane().openSession->messages.size();
                        app.pane().openSession->messages.push_back(std::move(um));
                        app.pane().openSession->messages.push_back(r.value);
                        app.pane().note_transcript_append(first, 2);
                        app.transcriptCache.put(*app.pane().openSession);
                    }
                } else {
                    app.pane().transcriptError = r.error;
                }
            }
        }

        // --- Kickoff (composer "Start" -> create a NEW session) ---
        if (!app.requestKickoffPrompt.empty() && !app.kickoffPending) {
            std::string prompt = app.requestKickoffPrompt;
            app.requestKickoffPrompt.clear();
            app.kickoffPending = true;
            api::Client* c = app.client.get();
            app.kickoffFuture = std::async(std::launch::async, [c, prompt] {
                return c->create_session(prompt);
            });
        }
        if (app.kickoffPending && app.kickoffFuture.valid()) {
            if (app.kickoffFuture.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready) {
                auto r = app.kickoffFuture.get();
                app.kickoffPending = false;
                if (r.ok) {
                    // Refresh the list so the new thread appears, and open it
                    // in a TAB (requestOpenTab, not requestOpenId) so the view
                    // transitions Home -> Chat and a tab is created for the new
                    // session — otherwise the kickoff loaded the transcript but
                    // left the user on Home with no visible tab.
                    app.requestListRefresh = true;
                    app.requestOpenTab = r.value;
                } else {
                    // Surface the failure on the list rail (non-fatal).
                    app.listError = r.error;
                }
            }
        }

        // --- Session rename (durable echo; no local optimism) ---------------
        // The modal parks the ask here and keeps its spinner up. The title is
        // applied only from what the server echoes back; a refusal goes back to
        // the modal, which is still open with the user's text in it.
        if (!app.requestRenameId.empty() && !app.renameFuture.valid()) {
            const std::string id = app.requestRenameId;
            const std::string title = app.requestRenameTitle;
            app.requestRenameId.clear();
            app.requestRenameTitle.clear();
            app.renameInFlightId = id;
            api::Client* c = app.client.get();
            app.renameFuture = std::async(std::launch::async, [c, id, title] {
                return c->rename_session(id, title);
            });
        }
        if (app.renameFuture.valid() &&
            app.renameFuture.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready) {
            auto r = app.renameFuture.get();
            app.renamePending = false;
            if (r.ok) {
                app.apply_renamed_title(app.renameInFlightId, r.value);
                app.renameOpen = false;
                app.renameSessionId.clear();
                app.renameDraft.clear();
                app.renameError.clear();
            } else {
                app.renameError = r.error;
            }
            app.renameInFlightId.clear();
        }

        if (!app.requestAskSessionId.empty() && !app.askFuture.valid()) {
            const std::string sid = app.requestAskSessionId;
            const api::PendingAsk ask = app.requestAsk;
            const api::AskAction action = app.requestAskAction;
            const api::AskAnswer answer = app.askState.answer_for(ask.id());
            app.requestAskSessionId.clear();
            app.askInFlightSession = sid;
            app.askState.busyId = ask.id();
            app.askState.errorId.clear();
            app.askState.errorText.clear();
            api::Client* c = app.client.get();
            app.askFuture =
                std::async(std::launch::async, [c, sid, ask, action, answer] {
                    return c->resolve_ask(sid, ask, action, answer);
                });
        }
        if (app.askFuture.valid() &&
            app.askFuture.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready) {
            auto r = app.askFuture.get();
            const std::string askId = app.askState.busyId;
            const std::string sid = app.askInFlightSession;
            app.askState.busyId.clear();
            app.askInFlightSession.clear();
            if (r.ok) {
                app.drop_attach_ask(sid, askId);
            } else if (r.error == api::elicitation::kAskGoneReason) {
                app.drop_attach_ask(sid, askId);
                app.raise_toast(r.error, "",
                               AppComponent::ToastUndo::None);
            } else {
                app.askState.errorId = askId;
                app.askState.errorText = r.error;
            }
        }

        if (!app.requestForkSourceId.empty() && !app.forkFuture.valid()) {
            const std::string source = app.requestForkSourceId;
            const std::string prompt = app.requestForkPrompt;
            const std::string title = app.requestForkTitle;
            app.forkPending = true;
            app.forkError.clear();
            api::Client* c = app.client.get();
            app.forkFuture =
                std::async(std::launch::async, [c, source, prompt, title] {
                    return prompt.empty()
                               ? c->fork_session(source)
                               : c->fork_with_prompt(source, prompt, title);
                });
        }
        if (app.forkFuture.valid() &&
            app.forkFuture.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready) {
            auto r = app.forkFuture.get();
            app.forkPending = false;
            if (r.ok) {
                app.requestListRefresh = true;
                app.requestOpenTab = r.value;
                app.requestOpenTabPane = std::clamp(app.requestForkPane, 0, 1);
                app.requestOpenTabKeep = true;
                app.forkError.clear();
                app.forkRestoreDraft.clear();
                app.forkRestoreSessionId.clear();
            } else {
                app.forkError = r.error;
            }
            app.requestForkSourceId.clear();
            app.requestForkPrompt.clear();
            app.requestForkTitle.clear();
        }

        // --- Reply (transcript composer "Send" -> continue the open thread) ---
        if (!app.requestSendPrompt.empty() && !app.sendPending &&
            !app.pane().selectedId.empty()) {
            std::string prompt = app.requestSendPrompt;
            std::string id = app.pane().selectedId;
            app.requestSendPrompt.clear();
            app.sendPending = true;
            app.sendSessionId = id;
            app.sendingPrompt = prompt;
            // OPTIMISTIC + local-first: append the user's message to the open
            // transcript IMMEDIATELY with sync=Persisting (in flight), and
            // record it in the local outbox (survives a crash). On success it
            // flips to Synced + the reply is appended; on failure it flips to
            // Failed and STAYS in the outbox, where drive_outbox() picks it up
            // and tries again. We remember the optimistic bubble's id so the
            // resolver can find + update it.
            //
            // A retry issued by drive_outbox() came OUT of the store, so it
            // must not be written back into it (that is how one failed prompt
            // becomes four), and its bubble is already in the transcript.
            const bool fromOutbox = claim_outbox_dispatch(app, id, prompt);
            if (!fromOutbox) api::disk_cache::outbox_add(id, prompt);
            app.optimisticSendId.clear();
            if (app.pane().openSession && app.pane().openSession->summary.id == id) {
                if (fromOutbox && adopt_local_bubble(app, prompt)) {
                    app.pane().scrollBottomPending = id;
                } else {
                    api::Message um;
                    um.role = api::Role::User;
                    um.id = id + "-u" +
                            std::to_string(app.pane().openSession->messages.size());
                    um.text = prompt;
                    um.created_at =
                        static_cast<int64_t>(std::time(nullptr));
                    um.sync = api::SyncState::Persisting;
                    app.optimisticSendId = um.id;
                    const std::size_t first =
                        app.pane().openSession->messages.size();
                    app.pane().openSession->messages.push_back(std::move(um));
                    app.pane().note_transcript_append(first, 1);
                    app.pane().scrollBottomPending = id;  // keep the new bubble in view
                }
            }
            api::Client* c = app.client.get();
            app.sendFuture = std::async(std::launch::async, [c, id, prompt] {
                return c->send_message(id, prompt);
            });
        }
        if (app.sendPending && app.sendFuture.valid()) {
            if (app.sendFuture.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready) {
                auto r = app.sendFuture.get();
                app.sendPending = false;
                // Remember the prompt for the user bubble, then clear the hint.
                std::string userText = app.sendingPrompt;
                app.sendingPrompt.clear();
                if (r.ok) {
                    // The optimistic user bubble is ALREADY in the transcript
                    // (appended at dispatch with sync=Persisting). Flip it to
                    // Synced, append the assistant reply, drop it from the local
                    // outbox (confirmed on the server), and refresh the cache.
                    if (app.pane().openSession &&
                        app.pane().openSession->summary.id == app.sendSessionId) {
                        mark_optimistic(app, api::SyncState::Synced);
                        // Fallback: if the optimistic bubble wasn't recorded
                        // (e.g. the thread was switched at dispatch), reconstruct
                        // the user turn so the transcript still reads complete.
                        const std::size_t first =
                            app.pane().openSession->messages.size();
                        if (app.optimisticSendId.empty()) {
                            api::Message um;
                            um.role = api::Role::User;
                            um.id = app.sendSessionId + "-u" +
                                    std::to_string(
                                        app.pane().openSession->messages.size());
                            um.text = userText;
                            um.created_at = r.value.created_at;
                            um.sync = api::SyncState::Synced;
                            app.pane().openSession->messages.push_back(std::move(um));
                        }
                        app.pane().openSession->messages.push_back(r.value);
                        app.pane().note_transcript_append(
                            first,
                            app.pane().openSession->messages.size() - first);
                        app.transcriptCache.put(*app.pane().openSession);
                    }
                    api::disk_cache::outbox_remove(app.sendSessionId, userText);
                    app.outboxRetry.confirmed(app.sendSessionId, userText);
                    app.optimisticSendId.clear();
                } else {
                    // Send failed: mark the optimistic bubble Failed, leave the
                    // prompt in the outbox and hand it to the retry policy,
                    // which backs off and tries it again. Before this existed
                    // the entry sat on disk forever and "kept in the outbox to
                    // retry" was only the first half of a sentence.
                    mark_optimistic(app, api::SyncState::Failed);
                    app.pane().transcriptError = r.error;
                    note_outbox_failure(app, app.sendSessionId, userText);
                }
            }
        }

        drive_stream(app);
        drive_load_older(app);
        sync_subscriptions(app);
        drive_live_events(app);
        drive_send_queue(app);
        drive_outbox(app);
        drive_settings(app);
        drive_settings_sync(app);
    }

  private:
    // ---- Settings-sync state (see drive_settings_sync) -------------------
    // Debounce window: coalesce a burst of preference clicks into one push.
    static constexpr std::chrono::milliseconds kSyncDebounce{1500};
    bool settings_sync_armed_ = false;   // debounce timer running
    bool settings_sync_pending_ = false; // a push is in flight
    std::chrono::steady_clock::time_point settings_sync_deadline_{};
    std::future<bool> settings_sync_future_;

    // ---- Message-send queue (FEATURE #3) ---------------------------------
    //
    // When the user fires a second send into a session that already has a
    // reply/stream in flight, we must QUEUE it (ordered) and dispatch the next
    // one only once the current completes — never drop it, never interleave.
    //
    // The composer (render, separate stream) enqueues EVERY send via
    // app.enqueue_send(id, prompt). This loader owns the mechanics: each frame,
    // for the head of the queue whose session is currently FREE (no
    // send/stream in flight), pop it and dispatch — preferring the streamed
    // path when the backend supports it (mirrors the composer's own choice),
    // else the synchronous reply path. Because sending_for(id) already covers
    // sendPending + streamCollecting + streamActive, a session drains exactly
    // one queued item per completed turn, in FIFO order.
    // ---- The outbox's READ side (api/outbox.h holds the policy) ----------
    //
    // Every prompt is written to disk before it is sent. Nothing read it back:
    // `disk_cache::outbox_list` had no caller in src, tests or tools, so a send
    // that failed left its entry on disk forever and a crash mid-send restored
    // nothing. The design note that specified the feature says so itself --
    // "HONEST GAP: there is no reconnect-DRAIN yet ... the user must re-send"
    // (LOCAL_FIRST_DESIGN.md) -- and docs/COMMIT_AUDIT.md CB3 found the same
    // hole from the code. These four functions are that drain.
    //
    // Three rules shape it and each one is a bug that would otherwise exist:
    //
    //   * A retry must not re-ADD its own prompt to the store it came from,
    //     or one failed send becomes two entries, then four.
    //   * A retry must not paint a SECOND bubble for a turn already on screen.
    //   * The user's own queued sends go first. A retry is old work; a
    //     keystroke is not.

    // Is this (id, prompt) the dispatch drive_outbox() just issued? Consumes
    // the flag, so the answer is true exactly once per retry.
    static bool claim_outbox_dispatch(AppComponent& app, const std::string& id,
                                      const std::string& prompt) {
        if (!app.outboxSuppressAdd) return false;
        if (app.outboxRetryId != id || app.outboxRetryPrompt != prompt)
            return false;
        app.outboxSuppressAdd = false;
        return true;
    }

    // Find the locally-originated bubble already holding `prompt` and take it
    // over as the optimistic bubble for this attempt, rather than appending a
    // duplicate. True if one was adopted.
    static bool adopt_local_bubble(AppComponent& app,
                                   const std::string& prompt) {
        if (!app.pane().openSession) return false;
        for (auto it = app.pane().openSession->messages.rbegin();
             it != app.pane().openSession->messages.rend(); ++it) {
            if (it->role != api::Role::User) continue;
            if (it->sync == api::SyncState::None) continue;
            if (it->text != prompt) continue;
            it->sync = api::SyncState::Persisting;
            app.optimisticSendId = it->id;
            const std::size_t index =
                app.pane().openSession->messages.size() - 1 -
                static_cast<std::size_t>(std::distance(
                    app.pane().openSession->messages.rbegin(), it));
            app.pane().note_transcript_update(index);
            return true;
        }
        return false;
    }

    static void mark_local_prompt(Pane& pane, const std::string& prompt,
                                  api::SyncState st) {
        if (!pane.openSession) return;
        for (auto it = pane.openSession->messages.rbegin();
             it != pane.openSession->messages.rend(); ++it) {
            if (it->role != api::Role::User) continue;
            if (it->sync == api::SyncState::None) continue;
            if (it->text != prompt) continue;
            it->sync = st;
            const std::size_t index =
                pane.openSession->messages.size() - 1 -
                static_cast<std::size_t>(
                    std::distance(pane.openSession->messages.rbegin(), it));
            pane.note_transcript_update(index);
            return;
        }
    }

    static void drop_local_prompt(Pane& pane, const std::string& prompt) {
        if (!pane.openSession) return;
        auto& msgs = pane.openSession->messages;
        for (auto it = msgs.begin(); it != msgs.end(); ++it) {
            if (it->role != api::Role::User) continue;
            if (it->sync == api::SyncState::None) continue;
            if (it->text != prompt) continue;
            msgs.erase(it);
            pane.note_transcript_reset();
            return;
        }
    }

    static void sync_stream_transcript(AppComponent& app, int ownerIndex) {
        Pane& owner = app.panes[static_cast<std::size_t>(ownerIndex)];
        if (!owner.openSession) return;
        for (std::size_t i = 0; i < app.active_pane_count(); ++i) {
            if (static_cast<int>(i) == ownerIndex) continue;
            Pane& pane = app.panes[i];
            if (!pane.openSession || pane.selectedId != owner.selectedId) continue;
            pane.openSession->messages = owner.openSession->messages;
            pane.note_transcript_reset();
        }
    }

    static void sync_stream_message(AppComponent& app, int ownerIndex,
                                    std::size_t messageIndex) {
        Pane& owner = app.panes[static_cast<std::size_t>(ownerIndex)];
        if (!owner.openSession || messageIndex >= owner.openSession->messages.size())
            return;
        for (std::size_t i = 0; i < app.active_pane_count(); ++i) {
            if (static_cast<int>(i) == ownerIndex) continue;
            Pane& pane = app.panes[i];
            if (!pane.openSession || pane.selectedId != owner.selectedId) continue;
            if (pane.openSession->messages.size() !=
                owner.openSession->messages.size()) {
                pane.openSession->messages = owner.openSession->messages;
                pane.note_transcript_reset();
                continue;
            }
            pane.openSession->messages[messageIndex] =
                owner.openSession->messages[messageIndex];
            pane.note_transcript_update(messageIndex);
        }
    }

    static void note_outbox_failure(AppComponent& app, const std::string& id,
                                    const std::string& prompt) {
        const int64_t now = static_cast<int64_t>(std::time(nullptr));
        // The prompt is already on disk (outbox_add ran at dispatch); this is
        // where the retry loop learns about it.
        app.outboxRetry.adopt(id, prompt);
        app.outboxRetry.failed(id, prompt, now);
        fprintf(stderr,
                "[outbox] send failed for %s; %zu prompt(s) held on disk, "
                "retrying (attempt %d)\n",
                id.c_str(), app.outboxRetry.count_for(id),
                app.outboxRetry.attempts_for(id, prompt));
    }

    // Paint any unconfirmed prompt for the OPEN thread that the transcript
    // does not already contain. This is what makes a crash mid-send visible
    // after a relaunch: the server never heard the prompt, so no refetch will
    // ever produce it, and without this the user's words are on disk and
    // nowhere else.
    static void restore_outbox_bubbles(AppComponent& app) {
        if (!app.pane().openSession) return;
        const std::string id = app.pane().openSession->summary.id;
        if (app.outboxRetry.count_for(id) == 0) return;
        const std::size_t firstAdded =
            app.pane().openSession->messages.size();
        for (const auto& e : app.outboxRetry.entries()) {
            if (e.sessionId != id) continue;
            bool present = false;
            for (const auto& m : app.pane().openSession->messages) {
                if (m.role == api::Role::User && m.text == e.prompt) {
                    present = true;
                    break;
                }
            }
            if (present) continue;
            api::Message um;
            um.role = api::Role::User;
            um.id = id + "-ob" +
                    std::to_string(app.pane().openSession->messages.size());
            um.text = e.prompt;
            um.created_at = static_cast<int64_t>(std::time(nullptr));
            um.sync = api::SyncState::LocalOnly;
            app.pane().openSession->messages.push_back(std::move(um));
            app.pane().scrollBottomPending = id;
        }
        const std::size_t added =
            app.pane().openSession->messages.size() - firstAdded;
        if (added != 0) app.pane().note_transcript_append(firstAdded, added);
    }

    void drive_outbox(AppComponent& app) {
        if (!app.client) return;

        // The startup enumeration. Once per process, as soon as there is a
        // client to send with. outbox_sessions() is the function that had to
        // be added for this: outbox_list can only answer about an id somebody
        // already named, and after a restart nothing in memory names any.
        if (!app.outboxRestored) {
            app.outboxRestored = true;
            std::vector<api::outbox::Entry> found;
            for (const auto& id : api::disk_cache::outbox_sessions())
                for (const auto& p : api::disk_cache::outbox_list(id))
                    found.push_back(api::outbox::Entry{id, p, 0, 0});
            if (!found.empty()) {
                app.outboxRetry.restore(found);
                fprintf(stderr,
                        "[outbox] restored %zu unconfirmed prompt(s) across "
                        "%zu thread(s) from the last run; will retry\n",
                        found.size(),
                        api::disk_cache::outbox_sessions().size());
            }
        }

        if (!app.requestRetryPrompt.empty() &&
            !app.requestRetrySessionId.empty()) {
            const std::string id = std::move(app.requestRetrySessionId);
            const std::string prompt = std::move(app.requestRetryPrompt);
            app.requestRetrySessionId.clear();
            app.requestRetryPrompt.clear();
            if (!app.outboxRetry.holds(id, prompt))
                api::disk_cache::outbox_add(id, prompt);
            app.outboxRetry.retry_now(id, prompt);
        }

        if (app.outboxRetry.empty()) return;
        restore_outbox_bubbles(app);

        const int64_t now = static_cast<int64_t>(std::time(nullptr));
        // A retry can only be dispatched into the OPEN thread, for the same
        // reason drive_send_queue's dispatch can: the send START blocks below
        // read the focused pane's selectedId, so a prompt aimed anywhere else would land in
        // the wrong transcript. An entry for a closed thread waits, durably,
        // until it is opened.
        const api::outbox::Entry* pick = app.outboxRetry.next(
            now, [&app](const std::string& id) {
                if (id != app.pane().selectedId) return false;
                if (app.sending_for(id)) return false;
                if (!app.pane().openSession || app.pane().openSession->summary.id != id)
                    return false;
                if (!app.requestSendPrompt.empty()) return false;
                if (!app.requestStreamPrompt.empty()) return false;
                if (!app.pendingSendQueue.empty()) return false;
                return true;
            });
        if (!pick) return;

        const std::string id = pick->sessionId;
        const std::string prompt = pick->prompt;
        app.outboxRetry.attempted(*pick);
        app.outboxRetryId = id;
        app.outboxRetryPrompt = prompt;
        app.outboxSuppressAdd = true;
        fprintf(stderr, "[outbox] retrying a held prompt for %s (attempt %d)\n",
                id.c_str(), app.outboxRetry.attempts_for(id, prompt));
        if (app.client->supports_stream())
            app.requestStreamPrompt = prompt;
        else
            app.requestSendPrompt = prompt;
    }

    void drive_send_queue(AppComponent& app) {
        if (app.pendingSendQueue.empty() || !app.client) return;
        // Find the FIRST queued send whose session is free AND not already
        // being dispatched this frame (requestSendPrompt/requestStreamPrompt
        // still unconsumed). Preserve per-session FIFO by scanning front-first.
        for (auto it = app.pendingSendQueue.begin();
             it != app.pendingSendQueue.end(); ++it) {
            const std::string& id = it->sessionId;
            if (app.sending_for(id)) continue;  // busy: keep it queued
            // Don't stomp an un-consumed dispatch for this same session.
            if (!app.requestSendPrompt.empty() && app.pane().selectedId == id) continue;
            if (!app.requestStreamPrompt.empty() && app.pane().selectedId == id)
                continue;
            // Dispatch this one. The existing reply/stream START blocks read
            // requestSendPrompt/requestStreamPrompt for app.pane().selectedId, so a
            // queued send only fires against the OPEN thread — which is the
            // only thread the composer can target anyway. If the queued send
            // is for a non-open thread, hold it until that thread is opened
            // (keeps ordering; never sends into the wrong transcript).
            if (app.pane().selectedId != id) continue;
            const std::string prompt = it->prompt;
            app.pendingSendQueue.erase(it);
            if (app.client->supports_stream())
                app.requestStreamPrompt = prompt;
            else
                app.requestSendPrompt = prompt;
            return;  // one dispatch per frame; the rest drain on later frames.
        }
    }

    // ---- Settings read (FEATURE #4) --------------------------------------
    //
    // On requestSettings, fetch user/account settings from the backend on a
    // WORKER THREAD (never the UI thread) and store the result on AppComponent
    // so the settings screen can verify setup. Same async + poll pattern as the
    // transcript/list fetches.
    void drive_settings(AppComponent& app) {
        if (app.requestSettings && !app.settingsPending && app.client) {
            app.requestSettings = false;
            if (!app.client->supports_settings()) {
                app.settingsState = LoadState::Error;
                app.settingsError = "backend does not expose settings";
            } else {
                app.settingsPending = true;
                app.settingsState = LoadState::Loading;
                api::Client* c = app.client.get();
                app.settingsFuture = std::async(
                    std::launch::async, [c] { return c->get_settings(); });
            }
        }
        if (app.settingsPending && app.settingsFuture.valid() &&
            app.settingsFuture.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready) {
            auto r = app.settingsFuture.get();
            app.settingsPending = false;
            if (r.ok) {
                app.settings = std::move(r.value);
                app.settingsState = LoadState::Loaded;
                app.settingsError.clear();
            } else {
                app.settingsState = LoadState::Error;
                app.settingsError = r.error;
            }
        }
    }

    // ---- Settings SYNC (web-matches-local) -------------------------------
    //
    // The settings modal persists every preference change LOCALLY first (the
    // Settings singleton auto-saves to disk) and flips Settings::mark_dirty().
    // This tick DEBOUNCES those changes and pushes a snapshot to the backend so
    // the web app matches — best-effort, non-blocking (std::async, like the
    // read path). When the backend has no write path configured (the zero-config
    // mock is the default) the push is a no-op that still clears the dirty flag,
    // so local-only persistence works and NO error is surfaced.
    //
    // Debounce: after the last change, wait kSyncDebounce before pushing (so a
    // burst of clicks coalesces into one push). The modal-close path can force
    // an immediate flush by leaving the flag set; the next tick with the timer
    // elapsed sends it. One in-flight push at a time; the future is polled and
    // reaped here.
    void drive_settings_sync(AppComponent& app) {
        auto& s = Settings::get();

        // Reap a completed in-flight push (clears the pending slot).
        if (settings_sync_pending_ && settings_sync_future_.valid() &&
            settings_sync_future_.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready) {
            (void)settings_sync_future_.get();  // best-effort: ignore result
            settings_sync_pending_ = false;
        }

        if (!s.is_settings_dirty()) return;    // nothing changed
        if (settings_sync_pending_) return;    // a push is already in flight
        if (!app.client) return;

        // Debounce: start/refresh the timer on the first dirty observation.
        const auto now = std::chrono::steady_clock::now();
        if (!settings_sync_armed_) {
            settings_sync_armed_ = true;
            settings_sync_deadline_ = now + kSyncDebounce;
            return;  // let the burst settle
        }
        if (now < settings_sync_deadline_) return;  // still coalescing

        // Timer elapsed: clear local flags NOW (so new changes re-arm cleanly)
        // and launch the push. Clearing dirty up front is safe — a change that
        // lands mid-push re-marks dirty and gets its own later push.
        settings_sync_armed_ = false;
        s.clear_settings_dirty();

        // If the backend can't write, we're done: local-only, no error. The
        // dirty flag is already cleared so we don't spin every frame.
        if (!app.client->supports_settings_write()) return;

        // Build the snapshot payload from the local Settings. Carried in
        // UserSettings.raw_json so the http adapter can PUT it verbatim and the
        // mock can store it — WITHOUT baking any field mapping into the client.
        api::UserSettings snap;
        snap.ok = true;
        snap.user_id = app.settings.user_id;
        snap.raw_json = build_settings_payload(s);

        api::Client* c = app.client.get();
        settings_sync_pending_ = true;
        settings_sync_future_ = std::async(std::launch::async, [c, snap] {
            return c->update_settings(snap);
        });
    }

    // Serialize the client-syncable preference slots to a compact JSON object
    // matching the web PUT-preferences field names. Built by hand (no json dep
    // in this header) — the values come straight from the local Settings.
    static std::string build_settings_payload(Settings& s) {
        auto esc = [](const std::string& in) {
            std::string o;
            o.reserve(in.size() + 2);
            for (char ch : in) {
                if (ch == '"' || ch == '\\') o.push_back('\\');
                o.push_back(ch);
            }
            return o;
        };
        std::string j = "{";
        j += "\"yapLevel\":" + std::to_string(s.get_yap_level());
        j += ",\"autoArchiveDays\":" +
             std::to_string(s.get_auto_archive_days());
        j += ",\"notificationSound\":";
        j += (s.get_notification_sound() ? "true" : "false");
        j += ",\"memoryBackend\":\"" + esc(s.get_memory_backend()) + "\"";
        j += ",\"defaultModelId\":\"" + esc(s.get_default_model()) + "\"";
        j += "}";
        return j;
    }

    // ---- Load OLDER (full transcript on demand) --------------------------
    //
    // Each visible pane owns its own request, future and completion id. Focus
    // changes therefore cannot redirect a fetch or make one pane wait behind
    // the other. Since the backend has no working backward cursor yet,
    // "load older" re-fetches the full transcript once (limit=0).
    void drive_load_older(AppComponent& app) {
        if (!app.client) return;
        for (size_t i = 0; i < app.active_pane_count(); ++i) {
            Pane& pane = app.panes[i];
            if (pane.requestLoadOlder && !pane.loadingOlder &&
                !pane.selectedId.empty()) {
                pane.requestLoadOlder = false;
                pane.loadingOlder = true;
                pane.anchorPrevMsgCount =
                    pane.openSession ? pane.openSession->messages.size() : 0;
                pane.loadOlderPendingId = pane.selectedId;
                api::Client* client = app.client.get();
                const std::string id = pane.selectedId;
                pane.loadOlderFuture = std::async(
                    std::launch::async,
                    [client, id] { return client->get_session(id, 0); });
            }
            service_load_older(app, pane);
        }
    }

    static void service_load_older(AppComponent& app, Pane& pane) {
        auto completion = model::take_load_older_completion(pane);
        if (!completion) return;
        if (!completion->result.ok) {
            if (pane.selectedId == completion->sessionId)
                pane.transcriptError = completion->result.error;
            return;
        }

        if (!model::load_older_completion_matches(pane, *completion)) return;
        reconcile_optimistic(pane, completion->result.value);
        app.transcriptCache.put(completion->result.value);
        save_and_trim(app, completion->result.value);
        (void) model::apply_load_older_completion(pane, *completion);
    }

    // ---- Live events (SSE) -----------------------------------------------
    //
    // Poll the atomic refetch flag the subscription worker flips, debounce it,
    // and re-fetch the OPEN session's newest-N + refresh the session list so
    // the transcript AND sidebar update live. Never blocks the UI thread: the
    // refetch is a std::async future polled here (same pattern as
    // transcriptFuture). The subscription itself is opened/torn-down by
    // ensure_subscription() on thread open/switch.
    // Poll EVERY open thread's live subscription. For each subscription whose
    // SSE worker flagged activity (debounced), kick a BACKGROUND refetch of
    // that thread's newest-N and write the fresh transcript straight to the
    // disk cache — even for threads that aren't currently focused — so
    // switching to any open tab shows already-fresh content instantly. If the
    // dirty thread IS the focused one, ALSO swap the result into openSession
    // (the visible transcript updates live). Never blocks the UI thread
    // (futures polled here). Subscriptions themselves are opened/reaped by
    // sync_subscriptions().
    void drive_live_events(AppComponent& app) {
        if (!app.client) return;
        const auto now = std::chrono::steady_clock::now();
        for (auto& [id, ls] : app.liveSubs) {
            // 1) Kick a background refetch when this thread's worker flagged
            //    activity, it's not already fetching, and past the debounce.
            if (ls.dirty->load() && !ls.pending &&
                now - ls.lastRefetch >= kEventDebounce) {
                ls.dirty->store(false);
                ls.lastRefetch = now;
                if (id == app.pane().selectedId) app.requestListRefresh = true;
                api::Client* c = app.client.get();
                std::string sid = id;
                ls.future = std::async(std::launch::async, [c, sid] {
                    return c->get_session(sid, kMessagesWindow);
                });
                ls.pending = true;
            }
            // 2) Service a completed background refetch: write to disk cache
            //    always; swap into the view only if this is the open thread.
            if (ls.pending && ls.future.valid() &&
                ls.future.wait_for(std::chrono::seconds(0)) ==
                    std::future_status::ready) {
                auto r = ls.future.get();
                ls.pending = false;
                if (r.ok) {
                    // Persist fresh transcript for ANY open tab (instant
                    // switch).
                    save_and_trim(app, r.value);
                    app.transcriptCache.put(r.value);
                    for (std::size_t paneIndex = 0;
                         paneIndex < app.active_pane_count(); ++paneIndex) {
                        Pane& pane = app.panes[paneIndex];
                        if (pane.selectedId != r.value.summary.id ||
                            pane.loadingOlder)
                            continue;
                        api::Session fresh = r.value;
                        reconcile_optimistic(pane, fresh);
                        apply_local_overlay(fresh.summary);
                        adopt_attach_brakes(app, fresh, /*authoritative=*/true);
                        adopt_attach_asks(app, fresh, /*authoritative=*/true);
                        pane.openSession = std::move(fresh);
                        pane.note_transcript_reset();
                        pane.transcriptState = LoadState::Loaded;
                        pane.transcriptError.clear();
                        pane.hasMoreOlder = pane.openSession->has_more_older;
                    }
                }
            }
        }
    }

    // Open a live (SSE) subscription bound to `id`, tearing down any previous
    // one first (so switching threads never leaks a worker/socket). No-op when
    // the backend doesn't support events (mock / unconfigured http) or when
    // already bound to this id. The subscription's worker callback ONLY flips
    // the atomic eventRefetch flag — it never touches the ECS, so it's safe to
    // fire from another thread; the loader services it on the UI-poll thread.
    // Keep the live-subscription pool in sync with the existing transcript LRU.
    // Visible panes are admitted first, then recently used open tabs. Cold tabs
    // stay durable on disk and take the existing async reload path when opened.
    // The same five-thread bound therefore owns transcript RAM and live workers;
    // there is no second recency policy to drift.
    void sync_subscriptions(AppComponent& app) {
        if (!app.client || !app.client->supports_events()) {
            // Backend can't stream: drop any stale pool (e.g. after a backend
            // swap) so we don't leak workers.
            reap_all(app);
            app.openThreadLive = false;
            return;
        }
        // Collect the set of currently-open tab session ids. A PREVIEW tab is
        // deliberately left out: a preview is a look at what the thread said
        // when you opened it, and holding a live subscription open behind every
        // row you glanced at is exactly the cost the preview is there to avoid.
        // Keeping the tab (a second click) subscribes it on the next tick.
        std::set<std::string> keptOpenIds;
        bool selectedIsPreview = false;
        if (auto* strip = find_singleton<TabStripComponent>()) {
            for (auto tabId : strip->tabOrder) {
                auto opt = afterhours::EntityHelper::getEntityForID(tabId);
                if (opt.valid() && opt->has<Tab>()) {
                    const auto& tab = opt->get<Tab>();
                    if (tab.sessionId.empty()) continue;
                    if (!tab.keptOpen) {
                        if (tab.sessionId == app.pane().selectedId)
                            selectedIsPreview = true;
                        continue;
                    }
                    keptOpenIds.insert(tab.sessionId);
                }
            }
        }
        if (!app.pane().selectedId.empty() && !selectedIsPreview)
            keptOpenIds.insert(app.pane().selectedId);

        std::vector<std::string> visible;
        visible.reserve(app.active_pane_count());
        for (std::size_t i = 0; i < app.active_pane_count(); ++i)
            visible.push_back(app.panes[i].selectedId);
        const auto hot = app.transcriptCache.live_hot_set(keptOpenIds, visible);
        std::set<std::string> openIds(hot.begin(), hot.end());

        // Reap subscriptions whose tab closed (detach so stop()/join is off the
        // UI thread — the blocking SSE read can sit up to the read timeout).
        for (auto it = app.liveSubs.begin(); it != app.liveSubs.end();) {
            if (openIds.count(it->first) == 0) {
                if (it->second.sub) {
                    std::thread([sub = std::move(it->second.sub)]() mutable {
                        sub->stop();
                        sub.reset();
                    }).detach();
                }
                it = app.liveSubs.erase(it);
            } else {
                ++it;
            }
        }
        // Open subscriptions for open tabs that don't have one yet.
        for (const auto& id : openIds) {
            if (app.liveSubs.count(id)) continue;
            AppComponent::LiveSub ls;
            std::shared_ptr<std::atomic<bool>> dirty = ls.dirty;
            std::atomic<long long>* stamp = &app.lastEventMs;
            api::EventSink sink;
            // Worker-thread callback: flip THIS thread's dirty flag + stamp the
            // global last-event time. Never touches the ECS (thread-safe).
            sink.on_activity = [dirty, stamp](const std::string&) {
                dirty->store(true);
                stamp->store(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count());
            };
            ls.sub = app.client->subscribe_events(id, std::move(sink));
            app.liveSubs.emplace(id, std::move(ls));
        }
        // The open thread is "live" iff it has an active subscription.
        app.openThreadLive =
            !app.pane().selectedId.empty() && app.liveSubs.count(app.pane().selectedId) &&
            app.liveSubs.at(app.pane().selectedId).sub != nullptr;
    }

    // Reap the whole pool off the UI thread (backend swap / shutdown).
    void reap_all(AppComponent& app) {
        for (auto& [id, ls] : app.liveSubs) {
            if (ls.sub) {
                std::thread([sub = std::move(ls.sub)]() mutable {
                    sub->stop();
                    sub.reset();
                }).detach();
            }
        }
        app.liveSubs.clear();
    }

    // ---- Streaming reply (Phase STREAM) ----------------------------------
    //
    // The visible payoff: an assistant reply that fills in token-by-token. The
    // frame-tick idiom mirrors the async futures above, but the "work" here is
    // draining a pre-built chunk queue a few tokens PER FRAME — deterministic,
    // offline for the mock, no worker thread and no timers.
    //
    // START (a streamed send was requested): append a User bubble + an EMPTY
    // Assistant bubble to the open transcript immediately, then collect the
    // reply's ordered text chunks up front via send_message_streaming (a sink
    // that captures deltas into a queue). This keeps the loader backend-generic
    // — any client that streams (or the default fallback that delivers one
    // delta) fills the same queue; only the DELIVERY granularity differs.
    //
    // DRAIN (each subsequent frame): move up to kTokensPerFrame chunks from the
    // queue into streamBuffer and rewrite the live Assistant message's text, so
    // the bubble grows. When the queue empties, finalize the message (stamp the
    // real id/created_at), refresh the cache, and mark the phase Done.
    void drive_stream(AppComponent& app) {
        // How many chunks to reveal per frame. >1 so a long reply doesn't take
        // hundreds of frames, but small enough that the growth is visibly
        // incremental. A screenshot demo can cap the drain (see below).
        constexpr size_t kTokensPerFrame = 2;

        // --- START (kick): collect the reply on a WORKER THREAD ---
        // A streamed send used to call send_message_streaming() directly here,
        // on the UI thread — which blocks the whole app (beachball) for the
        // entire network round-trip on a real backend. Instead we launch the
        // collection async and poll it below; the UI stays responsive while the
        // reply is gathered. The sink captures deltas into a queue; a
        // non-streaming backend simply yields one delta.
        if (!app.requestStreamPrompt.empty() && !app.streamActive &&
            !app.streamCollecting && !app.pane().selectedId.empty() && app.client &&
            app.pane().openSession &&
            app.pane().openSession->summary.id == app.pane().selectedId) {
            std::string prompt = app.requestStreamPrompt;
            std::string id = app.pane().selectedId;
            app.requestStreamPrompt.clear();
            // The outbox covers THIS path too, and for a long time it did not.
            // The write side was wired only into the synchronous reply above,
            // and every backend that ships -- the mock and agentcloud both --
            // reports supports_stream(), so the composer takes this branch and
            // the crash-safe log the local-first work was built for was never
            // written on the path the app actually runs. (COMMIT_AUDIT CB3
            // found the missing READER; this is the missing WRITER.)
            const bool fromOutbox = claim_outbox_dispatch(app, id, prompt);
            if (!fromOutbox) api::disk_cache::outbox_add(id, prompt);
            app.streamCollecting = true;
            app.streamPendingPrompt = prompt;
            app.streamPendingSession = id;
            app.streamPaneIndex = app.focusedPane;
            // Show the "thinking" affordance immediately so the send feels
            // instant even before the first chunk arrives.
            app.streamPhase = AppComponent::StreamPhase::Thinking;
            app.streamStartedAt = static_cast<int64_t>(std::time(nullptr));
            api::Client* c = app.client.get();
            app.streamCollectFuture = std::async(
                std::launch::async, [c, id, prompt]() {
                    AppComponent::StreamCollected out;
                    api::StreamSink sink;
                    sink.on_delta = [&out](const std::string& d) {
                        out.chunks.push_back(d);
                    };
                    sink.on_done = [&out](const api::Message& m) {
                        out.finalMsg = m;
                    };
                    sink.on_error = [&out](const std::string& e) {
                        out.error = e;
                    };
                    sink.on_event = [&out](const api::StreamEvent& ev) {
                        if (ev.kind == api::StreamEventKind::AsksChanged)
                            out.asksJson = ev.payload;
                    };
                    c->send_message_streaming(id, prompt, sink);
                    return out;
                });
        }

        // --- START (collect done): the worker finished gathering the reply ---
        if (app.streamCollecting && app.streamCollectFuture.valid() &&
            app.streamCollectFuture.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready) {
            AppComponent::StreamCollected got = app.streamCollectFuture.get();
            app.streamCollecting = false;
            const std::string id = app.streamPendingSession;
            const std::string prompt = app.streamPendingPrompt;
            app.streamPendingPrompt.clear();
            app.streamPendingSession.clear();
            const int ownerIndex = std::clamp(app.streamPaneIndex, 0, 1);
            Pane& streamPane = app.panes[static_cast<std::size_t>(ownerIndex)];

            // The open thread may have changed while we were collecting; only
            // apply the result if the target thread is still open. The OUTBOX
            // verdict does not depend on that: whether the server took the
            // prompt is decided by got.error, not by what the user is looking
            // at now.
            adopt_turn_asks(app, id, got.asksJson);
            if (got.error.empty()) {
                api::disk_cache::outbox_remove(id, prompt);
                app.outboxRetry.confirmed(id, prompt);
            } else {
                note_outbox_failure(app, id, prompt);
            }
            if (!streamPane.openSession || streamPane.openSession->summary.id != id) {
                app.streamPhase = AppComponent::StreamPhase::Idle;
            } else if (!got.error.empty()) {
                streamPane.transcriptError = got.error;
                app.streamPhase = AppComponent::StreamPhase::Idle;
                mark_local_prompt(streamPane, prompt, api::SyncState::Failed);
                sync_stream_transcript(app, ownerIndex);
            } else {
                // Append the User bubble + an empty Assistant bubble that fills
                // in as we drain. The live Assistant message's index is
                // remembered so the drain can rewrite its text each frame.
                // A restored/failed copy of this same prompt may already be
                // sitting in the transcript (drive_outbox paints one so the
                // user's words are visible before the retry lands); it is the
                // same turn, so it goes rather than doubling.
                drop_local_prompt(streamPane, prompt);
                api::Message um;
                um.role = api::Role::User;
                um.id = id + "-u" +
                        std::to_string(streamPane.openSession->messages.size());
                um.text = prompt;
                um.created_at = got.finalMsg.created_at;
                const std::size_t first =
                    streamPane.openSession->messages.size();
                streamPane.openSession->messages.push_back(std::move(um));

                api::Message assistant = got.finalMsg;
                assistant.text.clear();  // starts empty; fills as we drain.
                streamPane.openSession->messages.push_back(assistant);
                streamPane.note_transcript_append(first, 2);
                app.streamMsgIndex = streamPane.openSession->messages.size() - 1;
                sync_stream_transcript(app, ownerIndex);

                app.streamActive = true;
                app.streamSessionId = id;
                app.streamBuffer.clear();
                app.streamQueue = std::move(got.chunks);
                app.streamCursor = 0;
                app.streamFinal = got.finalMsg;
                app.streamPhase = app.streamQueue.empty()
                                      ? AppComponent::StreamPhase::Done
                                      : AppComponent::StreamPhase::Thinking;
            }
        }

        if (!app.streamActive) return;
        if (app.streamDemoHold) return;
        const int ownerIndex = std::clamp(app.streamPaneIndex, 0, 1);
        Pane& streamPane = app.panes[static_cast<std::size_t>(ownerIndex)];

        // If the open thread changed out from under an in-flight stream, drop
        // it cleanly rather than writing into the wrong transcript.
        if (!streamPane.openSession ||
            streamPane.openSession->summary.id != app.streamSessionId ||
            app.streamMsgIndex >= streamPane.openSession->messages.size()) {
            reset_stream(app);
            return;
        }

        // --- DRAIN a few chunks this frame ---
        // Screenshot affordance: HANABI_STREAM_DEMO_MAXTOKENS=<K> freezes the
        // drain after K chunks so a headless capture can photograph a genuine
        // MID-STREAM bubble (partial text + caret). K=0 (explicitly set) HOLDS
        // at the THINKING phase (no chunks drained) so the thinking indicator
        // can be captured. Ignored when unset; real rendered output, not a mock.
        static const bool kDemoCapSet = [] {
            const char* v = std::getenv("HANABI_STREAM_DEMO_MAXTOKENS");
            return v && *v;
        }();
        static const size_t kDemoCap = [] () -> size_t {
            if (const char* v = std::getenv("HANABI_STREAM_DEMO_MAXTOKENS");
                v && *v) {
                long n = std::atol(v);
                if (n >= 0) return static_cast<size_t>(n);
            }
            return 0;
        }();
        if (kDemoCapSet && app.streamCursor >= kDemoCap) {
            // Held for the demo capture. At K==0 keep the phase THINKING (no
            // tokens yet → thinking indicator shows); at K>0 keep it Streaming
            // so the mid-stream caret shows.
            app.streamPhase = (kDemoCap == 0)
                                  ? AppComponent::StreamPhase::Thinking
                                  : AppComponent::StreamPhase::Streaming;
            return;
        }

        size_t drained = 0;
        while (app.streamCursor < app.streamQueue.size() &&
               drained < kTokensPerFrame) {
            app.streamBuffer += app.streamQueue[app.streamCursor];
            ++app.streamCursor;
            ++drained;
        }
        if (drained > 0)
            app.streamPhase = AppComponent::StreamPhase::Streaming;

        api::Message& live =
            streamPane.openSession->messages[app.streamMsgIndex];
        live.text = app.streamBuffer;

        if (app.streamCursor >= app.streamQueue.size()) {
            live.text = app.streamFinal.text;
            if (!app.streamFinal.id.empty()) live.id = app.streamFinal.id;
            if (app.streamFinal.created_at != 0)
                live.created_at = app.streamFinal.created_at;
            streamPane.note_transcript_update(app.streamMsgIndex);
            sync_stream_message(app, ownerIndex, app.streamMsgIndex);
            app.transcriptCache.put(*streamPane.openSession);
            app.streamPhase = AppComponent::StreamPhase::Done;
            reset_stream(app, /*keepPhase=*/true);
        } else {
            streamPane.note_transcript_update(app.streamMsgIndex);
            sync_stream_message(app, ownerIndex, app.streamMsgIndex);
        }
    }

    // Clear the transient stream fields. keepPhase leaves streamPhase alone so
    // a just-completed stream can stay Done for the render this frame.
    void reset_stream(AppComponent& app, bool keepPhase = false) {
        app.streamActive = false;
        app.streamSessionId.clear();
        app.streamQueue.clear();
        app.streamCursor = 0;
        app.streamBuffer.clear();
        app.streamMsgIndex = 0;
        if (!keepPhase) app.streamPhase = AppComponent::StreamPhase::Idle;
    }
};

}  // namespace ecs
