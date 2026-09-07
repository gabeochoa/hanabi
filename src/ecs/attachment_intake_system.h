#pragma once

// ---------------------------------------------------------------------------
// Native intake: pasted images and dropped files become composer attachments.
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

#include "../api/attachments.h"
#include "../api/disk_cache.h"
#include "../keys.h"
#include "../native_extras.h"
#include "components.h"
#include "keyboard_focus.h"
#include "pane_state.h"
#include "ui_imports.h"

namespace ecs {

struct AttachmentIntakeSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>&, float) override {
        auto* app = find_singleton<AppComponent>();
        if (!app) return;

        // Diagnostic, fires once: HANABI_DROP_TEST=<path> pushes a path into
        // the same queue AppKit's drop delivers into, so everything below the
        // drag is reachable from a script. A drag is not in the widget tree
        // and the injector cannot produce one (afterhours_gaps.md #60 for why
        // sokol's own drop support is out of reach).
        if (!dropTestFired_) {
            ++dropTestFrames_;
            if (dropTestFrames_ >= 6) {
                dropTestFired_ = true;
                if (const char* d = std::getenv("HANABI_DROP_TEST"); d && *d) {
                    const std::string paths(d);
                    std::size_t start = 0;
                    while (start <= paths.size()) {
                        const std::size_t end = paths.find(';', start);
                        const std::string path = paths.substr(
                            start, end == std::string::npos ? std::string::npos
                                                            : end - start);
                        if (!path.empty()) add(*app, path.c_str());
                        if (end == std::string::npos) break;
                        start = end + 1;
                    }
                }
            }
        }

        char path[1024];

        // A paste is a pull: the chord is the question and the pasteboard
        // answers it on the spot, so there is nothing to latch. Asking every
        // frame would allocate for nothing.
        //
        // GATED ON THE COMPOSER HAVING THE CARET. This used to test the chord
        // globally and rely on native_take_clipboard_image answering false for
        // text -- so a Cmd+V aimed at the search field, the palette or a rename
        // box, with an image on the clipboard, staged that image onto a
        // conversation the reader was not typing into. The reference client
        // routes its paste through the text view itself for exactly this
        // reason; this is the same rule at the one seam hanabi has.
        // Ctrl is accepted as an alias of Cmd, the same bargain keys.h's own
        // ctrl_down() records: the scripted harness cannot hold Super, so a
        // chord that reads cmd_down() alone is unreachable from every test we
        // can write (afterhours_gaps.md #49, #256). This path had exactly that
        // shape -- the paste route was never once exercised by a test, and the
        // focus gate below would have passed vacuously without this.
        if (composer_field_focused() &&
            (hanabi::keys::cmd_down() || hanabi::keys::ctrl_down()) &&
            hanabi::keys::pressed(hanabi::keys::kV))
            if (native_take_clipboard_image(path, sizeof(path))) add(*app, path);

        // A drop is a push: whatever AppKit queued drains in one pass, so a
        // multi-image drop arrives as a multi-image drop.
        while (native_take_dropped_image(path, sizeof(path))) add(*app, path);
    }

  private:
    static void add(AppComponent& app, const char* path) {
        const api::OutgoingTarget target = app.current_composer_target();
        auto& state = model::pane_states().touch(
            model::pane_key(target.pane_index, target.draft_key));
        const std::string persistedKey = model::persisted_reply_key(
            target.pane_index, target.draft_key);
        if (!state.replyDraftLoaded) {
            const api::disk_cache::Draft saved =
                api::disk_cache::load_draft_state(persistedKey);
            state.replyDraft = saved.text;
            state.attachments = saved.attachments;
            state.persistedReplyDraft = saved.text;
            state.persistedAttachments = saved.attachments;
            state.replyDraftLoaded = true;
        }
        // ONE home for a complaint. Each of these used to set the note AND
        // raise a toast, so a refused file said the same sentence twice in two
        // places and neither could be dismissed. The note feeds the composer's
        // single notice channel (ecs/composer_notice.h), which has a dismiss.
        if (state.attachments.size() >= api::attachments::kMaxCount) {
            state.attachmentNotice = "A message can carry at most five files.";
            return;
        }
        auto staged = api::attachments::stage(path);
        if (!staged.ok) {
            state.attachmentNotice = staged.error;
            return;
        }
        auto retained = api::disk_cache::retain_attachment(staged.value);
        if (!retained.ok) {
            state.attachmentNotice = retained.error;
            return;
        }
        state.attachments.push_back(std::move(retained.value));
        state.attachmentNotice.clear();
        api::disk_cache::save_draft_state(
            persistedKey,
            api::disk_cache::Draft{state.replyDraft, state.attachments});
        state.persistedAttachments = state.attachments;
    }

    bool dropTestFired_ = false;
    int dropTestFrames_ = 0;
};

}  // namespace ecs
