#pragma once

// Saved templates for the composer's / menu (the reference's Puffin 0.8.9,
// `ComposerTemplates`). A template is a name and some text: typing `/name`
// (or picking its row) puts the text in the box and sends nothing, so a
// prompt the reader writes over and over is one keystroke away and still
// theirs to edit. Machine-local, kept in Settings.
//
// Pure: the rules (what a name may be, which templates a draft offers) live
// here so a unit test can hold them without the app.

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hanabi::templates {

struct Template {
    std::string name;  // without the slash, lowercase
    std::string text;
    bool operator==(const Template&) const = default;
};

inline constexpr std::size_t kNameMax = 32;
inline constexpr std::size_t kMaxTemplates = 50;

// Why `name` cannot be saved, or "" when it can. `builtins` is the menu's
// own verbs: a template can never shadow one, because `/new` must keep
// meaning what it says. Names are what the reader types after the slash, so
// they are the same alphabet verbs use.
inline std::string refusal(std::string_view name, std::string_view text,
                           const std::vector<Template>& existing,
                           const std::vector<std::string_view>& builtins) {
    if (name.empty()) return "Give the template a name.";
    if (name.size() > kNameMax)
        return "A template name is at most " + std::to_string(kNameMax) +
               " characters.";
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                        c == '-' || c == '_';
        if (!ok)
            return "Use lowercase letters, digits, - and _ in a template name.";
    }
    for (const std::string_view b : builtins)
        if (b == name)
            return "/" + std::string(name) + " is a built-in command.";
    for (const Template& t : existing)
        if (t.name == name)
            return "A template called /" + std::string(name) + " already exists.";
    if (std::all_of(text.begin(), text.end(),
                    [](char c) { return c == ' ' || c == '\n' || c == '\t'; }))
        return "A template needs some text.";
    if (existing.size() >= kMaxTemplates)
        return "That is " + std::to_string(kMaxTemplates) +
               " templates; remove one to add another.";
    return {};
}

// A name as the field gave it: trimmed, a leading slash dropped, lowercased.
inline std::string normalized_name(std::string_view raw) {
    while (!raw.empty() && (raw.front() == ' ' || raw.front() == '/'))
        raw.remove_prefix(1);
    while (!raw.empty() && raw.back() == ' ') raw.remove_suffix(1);
    std::string out(raw);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

// The templates a draft offers: while the draft is still choosing (a slash
// first, no space yet), every template whose name starts with what follows
// the slash. A draft that has reached an argument offers none.
inline std::vector<const Template*> offered(std::string_view draft,
                                            const std::vector<Template>& all) {
    std::vector<const Template*> out;
    if (draft.empty() || draft.front() != '/' ||
        draft.find(' ') != std::string_view::npos)
        return out;
    std::string prefix(draft.substr(1));
    for (char& c : prefix)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    for (const Template& t : all)
        if (t.name.rfind(prefix, 0) == 0) out.push_back(&t);
    return out;
}

inline const Template* find(std::string_view verb, const std::vector<Template>& all) {
    for (const Template& t : all)
        if (t.name == verb) return &t;
    return nullptr;
}

// The row's description: the text's first line, which is what a reader
// recognises a prompt by.
inline std::string first_line(std::string_view text) {
    const std::size_t nl = text.find('\n');
    return std::string(nl == std::string_view::npos ? text : text.substr(0, nl));
}

}  // namespace hanabi::templates
