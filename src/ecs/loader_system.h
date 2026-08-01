#pragma once

// Drives async data loading. Reads request flags on AppComponent, launches
// std::async fetches against the Client, and polls the futures each frame,
// writing results back into AppComponent. Keeps the UI thread responsive.

#include <chrono>
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
    }
};

}  // namespace ecs
