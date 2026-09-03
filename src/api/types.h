#pragma once

// Domain model for the Hanabi client.
//
// These types are intentionally backend-agnostic. Nothing here names or
// encodes any particular service's wire format — an adapter (see client.h)
// is responsible for mapping whatever the configured backend returns into
// these plain structs. The rest of the app only ever sees these.

#include <algorithm>
#include <cstdint>
#include <map>
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

// WHAT a transcript row is, as distinct from WHO authored it (Role).
//
// A session emits far more than four authors' worth of things. The wire's own
// vocabulary is ~90 event types (agentcloud's `SessionEvent`), and the ones a
// reader needs are not all speech: a skill being loaded, a node being
// attached, a child session being spawned and settling, a delivery the
// platform made into this session. With Role as the only axis, every one of
// them had to be somebody TALKING or be dropped, and they were dropped —
// "you are missing thinking and deliveries", "you are missing subagents",
// "you are missing nodes", "you are missing skills", all one root cause.
//
// Role stays what it is: the author, for the rows that have one. This says
// what the row IS, and the renderer keys its treatment off it. They are
// deliberately two fields — a Delivery is authored by the platform and a
// SubAgent row by the agent, and collapsing that into one enum would force
// every switch over Role (author label, bubble colour, tool piling, the find
// index) to grow cases for things that are not authors.
//
// APPEND-ONLY, same reason as ThreadState: these are persisted to the on-disk
// transcript cache as their integer values.
enum class EventKind {
    // Somebody said something: the row's text is the message. Role says who.
    Text,
    // The model's reasoning. Not the answer, and never presented as one.
    Thinking,
    // A tool call (Role::Tool). Its result folds back into the same row.
    ToolCall,
    // A child session this thread spawned, and how it ended.
    SubAgent,
    // A worker node attached, detached, granted or released.
    Node,
    // A skill loaded into the session.
    Skill,
    // Something the PLATFORM put into this session rather than the human: a
    // child's settlement, a peer session's message, a subscription firing.
    // Its own kind because a delivery reads as the user talking otherwise,
    // and it is not the user — nobody typed it.
    Delivery,
    // A server notice, and the agent's own status testimony.
    Notice,
    Status,
    // A durable event whose `type` this build does not know. Drawn as a muted
    // one-liner naming the wire tag rather than dropped, because a reader who
    // can see "this build cannot draw a X" can say so, and a reader looking
    // at a gap cannot.
    Unsupported,
    Plan,
    Goal,
};

// High-signal attention state of a thread. This is the single notion the UI
// uses to decide whether a row shouts (dot + bold) or stays calm. It is
// deliberately backend-agnostic: an adapter maps whatever a real service
// reports into one of these. The mock supplies a spread of states so the UI
// has something real to render; the http adapter simply leaves it Unknown.
//
//   Attention  — DONE or WAITING-ON-YOU. The only state that earns a dot+bold.
//   Ready      — agent-verified, ready for the user to review (no test step).
//   Running    — a run is LIVE right now (isProcessing / resolved kind
//                "running"). Dimmed and quiet, never nudges.
//   Working    — the agent's own last testimony said it was working, but NO
//                run is live. Distinct from Running because the two are
//                different facts and the list says so: a thread whose run has
//                ended must not wear a spinner. The wire carries both halves
//                separately — a `status.state` of "working" and a `running`
//                flag — and collapsing them was hanabi telling the reader a
//                run was live when it was not.
//   Parked     — muted. Greyed, never counts, never nudges.
//   Archived   — retired. Greyed, low-signal.
//   Unknown    — no state info (default for the generic http adapter). On a
//                thread that has a tag, this means the TAG is the whole of
//                what is known: nothing testified, only an outcome landed.
//
// APPEND-ONLY. These are persisted to the on-disk session cache as their
// integer values (disk_cache.cpp `{"state", static_cast<int>(...)}`), so
// inserting a member in the middle silently reinterprets every cached row —
// yesterday's Running reads back as today's Working. New members go at the
// end, whatever the reading order would prefer.
enum class ThreadState {
    Unknown,
    Attention,
    Ready,
    Running,
    Parked,
    Archived,
    Working,
};

// At most one tag chip is shown per row, and only when relevant.
//
// Failed is the fifth member of the vocabulary the backend actually speaks
// (`status.state` is one of working / waiting / blocked / done / failed —
// Puffin models the same five in `Wire/Vocabulary.swift:StatusState`).
// hanabi carried four of them and folded failure into Blocked, which made a
// run that DIED read as a run that is waiting for you: the same row, the same
// glyph, the opposite ask. A failed thread still rides in the Blocked smart
// view (see ecs::model::in_blocked_view) — Puffin puts it there too — it just
// stops claiming to be a decision you owe someone.
//
// Waiting is the sixth, and it is the half of `waiting` that is not Review.
// The backend's five status words are working / waiting / blocked / done /
// failed, and hanabi folded `waiting` into Blocked unless it also carried an
// explicit review flag — so a thread waiting for an ANSWER and a thread whose
// run is blocked wore the same mark. Puffin 0.6.5 split them (IconTable's
// `blocked` and `review` share a shape and differ in colour), and this is the
// same split one layer down: both still want the reader, and the mark says
// which kind of wanting it is.
//
// APPEND-ONLY, for the same reason ThreadState is.
enum class ThreadTag {
    None,
    Blocked,
    Review,
    Done,
    Failed,
    Waiting,
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

