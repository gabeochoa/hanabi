#pragma once

// ---------------------------------------------------------------------------
// Native intake: pasted and dropped images become composer attachments.
//
// AppKit observes (an image on the pasteboard, a file let go of over the
// window) and this is the one place that turns those observations into app
// state — the same single-owner rule the hotkey and the deep-link follow, one
// level in: the native seam latches, the frame reads, the immediate-mode core
// owns what is true.
//
// It is a SYSTEM rather than a block in main.cpp's app_frame() because
// app_frame is the windowed host loop only. The scripted-UI harness runs its
// own headless loop over the same SystemManager, so anything living in
// app_frame is invisible to every .e2e script — and an intake path no test can
// reach is one that breaks quietly. Everything here runs in both loops.
//
// Registered before the UI systems: MainPaneSystem reserves the composer strip
// from the attachment list, so the list has to be settled before it looks.
// ---------------------------------------------------------------------------

#include <cstdlib>
#include <string>

#include "../keys.h"
#include "../native_extras.h"
#include "components.h"
#include "ui_imports.h"

namespace ecs {

struct AttachmentIntakeSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>&, float) override {
        auto* app = find_singleton<AppComponent>();
        if (!app) return;

        // Diagnostic, fires once: HANABI_DROP_TEST=<path> pushes a path into
        // the same queue AppKit's drop delivers into, so everything below the
        // drag is reachable from a script. A drag is not in the widget tree
        // and the injector cannot produce one (afterhours_gaps.md #58 for why
        // sokol's own drop support is out of reach).
        if (!dropTestFired_) {
            dropTestFired_ = true;
            if (const char* d = std::getenv("HANABI_DROP_TEST"); d && *d)
                native_simulate_file_drop(d);
        }

        char path[1024];

        // A paste is a pull: the chord is the question and the pasteboard
        // answers it on the spot, so there is nothing to latch. Asking every
        // frame would allocate for nothing. A clipboard holding TEXT is not
        // ours — the field's own paste handles that and this answers false.
        if (hanabi::keys::cmd_down() && hanabi::keys::pressed(hanabi::keys::kV))
            if (native_take_clipboard_image(path, sizeof(path))) add(*app, path);

        // A drop is a push: whatever AppKit queued drains in one pass, so a
        // multi-image drop arrives as a multi-image drop.
        while (native_take_dropped_image(path, sizeof(path))) add(*app, path);
    }

  private:
    static void add(AppComponent& app, const char* path) {
        if (app.composerAttachments.size() >= AppComponent::kMaxAttachments)
            return;
        std::string p(path);
        const size_t slash = p.find_last_of('/');
        app.composerAttachments.push_back(
            {p, slash == std::string::npos ? p : p.substr(slash + 1)});
    }

    bool dropTestFired_ = false;
};

}  // namespace ecs
