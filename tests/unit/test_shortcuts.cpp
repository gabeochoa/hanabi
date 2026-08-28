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

int main() {
    test_defaults_are_unique_and_valid();
    test_serialization_round_trips();
    test_conflicts_name_the_owner();
    test_reserved_chords_explain_why();
    test_safe_custom_chord_is_accepted();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
