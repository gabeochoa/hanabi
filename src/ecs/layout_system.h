#pragma once

#include <algorithm>
#include <cmath>

#include "ui_imports.h"

namespace ecs {

// Recomputes panel rectangles from the current window size each frame, and
// drives the sidebar collapse animation (width tweened with a smoothstep ease,
// mirroring floatinghotel's approach — a CSS-transition feel in app code).
struct LayoutSystem : afterhours::System<LayoutComponent> {
    // smoothstep(0..1): ease-in-out, matches the mock's `.18s ease` feel.
    static float smoothstep(float x) {
        x = std::clamp(x, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    void for_each_with(Entity&, LayoutComponent& layout, float dt) override {
        float w = hanabi::viewport::width();
        float h = hanabi::viewport::height();

        // --- Sidebar width animation ---
        float target =
            layout.sidebarCollapsed ? layout.sidebarRailWidth : layout.sidebarWidth;
        if (std::abs(target - layout.sidebarAnimTarget) > 0.5f) {
            // A new toggle target: start a fresh tween from the current width.
            layout.sidebarAnimFrom = layout.sidebarAnimWidth;
            layout.sidebarAnimTarget = target;
            layout.sidebarAnimT = 0.0f;
        }
        if (layout.sidebarAnimT < 1.0f) {
            // ~0.18s transition.
            layout.sidebarAnimT =
                std::min(1.0f, layout.sidebarAnimT + dt / 0.18f);
            float e = smoothstep(layout.sidebarAnimT);
            layout.sidebarAnimWidth =
                layout.sidebarAnimFrom +
                (layout.sidebarAnimTarget - layout.sidebarAnimFrom) * e;
        } else {
            layout.sidebarAnimWidth = layout.sidebarAnimTarget;
        }

        float sidebarW = layout.sidebarAnimWidth;
        if (sidebarW > w * 0.5f) sidebarW = w * 0.5f;

        float barH = layout.statusBarHeight;
        float tabH = layout.tabStripHeight;
        float contentH = h - barH;
        if (contentH < 0) contentH = 0;

        float mainX = sidebarW;
        float mainW = w - sidebarW;
        if (mainW < 0) mainW = 0;

        // Composer strip: pinned at the bottom of the main pane, directly above
        // the status bar. Carved OUT of `main` (which shrinks by its height) so
        // the content area and the composer never overlap. Same
        // dedicated-rect + absolute-render pattern as the status bar (which
        // renders reliably at the bottom every frame).
        float compH = layout.composerHeight;
        float mainH = contentH - tabH - compH;
        if (mainH < 0) mainH = 0;

        layout.sidebar = {0, 0, sidebarW, h};
        layout.tabStrip = {mainX, 0, mainW, tabH};
        layout.main = {mainX, tabH, mainW, mainH};
        layout.composer = {mainX, tabH + mainH, mainW, compH};
        // The status bar spans the MAIN pane only. Puffin's sidebar owns its
        // own bottom strip (version + actions) and runs to the window's floor,
        // so a full-width bar would paint over it. The main pane's geometry is
        // unchanged: it was already sized against contentH.
        layout.statusBar = {mainX, contentH, mainW, barH};
    }
};

}  // namespace ecs
