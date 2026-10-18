#include <cstdio>
#include <string>

#include "../../src/ui/session_sound.h"

namespace ss = hanabi::session_sound;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

int main() {
    std::printf("=== test_session_sound ===\n");
    CHECK(ss::from_int(0) == ss::Tone::Inherit && ss::from_int(-1) == ss::Tone::Silent);
    CHECK(ss::from_int(1) == ss::Tone::TwoTone && ss::from_int(7) == ss::Tone::TwoTone);
    CHECK(ss::from_int(2) == ss::Tone::Low && ss::from_int(3) == ss::Tone::Bright && ss::from_int(4) == ss::Tone::Triple);
    for (ss::Tone t : ss::kMenuOrder) CHECK(ss::parse(ss::wire(t)) == t && ss::from_int(ss::to_int(t)) == t);
    CHECK(ss::parse("on") == ss::Tone::TwoTone && ss::parse("off") == ss::Tone::Silent && ss::parse("default") == ss::Tone::Inherit);
    CHECK(!ss::parse("loud"));
    CHECK(ss::menu_label(ss::Tone::Inherit, true) == "Sound: Default (on)");
    CHECK(ss::menu_label(ss::Tone::Inherit, false) == "Sound: Default (off)");
    CHECK(ss::menu_label(ss::Tone::Triple, true) == "Sound: Triple");
    CHECK(ss::action_id(ss::Tone::Low) == "sound:low" && ss::debug_name(ss::Tone::Low) == "row_menu_sound_low");
    CHECK(ss::debug_name(ss::Tone::Silent) == "row_menu_sound_off" && ss::debug_name(ss::Tone::Inherit) == "row_menu_sound_default");

    const auto two = ss::notes(ss::Tone::TwoTone);
    CHECK(two.count == 2 && two.notes[0].hz == 880.0 && two.notes[1].hz == 1174.66);
    const auto low = ss::notes(ss::Tone::Low);
    CHECK(low.count == 2 && low.notes[0].hz == 329.63 && low.notes[1].hz == 440.0);
    const auto bright = ss::notes(ss::Tone::Bright);
    CHECK(bright.count == 2 && bright.notes[0].hz == 1318.51 && bright.notes[1].hz == 1760.0);
    const auto triple = ss::notes(ss::Tone::Triple);
    CHECK(triple.count == 3 && triple.notes[0].hz == 659.25 && triple.notes[1].hz == 880.0 && triple.notes[2].hz == 1174.66);
    CHECK(ss::notes(ss::Tone::Silent).count == 0 && ss::notes(ss::Tone::Inherit).count == 0);
    for (ss::Tone t : ss::kAudible) {
        const auto seq = ss::notes(t);
        for (std::size_t i = 0; i < seq.count; ++i) CHECK(seq.notes[i].seconds > 0.05 && seq.notes[i].seconds < 0.5);
    }

    {
        const auto v = ss::resolve(ss::Tone::Inherit, true, true);
        CHECK(v.chime == ss::kDefaultChime && v.banner_sound);
    }
    {
        const auto v = ss::resolve(ss::Tone::Inherit, false, false);
        CHECK(!v.chime && !v.banner_sound);
    }
    {
        const auto v = ss::resolve(ss::Tone::Inherit, false, true);
        CHECK(!v.chime && v.banner_sound);
    }
    {
        const auto v = ss::resolve(ss::Tone::Bright, false, false);
        CHECK(v.chime == ss::Tone::Bright && v.banner_sound);
    }
    {
        const auto v = ss::resolve(ss::Tone::Silent, true, true);
        CHECK(!v.chime && !v.banner_sound);
    }
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
