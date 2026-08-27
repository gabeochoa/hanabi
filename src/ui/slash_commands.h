#pragma once

// ---------------------------------------------------------------------------
// The composer's slash-command vocabulary: the registry, the parser, and the
// prefix filter the menu renders. Graphics-free on purpose — the parse rules
// are asserted in tests/unit/test_data.cpp, where no window exists.
//
// WHAT EACH VERB MEANS HERE, and why only one of them runs today. The verbs
// come from the agentcloud wire, and the orchestrator advertises what an
// attached client may exercise in `hello.capabilities`
// (fbcode/agentcloud/orchestrator/webserver/src/chat.rs): `rename_v1`,
// `fork_with_prompt_v1`, `tuning_v1`, `queue_item_ops_v1`, `halt_v1` and the
// rest. Two things follow from reading that list against this client:
//
//   * `/btw` is `fork_with_prompt_v1` — a server-side fork that seeds a child
//     with the question AND carries the parent's context. hanabi has no fork
//     call. create_session, which kickoff already uses, makes an UNRELATED
//     session with no parent and no history, so routing /btw into it would
//     ship the wrong thing under the right name.
//   * `/compact` is the session `compact` command (agentcloud spec 155/314).
//     It is NOT capability-advertised, and hanabi's Client has no compact
//     verb at all — only the compaction BUDGET the context meter measures
//     against.
//
// `/new` opens the new-task sheet. `/model` and `/effort` open the same live
// pickers as the composer chips. `/btw` and `/compact` remain visible but
// explicitly unavailable because this client has neither wire operation.
// ---------------------------------------------------------------------------

#include <branding.h>
#include <string>
#include <string_view>
#include <vector>

namespace hanabi::slash {

struct Command {
    // The verb, with no leading slash.
    std::string_view name;
    // Placeholder for the argument, or empty when the verb takes none.
    std::string_view arg;
    // One line, shown right of the name in the menu.
    std::string_view blurb;
    // Whether this client can actually carry the command out today.
    bool runnable = false;
    // Shown when it cannot: what is missing, in the user's words.
    std::string unwired;
};

inline const std::vector<Command>& all() {
    static const std::vector<Command> kCommands = {
        {"new", "", "start a new conversation", true, ""},
        {"model", "", "choose the default model", true, ""},
        {"effort", "", "choose the thinking effort", true, ""},
        {"btw", "<question>", "fork this thread", false,
         std::string(product_branding::kAppName) + " has no fork call yet"},
        {"compact", "", "compact the context now", false,
         std::string(product_branding::kAppName) + " has no compact call yet"},
    };
    return kCommands;
}

struct Parsed {
    // False for anything that is not a slash command at all.
    bool matched = false;
    // The verb with no slash, lowercased. Empty for a bare "/".
    std::string verb;
    // Everything after the first run of spaces, verbatim.
    std::string args;
    // Whether the verb is one this client knows.
    bool known = false;
};

inline std::string lowered(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

inline const Command* find(std::string_view verb) {
    const std::string v = lowered(verb);
    for (const Command& c : all())
        if (c.name == v) return &c;
    return nullptr;
}

// A draft is a command only when the slash is the FIRST character: a message
// that merely mentions a path is not a command, and neither is "/ btw".
inline bool is_command_text(std::string_view draft) {
    return !draft.empty() && draft.front() == '/';
}

inline Parsed parse(std::string_view draft) {
    Parsed p;
    if (!is_command_text(draft)) return p;
    const std::string_view rest = draft.substr(1);
    const size_t sp = rest.find(' ');
    const std::string_view verb = rest.substr(0, sp);
    // "/ btw" has an empty verb followed by text: a slash and a space is a
    // sentence, not a command.
    if (verb.empty() && sp != std::string_view::npos) return p;
    p.matched = true;
    p.verb = lowered(verb);
    if (sp != std::string_view::npos) {
        std::string_view args = rest.substr(sp + 1);
        while (!args.empty() && args.front() == ' ') args.remove_prefix(1);
        p.args = std::string(args);
    }
    p.known = find(p.verb) != nullptr;
    return p;
}

// The menu's rows for a draft: every command whose name starts with what has
// been typed so far. A draft that has reached its argument (there is a space)
// has stopped choosing a command, so the menu has nothing to offer.
inline std::vector<const Command*> filter(std::string_view draft) {
    std::vector<const Command*> out;
    const Parsed p = parse(draft);
    if (!p.matched || draft.find(' ') != std::string_view::npos) return out;
    for (const Command& c : all())
        if (c.name.rfind(p.verb, 0) == 0) out.push_back(&c);
    return out;
}

// What the field should hold once a row is chosen: a verb that takes an
// argument leaves the caret after a space, one that does not is complete.
inline std::string completion(const Command& c) {
    std::string text = "/" + std::string(c.name);
    if (!c.arg.empty()) text += " ";
    return text;
}

}  // namespace hanabi::slash
