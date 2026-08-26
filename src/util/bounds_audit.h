#pragma once

// ---------------------------------------------------------------------------
// The containment probe: which widgets draw outside the box they were given?
//
// Reported symptom: "many buttons are going outside the bounds". A screenshot
// shows you one of them if you happen to be looking at the right pixel, and
// says nothing at all about the ones on a screen you did not capture. A widget
// that escapes its parent is a property of the LAYOUT, not of the picture, so
// it can be read off the tree directly and completely.
//
// WHY IT LIVES IN THE APP rather than in a script: containment is a
// relationship between two rects that only exist after autolayout has run,
// inside the ECS. Nothing outside the process can see either one.
//
// WHAT IT MEASURES. For every laid-out UI element with a parent, whether its
// rect() lies inside the parent's CONTENT box -- the parent's rect inset by the
// parent's own computed padding, which is the space the parent set aside for
// its children. An element sticking out of that is drawing over its parent's
// padding, its parent's border, or its parent's siblings.
//
// WHAT IT DELIBERATELY DOES NOT FLAG:
//   * absolutely-positioned elements. They are out of flow by construction --
//     the sidebar row's star and the composer's slash menu are both meant to
//     escape -- so flagging them would bury the flow overflows in noise. They
//     are counted separately so the number is still visible.
//   * anything under kEpsilon. A fractional overflow is autolayout rounding
//     (afterhours_gaps.md #110: nothing rounds a widget's origin), not a
//     design defect, and it fires on almost every percent()-sized element.
//   * a hidden element, or one autolayout never sized (computed < 0).
//   * an element that did not RENDER on the last frame. This one is not an
//     optimisation, it is correctness, and it cost this audit its first
//     finding: a widget built on an earlier frame and never rebuilt keeps its
//     parent id, its computed rect and its last position, so it looks exactly
//     like a live overflow and is not one (afterhours_gaps.md #115 and
//     src/ui/widget_epoch.h are what that staleness is). afterhours' own
//     assert_no_overflow tests the same flag first, for the same reason.
//   * a child of a scroll view. Content taller than the viewport IS a scroll
//     view, so every list in the app would report its whole overhang and the
//     real overflows would be under it.
//
//   HANABI_BOUNDS_AUDIT=1  print the report after the capture's last frame
//
// Hard no-op when unset, the same contract as test_hooks.h and util/soak.h.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "../../vendor/afterhours/ah.h"

namespace hanabi::bounds_audit {

inline bool on() {
    static const bool v = [] {
        const char* s = std::getenv("HANABI_BOUNDS_AUDIT");
        return s != nullptr && *s != '\0' && std::string_view(s) != "0";
    }();
    return v;
}

// Fractions below this are autolayout rounding, not an escaped widget.
inline constexpr float kEpsilon = 0.5f;

struct Overflow {
    std::string name;
    std::string parentName;
    float left = 0.0f;
    float right = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
    // The child's own rect, and the parent's content box it escaped.
    ::RectangleType rect{};
    ::RectangleType box{};

