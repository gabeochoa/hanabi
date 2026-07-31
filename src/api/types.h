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

// High-signal attention state of a thread. This is the single notion the UI
// uses to decide whether a row shouts (dot + bold) or stays calm. It is
// deliberately backend-agnostic: an adapter maps whatever a real service
// reports into one of these. The mock supplies a spread of states so the UI
// has something real to render; the http adapter simply leaves it Unknown.
//
//   Attention  — DONE or WAITING-ON-YOU. The only state that earns a dot+bold.
//   Ready      — agent-verified, ready for the user to review (no test step).
//   Running    — self-running / in progress. Dimmed and quiet, never nudges.
//   Parked     — muted. Greyed, never counts, never nudges.
//   Archived   — retired. Greyed, low-signal.
//   Unknown    — no state info (default for the generic http adapter).
enum class ThreadState {
    Unknown,
    Attention,
    Ready,
    Running,
    Parked,
    Archived,
};

// At most one tag chip is shown per row, and only when relevant.
enum class ThreadTag {
    None,
    Blocked,
    Review,
    Done,
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

    // High-signal attention model (see ThreadState / ThreadTag above). The
    // mock backend populates these; the generic http adapter leaves them at
    // their defaults so it degrades to a plain, calm list.
    ThreadState state = ThreadState::Unknown;
    ThreadTag tag = ThreadTag::None;
    // Optional user-defined folder this thread is filed under ("" = none).
    std::string folder;
    // User-pinned to the top / Starred view.
    bool starred = false;
};

// A sub-agent (child worker) running under a session. Visualized ONLY in the
// transcript sub-agent panel (never the sidebar), per docs/decisions.md.
enum class SubAgentState {
    Running,
    Done,
    Blocked,
};

struct SubAgent {
    std::string id;
    std::string title;
    SubAgentState state = SubAgentState::Running;
    std::string note;
};

// A full session: summary + ordered transcript.
struct Session {
    SessionSummary summary;
    std::vector<Message> messages;
    // Optional child workers. Empty for most sessions; the transcript panel
    // falls back to deriving steps from Tool-role messages when this is empty.
    std::vector<SubAgent> sub_agents;
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
