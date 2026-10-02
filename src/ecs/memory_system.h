#pragma once

#include <ctime>

// Services Settings > Memory's requests (memory_page.h) once a frame.

#include "components.h"
#include "memory_page.h"

namespace ecs {

struct MemorySystem : afterhours::System<AppComponent> {
    void for_each_with(afterhours::Entity&, AppComponent& app, float) override {
        service_memory(app.memory, app.client);
        // The Spaces list for the @ picker: asked once, off the frame; a
        // failure leaves the picker with threads only.
        using namespace std::chrono_literals;
        if (!app.spacesRequested && app.client && app.client->supports_spaces()) {
            app.spacesRequested = true;
            auto c = app.client;
            app.spacesFuture = std::async(std::launch::async, [c] { return c->list_spaces(); });
        }
        // Pins and archives on their way to the server (kt-if8e).
        if (!app.overlayWriteQueue.empty() && app.client && app.client->supports_overlay_writes()) {
            for (auto& [pin, id, on] : app.overlayWriteQueue) {
                AppComponent::OverlayWrite w;
                w.pin = pin;
                w.id = id;
                w.on = on;
                auto c = app.client;
                const bool isPin = pin;
                const std::string sid = id;
                const bool value = on;
                w.future = std::async(std::launch::async, [c, isPin, sid, value] {
                    return isPin ? c->set_pinned(sid, value) : c->set_archived(sid, value);
                });
                app.overlayWrites.push_back(std::move(w));
            }
        }
        app.overlayWriteQueue.clear();
        for (auto it = app.overlayWrites.begin(); it != app.overlayWrites.end();) {
            if (!it->future.valid() || it->future.wait_for(0s) != std::future_status::ready) {
                ++it;
                continue;
            }
            auto r = it->future.get();
            if (!r.ok)
                app.raise_toast(std::string(it->pin ? (it->on ? "Pinned" : "Unpinned")
                                                    : (it->on ? "Archived" : "Unarchived")) +
                                    " on this Mac; the server did not take it (" + r.error + ")",
                                std::string(), AppComponent::ToastUndo::None);
            it = app.overlayWrites.erase(it);
        }
        // The Sensitive-mode gate: asked once; anything but a clear admission
        // is a refusal (the control is then not offered at all).
        if (!app.sensitiveGateAsked && app.client && app.client->supports_graphql()) {
            app.sensitiveGateAsked = true;
            auto c = app.client;
            app.sensitiveGateFuture = std::async(std::launch::async, [c] {
                return c->graphql(api::companion::sensitive_gate_body());
            });
        }
        if (app.sensitiveGateFuture.valid() &&
            app.sensitiveGateFuture.wait_for(0s) == std::future_status::ready) {
            auto r = app.sensitiveGateFuture.get();
            app.sensitiveGate = r.ok && api::companion::sensitive_gate_admits(r.value)
                                    ? AppComponent::Gate::Admitted
                                    : AppComponent::Gate::Denied;
        }
        // The session->Space index: a bounded paged walk off the frame, every
        // ten minutes; a failed walk leaves the last filing in place.
        {
            const double now = static_cast<double>(std::time(nullptr));
            if (!app.spaceIndexFuture.valid() && app.client && app.client->supports_graphql() &&
                (app.spaceIndexAt < 0.0 || now - app.spaceIndexAt >= 600.0)) {
                app.spaceIndexAt = now;
                auto c = app.client;
                app.spaceIndexFuture = std::async(std::launch::async, [c] {
                    using Filed = std::vector<std::pair<std::string, std::string>>;
                    Filed all;
                    std::string after;
                    for (int page = 0; page < api::spaces::kIndexMaxPages; ++page) {
                        auto r = c->graphql(api::spaces::index_body(after));
                        if (!r.ok)
                            return page == 0 ? api::Result<Filed>::failure(r.error)
                                             : api::Result<Filed>::success(std::move(all));
                        auto p = api::spaces::parse_index(r.value);
                        all.insert(all.end(), p.filed.begin(), p.filed.end());
                        if (p.next.empty()) break;
                        after = p.next;
                    }
                    return api::Result<Filed>::success(std::move(all));
                });
            }
            if (app.spaceIndexFuture.valid() &&
                app.spaceIndexFuture.wait_for(0s) == std::future_status::ready) {
                auto r = app.spaceIndexFuture.get();
                if (r.ok) {
                    std::unordered_map<std::string, std::string> next(r.value.begin(), r.value.end());
                    if (next != app.sessionSpace) {
                        app.sessionSpace = std::move(next);
                        app.apply_space_filing(app.sessions);
                        app.mark_session_catalog_changed();
                    }
                }
            }
        }
        if (app.spacesFuture.valid() &&
            app.spacesFuture.wait_for(0s) == std::future_status::ready) {
            auto r = app.spacesFuture.get();
            if (r.ok) {
                app.spaces = std::move(r.value);
                ++app.spacesRevision;
            }
        }
    }
};

}  // namespace ecs
