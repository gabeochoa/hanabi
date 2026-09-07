
#include <cstdio>
#include <set>
#include <string>

#include "../../src/global_hotkeys.h"
#include "../../src/shortcuts.h"

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        ++g_checks;                                                    \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

namespace g = hanabi::globals;
namespace sc = hanabi::shortcuts;

static sc::Shortcut chord(int key, unsigned char mods) {
    return sc::Shortcut{key, mods};
}

static void test_the_shipped_chords_are_what_they_always_were() {
    const auto nt = g::definition(g::Slot::NewTask).shortcut;
    const auto pl = g::definition(g::Slot::Palette).shortcut;
    CHECK(nt.key == afterhours::keys::N);
    CHECK(nt.modifiers == (sc::CommandModifier | sc::ShiftModifier));
    CHECK(pl.key == afterhours::keys::K);
    CHECK(pl.modifiers == (sc::CommandModifier | sc::ShiftModifier));
    CHECK(g::carbon_key(nt.key).value_or(-1) == g::kVkN);
    CHECK(g::carbon_key(pl.key).value_or(-1) == g::kVkK);
    CHECK(g::carbon_modifiers(nt.modifiers) ==
          (g::kCarbonCmd | g::kCarbonShift));
}

static void test_every_slot_has_a_unique_key_and_a_registrable_default() {
    std::set<std::string> keys;
    for (const auto& item : g::kDefinitions) {
        CHECK(keys.insert(std::string(item.key)).second);
        CHECK(std::string(item.title).size() > 0);
        CHECK(std::string(item.help).size() > 0);
        CHECK(g::carbon_key(item.shortcut.key).has_value());
        CHECK((item.shortcut.modifiers & g::kRequiredAny) != 0);
    }
    CHECK(keys.size() == g::kSlotCount);
}

static void test_defaults_are_enabled_and_do_not_collide() {
    const auto req = g::defaults();
    CHECK(req.size() == g::kSlotCount);
    for (const auto& r : req) CHECK(r.enabled);
    CHECK(!(req[0].shortcut == req[1].shortcut));
}

static void test_carbon_key_covers_letters_and_space_only() {
    for (int k = afterhours::keys::A; k <= afterhours::keys::Z; ++k)
        CHECK(g::carbon_key(k).has_value());
    CHECK(g::carbon_key(afterhours::keys::SPACE).has_value());
    CHECK(!g::carbon_key(afterhours::keys::ONE).has_value());
    CHECK(!g::carbon_key(afterhours::keys::F1).has_value());
    CHECK(!g::carbon_key(afterhours::keys::ENTER).has_value());
    CHECK(!g::carbon_key(afterhours::keys::TAB).has_value());
}

static void test_every_offered_key_has_a_distinct_keycode() {
    std::set<int> codes;
    for (int k = afterhours::keys::A; k <= afterhours::keys::Z; ++k) {
        const auto code = g::carbon_key(k);
        CHECK(code.has_value());
        if (code) CHECK(codes.insert(*code).second);
    }
    const auto space = g::carbon_key(afterhours::keys::SPACE);
    CHECK(space.has_value());
    if (space) CHECK(codes.insert(*space).second);
    CHECK(codes.size() == 27);
}

static void test_modifier_masks_are_independent_bits() {
    CHECK(g::carbon_modifiers(0) == 0u);
    CHECK(g::carbon_modifiers(sc::CommandModifier) == g::kCarbonCmd);
    CHECK(g::carbon_modifiers(sc::ShiftModifier) == g::kCarbonShift);
    CHECK(g::carbon_modifiers(sc::OptionModifier) == g::kCarbonOption);
    CHECK(g::carbon_modifiers(sc::ControlModifier) == g::kCarbonControl);
    const auto all = g::carbon_modifiers(
        sc::CommandModifier | sc::ShiftModifier | sc::OptionModifier |
        sc::ControlModifier);
    CHECK(all == (g::kCarbonCmd | g::kCarbonShift | g::kCarbonOption |
                  g::kCarbonControl));
}

static void test_a_global_needs_a_real_modifier() {
    const auto other = chord(afterhours::keys::K,
                             sc::CommandModifier | sc::ShiftModifier);
    auto v = g::validate(g::Slot::NewTask,
                         chord(afterhours::keys::J, sc::ShiftModifier), other);
    CHECK(!v.ok);
    CHECK(v.explanation.find("Command") != std::string::npos);
    CHECK(!g::validate(g::Slot::NewTask, chord(afterhours::keys::J, 0), other)
               .ok);
    CHECK(g::validate(g::Slot::NewTask,
                      chord(afterhours::keys::J, sc::CommandModifier), other)
              .ok);
    CHECK(g::validate(g::Slot::NewTask,
                      chord(afterhours::keys::J, sc::ControlModifier), other)
              .ok);
    CHECK(g::validate(g::Slot::NewTask,
                      chord(afterhours::keys::J, sc::OptionModifier), other)
              .ok);
}