    // The RUN this message closed, as the backend's own word for how it ended
    // ("completed", "failed", "interrupted", ...) — empty on every message
    // that did not end a run, which is nearly all of them. The transcript
    // draws a rule with that word centred in it below the message.
    //
    // A run ending is an EVENT, not a message: the backend reports it as its
    // own row (Puffin's `runFinished`, which `AgentcloudTranscriptView`
    // renders through `runSeparator`). Modelling it as a fifth Role would have
    // forced every switch over Role — author label, bubble colour, tool
    // piling, the find index — to grow a case for something that is not an
    // author and has no text, so it rides on the message it follows instead.
    // The cost of that choice is that a run which produced NO message cannot
    // be drawn at all; nothing in the mock or either adapter emits one today,
    // and the alternative was worse.
    //
    // Free string rather than an enum for the same reason Puffin keeps it one:
    // the divider prints the word the server said, so an outcome this build
    // predates still reads instead of rendering as "unknown".
    std::string run_outcome;

    // What this row IS (see EventKind). LAST, and defaulted, because the mock
    // builds Messages by aggregate initialization — a member inserted above
    // this line silently reinterprets every one of those literals.
    EventKind kind = EventKind::Text;
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
    // Silenced on THIS machine: the thread still changes state and still shows
    // it in the list, it just never raises a notification. Deliberately not
    // synced — which machine you want quiet on is a property of the machine.
    bool muted = false;

    // --- Sub-agents, as a COUNT the list row can draw --------------------
    // How many child sessions this thread spawned, and how many of those are
    // still working. Two integers rather than the children themselves,
    // because the sidebar only ever renders "1" or "1/3" and holding a vector
    // per row would put the whole child catalog behind every list refresh.
    //
    // The pair, not a single number, is what makes the row readable: the
    // denominator only appears when some but not all children are live (see
    // ecs::model::sub_agent_label), and the live/settled distinction is what
    // the colour encodes. A single "count" could not say either.
    //
    // BOTH backends fill these. The mock counts Session::sub_agents. The
    // agentcloud adapter folds them out of the session list itself: every
    // child session is its own row in the `list` reply carrying a `parent`
    // session id (the durable parent link — agentcloud spec 024, projected
    // into the catalog by spec 036 DEC-9), so the adapter tallies children
    // onto their parent and drops the child rows. The generic http adapter
    // reports no parentage and leaves both at 0, which renders as no column.
    int sub_agent_count = 0;
    int sub_agent_running_count = 0;

    std::string parent_id;
    std::string forked_from;

    // --- Brakes the SERVER holds ------------------------------------------
    //
    //   frozen  — `frozen: {by, reason}` on the LIST ROW and on the attach
    //             greeting both (WireSessionSummary and WireState each carry
    //             it), which is what lets the list mark it without opening
    //             anything. Serve-time derived over the containment chain, so
    //             `frozen_by` is the SESSION at the root of the freeze and is
    //             not this thread when an ancestor is the one frozen. It
    //             outranks every other mark: a frozen thread can be running,
    //             and nothing the reader does changes that. The object's
    //             PRESENCE is the brake and `by`/`reason` are display metadata
    //             only, so a malformed `{}` still freezes -- fail closed.
    //   paused  — `channel_replies_paused`. ATTACH ONLY: it is a `WireState`
    //             key and the list row does not carry it, so this is known
    //             for a thread that has been opened and for no other. Copied
    //             onto the catalog row on load
    //             (AppComponent::apply_attach_brakes) so the mark survives the
    //             pane it was learned in -- everything that decides what the
    //             UI does reads the catalog row, not the pane's own copy.
    bool frozen = false;
    std::string frozen_by;
    std::string frozen_reason;
    bool replies_paused = false;

    // `archived_at_unix_ms` on the list row, the server's own archive stamp
    // (0 = unarchived). Held apart from `archive_override` — that one is this
    // Mac's answer and must still be able to say NOT archived about a thread
    // the server has archived — and REPLACED, never merged, on every refresh:
    // a row that lost its stamp was unarchived somewhere else.
    int64_t server_archived_at_ms = 0;
};

