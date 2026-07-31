#pragma once

#include "ui_imports.h"

namespace ecs {

// Recomputes panel rectangles from the current window size each frame.
struct LayoutSystem : afterhours::System<LayoutComponent> {
    void for_each_with(Entity&, LayoutComponent& layout, float) override {
        float w = static_cast<float>(afterhours::graphics::get_screen_width());
        float h = static_cast<float>(afterhours::graphics::get_screen_height());

        float sidebarW = layout.sidebarWidth;
        if (sidebarW > w * 0.5f) sidebarW = w * 0.5f;
        float barH = layout.statusBarHeight;
        float contentH = h - barH;
        if (contentH < 0) contentH = 0;

        layout.sidebar = {0, 0, sidebarW, contentH};
        layout.transcript = {sidebarW, 0, w - sidebarW, contentH};
        layout.statusBar = {0, contentH, w, barH};
    }
};

}  // namespace ecs
