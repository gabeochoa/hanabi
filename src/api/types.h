#pragma once

// Domain model for the Hanabi client.
//
// These types are intentionally backend-agnostic. Nothing here names or
// encodes any particular service's wire format — an adapter (see client.h)
// is responsible for mapping whatever the configured backend returns into
// these plain structs. The rest of the app only ever sees these.

#include <cstdint>
#include <optional>
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

// Local-first sync state for anything with a "on this device" vs "confirmed on
// the server" distinction (a sent message, a queued/outbox item). Drives a
// small trailing glyph (WhatsApp-style checks) so the user always knows whether
// their work is merely LOCAL or actually PERSISTED to the backend.
//   None        — not applicable (e.g. a message that came FROM the server on
//                 load — it's inherently synced; we don't badge those).
//   LocalOnly   — saved on this device only (in the local outbox), not yet sent.
//   Persisting  — send in flight to the server.
//   Synced      — confirmed accepted by the server.
//   Failed      — send failed; held locally, will retry.
enum class SyncState {
    None,
    LocalOnly,
    Persisting,
    Synced,
    Failed,
};

// One message inside a session transcript.
struct Message {
    std::string id;
    Role role = Role::Assistant;
    std::string text;
    // Unix epoch seconds. 0 means "unknown".
    int64_t created_at = 0;
    // Optional short tag for tool/system messages (e.g. a tool name). For a
    // Role::Tool message this carries the TOOL NAME (e.g. "bash", "ipython") —
    // the transcript's tool-row renderer keys its label + wrench icon off it.
    std::string subtitle;

    // --- Real tool-call metadata (Role::Tool messages) --------------------
    // The generic http adapter emits these for Role::Tool messages split out
    // of an assistant message's interleaved blocks (see http_client.cpp's
    // block-splitting parser). They carry REAL values from the backend so the
    // tool-row renderer can show the true output / status / duration instead
    // of hashing plausible-looking fakes from the id. Append-only fields: a
    // renderer that doesn't read them is unaffected; the mock leaves them
    // empty/zero (its Tool messages already read fine from subtitle+text).
    //   tool_result       — the tool's captured output (for the nested sub-row)
    //   tool_status        — "completed" / "failed" / "" (drives the check mark)
    //   tool_duration_ms   — completedAt - startedAt in ms; 0 = unknown
    //   tool_node          — the node/host the tool ran on (from tool input),
    //                        e.g. "cli:aspen"; empty = unknown (renderer shows
    //                        nothing rather than a fabricated node).
    std::string tool_result;
    std::string tool_status;
    int64_t tool_duration_ms = 0;
    std::string tool_node;

    // Optional inline image (agent surface): a LOCAL filesystem path to an
    // image the agent produced/attached (e.g. a screenshot). When set, the
    // transcript renders the decoded image inline under the message text at
    // column width. Empty = no image (the common case). Kept a local path
    // (not a URL) so rendering never blocks on the network; a future adapter
    // can download a remote image to the cache dir and set this.
    std::string image_path;

    // Local-first sync badge (see SyncState). Defaults to None: a message
    // parsed from the server on transcript load is inherently synced and shows
    // no badge. A locally-authored message (optimistic send / outbox) sets this
    // to LocalOnly → Persisting → Synced/Failed so the transcript shows a
    // WhatsApp-style check reflecting whether the work has reached the server.
    SyncState sync = SyncState::None;
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
    // Machine-local archive overlay; unset means "whatever `state` says".
    // The per-viewer route that would carry archive to the server is not
    // reachable from this client, so the local answer is the only one there
    // is — and it has to be able to say NOT archived about a thread the
    // backend reports archived, or Unarchive could never mean anything.
    std::optional<bool> archive_override;
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

// Token accounting for one session, as the BACKEND reports it. Every field is
// the provider's own number; nothing here is estimated or derived.
//
// `budget_tokens` is the denominator, and it is deliberately the compaction
// budget rather than the model's context window. Hitting the budget is what
// summarises the thread out from under the reader; the window is trivia.
//
// `stale` means the reading predates content the server has not accounted for
// yet. It is rendered, not hidden — a stale number presented as live is a lie
// with a decimal point on it.
//
// Defaults mean "the backend said nothing", which is how an adapter that
// reports no accounting degrades to a plain figure with no bar.
struct ContextUsage {
    int64_t used_tokens = -1;
    int64_t budget_tokens = -1;
    bool stale = false;

    [[nodiscard]] bool counted() const { return used_tokens >= 0; }
    [[nodiscard]] bool has_denominator() const { return budget_tokens > 0; }
};

// A full session: summary + ordered transcript.
struct Session {
    SessionSummary summary;
    std::vector<Message> messages;
    // Optional child workers. Empty for most sessions; the transcript panel
    // falls back to deriving steps from Tool-role messages when this is empty.
    std::vector<SubAgent> sub_agents;

    // MEMORY-LIGHT WINDOW: true when this transcript is only the NEWEST N
    // messages and OLDER messages exist that were not loaded (parsed from the
    // backend's "hasMore" flag on a ?limit=N fetch). The UI reads this to show
    // a "load older" affordance at the top; when the full transcript is loaded
    // (no limit) it is false. Defaults false so a full/mock transcript reads as
    // complete.
    bool has_more_older = false;

    // Token accounting from the backend (see ContextUsage). Left at its
    // defaults by any adapter that reports none.
    ContextUsage context;
};

// User/account settings read back from the backend, so the app can verify it
// is set up correctly (feature #4 — "read the settings from the api"). These
// fields are backend-agnostic and OPTIONAL: an adapter fills whatever the
// configured endpoint reports and leaves the rest at defaults. `ok`
// distinguishes "fetched real settings" from a default-constructed value. On
// the real backend the endpoint is GET /whoami — {userId, bankId, counts:
// {sessions, assets, schedules, authoredSkills}} — which maps onto these
// fields; a backend with a different shape just populates a different subset.
struct UserSettings {
    bool ok = false;              // true once a real fetch populated this
    std::string user_id;          // account identity (e.g. userId)
    std::string bank_id;          // memory/bank identity (e.g. bankId)
    int64_t session_count = -1;   // counts.sessions   (-1 = unknown)
    int64_t asset_count = -1;     // counts.assets
    int64_t schedule_count = -1;  // counts.schedules
    int64_t skill_count = -1;     // counts.authoredSkills
    // The model's context window, in tokens, when the backend reports one.
    // Nothing this repo talks to reports it, and the context meter no longer
    // reads it: the meter's denominator is the COMPACTION BUDGET (see
    // ContextUsage), because the budget is what summarises a thread, and the
    // window is trivia beside that. Kept because the settings screen shows
    // whatever the backend sends.
    int64_t context_window_tokens = -1;
    // Raw JSON of the settings response, verbatim, for the settings screen to
    // show / debug the exact backend payload without re-fetching.
    std::string raw_json;
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
