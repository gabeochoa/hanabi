#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "shortcuts.h"

namespace hanabi::globals {

enum class Slot : int { NewTask = 0, Palette = 1 };

inline constexpr std::size_t kSlotCount = 2;

struct Definition {
    Slot slot;
    std::string_view key;
    std::string_view title;
    std::string_view help;
    shortcuts::Shortcut shortcut;
};

inline constexpr std::array<Definition, kSlotCount> kDefinitions{{
    {Slot::NewTask, "global_new_task", "Start a task from anywhere",
     "Brings the app forward and opens a new task, whatever you were in.",
     shortcuts::Shortcut{afterhours::keys::N,
                         shortcuts::CommandModifier |
                             shortcuts::ShiftModifier}},
    {Slot::Palette, "global_palette", "Search from anywhere",
     "Brings the app forward and opens the command palette.",
     shortcuts::Shortcut{afterhours::keys::K,
                         shortcuts::CommandModifier |
                             shortcuts::ShiftModifier}},
}};

inline constexpr std::size_t index(Slot slot) {
    return static_cast<std::size_t>(slot);
}

inline constexpr const Definition& definition(Slot slot) {
    return kDefinitions[index(slot)];
}

inline constexpr int kVkA = 0;
inline constexpr int kVkS = 1;
inline constexpr int kVkD = 2;
inline constexpr int kVkF = 3;
inline constexpr int kVkH = 4;
inline constexpr int kVkG = 5;
inline constexpr int kVkZ = 6;
inline constexpr int kVkX = 7;
inline constexpr int kVkC = 8;
inline constexpr int kVkV = 9;
inline constexpr int kVkB = 11;
inline constexpr int kVkQ = 12;
inline constexpr int kVkW = 13;
inline constexpr int kVkE = 14;
inline constexpr int kVkR = 15;
inline constexpr int kVkY = 16;
inline constexpr int kVkT = 17;
inline constexpr int kVkO = 31;
inline constexpr int kVkU = 32;
inline constexpr int kVkI = 34;
inline constexpr int kVkP = 35;
inline constexpr int kVkL = 37;
inline constexpr int kVkJ = 38;
inline constexpr int kVkK = 40;
inline constexpr int kVkN = 45;
inline constexpr int kVkM = 46;
inline constexpr int kVkSpace = 49;

inline std::optional<int> carbon_key(int key) {
    using namespace afterhours::keys;
    switch (key) {
        case A: return kVkA;
        case B: return kVkB;
        case C: return kVkC;
        case D: return kVkD;
        case E: return kVkE;
        case F: return kVkF;
        case G: return kVkG;
        case H: return kVkH;
        case I: return kVkI;
        case J: return kVkJ;
        case K: return kVkK;
        case L: return kVkL;
        case M: return kVkM;
        case N: return kVkN;
        case O: return kVkO;
        case P: return kVkP;
        case Q: return kVkQ;
        case R: return kVkR;
        case S: return kVkS;
        case T: return kVkT;
        case U: return kVkU;
        case V: return kVkV;
        case W: return kVkW;
        case X: return kVkX;
        case Y: return kVkY;
        case Z: return kVkZ;
        case SPACE: return kVkSpace;
        default: return std::nullopt;
    }
}

inline constexpr std::uint32_t kCarbonCmd = 1u << 8;    // cmdKey
inline constexpr std::uint32_t kCarbonShift = 1u << 9;  // shiftKey
inline constexpr std::uint32_t kCarbonOption = 1u << 11; // optionKey
inline constexpr std::uint32_t kCarbonControl = 1u << 12; // controlKey

inline std::uint32_t carbon_modifiers(std::uint8_t modifiers) {
    std::uint32_t out = 0;
    if (modifiers & shortcuts::CommandModifier) out |= kCarbonCmd;
    if (modifiers & shortcuts::ShiftModifier) out |= kCarbonShift;
    if (modifiers & shortcuts::OptionModifier) out |= kCarbonOption;
    if (modifiers & shortcuts::ControlModifier) out |= kCarbonControl;
    return out;
}

inline constexpr std::uint8_t kRequiredAny = shortcuts::CommandModifier |
                                             shortcuts::ControlModifier |
                                             shortcuts::OptionModifier;

struct Validation {
    bool ok = false;
    std::string explanation;
};

inline Validation validate(Slot slot, shortcuts::Shortcut candidate,
                           shortcuts::Shortcut other) {
    if (candidate.empty())
        return {false, "That is not a shortcut this app can register."};
    if (!carbon_key(candidate.key))
        return {false,
                "That key cannot be a global shortcut. Try a letter or the "
                "space bar."};
    if ((candidate.modifiers & kRequiredAny) == 0)
        return {false, "A global shortcut needs Command, Control or Option."};
    if (candidate == other)
        return {false, std::string("Already used by ") +
                           std::string(definition(slot == Slot::NewTask
                                                      ? Slot::Palette
                                                      : Slot::NewTask)
                                           .title) +
                           "."};
    return {true, {}};
}

struct Request {
    shortcuts::Shortcut shortcut;
    bool enabled = true;
};

using Requests = std::array<Request, kSlotCount>;

inline Requests defaults() {
    Requests out;
    for (const auto& item : kDefinitions)
        out[index(item.slot)] = Request{item.shortcut, true};
    return out;
}

}  // namespace hanabi::globals
