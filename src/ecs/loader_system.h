#pragma once

// Drives async data loading. Reads request flags on AppComponent, launches
// std::async fetches against the Client, and polls the futures each frame,
// writing results back into AppComponent. Keeps the UI thread responsive.

#include <chrono>
#include <cstdlib>
#include <future>

#include "../settings.h"
#include "../api/disk_cache.h"
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
    static bool disk_cache_enabled(const AppComponent& app) {
        return app.backend_label == "http";
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

    void for_each_with(Entity&, AppComponent& app, float) override {
        if (!app.client) return;

        // --- Auth: deferred device-code begin() (launch-perf) ---
        // begin() does a BLOCKING network POST; main.cpp deferred it off the
        // launch path by setting authNeedsBegin. Kick it on a worker exactly
        // like the list fetch so the window paints immediately. The flow is not
        // thread-safe, so while the begin() future is in flight nothing else
        // touches app.authFlow (app_frame's poll_step is guarded on
        // authBeginPending). When it resolves the flow is in AwaitingUser/Failed
        // and the normal frame-driven poll takes over.
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
        if (!app.selectedId.empty() && app.sending_for(app.selectedId)) {
            if (!app.requestStreamPrompt.empty()) {
                app.enqueue_send(app.selectedId,
                                 std::move(app.requestStreamPrompt));
                app.requestStreamPrompt.clear();
            }
            if (!app.requestSendPrompt.empty()) {
                app.enqueue_send(app.selectedId,
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
                    app.sessions = std::move(*cached);
                    for (auto& s : app.sessions)
                        if (Settings::get().is_starred(s.id)) s.starred = true;
                    app.listState = LoadState::Loaded;  // show stale now
                    if (app.selectedId.empty() && !app.sessions.empty())
                        app.requestOpenId = app.sessions.front().id;
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
                    app.sessions = std::move(r.value);
                    // Re-apply the user's persisted stars over whatever the
                    // backend reported (Settings is the durable source of truth
                    // for starring — Phase I). Without this, a star flipped in a
                    // prior launch would be lost because the mock/backend seeds
                    // its own starred flags fresh each list fetch.
                    for (auto& s : app.sessions)
                        if (Settings::get().is_starred(s.id)) s.starred = true;
                    app.listState = LoadState::Loaded;
                    app.listError.clear();
                    // Persist the fresh list for the next launch's instant paint.
                    if (disk_cache_enabled(app))
                        api::disk_cache::save_sessions(app.sessions);
                    // Auto-open the first session if nothing selected yet.
                    if (app.selectedId.empty() && !app.sessions.empty())
                        app.requestOpenId = app.sessions.front().id;
                    // Screenshot affordance: HANABI_STREAM_DEMO forces the
                    // first thread OPEN in the Chat transcript so a headless
                    // capture has an openSession for the composer's stream demo
                    // to fire into (main.cpp's wait/capture path doesn't open a
                    // tab on its own). Mirrors HANABI_VIEW; ignored when unset;
                    // no network (the mock resolves the transcript from cache).
                    if (const char* d = std::getenv("HANABI_STREAM_DEMO");
                        d && *d && !app.sessions.empty()) {
                        app.view = SmartView::Chat;
                        app.selectedId = app.sessions.front().id;
                        app.requestOpenId = app.sessions.front().id;
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

        // --- Transcript ---
        if (!app.requestOpenId.empty() && !app.transcriptPending) {
            std::string id = app.requestOpenId;
            app.requestOpenId.clear();
            app.selectedId = id;
            // Refresh this thread's disk-cache recency (mtime) so the cache-cap
            // eviction's LRU ordering reflects OPENS, not just saves — the
            // least-recently-OPENED thread is trimmed first when over cap.
            if (disk_cache_enabled(app)) api::disk_cache::touch_transcript(id);

            // Phase X fast path: cache HIT -> render synchronously, no async
            // round-trip, no Loading flash. Marks the thread most-recently-used
            // so "last 5 interacted with" stays accurate on every open/switch.
            if (auto hit = app.transcriptCache.get(id)) {
                app.openSession = std::move(*hit);
                app.transcriptState = LoadState::Loaded;
                app.transcriptError.clear();
                app.transcriptLoadingId.clear();  // nothing loading now
                app.hasMoreOlder = app.openSession->has_more_older;
                // Mock is static -> cache is authoritative (no revalidation).
                // SEAM: a live backend would kick a background revalidate here
                // and swap in fresh data if it changed. Not built for the mock.
                // Bind the live (SSE) subscription to this session.
                ensure_subscription(app, id);
            } else {
                // MISS in the in-memory LRU. FEATURE #1 (never beachball): the
                // disk-cache read USED to happen synchronously RIGHT HERE, on
                // the UI thread — open + JSON-parse of an up-to-690-message
                // transcript file. On a big thread that blocked the UI thread
                // for tens of ms (the beachball). Now we:
                //   (a) set transcriptState=Loading + transcriptLoadingId
                //       IMMEDIATELY (trivially cheap) so the pane can paint a
                //       spinner on the very next frame, and
                //   (b) launch the disk read on a WORKER THREAD (diskReadFuture)
                //       and the network revalidate on ANOTHER worker
                //       (transcriptFuture) — the UI thread does NEITHER the
                //       disk read/parse NOR the network. Whichever lands first
                //       (disk = stale paint, network = fresh) is applied by the
                //       pollers below.
                // The heavy parse/window work is entirely off the UI thread.
                app.transcriptState = LoadState::Loading;
                app.transcriptLoadingId = id;

                // (a) Disk-cache read on a worker (stale-while-revalidate).
                if (disk_cache_enabled(app)) {
                    app.diskReadPending = true;
                    app.diskReadId = id;
                    app.diskReadFuture = std::async(
                        std::launch::async,
                        [id] { return api::disk_cache::load_transcript(id); });
                }
                // (b) Network revalidate / first-ever load, newest-N only.
                app.transcriptPending = true;
                app.transcriptPendingId = id;
                api::Client* c = app.client.get();
                app.transcriptFuture = std::async(
                    std::launch::async,
                    [c, id] { return c->get_session(id, kMessagesWindow); });
                // Bind the live (SSE) subscription to this session.
                ensure_subscription(app, id);
            }
        }
        // Poll the worker-thread disk-cache read (stale paint). Lands ahead of
        // — or alongside — the network fetch; applied only if this is still the
        // thread the user wants AND the network hasn't already painted it.
        if (app.diskReadPending && app.diskReadFuture.valid() &&
            app.diskReadFuture.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready) {
            auto disk = app.diskReadFuture.get();
            app.diskReadPending = false;
            if (disk && app.selectedId == app.diskReadId &&
                // Don't clobber a fresh network result that already landed.
                app.transcriptState != LoadState::Loaded) {
                app.transcriptCache.put(*disk);
                app.openSession = std::move(*disk);
                app.transcriptState = LoadState::Loaded;  // show stale now
                app.transcriptError.clear();
                app.hasMoreOlder = app.openSession->has_more_older;
                // A stale paint clears the spinner for THIS thread; the network
                // revalidate still runs in the background (no Loading flash).
                if (app.transcriptLoadingId == app.diskReadId)
                    app.transcriptLoadingId.clear();
            }
        }
        if (app.transcriptPending && app.transcriptFuture.valid()) {
            if (app.transcriptFuture.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready) {
                auto r = app.transcriptFuture.get();
                app.transcriptPending = false;
                if (r.ok) {
                    // Insert into the cache (capped to the last 20 msgs) and
                    // mark most-recently-used, then render. Also persist to disk
                    // for the next session's instant (stale) paint.
                    app.transcriptCache.put(r.value);
                    save_and_trim(app, r.value);
                    // Only swap into the view if this is still the open thread
                    // (the user may have switched tabs during a slow fetch).
                    if (app.selectedId == r.value.summary.id) {
                        app.openSession = std::move(r.value);
                        app.transcriptState = LoadState::Loaded;
                        app.transcriptError.clear();
                        app.hasMoreOlder = app.openSession->has_more_older;
                        // Fresh data landed — clear the "loading this thread"
                        // spinner flag for this id.
                        if (app.transcriptLoadingId ==
                            app.openSession->summary.id)
                            app.transcriptLoadingId.clear();
                    }
                } else {
                    // Network fetch failed. If we already painted a stale copy
                    // from disk/LRU, KEEP it rather than blanking the pane on a
                    // transient slow-network error; only surface the error when
                    // there's nothing to show.
                    app.transcriptError = r.error;
                    if (app.openSession &&
                        app.openSession->summary.id == app.transcriptPendingId) {
                        app.transcriptState = LoadState::Loaded;  // keep stale
                    } else {
                        app.openSession.reset();
                        app.transcriptState = LoadState::Error;
                    }
                    // Fetch resolved (success or fail) for this thread — stop
                    // showing the spinner. A pending disk read (if any) may
                    // still paint a stale copy afterwards.
                    if (app.transcriptLoadingId == app.transcriptPendingId &&
                        !app.diskReadPending)
                        app.transcriptLoadingId.clear();
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
                    // Refresh the list so the new thread appears, and open it.
                    app.requestListRefresh = true;
                    app.requestOpenId = r.value;
                } else {
                    // Surface the failure on the list rail (non-fatal).
                    app.listError = r.error;
                }
            }
        }

        // --- Reply (transcript composer "Send" -> continue the open thread) ---
        if (!app.requestSendPrompt.empty() && !app.sendPending &&
            !app.selectedId.empty()) {
            std::string prompt = app.requestSendPrompt;
            std::string id = app.selectedId;
            app.requestSendPrompt.clear();
            app.sendPending = true;
            app.sendSessionId = id;
            app.sendingPrompt = prompt;
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
                    // Append BOTH the user's prompt and the returned assistant
                    // reply to the open transcript, then refresh the cache so a
                    // later re-open shows the full exchange. The user message is
                    // reconstructed here (the client returns only the assistant
                    // reply) so the transcript reads as a full turn.
                    if (app.openSession &&
                        app.openSession->summary.id == app.sendSessionId) {
                        api::Message um;
                        um.role = api::Role::User;
                        um.id = app.sendSessionId + "-u" +
                                std::to_string(app.openSession->messages.size());
                        um.text = userText;
                        um.created_at = r.value.created_at;
                        app.openSession->messages.push_back(std::move(um));
                        app.openSession->messages.push_back(r.value);
                        app.transcriptCache.put(*app.openSession);
                    }
                } else {
                    app.transcriptError = r.error;
                }
            }
        }

        drive_stream(app);
        drive_load_older(app);
        drive_live_events(app);
        drive_send_queue(app);
        drive_settings(app);
    }

  private:
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
            if (!app.requestSendPrompt.empty() && app.selectedId == id) continue;
            if (!app.requestStreamPrompt.empty() && app.selectedId == id)
                continue;
            // Dispatch this one. The existing reply/stream START blocks read
            // requestSendPrompt/requestStreamPrompt for app.selectedId, so a
            // queued send only fires against the OPEN thread — which is the
            // only thread the composer can target anyway. If the queued send
            // is for a non-open thread, hold it until that thread is opened
            // (keeps ordering; never sends into the wrong transcript).
            if (app.selectedId != id) continue;
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

    // ---- Load OLDER (full transcript on demand) --------------------------
    //
    // The render side sets app.requestLoadOlder when the user scrolls to the
    // top of a windowed transcript (app.hasMoreOlder == true). Since the
    // backend has NO working backward cursor yet (offset/before are ignored),
    // "load older" is serviced by re-fetching the FULL transcript once (no
    // limit) and replacing openSession->messages, preserving the open session.
    // This is the documented interim until the backend cursor lands — at which
    // point this becomes an incremental page-back instead of a full re-fetch.
    void drive_load_older(AppComponent& app) {
        if (app.requestLoadOlder && !app.loadingOlder && app.client &&
            !app.selectedId.empty()) {
            app.requestLoadOlder = false;
            app.loadingOlder = true;
            std::string id = app.selectedId;
            api::Client* c = app.client.get();
            // limit=0 => the FULL transcript (no ?limit query).
            app.liveFuture = std::async(std::launch::async,
                                        [c, id] { return c->get_session(id, 0); });
            app.livePending = true;
            app.livePendingId = id;
        }
        service_transcript_swap(app, /*fromLoadOlder=*/true);
    }

    // ---- Live events (SSE) -----------------------------------------------
    //
    // Poll the atomic refetch flag the subscription worker flips, debounce it,
    // and re-fetch the OPEN session's newest-N + refresh the session list so
    // the transcript AND sidebar update live. Never blocks the UI thread: the
    // refetch is a std::async future polled here (same pattern as
    // transcriptFuture). The subscription itself is opened/torn-down by
    // ensure_subscription() on thread open/switch.
    void drive_live_events(AppComponent& app) {
        // 1) A worker-thread event asked us to refetch. Debounce + kick.
        if (app.eventRefetch.load() && !app.livePending && !app.loadingOlder &&
            app.client && !app.selectedId.empty()) {
            const auto now = std::chrono::steady_clock::now();
            if (now - app.lastEventRefetch >= kEventDebounce) {
                app.eventRefetch.store(false);
                app.lastEventRefetch = now;
                // Refresh the sidebar so new activity reorders it.
                app.requestListRefresh = true;
                std::string id = app.selectedId;
                api::Client* c = app.client.get();
                app.liveFuture = std::async(
                    std::launch::async,
                    [c, id] { return c->get_session(id, kMessagesWindow); });
                app.livePending = true;
                app.livePendingId = id;
            }
        }
        service_transcript_swap(app, /*fromLoadOlder=*/false);
    }

    // Shared: poll the liveFuture used by BOTH drive_load_older and
    // drive_live_events (mutually exclusive — one at a time) and swap the
    // result into the open transcript if it's still the selected thread.
    void service_transcript_swap(AppComponent& app, bool fromLoadOlder) {
        if (!app.livePending || !app.liveFuture.valid()) return;
        if (app.liveFuture.wait_for(std::chrono::seconds(0)) !=
            std::future_status::ready)
            return;
        auto r = app.liveFuture.get();
        app.livePending = false;
        if (fromLoadOlder) app.loadingOlder = false;
        if (!r.ok) {
            // Non-fatal: keep whatever is on screen; surface the error.
            app.transcriptError = r.error;
            return;
        }
        // Only swap if this is still the open thread (the user may have
        // switched during the fetch).
        if (app.selectedId != r.value.summary.id) return;
        app.transcriptCache.put(r.value);
        save_and_trim(app, r.value);
        app.openSession = std::move(r.value);
        app.transcriptState = LoadState::Loaded;
        app.transcriptError.clear();
        app.hasMoreOlder = app.openSession->has_more_older;
    }

    // Open a live (SSE) subscription bound to `id`, tearing down any previous
    // one first (so switching threads never leaks a worker/socket). No-op when
    // the backend doesn't support events (mock / unconfigured http) or when
    // already bound to this id. The subscription's worker callback ONLY flips
    // the atomic eventRefetch flag — it never touches the ECS, so it's safe to
    // fire from another thread; the loader services it on the UI-poll thread.
    void ensure_subscription(AppComponent& app, const std::string& id) {
        if (!app.client || !app.client->supports_events()) return;
        if (app.subscribedId == id && app.eventSub) return;
        // FEATURE #1 (never beachball): tearing down the previous subscription
        // must NOT block the UI thread. eventSub->stop() JOINS the SSE worker,
        // whose blocking read can sit for up to the read timeout — so calling
        // it here on the switch path is a UI-thread stall on every thread
        // switch. Instead, hand the old handle to a DETACHED reaper thread that
        // stops+joins+destroys it in the background; the UI thread returns
        // immediately. (The handle is owned solely by the reaper via move, so
        // there's no lifetime race with the ECS.)
        if (app.eventSub) {
            std::thread([sub = std::move(app.eventSub)]() mutable {
                sub->stop();   // joins the worker off the UI thread
                sub.reset();   // destroy the handle here, not on the UI thread
            }).detach();
            app.eventSub.reset();  // already moved-from; make it explicit
        }
        app.subscribedId = id;
        std::atomic<bool>* flag = &app.eventRefetch;
        std::atomic<long long>* stamp = &app.lastEventMs;
        api::EventSink sink;
        sink.on_activity = [flag, stamp](const std::string&) {
            // Cheap + thread-safe: mark "something changed" AND record when, so
            // the status bar can briefly flash a "live" indicator. The loader
            // polls the flag on the UI thread, debounces, and refetches.
            flag->store(true);
            stamp->store(std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count());
        };
        // on_error left unset: a failed stream just stops (capped reconnects
        // happen inside the subscription); the transcript still refreshes on
        // open/switch, so a dead stream degrades gracefully.
        app.eventSub = app.client->subscribe_events(id, std::move(sink));
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
            !app.streamCollecting && !app.selectedId.empty() && app.client &&
            app.openSession &&
            app.openSession->summary.id == app.selectedId) {
            std::string prompt = app.requestStreamPrompt;
            std::string id = app.selectedId;
            app.requestStreamPrompt.clear();
            app.streamCollecting = true;
            app.streamPendingPrompt = prompt;
            app.streamPendingSession = id;
            // Show the "thinking" affordance immediately so the send feels
            // instant even before the first chunk arrives.
            app.streamPhase = AppComponent::StreamPhase::Thinking;
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

            // The open thread may have changed while we were collecting; only
            // apply the result if the target thread is still open.
            if (!app.openSession || app.openSession->summary.id != id) {
                app.streamPhase = AppComponent::StreamPhase::Idle;
            } else if (!got.error.empty()) {
                app.transcriptError = got.error;
                app.streamPhase = AppComponent::StreamPhase::Idle;
            } else {
                // Append the User bubble + an empty Assistant bubble that fills
                // in as we drain. The live Assistant message's index is
                // remembered so the drain can rewrite its text each frame.
                api::Message um;
                um.role = api::Role::User;
                um.id = id + "-u" +
                        std::to_string(app.openSession->messages.size());
                um.text = prompt;
                um.created_at = got.finalMsg.created_at;
                app.openSession->messages.push_back(std::move(um));

                api::Message assistant = got.finalMsg;
                assistant.text.clear();  // starts empty; fills as we drain.
                app.openSession->messages.push_back(assistant);
                app.streamMsgIndex = app.openSession->messages.size() - 1;

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

        // If the open thread changed out from under an in-flight stream, drop
        // it cleanly rather than writing into the wrong transcript.
        if (!app.openSession ||
            app.openSession->summary.id != app.streamSessionId ||
            app.streamMsgIndex >= app.openSession->messages.size()) {
            reset_stream(app);
            return;
        }

        // --- DRAIN a few chunks this frame ---
        // Screenshot affordance: HANABI_STREAM_DEMO_MAXTOKENS=<K> freezes the
        // drain after K chunks so a headless capture can photograph a genuine
        // MID-STREAM bubble (partial text + caret). Ignored when unset; real
        // rendered output, not a mock. Read once, cached.
        static const size_t kDemoCap = [] () -> size_t {
            if (const char* v = std::getenv("HANABI_STREAM_DEMO_MAXTOKENS");
                v && *v) {
                long n = std::atol(v);
                if (n > 0) return static_cast<size_t>(n);
            }
            return 0;  // 0 = no cap.
        }();
        if (kDemoCap != 0 && app.streamCursor >= kDemoCap) {
            // Held mid-stream for the demo capture; keep the phase Streaming so
            // the caret affordance shows.
            app.streamPhase = AppComponent::StreamPhase::Streaming;
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

        // Rewrite the live Assistant bubble's text with what we have so far.
        app.openSession->messages[app.streamMsgIndex].text = app.streamBuffer;

        // --- DONE ---
        if (app.streamCursor >= app.streamQueue.size()) {
            // Finalize: stamp the real id/created_at, ensure the full text is
            // in place, refresh the cache so a re-open shows the complete turn.
            api::Message& live = app.openSession->messages[app.streamMsgIndex];
            live.text = app.streamFinal.text;
            if (!app.streamFinal.id.empty()) live.id = app.streamFinal.id;
            if (app.streamFinal.created_at != 0)
                live.created_at = app.streamFinal.created_at;
            app.transcriptCache.put(*app.openSession);
            app.streamPhase = AppComponent::StreamPhase::Done;
            reset_stream(app, /*keepPhase=*/true);
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
