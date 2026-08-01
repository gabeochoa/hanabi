#pragma once

// Drives async data loading. Reads request flags on AppComponent, launches
// std::async fetches against the Client, and polls the futures each frame,
// writing results back into AppComponent. Keeps the UI thread responsive.

#include <chrono>
#include <cstdlib>
#include <future>

#include "ui_imports.h"

namespace ecs {

struct LoaderSystem : afterhours::System<AppComponent> {
    void for_each_with(Entity&, AppComponent& app, float) override {
        if (!app.client) return;

        // --- Session list ---
        if (app.requestListRefresh && !app.listPending) {
            app.requestListRefresh = false;
            app.listPending = true;
            app.listState = LoadState::Loading;
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
                    app.listState = LoadState::Loaded;
                    app.listError.clear();
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
                    app.listState = LoadState::Error;
                    app.listError = r.error;
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
                // Mock is static -> cache is authoritative (no revalidation).
                // SEAM: a live backend would kick a background revalidate here
                // and swap in fresh data if it changed. Not built for the mock.
            } else {
                // MISS: existing async fetch path.
                app.transcriptPending = true;
                app.transcriptPendingId = id;
                app.transcriptState = LoadState::Loading;
                api::Client* c = app.client.get();
                app.transcriptFuture = std::async(
                    std::launch::async, [c, id] { return c->get_session(id); });
            }
        }
        if (app.transcriptPending && app.transcriptFuture.valid()) {
            if (app.transcriptFuture.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready) {
                auto r = app.transcriptFuture.get();
                app.transcriptPending = false;
                if (r.ok) {
                    // Insert into the cache (capped to the last 20 msgs) and
                    // mark most-recently-used, then render.
                    app.transcriptCache.put(r.value);
                    app.openSession = std::move(r.value);
                    app.transcriptState = LoadState::Loaded;
                    app.transcriptError.clear();
                } else {
                    app.openSession.reset();
                    app.transcriptState = LoadState::Error;
                    app.transcriptError = r.error;
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
    }

  private:
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

        // --- START ---
        if (!app.requestStreamPrompt.empty() && !app.streamActive &&
            !app.selectedId.empty() && app.client &&
            app.openSession &&
            app.openSession->summary.id == app.selectedId) {
            std::string prompt = app.requestStreamPrompt;
            std::string id = app.selectedId;
            app.requestStreamPrompt.clear();

            // Collect the reply's chunks + final Message up front. The sink
            // captures every text delta into a local queue; a non-streaming
            // backend simply yields one delta. No UI mutation happens in here.
            std::vector<std::string> chunks;
            api::Message finalMsg;
            std::string streamErr;
            api::StreamSink sink;
            sink.on_delta = [&chunks](const std::string& d) {
                chunks.push_back(d);
            };
            sink.on_done = [&finalMsg](const api::Message& m) { finalMsg = m; };
            sink.on_error = [&streamErr](const std::string& e) {
                streamErr = e;
            };
            app.client->send_message_streaming(id, prompt, sink);

            if (!streamErr.empty()) {
                app.transcriptError = streamErr;
                return;
            }

            // Append the User bubble + an empty Assistant bubble that will fill
            // in as we drain. The live Assistant message's index is remembered
            // so the drain can rewrite its text each frame.
            api::Message um;
            um.role = api::Role::User;
            um.id = id + "-u" +
                    std::to_string(app.openSession->messages.size());
            um.text = prompt;
            um.created_at = finalMsg.created_at;
            app.openSession->messages.push_back(std::move(um));

            api::Message assistant = finalMsg;
            assistant.text.clear();  // starts empty; fills as we drain.
            app.openSession->messages.push_back(assistant);
            app.streamMsgIndex = app.openSession->messages.size() - 1;

            app.streamActive = true;
            app.streamSessionId = id;
            app.streamBuffer.clear();
            app.streamQueue = std::move(chunks);
            app.streamCursor = 0;
            app.streamFinal = finalMsg;
            app.streamPhase = app.streamQueue.empty()
                                  ? AppComponent::StreamPhase::Done
                                  : AppComponent::StreamPhase::Thinking;
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
