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
//   expect_not_focused <name>
//                         The inverse of afterhours' expect_focused, which has
//                         no negation. A widget the app deliberately takes OUT
//                         of the focus ring -- an ask option row behind a modal
//                         sheet -- can only be asserted about by naming what
//                         focus_ui failed to move. Without it the scripts said
//                         nothing at all about the removal, and deleting the
//                         disabled/tab-stop treatment left every arm green.
//
// Registered from run_e2e BEFORE register_unknown_handler, which is the
// ordering the unknown handler's own error message asks for.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <afterhours/src/plugins/clipboard.h>
#include "../api/attachments.h"
#include "../api/disk_cache.h"
#include "../api/mock_client.h"
#include "../build_stamp.h"
#include "../native_capture_probe.h"
#include "capture_marker_system.h"
#include "../resize_drive.h"
#include "../test_hooks.h"
#include <afterhours/src/plugins/e2e_testing/platform_test_input.h>
#include "../ui/font_system.h"
#include "../ui/link_detect.h"
#include "../ui/text_select.h"
#include "../ui_context.h"
#include "../util/clipboard.h"
#include "../util/gfx_resize.h"
#include "../util/latency.h"
#include "../ui/control_state.h"
#include "../ui/reply_quote.h"
#include "../ui/tooltip.h"
#include "tooltip_system.h"
#include "../a11y_bridge.h"
#include "components.h"
#include "click_observer_system.h"
#include "pane_state.h"

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
        // expect_clipboard_contains <text>: the same read, matched as a
        // substring, for a copy whose full text the script cannot spell
        // (the kept-message notice copies a line break the runner has no
        // escape for).
        const bool contains = cmd.is("expect_clipboard_contains");
        if (cmd.is_consumed() || (!cmd.is("expect_clipboard") && !contains)) return;
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
        if (contains ? actual.find(expected) != std::string::npos
                     : actual == expected) {
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

struct HandleSeedReplyDraftCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("seed_reply_draft")) return;
        if (!cmd.has_args(3)) {
            cmd.fail("seed_reply_draft requires session id, pane, and text");
            return;
        }
        const int pane = std::stoi(cmd.arg(1));
        const std::string key = ecs::model::persisted_reply_key(pane, cmd.arg(0));
        api::disk_cache::save_draft(key, joined_args(cmd, 2));
        ecs::model::pane_states().forget(ecs::model::pane_key(pane, cmd.arg(0)));
        cmd.consume();
    }
};

struct HandleExpectReplyDraftCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_reply_draft")) return;
        if (!cmd.has_args(3)) {
            cmd.fail("expect_reply_draft requires session id, pane, and text");
            return;
        }
        const int pane = std::stoi(cmd.arg(1));
        const std::string key =
            ecs::model::persisted_reply_key(pane, cmd.arg(0));
        const std::string actual = api::disk_cache::load_draft(key);
        const std::string expected =
            cmd.arg(2) == "<empty>" ? std::string() : joined_args(cmd, 2);
        if (actual == expected) {
            cmd.consume();
            return;
        }
        if (cmd.frames_alive < kGiveUpFrame) {
            cmd.retry();
            return;
        }
        cmd.fail(std::format("persisted reply draft '{}' did not match '{}'",
                             actual, expected));
    }
};

struct HandleExpectNotFocusedCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_not_focused")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("expect_not_focused requires component name");
            return;
        }
        auto* ctx = afterhours::EntityHelper::get_singleton_cmp<
            ui_imm::UIContextType>();
        if (ctx == nullptr) {
            cmd.fail("UIContext not found");
            return;
        }
        const std::string name = cmd.arg(0);
        auto q = afterhours::EntityQuery({.force_merge = true})
                     .whereHasComponent<afterhours::ui::UIComponent>()
                     .whereHasComponent<afterhours::ui::UIComponentDebug>()
                     .whereLambda([&](const afterhours::Entity& e) {
                         return e.get<afterhours::ui::UIComponentDebug>()
                                    .name() == name;
                     })
                     .gen();
        for (auto& ref : q) {
            const auto& cmp = ref.get().get<afterhours::ui::UIComponent>();
            if (ctx->has_focus(cmp.id)) {
                cmd.fail(std::format("'{}' holds focus but should not. "
                                     "Focus set by {}",
                                     name, ctx->focus_origin()));
                return;
            }
            for (int child_id : cmp.children) {
                auto child = afterhours::ui::UICollectionHolder::
                    getEntityForID(child_id);
                if (child.valid() &&
                    child.asE().has<afterhours::ui::InFocusCluster>() &&
                    ctx->has_focus(child_id)) {
                    cmd.fail(std::format("a focus-cluster child of '{}' holds "
                                         "focus but should not. Focus set "
                                         "by {}",
                                         name, ctx->focus_origin()));
                    return;
                }
            }
        }
        cmd.consume();
    }
};

struct HandleSubmitThenFocusCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("submit_then_focus")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("submit_then_focus requires pane and text");
            return;
        }
        ecs::AppComponent* app = app_component();
        if (app == nullptr || !app->splitOpen) {
            cmd.fail("submit_then_focus requires an open split");
            return;
        }
        const api::OutgoingTarget target = app->current_composer_target();
        auto& state = ecs::model::pane_states().touch(
            ecs::model::pane_key(target.pane_index, target.draft_key));
        const std::string text = joined_args(cmd, 1);
        state.replyDraft = text;
        ecs::AppComponent::ComposerSubmission submitted;
        submitted.message = ecs::model::snapshot_outgoing(
            state, text, target,
            app->client && app->client->supports_attachments());
        app->composerSubmit = std::move(submitted);
        app->focusedPane = std::clamp(cmd.arg_as<int>(0), 0, 1);
        cmd.consume();
    }
};

struct HandleSubmitThenRetargetCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("submit_then_retarget")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("submit_then_retarget requires session and text");
            return;
        }
        ecs::AppComponent* app = app_component();
        if (app == nullptr || !app->client) {
            cmd.fail("submit_then_retarget needs an app client");
            return;
        }
        const api::OutgoingTarget target = app->current_composer_target();
        auto& state = ecs::model::pane_states().touch(
            ecs::model::pane_key(target.pane_index, target.draft_key));
        const std::string text = joined_args(cmd, 1);
        state.replyDraft = text;
        ecs::AppComponent::ComposerSubmission submitted;
        submitted.message = ecs::model::snapshot_outgoing(
            state, text, target, app->client->supports_attachments());
        app->composerSubmit = std::move(submitted);
        auto replacement = app->client->get_session(cmd.arg(0));
        if (!replacement.ok) {
            cmd.fail("submit_then_retarget could not open the replacement session");
            return;
        }
        ecs::Pane& pane = app->panes[static_cast<std::size_t>(target.pane_index)];
        pane.selectedId = cmd.arg(0);
        pane.openSession = std::move(replacement.value);
        pane.note_transcript_reset();
        app->view = ecs::SmartView::Chat;
        cmd.consume();
    }
};

struct HandleKickoffThenFocusCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("kickoff_then_focus")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("kickoff_then_focus requires pane and text");
            return;
        }
        ecs::AppComponent* app = app_component();
        if (app == nullptr || !app->client || !app->splitOpen) {
            cmd.fail("kickoff_then_focus requires an open split");
            return;
        }
        const int owner = std::clamp(app->focusedPane, 0, 1);
        const api::OutgoingTarget target{owner, "", "__kickoff__"};
        auto& state = ecs::model::pane_states().touch(
            ecs::model::pane_key(target.pane_index, target.draft_key));
        const std::string text = joined_args(cmd, 1);
        state.replyDraft = text;
        app->request_kickoff(ecs::model::snapshot_outgoing(
            state, text, target, app->client->supports_attachments()));
        app->focusedPane = std::clamp(cmd.arg_as<int>(0), 0, 1);
        cmd.consume();
    }
};

struct HandleExpectBackendMessageCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() ||
            (!cmd.is("expect_backend_message") &&
             !cmd.is("expect_backend_no_message") &&
             !cmd.is("expect_backend_attachment")))
            return;
        if (!cmd.has_args(2)) {
            cmd.fail("backend assertion requires session and value");
            return;
        }
        ecs::AppComponent* app = app_component();
        if (app == nullptr || !app->client) {
            cmd.fail("backend message assertion needs an app client");
            return;
        }
        const std::string expected = joined_args(cmd, 1);
        const auto session = app->client->get_session(cmd.arg(0));
        bool found = false;
        if (session.ok) {
            if (cmd.is("expect_backend_attachment")) {
                for (const auto& message : session.value.messages)
                    for (const auto& attachment : message.attachments)
                        if (attachment.name == expected) found = true;
            } else {
                for (const auto& message : session.value.messages)
                    if (message.role == api::Role::User &&
                        message.text == expected) {
                        found = true;
                        break;
                    }
            }
        }
        const bool want = !cmd.is("expect_backend_no_message");
        if (found == want) {
            cmd.consume();
            return;
        }
        if (want && cmd.frames_alive < kGiveUpFrame) {
            cmd.retry();
            return;
        }
        cmd.fail(std::format("session '{}' {} message '{}'",
                             cmd.arg(0), found ? "unexpectedly contains"
                                              : "does not contain",
                             expected));
    }
};

// expect_backend_state <session> <running|ready|...>: the thread's state as
// the BACKEND reports it, so a script can prove a control reached the server
// (the composer's Stop) rather than only repainted.
struct HandleExpectBackendStateCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    static const char* name_of(api::ThreadState s) {
        switch (s) {
            case api::ThreadState::Unknown: return "unknown";
            case api::ThreadState::Attention: return "attention";
            case api::ThreadState::Ready: return "ready";
            case api::ThreadState::Running: return "running";
            case api::ThreadState::Parked: return "parked";
            case api::ThreadState::Archived: return "archived";
            case api::ThreadState::Working: return "working";
        }
        return "unknown";
    }
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_backend_state")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("expect_backend_state requires session and state");
            return;
        }
        ecs::AppComponent* app = app_component();
        if (app == nullptr || !app->client) {
            cmd.fail("backend state assertion needs an app client");
            return;
        }
        const auto session = app->client->get_session(cmd.arg(0));
        const std::string actual =
            session.ok ? name_of(session.value.summary.state) : "(no session)";
        if (actual == cmd.arg(1)) {
            cmd.consume();
            return;
        }
        if (cmd.frames_alive < kGiveUpFrame) {
            cmd.retry();
            return;
        }
        cmd.fail(std::format("session '{}' is {}, not {}", cmd.arg(0), actual,
                             cmd.arg(1)));
    }
};

// expect_no_ui <name>: no widget carrying that debug name was drawn this
// frame. assert_ui can only speak about a widget that exists; this is the
// other half, for a control that must be ABSENT (the other pane's attachment
// strip, say). Waits a few frames the way assert_ui does before failing, so
// a widget that is about to disappear is not read as present.
struct HandleExpectNoUiCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_no_ui")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("expect_no_ui requires <name>");
            return;
        }
        const std::string name = cmd.arg(0);
        auto opt = afterhours::testing::ui_commands::ui_query()
                       .whereHasComponent<afterhours::ui::UIComponent>()
                       .whereHasComponent<afterhours::ui::UIComponentDebug>()
                       .whereLambda([&](const afterhours::Entity& e) {
                           return e.get<afterhours::ui::UIComponentDebug>()
                                          .name_value == name &&
                                  e.get<afterhours::ui::UIComponent>()
                                      .was_rendered_to_screen;
                       })
                       .gen_first();
        if (!opt.has_value()) {
            cmd.consume();
            return;
        }
        if (cmd.frames_alive < kGiveUpFrame) {
            cmd.retry();
            return;
        }
        cmd.fail(std::format("expect_no_ui: '{}' is on screen", name));
    }
};

struct HandleExpectUploadCancelledCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_upload_cancelled")) return;
        const ecs::AppComponent* app = app_component();
        if (app != nullptr && app->transfer && app->transfer->cancel.load()) {
            cmd.consume();
            return;
        }
        if (cmd.frames_alive < kGiveUpFrame) {
            cmd.retry();
            return;
        }
        cmd.fail("upload cancellation was not requested");
    }
};

// `resize W H`, taken over from afterhours' builtin.
//
// The builtin destroys and recreates the headless render target inline, from
// inside a System -- which runs mid-pass, with that target's own pass open and
// its draw commands recorded. That is a teardown of live pass attachments and
// sokol aborts on it. This consumes the command first and splits it: the
// resolution moves now, the render target moves at the next frame boundary.
// src/util/gfx_resize.h has the full account.
//
// Consuming it is what keeps the builtin out: HandleResizeCommand returns
// early on cmd.is_consumed(). Registration therefore has to come BEFORE
// register_builtin_handlers -- SystemManager::run walks update_systems_ in
// registration order -- and `expect_resizes_applied` is the planted control
// that proves this handler, not the builtin, did the work.
struct HandleResizeDeferredCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    // SystemManager::tick merges newly created entities AFTER each system
    // runs (core/system.h, `EntityHelper::merge_entity_arrays()` at the foot
    // of the per-system block), so the FIRST system in the list never sees an
    // entity created since the previous system -- and the runner creates the
    // PendingE2ECommand between frames, in runner.tick(). Registering ahead of
    // the builtins is therefore not enough on its own: without this the
    // handler runs, iterates an entity list that does not hold the command
    // yet, and the builtin picks the command up one system later. Merging here
    // is what makes "registered first" mean "sees it first": tick() reads
    // entities.size() after once() returns.
    void once(float) override { afterhours::EntityHelper::merge_entity_arrays(); }
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("resize")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("resize requires width height arguments");
            return;
        }
        const int w = cmd.arg_as<int>(0);
        const int h = cmd.arg_as<int>(1);
        if (w <= 0 || h <= 0) {
            cmd.fail("resize: invalid dimensions");
            return;
        }
        hanabi::gfx::request_resize(w, h);
        cmd.consume();
    }
};

