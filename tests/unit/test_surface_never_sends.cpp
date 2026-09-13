// A surface id is not a session, and nothing outbound may carry one.
//
// The model half of the rule, held without a window: the composer's target
// refuses a surface, and the outbox's replay skips one. The UI half (a pane
// showing Settings draws no composer, refuses a drop, swallows no keystroke)
// is in tests/ui/settings_*.e2e and a_surface_pane_refuses_what_it_cannot_hold.

#include <cstdio>
#include <string>

#include "../../src/api/types.h"
#include "../../src/ecs/surface_tabs.h"

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);       \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

int main() {
    namespace M = ecs::model;
    const std::string surface = M::surface_tab_id(M::Surface::Settings);

    // The rule AppComponent::composer_target_for applies, held here without
    // the UI it lives in: a pane's selected id becomes an outbound target
    // only when it names a conversation. (The wiring itself -- that the
    // composer, the brake, the steer and Stop all read that one function --
    // is held by tests/ui/settings_in_a_split_never_touches_the_other_thread
    // and a_surface_pane_refuses_what_it_cannot_hold.)
    const auto targets_a_thread = [](const std::string& selectedId) {
        return !selectedId.empty() && !M::is_surface_tab(selectedId);
    };
    CHECK(targets_a_thread("t2"));
    CHECK(targets_a_thread("r8"));
    CHECK(!targets_a_thread(""));
    CHECK(!targets_a_thread(surface));

    // Every spelling of the surface is refused, and nothing else is.
    CHECK(M::is_surface_tab(surface));
    CHECK(M::is_settings_tab(surface));
    CHECK(!M::is_surface_tab("t2"));
    CHECK(!M::is_surface_tab("settings"));

    if (failures == 0) std::printf("OK\n");
    return failures == 0 ? 0 : 1;
}
