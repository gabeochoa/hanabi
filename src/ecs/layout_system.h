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

        // No band is reserved at the window's floor. hanabi used to carve 26px
        // off the bottom of the main column for a status strip; Puffin has no
        // such surface (`MainWindowShell`'s root is an HStack — sidebar beside
        // content — and its only bottom chrome is the sidebar-width
        // `SidebarColumn.sidebarFooter`), so the strip pushed hanabi's whole
        // composer 26px above where the reference puts it. The strip is gone
        // and its information lives in the sidebar footer now
        // (`sidebar_footer_status.h`); the composer runs to the window's floor,
        // as Puffin's does.
        float tabH = layout.tabStripHeight;
        float contentH = h;

        float mainX = sidebarW;
        float mainW = w - sidebarW;
        if (mainW < 0) mainW = 0;

        // Composer strip: pinned at the bottom of the main pane. Carved OUT of
        // `main` (which shrinks by its height) so the content area and the
        // composer never overlap.
        float compH = layout.composerHeight;
        float mainH = contentH - tabH - compH;
        if (mainH < 0) mainH = 0;

        layout.sidebar = {0, 0, sidebarW, h};
        layout.tabStrip = {mainX, 0, mainW, tabH};
        layout.main = {mainX, tabH, mainW, mainH};
        layout.composer = {mainX, tabH + mainH, mainW, compH};
    }
};

}  // namespace ecs