// `expect_resizes_applied N` -- the planted control for the deferral. It reads
// the count of resizes applied AT A FRAME BOUNDARY, so it fails both ways: if
// the deferral regresses and the builtin resizes inline the count stays 0, and
// if a script's resize silently stopped happening the count stays 0 too.
// ---------------------------------------------------------------------------
// The NATIVE input segment of a script (resize_drive.h's bridge).
//
//   native_mode on|off      -- leave (or re-enter) the harness's injected
//                              input: while off, afterhours reads the
//                              backend's own mouse and keys, i.e. what real
//                              NSEvents produced through sokol and the
//                              letterbox. `click`/`type`/`key` do nothing in
//                              this mode; the native_* commands below do.
//   native_move x y         -- post a mouse-moved at content point (x, y)
//   native_click x y        -- mouse-moved + left down now, left up two frames
//                              later (so press-activated and release-activated
//                              widgets both see their edge on a frame)
//   native_right_click x y
//   native_drag_resize dw dh [steps]
//                           -- drag the bottom-right corner through AppKit's
//                              tracking loop; the window ends (dw, dh) larger
//   native_key <keycode> [chars] [shift|ctrl|alt|cmd ...]
//   expect_content_size w h -- the window's CONTENT size as AppKit reports it
//
// Coordinates are content-space logical points, origin top-left of the
// contentView -- the same space assert_ui reports. Nothing here corrects
// anything: a script picks a point from a painted rect and the click has to
// land where the paint says.
// Whether a script has handed input over to the native path. Read by the
// native commands (which refuse without a window) and by the injected ones
// (which refuse while it is on, rather than quietly doing nothing).
inline bool& native_mode_on() {
    static bool on = false;
    return on;
}

// expect_open <id>: the focused pane's SELECTED session is <id>, its loaded
// session is <id>, and the view is Chat -- the single-pane counterpart of
// expect_panes, so a script can prove what a click OPENED rather than what
// is highlighted. `expect_open -` asserts nothing is open (Home).
struct HandleExpectOpenCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_open")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("expect_open requires <session id> or -");
            return;
        }
        const ecs::AppComponent* app = app_component();
        const std::string& want = cmd.arg(0);
        bool ok = false;
        std::string actual = "(no app)";
        if (app != nullptr) {
            const ecs::Pane& pane = app->panes[static_cast<std::size_t>(
                std::clamp(app->focusedPane, 0, 1))];
            const bool open = pane.openSession &&
                              pane.openSession->summary.id == pane.selectedId &&
                              app->view == ecs::SmartView::Chat;
            actual = open ? pane.selectedId : std::string("-");
            ok = want == "-" ? (!open || pane.selectedId.empty())
                             : (open && pane.selectedId == want);
        }
        if (ok) {
            cmd.consume();
            return;
        }
        if (cmd.frames_alive < kGiveUpFrame) {
            cmd.retry();
            return;
        }
        cmd.fail(std::format("expect_open: the focused pane has '{}' open, not '{}'",
                             actual, want));
    }
};

struct HandleNativeModeCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("native_mode")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("native_mode requires on|off");
            return;
        }
        const bool on = cmd.arg(0) == "on";
        // A test-only hook that posts input into a REAL window must not be
        // able to drive the app against anyone's real backend: it would send
        // messages to live threads. The gate is the app's own backend label,
        // checked here rather than left to the runner's environment.
        if (on) {
            const ecs::AppComponent* app = app_component();
            const std::string backend = app ? app->backend_label : std::string("none");
            if (backend != "mock") {
                cmd.fail(std::format(
                    "native_mode refuses a '{}' backend: these hooks post real "
                    "input into a real window, and the only destination that "
                    "cannot reach anyone's real threads is the offline mock "
                    "(HANABI_BACKEND=mock).",
                    backend));
                return;
            }
        }
        if (on && !hanabi_native_has_window()) {
            cmd.fail(
                "native_mode on needs a real window: this run is headless. "
                "A native script must be run with HANABI_E2E_WINDOWED=1 "
                "(scripts carry it in their `# env:` line).");
            return;
        }
        // Frontmost, because that is the state a person's input arrives in:
        // menu key equivalents (Cmd+W, Cmd+digit) are dispatched by
        // NSApplication to the main menu and a background app's menu never
        // answers.
        if (on) {
            hanabi_native_activate();
            // Drop anything the injected path was still holding, so a key
            // "held" by a previous command cannot colour what the native
            // events produce.
            afterhours::testing::test_input::clear_queue();
            afterhours::testing::input_injector::detail::mouse = {};
        }
        native_mode_on() = on;
        // Leaving test mode also drops the injector's mouse override, so the
        // backend position -- the letterboxed one -- is what gets read.
        afterhours::testing::platform_input::set_test_mode(!on);
        afterhours::testing::input_injector::detail::mouse.active = false;
        cmd.consume();
    }
};

struct HandleNativeMouseCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed()) return;
        const bool click = cmd.is("native_click");
        const bool rclick = cmd.is("native_right_click");
        const bool move = cmd.is("native_move");
        if (!click && !rclick && !move) return;
        if (!cmd.has_args(2)) {
            cmd.fail("native mouse commands require x y");
            return;
        }
        if (!hanabi_native_has_window() || !native_mode_on()) {
            cmd.fail("a native input command needs `native_mode on` in a "
                     "windowed run (HANABI_E2E_WINDOWED=1)");
            return;
        }
        const float x = std::strtof(cmd.arg(0).c_str(), nullptr);
        const float y = std::strtof(cmd.arg(1).c_str(), nullptr);
        if (cmd.frames_alive == 0) {
            hanabi_native_mouse_move(x, y);
            if (move) {
                cmd.consume();
                return;
            }
            hanabi_native_mouse_down(x, y, rclick ? 1 : 0);
            cmd.retry();
            return;
        }
        if (cmd.frames_alive < 2) {
            cmd.retry();
            return;
        }
        hanabi_native_mouse_up(x, y, rclick ? 1 : 0);
        cmd.consume();
    }
};

// native_click_ui <name> / native_click_text "<text>": the SAME native click,
// aimed at the centre of a widget's PAINTED rect (the rect afterhours laid it
// out at last frame -- assert_ui's numbers). The lookup only picks the point;
// the event still enters through NSApp and is mapped by the backend, so a
// wrong mapping still lands the click somewhere else and the assertion that
// follows (what opened, what was sent) says so.
struct HandleNativeClickTargetCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    std::optional<std::pair<float, float>> target_;
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed()) return;
        const bool byName = cmd.is("native_click_ui");
        const bool byText = cmd.is("native_click_text");
        if (!byName && !byText) return;
        if (!cmd.has_args(1)) {
            cmd.fail("native_click_ui/native_click_text require a target");
            return;
        }
        if (!hanabi_native_has_window() || !native_mode_on()) {
            cmd.fail("a native input command needs `native_mode on` in a "
                     "windowed run (HANABI_E2E_WINDOWED=1)");
            return;
        }
        if (cmd.frames_alive == 0) {
            // The runner splits an unlisted command's line on whitespace, so a
            // quoted title arrives as several args: join them and drop the
            // quotes (click_text gets this from the runner itself).
            std::string target = byName ? cmd.arg(0) : joined_args(cmd, 0);
            if (target.size() >= 2 && target.front() == '"' && target.back() == '"')
                target = target.substr(1, target.size() - 2);
            const auto pos =
                byName ? afterhours::testing::ui_commands::find_component_center<InputAction>(target)
                       : afterhours::testing::ui_commands::find_component_with_text<InputAction>(target);
            if (!pos.has_value()) {
                cmd.fail(std::format("native click target not on screen: {}", target));
                return;
            }
            target_ = std::make_pair(static_cast<float>(pos->x), static_cast<float>(pos->y));
            hanabi_native_mouse_move(target_->first, target_->second);
            hanabi_native_mouse_down(target_->first, target_->second, 0);
            cmd.retry();
            return;
        }
        if (cmd.frames_alive < 2) {
            cmd.retry();
            return;
        }
        if (target_) hanabi_native_mouse_up(target_->first, target_->second, 0);
        target_.reset();
        cmd.consume();
    }
};

struct HandleNativeDragResizeCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("native_drag_resize")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("native_drag_resize requires dw dh [steps]");
            return;
        }
        if (!hanabi_native_has_window() || !native_mode_on()) {
            cmd.fail("a native input command needs `native_mode on` in a "
                     "windowed run (HANABI_E2E_WINDOWED=1)");
            return;
        }
        const int dw = std::atoi(cmd.arg(0).c_str());
        const int dh = std::atoi(cmd.arg(1).c_str());
        const int steps = cmd.has_args(3) ? std::max(1, std::atoi(cmd.arg(2).c_str())) : 12;
        hanabi_native_drag_resize(dw, dh, steps);
        cmd.consume();
    }
};

struct HandleNativeKeyCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("native_key")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("native_key requires <keycode> [chars] [shift|ctrl|alt|cmd...]");
            return;
        }
        if (!hanabi_native_has_window() || !native_mode_on()) {
            cmd.fail("a native input command needs `native_mode on` in a "
                     "windowed run (HANABI_E2E_WINDOWED=1)");
            return;
        }
        const unsigned short code =
            static_cast<unsigned short>(std::atoi(cmd.arg(0).c_str()));
        std::string chars;
        unsigned mods = 0;
        for (std::size_t i = 1; cmd.has_args(i + 1); ++i) {
            const std::string& a = cmd.arg(i);
            if (a == "shift") mods |= 1u;
            else if (a == "ctrl") mods |= 2u;
            else if (a == "alt") mods |= 4u;
            else if (a == "cmd") mods |= 8u;
            else chars = a == "-" ? std::string() : a;
        }
        // Three frames, because a modifier is STATE: the app polls "is Cmd
        // down" during a frame (keys.h cmd_down), so pressing and releasing
        // in one batch -- every event consumed before the next frame runs --
        // leaves every poll reading false. Hold, let a frame see it, press
        // the key, then release on a later frame. (The menu path answers
        // synchronously inside performKeyEquivalent, which is why chords
        // worked even when the poll could not see them.)
        if (cmd.frames_alive == 0) {
            if (mods != 0) hanabi_native_mods_down(mods);
            cmd.retry();
            return;
        }
        if (cmd.frames_alive == 1) {
            hanabi_native_key(code, chars.c_str(), mods);
            cmd.retry();
            return;
        }
        if (cmd.frames_alive < 3) {
            cmd.retry();
            return;
        }
        if (mods != 0) hanabi_native_mods_up(mods);
        cmd.consume();
    }
};

// Was this UI entity BUILT by the most recent UI build? The library clears
// `was_rendered_to_screen` every frame but never clears an element's rect,
// and an element whose imm call stopped survives with its old rect and label
// for `ui_retire_grace_frames` (90) frames before it is retired -- so "has a
// rect" is stale for a second and a half. The build ledger is the truth:
// `existing_ui_elements` records each element's `last_built_frame`, stamped
// with `ui_build_frame`, which advances once per frame in the UI post-layout
// pass. Whether an E2E handler runs before or after this frame's build, the
// element's most recent stamp is `ui_build_frame` (built this frame, handler
// after the build) or `ui_build_frame - 1` (handler before the build); a
// withdrawn element's stamp is older than both by the frame after. A row
// scrolled below the viewport was built (its imm call ran) and passes.
inline bool built_last_frame(afterhours::EntityID id) {
    using namespace afterhours::ui::imm;
    const size_t floor = ui_build_frame == 0 ? 0 : ui_build_frame - 1;
    for (const auto& [hash, rec] : existing_ui_elements)
        if (rec.id == id) return rec.last_built_frame >= floor;
    return false;
}

