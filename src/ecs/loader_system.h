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
    }
};

}  // namespace ecs
