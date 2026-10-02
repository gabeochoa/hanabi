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
        const bool empty = bindings[hanabi::shortcuts::index(item.command)].empty();
        CHECK(empty == hanabi::shortcuts::unassigned_by_default(item.command));
        if (empty) continue;
        CHECK(hanabi::shortcuts::validate(item.command, item.shortcut, bindings)
                  .ok);
    }
    for (std::size_t i = 0; i < bindings.size(); ++i)
        for (std::size_t j = i + 1; j < bindings.size(); ++j)
            CHECK(bindings[i].empty() || !(bindings[i] == bindings[j]));
}

static void test_serialization_round_trips() {
    auto bindings = hanabi::shortcuts::defaults();
    for (const auto& binding : bindings) {
        if (binding.empty()) continue;
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

static void test_zoom_in_alone_takes_a_shifted_alias_on_its_default_binding() {
    using namespace hanabi::shortcuts;
    using namespace afterhours::keys;
    const Shortcut dflt = definition(Command::ZoomIn).shortcut;
    CHECK(dflt == (Shortcut{EQUAL, CommandModifier}));
    const auto alias = shifted_alias(Command::ZoomIn, dflt);
    CHECK(alias.has_value());
    CHECK(alias && *alias == (Shortcut{EQUAL, static_cast<std::uint8_t>(CommandModifier | ShiftModifier)}));
    CHECK(!shifted_alias(Command::ZoomIn, Shortcut{EQUAL, static_cast<std::uint8_t>(CommandModifier | OptionModifier)}));
    CHECK(!shifted_alias(Command::ZoomIn, Shortcut{P, CommandModifier}));
    CHECK(!shifted_alias(Command::ZoomOut, definition(Command::ZoomOut).shortcut));
    CHECK(!shifted_alias(Command::ZoomActual, definition(Command::ZoomActual).shortcut));
    for (const auto& item : kDefinitions)
        if (item.command != Command::ZoomIn) CHECK(!shifted_alias(item.command, item.shortcut));
    CHECK(definition(Command::ZoomOut).shortcut == (Shortcut{MINUS, CommandModifier}));
    CHECK(definition(Command::ZoomActual).shortcut == (Shortcut{ZERO, CommandModifier}));
    CHECK(tab_slot_for(Command::ZoomActual) == 0);
}

static void test_relative_tab_moves_follow_the_reference() {
    using namespace hanabi::shortcuts;
    using namespace afterhours::keys;
    // The six rows and their reference chords.
    CHECK(definition(Command::ShowPreviousTab).shortcut ==
          (Shortcut{LEFT_BRACKET, CommandModifier}));
    CHECK(definition(Command::ShowNextTab).shortcut ==
          (Shortcut{RIGHT_BRACKET, CommandModifier}));
    CHECK(definition(Command::CycleTabsBackward).shortcut ==
          (Shortcut{LEFT, static_cast<std::uint8_t>(CommandModifier | OptionModifier)}));
    CHECK(definition(Command::CycleTabsForward).shortcut ==
          (Shortcut{RIGHT, static_cast<std::uint8_t>(CommandModifier | OptionModifier)}));
    CHECK(definition(Command::PreviousTab).shortcut ==
          (Shortcut{TAB, static_cast<std::uint8_t>(ControlModifier | ShiftModifier)}));
    CHECK(definition(Command::NextTab).shortcut ==
          (Shortcut{TAB, ControlModifier}));

    // Show stops at the ends; both cycles wrap.
    CHECK(tab_step_for(Command::ShowNextTab).delta == 1);
    CHECK(!tab_step_for(Command::ShowNextTab).wraps);
    CHECK(tab_step_for(Command::ShowPreviousTab).delta == -1);
    CHECK(tab_step_for(Command::CycleTabsForward).wraps);
    CHECK(tab_step_for(Command::NextTab).wraps);
    CHECK(tab_step_for(Command::PreviousTab).delta == -1);
    CHECK(tab_step_for(Command::SelectTab1).delta == 0);
    CHECK(tab_slot_for(Command::NextTab) == 0);

    const TabStep next{1, false}, prev{-1, false};
    const TabStep cycleNext{1, true}, cyclePrev{-1, true};
    CHECK(tab_index_after_step(0, 4, next) == 1);
    CHECK(tab_index_after_step(3, 4, next) == -1);      // end: stays put
    CHECK(tab_index_after_step(0, 4, prev) == -1);
    CHECK(tab_index_after_step(3, 4, cycleNext) == 0);  // end: wraps
    CHECK(tab_index_after_step(0, 4, cyclePrev) == 3);
    CHECK(tab_index_after_step(2, 4, cyclePrev) == 1);
    // One tab, no tab, or no current tab: nothing to land on.
    CHECK(tab_index_after_step(0, 1, cycleNext) == -1);
    CHECK(tab_index_after_step(0, 0, cycleNext) == -1);
    CHECK(tab_index_after_step(-1, 4, cycleNext) == -1);

    // AppKit spells Shift Tab as backtab; arrows are function-key characters.
    CHECK(native_key_equivalent(definition(Command::NextTab).shortcut) == "\t");
    CHECK(native_key_equivalent(definition(Command::PreviousTab).shortcut) == "\x19");
    CHECK(native_key_equivalent(definition(Command::CycleTabsForward).shortcut) ==
          "\xef\x9c\x83");
}

static void test_control_tab_is_the_only_commandless_chord() {
    using namespace hanabi::shortcuts;
    using namespace afterhours::keys;
    auto bindings = defaults();
    // Admitted for its own command, refused as a duplicate elsewhere.
    CHECK(validate(Command::NextTab, Shortcut{TAB, ControlModifier}, bindings).ok);
    const auto taken =
        validate(Command::OpenPalette, Shortcut{TAB, ControlModifier}, bindings);
    CHECK(!taken.ok);
    CHECK(taken.explanation.find("Next Tab") != std::string::npos);
    // Nothing else drops Command: not Ctrl on a letter, not Opt on Tab.
    CHECK(!validate(Command::OpenPalette, Shortcut{A, ControlModifier}, bindings).ok);
    CHECK(!validate(Command::NextTab,
                    Shortcut{TAB, static_cast<std::uint8_t>(ControlModifier | OptionModifier)},
                    bindings).ok);
    CHECK(!validate(Command::NextTab, Shortcut{TAB, 0}, bindings).ok);
    // Cmd Tab stays the system's.
    CHECK(!validate(Command::NextTab, Shortcut{TAB, CommandModifier}, bindings).ok);
}

static void test_archive_current_conversation_ships_without_a_key() {
    using namespace hanabi::shortcuts;
    using namespace afterhours::keys;
    const auto& def = definition(Command::ArchiveCurrentConversation);
    CHECK(def.shortcut.empty());
    CHECK(def.section == "File");
    CHECK(def.title == "Archive Current Conversation");
    CHECK(display(def.shortcut) == "Unassigned");
    CHECK(native_key_equivalent(def.shortcut).empty());
    // A key the user records is held to the ordinary rules.
    auto bindings = defaults();
    CHECK(validate(Command::ArchiveCurrentConversation, Shortcut{E, CommandModifier},
                   bindings).ok);
    CHECK(!validate(Command::ArchiveCurrentConversation, Shortcut{E, 0}, bindings).ok);
    CHECK(!validate(Command::ArchiveCurrentConversation, Shortcut{W, CommandModifier},
                    bindings).ok);
    // Exactly one command ships unassigned.
    int unassigned = 0;
    for (const auto& item : kDefinitions)
        if (unassigned_by_default(item.command)) ++unassigned;
    CHECK(unassigned == 1);
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
    test_zoom_in_alone_takes_a_shifted_alias_on_its_default_binding();
    test_relative_tab_moves_follow_the_reference();
    test_control_tab_is_the_only_commandless_chord();
    test_archive_current_conversation_ships_without_a_key();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