// double_click_word "<line substring>" <word> [nth=1]: a double-click ON A WORD
// of a transcript line, placed by the same layout the selection code uses
// (text_select::detail::layout_of over the active face at the line's own
// size), so a script names the word it means -- "ledger" -- instead of the
// pixel a given face put it at. The line is the one painted label whose text
// contains <line substring>; the <nth> occurrence of <word> in it is the
// target and the click lands on that word's centre. Two clicks three frames
// apart, exactly as `double_click` does.
struct HandleDoubleClickWordCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    int phase = 0;
    float sx = 0, sy = 0;
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("double_click_word")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("double_click_word requires \"<line substring>\" <word> [nth]");
            return;
        }
        if (phase == 0) {
            // The runner hands a custom command its tokens as typed and split
            // on spaces, quotes included: the WORD is the last token, an
            // all-digit final token before it is <nth>, and everything
            // earlier re-joined with spaces and stripped of its quote pair is
            // the line text.
            // The line text is the QUOTED run: from the first token that opens
            // a quote to the token that closes it (a word can be all digits,
            // so the quotes, not a digit test, decide where the text ends).
            // Then the word; then an optional all-digit <nth>.
            std::string needle;
            size_t after = 0;
            const std::string& t0 = cmd.arg(0);
            const char q = (!t0.empty() && (t0.front() == '"' || t0.front() == '\'')) ? t0.front() : 0;
            if (q != 0) {
                size_t i = 0;
                for (; i < cmd.args.size(); ++i) {
                    if (!needle.empty()) needle.push_back(' ');
                    needle += cmd.arg(i);
                    const std::string& t = cmd.arg(i);
                    const bool closes = t.size() >= (i == 0 ? 2u : 1u) && t.back() == q;
                    if (closes) break;
                }
                if (i >= cmd.args.size()) {
                    cmd.fail("double_click_word: the quoted line text never closes");
                    return;
                }
                needle = needle.substr(1, needle.size() - 2);
                after = i + 1;
            } else {
                needle = t0;
                after = 1;
            }
            if (after >= cmd.args.size()) {
                cmd.fail("double_click_word requires \"<line substring>\" <word> [nth]");
                return;
            }
            const std::string word = cmd.arg(after);
            int nth = 1;
            if (after + 1 < cmd.args.size()) {
                const std::string& t = cmd.arg(after + 1);
                if (t.empty() || !std::all_of(t.begin(), t.end(), ::isdigit)) {
                    cmd.fail(std::format("double_click_word: <nth> must be digits, got '{}'", t));
                    return;
                }
                nth = std::atoi(t.c_str());
            }
            if (needle.empty() || word.empty()) {
                cmd.fail("double_click_word requires \"<line substring>\" <word> [nth]");
                return;
            }
            const afterhours::Entity* line = nullptr;
            for (const auto& e :
                 afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
                if (!e || !e->has<afterhours::ui::UIComponent>() ||
                    !e->has<afterhours::ui::HasLabel>())
                    continue;
                if (!e->get<afterhours::ui::UIComponent>().was_rendered_to_screen) continue;
                if (e->get<afterhours::ui::HasLabel>().label.find(needle) == std::string::npos)
                    continue;
                line = e.get();
                break;
            }
            if (line == nullptr) {
                if (cmd.frames_alive < kGiveUpFrame) {
                    cmd.retry();
                    return;
                }
                cmd.fail(std::format("double_click_word: no painted line containing \"{}\"", needle));
                return;
            }
            const std::string& text = line->get<afterhours::ui::HasLabel>().label;
            size_t at = std::string::npos, from = 0;
            for (int k = 0; k < nth; ++k) {
                at = text.find(word, from);
                if (at == std::string::npos) break;
                from = at + 1;
            }
            if (at == std::string::npos) {
                cmd.fail(std::format("double_click_word: \"{}\" has no #{} \"{}\"", text, nth, word));
                return;
            }
            const auto& cmp = line->get<afterhours::ui::UIComponent>();
            const RectangleType r = cmp.rect();
            const float fontPx = cmp.font_size.value;
            const auto lay = hanabi::text_select::detail::layout_of(r, text, fontPx);
            if (!lay.ok) {
                cmd.fail("double_click_word: the line has no layout this frame");
                return;
            }
            // Which wrapped row holds the word, and where along it.
            size_t consumed = 0;
            int row = 0;
            size_t col = at;
            for (size_t li = 0; li < lay.lines.size(); ++li) {
                const size_t n = lay.lines[li].size();
                if (at < consumed + n) {
                    row = static_cast<int>(li);
                    col = at - consumed;
                    break;
                }
                consumed += n;
                // wrap_text_to_width drops the break's whitespace; skip it
                while (consumed < text.size() && std::isspace(static_cast<unsigned char>(text[consumed]))) ++consumed;
            }
            auto* fm = afterhours::EntityHelper::get_singleton_cmp<afterhours::ui::FontManager>();
            const afterhours::Font font = fm->get_active_font();
            const std::string& ln = lay.lines[static_cast<size_t>(row)];
            const float xa = lay.x0 + afterhours::measure_text(font, ln.substr(0, col).c_str(), fontPx, 1.0f).x;
            const float xb = lay.x0 + afterhours::measure_text(font, ln.substr(0, col + word.size()).c_str(), fontPx, 1.0f).x;
            sx = (xa + xb) * 0.5f;
            sy = lay.y0 + lay.lineH * (static_cast<float>(row) + 0.5f);
            std::printf("[double_click_word] \"%s\" in \"%s\" -> row %d col %zu at (%.1f,%.1f)\n",
                        word.c_str(), needle.c_str(), row, col, sx, sy);
            afterhours::testing::test_input::simulate_click(sx, sy);
            phase = 1;
            cmd.retry();
            return;
        }
        if (phase < 3) {
            ++phase;
            cmd.retry();
            return;
        }
        afterhours::testing::test_input::simulate_click(sx, sy);
        phase = 0;
        cmd.consume();
    }
};

// expect_ui_rel <a> <relation> <b> [tolerance=1]: a GEOMETRIC RELATION between
// two named widgets, so a script states the invariant it means ("the bubble
// hugs the pane's right edge", "the last row is below the viewport", "after
// the scroll it is inside it") instead of a pixel a given face produced.
// Both widgets must have been built by the most recent UI build (a row below
// the fold counts: its builder ran; a widget whose builder stopped does not,
// whatever rect it still carries). Relations:
//   inside        a's rect lies within b's (each edge, within tolerance)
//   outside_below a's top edge is at or below b's bottom edge
//   above         a's bottom edge is at or above b's top edge
//   right_edge    a.right == b.right   (within tolerance)
//   left_edge     a.left  == b.left
//   left_of_by:N  a.right + N == b.left  (a sits N px clear, left of b)
//   same_y        a.y == b.y            same_h  a.h == b.h
// A missing widget retries until the give-up frame, then fails by name.
struct HandleExpectUiRelCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    // Built by the most recent build (see built_last_frame): an off-screen
    // row qualifies, a retired-in-grace one does not. The rect is the SCREEN
    // rect -- modifiers and the enclosing scroll offset applied, the same
    // `get_screen_rect` assert_ui reads -- so a row inside a scrolled body is
    // where it is drawn, not where it was laid out.
    static std::optional<RectangleType> find(const std::string& name) {
        std::optional<RectangleType> out;
        for (const auto& e :
             afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
            if (!e || !e->has<afterhours::ui::UIComponent>() ||
                !e->has<afterhours::ui::UIComponentDebug>())
                continue;
            if (e->get<afterhours::ui::UIComponentDebug>().name() != name) continue;
            if (!built_last_frame(e->id)) continue;
            out = afterhours::testing::ui_commands::get_screen_rect(*e);
        }
        return out;
    }
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_ui_rel")) return;
        if (!cmd.has_args(3)) {
            cmd.fail("expect_ui_rel requires <a> <relation> <b> [tolerance]");
            return;
        }
        const float tol = cmd.args.size() >= 4 ? std::atof(cmd.arg(3).c_str()) : 1.0f;
        const auto A = find(cmd.arg(0));
        const auto B = find(cmd.arg(2));
        if (!A || !B) {
            if (cmd.frames_alive < kGiveUpFrame) {
                cmd.retry();
                return;
            }
            cmd.fail(std::format("expect_ui_rel: no widget named '{}'",
                                 !A ? cmd.arg(0) : cmd.arg(2)));
            return;
        }
        const auto a = *A;
        const auto b = *B;
        const std::string& rel = cmd.arg(1);
        const auto eq = [tol](float p, float q) { return std::fabs(p - q) <= tol; };
        bool ok = false;
        if (rel == "inside")
            ok = a.x >= b.x - tol && a.y >= b.y - tol &&
                 a.x + a.width <= b.x + b.width + tol &&
                 a.y + a.height <= b.y + b.height + tol;
        else if (rel == "outside_below")
            ok = a.y >= b.y + b.height - tol;
        else if (rel == "above")
            ok = a.y + a.height <= b.y + tol;
        else if (rel == "right_edge")
            ok = eq(a.x + a.width, b.x + b.width);
        else if (rel == "left_edge")
            ok = eq(a.x, b.x);
        else if (rel == "same_y")
            ok = eq(a.y, b.y);
        else if (rel == "same_h")
            ok = eq(a.height, b.height);
        else if (rel.rfind("left_of_by:", 0) == 0)
            ok = eq(a.x + a.width + std::atof(rel.c_str() + 11), b.x);
        else {
            cmd.fail(std::format("expect_ui_rel: unknown relation '{}'", rel));
            return;
        }
        if (ok) {
            cmd.consume();
            return;
        }
        if (cmd.frames_alive < kGiveUpFrame) {
            cmd.retry();
            return;
        }
        cmd.fail(std::format(
            "expect_ui_rel: '{}' [{:.0f},{:.0f} {:.0f}x{:.0f}] is not {} '{}' [{:.0f},{:.0f} {:.0f}x{:.0f}] (tol {})",
            cmd.arg(0), a.x, a.y, a.width, a.height, rel, cmd.arg(2), b.x, b.y, b.width,
            b.height, tol));
    }
};

// expect_ui_rows_join <prefix> <text...>: the labels of <prefix>_0, _1, ...
// (every row BUILT this frame, in index order -- a row scrolled below the
// viewport counts: it exists and is reachable, which is the opposite of
// clipped) hold the given text, allowing ONLY what a line wrapper is allowed
// to do: at a row boundary the text's whitespace may be dropped (a break at a
// space) or a token may continue on the next row (a break inside a long
// token). Whitespace INSIDE a row is compared as written -- a space lost or
// added inside a row fails; a space lost exactly AT a break point is not
// observable from rendered rows (see `mismatch`). Nothing clipped: every
// character present; nothing invented: no character extra; an ellipsised
// last row fails (the ellipsis is not in the text). `<text>` = `-` asserts
// that NO row under the prefix was built by the most recent build.
struct HandleExpectUiRowsJoinCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    // Match `rows` against `want`: rows concatenate exactly, except that at
    // each row boundary any run of whitespace in `want` may be absent from the
    // rows (the wrapper dropped it). Returns the first mismatch description or
    // "" on success.
    struct Row {
        float y, x;
        std::string label;
    };
    // Match `rows` against `want`. Rows concatenate exactly; at a row boundary
    // the wrapper is allowed to have dropped whitespace that is in `want`, or
    // to have continued a token onto the next row. What the rendered rows
    // CANNOT tell is whether a boundary with no whitespace in `want` was a
    // break inside a token or a space the text lost: two source strings that
    // differ only by a space at a break point render the same rows. rows_join
    // asserts rendered content and order; a space's presence is provable only
    // where both sides of it are inside ONE row, and a control for that first
    // asserts the pair is visible on one row (expect_text) before asserting
    // the lost-space text. Exact source semantics beyond that come from a
    // source receipt (the backend's own copy of the command), not the rows.
    // Returns "" on success.
    static std::string mismatch(const std::vector<Row>& rows, const std::string& want) {
        size_t w = 0;
        for (size_t ri = 0; ri < rows.size(); ++ri) {
            const std::string& row = rows[ri].label;
            for (size_t i = 0; i < row.size(); ++i, ++w) {
                if (w >= want.size())
                    return std::format("row {} has extra text starting \"{}\"", ri, row.substr(i, 24));
                if (row[i] != want[w])
                    return std::format("row {} col {}: got '{}' expected '{}'", ri, i, row[i], want[w]);
            }
            if (ri + 1 < rows.size())
                while (w < want.size() && std::isspace(static_cast<unsigned char>(want[w]))) ++w;
        }
        while (w < want.size() && std::isspace(static_cast<unsigned char>(want[w]))) ++w;
        if (w < want.size())
            return std::format("rows end before the text; missing \"{}\"", want.substr(w, 32));
        return "";
    }
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_ui_rows_join")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("expect_ui_rows_join requires <prefix> <text...>");
            return;
        }
        const std::string prefix = cmd.arg(0) + "_";
        // `-` = expect NO rows built under the prefix: the negative case, so a
        // script can prove rows that were built and then withdrawn do not
        // count while their old rects and labels linger in the grace window.
        const std::string want = joined_args(cmd, 1);
        const bool wantNone = want == "-";
        // Only rows the most recent build produced (built_last_frame): a
        // row scrolled below the viewport counts, a row whose builder stopped
        // does not -- however long its old rect and label linger.
        // Rows in VISUAL order (y, then x) -- the index suffix is a build
        // key, not a reading order, and the same prefix can be built by more
        // than one card in a frame. Only names of the exact shape
        // <prefix>_<digits> count (no `_line`, no `_body`).
        std::vector<Row> rows;
        int named = 0;  // candidates by name, before the build-ledger gate
        for (const auto& e :
             afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
            if (!e || !e->has<afterhours::ui::UIComponent>() ||
                !e->has<afterhours::ui::UIComponentDebug>() ||
                !e->has<afterhours::ui::HasLabel>())
                continue;
            const std::string& n = e->get<afterhours::ui::UIComponentDebug>().name();
            if (n.rfind(prefix, 0) != 0) continue;
            const std::string tail = n.substr(prefix.size());
            if (tail.empty() || !std::all_of(tail.begin(), tail.end(), ::isdigit)) continue;
            ++named;
            if (!built_last_frame(e->id)) continue;
            const auto r = afterhours::testing::ui_commands::get_screen_rect(*e);
            rows.push_back({r.y, r.x, e->get<afterhours::ui::HasLabel>().label});
        }
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            return a.y != b.y ? a.y < b.y : a.x < b.x;
        });
        std::string why;
        if (wantNone)
            why = rows.empty() ? std::string()
                               : std::format("{} rows still built, first \"{}\"", rows.size(),
                                             rows.front().label.substr(0, 24));
        else if (rows.empty())
            why = named == 0 ? std::string("no rows by name")
                             : std::format("{} rows by name, none stamped by the last build "
                                           "(ui_build_frame={})",
                                           named, afterhours::ui::imm::ui_build_frame);
        else
            why = mismatch(rows, want);
        if (why.empty()) {
            cmd.consume();
            return;
        }
        if (cmd.frames_alive < kGiveUpFrame) {
            cmd.retry();
            return;
        }
        cmd.fail(std::format("expect_ui_rows_join: {} rows under '{}': {}", rows.size(),
                             cmd.arg(0), why));
    }
};

