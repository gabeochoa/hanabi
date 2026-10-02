#pragma once

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
