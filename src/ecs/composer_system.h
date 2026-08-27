#pragma once

// Composer (Phase K). Renders a centered "New task" compose sheet over a dimmed
// full-window backdrop when AppComponent::composerOpen is true. The sheet has a
// text input bound to AppComponent::composerDraft and a Start affordance.
//
// Cmd+N toggles it; Esc / the ✕ / clicking the backdrop closes it.
//
// KICKOFF (Phase SEND): on Start with text, the composer sets the one-shot
// app.requestKickoffPrompt flag, clears the draft, and closes. LoaderSystem
// services it (create_session async), then refreshes the list and opens the
// new thread. The mock creates an in-memory session; the http adapter POSTs to
// the configured chat path. (client.h/mock_client.h are shared — not edited
// here.)
//
// Owns this file only. The + button that would toggle composerOpen lives in
// sidebar_system.h (owned by another agent); until that one-line hook lands,
// Cmd+N opens/closes this overlay.

#include <string>

#include "../keys.h"
#include "ui_imports.h"

#include "../api/disk_cache.h"
#include "../ui/icons.h"
#include "../ui/secondary_surface.h"
#include "keyboard_focus.h"

namespace ecs {

struct ComposerSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* app = find_singleton<AppComponent>();
        if (!app) return;

        // Cmd+N toggles the composer (343/347 = super, 78 = KEY_N).
        bool cmdDown = hanabi::keys::cmd_down();
        if (cmdDown && hanabi::keys::pressed(hanabi::keys::kN)) {
            app->composerOpen = !app->composerOpen;
        }

        // CRASH-SAFE DRAFT RESTORE (local-first): on the frame the composer
        // OPENS, restore whatever the user was mid-typing before a crash/quit.
        // The "New task" composer has no session yet, so its draft persists
        // under the stable "new" key (disk_cache::new_draft_key()). The backend
        // namespace is already set (main.cpp set_namespace before the loop), so
        // this reads from the correct per-backend local cache dir — never the
        // network. We only overwrite the live draft when disk has something AND
        // the field is currently empty, so a restore never clobbers text the
        // user just started typing this session.
        const bool justOpened = app->composerOpen && !wasOpen_;
        if (justOpened) {
            std::string saved =
                api::disk_cache::load_draft(api::disk_cache::new_draft_key());
            if (!saved.empty() && app->composerDraft.empty())
                app->composerDraft = saved;
            lastPersisted_ = app->composerDraft;
        }
        wasOpen_ = app->composerOpen;

        if (!app->composerOpen) return;
        if (justOpened) focusFrames_ = 3;

        // Esc closes (escape_system.h decides which overlay it belongs to).
        if (app->escape == EscapeIntent::CloseComposer) {
            app->composerOpen = false;
            return;
        }

        Entity& uiRoot = ui_imm::getUIRootEntity();
        const float sw =
            hanabi::viewport::width();
        const float sh =
            hanabi::viewport::height();

        // Dimmed full-window backdrop (pre-blended, gap #13). Click = close.
        auto backdrop = button(
            ctx, mk(uiRoot, 8100),
            hanabi::surface::scrim(sw, sh, 10)
                .with_debug_name("composer_backdrop"));
        if (backdrop) {
            app->composerOpen = false;
            return;
        }

        const auto panelRect =
            hanabi::surface::centered(sw, sh, 440.0f, 216.0f);
        auto panel = div(
            ctx, mk(uiRoot, 8110),
            hanabi::surface::sheet(panelRect, 11)
                .with_debug_name("composer_panel"));

        render_header(ctx, panel.ent(), *app);
        render_input(ctx, panel.ent(), *app);
        render_actions(ctx, panel.ent(), *app);