// capture_receipt <debug_name> [expect_scale=<s>]: WHERE a painted widget is,
// for cropping an outside capture of THIS window -- every number read from
// afterhours' own letterbox and AppKit's own conversions, none guessed. One
// line on stdout:
//
//   [capture-receipt] name=<n> stamp=<build> pid=<p> window=<num> owned=1
//       backing=<scale> content_pt=<x>,<y>,<w>x<h> window_pt=<x>,<y>,<w>x<h>
//       screen_pt=<x>,<y>,<w>x<h> frame_pt=<x>,<y>,<w>x<h>
//       crop_px=<x>,<y>,<w>x<h> fill=<RRGGBB|-> letterbox=<dest x,y,wxh>
//       letterbox_scale=<s>
//
// Spaces, named exactly: `content_pt` is the UI's own coordinate space
// (afterhours logical points, top-left) -- the rect as the app laid it out;
// `window_pt` is that rect through the letterbox's inverse (equal to
// content_pt when letterbox_scale is 1, as it is here); `screen_pt` /
// `frame_pt` are AppKit points; `crop_px` is PIXELS = points x `backing`.
// The one field in pixels is crop_px. (`scale` used to name two different
// things on one line -- the backing scale and the letterbox scale -- and a
// reader taking "the scale field" got the wrong one.)
//
// `crop_px` is the rect inside `screencapture -x -o -l <window>` of this
// window (that capture is the window FRAME without shadow, at backing
// scale): crop the PNG at exactly those pixels -- that is the whole rigid
// transform. Verification is the crop's own edges against the reported
// rect, never against the reference image.
//
// Refuses when: headless / no window; the backend is not the offline mock;
// the widget was not painted this frame; the window server says the window
// is not this process's; the letterbox scale is not 1 (a letterboxed frame
// means the content is not 1:1 with the window and a crop would need a
// resample, which this never does); or the two content sizes -- sokol's and
// AppKit's contentView -- disagree by more than a point.
// capture_marker on <x> <y> <w> <h> | off: the flat test marker the receipt is
// PROVEN on (see capture_marker_system.h). Same gate as capture_receipt --
// windowed, offline mock -- and never persisted.
struct HandleCaptureMarkerCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("capture_marker")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("capture_marker requires on <x> <y> <w> <h> | off");
            return;
        }
        auto& m = ecs::capture_marker_state();
        if (cmd.arg(0) == "off") {
            m = {};
            cmd.consume();
            return;
        }
        const ecs::AppComponent* app = app_component();
        const std::string backend = app ? app->backend_label : std::string("none");
        if (backend != "mock") {
            cmd.fail(std::format("capture_marker refuses a '{}' backend", backend));
            return;
        }
        if (!hanabi_native_has_window()) {
            cmd.fail("capture_marker needs a real window (HANABI_E2E_WINDOWED=1)");
            return;
        }
        if (cmd.arg(0) != "on" || !cmd.has_args(5)) {
            cmd.fail("capture_marker requires on <x> <y> <w> <h> | off");
            return;
        }
        m.on = true;
        m.x = std::atof(cmd.arg(1).c_str());
        m.y = std::atof(cmd.arg(2).c_str());
        m.w = std::atof(cmd.arg(3).c_str());
        m.h = std::atof(cmd.arg(4).c_str());
        cmd.consume();
    }
};

struct HandleCaptureReceiptCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("capture_receipt")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("capture_receipt requires <debug_name> [expect_scale=<s>]");
            return;
        }
        const ecs::AppComponent* app = app_component();
        const std::string backend = app ? app->backend_label : std::string("none");
        if (backend != "mock") {
            cmd.fail(std::format("capture_receipt refuses a '{}' backend: a window "
                                 "capture may only be taken of the offline mock.",
                                 backend));
            return;
        }
        if (!hanabi_native_has_window()) {
            cmd.fail("capture_receipt needs a real window: this run is headless "
                     "(HANABI_E2E_WINDOWED=1).");
            return;
        }
        const std::string& name = cmd.arg(0);
        const afterhours::Entity* target = nullptr;
        for (const auto& e :
             afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
            if (!e || !e->has<afterhours::ui::UIComponent>() ||
                !e->has<afterhours::ui::UIComponentDebug>())
                continue;
            if (e->get<afterhours::ui::UIComponentDebug>().name() != name) continue;
            if (!e->get<afterhours::ui::UIComponent>().was_rendered_to_screen) continue;
            target = e.get();
        }
        if (target == nullptr) {
            if (cmd.frames_alive < kGiveUpFrame) {
                cmd.retry();
                return;
            }
            cmd.fail(std::format("capture_receipt: no painted widget named '{}'", name));
            return;
        }
        const auto r = target->get<afterhours::ui::UIComponent>().rect();

        // Content -> window: the letterbox's inverse, the same Viewport the
        // pointer's window_to_content reads. Refuse a scaled letterbox: the
        // crop is rigid or it is nothing.
        const int winW = afterhours::graphics::get_screen_width();
        const int winH = afterhours::graphics::get_screen_height();
        const auto vp = afterhours::window_manager::content_viewport(winW, winH);
        if (std::fabs(vp.scale - 1.0f) > 0.001f) {
            cmd.fail(std::format("capture_receipt: the content is letterboxed at scale {:.4f} "
                                 "(dest {:.0f},{:.0f} {:.0f}x{:.0f} in a {}x{} window); a "
                                 "rigid crop cannot represent that.",
                                 vp.scale, vp.dest.x, vp.dest.y, vp.dest.width,
                                 vp.dest.height, winW, winH));
            return;
        }
        const auto tl = afterhours::window_manager::content_to_window(
            Vector2Type{r.x, r.y}, winW, winH);
        const auto br = afterhours::window_manager::content_to_window(
            Vector2Type{r.x + r.width, r.y + r.height}, winW, winH);
        const double wx = tl.x, wy = tl.y, ww = br.x - tl.x, wh = br.y - tl.y;

        HanabiNativeWindowReceipt win{};
        hanabi_native_window_receipt(&win);
        if (!win.ok) {
            cmd.fail(std::format("capture_receipt: {}", win.why));
            return;
        }
        // sokol's window size and AppKit's contentView must be the same
        // surface, or the content->window step above is about a different
        // rectangle than the one AppKit will capture.
        if (std::fabs(win.content_w - winW) > 1.0 || std::fabs(win.content_h - winH) > 1.0) {
            cmd.fail(std::format("capture_receipt: sokol says the window is {}x{} but "
                                 "AppKit's contentView is {:.1f}x{:.1f}; transforms disagree.",
                                 winW, winH, win.content_w, win.content_h));
            return;
        }
        if (cmd.args.size() >= 2) {
            const std::string& a = cmd.arg(1);
            if (a.rfind("expect_scale=", 0) == 0) {
                const double want = std::atof(a.c_str() + 13);
                if (std::fabs(want - win.backing_scale) > 0.001) {
                    cmd.fail(std::format("capture_receipt: backing scale is {:.2f}, expected {:.2f}",
                                         win.backing_scale, want));
                    return;
                }
            }
        }
        double sx = 0, sy = 0, sw = 0, sh = 0, fx = 0, fy = 0;
        if (!hanabi_native_content_rect_to_screen(wx, wy, ww, wh, &sx, &sy, &sw, &sh, &fx, &fy)) {
            cmd.fail("capture_receipt: the window went away during the read");
            return;
        }
        const double k = win.backing_scale;
        // The widget's own fill this frame, so the check can prove the crop
        // holds THESE pixels (its border is this colour, one pixel outside is
        // not) rather than merely a rectangle of the right size. "-" when the
        // widget paints no fill of its own.
        std::string fill = "-";
        if (target->has<afterhours::HasColor>()) {
            const auto c = target->get<afterhours::HasColor>().color();
            if (c.a == 255)
                fill = std::format("{:02X}{:02X}{:02X}", static_cast<unsigned>(c.r),
                                   static_cast<unsigned>(c.g), static_cast<unsigned>(c.b));
        }
        std::printf(
            "[capture-receipt] name=%s stamp=%s pid=%d window=%ld owned=%d visible=%d "
            "backing=%.2f content_pt=%.1f,%.1f,%.1fx%.1f window_pt=%.1f,%.1f,%.1fx%.1f "
            "screen_pt=%.1f,%.1f,%.1fx%.1f frame_pt=%.1f,%.1f,%.1fx%.1f "
            "contentview_pt=%.1f,%.1f,%.1fx%.1f crop_px=%.0f,%.0f,%.0fx%.0f fill=%s "
            "letterbox=%.0f,%.0f,%.0fx%.0f letterbox_scale=%.4f\n",
            name.c_str(), build_stamp(), win.pid, win.window_number, win.owned, win.visible,
            k, r.x, r.y, r.width, r.height, wx, wy, ww, wh, sx, sy, sw, sh, win.frame_x,
            win.frame_y, win.frame_w, win.frame_h, win.content_x, win.content_y,
            win.content_w, win.content_h, std::round(fx * k), std::round(fy * k),
            std::round(sw * k), std::round(sh * k), fill.c_str(), vp.dest.x, vp.dest.y,
            vp.dest.width, vp.dest.height, vp.scale);
        std::fflush(stdout);
        cmd.consume();
    }
};

struct HandleExpectContentSizeCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_content_size")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("expect_content_size requires w h");
            return;
        }
        float w = 0, h = 0;
        hanabi_native_content_size(&w, &h);
        const int ww = static_cast<int>(w + 0.5f), hh = static_cast<int>(h + 0.5f);
        if (ww == std::atoi(cmd.arg(0).c_str()) && hh == std::atoi(cmd.arg(1).c_str())) {
            cmd.consume();
            return;
        }
        if (cmd.frames_alive < kGiveUpFrame) {
            cmd.retry();
            return;
        }
        cmd.fail(std::format("expect_content_size: window content is {}x{}, not {}x{}",
                             ww, hh, cmd.arg(0), cmd.arg(1)));
    }
};

struct HandleExpectResizesAppliedCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_resizes_applied")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("expect_resizes_applied requires a count");
            return;
        }
        const int want = cmd.arg_as<int>(0);
        const int got = static_cast<int>(hanabi::gfx::applied_resize_count());
        if (got == want) {
            cmd.consume();
            return;
        }
        cmd.fail(std::format(
            "expected {} resize(s) applied at a frame boundary, saw {}", want,
            got));
    }
};

inline bool latency_target_present(hanabi::latency::Outcome outcome,
                                   const std::string& target) {
    using hanabi::latency::Outcome;
    if (outcome == Outcome::UiAppears || outcome == Outcome::UiDisappears)
        return afterhours::testing::ui_commands::find_component_center<
                   InputAction>(target)
            .has_value();
    if (outcome == Outcome::TextAppears || outcome == Outcome::TextDisappears)
        return afterhours::testing::VisibleTextRegistry::instance().contains(
            target);
    auto* ctx = afterhours::EntityHelper::get_singleton_cmp<
        afterhours::ui::UIContext<InputAction>>();
    if (ctx == nullptr) return false;
    if (outcome == Outcome::PressHeld || outcome == Outcome::HoverHeld) {
        afterhours::EntityID want = outcome == Outcome::PressHeld
                                        ? ctx->active_id
                                        : ctx->hot_id;
        const bool pointerHeld =
            outcome == Outcome::HoverHeld ||
            (ctx->mouse.left_down && want != ctx->ROOT);
        if (outcome == Outcome::PressHeld && !pointerHeld)
            want = hanabi::control::keyboard_pressed_id();
        if (want == ctx->ROOT || want == -1) return false;
        auto opt = afterhours::ui::UICollectionHolder::getEntityForID(want);
        if (!opt.valid()) return false;
        const afterhours::Entity& held = opt.asE();
        return held.has<afterhours::ui::UIComponentDebug>() &&
               held.get<afterhours::ui::UIComponentDebug>().name() == target;
    }
    for (const auto& handle :
         afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
        if (!handle) continue;
        const afterhours::Entity& entity = *handle;
        if (!entity.has<afterhours::ui::UIComponentDebug>()) continue;
        if (entity.get<afterhours::ui::UIComponentDebug>().name() != target)
            continue;
        const auto id = entity.get<afterhours::ui::UIComponent>().id;
        if (ctx->has_focus(id) || ctx->contains_in_subtree(id, ctx->focus_id))
            return true;
    }
    return false;
}

