#pragma once

// Help > Report a Bug (the reference's 0.8.9 bug report): a sheet that files
// a Knots issue as the person at the keyboard, with a capture of the window
// taken as it opened (bug_report_open.h).
//
// What it shows is what gets sent: the text box (line 1 the title, the rest
// the body), which board -- this app's or the backend's, each saying what it
// is for -- whether the capture rides along, and the exact context lines the
// issue will carry. Nothing is attached invisibly. Filing runs off the frame
// (api/knots_runner.h, the fake runner in every scripted run); a refusal
// keeps the sheet and every word in it, a success closes it with a Copy for
// the knot's link.

#include <chrono>
#include <cstdlib>
#include <future>
#include <string>

#include "../api/knots_runner.h"
#include "../ui/overlay_lifecycle.h"
#include "../ui/secondary_surface.h"
#include "../version.h"
#include "bug_report_open.h"
#include "components.h"
#include "knots_system.h"
#include "ui_imports.h"

namespace ecs {

struct BugReportSystem : afterhours::System<UIContext<InputAction>> {
    static constexpr float kPanelW = 620.0f;
    static constexpr float kPanelH = 470.0f;

    static api::knots::Context context_for(const AppComponent& app) {
        api::knots::Context c;
        c.version = hanabi::kVersion;
        c.os = macos_version();
        c.surface = app.bugReportSurface;
        if (const char* u = std::getenv("USER")) c.reporter = u;
        return c;
    }

    static void close(AppComponent& app) {
        app.showBugReport = false;
        app.bugReportError.clear();
    }

    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        using namespace std::chrono_literals;
        auto* app = find_singleton<AppComponent>();
        if (app == nullptr) return;
        // Land a filing even if the sheet was closed meanwhile.
        if (app->bugReportPending && app->bugReportFuture.valid() &&
            app->bugReportFuture.wait_for(0s) == std::future_status::ready) {
            auto r = app->bugReportFuture.get();
            app->bugReportPending = false;
            if (r.ok) {
                app->lastKnotUrl = r.value.url;
                app->bugReportText.clear();
                app->showBugReport = false;
                app->raise_copy_toast("Filed " + r.value.id, r.value.url);
            } else {
                app->bugReportError = r.error;
            }
        }
        if (!app->showBugReport) return;
        if (app->escape == EscapeIntent::CloseBugReport) {
            close(*app);
            return;
        }

        ctx.theme.background = theme::panel_bg();
        Entity& uiRoot = ui_imm::getUIRootEntity();
        const auto rect = hanabi::surface::centered(hanabi::viewport::width(),
                                                    hanabi::viewport::height(), kPanelW, kPanelH);
        const float w = rect.width - hanabi::surface::kSheetPadH * 2.0f;
        auto backdrop = button(ctx, mk(uiRoot, 8400),
            hanabi::surface::scrim(hanabi::viewport::width(), hanabi::viewport::height(), 10)
                .with_debug_name("bug_report_backdrop"));
        if (hanabi::overlay::dismisses(static_cast<bool>(backdrop), ctx.mouse.pos.x,
                                       ctx.mouse.pos.y, rect)) {
            close(*app);
            return;
        }
        auto panel = div(ctx, mk(uiRoot, 8410),
                         hanabi::surface::sheet(rect, 11).with_debug_name("bug_report_panel"));

        const auto text = [&](Entity& at, int id, const std::string& words, float h,
                              theme::Color ink, float size, const std::string& dbg) {
            div(ctx, mk(at, id),
                ComponentConfig{}
                    .with_label(words)
                    .with_size(ComponentSize{pixels(w), pixels(h)})
                    .with_transparent_bg()
                    .with_custom_text_color(ink)
                    .with_font_size(size)
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(TextOverflow::Ellipsis)
                    .with_roundness(0.0f)
                    .with_render_layer(11)
                    .with_debug_name(dbg));
        };
        text(panel.ent(), 1, "Report a bug", 30.0f, theme::text_primary(), theme::type::H1,
             "bug_report_title");
        text(panel.ent(), 2, "First line is the title; anything after it is the detail.", 20.0f,
             theme::text_secondary(), theme::type::SM, "bug_report_hint");

