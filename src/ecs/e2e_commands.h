#pragma once

// ---------------------------------------------------------------------------
// Scripted-UI commands that only hanabi can implement.
//
// afterhours' e2e vocabulary is deliberately generic: click a point, type a
// key, assert on visible text. Four things this app's scripts need are not
// expressible in it, and all four are the reason a test has read as a
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
//   expect_panes <left> <right> <focus>
//                         Assert both pane contents and the focus owner without
//                         relying on which transcript lines are in the
//                         viewport.
//
//   reset_clipboard_probe  Clear the test-only write generation.
//   expect_clipboard <text> Assert that a fresh tab-menu copy reached the
//                           platform clipboard rather than only changing UI.
//
// Registered from run_e2e BEFORE register_unknown_handler, which is the
// ordering the unknown handler's own error message asks for.
// ---------------------------------------------------------------------------

#include <format>
#include <memory>
#include <string>

#include "../../vendor/afterhours/src/plugins/clipboard.h"
#include "../api/disk_cache.h"
#include "../test_hooks.h"
#include "../ui/link_detect.h"
#include "../util/clipboard.h"
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

inline std::string joined_args(
    const afterhours::testing::PendingE2ECommand& cmd, size_t first) {
    std::string out;
    for (size_t i = first; i < cmd.args.size(); ++i) {
        if (!out.empty()) out.push_back(' ');
        out += cmd.arg(i);
    }
    if (out.size() >= 2 &&
        ((out.front() == '"' && out.back() == '"') ||
         (out.front() == '\'' && out.back() == '\'')))
        return out.substr(1, out.size() - 2);
    return out;
}

struct HandleExpectPanesCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_panes")) return;
        if (!cmd.has_args(3)) {
            cmd.fail("expect_panes requires left_id right_id focused_index");
            return;
        }
        const ecs::AppComponent* app = app_component();
        const int focused = cmd.arg_as<int>(2);
        const auto holds = [](const ecs::Pane& pane, const std::string& id) {
            return pane.selectedId == id && pane.openSession &&
                   pane.openSession->summary.id == id;
        };
        if (app != nullptr && app->splitOpen &&
            holds(app->panes[0], cmd.arg(0)) &&
            holds(app->panes[1], cmd.arg(1)) && app->focusedPane == focused) {
            cmd.consume();
            return;
        }
        if (cmd.frames_alive < kGiveUpFrame) {
            cmd.retry();
            return;
        }
        const std::string left = app != nullptr && app->panes[0].openSession
                                     ? app->panes[0].openSession->summary.id
                                     : "-";
        const std::string right = app != nullptr && app->panes[1].openSession
                                      ? app->panes[1].openSession->summary.id
                                      : "-";
        cmd.fail(
            std::format("pane mismatch: left={} right={} focused={} split={}",
                        left, right, app != nullptr ? app->focusedPane : -1,
                        app != nullptr && app->splitOpen));
    }
};

inline std::uint64_t fingerprint(std::string_view value) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char c : value) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

struct HandleResetClipboardCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("reset_clipboard_probe")) return;
        hanabi::clipboard::reset_test_probe();
        cmd.consume();
    }
};

struct HandleExpectClipboardCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_clipboard")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("expect_clipboard requires text");
            return;
        }
        const std::string expected = joined_args(cmd, 0);
        const auto& probe = hanabi::clipboard::test_probe();
        std::string actual;
        bool fresh = probe.generation > 0;
        if (fresh) {
            actual = probe.text;
        } else {
            actual = std::string(hanabi::test_hooks::recorded_clipboard_text());
            if (actual.empty()) actual = afterhours::clipboard::get_text();
        }
        if (actual == expected) {
            cmd.consume();
            return;
        }
        if (cmd.frames_alive < kGiveUpFrame) {
            cmd.retry();
            return;
        }
        cmd.fail(std::format(
            "clipboard mismatch: fresh={} expected_len={} expected_hash={:016x} "
            "actual_len={} actual_hash={:016x}",
            fresh, expected.size(), fingerprint(expected), actual.size(),
            fingerprint(actual)));
    }
};

struct HandleExpectOutboxCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_outbox")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("expect_outbox requires session id and prompt");
            return;
        }
        const std::string promptText = joined_args(cmd, 1);
        const auto prompts = api::disk_cache::outbox_list(cmd.arg(0));
        for (const auto& prompt : prompts) {
            if (prompt == promptText) {
                cmd.consume();
                return;
            }
        }
        if (cmd.frames_alive < kGiveUpFrame) {
            cmd.retry();
            return;
        }
        cmd.fail(std::format("outbox missing prompt for '{}'", cmd.arg(0)));
    }
};

struct HandleSeedCacheCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("seed_cache")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("seed_cache requires session id and text");
            return;
        }
        api::Session session;
        session.summary.id = cmd.arg(0);
        api::Message message;
        message.id = "seed-message";
        message.role = api::Role::User;
        message.text = joined_args(cmd, 1);
        session.messages.push_back(std::move(message));
        api::disk_cache::save_transcript(session);
        api::disk_cache::save_draft(cmd.arg(0), "draft survives wipe");
        api::disk_cache::outbox_add(cmd.arg(0), "outbox survives wipe");
        cmd.consume();
    }
};

struct HandleExpectCacheWipedCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_cache_wiped")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("expect_cache_wiped requires session id");
            return;
        }
        const std::string& id = cmd.arg(0);
        const auto outbox = api::disk_cache::outbox_list(id);
        const bool ok = api::disk_cache::total_bytes() == 0 &&
                        !api::disk_cache::load_transcript(id).has_value() &&
                        api::disk_cache::load_draft(id) == "draft survives wipe" &&
                        outbox.size() == 1 && outbox[0] == "outbox survives wipe";
        if (ok) {
            cmd.consume();
            return;
        }
        if (cmd.frames_alive < kGiveUpFrame) {
            cmd.retry();
            return;
        }
        cmd.fail("cache wipe removed protected data or left transcript bytes");
    }
};

inline void register_hanabi_commands(afterhours::SystemManager& sm) {
    sm.register_update_system(std::make_unique<HandleRequireThreadCommand>());
    sm.register_update_system(std::make_unique<HandleClickLinkCommand>());
    sm.register_update_system(std::make_unique<HandleExpectPanesCommand>());
    sm.register_update_system(std::make_unique<HandleResetClipboardCommand>());
    sm.register_update_system(std::make_unique<HandleExpectClipboardCommand>());
    sm.register_update_system(std::make_unique<HandleExpectOutboxCommand>());
    sm.register_update_system(std::make_unique<HandleSeedCacheCommand>());
    sm.register_update_system(std::make_unique<HandleExpectCacheWipedCommand>());
}

}  // namespace hanabi::e2e
