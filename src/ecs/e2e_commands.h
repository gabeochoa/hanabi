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
#include <cstdlib>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "../../vendor/afterhours/src/plugins/clipboard.h"
#include "../api/disk_cache.h"
#include "../test_hooks.h"
#include "../ui/link_detect.h"
#include "../ui_context.h"
#include "../util/clipboard.h"
#include "../util/gfx_resize.h"
#include "../util/latency.h"
#include "components.h"
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
        app->requestKickoff = ecs::model::snapshot_outgoing(
            state, text, target, app->client->supports_attachments());
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
        "triple_click",   "right_click",
        "right_click_ui", "right_click_text",
        "middle_click",   "key",
        "type",           "scroll_wheel",
        "focus_ui",       "toggle_checkbox",
        "drag",           "drag_to",
        "mouse_down",     "mouse_up",
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
                "<ui|ui_gone|text|text_gone|focus|blur> <target>");
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

// Registered BEFORE afterhours' builtins, so the resize handler above sees
// `resize` first. Everything else hanabi owns goes in the function below.
inline void register_hanabi_pre_handlers(afterhours::SystemManager& sm) {
    sm.register_update_system(std::make_unique<HandleResizeDeferredCommand>());
    sm.register_update_system(
        std::make_unique<HandleExpectResizesAppliedCommand>());
    sm.register_update_system(std::make_unique<LatencyInputEventSystem>());
}

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
    sm.register_update_system(std::make_unique<HandleExpectCacheWipedCommand>());
    sm.register_update_system(std::make_unique<HandleSeedReplyDraftCommand>());
    sm.register_update_system(std::make_unique<HandleExpectReplyDraftCommand>());
    sm.register_update_system(std::make_unique<HandleSubmitThenFocusCommand>());
    sm.register_update_system(std::make_unique<HandleSubmitThenRetargetCommand>());
    sm.register_update_system(std::make_unique<HandleKickoffThenFocusCommand>());
    sm.register_update_system(std::make_unique<HandleExpectBackendMessageCommand>());
    sm.register_update_system(std::make_unique<HandleExpectUploadCancelledCommand>());
}

}  // namespace hanabi::e2e
