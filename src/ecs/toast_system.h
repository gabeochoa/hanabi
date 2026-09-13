#pragma once

// The toast: one transient bar at the bottom of the window carrying a message
// and, optionally, a single action.
//
// Archive, star and mute are what raise it today. Each is easy to do by
// accident and none of them announces itself where the user is looking —
// archive files the thread into a view nobody is watching, and a mute is
// invisible until the notification it swallowed never arrives — so the action
// has to be takeable back without going to find it. The bar counts itself down
// and disappears; Undo re-runs the toggle named by the toast's undo kind, which
// is why undoing an undo is simply doing the thing again.

#include <string>

#include <afterhours/src/plugins/clipboard.h>
#include "../api/disk_cache.h"
#include "../settings.h"
#include "../ui/secondary_surface.h"
#include "../ui/icons.h"
#include "ui_imports.h"

namespace ecs {

struct ToastSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float dt) override {
        auto* app = find_singleton<AppComponent>();
        if (!app || app->toastMessage.empty()) return;

        if (!app->toastHolds) {
            app->toastSecondsLeft -= dt;
            if (app->toastSecondsLeft <= 0.0f) {
                app->dismiss_toast();
                return;
            }
        }

        Entity& uiRoot = ui_imm::getUIRootEntity();
        const float sw =
            hanabi::viewport::width();
        const float sh =
            hanabi::viewport::height();

        const bool copyable = app->toastUndoKind == AppComponent::ToastUndo::CopyText;
        const bool undoable = !app->toastUndoSessionId.empty();
        const float barW = hanabi::surface::toast_width(
            sw, app->toastMessage.size(), undoable || copyable);
        const float barH = 50.0f;
        const auto* layout = find_singleton<LayoutComponent>();
        const float desiredY = layout == nullptr
            ? sh - barH - 44.0f
            : layout->composer.y - barH - 12.0f;
        const float y =
            std::clamp(desiredY, 12.0f, std::max(12.0f, sh - barH - 12.0f));
        const float actionW = undoable ? 76.0f : 0.0f;
        const float closeW = 28.0f;
        const float messageW =
            std::max(72.0f, barW - actionW - closeW - 28.0f);

        auto barConfig = hanabi::surface::menu(barW, barH, 20);
        barConfig.with_absolute_position()
            .with_translate((sw - barW) * 0.5f, y)
            .with_flex_direction(FlexDirection::Row)
            .with_align_items(AlignItems::Center)
            .with_padding(Padding{.top = pixels(8), .left = pixels(12),
                                  .bottom = pixels(8), .right = pixels(8)})
            .with_debug_name("toast");
        auto bar = div(ctx, mk(uiRoot, 8300), barConfig);

        div(ctx, mk(bar.ent(), 1),
            ComponentConfig{}
                .with_label(app->toastMessage)
                .with_size(ComponentSize{pixels(messageW), pixels(28)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::ROW)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("toast_message"));

        if (copyable) {
            // The one affordance a blocked-unsent message gets: take the
            // words back. No retry -- there is no thread to retry to -- and
            // no destination invented for it.
            auto copy = button(ctx, mk(bar.ent(), 2),
                hanabi::surface::action_button(68.0f, false, 21)
                    .with_label(app->toastCopyHasFiles ? "Copy text" : "Copy")
                    .with_margin(Margin{.left = pixels(8)})
                    .with_font_size(theme::type::ROW)
                    .with_justify_content(JustifyContent::Center)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_debug_name("toast_copy"));
            if (copy) {
                afterhours::clipboard::set_text(app->toastCopyText);
                // Nothing is removed here, ever. Copy takes the words and
                // the NAMES of any files that were staged -- not the files
                // themselves -- so retiring the record would trade a
                // recovered paragraph for lost bytes. Copying is also not
                // being DONE with it: the record stays unseen and is raised
                // again next launch. The x below is the explicit "quiet
                // this one".
                // Copy acknowledges this record as well, so the notice it
                // just closed cannot be re-selected on the same frame, and
                // the next unacknowledged one takes its place. The record
                // itself stays: acknowledgement is about the NOTICE, never
                // about the words or the files.
                app->note_blocked_seen(app->toastCopyLocalId);
                app->dismiss_toast();
                app->requestNextBlockedNotice = true;
            }
        } else if (undoable) {
            auto undo = button(ctx, mk(bar.ent(), 2),
                hanabi::surface::action_button(68.0f, false, 21)
                    .with_label("Undo")
                    .with_margin(Margin{.left = pixels(8)})
                    .with_font_size(theme::type::ROW)
                    .with_justify_content(JustifyContent::Center)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_debug_name("toast_undo"));
            if (undo) {
                // Re-run the SAME toggle the action ran, chosen by the kind the
                // toast carries — the bar used to know only how to unarchive,
                // which would have archived a thread whose mute you undid.
                // Every toggle writes Settings on its way through, so the undo
                // restores the durable state as well as the on-screen one, and
                // undoing an undo is simply doing the thing again.
                switch (app->toastUndoKind) {
                    // Not reachable: a CopyText toast takes the branch above
                    // and never has an undo session. Named so the switch
                    // stays exhaustive.
                    case AppComponent::ToastUndo::CopyText:
                        break;
                    case AppComponent::ToastUndo::Archive:
                        app->requestToggleArchive = app->toastUndoSessionId;
                        break;
                    case AppComponent::ToastUndo::Mute:
                        app->requestToggleMute = app->toastUndoSessionId;
                        break;
                    case AppComponent::ToastUndo::Star:
                        app->requestToggleStar = app->toastUndoSessionId;
                        break;
                    case AppComponent::ToastUndo::None:
                        break;
                }
                // The toggle raises its own toast next frame, saying what the
                // undo did; leaving this one up would stack two.
                app->dismiss_toast();
                return;
            }
        }

        auto close = button(ctx, mk(bar.ent(), 3),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(closeW), pixels(34)})
                .with_margin(Margin{.left = pixels(4)})
                .with_transparent_bg()
                .with_custom_hover_bg(theme::hover_over(theme::panel_bg()))
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_click_activation(ClickActivationMode::Press)
                .with_corner_radius(hanabi::surface::kControlCorner)
                .with_render_layer(21)
                .with_on_draw_fg(hanabi::icons::draw_fg(
                    "close", "\xc3\x97", theme::text_faint(), 12.0f))
                .with_debug_name("toast_close"));
        if (close) {
            // Dismissing a blocked-unsent notice means "seen and quiet", not
            // "delete": the record stays on disk, it is simply not raised at
            // the person again, and the next unseen one takes its place.
            if (copyable) {
                // The × is the explicit "I am done with this one": the
                // record STAYS on disk, it is marked seen so it is not
                // raised again, and the next unseen one takes its place.
                app->note_blocked_seen(app->toastCopyLocalId);
                app->dismiss_toast();
                app->requestNextBlockedNotice = true;
            } else {
                app->dismiss_toast();
            }
        }
    }
};

}  // namespace ecs
