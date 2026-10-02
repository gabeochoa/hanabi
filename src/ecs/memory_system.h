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