// A sub-agent (child worker) running under a session. The transcript's
// sub-agent panel renders these in full; the sidebar renders only their COUNT
// (see SessionSummary::sub_agent_count), never the sub-agents themselves.
enum class SubAgentState {
    Running,
    Done,
    Blocked,
    Failed,
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

struct SessionPlanStep {
    enum class Status {
        Pending,
        InProgress,
        Completed,
        Cancelled,
        Unknown,
    };

    std::string id;
    std::string text;
    std::string note;
    Status status = Status::Unknown;
};

struct SessionPlan {
    std::string title;
    std::vector<SessionPlanStep> steps;
    int64_t revision = 0;

    [[nodiscard]] int completed() const {
        int total = 0;
        for (const auto& step : steps)
            if (step.status == SessionPlanStep::Status::Completed) ++total;
        return total;
    }

    [[nodiscard]] const SessionPlanStep* current() const {
        for (const auto& step : steps)
            if (step.status == SessionPlanStep::Status::InProgress) return &step;
        return nullptr;
    }

    [[nodiscard]] bool finished() const {
        if (steps.empty()) return false;
        for (const auto& step : steps)
            if (step.status != SessionPlanStep::Status::Completed &&
                step.status != SessionPlanStep::Status::Cancelled)
                return false;
        return true;
    }

    [[nodiscard]] bool has_cancelled() const {
        for (const auto& step : steps)
            if (step.status == SessionPlanStep::Status::Cancelled) return true;
        return false;
    }

    [[nodiscard]] std::string chip_label() const {
        if (finished()) return has_cancelled() ? "Plan cancelled" : "Plan complete";
        return "Plan " + std::to_string(completed()) + "/" +
               std::to_string(steps.size());
    }
};

enum class GoalPhase {
    Active,
    Paused,
    Blocked,
    Completed,
    Cleared,
    Unknown,
};

struct SessionGoal {
    std::string objective;
    std::string done_when;
    GoalPhase phase = GoalPhase::Unknown;
    std::string note;
    std::string set_by;
    int64_t revision = 0;
};

enum class AskKind {
    Form,
    Approval,
};

enum class AskControl {
    Text,
    Single,
    Multi,
    File,
};

enum class AskValueType {
    String,
    Number,
    Integer,
    Boolean,
};

enum class AskAction {
    Accept,
    Decline,
    Cancel,
};

struct AskOption {
    std::string value;
    std::string label;
    std::string detail;
};

struct AskQuestion {
    std::string key;
    std::string prompt;
    AskControl control = AskControl::Text;
    AskValueType value_type = AskValueType::String;
    std::vector<AskOption> options;
    std::string free_text_key;
    std::string free_text_label;
};

struct PendingAsk {
    uint64_t seq = 0;
    std::string owner_session;
    std::string child_session;
    std::string tool;
    std::string message;
    AskKind kind = AskKind::Form;
    std::string input;
    int64_t timeout_ms = 0;
    std::vector<AskQuestion> questions;
    bool schema_unreadable = false;
    bool child_keys_unknown = false;

    [[nodiscard]] std::string id() const {
        return owner_session + "/" + child_session + "#" +
               std::to_string(seq);
    }

    [[nodiscard]] std::string answering_session() const {
        return child_session.empty() ? owner_session : child_session;
    }

    [[nodiscard]] bool has_file_question() const {
        for (const auto& q : questions)
            if (q.control == AskControl::File) return true;
        return false;
    }

    [[nodiscard]] int answerable_questions() const {
        int total = 0;
        for (const auto& q : questions)
            if (q.control != AskControl::File) ++total;
        return total;
    }
};

struct AskAnswer {
    std::map<std::string, std::vector<std::string>> picks;
    std::map<std::string, std::string> text;

    [[nodiscard]] bool picked(const std::string& key,
                              const std::string& value) const {
        const auto it = picks.find(key);
        if (it == picks.end()) return false;
        return std::find(it->second.begin(), it->second.end(), value) !=
               it->second.end();
    }
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
    std::optional<SessionPlan> plan;
    std::optional<SessionGoal> goal;
    std::vector<PendingAsk> pending_asks;

    // --- Halt, which only an attach can see -------------------------------
    // `halted` is this session's OWN journal-folded flag. `halted_by` is a
    // SEPARATE fact -- halt CONTAINMENT, present when a halt over the ancestor
    // chain contains this session, and the server's contract calls it
    // "distinct from `halted`". A descendant therefore arrives as
    // halted:false WITH a halted_by, so `halt_contained` is what the brake
    // asks: either is enough to stop the thread.
    //
    // `halted_by` names the SESSION to resume, never a person, so it is never
    // printed where a name belongs.
    //
    // Absence clears, in both cases: the state bag replaces, and reading a
    // missing key as "unchanged" leaves a resumed thread wearing a brake it no
    // longer has.
    bool halted = false;
    bool halt_contained = false;
    std::string halted_by;
    std::string halted_reason;

    [[nodiscard]] bool halt_engaged() const {
        return halted || halt_contained;
    }
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
