#pragma once

// ---------------------------------------------------------------------------
// Scripted-UI commands that only hanabi can implement.
//
// afterhours' e2e vocabulary is deliberately generic: click a point, type a
// key, assert on visible text. Two things this app's scripts need are not
// expressible in it, and BOTH of them are the reason a test has read as a
// flake here rather than as a broken app:
//
//   require_thread <id>   A script that asserts on a transcript has a
//                         PRECONDITION -- that thread is open and its
//                         messages are on screen. Until now the only way to
//                         say it was a `# settings:` line the script's own
//                         reader could not see the effect of, and when the
//                         precondition did not hold the script did not say
//                         so: it ran on, and the first content assertion
//                         failed with "text not found" and a dump of the
//                         sidebar. Three investigations in one session read
//                         that dump as a broken feature. This states the
//                         precondition IN the script, waits for it, and
//                         fails naming what was open instead
//                         (afterhours_gaps.md #117, #232).
//
//   click_link <id>       A tracker id is a byte range inside a wrapped
//                         label, not an element, so there is no name to
//                         click (gap #51) and the script pinned the pixel.
//                         The pixel rots every time the transcript's layout
//                         moves -- twice so far, 321px and 26px, each time
//                         read as "the link feature broke". The renderer
//                         derives the rect anyway; this aims at its centre.
//
// Registered from run_e2e BEFORE register_unknown_handler, which is the
// ordering the unknown handler's own error message asks for.
// ---------------------------------------------------------------------------

#include <format>
#include <memory>
#include <string>

#include "../ui/link_detect.h"
#include "components.h"

namespace hanabi::e2e {

// A handler that retries has to give up before the runner's generic 30-frame
// timeout does, or the failure reads "Command 'x' timed out" and says nothing
// about what was wrong. 24 leaves the diagnosis to the command.
inline constexpr int kGiveUpFrame = 24;

inline ecs::AppComponent* app_component() {
    auto q = afterhours::EntityQuery({.force_merge = true})
                 .whereHasComponent<ecs::AppComponent>()
                 .gen();
    if (q.empty()) return nullptr;
    return &q[0].get().get<ecs::AppComponent>();
}

struct HandleRequireThreadCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("require_thread")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("require_thread requires a thread id");
            return;
        }
        const std::string& want = cmd.arg(0);
        const ecs::AppComponent* app = app_component();
        // Any pane will do. The precondition this states is "the thread is
        // on screen with content", and with a split open it is on screen if
        // either pane holds it -- the assertions below read the frame, not
        // the focus.
        const auto holds = [&want](const ecs::Pane& p) {
            return p.openSession && p.openSession->summary.id == want &&
                   !p.openSession->messages.empty();
        };
        bool ok = false;
        if (app != nullptr)
            for (const auto& p : app->panes)
                if (holds(p)) ok = true;
        if (ok) {
            cmd.consume();
            return;
        }
        if (cmd.frames_alive < kGiveUpFrame) {
            cmd.retry();
            return;
        }
        std::string open = "nothing";
        size_t msgs = 0;
        if (app != nullptr && app->pane().openSession) {
            open = app->pane().openSession->summary.id;
            msgs = app->pane().openSession->messages.size();
        }
        cmd.fail(std::format(
            "precondition not met: thread '{}' is not open with content "
            "(open={}, messages={}). The assertions below this line would "
            "have been checked against a screen that does not have the "
            "thread on it.",
            want, open, msgs));
    }
};

struct HandleClickLinkCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("click_link")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("click_link requires a link id");
            return;
        }
        const std::string& want = cmd.arg(0);
        const auto& painted = links::painted_rects();
        const auto it = painted.find(want);
        if (it != painted.end()) {
            const auto& r = it->second;
            afterhours::testing::test_input::simulate_click(
                r.x + r.width * 0.5f, r.y + r.height * 0.5f);
            cmd.consume();
            return;
        }
        if (cmd.frames_alive < kGiveUpFrame) {
            cmd.retry();
            return;
        }
        std::string seen;
        for (const auto& [id, r] : painted) {
            if (!seen.empty()) seen += ", ";
            seen += id;
        }
        cmd.fail(std::format(
            "no link '{}' painted on screen. Links drawn this run: {}", want,
            seen.empty() ? "(none)" : seen));
    }
};

inline void register_hanabi_commands(afterhours::SystemManager& sm) {
    sm.register_update_system(std::make_unique<HandleRequireThreadCommand>());
    sm.register_update_system(std::make_unique<HandleClickLinkCommand>());
}

}  // namespace hanabi::e2e