        auto box = div(ctx, mk(panel.ent(), 3),
            hanabi::surface::field(w, 11)
                .with_size(ComponentSize{pixels(w), pixels(150)})
                .with_border(theme::border_raised(), pixels(1.0f))
                // The text area paints its own ground (Theme Secondary, not
                // its config -- afterhours_gaps.md #262), so the box matches
                // it and leaves room for its hairline.
                .with_custom_background(theme::panel_bg())
                .with_padding(Padding{.top = pixels(6), .right = pixels(8),
                                      .bottom = pixels(6), .left = pixels(8)})
                .with_render_layer(11)
                .with_debug_name("bug_report_text_wrap"));
        afterhours::ui::imm::text_area(
            ctx, mk(box.ent(), 1), app->bugReportText,
            ComponentConfig{}
                .with_line_height(pixels(18.0f))
                .with_max_lines(200)
                .with_submit_on_enter(false)
                .with_size(ComponentSize{percent(1.0f), percent(1.0f)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_disabled(app->bugReportPending)
                .with_render_layer(11)
                .with_debug_name("bug_report_text"));

        // Which board: named by what it is for, not by namespace.
        auto boards = div(ctx, mk(panel.ent(), 4),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), pixels(34)})
                .with_margin(Margin{.top = pixels(8)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_render_layer(11)
                .with_debug_name("bug_report_boards"));
        const auto choice = [&](int id, const char* label, bool on, const std::string& dbg) {
            return button(ctx, mk(boards.ent(), id),
                ComponentConfig{}
                    .with_label(label)
                    .with_size(ComponentSize{pixels(220), pixels(28)})
                    .with_margin(Margin{.right = pixels(8)})
                    .with_custom_background(on ? theme::accent() : theme::panel_bg_2())
                    .with_custom_text_color(on ? theme::Color{255, 255, 255, 255}
                                               : theme::text_primary())
                    .with_border(theme::border_raised(), pixels(1.0f))
                    .with_font_size(theme::type::SM)
                    .with_corner_radius(6.0f)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_render_layer(11)
                    .with_debug_name(dbg));
        };
        if (choice(1, "Hanabi (this app)", !app->bugReportBackend, "bug_report_board_app"))
            app->bugReportBackend = false;
        if (choice(2, "Agentcloud (the backend)", app->bugReportBackend,
                   "bug_report_board_backend"))
            app->bugReportBackend = true;
        const std::string ns = api::knots::namespace_for(
            app->bugReportBackend ? api::knots::Board::Backend : api::knots::Board::App);
        text(panel.ent(), 5,
             (app->bugReportBackend
                  ? std::string("Something the server did \xe2\x80\x94 a session, a run, the wire. ")
                  : std::string("Something this app drew, typed, froze on or got wrong. ")) +
                 "Files to " + ns + ".",
             20.0f, theme::text_secondary(), theme::type::SM, "bug_report_board_note");

        auto attach = button(ctx, mk(panel.ent(), 6),
            ComponentConfig{}
                .with_label(app->bugReportCapture.empty()
                                ? std::string("Window capture: none was taken")
                                : (app->bugReportAttach
                                       ? std::string("Window capture: attached \xe2\x80\x94 click to leave it out")
                                       : std::string("Window capture: left out \xe2\x80\x94 click to attach")))
                .with_size(ComponentSize{pixels(w), pixels(28)})
                .with_margin(Margin{.top = pixels(6)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_disabled(app->bugReportCapture.empty())
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_render_layer(11)
                .with_debug_name("bug_report_attach"));
        if (attach) app->bugReportAttach = !app->bugReportAttach;

        // The exact lines the issue will carry.
        text(panel.ent(), 7, "Sent with the report:", 20.0f, theme::text_secondary(),
             theme::type::SM, "bug_report_context_label");
        int id = 20;
        for (const auto& line : api::knots::context_lines(context_for(*app))) {
            text(panel.ent(), id, line, 18.0f, theme::text_faint(), theme::type::SM,
                 "bug_report_context_" + std::to_string(id - 20));
            ++id;
        }

        text(panel.ent(), 40,
             !app->bugReportError.empty()
                 ? app->bugReportError
                 : (app->bugReportPending ? std::string("Filing\xe2\x80\xa6") : std::string()),
             20.0f, app->bugReportError.empty() ? theme::text_secondary() : theme::destructive(),
             theme::type::SM, "bug_report_status");

        auto actions = div(ctx, mk(panel.ent(), 41),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), pixels(34)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_justify_content(JustifyContent::FlexEnd)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_render_layer(11)
                .with_debug_name("bug_report_actions"));
        auto cancel = button(ctx, mk(actions.ent(), 1),
            ComponentConfig{}
                .with_label("Cancel")
                .with_size(ComponentSize{pixels(90), pixels(28)})
                .with_margin(Margin{.right = pixels(8)})
                .with_transparent_bg()
                .with_border(theme::border_raised(), pixels(1.0f))
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::SM)
                .with_corner_radius(6.0f)
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_render_layer(11)
                .with_debug_name("bug_report_cancel"));
        if (cancel) {
            close(*app);
            return;
        }
        const bool canSend = !app->bugReportPending &&
                             api::knots::capture(app->bugReportText).has_value();
        auto send = button(ctx, mk(actions.ent(), 2),
            ComponentConfig{}
                .with_label("Send report")
                .with_size(ComponentSize{pixels(120), pixels(28)})
                .with_custom_background(canSend ? theme::accent() : theme::panel_bg_2())
                .with_custom_text_color(canSend ? theme::Color{255, 255, 255, 255}
                                                : theme::text_faint())
                .with_font_size(theme::type::SM)
                .with_corner_radius(6.0f)
                .with_disabled(!canSend)
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_render_layer(11)
                .with_debug_name("bug_report_send"));
        if (send && canSend) {
            app->bugReportError.clear();
            app->bugReportPending = true;
            const std::string text = app->bugReportText;
            const auto ctxv = context_for(*app);
            const auto board =
                app->bugReportBackend ? api::knots::Board::Backend : api::knots::Board::App;
            const std::string capture = app->bugReportAttach ? app->bugReportCapture : "";
            app->bugReportFuture = std::async(std::launch::async, [text, ctxv, board, capture] {
                return api::knots::file(text, ctxv, board, capture);
            });
        }
    }
};

}  // namespace ecs
