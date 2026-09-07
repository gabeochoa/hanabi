#pragma once

// ---------------------------------------------------------------------------
// The one New Thread path.
//
// WHAT WAS THERE. ecs/composer_system.h drew a second composer -- a modal sheet
// over a dimmed backdrop, with its own text field, its own draft string on
// AppComponent, its own attachment summary (one "Remove" that emptied the whole
// list) and its own Start button that built its own outgoing message. The
// in-pane composer had the slash menu, the history walk, the per-file chips,
// the model and effort pickers, the send-key rule, the refusal caption and the
// steer face; the sheet had none of them. Six entry points reached one of the
// two composers depending on which one they happened to know about.
//
// WHAT THIS IS. New Thread is a SURFACE, not a second widget tree: the pane
// deselects its thread and shows its own landing page, which already carries
// the production composer in kickoff mode. Every entry point sets
// AppComponent::requestNewThread and this system is the only thing that reads
// it, so there is exactly one place that decides what "new thread" means and
// exactly one composer that can be typed into.
//
// The surface is DERIVED, never stored: it is showing when the focused pane has
// no thread selected in Chat view. A stored flag is a second opinion about
// what is on screen, and the sheet's flag had already disagreed with the screen
// once (a second Cmd+N closed the surface where every other client opens
// another).
//
// Pure part first, so a unit test can drive the routing with no window.
// ---------------------------------------------------------------------------

#include <string>

#include "components.h"
#include "tab_model.h"
#include "ui_imports.h"

namespace ecs {

namespace model {

// Is the new-thread surface what this pane is showing right now?
[[nodiscard]] inline bool showing_new_thread(SmartView view,
                                             const std::string& selectedId,
                                             bool hasOpenSession) {
    return view == SmartView::Chat && selectedId.empty() && !hasOpenSession;
}

[[nodiscard]] inline bool showing_new_thread(const AppComponent& app) {
    const Pane& pane = app.panes[static_cast<std::size_t>(
        std::clamp(app.focusedPane, 0, 1))];
    return showing_new_thread(app.view, pane.selectedId,
                              pane.openSession.has_value());
}

// What opening the surface does to a pane. Separated from the frame so the
// rule -- remember where we came from, deselect, and never lose the return
// path by opening twice -- is assertable headlessly.
inline void open_new_thread(Pane& pane, const std::string& fromId) {
    // Opening the surface twice must not overwrite the thread behind it with
    // the empty id the first open left, or Escape has nowhere to go back to.
    if (!fromId.empty()) pane.newThreadReturnId = fromId;
    ecs::model::reset_pane_to(pane, "");
}

// Where an empty-box Escape goes. Empty means nothing is behind the surface.
[[nodiscard]] inline std::string new_thread_return_target(const Pane& pane) {
    return pane.newThreadReturnId;
}

}  // namespace model

// Services the one request, and nothing else. Registered where ComposerSystem
// used to be, ahead of the UI systems, so the surface and the focus request are
// settled before the composer is built this frame.
struct NewThreadSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>&, float) override {
        auto* app = find_singleton<AppComponent>();
        if (!app) return;

        if (app->requestNewThread) {
            app->requestNewThread = false;
            Pane& pane = app->pane();
            model::open_new_thread(pane, pane.selectedId);
            app->view = SmartView::Chat;
            app->listCursorId.clear();
            // Asking again re-focuses: the counter is why a second Cmd+N puts
            // the caret back rather than toggling the surface shut.
            app->request_composer_focus();
            app->composerEscape.disarm();
        }

        if (app->requestCloseNewThread) {
            app->requestCloseNewThread = false;
            Pane& pane = app->pane();
            const std::string back = model::new_thread_return_target(pane);
            pane.newThreadReturnId.clear();
            if (!back.empty()) {
                model::reset_pane_to(pane, back);
                pane.scrollBottomPending.clear();
            }
            app->composerEscape.disarm();
        }

        // A failed create put the text and the files back in a composer; this
        // puts the READER back in front of it. Restoring bytes into a surface
        // nobody is looking at is how the old sheet lost a draft in silence.
        if (app->composerRestore.pending) {
            app->composerRestore.pending = false;
            const api::OutgoingTarget& target = app->composerRestore.target;
            const int paneIndex = std::clamp(target.pane_index, 0, 1);
            app->focusedPane = paneIndex;
            if (target.session_id.empty()) {
                Pane& pane = app->panes[static_cast<std::size_t>(paneIndex)];
                model::open_new_thread(pane, pane.selectedId);
                app->view = SmartView::Chat;
            }
            app->request_composer_focus();
            app->composerEscape.disarm();
        }
    }
};

}  // namespace ecs
