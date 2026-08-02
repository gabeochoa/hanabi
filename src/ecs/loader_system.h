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

    void for_each_with(Entity&, AppComponent& app, float) override {
        if (!app.client) return;

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

            // Phase X fast path: cache HIT -> render synchronously, no async
            // round-trip, no Loading flash. Marks the thread most-recently-used
            // so "last 5 interacted with" stays accurate on every open/switch.
            if (auto hit = app.transcriptCache.get(id)) {
                app.openSession = std::move(*hit);
                app.transcriptState = LoadState::Loaded;
                app.transcriptError.clear();
                app.hasMoreOlder = app.openSession->has_more_older;
                // Mock is static -> cache is authoritative (no revalidation).
                // SEAM: a live backend would kick a background revalidate here
                // and swap in fresh data if it changed. Not built for the mock.
                // Bind the live (SSE) subscription to this session.
                ensure_subscription(app, id);
            } else {
                // MISS in the in-memory LRU. Before falling back to a
                // (possibly slow) network fetch, try the ON-DISK cache: if this
                // thread was opened in a prior session, paint it INSTANTLY from
                // disk so a slow-network open shows the transcript immediately.
                // We then STILL kick the async fetch below to revalidate and
                // swap in fresh data when it arrives (stale-while-revalidate).
                bool paintedStale = false;
                if (auto disk = disk_cache_enabled(app)
                                    ? api::disk_cache::load_transcript(id)
                                    : std::nullopt) {
                    app.transcriptCache.put(*disk);
                    app.openSession = std::move(*disk);
                    app.transcriptState = LoadState::Loaded;  // show stale now
                    app.transcriptError.clear();
                    app.hasMoreOlder = app.openSession->has_more_older;
                    paintedStale = true;
                }
                // Kick the network fetch to revalidate (or to do the first-ever
                // load when there's no disk copy). When we've already painted
                // stale, this refresh happens in the background without a
                // Loading flash. MEMORY-LIGHT: fetch only the newest N (not the
                // full transcript) so a huge thread doesn't balloon RAM — you
                // open at the bottom, older messages load on demand.
                app.transcriptPending = true;
                app.transcriptPendingId = id;
                if (!paintedStale) app.transcriptState = LoadState::Loading;
                api::Client* c = app.client.get();
                app.transcriptFuture = std::async(
                    std::launch::async,
                    [c, id] { return c->get_session(id, kMessagesWindow); });
                // Bind the live (SSE) subscription to this session.
                ensure_subscription(app, id);
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
                    if (disk_cache_enabled(app))
                        api::disk_cache::save_transcript(r.value);
                    // Only swap into the view if this is still the open thread
                    // (the user may have switched tabs during a slow fetch).
                    if (app.selectedId == r.value.summary.id) {
                        app.openSession = std::move(r.value);
                        app.transcriptState = LoadState::Loaded;
                        app.transcriptError.clear();
                        app.hasMoreOlder = app.openSession->has_more_older;
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
    }

  private:
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
        if (disk_cache_enabled(app))
            api::disk_cache::save_transcript(r.value);
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
        // Tear down the previous subscription (joins its worker) before
        // rebinding — never leak.
        if (app.eventSub) {
            app.eventSub->stop();
            app.eventSub.reset();
        }
        app.subscribedId = id;
        std::atomic<bool>* flag = &app.eventRefetch;
        api::EventSink sink;
        sink.on_activity = [flag](const std::string&) {
            // Cheap + thread-safe: just mark "something changed". The loader
            // polls this on the UI thread, debounces, and refetches.
            flag->store(true);
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
