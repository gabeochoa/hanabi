#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hanabi::triage {

enum class Command { Next, Previous, Open, Archive, Pin, Search, GoHome, GoPinned, GoArchived, Count };

struct Definition {
    Command command;
    std::string_view key;
    std::string_view label;
    std::string_view detail;
    std::string_view shipped;
};

inline constexpr std::array<Definition, static_cast<std::size_t>(Command::Count)> kDefinitions{{
    {Command::Next, "next", "Next thread", "Moves the cursor down the thread list", "j"},
    {Command::Previous, "previous", "Previous thread", "Moves the cursor up the thread list", "k"},
    {Command::Open, "open", "Open thread", "Opens the thread under the cursor", "return"},
    {Command::Archive, "archive", "Archive thread", "Archives the thread under the cursor", "e"},
    {Command::Pin, "pin", "Pin or unpin thread", "Pins or unpins the thread under the cursor", "s"},
    {Command::Search, "search", "Search threads", "Opens the thread search", "/"},
    {Command::GoHome, "go_home", "Go to Home", "Shows the Home shelf", "g i"},
    {Command::GoPinned, "go_pinned", "Go to Pinned", "Shows the Pinned shelf", "g s"},
    {Command::GoArchived, "go_archived", "Go to Archived", "Shows the Archived shelf", "g e"},
}};

inline const Definition& definition(Command c) {
    return kDefinitions[static_cast<std::size_t>(c)];
}

inline const Definition* find(std::string_view key) {
    for (const Definition& d : kDefinitions)
        if (d.key == key) return &d;
    return nullptr;
}

struct Binding {
    std::string first;
    std::string second;
    bool chorded() const { return !second.empty(); }
    bool empty() const { return first.empty(); }
    bool operator==(const Binding&) const = default;
};

inline Binding parse_binding(std::string_view spec) {
    Binding b;
    std::size_t i = 0;
    while (i < spec.size() && spec[i] == ' ') ++i;
    const std::size_t s1 = i;
    while (i < spec.size() && spec[i] != ' ') ++i;
    b.first = std::string(spec.substr(s1, i - s1));
    while (i < spec.size() && spec[i] == ' ') ++i;
    const std::size_t s2 = i;
    while (i < spec.size() && spec[i] != ' ') ++i;
    b.second = std::string(spec.substr(s2, i - s2));
    return b;
}

inline std::string serialize_binding(const Binding& b) {
    if (b.empty()) return {};
    return b.chorded() ? b.first + " " + b.second : b.first;
}

inline Binding shipped(Command c) { return parse_binding(definition(c).shipped); }

struct Resolved {
    Command command = Command::Count;
    bool pending_prefix = false;
    bool matched() const { return command != Command::Count; }
};

template <typename BindingOf>
Resolved resolve(std::string_view key, std::string_view pending, BindingOf binding_of) {
    Resolved out;
    if (!pending.empty()) {
        for (const Definition& d : kDefinitions) {
            const Binding b = binding_of(d.command);
            if (b.chorded() && b.first == pending && b.second == key) {
                out.command = d.command;
                return out;
            }
        }
        return out;
    }
    for (const Definition& d : kDefinitions) {
        const Binding b = binding_of(d.command);
        if (b.empty() || b.first != key) continue;
        if (b.chorded()) {
            out.pending_prefix = true;
            continue;
        }
        out.command = d.command;
        return out;
    }
    return out;
}

inline std::string display(const Binding& b) {
    if (b.empty()) return "Unassigned";
    const auto word = [](const std::string& k) -> std::string {
        if (k == "return") return "Return";
        if (k == "/") return "/";
        if (k.size() == 1 && k[0] >= 'a' && k[0] <= 'z') return std::string(1, static_cast<char>(k[0] - 'a' + 'A'));
        return k;
    };
    return b.chorded() ? word(b.first) + " then " + word(b.second) : word(b.first);
}

}