inline bool latency_outcome_reached(const hanabi::latency::Watch& watch) {
    return latency_target_present(watch.outcome, watch.target) ==
           hanabi::latency::expects_present(watch.outcome);
}

inline bool latency_input_command(std::string_view name) {
    static constexpr std::string_view inputs[] = {
        "click",          "click_ui",
        "click_text",     "click_button",
        "double_click",   "double_click_ui",
        "double_click_word",
        "triple_click",   "right_click",
        "right_click_ui", "right_click_text",
        "middle_click",   "key",
        "type",           "scroll_wheel",
        "focus_ui",       "toggle_checkbox",
        "drag",           "drag_to",
        "mouse_down",     "mouse_up",
        "mouse_move",     "hover_ui",
        "mouse_down_ui",  "focus_ui",
        "select_all",     "action",
        "click_link",     "resize",
        "enter",          "tab",
        "escape",
    };
    return std::find(std::begin(inputs), std::end(inputs), name) !=
           std::end(inputs);
}

inline int latency_input_effect_lag(std::string_view name) {
    return name == "key" || name == "enter" || name == "escape" || name == "tab"
               ? 1
               : 0;
}

inline void stamp_queued_latency_event() {
    if (hanabi::latency::active() == nullptr ||
        hanabi::latency::active()->phase !=
            hanabi::latency::Phase::AwaitingEvent)
        return;
    for (const auto& handle : afterhours::EntityHelper::get_temp()) {
        if (!handle || !handle->has<afterhours::testing::PendingE2ECommand>())
            continue;
        const auto& cmd = handle->get<afterhours::testing::PendingE2ECommand>();
        if (!cmd.is_consumed() && latency_input_command(cmd.name)) {
            (void) hanabi::latency::event_queued();
            return;
        }
    }
}

struct DelayedLatencyInput {
    std::string command;
    int frames_left = 0;
};

inline std::unordered_map<afterhours::EntityID, DelayedLatencyInput>&
delayed_latency_inputs() {
    static std::unordered_map<afterhours::EntityID, DelayedLatencyInput> value;
    return value;
}

inline int latency_app_delay_frames() {
    static const int value = [] {
        const char* raw = std::getenv("HANABI_LATENCY_APP_DELAY_FRAMES");
        if (raw == nullptr || *raw == '\0') return 0;
        return std::clamp(std::atoi(raw), 0, 20);
    }();
    return value;
}

struct LatencyInputEventSystem
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void once(float) override {
        afterhours::EntityHelper::merge_entity_arrays();
    }

    void for_each_with(afterhours::Entity& entity,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed()) return;
        auto delayed = delayed_latency_inputs().find(entity.id);
        if (delayed != delayed_latency_inputs().end()) {
            if (delayed->second.frames_left > 0) {
                --delayed->second.frames_left;
                cmd.retry();
                return;
            }
            const std::string command = delayed->second.command;
            cmd.name = command;
            cmd.reset_retry();
            delayed_latency_inputs().erase(delayed);
            hanabi::latency::input_delivered(latency_input_effect_lag(command));
            return;
        }
        if (!latency_input_command(cmd.name) ||
            !hanabi::latency::awaiting_delivery())
            return;
        const int delay = latency_app_delay_frames();
        if (delay == 0) {
            hanabi::latency::input_delivered(
                latency_input_effect_lag(cmd.name));
            return;
        }
        delayed_latency_inputs()[entity.id] = {cmd.name, delay - 1};
        cmd.name = "__hanabi_latency_delayed_input";
        cmd.retry();
    }
};

struct HandleWatchInkCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("watch_ink")) return;
        if (!cmd.has_args(3)) {
            cmd.fail(
                "watch_ink requires <label> "
                "<ui|ui_gone|text|text_gone|focus|blur|press|hover> <target>");
            return;
        }
        const std::string label = cmd.arg(0);
        const std::string kind = cmd.arg(1);
        std::string target;
        for (std::size_t i = 2; i < cmd.args.size(); ++i) {
            if (!target.empty()) target += ' ';
            target += cmd.args[i];
        }
        if (target.size() >= 2 && target.front() == '"' && target.back() == '"')
            target = target.substr(1, target.size() - 2);

        using hanabi::latency::Outcome;
        std::optional<Outcome> outcome;
        if (kind == "ui")
            outcome = Outcome::UiAppears;
        else if (kind == "ui_gone")
            outcome = Outcome::UiDisappears;
        else if (kind == "text")
            outcome = Outcome::TextAppears;
        else if (kind == "text_gone")
            outcome = Outcome::TextDisappears;
        else if (kind == "focus")
            outcome = Outcome::FocusGained;
        else if (kind == "blur")
            outcome = Outcome::FocusLost;
        else if (kind == "press")
            outcome = Outcome::PressHeld;
        else if (kind == "hover")
            outcome = Outcome::HoverHeld;
        if (!outcome.has_value()) {
            cmd.fail(std::format("watch_ink: unknown outcome '{}'", kind));
            return;
        }

        const bool present = latency_target_present(*outcome, target);
        if (auto error =
                hanabi::latency::arm(label, *outcome, target, present)) {
            cmd.fail(std::format("watch_ink {}: {}", label, *error));
            return;
        }
        cmd.consume();
    }
};

struct HandleExpectLatencyCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_latency")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("expect_latency requires <label> <max_frames>");
            return;
        }
        const std::string label = cmd.arg(0);
        const int budget = cmd.arg_as<int>(1);
        hanabi::latency::Watch* watch = hanabi::latency::find(label);
        if (watch == nullptr) {
            cmd.fail(std::format("expect_latency {}: no such arm", label));
            return;
        }
        if (watch->phase != hanabi::latency::Phase::Settled ||
            !watch->reading.has_value()) {
            cmd.fail(std::format("expect_latency {}: {} ('{}')", label,
                                 hanabi::latency::unresolved_reason(*watch),
                                 watch->target));
            return;
        }
        const auto reading = *watch->reading;
        std::fprintf(stderr,
                     "[latency] %s event_to_ink_us=%llu frames=%ld changed=%d "
                     "ink_gained=%d ink_lost=%d required_direction=%s "
                     "required_ink=%d ink_before=%d ink_after=%d probe_us=%llu "
                     "budget_frames=%d\n",
                     label.c_str(),
                     static_cast<unsigned long long>(reading.event_to_ink_us),
                     reading.frames, reading.changed, reading.ink_gained,
                     reading.ink_lost,
                     hanabi::latency::ink_direction_name(watch->outcome),
                     reading.required_ink, reading.ink_before,
                     reading.ink_after,
                     static_cast<unsigned long long>(reading.probe_us), budget);
        hanabi::latency::mark_reported(*watch);
        if (reading.frames > budget) {
            cmd.fail(
                std::format("expect_latency {}: {} frames from event timestamp "
                            "to first ink, budget {}",
                            label, reading.frames, budget));
            return;
        }
        cmd.consume();
    }
};

struct HandleHoverUICommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("hover_ui")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("hover_ui requires <name>");
            return;
        }
        const std::string name = cmd.arg(0);
        auto center =
            afterhours::testing::ui_commands::find_component_center<InputAction>(
                name);
        if (!center.has_value()) {
            if (cmd.frames_alive < kGiveUpFrame) {
                cmd.retry();
                return;
            }
            cmd.fail(std::format("hover_ui: no widget named '{}'", name));
            return;
        }
        afterhours::testing::platform_input::set_mouse_position(center->x,
                                                                center->y);
        cmd.consume();
    }
};

inline bool text_entry(const afterhours::Entity& e) {
    return e.has<afterhours::text_input::HasTextInputState>() ||
           e.has<afterhours::text_input::HasTextAreaState>();
}

inline bool text_entry_parent(const afterhours::ui::UIComponent& uic) {
    auto opt = afterhours::ui::UICollectionHolder::getEntityForID(uic.parent);
    return opt.valid() && text_entry(opt.asE());
}

inline std::vector<std::string> rejoin_quoted(
    const std::vector<std::string>& raw) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        std::string piece = raw[i];
        const std::size_t quote = piece.find('"');
        if (quote != std::string::npos &&
            piece.find('"', quote + 1) == std::string::npos) {
            while (++i < raw.size()) {
                piece += " " + raw[i];
                if (raw[i].find('"') != std::string::npos) break;
            }
        }
        std::string cleaned;
        for (char c : piece)
            if (c != '"') cleaned += c;
        out.push_back(cleaned);
    }
    return out;
}

struct HandleA11yPressCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("a11y_press")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("a11y_press requires <name>");
            return;
        }
        const std::string name = rejoin_quoted(cmd.args)[0];
        if (native_a11y_perform_press(name.c_str()) == 0) {
            if (cmd.frames_alive < kGiveUpFrame) {
                cmd.retry();
                return;
            }
            cmd.fail(std::format(
                "a11y_press: '{}' refused the platform press action "
                "({} elements published)",
                name, native_a11y_published_count()));
            return;
        }
        std::printf("[a11y] pressed %s through the platform action\n",
                    name.c_str());
        cmd.consume();
    }
};

struct HandleExpectA11yPressRefusedCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_a11y_press_refused")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("expect_a11y_press_refused requires <name>");
            return;
        }
        const std::string name = rejoin_quoted(cmd.args)[0];
        char spoken[512] = {};
        native_a11y_describe(name.c_str(), spoken, sizeof(spoken));
        if (spoken[0] == '\0') {
            if (cmd.frames_alive < kGiveUpFrame) {
                cmd.retry();
                return;
            }
            cmd.fail(std::format(
                "expect_a11y_press_refused: '{}' is not published", name));
            return;
        }
        if (native_a11y_perform_press(name.c_str()) != 0) {
            cmd.fail(std::format(
                "expect_a11y_press_refused: '{}' ACCEPTED the platform press "
                "action; a disabled control must advertise none",
                name));
            return;
        }
        std::printf("[a11y] %s refused the platform press\n", name.c_str());
        cmd.consume();
    }
};

// The inverse of expect_a11y: the name must NOT be published. Settles only
// after the same give-up window, so a mark that is about to appear cannot
// pass by being early.
struct HandleExpectNoA11yCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_no_a11y")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("expect_no_a11y requires <name>");
            return;
        }
        const std::string name = rejoin_quoted(cmd.args)[0];
        char spoken[512] = {};
        native_a11y_describe(name.c_str(), spoken, sizeof(spoken));
        if (spoken[0] != '\0') {
            cmd.fail(std::format(
                "expect_no_a11y: '{}' is published to the platform "
                "accessibility tree as '{}'",
                name, spoken));
            return;
        }
        std::printf("[a11y] %s is not published\n", name.c_str());
        cmd.consume();
    }
};

struct HandleExpectA11yCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_a11y")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("expect_a11y requires <name> <prop>=<value> [...]");
            return;
        }
        std::vector<std::string> args = rejoin_quoted(cmd.args);
        const std::string name = args[0];
        char spoken[512] = {};
        native_a11y_describe(name.c_str(), spoken, sizeof(spoken));
        if (spoken[0] == '\0') {
            if (cmd.frames_alive < kGiveUpFrame) {
                cmd.retry();
                return;
            }
            cmd.fail(std::format(
                "expect_a11y: '{}' is not published to the platform "
                "accessibility tree ({} elements are)",
                name, native_a11y_published_count()));
            return;
        }
        for (std::size_t i = 1; i < args.size(); ++i) {
            const std::string& arg = args[i];
            const auto eq = arg.find('=');
            if (eq == std::string::npos) {
                cmd.fail(std::format("expect_a11y: '{}' is not prop=value",
                                     arg));
                return;
            }
            const std::string prop = arg.substr(0, eq);
            const std::string want = arg.substr(eq + 1);
            char got[512] = {};
            if (prop == "role") {
                native_a11y_role_of(name.c_str(), got, sizeof(got));
            } else if (prop == "parent") {
                native_a11y_parent_of(name.c_str(), got, sizeof(got));
            } else if (prop == "children") {
                std::snprintf(got, sizeof(got), "%zu",
                              native_a11y_child_count(name.c_str()));
            } else if (prop == "says") {
                std::snprintf(got, sizeof(got), "%s", spoken);
            } else {
                cmd.fail(std::format("expect_a11y: unknown property '{}'",
                                     prop));
                return;
            }
            if (want != got) {
                cmd.fail(std::format(
                    "expect_a11y '{}': {}='{}' but the platform says '{}'",
                    name, prop, want, got));
                return;
            }
        }
        std::printf("[a11y] %s: %s\n", name.c_str(), spoken);
        cmd.consume();
    }
};

struct HandleExpectChildrenInsideCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_children_inside")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("expect_children_inside requires <name>");
            return;
        }
        const std::string name = cmd.arg(0);

        const afterhours::ui::UIComponent* root = nullptr;
        for (const auto& e :
             afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
            if (!e || !e->has<afterhours::ui::UIComponentDebug>()) continue;
            if (e->get<afterhours::ui::UIComponentDebug>().name() != name)
                continue;
            if (!e->has<afterhours::ui::UIComponent>()) continue;
            const auto& uic = e->get<afterhours::ui::UIComponent>();
            if (!uic.was_rendered_to_screen) continue;
            root = &uic;
            break;
        }
        if (root == nullptr) {
            if (cmd.frames_alive < kGiveUpFrame) {
                cmd.retry();
                return;
            }
            cmd.fail(std::format(
                "expect_children_inside: no rendered widget named '{}'", name));
            return;
        }

        const auto box = root->rect();
        std::vector<std::string> escaped;
        int checked = 0;
        walk(*root, box, escaped, checked);
        if (checked == 0) {
            cmd.fail(std::format(
                "expect_children_inside: '{}' has no rendered children", name));
            return;
        }
        if (!escaped.empty()) {
            std::string joined;
            for (const std::string& one : escaped) {
                if (!joined.empty()) joined += ", ";
                joined += one;
            }
            cmd.fail(std::format(
                "expect_children_inside: {} of {} descendants of '{}' paint "
                "outside it: {}",
                escaped.size(), checked, name, joined));
            return;
        }
        std::printf("[children-inside] %d descendants of %s all within\n",
                    checked, name.c_str());
        cmd.consume();
    }

   private:
    static void walk(const afterhours::ui::UIComponent& parent,
                     const RectangleType& box,
                     std::vector<std::string>& escaped, int& checked) {
        for (afterhours::EntityID childId : parent.children) {
            auto opt = afterhours::ui::UICollectionHolder::getEntityForID(childId);
            if (!opt.valid()) continue;
            const auto& child = opt.asE();
            if (!child.has<afterhours::ui::UIComponent>()) continue;
            const auto& uic = child.get<afterhours::ui::UIComponent>();
            if (!uic.was_rendered_to_screen || uic.should_hide) continue;
            const auto r = uic.rect();
            if (r.width > 0.0f && r.height > 0.0f) {
                ++checked;
                const bool inside = r.x >= box.x - 0.5f && r.y >= box.y - 0.5f &&
                                    r.x + r.width <= box.x + box.width + 0.5f &&
                                    r.y + r.height <= box.y + box.height + 0.5f;
                if (!inside) {
                    const std::string cname =
                        child.has<afterhours::ui::UIComponentDebug>()
                            ? child.get<afterhours::ui::UIComponentDebug>().name()
                            : std::string("<unnamed>");
                    escaped.push_back(std::format("{} [{},{} {}x{}]", cname,
                                                  r.x, r.y, r.width, r.height));
                }
            }
            walk(uic, box, escaped, checked);
        }
    }
};

struct HandleMouseDownUICommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("mouse_down_ui")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("mouse_down_ui requires <name>");
            return;
        }
        const std::string name = cmd.arg(0);
        auto center =
            afterhours::testing::ui_commands::find_component_center<InputAction>(
                name);
        if (!center.has_value()) {
            if (cmd.frames_alive < kGiveUpFrame) {
                cmd.retry();
                return;
            }
            cmd.fail(std::format("mouse_down_ui: no widget named '{}'", name));
            return;
        }
        afterhours::testing::platform_input::set_mouse_position(center->x,
                                                                center->y);
        auto& m = afterhours::testing::input_injector::detail::mouse;
        m.left_down = true;
        m.just_pressed = true;
        m.press_frames = 0;
        m.auto_release = false;
        m.active = true;
        cmd.consume();
    }
};

struct HandleExpectHitTargetsCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_hit_targets")) return;
        std::vector<std::string> allowed;
        bool requireText = false;
        for (const auto& a : cmd.args) {
            if (a == "with_text") {
                requireText = true;
                continue;
            }
            allowed.push_back(a);
        }

        std::vector<std::string> undersized;
        int checked = 0;
        int textSurfaces = 0;
        for (const auto& e :
             afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
            if (!e) continue;
            if (!e->has<afterhours::ui::HasClickListener>()) continue;
            if (!e->has<afterhours::ui::UIComponent>()) continue;
            const auto& uic = e->get<afterhours::ui::UIComponent>();
            if (!uic.was_rendered_to_screen) continue;
            const auto r = uic.rect();
            if (r.width <= 0.0f || r.height <= 0.0f) continue;
            if (hanabi::control::is_text_surface(*e) ||
                text_entry(*e) || text_entry_parent(uic)) {
                ++textSurfaces;
                continue;
            }
            const std::string name =
                e->has<afterhours::ui::UIComponentDebug>()
                    ? e->get<afterhours::ui::UIComponentDebug>().name()
                    : std::string("<unnamed>");
            if (std::find(allowed.begin(), allowed.end(), name) !=
                allowed.end())
                continue;
            ++checked;
            if (hanabi::control::meets_hit_target(r.width, r.height)) continue;
            undersized.push_back(std::format("{} {}x{}", name, r.width,
                                             r.height));
        }
        if (checked == 0) {
            cmd.fail("expect_hit_targets found no clickable widget to measure");
            return;
        }
        if (requireText && textSurfaces == 0) {
            cmd.fail(
                "expect_hit_targets with_text classified no text surface; the "
                "split between controls and selectable text is not live");
            return;
        }
        if (!undersized.empty()) {
            std::string joined;
            for (const auto& u : undersized) {
                if (!joined.empty()) joined += ", ";
                joined += u;
            }
            cmd.fail(std::format(
                "expect_hit_targets: {} of {} clickable widgets are under {}pt: {}",
                undersized.size(), checked, hanabi::control::kMinHitTarget,
                joined));
            return;
        }
        std::printf(
            "[hit-targets] %d controls all >= %.0fpt, %d text surfaces "
            "classified out\n",
            checked, hanabi::control::kMinHitTarget, textSurfaces);
        cmd.consume();
    }
};

struct HandleExpectTooltipsCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_tooltips")) return;
        const std::vector<std::string> allowed = cmd.args;

        std::vector<std::string> silent;
        int checked = 0;
        for (const auto& e :
             afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
            if (!e) continue;
            if (!e->has<afterhours::ui::HasClickListener>()) continue;
            if (!e->has<afterhours::ui::UIComponent>()) continue;
            const auto& uic = e->get<afterhours::ui::UIComponent>();
            if (!uic.was_rendered_to_screen) continue;
            const auto r = uic.rect();
            if (r.width <= 0.0f || r.height <= 0.0f) continue;
            if (hanabi::control::is_text_surface(*e) || text_entry(*e) ||
                text_entry_parent(uic))
                continue;
            if (ecs::control_says_words(*e)) continue;
            const std::string name =
                e->has<afterhours::ui::UIComponentDebug>()
                    ? e->get<afterhours::ui::UIComponentDebug>().name()
                    : std::string("<unnamed>");
            if (std::find(allowed.begin(), allowed.end(), name) !=
                allowed.end())
                continue;
            ++checked;
            if (!ecs::tooltip_text_for(name, *e).empty()) continue;
            silent.push_back(name);
        }
        if (checked == 0) {
            cmd.fail("expect_tooltips found no wordless control to check");
            return;
        }
        if (!silent.empty()) {
            std::string joined;
            for (const std::string& s : silent) {
                if (!joined.empty()) joined += ", ";
                joined += s;
            }
            cmd.fail(std::format(
                "expect_tooltips: {} of {} wordless controls say nothing on "
                "hover: {}",
                silent.size(), checked, joined));
            return;
        }
        std::printf("[tooltips] %d wordless controls all name themselves\n",
                    checked);
        cmd.consume();
    }
};

// `release_compaction`: let the mock's latched compaction round finish.
//
// The runner's only notion of time is the tick, three times over: `wait N` is
// N seconds of the host's dt, `wait_frames N` is N ticks, the per-command
// retry deadline is MAX_FRAMES = 30 ticks with only those two builtin names
// exempt (`pending_command.h:60,70-72`, `command_handlers.h:874`), and the
// whole-script deadline is 10 seconds of dt (`runner.h:630`, #223). A worker
// thread holding a state for real seconds cannot be awaited by any of them
// (afterhours_gaps.md #591), and holding the frame loop instead would stop
// the very frames the running state is observed through. So the mock's round
// waits on a latch (HANABI_MOCK_COMPACT_HOLD_MS=latch) and this command opens
// it: the script looks at the running divider for as many frames as it likes,
// releases, and watches the promotion land through the ordinary collect ->
// drain path, frames, input and paint all running.
struct HandleReleaseCompactionCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("release_compaction")) return;
        api::MockClient::release_compaction();
        cmd.consume();
    }
};

// Registered BEFORE afterhours' builtins, so the resize handler above sees
// `resize` first. Everything else hanabi owns goes in the function below.
// The wrong door, refused. While `native_mode on` holds, the harness's
// injected commands (click, type, key ...) write the injector's state, which
// nothing reads in that mode: the command "runs", moves nothing, and the run
// stays green until some later assertion happens to notice. Registered ahead
// of the library's handlers (register_hanabi_pre_handlers), so the refusal
// is at the line that mixed the two paths, not at the assertion after it.
struct HandleInjectedInputWhileNativeCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    static bool is_injected_input(const afterhours::testing::PendingE2ECommand& cmd) {
        static constexpr const char* kNames[] = {
            "click", "click_ui", "click_text", "click_button", "double_click",
            "triple_click", "right_click", "right_click_ui", "right_click_text",
            "double_click_word", "middle_click", "middle_down", "middle_up", "mouse_move",
            "mouse_down", "mouse_up", "drag", "drag_to", "pinch",
            "scroll_wheel", "type", "key", "hold", "release", "arrow", "enter",
            "escape", "tab", "shift_tab", "action", "select_all",
            "toggle_checkbox", "set_slider", "select_dropdown", "focus_ui"};
        for (const char* n : kNames)
            if (cmd.is(n)) return true;
        return false;
    }
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !native_mode_on()) return;
        if (!is_injected_input(cmd)) return;
        cmd.fail(std::format(
            "`{}` is injected input, and this script is in native mode: nothing "
            "reads the injector while `native_mode on` holds, so the command "
            "would move nothing. Use its native_* form, or `native_mode off` first.",
            cmd.name));
    }
};

inline void register_hanabi_pre_handlers(afterhours::SystemManager& sm) {
    sm.register_update_system(std::make_unique<HandleResizeDeferredCommand>());
    sm.register_update_system(
        std::make_unique<HandleInjectedInputWhileNativeCommand>());
    sm.register_update_system(
        std::make_unique<HandleExpectResizesAppliedCommand>());
    sm.register_update_system(std::make_unique<LatencyInputEventSystem>());
}

// ---------------------------------------------------------------------------
// The surface-target refusal contract, driven from a script.
//
// composer_target_for refuses a surface id, so no script can make the
// composer build one; these commands make the MALFORMED request the loader's
// backstop exists for -- a persisted or replayed message whose target names a
// surface -- and read the state the contract promises: never sent, never
// retried, text and attachments kept, the notice held until acknowledged and
// raised again from the retained record at the next restore pass.
//
//   force_send <session_id> <text...>         set requestSend with that target,
//                                             one attachment named forced.png
//   seed_outbox <session_id> <text...>        write a kept record (with the same
//                                             attachment) straight to disk, the
//                                             way a previous run would have
//   reload_settings                           read the settings file back from
//                                             disk (the path a restart takes)
//   outbox_restore_again                      run the launch-time restore pass
//                                             again (the path a restart takes)
//   expect_toast_holds <text...>              the toast is up, holding, and its
//                                             message contains <text>
//   expect_no_toast                           no toast is showing
//   expect_outbox_attachment <id> <name>      a kept record under <id> carries
//                                             an attachment called <name>
//   expect_outbox_retry_count <id> <n>        the retry queue holds <n> entries
//                                             for <id>
//   expect_mock_outbound_calls <n>            the mock has been asked to
//                                             send/steer/create <n> times
// ---------------------------------------------------------------------------
inline api::OutgoingMessage forced_surface_message(const std::string& sessionId,
                                                   std::string text, int pane) {
    api::OutgoingMessage m;
    m.local_id = api::attachments::make_local_id();
    m.text = std::move(text);
    api::Attachment a;
    a.path = "/nonexistent/forced.png";
    a.name = "forced.png";
    a.media_type = "image/png";
    a.size_bytes = 3;
    m.attachments.push_back(a);
    m.target.pane_index = pane;
    m.target.session_id = sessionId;
    m.target.draft_key = sessionId;
    return m;
}

struct HandleForceSendCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("force_send")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("force_send requires <session_id> <text>");
            return;
        }
        ecs::AppComponent* app = app_component();
        if (app == nullptr) {
            cmd.fail("force_send: no app");
            return;
        }
        app->requestSend = forced_surface_message(
            cmd.arg(0), joined_args(cmd, 1), std::clamp(app->focusedPane, 0, 1));
        cmd.consume();
    }
};

struct HandleSeedOutboxCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("seed_outbox")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("seed_outbox requires <session_id> <text>");
            return;
        }
        api::disk_cache::outbox_add(
            cmd.arg(0), forced_surface_message(cmd.arg(0), joined_args(cmd, 1), 0));
        cmd.consume();
    }
};