        // CRASH-SAFE DRAFT PERSIST (local-first): after the input widget has
        // applied this frame's keystrokes into app->composerDraft, write the
        // draft to the local cache dir whenever it changed. Drafts are tiny, so
        // a synchronous atomic JSON rewrite per change is fine (see disk_cache
        // save_draft) and no debounce is needed. render_actions() may have
        // consumed the draft on Start (clearing it + closing) — that path calls
        // clear_draft() itself, and here composerDraft is already empty so we
        // don't rewrite a stale value. Only persist while still open.
        if (app->composerOpen && app->composerDraft != lastPersisted_) {
            api::disk_cache::save_draft(api::disk_cache::new_draft_key(),
                                        app->composerDraft);
            lastPersisted_ = app->composerDraft;
        }
    }

  private:
    void render_header(UIContext<InputAction>& ctx, Entity& parent,
                       AppComponent& app) {
        auto header = div(ctx, mk(parent, 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(hanabi::surface::kHeaderH)})
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("composer_header"));
        auto titleRow = div(ctx, mk(header.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(hanabi::surface::kTitleH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("composer_title_row"));
        div(ctx, mk(titleRow.ent(), 1),
            ComponentConfig{}
                .with_label("New task")
                .with_size(ComponentSize{pixels(332), pixels(24)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(FontSize::Large)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("composer_title"));
        auto closeBtn = button(ctx, mk(titleRow.ent(), 2),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(26), pixels(26)})
                .with_margin(Margin{.left = pixels(8)})
                .with_custom_background(theme::panel_bg())
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg()))
                .with_click_activation(ClickActivationMode::Press)
                .with_corner_radius(hanabi::surface::kControlCorner)
                .with_on_draw_fg(hanabi::icons::draw_fg(
                    "close", "\xc3\x97", theme::text_secondary(), 14.0f))
                .with_debug_name("composer_close"));
        div(ctx, mk(header.ent(), 2),
            ComponentConfig{}
                .with_label("Start a focused conversation with your agent")
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(hanabi::surface::kSubtitleH)})
                .with_margin(Margin{.top = pixels(4)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("composer_subtitle"));
        if (closeBtn) app.composerOpen = false;
    }

    void render_input(UIContext<InputAction>& ctx, Entity& parent,
                      AppComponent& app) {
        // Bound text input for the new-task description. The imm text_input
        // widget writes edits straight into app.composerDraft.
        //
        // NOTE (afterhours_gaps.md #17): the text_input widget derives its font
        // size from the field HEIGHT (field_h * 0.5f) and forces its own
        // Theme::Usage::Secondary background — it ignores with_font_size /
        // with_custom_background. So the field is kept single-line-height (~34)
        // to yield a ~17px readable font, and its inner surface is themed by
        // ctx.theme (not our tokens). A caption above labels it since the
        // widget also ignores placeholder styling here.
        div(ctx, mk(parent, 1),
            ComponentConfig{}
                .with_label("Describe the task")
                .with_size(ComponentSize{percent(1.0f), pixels(18)})
                .with_margin(Margin{.top = pixels(12)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(FontSize::Small)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("composer_caption"));

        auto field = afterhours::ui::imm::text_input(
            ctx, mk(parent, 2), app.composerDraft,
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(hanabi::surface::kFieldH)})
                .with_margin(Margin{.top = pixels(6)})
                .with_custom_background(theme::panel_bg_2())
                .with_border(theme::border(), pixels(1.0f))
                .with_custom_text_color(theme::text_primary())
                .with_alignment(TextAlignment::Left)
                .with_corner_radius(hanabi::surface::kControlCorner)
                .with_debug_name("composer_input"));
        if (focusFrames_ > 0) {
            --focusFrames_;
            ctx.set_focus(focusable_field(field.ent()));
        }
    }

    void render_actions(UIContext<InputAction>& ctx, Entity& parent,
                        AppComponent& app) {
        auto row = div(ctx, mk(parent, 3),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(48)})
                .with_padding(Padding{.top = pixels(14)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::FlexEnd)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("composer_actions"));

        bool hasText = !app.composerDraft.empty();

        auto cancel = button(ctx, mk(row.ent(), 1),
            hanabi::surface::action_button(92.0f, false, 11)
                .with_label("Cancel")
                .with_margin(Margin{.right = pixels(8)})
                .with_font_size(FontSize::Medium)
                .with_justify_content(JustifyContent::Center)
                .with_debug_name("composer_cancel"));
        if (cancel) {
            app.composerOpen = false;
            return;
        }

        auto start = button(ctx, mk(row.ent(), 2),
            hanabi::surface::action_button(92.0f, hasText, 11)
                .with_label("Start")
                .with_custom_background(hasText ? theme::button_primary()
                                                : theme::disabled_bg())
                .with_custom_hover_bg(hasText ? theme::hover_over(theme::button_primary())
                                              : theme::disabled_bg())
                .with_custom_text_color(hasText ? theme::window_bg()
                                                : theme::disabled_text())
                .with_font_size(FontSize::Medium)
                .with_justify_content(JustifyContent::Center)
                .with_debug_name("composer_start"));
        if (start && hasText) {
            // Kick off a new session: hand the draft to the loader via the
            // one-shot requestKickoffPrompt flag (LoaderSystem runs
            // create_session async, then refreshes the list + opens the new
            // thread). Clear the draft and close the overlay.
            app.requestKickoffPrompt = app.composerDraft;
            app.composerDraft.clear();
            // The draft is now in flight as a real session — drop its crash-safe
            // local copy so a relaunch doesn't restore an already-sent prompt.
            api::disk_cache::clear_draft(api::disk_cache::new_draft_key());
            lastPersisted_.clear();
            app.composerOpen = false;
        }
    }

    // Tracks the composer's open state across frames so we can detect the
    // false->true edge and restore the persisted draft exactly once on open.
    bool wasOpen_ = false;
    int focusFrames_ = 0;
    // The draft value last written to disk, so we only persist on change
    // (avoids rewriting drafts.json on every idle frame).
    std::string lastPersisted_;
};

}  // namespace ecs