static void test_an_unregistrable_key_is_refused_by_name() {
    const auto other = chord(afterhours::keys::K, sc::CommandModifier);
    const auto v = g::validate(
        g::Slot::NewTask, chord(afterhours::keys::F1, sc::CommandModifier),
        other);
    CHECK(!v.ok);
    CHECK(v.explanation.find("key") != std::string::npos);
}

static void test_the_two_slots_cannot_share_a_chord() {
    const auto taken = chord(afterhours::keys::K,
                             sc::CommandModifier | sc::ShiftModifier);
    const auto v = g::validate(g::Slot::NewTask, taken, taken);
    CHECK(!v.ok);
    CHECK(v.explanation.find(std::string(
              g::definition(g::Slot::Palette).title)) != std::string::npos);
    const auto v2 = g::validate(g::Slot::Palette, taken, taken);
    CHECK(!v2.ok);
    CHECK(v2.explanation.find(std::string(
              g::definition(g::Slot::NewTask).title)) != std::string::npos);
}

static void test_an_empty_chord_is_refused() {
    CHECK(!g::validate(g::Slot::NewTask, sc::Shortcut{},
                       chord(afterhours::keys::K, sc::CommandModifier))
               .ok);
}

static void test_chords_render_in_canonical_modifier_order() {
    CHECK(sc::display(chord(afterhours::keys::E,
                            sc::CommandModifier | sc::ShiftModifier |
                                sc::OptionModifier | sc::ControlModifier)) ==
          "Ctrl Opt Shift Cmd E");
    CHECK(sc::display(chord(afterhours::keys::E,
                            sc::CommandModifier | sc::ShiftModifier)) ==
          "Shift Cmd E");
    CHECK(sc::display(chord(afterhours::keys::N, sc::CommandModifier)) ==
          "Cmd N");
    CHECK(sc::display(sc::Shortcut{}) == "Unassigned");
    for (const auto& item : sc::kDefinitions) {
        if (!(item.shortcut.modifiers & sc::CommandModifier)) continue;
        const std::string shown = sc::display(item.shortcut);
        const std::string key = sc::key_name(item.shortcut.key);
        CHECK(shown.rfind("Cmd " + key) == shown.size() - key.size() - 4);
    }
}

static bool is_ascii_printable(const std::string& value) {
    for (unsigned char c : value)
        if (c < 0x20 || c > 0x7e) return false;
    return true;
}

static void test_no_modifier_can_render_as_an_unprintable_glyph() {
    const unsigned char mods[4] = {sc::CommandModifier, sc::ControlModifier,
                                   sc::OptionModifier, sc::ShiftModifier};
    for (unsigned char m : mods) {
        const std::string shown = sc::display(chord(afterhours::keys::E, m));
        CHECK(is_ascii_printable(shown));
        CHECK(shown.size() > 1);
        CHECK(shown != "E");
    }
    for (unsigned char m : mods)
        for (unsigned char n : mods) {
            const std::string shown =
                sc::display(chord(afterhours::keys::E,
                                  static_cast<unsigned char>(m | n)));
            CHECK(is_ascii_printable(shown));
            CHECK(shown.find('E') != std::string::npos);
            if (m != n) CHECK(shown.size() >= 7);
        }
    for (const auto& item : sc::kDefinitions)
        CHECK(is_ascii_printable(sc::display(item.shortcut)));
    for (const auto& item : g::kDefinitions)
        CHECK(is_ascii_printable(sc::display(item.shortcut)));
    for (int k = afterhours::keys::A; k <= afterhours::keys::Z; ++k)
        CHECK(is_ascii_printable(sc::key_name(k)));
    CHECK(is_ascii_printable(sc::key_name(afterhours::keys::SPACE)));
    CHECK(is_ascii_printable(sc::key_name(afterhours::keys::ENTER)));
    CHECK(is_ascii_printable(sc::key_name(afterhours::keys::UP)));
}

int main() {
    std::printf("== global hotkeys ==\n");
    test_the_shipped_chords_are_what_they_always_were();
    test_every_slot_has_a_unique_key_and_a_registrable_default();
    test_defaults_are_enabled_and_do_not_collide();
    test_carbon_key_covers_letters_and_space_only();
    test_every_offered_key_has_a_distinct_keycode();
    test_modifier_masks_are_independent_bits();
    test_a_global_needs_a_real_modifier();
    test_an_unregistrable_key_is_refused_by_name();
    test_the_two_slots_cannot_share_a_chord();
    test_an_empty_chord_is_refused();
    test_chords_render_in_canonical_modifier_order();
    test_no_modifier_can_render_as_an_unprintable_glyph();

    if (g_failures != 0) {
        std::printf("FAILED (%d of %d checks)\n", g_failures, g_checks);
        return 1;
    }
    if (g_checks == 0) {
        std::printf("FAILED (no checks ran)\n");
        return 1;
    }
    std::printf("OK (%d checks)\n", g_checks);
    return 0;
}
