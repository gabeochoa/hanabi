#pragma once

#include <algorithm>
#include <string>

#include "../ui/inline_image.h"
#include "../ui/secondary_surface.h"
#include "components.h"
#include "ui_imports.h"

namespace ecs {

struct ArtifactViewerSystem : afterhours::System<UIContext<InputAction>> {
    static constexpr float kMargin = 48.0f;
    static constexpr float kCaptionH = 24.0f;

    static void close(AppComponent& app) {
        app.viewerImagePath.clear();
        app.viewerImageName.clear();
    }

    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* app = find_singleton<AppComponent>();
        if (!app || app->viewerImagePath.empty()) return;
        if (app->escape == EscapeIntent::CloseArtifactViewer ||
            app->paletteOpen || app->sessionSearchOpen || app->renameOpen ||
            app->showShortcuts || app->showAuth || !app->requestOpenTab.empty() ||
            app->requestNewThread) {
            app->escape = EscapeIntent::None;
            close(*app);
            return;
        }
        const std::string path = app->viewerImagePath;
        if (!hanabi::inline_image::available(path)) {
            close(*app);
            return;
        }

        Entity& uiRoot = ui_imm::getUIRootEntity();
        const float sw = hanabi::viewport::width();
        const float sh = hanabi::viewport::height();

        auto backdrop = button(ctx, mk(uiRoot, 8300),
                               hanabi::surface::scrim(sw, sh, 12)
                                   .with_debug_name("artifact_viewer_backdrop"));
        if (backdrop) {
            close(*app);
            return;
        }

        float natW = 0.0f;
        float natH = 0.0f;
        hanabi::inline_image::natural_size(path, natW, natH);
        if (natW <= 0.0f || natH <= 0.0f) return;
        const float boxW = std::max(64.0f, sw - kMargin * 2.0f);
        const float boxH = std::max(64.0f, sh - kMargin * 2.0f - kCaptionH);
        const float scale = std::min({1.0f, boxW / natW, boxH / natH});
        const float drawW = natW * scale;
        const float drawH = natH * scale;
        const float x = (sw - drawW) * 0.5f;
        const float y = (sh - kCaptionH - drawH) * 0.5f;

        div(ctx, mk(uiRoot, 8310),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(drawW), pixels(drawH)})
                .with_absolute_position()
                .with_translate(x, y)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_render_layer(13)
                .with_on_draw_fg([path, drawW, drawH](RectangleType r) {
                    hanabi::inline_image::draw(path, r.x, r.y, drawW, drawH);
                })
                .with_debug_name("artifact_viewer_image"));

        const std::string caption =
            app->viewerImageName + "   " + std::to_string(static_cast<int>(natW)) +
            " \xc3\x97 " + std::to_string(static_cast<int>(natH)) +
            "   \xc2\xb7   Esc closes";
        div(ctx, mk(uiRoot, 8320),
            ComponentConfig{}
                .with_label(caption)
                .with_size(ComponentSize{pixels(sw), pixels(kCaptionH)})
                .with_absolute_position()
                .with_translate(0.0f, y + drawH + 8.0f)
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_render_layer(13)
                .with_debug_name("artifact_viewer_caption"));
    }
};

}  // namespace ecs
