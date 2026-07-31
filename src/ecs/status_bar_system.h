#pragma once

// Bottom status bar: backend label + session count.

#include <string>

#include "ui_imports.h"

namespace ecs {

struct StatusBarSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* layout = find_singleton<LayoutComponent>();
        auto* app = find_singleton<AppComponent>();
        if (!layout || !app) return;

        Entity& uiRoot = ui_imm::getUIRootEntity();
        const auto& r = layout->statusBar;

        div(ctx, mk(uiRoot, 3000),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(r.width), pixels(r.height)})
                .with_absolute_position()
                .with_translate(r.x, r.y)
                .with_custom_background(theme::sidebar_bg())
                .with_roundness(0.0f)
                .with_render_layer(5)
                .with_debug_name("status_bar_bg"));

        std::string left = "backend: " + app->backend_label;
        int blocked = 0;
        for (const auto& s : app->sessions)
            if (s.tag == api::ThreadTag::Blocked) ++blocked;
        if (blocked > 0)
            left += "   \xe2\x97\x8f " + std::to_string(blocked) +
                    " blocked on you";
        div(ctx, mk(uiRoot, 3001),
            ComponentConfig{}
                .with_label(left)
                .with_size(ComponentSize{pixels(r.width * 0.6f), pixels(r.height)})
                .with_absolute_position()
                .with_translate(r.x + 10.0f, r.y)
                .with_padding(Padding{.top = pixels(5), .right = pixels(8),
                                      .bottom = pixels(4), .left = pixels(8)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(11.0f)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_render_layer(5)
                .with_debug_name("status_left"));

        std::string right = std::to_string(app->sessions.size()) + " sessions";
        float rw = r.width * 0.4f - 18.0f;
        if (rw < 40.0f) rw = 40.0f;
        div(ctx, mk(uiRoot, 3002),
            ComponentConfig{}
                .with_label(right)
                .with_size(ComponentSize{pixels(rw), pixels(r.height)})
                .with_absolute_position()
                .with_translate(r.x + r.width * 0.6f, r.y)
                .with_padding(Padding{.top = pixels(5), .right = pixels(12),
                                      .bottom = pixels(4), .left = pixels(8)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(11.0f)
                .with_alignment(TextAlignment::Right)
                .with_roundness(0.0f)
                .with_render_layer(5)
                .with_debug_name("status_right"));
    }
};

}  // namespace ecs