// `reload_settings`: read the settings file back from disk, the way a fresh
// process would, and re-seed what the app copies out of it at startup (the
// saved views are read from Settings each frame; expansion is copied). Holds
// "it survives a restart" without a restart.
struct HandleReloadSettingsCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("reload_settings")) return;
        ecs::AppComponent* app = app_component();
        if (app == nullptr) {
            cmd.fail("reload_settings: no app");
            return;
        }
        if (!Settings::get().load_save_file()) {
            cmd.fail("reload_settings: the settings file could not be read");
            return;
        }
        app->expandedParents = Settings::get().get_expanded_parents();
        ++app->expandedRevision;
        // Same rule as startup: the lit view returns only if the store has it.
        app->savedViewId.clear();
        if (const std::string& sv = Settings::get().get_selected_view();
            !sv.empty() && Settings::get().saved_views().find(sv) != nullptr)
            app->savedViewId = sv;
        cmd.consume();
    }
};

struct HandleOutboxRestoreAgainCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("outbox_restore_again")) return;
        ecs::AppComponent* app = app_component();
        if (app == nullptr) {
            cmd.fail("outbox_restore_again: no app");
            return;
        }
        // The same flag a fresh process starts with; the loader's next tick
        // runs the launch-time pass over whatever the disk holds now.
        app->outboxRestored = false;
        cmd.consume();
    }
};

struct HandleExpectToastCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        const bool holds = cmd.is("expect_toast_holds");
        const bool none = cmd.is("expect_no_toast");
        if (cmd.is_consumed() || (!holds && !none)) return;
        ecs::AppComponent* app = app_component();
        if (app == nullptr) {
            cmd.fail("toast assertion: no app");
            return;
        }
        bool ok = false;
        std::string actual = app->toastMessage.empty() ? "(no toast)" : app->toastMessage;
        if (none) {
            ok = app->toastMessage.empty();
        } else {
            if (!cmd.has_args(1)) {
                cmd.fail("expect_toast_holds requires <text>");
                return;
            }
            // Matches the notice OR the words it offers to copy, so a script
            // can tell WHICH kept record the one toast is speaking for.
            const std::string want = joined_args(cmd, 0);
            ok = app->toastHolds &&
                 (app->toastMessage.find(want) != std::string::npos ||
                  app->toastCopyText.find(want) != std::string::npos);
        }
        if (ok) {
            cmd.consume();
            return;
        }
        if (cmd.frames_alive < kGiveUpFrame) {
            cmd.retry();
            return;
        }
        cmd.fail(std::format("{}: toast is \"{}\" (holds={})", cmd.name, actual,
                             app->toastHolds ? 1 : 0));
    }
};

struct HandleExpectOutboxAttachmentCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_outbox_attachment")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("expect_outbox_attachment requires <id> <name>");
            return;
        }
        for (const auto& kept : api::disk_cache::outbox_messages(cmd.arg(0)))
            for (const auto& a : kept.attachments)
                if (a.name == cmd.arg(1)) {
                    cmd.consume();
                    return;
                }
        if (cmd.frames_alive < kGiveUpFrame) {
            cmd.retry();
            return;
        }
        cmd.fail(std::format("outbox for '{}' has no attachment named '{}'",
                             cmd.arg(0), cmd.arg(1)));
    }
};

// expect_mock_outbound_calls <n>: the mock backend has been asked to send,
// steer or create exactly <n> times since launch. Direct evidence that a
// refused request never reached the client, independent of any state the
// app keeps about it (the retry queue is a consequence, not the call).
struct HandleExpectMockOutboundCallsCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_mock_outbound_calls")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("expect_mock_outbound_calls requires <n>");
            return;
        }
        const int have = api::MockClient::outbound_calls().load();
        const int want = std::atoi(cmd.arg(0).c_str());
        if (have == want) {
            cmd.consume();
            return;
        }
        if (have < want && cmd.frames_alive < kGiveUpFrame) {
            cmd.retry();
            return;
        }
        cmd.fail(std::format("mock backend saw {} outbound call(s), expected {}",
                             have, want));
    }
};

// expect_font_face <alias> <weight> <real|fallback> [family]: what the font
// loader REGISTERED under a weight alias (`semibold` = `__default@semibold`,
// `regular` = `__default`), from its own report -- the weight of the face file
// it landed on, whether that was a fallback down the ladder, that the file
// loaded, and (optionally) which family the face came from -- so a script
// proves a heavier face is really behind the alias rather than reading pixels.
struct HandleExpectFontFaceCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_font_face")) return;
        if (!cmd.has_args(3)) {
            cmd.fail("expect_font_face requires <alias> <weight> <real|fallback> [family]");
            return;
        }
        const std::string alias = cmd.arg(0) == "regular"
                                      ? std::string(hanabi::fonts::kDefaultFontName)
                                      : std::string(hanabi::fonts::kDefaultFontName) + "@" + cmd.arg(0);
        const hanabi::fonts::AliasReport* found = nullptr;
        for (const auto& r : hanabi::fonts::report())
            if (r.plan.alias == alias) found = &r;
        std::string got = "(not registered)";
        bool ok = false;
        if (found != nullptr) {
            got = found->plan.weight + (found->plan.fallback ? " fallback" : " real") +
                  (found->loaded ? "" : " NOT LOADED") + " from " + found->family +
                  " " + found->plan.path;
            const bool wantFallback = cmd.arg(2) == "fallback";
            ok = found->loaded && found->plan.weight == cmd.arg(1) &&
                 found->plan.fallback == wantFallback &&
                 (cmd.args.size() < 4 || found->family == cmd.arg(3));
        }
        if (ok) {
            cmd.consume();
            return;
        }
        if (cmd.frames_alive < kGiveUpFrame) {
            cmd.retry();
            return;
        }
        cmd.fail(std::format("font alias {} is [{}], expected {} {}{}", alias, got,
                             cmd.arg(1), cmd.arg(2),
                             cmd.args.size() >= 4 ? " from " + cmd.arg(3) : std::string()));
    }
};

// expect_saved_views <id=name,...> <removed,...|->: the saved-view STORE, as
// records, in order -- the exact list a restart would read back, and the
// built-ins the reader deleted -- so a shelf-menu script asserts what was
// stored rather than what happens to be painted. A built-in reads by its id
// (home, blocked, ...); a saved view by its own id, which a script knows
// only through `sb_view_<id>` markers -- so for one made in the script, the
// name alone may be given as `*=Name`.
struct HandleExpectSavedViewsCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    static std::string joined(const hanabi::views::Store& store) {
        std::string out;
        for (const auto& v : store.all()) {
            if (!out.empty()) out += ",";
            out += v.id + "=" + v.name;
        }
        return out;
    }
    static bool matches(const std::string& want, const hanabi::views::Store& store) {
        std::vector<std::string> parts;
        std::string cur;
        for (char c : want) {
            if (c == ',') { parts.push_back(cur); cur.clear(); } else cur += c;
        }
        parts.push_back(cur);
        if (want.empty()) parts.clear();
        const auto& all = store.all();
        if (parts.size() != all.size()) return false;
        for (std::size_t i = 0; i < all.size(); ++i) {
            const auto eq = parts[i].find('=');
            const std::string id = eq == std::string::npos ? parts[i] : parts[i].substr(0, eq);
            const std::string name = eq == std::string::npos ? std::string() : parts[i].substr(eq + 1);
            if (id != "*" && id != all[i].id) return false;
            if (!name.empty() && name != all[i].name) return false;
        }
        return true;
    }
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_saved_views")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("expect_saved_views requires <id=name,...> <removed,...|->");
            return;
        }
        // The runner whitespace-splits a custom command's tokens as typed,
        // quotes included: a name with a space arrives as several args. The
        // removed list is the LAST arg; everything before it, re-joined with
        // spaces and stripped of a matching quote pair (joined_args' rule), is
        // the expectation.
        const std::string wantRemoved = cmd.arg(cmd.args.size() - 1);
        std::string wantViews;
        for (std::size_t i = 0; i + 1 < cmd.args.size(); ++i) {
            if (!wantViews.empty()) wantViews.push_back(' ');
            wantViews += cmd.arg(i);
        }
        if (wantViews.size() >= 2 &&
            ((wantViews.front() == '"' && wantViews.back() == '"') ||
             (wantViews.front() == '\'' && wantViews.back() == '\'')))
            wantViews = wantViews.substr(1, wantViews.size() - 2);
        const auto& store = Settings::get().saved_views();
        std::string removed;
        for (const auto& r : store.removed_built_ins()) {
            if (!removed.empty()) removed += ",";
            removed += r;
        }
        if (removed.empty()) removed = "-";
        if (matches(wantViews, store) && removed == wantRemoved) {
            cmd.consume();
            return;
        }
        if (cmd.frames_alive < kGiveUpFrame) {
            cmd.retry();
            return;
        }
        cmd.fail(std::format("saved views are [{}] removed [{}], expected [{}] removed [{}]",
                             joined(store), removed, wantViews, wantRemoved));
    }
};

// expect_backend_tuning <id> <model|-> <effort|->: the mock session's OWN
// tuning as the backend holds it after a patch_session_options -- the
// receipt that the change reached the session (and the right one), read
// from the client, not from the chip. `-` = no pin.
struct HandleExpectBackendTuningCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_backend_tuning")) return;
        if (!cmd.has_args(3)) {
            cmd.fail("expect_backend_tuning requires <id> <model|-> <effort|->");
            return;
        }
        ecs::AppComponent* app = app_component();
        if (app == nullptr || !app->client) {
            cmd.fail("expect_backend_tuning: no client");
            return;
        }
        const auto r = app->client->get_session(cmd.arg(0));
        std::string model = "-", effort = "-";
        if (r.ok) {
            if (r.value.model.model_pinned) model = r.value.model.requested;
            if (!r.value.model.requested_effort.empty()) effort = r.value.model.requested_effort;
        }
        if (r.ok && model == cmd.arg(1) && effort == cmd.arg(2)) {
            cmd.consume();
            return;
        }
        if (cmd.frames_alive < kGiveUpFrame) {
            cmd.retry();
            return;
        }
        cmd.fail(std::format("backend tuning for '{}' is model={} effort={}, expected {} {}",
                             cmd.arg(0), model, effort, cmd.arg(1), cmd.arg(2)));
    }
};

// expect_backend_compaction <id> <pending|none> [<sends>]: the backend's
// view -- whether the session's pending-compaction slot is filled (the
// authoritative "queued") -- and, optionally, how many compact commands
// reached it (the transport count). The two are asserted SEPARATELY on
// purpose: a send that reached the server is not a queued compaction.
struct HandleExpectBackendCompactionCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_backend_compaction")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("expect_backend_compaction requires <id> <pending|none> [<sends>]");
            return;
        }
        ecs::AppComponent* app = app_component();
        if (app == nullptr || !app->client) {
            cmd.fail("expect_backend_compaction: no client");
            return;
        }
        const auto r = app->client->get_session(cmd.arg(0));
        const std::string state =
            r.ok ? (r.value.pending_compaction ? "pending" : "none") : "unknown";
        const int sends = static_cast<int>(api::MockClient::compact_sends().size());
        const bool sendsOk = !cmd.has_args(3) || std::to_string(sends) == cmd.arg(2);
        if (r.ok && state == cmd.arg(1) && sendsOk) {
            cmd.consume();
            return;
        }
        if (cmd.frames_alive < kGiveUpFrame) {
            cmd.retry();
            return;
        }
        cmd.fail(std::format("backend compaction for '{}' is {} with {} send(s), expected {}{}",
                             cmd.arg(0), state, sends, cmd.arg(1),
                             cmd.has_args(3) ? " " + cmd.arg(2) : std::string()));
    }
};

// expect_compaction_keys_equal: every compact command the backend saw
// carried the SAME idempotency key (one gesture, retried), and at least one.
struct HandleExpectCompactionKeysEqualCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_compaction_keys_equal")) return;
        const auto& sends = api::MockClient::compact_sends();
        if (sends.empty()) {
            cmd.fail("expect_compaction_keys_equal: no compact command reached the backend");
            return;
        }
        for (const auto& r : sends) {
            if (r.idempotency_key.empty() || r.idempotency_key != sends.front().idempotency_key) {
                cmd.fail(std::format("compaction keys differ across {} send(s): '{}' vs '{}'",
                                     sends.size(), sends.front().idempotency_key,
                                     r.idempotency_key));
                return;
            }
        }
        cmd.consume();
    }
};

// dump_last_press: the pointer pipeline's decision on the last press frame
// (after HandleClicks) -- hot / active / focus winners by debug name, the
// press point, and every listener whose `down` was set. A rect containing
// the point is not dispatch; this is.
// release_compact_send: lets the mock's held compact send complete (pairs
// with HANABI_MOCK_COMPACT_SEND_HOLD=latch).
struct HandleReleaseCompactSendCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("release_compact_send")) return;
        api::MockClient::release_compact_send();
        cmd.consume();
    }
};

