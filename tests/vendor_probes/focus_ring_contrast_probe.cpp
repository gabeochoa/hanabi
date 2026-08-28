#include <afterhours/tests/ui_test_harness.h>

using namespace afterhours;
using namespace afterhours::ui;
using namespace afterhours::ui::imm;

int outlines(const std::vector<DrawCall> &calls) {
    int count = 0;
    for (const auto &call : calls)
        count += call.op == "rectangle_rounded_lines";
    return count;
}

template<typename T>
concept HasFocusRingContrast = requires(T theme) { theme.focus_ring_contrast; };

template<typename T>
void disable_contrast(T &theme) {
    if constexpr (HasFocusRingContrast<T>) theme.focus_ring_contrast = false;
}

int render_ring(bool disable, bool batched) {
    ui_test::ImmTestHarness harness;
    harness.context().theme.focus = {255, 0, 255, 255};
    harness.context().theme.focus_ring_thickness = 1.f;
    harness.context().theme.focus_ring_offset = 0.f;
    if (disable) disable_contrast(harness.context().theme);
    auto target = button(harness.context(), mk(harness.root(), 0),
                         ComponentConfig{}
                             .with_size(ComponentSize{pixels(200), pixels(40)})
                             .with_custom_background(Color{10, 20, 30, 255})
                             .with_debug_name("focus_target"));
    harness.context().focus_id = target.ent().id;
    harness.context().visual_focus_id = target.ent().id;
    return outlines(batched ? harness.render_batched() : harness.render());
}

int main() {
    const int default_immediate = render_ring(false, false);
    const int disabled_immediate = render_ring(true, false);
    const int default_batched = render_ring(false, true);
    const int disabled_batched = render_ring(true, true);
    return HasFocusRingContrast<Theme> && default_immediate == 3 &&
                   disabled_immediate == 1 && default_batched == 3 &&
                   disabled_batched == 1
               ? 0
               : 1;
}
