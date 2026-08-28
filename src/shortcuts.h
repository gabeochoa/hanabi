#pragma once

#include <afterhours/src/core/key_codes.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace hanabi::shortcuts {

enum class Command : int {
    NewTask,
    CloseTab,
    ToggleSidebar,
    ToggleSplit,
    OpenSettings,
    OpenShortcuts,
    OpenPalette,
    FindInThread,
    FindNext,
    FindPrevious,
    SearchThreads,
    Count,
};

inline constexpr std::uint8_t CommandModifier = 1 << 0;
inline constexpr std::uint8_t ShiftModifier = 1 << 1;
inline constexpr std::uint8_t OptionModifier = 1 << 2;
inline constexpr std::uint8_t ControlModifier = 1 << 3;

struct Shortcut {
    int key = 0;
    std::uint8_t modifiers = 0;
    constexpr bool operator==(const Shortcut&) const = default;
    constexpr bool empty() const { return key == 0; }
};

struct Definition {
    Command command;
    std::string_view key;
    std::string_view title;
    std::string_view section;
    Shortcut shortcut;
    bool in_palette;
};

inline constexpr std::array<Definition,
                            static_cast<std::size_t>(Command::Count)>
    kDefinitions{{
        {Command::NewTask,
         "new_task",
         "New task",
         "File",
         {afterhours::keys::N, CommandModifier},
         true},
        {Command::CloseTab,
         "close_tab",
         "Close current tab",
         "File",
         {afterhours::keys::W, CommandModifier},
         true},
        {Command::ToggleSidebar,
         "toggle_sidebar",
         "Show or hide the sidebar",
         "View",
         {afterhours::keys::B, CommandModifier},
         true},
        {Command::ToggleSplit,
         "toggle_split",
         "Split the pane",
         "View",
         {afterhours::keys::BACKSLASH, CommandModifier},
         true},
        {Command::OpenSettings,
         "open_settings",
         "Settings",
         "Application",
         {afterhours::keys::COMMA, CommandModifier},
         true},
        {Command::OpenShortcuts,
         "open_shortcuts",
         "Keyboard shortcuts",
         "Help",
         {afterhours::keys::SLASH, CommandModifier},
         true},
        {Command::OpenPalette,
         "open_palette",
         "Command palette",
         "View",
         {afterhours::keys::K, CommandModifier},
         true},
        {Command::FindInThread,
         "find_in_thread",
         "Find in this conversation",
         "View",
         {afterhours::keys::F, CommandModifier},
         true},
        {Command::FindNext,
         "find_next",
         "Find next match",
         "View",
         {afterhours::keys::G, CommandModifier},
         true},
        {Command::FindPrevious,
         "find_previous",
         "Find previous match",
         "View",
         {afterhours::keys::G,
          static_cast<std::uint8_t>(CommandModifier | ShiftModifier)},
         true},
        {Command::SearchThreads,
         "search_threads",
         "Search conversations",
         "View",
         {afterhours::keys::F,
          static_cast<std::uint8_t>(CommandModifier | ShiftModifier)},
         true},
    }};

using Bindings = std::array<Shortcut, kDefinitions.size()>;

inline constexpr std::size_t index(Command command) {
    return static_cast<std::size_t>(command);
}

inline constexpr const Definition& definition(Command command) {
    return kDefinitions[index(command)];
}

inline constexpr Bindings defaults() {
    Bindings out{};
    for (const auto& item : kDefinitions)
        out[index(item.command)] = item.shortcut;
    return out;
}

inline const Definition* definition_for_key(std::string_view key) {
    for (const auto& item : kDefinitions)
        if (item.key == key) return &item;
    return nullptr;
}

inline std::string key_name(int key) {
    using namespace afterhours::keys;
    if (key >= A && key <= Z) return std::string(1, static_cast<char>(key));
    if (key >= ZERO && key <= NINE)
        return std::string(1, static_cast<char>(key));
    switch (key) {
        case APOSTROPHE:
            return "'";
        case COMMA:
            return ",";
        case MINUS:
            return "-";
        case PERIOD:
            return ".";
        case SLASH:
            return "/";
        case SEMICOLON:
            return ";";
        case EQUAL:
            return "=";
        case LEFT_BRACKET:
            return "[";
        case BACKSLASH:
            return "\\";
        case RIGHT_BRACKET:
            return "]";
        case GRAVE:
            return "`";
        case SPACE:
            return "Space";
        case TAB:
            return "Tab";
        case ENTER:
            return "Return";
        case BACKSPACE:
            return "Delete";
        case DELETE_KEY:
            return "Forward Delete";
        case LEFT:
            return "Left";
        case RIGHT:
            return "Right";
        case UP:
            return "Up";
        case DOWN:
            return "Down";
        default:
            break;
    }
    if (key >= F1 && key <= F12) return "F" + std::to_string(key - F1 + 1);
    return "Unknown";
}

