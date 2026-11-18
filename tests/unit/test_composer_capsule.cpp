// Regression fixtures for the floating composer capsule (kt-xuit).
// Each CHECK fails if the capsule change is reverted: the bar would be
// flush (no detachment) or every style would share one radius.
#include <cstdio>
#include <cstring>
#include "../../src/ecs/composer_capsule.h"

static int failures = 0;
#define CHECK(cond, name) do { if (!(cond)) { \
    std::printf("FAIL %s\n", name); ++failures; } \
    else std::printf("ok %s\n", name); } while (0)

int main() {
    using namespace ecs::model;
    // Detached: capsule is inset from both pane edges and the floor.
    auto c = capsule_rect(100, 800, 600, 98);
    CHECK(c.x > 100 && c.x + c.w < 700, "capsule_detached_sides");
    CHECK(c.y > 800 && c.y + c.h < 898, "capsule_detached_top_bottom");
    // Styles keep distinct radii; unknown setting falls back to Card.
    CHECK(capsule_radius(ComposerStyle::Glass) != capsule_radius(ComposerStyle::Card)
       && capsule_radius(ComposerStyle::Puffling) != capsule_radius(ComposerStyle::Card),
          "capsule_style_radii_distinct");
    CHECK(composer_style_from("glass") == ComposerStyle::Glass
       && composer_style_from("puffling") == ComposerStyle::Puffling
       && composer_style_from("bogus") == ComposerStyle::Card,
          "capsule_style_parse");
    CHECK(std::strcmp(name_of(ComposerStyle::Glass), "glass") == 0,
          "capsule_style_name");
    // Tiny bars never go negative.
    auto t = capsule_rect(0, 0, 10, 8);
    CHECK(t.w > 0 && t.h > 0, "capsule_tiny_bar_positive");
    if (failures) { std::printf("%d FAILURES\n", failures); return 1; }
    std::printf("ALL PASS\n");
    return 0;
}
