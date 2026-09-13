// A tab that is not a conversation: the id round trip, and the promise that
// no ordinary session id can be mistaken for one.
//
// The whole point of the reserved id is that the tab strip needs no new
// vocabulary -- a surface is a tab whose "session" is a scheme -- so the one
// thing that must hold is that the two id spaces never overlap.

#include <cstdio>
#include <string>

#include "../../src/ecs/surface_tabs.h"
#include "../../src/ecs/thread_model.h"

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);       \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

int main() {
    namespace S = ecs::model;

    const std::string id = S::surface_tab_id(S::Surface::Settings);
    CHECK(id == "hanabi:surface/settings");
    CHECK(S::is_surface_tab(id));
    CHECK(S::is_settings_tab(id));
    CHECK(S::surface_of(id) == S::Surface::Settings);
    CHECK(std::string(S::surface_title(S::Surface::Settings)) == "Settings");
    CHECK(std::string(S::surface_name(S::Surface::Settings)) == "settings");

    // An ordinary session id, however it is spelled, is a conversation.
    for (const char* other : {"t2", "r8", "new1", "settings",
                              "surface/settings", "hanabi:surface",
                              "hanabi:surface/", ""}) {
        CHECK(!S::is_surface_tab(other));
        CHECK(!S::is_settings_tab(other));
    }

    // A name the vocabulary does not have is not a surface either: an id
    // from a newer build must not open a blank pane here.
    CHECK(!S::is_surface_tab("hanabi:surface/companion"));
    CHECK(!S::surface_of("hanabi:surface/welcome").has_value());

    // The sidebar's age column, in the reference's ladder: now / Nm / Nh /
    // Nd, and days never roll over into weeks, months or a date.
    CHECK(S::sidebar_age(1000, 1000) == "now");
    CHECK(S::sidebar_age(1000, 1059) == "now");
    CHECK(S::sidebar_age(1000, 1060) == "1m");
    CHECK(S::sidebar_age(1000, 1000 + 59 * 60) == "59m");
    CHECK(S::sidebar_age(1000, 1000 + 3600) == "1h");
    CHECK(S::sidebar_age(1000, 1000 + 23 * 3600) == "23h");
    CHECK(S::sidebar_age(1000, 1000 + 86400) == "1d");
    CHECK(S::sidebar_age(1000, 1000 + 9 * 86400) == "9d");
    CHECK(S::sidebar_age(1000, 1000 + 412 * 86400) == "412d");
    // A clock ahead of this machine's is "now", never a dash or a negative.
    CHECK(S::sidebar_age(2000, 1000) == "now");
    CHECK(S::sidebar_age(0, 1000).empty());

    if (failures == 0) std::printf("OK\n");
    return failures == 0 ? 0 : 1;
}