    [[nodiscard]] float worst() const {
        return std::max(std::max(left, right), std::max(top, bottom));
    }
};

inline std::string name_of(const afterhours::Entity& e) {
    if (e.has<afterhours::ui::UIComponentDebug>())
        return e.get<afterhours::ui::UIComponentDebug>().name_value;
    return "(unnamed)";
}

// Every flow-positioned element whose rect escapes its parent's content box,
// worst first. `absoluteSkipped` receives the count of out-of-flow elements
// that were not examined, so the report can say what it did not look at.
inline std::vector<Overflow> collect(int* absoluteSkipped = nullptr) {
    using afterhours::ui::UIComponent;
    std::vector<Overflow> out;
    int skipped = 0;
    for (auto& ptr : afterhours::EntityHelper::get_entities_for_mod()) {
        if (!ptr) continue;
        afterhours::Entity& e = *ptr;
        if (!e.has<UIComponent>()) continue;
        const UIComponent& c = e.get<UIComponent>();
        if (c.should_hide) continue;
        if (!c.was_rendered_to_screen) continue;
        if (c.parent < 0) continue;
        if (c.computed[afterhours::ui::Axis::X] < 0.0f ||
            c.computed[afterhours::ui::Axis::Y] < 0.0f)
            continue;
        if (c.absolute) {
            ++skipped;
            continue;
        }
        auto popt = afterhours::EntityHelper::getEntityForID(c.parent);
        if (!popt.valid()) continue;
        const afterhours::Entity& pe = popt.asE();
        if (!pe.has<UIComponent>()) continue;
        if (pe.has<afterhours::ui::HasScrollView>()) continue;
        const UIComponent& p = pe.get<UIComponent>();
        if (p.should_hide) continue;
        if (!p.was_rendered_to_screen) continue;
        if (p.computed[afterhours::ui::Axis::X] < 0.0f ||
            p.computed[afterhours::ui::Axis::Y] < 0.0f)
            continue;

        const auto r = c.rect();
        const auto pr = p.rect();
        // The parent's CONTENT box: what it set aside for children.
        const float pl = pr.x + p.computed_padd[afterhours::ui::Axis::left];
        const float pt = pr.y + p.computed_padd[afterhours::ui::Axis::top];
        const float prr =
            pr.x + pr.width - p.computed_padd[afterhours::ui::Axis::right];
        const float pb =
            pr.y + pr.height - p.computed_padd[afterhours::ui::Axis::bottom];

        Overflow o;
        o.left = pl - r.x;
        o.top = pt - r.y;
        o.right = (r.x + r.width) - prr;
        o.bottom = (r.y + r.height) - pb;
        o.left = std::max(0.0f, o.left);
        o.top = std::max(0.0f, o.top);
        o.right = std::max(0.0f, o.right);
        o.bottom = std::max(0.0f, o.bottom);
        if (o.worst() <= kEpsilon) continue;
        o.rect = ::RectangleType{r.x, r.y, r.width, r.height};
        o.box = ::RectangleType{pl, pt, prr - pl, pb - pt};
        o.name = name_of(e);
        o.parentName = name_of(pe);
        out.push_back(std::move(o));
    }
    std::sort(out.begin(), out.end(), [](const Overflow& a, const Overflow& b) {
        return a.worst() > b.worst();
    });
    if (absoluteSkipped) *absoluteSkipped = skipped;
    return out;
}

inline void report() {
    if (!on()) return;
    int skipped = 0;
    const std::vector<Overflow> rows = collect(&skipped);
    std::printf("[bounds] %zu flow element(s) outside their parent's content "
                "box (%d absolute skipped)\n",
                rows.size(), skipped);
    for (const Overflow& o : rows) {
        std::printf("[bounds]   %-28s in %-24s  over by", o.name.c_str(),
                    o.parentName.c_str());
        if (o.left > kEpsilon) std::printf(" left=%.1f", static_cast<double>(o.left));
        if (o.right > kEpsilon) std::printf(" right=%.1f", static_cast<double>(o.right));
        if (o.top > kEpsilon) std::printf(" top=%.1f", static_cast<double>(o.top));
        if (o.bottom > kEpsilon) std::printf(" bottom=%.1f", static_cast<double>(o.bottom));
        std::printf("   [%.1f,%.1f %.1fx%.1f in %.1f,%.1f %.1fx%.1f]\n",
                    static_cast<double>(o.rect.x), static_cast<double>(o.rect.y),
                    static_cast<double>(o.rect.width),
                    static_cast<double>(o.rect.height),
                    static_cast<double>(o.box.x), static_cast<double>(o.box.y),
                    static_cast<double>(o.box.width),
                    static_cast<double>(o.box.height));
    }
    std::fflush(stdout);
}

}  // namespace hanabi::bounds_audit
