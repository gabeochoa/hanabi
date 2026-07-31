#pragma once

// Domain model for the Hanabi client.
//
// These types are intentionally backend-agnostic. Nothing here names or
// encodes any particular service's wire format — an adapter (see client.h)
// is responsible for mapping whatever the configured backend returns into
// these plain structs. The rest of the app only ever sees these.

#include <cstdint>
#include <string>
#include <vector>

namespace api {

// Role of a single message in a conversation transcript.
enum class Role {
    User,
    Assistant,
    System,
    Tool,
};

// One message inside a session transcript.
struct Message {
    std::string id;
    Role role = Role::Assistant;
    std::string text;
    // Unix epoch seconds. 0 means "unknown".
    int64_t created_at = 0;
    // Optional short tag for tool/system messages (e.g. a tool name).
    std::string subtitle;
};

// Lightweight summary of a session for the list view.
struct SessionSummary {
    std::string id;
    std::string title;
    // Unix epoch seconds of the most recent activity. 0 means "unknown".
    int64_t updated_at = 0;
    // "active" | "idle" | "archived" | "" (unknown). Kept as a free string so
    // the adapter can pass through whatever the backend reports.
    std::string status;
    // Optional preview snippet of the latest message.
    std::string preview;
};

// A full session: summary + ordered transcript.
struct Session {
    SessionSummary summary;
    std::vector<Message> messages;
};

// Result of a fetch. `ok == false` carries a human-readable error in `error`.
template <typename T>
struct Result {
    bool ok = false;
    T value{};
    std::string error;

    static Result success(T v) { return Result{true, std::move(v), ""}; }
    static Result failure(std::string e) { return Result{false, T{}, std::move(e)}; }
};

}  // namespace api
