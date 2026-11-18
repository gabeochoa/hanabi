#pragma once

// The notice kinds the server can put on a `notice` event, and how each
// reads in a transcript.
//
// A notice used to be one shape for every kind: the word "notice" in the
// alarm colour, then the message. That is the shape the reference had before
// it ruled on kinds, and it makes opposite news read the same -- a model
// declining to continue and a session quietly saying where its skills come
// from both wore red. The table below is the one place a wire kind becomes
// a title and a loudness, mirroring the reference's ruling for each kind.
//
// Tolerance is the other half. The parser keeps the wire word verbatim in
// the row's subtitle, and a word this build predates folds to Unknown here:
// it draws as the generic platform notice, one row, never a dropped frame.
// A future kind is therefore cheap until somebody rules on it, and ruling
// on it is adding a row to this table.
//
// The six kinds the reference added most recently (2026-11-16) are
// conversation_lost, ancestor_halted, mcp_roster_updated, memory_not_loaded,
// settings_switch and plugin_hook; delivery_failures_parked and
// directory_context_unavailable followed, and the reference itself draws
// those two as the generic platform notice, which is what they say here.

#include <string>
#include <string_view>

namespace api::notice_kinds {

enum class Kind {
    Refusal,
    ModelRetry,
    SkillDigestMismatch,
    SkillLoadFailed,
    NoReply,
    ApprovalTimeoutsParked,
    MediaWithheld,
    Components,
    BudgetBreached,
    ConversationLost,
    AncestorHalted,
    McpRosterUpdated,
    MemoryNotLoaded,
    SettingsSwitch,
    PluginHook,
    DeliveryFailuresParked,
    DirectoryContextUnavailable,
    Unknown,
};

// How loudly a notice reads. Alert is the alarm colour, Attention is the
// highlight ink, Informational is the faint ink: an observation with no
// action owed must not shout.
enum class Severity { Alert, Attention, Informational };

inline Kind from_wire(std::string_view wire) {
    if (wire == "refusal") return Kind::Refusal;
    if (wire == "model_retry") return Kind::ModelRetry;
    if (wire == "skill_digest_mismatch") return Kind::SkillDigestMismatch;
    if (wire == "skill_load_failed") return Kind::SkillLoadFailed;
    if (wire == "no_reply") return Kind::NoReply;
    if (wire == "approval_timeouts_parked") return Kind::ApprovalTimeoutsParked;
    if (wire == "media_withheld") return Kind::MediaWithheld;
    if (wire == "components") return Kind::Components;
    if (wire == "budget_breached") return Kind::BudgetBreached;
    if (wire == "conversation_lost") return Kind::ConversationLost;
    if (wire == "ancestor_halted") return Kind::AncestorHalted;
    if (wire == "mcp_roster_updated") return Kind::McpRosterUpdated;
    if (wire == "memory_not_loaded") return Kind::MemoryNotLoaded;
    if (wire == "settings_switch") return Kind::SettingsSwitch;
    if (wire == "plugin_hook") return Kind::PluginHook;
    if (wire == "delivery_failures_parked") return Kind::DeliveryFailuresParked;
    if (wire == "directory_context_unavailable") return Kind::DirectoryContextUnavailable;
    return Kind::Unknown;
}

inline std::string_view wire_name(Kind kind) {
    switch (kind) {
        case Kind::Refusal: return "refusal";
        case Kind::ModelRetry: return "model_retry";
        case Kind::SkillDigestMismatch: return "skill_digest_mismatch";
        case Kind::SkillLoadFailed: return "skill_load_failed";
        case Kind::NoReply: return "no_reply";
        case Kind::ApprovalTimeoutsParked: return "approval_timeouts_parked";
        case Kind::MediaWithheld: return "media_withheld";
        case Kind::Components: return "components";
        case Kind::BudgetBreached: return "budget_breached";
        case Kind::ConversationLost: return "conversation_lost";
        case Kind::AncestorHalted: return "ancestor_halted";
        case Kind::McpRosterUpdated: return "mcp_roster_updated";
        case Kind::MemoryNotLoaded: return "memory_not_loaded";
        case Kind::SettingsSwitch: return "settings_switch";
        case Kind::PluginHook: return "plugin_hook";
        case Kind::DeliveryFailuresParked: return "delivery_failures_parked";
        case Kind::DirectoryContextUnavailable: return "directory_context_unavailable";
        case Kind::Unknown: break;
    }
    return "";
}

inline bool is_known(std::string_view wire) {
    return from_wire(wire) != Kind::Unknown;
}

// The row's heading, written out rather than derived from the wire word:
// "model_retry" with its underscore swapped reads as a field name, and
// "Retrying" is what actually happened.
inline const char* title(Kind kind) {
    switch (kind) {
        case Kind::Refusal: return "Refused";
        case Kind::ModelRetry: return "Retrying";
        case Kind::SkillDigestMismatch: return "Skill changed on disk";
        case Kind::SkillLoadFailed: return "Skill could not load";
        case Kind::NoReply: return "No reply";
        case Kind::ApprovalTimeoutsParked: return "Parked after approval timeouts";
        case Kind::MediaWithheld: return "Older media withheld";
        case Kind::Components: return "Plugins not all loaded";
        case Kind::BudgetBreached: return "Budget reached";
        case Kind::ConversationLost: return "Conversation lost";
        case Kind::AncestorHalted: return "Ancestor halted";
        case Kind::McpRosterUpdated: return "Tool roster updated";
        case Kind::MemoryNotLoaded: return "Memory not loaded";
        case Kind::SettingsSwitch: return "Skills from settings";
        case Kind::PluginHook: return "Plugin hook";
        case Kind::DeliveryFailuresParked:
        case Kind::DirectoryContextUnavailable:
        case Kind::Unknown: return "Platform notice";
    }
    return "Platform notice";
}

inline Severity severity(Kind kind) {
    switch (kind) {
        case Kind::Refusal: return Severity::Alert;
        case Kind::ApprovalTimeoutsParked: return Severity::Alert;
        case Kind::BudgetBreached: return Severity::Alert;
        case Kind::McpRosterUpdated: return Severity::Informational;
        case Kind::SettingsSwitch: return Severity::Informational;
        case Kind::ModelRetry:
        case Kind::SkillDigestMismatch:
        case Kind::SkillLoadFailed:
        case Kind::NoReply:
        case Kind::MediaWithheld:
        case Kind::Components:
        case Kind::ConversationLost:
        case Kind::AncestorHalted:
        case Kind::MemoryNotLoaded:
        case Kind::PluginHook:
        case Kind::DeliveryFailuresParked:
        case Kind::DirectoryContextUnavailable:
        case Kind::Unknown: return Severity::Attention;
    }
    return Severity::Attention;
}

// Whether a notice with this kind carries a heading of its own. The two
// generic kinds and anything unrecognised do not: the reference draws them
// as the plain platform notice, and inventing a heading from the wire word
// would put a field name in front of the reader.
inline bool has_own_title(Kind kind) {
    return kind != Kind::Unknown && kind != Kind::DeliveryFailuresParked &&
           kind != Kind::DirectoryContextUnavailable;
}

// The one line a notice row shows: its heading, then the server's message.
// The new kinds carry no separate detail line in the reference -- the
// message is the whole notice -- so nothing is read from an info bag here.
// An unrecognised kind keeps the old shape exactly: the message, or the
// wire word when there is no message, so a kind this build predates still
// reads instead of vanishing.
inline std::string display_line(std::string_view wire, const std::string& message) {
    const Kind kind = from_wire(wire);
    if (!has_own_title(kind)) {
        if (!message.empty()) return message;
        return wire.empty() ? std::string(title(kind)) : std::string(wire);
    }
    const std::string heading(title(kind));
    if (message.empty() || message == heading) return heading;
    return heading + " \xe2\x80\x94 " + message;
}

}  // namespace api::notice_kinds