inline int key_from_token(std::string token) {
    std::transform(
        token.begin(), token.end(), token.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (token.size() == 1) {
        const unsigned char c = static_cast<unsigned char>(token[0]);
        if (c >= 'a' && c <= 'z') return afterhours::keys::A + c - 'a';
        if (c >= '0' && c <= '9') return afterhours::keys::ZERO + c - '0';
        switch (c) {
            case '\'':
                return afterhours::keys::APOSTROPHE;
            case ',':
                return afterhours::keys::COMMA;
            case '-':
                return afterhours::keys::MINUS;
            case '.':
                return afterhours::keys::PERIOD;
            case '/':
                return afterhours::keys::SLASH;
            case ';':
                return afterhours::keys::SEMICOLON;
            case '=':
                return afterhours::keys::EQUAL;
            case '[':
                return afterhours::keys::LEFT_BRACKET;
            case '\\':
                return afterhours::keys::BACKSLASH;
            case ']':
                return afterhours::keys::RIGHT_BRACKET;
            case '`':
                return afterhours::keys::GRAVE;
            default:
                break;
        }
    }
    if (token == "space") return afterhours::keys::SPACE;
    if (token == "tab") return afterhours::keys::TAB;
    if (token == "return") return afterhours::keys::ENTER;
    if (token == "delete") return afterhours::keys::BACKSPACE;
    if (token == "forward-delete") return afterhours::keys::DELETE_KEY;
    if (token == "left") return afterhours::keys::LEFT;
    if (token == "right") return afterhours::keys::RIGHT;
    if (token == "up") return afterhours::keys::UP;
    if (token == "down") return afterhours::keys::DOWN;
    if (token.size() >= 2 && token[0] == 'f') {
        const int n = std::atoi(token.c_str() + 1);
        if (n >= 1 && n <= 12) return afterhours::keys::F1 + n - 1;
    }
    return 0;
}

inline std::string display(Shortcut shortcut) {
    if (shortcut.empty()) return "Unassigned";
    std::string out;
    const auto add = [&out](std::string_view value) {
        if (!out.empty()) out += " ";
        out += value;
    };
    if (shortcut.modifiers & CommandModifier) add("Cmd");
    if (shortcut.modifiers & ControlModifier) add("Ctrl");
    if (shortcut.modifiers & OptionModifier) add("Opt");
    if (shortcut.modifiers & ShiftModifier) add("Shift");
    add(key_name(shortcut.key));
    return out;
}

inline std::string serialize(Shortcut shortcut) {
    if (shortcut.empty()) return "";
    std::string out;
    const auto add = [&out](std::string_view value) {
        if (!out.empty()) out += "+";
        out += value;
    };
    if (shortcut.modifiers & CommandModifier) add("cmd");
    if (shortcut.modifiers & ControlModifier) add("ctrl");
    if (shortcut.modifiers & OptionModifier) add("opt");
    if (shortcut.modifiers & ShiftModifier) add("shift");
    std::string key = key_name(shortcut.key);
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (key == "forward delete") key = "forward-delete";
    add(key);
    return out;
}

inline std::optional<Shortcut> parse(std::string_view encoded) {
    Shortcut out;
    std::size_t from = 0;
    while (from <= encoded.size()) {
        const std::size_t at = encoded.find('+', from);
        std::string token(encoded.substr(from, at == std::string_view::npos
                                                   ? encoded.size() - from
                                                   : at - from));
        std::transform(
            token.begin(), token.end(), token.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (token == "cmd")
            out.modifiers |= CommandModifier;
        else if (token == "ctrl")
            out.modifiers |= ControlModifier;
        else if (token == "opt")
            out.modifiers |= OptionModifier;
        else if (token == "shift")
            out.modifiers |= ShiftModifier;
        else {
            if (out.key != 0) return std::nullopt;
            out.key = key_from_token(token);
            if (out.key == 0) return std::nullopt;
        }
        if (at == std::string_view::npos) break;
        from = at + 1;
    }
    if (out.empty()) return std::nullopt;
    return out;
}

struct Validation {
    bool ok = false;
    std::string explanation;
};

inline Validation validate(Command command, Shortcut candidate,
                           const Bindings& bindings) {
    using namespace afterhours::keys;
    if (candidate.empty())
        return {false, "Press a letter, number, or supported key."};
    if ((candidate.modifiers & CommandModifier) == 0)
        return {
            false,
            "Include Command so normal typing and navigation stay unchanged."};

    const auto exact = [&](int key, std::uint8_t modifiers) {
        return candidate.key == key && candidate.modifiers == modifiers;
    };
    if (exact(Q, CommandModifier))
        return {false, "Cmd Q is reserved for Quit."};
    if (exact(Q, static_cast<std::uint8_t>(CommandModifier | ShiftModifier)) ||
        exact(Q, static_cast<std::uint8_t>(CommandModifier | ShiftModifier |
                                           OptionModifier)))
        return {false, "That chord is reserved for logging out of macOS."};
    if (exact(H, CommandModifier))
        return {false, "Cmd H is reserved for Hide."};
    if (exact(H, static_cast<std::uint8_t>(CommandModifier | OptionModifier)))
        return {false, "Opt Cmd H is reserved for Hide Others."};
    if (exact(M, CommandModifier))
        return {false, "Cmd M is reserved for Minimize."};
    if (exact(D, static_cast<std::uint8_t>(CommandModifier | OptionModifier)))
        return {false, "Opt Cmd D is reserved for the Dock."};
    if (exact(F, static_cast<std::uint8_t>(CommandModifier | ControlModifier)))
        return {false, "Ctrl Cmd F is reserved for full screen."};
    if (exact(SPACE, CommandModifier))
        return {false, "Cmd Space is reserved for Spotlight."};
    if (exact(SPACE,
              static_cast<std::uint8_t>(CommandModifier | ControlModifier)))
        return {false, "Ctrl Cmd Space is reserved for emoji and symbols."};
    if (exact(THREE,
              static_cast<std::uint8_t>(CommandModifier | ShiftModifier)) ||
        exact(FOUR,
              static_cast<std::uint8_t>(CommandModifier | ShiftModifier)) ||
        exact(FIVE, static_cast<std::uint8_t>(CommandModifier | ShiftModifier)))
        return {false, "That chord is reserved for macOS screenshots."};
    if (exact(TAB, CommandModifier))
        return {false, "Cmd Tab is reserved for switching apps."};
    if (exact(GRAVE, CommandModifier))
        return {false, "Cmd ` is reserved for switching windows."};
    if (exact(Q, static_cast<std::uint8_t>(CommandModifier | ControlModifier)))
        return {false, "Ctrl Cmd Q is reserved for locking the Mac."};
    if (exact(ESCAPE,
              static_cast<std::uint8_t>(CommandModifier | OptionModifier)))
        return {false, "Opt Cmd Esc is reserved for Force Quit."};
    if (exact(N, static_cast<std::uint8_t>(CommandModifier | ShiftModifier)) ||
        exact(K, static_cast<std::uint8_t>(CommandModifier | ShiftModifier)))
        return {false,
                "That chord is reserved by Hanabi's focus-gated desktop "
                "shortcuts."};
    if ((candidate.modifiers == CommandModifier &&
         (candidate.key == A || candidate.key == C || candidate.key == V ||
          candidate.key == X || candidate.key == Z)) ||
        exact(Z, static_cast<std::uint8_t>(CommandModifier | ShiftModifier)))
        return {false, "That chord is reserved for standard text editing."};

    for (const auto& item : kDefinitions) {
        if (item.command == command) continue;
        if (bindings[index(item.command)] == candidate)
            return {false, "Already used by " + std::string(item.title) + "."};
    }
    return {true, ""};
}

inline std::string native_key_equivalent(Shortcut shortcut) {
    using namespace afterhours::keys;
    if (shortcut.key >= A && shortcut.key <= Z)
        return std::string(1, static_cast<char>('a' + shortcut.key - A));
    if (shortcut.key >= ZERO && shortcut.key <= NINE)
        return std::string(1, static_cast<char>('0' + shortcut.key - ZERO));
    switch (shortcut.key) {
        case APOSTROPHE:
            return "'";
        case COMMA:
            return ",";
        case MINUS:
            return "-";
        case PERIOD:
            return ".";
        case SLASH:
            return "/";
        case SEMICOLON:
            return ";";
        case EQUAL:
            return "=";
        case LEFT_BRACKET:
            return "[";
        case BACKSLASH:
            return "\\";
        case RIGHT_BRACKET:
            return "]";
        case GRAVE:
            return "`";
        default:
            return "";
    }
}

}  // namespace hanabi::shortcuts