// dump_popover_flags: the app's popover state as the frame sees it.
struct HandleDumpPopoverFlagsCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("dump_popover_flags")) return;
        ecs::AppComponent* app = app_component();
        if (app == nullptr) {
            cmd.fail("dump_popover_flags: no app");
            return;
        }
        const std::string sid = app->pane().selectedId;
        const auto* g = app->compaction_gesture(sid);
        const bool slot = app->pane().openSession && app->pane().openSession->pending_compaction;
        std::printf("[E2E] dump_popover_flags: contextPopoverOpen=%d contextPopoverSession='%s' "
                    "detailsExpanded=%d composerPopoverPane=%d focusedPane=%d selected='%s' "
                    "modelPopoverOpen=%d escape=%d | compaction: request=%d inFlight=%s future=%d "
                    "gesture=%s paneSlot=%d transferQueued=%d\n",
                    app->contextPopoverOpen ? 1 : 0, app->contextPopoverSession.c_str(),
                    app->contextDetailsExpanded ? 1 : 0, app->composerPopoverPane,
                    app->focusedPane, sid.c_str(), app->modelPopoverOpen ? 1 : 0,
                    static_cast<int>(app->escape), app->requestCompaction ? 1 : 0,
                    app->compactionInFlight ? app->compactionInFlight->sessionId.c_str() : "-",
                    app->compactionFuture.valid() ? 1 : 0,
                    g ? std::to_string(static_cast<int>(g->outcome)).c_str() : "-", slot ? 1 : 0,
                    app->transfer && app->transfer->compactQueued.load() ? 1 : 0);
        cmd.consume();
    }
};

// Freshness of an imm entity: frames since it was last BUILT (0 = this
// frame). The library retains an unbuilt entity for ui_retire_grace_frames
// (90), rect and label intact, so a name match alone can be a ghost -- the
// parent's hypothesis for the two Context anomalies.
inline long e2e_frames_since_built(afterhours::EntityID id) {
    for (const auto& [_, record] : afterhours::ui::imm::existing_ui_elements)
        if (record.id == id)
            return static_cast<long>(afterhours::ui::imm::ui_build_frame) -
                   static_cast<long>(record.last_built_frame);
    return -1;  // not an imm-built element
}

// dump_text_owners <text>: every entity whose label CONTAINS the text, with
// id, freshness, rendered flag, hide flags and parent -- to tell a live
// label from a retained ghost when a text assertion reads the wrong one.
struct HandleDumpTextOwnersCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("dump_text_owners")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("dump_text_owners requires <text>");
            return;
        }
        const std::string needle = joined_args(cmd, 0);  // quotes stripped, spaces kept
        int found = 0;
        for (const auto& e :
             afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
            if (!e || !e->has<afterhours::ui::UIComponent>() ||
                !e->has<afterhours::ui::HasLabel>())
                continue;
            const std::string& label = e->get<afterhours::ui::HasLabel>().label;
            if (label.find(needle) == std::string::npos) continue;
            ++found;
            const auto& cmp = e->get<afterhours::ui::UIComponent>();
            const std::string name = e->has<afterhours::ui::UIComponentDebug>()
                                         ? e->get<afterhours::ui::UIComponentDebug>().name()
                                         : std::string("<unnamed>");
            std::printf("[E2E] dump_text_owners '%s': id=%d name=%s builtAgo=%ld rendered=%d "
                        "ShouldHide=%d cmp.should_hide=%d parent=%d rect=%.0f,%.0f %.0fx%.0f "
                        "label='%s'\n",
                        needle.c_str(), static_cast<int>(e->id), name.c_str(),
                        e2e_frames_since_built(e->id), cmp.was_rendered_to_screen ? 1 : 0,
                        e->has<afterhours::ui::ShouldHide>() ? 1 : 0, cmp.should_hide ? 1 : 0,
                        static_cast<int>(cmp.parent), cmp.rect().x, cmp.rect().y,
                        cmp.rect().width, cmp.rect().height, label.c_str());
        }
        if (found == 0) std::printf("[E2E] dump_text_owners '%s': none\n", needle.c_str());
        cmd.consume();
    }
};

// dump_focusable <name>: the five predicates HandleTabbing's can_be_focused
// applies to an entity -- listener, skip-when-tabbing, should-hide (entity
// and component), rendered, input allowed -- plus the components that
// decide them. The trace said the row never entered focused_ids; this says
// which predicate kept it out.
struct HandleDumpFocusableCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("dump_focusable")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("dump_focusable requires <name>");
            return;
        }
        auto* ctx = afterhours::EntityHelper::get_singleton_cmp<UIContext<InputAction>>();
        const std::string& name = cmd.arg(0);
        int found = 0;
        for (const auto& e :
             afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
            if (!e || !e->has<afterhours::ui::UIComponent>() ||
                !e->has<afterhours::ui::UIComponentDebug>())
                continue;
            if (e->get<afterhours::ui::UIComponentDebug>().name() != name) continue;
            ++found;
            const auto& cmp = e->get<afterhours::ui::UIComponent>();
            std::printf("[E2E] dump_focusable '%s' id=%d builtAgo=%ld hot=%d focus=%d "
                        "listener=%d drag=%d skipTab=%d "
                        "ShouldHide=%d cmp.should_hide=%d rendered=%d allowed=%d "
                        "inCluster=%d labelDisabled=%d parent=%d layer=%d\n",
                        name.c_str(), static_cast<int>(e->id), e2e_frames_since_built(e->id),
                        ctx && ctx->hot_id == e->id ? 1 : 0,
                        ctx && ctx->focus_id == e->id ? 1 : 0,
                        e->has<afterhours::ui::HasClickListener>() ? 1 : 0,
                        e->has<afterhours::ui::HasDragListener>() ? 1 : 0,
                        e->has<afterhours::ui::SkipWhenTabbing>() ? 1 : 0,
                        e->has<afterhours::ui::ShouldHide>() ? 1 : 0, cmp.should_hide ? 1 : 0,
                        cmp.was_rendered_to_screen ? 1 : 0,
                        ctx && ctx->is_input_allowed(e->id) ? 1 : 0,
                        e->has<afterhours::ui::InFocusCluster>() ? 1 : 0,
                        e->has<afterhours::ui::HasLabel>() &&
                                e->get<afterhours::ui::HasLabel>().is_disabled
                            ? 1
                            : 0,
                        static_cast<int>(cmp.parent), cmp.render_layer);
        }
        if (found == 0) std::printf("[E2E] dump_focusable '%s': no such entity\n", name.c_str());
        cmd.consume();
    }
};

struct HandleDumpLastPressCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("dump_last_press")) return;
        const ecs::LastPress& lp = ecs::last_press();
        if (!lp.seen) {
            std::printf("[E2E] dump_last_press: no press seen yet\n");
        } else {
            std::printf("[E2E] dump_last_press: frame=%d at=%.0f,%.0f hot=%s(%d) active=%s(%d) "
                        "focus=%s(%d) down=[%s]\n",
                        lp.frame, lp.x, lp.y, lp.hotName.c_str(), static_cast<int>(lp.hot),
                        lp.activeName.c_str(), static_cast<int>(lp.active),
                        lp.focusName.c_str(), static_cast<int>(lp.focus), lp.downNames.c_str());
        }
        cmd.consume();
    }
};

struct HandleExpectOutboxRetryCountCommand
    : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&,
                       afterhours::testing::PendingE2ECommand& cmd,
                       float) override {
        if (cmd.is_consumed() || !cmd.is("expect_outbox_retry_count")) return;
        if (!cmd.has_args(2)) {
            cmd.fail("expect_outbox_retry_count requires <id> <n>");
            return;
        }
        ecs::AppComponent* app = app_component();
        if (app == nullptr) {
            cmd.fail("expect_outbox_retry_count: no app");
            return;
        }
        const auto have = app->outboxRetry.count_for(cmd.arg(0));
        const auto want = static_cast<std::size_t>(std::atoi(cmd.arg(1).c_str()));
        if (have == want) {
            cmd.consume();
            return;
        }
        // A retry that WOULD be scheduled is scheduled on the loader's tick
        // that consumes the request; give it the same grace every other
        // assertion gets before calling the count wrong.
        if (cmd.frames_alive < kGiveUpFrame) {
            cmd.retry();
            return;
        }
        cmd.fail(std::format("retry queue holds {} for '{}', expected {}", have,
                             cmd.arg(0), want));
    }
};

inline void register_hanabi_commands(afterhours::SystemManager& sm) {
    sm.register_update_system(std::make_unique<HandleWatchInkCommand>());
    sm.register_update_system(std::make_unique<HandleExpectLatencyCommand>());
    sm.register_update_system(std::make_unique<HandleRequireThreadCommand>());
    sm.register_update_system(
        std::make_unique<HandleExpectNotFocusedCommand>());
    sm.register_update_system(std::make_unique<HandleClickLinkCommand>());
    sm.register_update_system(std::make_unique<HandleExpectPanesCommand>());
    sm.register_update_system(std::make_unique<HandleResetClipboardCommand>());
    sm.register_update_system(std::make_unique<HandleExpectClipboardCommand>());
    sm.register_update_system(std::make_unique<HandleExpectOutboxCommand>());
    sm.register_update_system(std::make_unique<HandleSeedCacheCommand>());
    sm.register_update_system(std::make_unique<HandleForceSendCommand>());
    sm.register_update_system(std::make_unique<HandleSeedOutboxCommand>());
    sm.register_update_system(std::make_unique<HandleOutboxRestoreAgainCommand>());
    sm.register_update_system(std::make_unique<HandleReloadSettingsCommand>());
    sm.register_update_system(std::make_unique<HandleExpectToastCommand>());
    sm.register_update_system(std::make_unique<HandleExpectOutboxAttachmentCommand>());
    sm.register_update_system(std::make_unique<HandleExpectOutboxRetryCountCommand>());
    sm.register_update_system(std::make_unique<HandleExpectBackendTuningCommand>());
    sm.register_update_system(std::make_unique<HandleExpectBackendCompactionCommand>());
    sm.register_update_system(std::make_unique<HandleExpectCompactionKeysEqualCommand>());
    sm.register_update_system(std::make_unique<HandleDumpLastPressCommand>());
    sm.register_update_system(std::make_unique<HandleReleaseCompactSendCommand>());
    sm.register_update_system(std::make_unique<HandleDumpPopoverFlagsCommand>());
    sm.register_update_system(std::make_unique<HandleDumpFocusableCommand>());
    sm.register_update_system(std::make_unique<HandleDumpTextOwnersCommand>());
    sm.register_update_system(std::make_unique<HandleExpectSavedViewsCommand>());
    sm.register_update_system(std::make_unique<HandleExpectFontFaceCommand>());
    sm.register_update_system(std::make_unique<HandleExpectMockOutboundCallsCommand>());
    sm.register_update_system(std::make_unique<HandleExpectCacheWipedCommand>());
    sm.register_update_system(std::make_unique<HandleSeedReplyDraftCommand>());
    sm.register_update_system(std::make_unique<HandleExpectReplyDraftCommand>());
    sm.register_update_system(std::make_unique<HandleSubmitThenFocusCommand>());
    sm.register_update_system(std::make_unique<HandleSubmitThenRetargetCommand>());
    sm.register_update_system(std::make_unique<HandleReleaseCompactionCommand>());
    sm.register_update_system(std::make_unique<HandleKickoffThenFocusCommand>());
    sm.register_update_system(std::make_unique<HandleExpectBackendMessageCommand>());
    sm.register_update_system(std::make_unique<HandleExpectUploadCancelledCommand>());
    sm.register_update_system(std::make_unique<HandleExpectBackendStateCommand>());
    sm.register_update_system(std::make_unique<HandleExpectNoUiCommand>());
    sm.register_update_system(std::make_unique<HandleExpectOpenCommand>());
    sm.register_update_system(std::make_unique<HandleNativeModeCommand>());
    sm.register_update_system(std::make_unique<HandleNativeMouseCommand>());
    sm.register_update_system(std::make_unique<HandleNativeClickTargetCommand>());
    sm.register_update_system(std::make_unique<HandleNativeDragResizeCommand>());
    sm.register_update_system(std::make_unique<HandleNativeKeyCommand>());
    sm.register_update_system(std::make_unique<HandleExpectContentSizeCommand>());
    sm.register_update_system(std::make_unique<HandleCaptureReceiptCommand>());
    sm.register_update_system(std::make_unique<HandleCaptureMarkerCommand>());
    sm.register_update_system(std::make_unique<HandleExpectUiRowsJoinCommand>());
    sm.register_update_system(std::make_unique<HandleExpectUiRelCommand>());
    sm.register_update_system(std::make_unique<HandleDoubleClickWordCommand>());
    sm.register_update_system(std::make_unique<HandleHoverUICommand>());
    sm.register_update_system(std::make_unique<HandleMouseDownUICommand>());
    sm.register_update_system(
        std::make_unique<HandleExpectChildrenInsideCommand>());
    sm.register_update_system(std::make_unique<HandleExpectA11yCommand>());
    sm.register_update_system(std::make_unique<HandleExpectNoA11yCommand>());
    sm.register_update_system(std::make_unique<HandleA11yPressCommand>());
    sm.register_update_system(
        std::make_unique<HandleExpectA11yPressRefusedCommand>());
    sm.register_update_system(std::make_unique<HandleExpectHitTargetsCommand>());
    sm.register_update_system(std::make_unique<HandleExpectTooltipsCommand>());
}

}  // namespace hanabi::e2e
