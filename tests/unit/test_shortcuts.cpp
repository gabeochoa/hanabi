#include <cstdio>
#include <string>

#include "../../src/shortcuts.h"

static int failures = 0;
#define CHECK(value)                                             \
    do {                                                         \
        if (!(value)) {                                          \
            std::printf("FAIL: %s line %d\n", #value, __LINE__); \
            ++failures;                                          \
        }                                                        \
    } while (0)

using hanabi::shortcuts::Command;
using hanabi::shortcuts::Shortcut;

static void test_defaults_are_unique_and_valid() {
    auto bindings = hanabi::shortcuts::defaults();
    for (const auto& item : hanabi::shortcuts::kDefinitions) {
        CHECK(!bindings[hanabi::shortcuts::index(item.command)].empty());
        CHECK(hanabi::shortcuts::validate(item.command, item.shortcut, bindings)
                  .ok);
    }
    for (std::size_t i = 0; i < bindings.size(); ++i)
        for (std::size_t j = i + 1; j < bindings.size(); ++j)
            CHECK(!(bindings[i] == bindings[j]));
}

static void test_serialization_round_trips() {
    auto bindings = hanabi::shortcuts::defaults();
    for (const auto& binding : bindings) {
        const auto parsed =
            hanabi::shortcuts::parse(hanabi::shortcuts::serialize(binding));
        CHECK(parsed.has_value());
        CHECK(parsed.value_or(Shortcut{}) == binding);
        CHECK(!hanabi::shortcuts::display(binding).empty());
        CHECK(!hanabi::shortcuts::native_key_equivalent(binding).empty());
    }
}

static void test_conflicts_name_the_owner() {
    auto bindings = hanabi::shortcuts::defaults();
    const auto result = hanabi::shortcuts::validate(
        Command::OpenSettings,
        bindings[hanabi::shortcuts::index(Command::NewTask)], bindings);
    CHECK(!result.ok);
    CHECK(result.explanation.find("New task") != std::string::npos);
}

static void test_reserved_chords_explain_why() {
    using namespace hanabi::shortcuts;
    using namespace afterhours::keys;
    auto bindings = defaults();
    const Shortcut reserved[] = {
        {Q, CommandModifier},
        {SPACE, CommandModifier},
        {A, CommandModifier},
        {D, static_cast<std::uint8_t>(CommandModifier | OptionModifier)},
        {FOUR, static_cast<std::uint8_t>(CommandModifier | ShiftModifier)},
        {SPACE, static_cast<std::uint8_t>(CommandModifier | ControlModifier)},
        {N, static_cast<std::uint8_t>(CommandModifier | ShiftModifier)},
    };
    for (const auto chord : reserved) {
        const auto result = validate(Command::OpenPalette, chord, bindings);
        CHECK(!result.ok);
        CHECK(!result.explanation.empty());
    }
    const auto bare = validate(Command::OpenPalette, {P, 0}, bindings);
    CHECK(!bare.ok);
    CHECK(bare.explanation.find("Command") != std::string::npos);
}

static void test_safe_custom_chord_is_accepted() {
    using namespace hanabi::shortcuts;
    using namespace afterhours::keys;
    auto bindings = defaults();
    Shortcut custom{P,
                    static_cast<std::uint8_t>(CommandModifier | ShiftModifier)};
    CHECK(validate(Command::OpenPalette, custom, bindings).ok);
    bindings[index(Command::OpenPalette)] = custom;
    CHECK(!validate(Command::NewTask, custom, bindings).ok);
}

static void test_tab_slots_name_their_position() {
    using namespace hanabi::shortcuts;
    using namespace afterhours::keys;
    CHECK(tab_slot_for(Command::NewTask) == 0);
    CHECK(tab_slot_for(Command::SearchThreads) == 0);
    CHECK(tab_slot_for(Command::SelectTab1) == 1);
    CHECK(tab_slot_for(Command::SelectTab9) == kTabSlots);
    for (int slot = 1; slot <= kTabSlots; ++slot) {
        const Command command = command_for_tab_slot(slot);
        CHECK(tab_slot_for(command) == slot);
        const Definition& def = definition(command);
        CHECK(def.shortcut.modifiers == CommandModifier);
        CHECK(def.shortcut.key == ZERO + slot);
        CHECK(def.section == "Window");
    }
}

static void test_tab_index_for_slot_covers_short_strips() {
    using hanabi::shortcuts::tab_index_for_slot;
    for (int slot = 1; slot <= hanabi::shortcuts::kTabSlots; ++slot)
        CHECK(tab_index_for_slot(slot, 0) == -1);

    CHECK(tab_index_for_slot(1, 3) == 0);
    CHECK(tab_index_for_slot(3, 3) == 2);
    CHECK(tab_index_for_slot(4, 3) == -1);
    CHECK(tab_index_for_slot(8, 3) == -1);

    // The last slot is the last TAB, which is the whole point of it: on a
    // strip of three it is the third, and it is never a miss.
    CHECK(tab_index_for_slot(9, 1) == 0);
    CHECK(tab_index_for_slot(9, 3) == 2);
    CHECK(tab_index_for_slot(9, 9) == 8);
    CHECK(tab_index_for_slot(9, 20) == 19);
    CHECK(tab_index_for_slot(8, 20) == 7);

    // Out of range is refused, never wrapped.
    CHECK(tab_index_for_slot(0, 5) == -1);
    CHECK(tab_index_for_slot(10, 5) == -1);
    CHECK(tab_index_for_slot(-1, 5) == -1);
}

static void test_tab_chords_clear_the_reserved_list() {
    using namespace hanabi::shortcuts;
    using namespace afterhours::keys;
    auto bindings = defaults();
    for (int slot = 1; slot <= kTabSlots; ++slot) {
        const Command command = command_for_tab_slot(slot);
        CHECK(validate(command, definition(command).shortcut, bindings).ok);
    }
    // Shift Cmd 3/4/5 stay the system's screenshot chords, so the digits are
    // allowed as a plain Cmd chord and not blanket-allowed.
    const auto shot = validate(
        Command::SelectTab3,
        Shortcut{THREE,
                 static_cast<std::uint8_t>(CommandModifier | ShiftModifier)},
        bindings);
    CHECK(!shot.ok);
}

int main() {
    test_defaults_are_unique_and_valid();
    test_serialization_round_trips();
    test_conflicts_name_the_owner();
    test_reserved_chords_explain_why();
    test_safe_custom_chord_is_accepted();
    test_tab_slots_name_their_position();
    test_tab_index_for_slot_covers_short_strips();
    test_tab_chords_clear_the_reserved_list();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
