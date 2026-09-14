#pragma once

// The harness a session runs on, as the wire spells it (agentcloud
// HarnessKind: native / claude_code / codex / muse_code) and as the reference
// names it on screen (Vocabulary.swift HarnessKind.displayName). A token this
// build does not know is shown as itself rather than guessed at.

#include <string>
#include <string_view>

namespace hanabi::harness {

inline std::string display_name(std::string_view token) {
    if (token == "native") return "Native";
    if (token == "claude_code") return "Claude Code";
    if (token == "codex") return "Codex";
    if (token == "muse_code") return "Muse Code";
    return std::string(token);
}

}  // namespace hanabi::harness
