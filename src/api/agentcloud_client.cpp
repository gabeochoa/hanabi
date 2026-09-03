#include "agentcloud_client.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <limits>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <string>

#include "../../vendor/nlohmann/json.hpp"
#include "../ws_socket.h"

namespace api {
namespace {

using json = nlohmann::json;

// A list of 2000+ sessions is over a megabyte, and the mint plus upgrade is a
// couple of round trips before that. Generous, but bounded: a hung socket must
// not hang the loader thread forever.
constexpr int kReplyTimeoutSecs = 30;
constexpr int kForkTimeoutSecs = 60;

constexpr int kRetractTimeoutSecs = 8;

// A turn is bounded by SILENCE, not by total time: tool rounds and model calls
// legitimately take minutes, but a socket that has said nothing for this long
// has stopped talking to us.
constexpr int kTurnIdleTimeoutSecs = 120;

// Collects one reply off the socket's private queue and hands it to the
// waiting caller. The socket calls these from its own thread.
struct Waiter {
    std::mutex m;
    std::condition_variable cv;
    std::string reply;
    std::string closed_reason;
    bool done = false;

    void finish(std::string text, std::string reason) {
        std::lock_guard<std::mutex> lock(m);
        if (done) return;  // first outcome wins
        reply = std::move(text);
        closed_reason = std::move(reason);
        done = true;
        cv.notify_all();
    }
};

void on_text_cb(void* user, const char* text, size_t len) {
    static_cast<Waiter*>(user)->finish(std::string(text, len), "");
}

void on_close_cb(void* user, const char* reason) {
    static_cast<Waiter*>(user)->finish("", reason != nullptr ? reason : "closed");
}

// nlohmann's .value() falls back only when the key is ABSENT -- a key present
// and null throws type_error.302. Real data is full of nulls (40 of 2066
// sessions have title:null), so every read goes through these instead. Found
// the hard way: the first live run died on session number who-knows-what.
std::string str_or(const json& j, const char* key, const std::string& dflt) {
    if (!j.is_object() || !j.contains(key)) return dflt;
    const json& v = j.at(key);
    return v.is_string() ? v.get<std::string>() : dflt;
}

int64_t int_or(const json& j, const char* key, int64_t dflt) {
    if (!j.is_object() || !j.contains(key)) return dflt;
    const json& v = j.at(key);
    return v.is_number_integer() ? v.get<int64_t>() : dflt;
}

bool bool_or(const json& j, const char* key, bool dflt) {
    if (!j.is_object() || !j.contains(key)) return dflt;
    const json& v = j.at(key);
    return v.is_boolean() ? v.get<bool>() : dflt;
}

const json& obj_at(const json& j, const char* key) {
    static const json kEmpty = json::object();
    if (!j.is_object() || !j.contains(key) || !j.at(key).is_object())
        return kEmpty;
    return j.at(key);
}

// round_trip's Waiter takes the first message and stops. attach+page needs two
// replies in order on the same socket, so this one queues everything and lets
// the caller wait for a named type -- the protocol has no request ids for
// these commands, so "the next reply of type X" is the only correlation there
// is (and why one request per type may be outstanding at a time).
struct FrameQueue {
    std::mutex m;
    std::condition_variable cv;
    std::vector<std::string> frames;
    std::string closed_reason;
    bool closed = false;

    void push(std::string f) {
        std::lock_guard<std::mutex> lock(m);
        frames.push_back(std::move(f));
        cv.notify_all();
    }
    void close(std::string reason) {
        std::lock_guard<std::mutex> lock(m);
        if (closed) return;
        closed = true;
        closed_reason = std::move(reason);
        cv.notify_all();
    }
    // Next queued message of ANY type, for reading a turn out frame by frame.
    // Returns a discarded json when nothing arrives before `deadline`.
    json wait_for_next(std::chrono::steady_clock::time_point deadline) {
        std::unique_lock<std::mutex> lock(m);
        for (;;) {
            if (next_ < frames.size()) {
                json root = json::parse(frames[next_++], nullptr, false);
                if (root.is_discarded()) continue;
                return obj_at(root, "msg");
            }
            if (closed) return json(json::value_t::discarded);
            if (cv.wait_until(lock, deadline) == std::cv_status::timeout &&
                next_ >= frames.size())
                return json(json::value_t::discarded);
        }
    }
    bool is_closed() {
        std::lock_guard<std::mutex> lock(m);
        return closed;
    }
    std::string why_closed() {
        std::lock_guard<std::mutex> lock(m);
        return closed_reason;
    }
    std::string closed_note(const std::string& if_open) {
        std::lock_guard<std::mutex> lock(m);
        return closed ? closed_reason : if_open;
    }

    size_t next_ = 0;  // read cursor for wait_for_next

    // Next queued message whose msg.type == want, waiting up to timeout.
    // Returns the msg object, or a discarded json on timeout/close.
    json wait_for_type(const std::string& want, int timeout_secs) {
        std::unique_lock<std::mutex> lock(m);
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(timeout_secs);
        for (;;) {
            while (next_ < frames.size()) {
                json root = json::parse(frames[next_++], nullptr, false);
                if (root.is_discarded()) continue;
                const json& msg = obj_at(root, "msg");
                const std::string t = str_or(msg, "type", "");
                if (t == "error") return msg;  // surfaced by the caller
                if (t == want) return msg;
            }
            if (closed) return json(json::value_t::discarded);
            if (cv.wait_until(lock, deadline) == std::cv_status::timeout &&
                next_ >= frames.size())
                return json(json::value_t::discarded);
        }
    }
};

void fq_text_cb(void* user, const char* text, size_t len) {
    static_cast<FrameQueue*>(user)->push(std::string(text, len));
}
void fq_close_cb(void* user, const char* reason) {
    static_cast<FrameQueue*>(user)->close(reason != nullptr ? reason : "closed");
}

// A tool's `input` is a JSON STRING of the tool's own argument object. Showing
// it raw puts {"command": "ls -l"} in the transcript where `ls -l` belongs, so
// unwrap the argument that IS the call when there is an obvious one. Tools seen
// live: bash/meta__run take `command`, step takes `text`, and the rest are
// small enough that their JSON reads fine.
std::string readable_tool_input(const std::string& raw) {
    json in = json::parse(raw, nullptr, false);
    if (in.is_discarded() || !in.is_object()) return raw;
    for (const char* key : {"command", "text", "query", "path", "pattern"}) {
        const std::string v = str_or(in, key, "");
        if (!v.empty()) return v;
    }
    return raw;
}

// `list` sorts newest-first by last_seq. There is no timestamp on the wire
// (see the note in list_sessions), so sequence is the only recency signal.
bool by_last_seq_desc(const json& a, const json& b) {
    return int_or(a, "last_seq", 0) > int_or(b, "last_seq", 0);
}

// The server reports two overlapping notions of "how is this session doing":
// a coarse resolved_status/running, and an optional richer `status` bag that
// carries the model's own read. Prefer the rich one where present.
void apply_state(const json& s, SessionSummary& out) {
    const bool running = bool_or(s, "running", false);

    // status: {state, subject, headline, attention, updated_seq}. This is the
    // server-side attention signal hanabi has been asking its other backend
    // for — believe it over any client-side guess.
    const json& st = obj_at(s, "status");
    if (!st.empty()) {
        const std::string state = str_or(st, "state", "");
        const std::string attention = str_or(st, "attention", "");
        out.preview = str_or(st, "headline", "");

        if (state == "blocked") {
            out.state = ThreadState::Attention;
            out.tag = ThreadTag::Blocked;
        } else if (state == "waiting") {
            // `waiting` and `blocked` are two different asks and used to wear
            // one tag: a thread waiting for an ANSWER read as a thread whose
            // run is blocked. Both still want the reader -- they stay in the
            // Blocked smart view together -- but the mark now says which.
            out.state = ThreadState::Attention;
            out.tag = attention == "review" ? ThreadTag::Review
                                            : ThreadTag::Waiting;
        } else if (state == "failed") {
            // The fifth state this bag can carry, and the one hanabi used to
            // drop on the floor: with no branch here a failed session fell
            // through to the resolved_status fallback below and came back as
            // Ready — a dead run presented as an asset waiting to be reviewed.
            out.state = ThreadState::Attention;
            out.tag = ThreadTag::Failed;
        } else if (state == "working") {
            // "working" is the agent's TESTIMONY; `running` is whether a run is
            // actually live. They are different facts and the row draws them
            // differently (a spinner only for the live one), so believe both
            // rather than promoting every "working" to a live run.
            out.state = running ? ThreadState::Running : ThreadState::Working;
        } else if (state == "done") {
            // "done" plus an explicit review request is the one case where a
            // finished thread still wants the reader.
            out.state = attention == "review" ? ThreadState::Attention
                                              : ThreadState::Ready;
            out.tag = attention == "review" ? ThreadTag::Review : ThreadTag::Done;
        }
        out.status = state;
        if (out.state != ThreadState::Unknown) return;
    }

    if (running) {
        out.state = ThreadState::Running;
        out.status = "active";
        return;
    }
    const std::string kind = str_or(obj_at(s, "resolved_status"), "kind", "");
    if (kind == "idle") {
        out.state = ThreadState::Parked;
        out.status = "idle";
    } else {
        out.state = ThreadState::Ready;
        out.status = "idle";
        if (str_or(obj_at(s, "last_outcome"), "outcome", "") == "completed")
            out.tag = ThreadTag::Done;
    }
}

// `frozen` -> the summary's freeze, from a summary row or an attach state.
//
// FAIL CLOSED: the PRESENCE of the object is the brake. `by` and `reason` are
// display metadata, and a server that sends `{}` or omits `by` has still said
// this session is frozen -- treating that as unfrozen would let the composer
// accept a message nothing will ever answer, which is the one failure this
// exists to prevent. Absence, and only absence, means not frozen.
void apply_frozen_from(const json& src, SessionSummary& sum) {
    if (!src.is_object() || !src.contains("frozen") ||
        src.at("frozen").is_null()) {
        sum.frozen = false;
        sum.frozen_by.clear();
        sum.frozen_reason.clear();
        return;
    }
    const json& frozen = src.at("frozen");
    sum.frozen = true;
    sum.frozen_by = str_or(frozen, "by", "");
    sum.frozen_reason = str_or(frozen, "reason", "");
}

SessionSummary summary_from_row(const json& s) {
    SessionSummary sum;
    sum.id = str_or(s, "session_id", "");
    if (sum.id.empty()) return sum;
    sum.title = str_or(s, "title", "");
    if (sum.title.empty())
        sum.title = str_or(obj_at(s, "status"), "subject", "");
    if (sum.title.empty()) sum.title = "(untitled)";
    const std::string workspace = str_or(s, "workspace", "");
    sum.folder = workspace.find(sum.id) == std::string::npos ? workspace : "";
    sum.parent_id = str_or(s, "parent", "");
    sum.forked_from = str_or(obj_at(s, "forked_from"), "session_id", "");
    // `frozen` and `archived_at_unix_ms` are summary-row keys
    // (`WireSessionSummary`), which is what lets the list mark them without
    // attaching.
    apply_frozen_from(s, sum);
    sum.server_archived_at_ms = int_or(s, "archived_at_unix_ms", 0);
    apply_state(s, sum);
    return sum;
}



// state bag is a snapshot, so a key that stopped being sent means the brake
// was lifted, not that it should be kept.
//
//   halted     = bool, skip-if-false. The session's OWN journal-folded flag.
//   halted_by  = { by, reason }, skip-if-none. Halt CONTAINMENT, a separate
//                fact: it is present when a halt over this session's ancestor
//                chain contains it, and the server's own contract calls it
//                "distinct from `halted`, the exact journal-folded own flag".
//                So a descendant can arrive as halted:false WITH a halted_by,
//                and reading the containment only when the own flag is set
//                would leave that thread unbraked. `by` names the SESSION to
//                resume -- never a person.
//   channel_replies_paused = bool, skip-if-false. A `WireState` key with no
//                summary-row equivalent, so an attach is the only place this
//                client can learn it.
//   frozen     = { by, reason }, skip-if-none. Rides the summary row AND the
//                attach greeting; read here too so a freeze that landed after
//                the last catalog poll brakes the composer immediately rather
//                than at the next refresh.
void apply_brakes_from_state(const json& state, Session& out) {
    out.summary.replies_paused =
        bool_or(state, "channel_replies_paused", false);
    out.halted = bool_or(state, "halted", false);
    // Independent of `halted`, and cleared only by ABSENCE.
    if (state.is_object() && state.contains("halted_by")) {
        const json& by = obj_at(state, "halted_by");
        out.halted_by = str_or(by, "by", "");
        out.halted_reason = str_or(by, "reason", "");
        out.halt_contained = true;
    } else {
        out.halted_by.clear();
        out.halted_reason.clear();
        out.halt_contained = false;
    }
    apply_frozen_from(state, out.summary);
}

// hello.state.tokens -> ContextUsage.
//
//   tokens.context   = { budget, window }
//   tokens.occupancy = { tokens, basis, stale, anchor_seq }
//
// budget is the denominator: it is what triggers compaction, and compaction is
// the consequence the reader cares about. window is read past deliberately.
//
// stale arrives as 0/1 on this wire but is a boolean in spirit, so both
// spellings are accepted rather than one of them silently reading as "fresh".
ContextUsage context_usage_from_state(const json& state) {
    ContextUsage out;
    const json& tokens = obj_at(state, "tokens");
    if (tokens.empty()) return out;

    out.budget_tokens = int_or(obj_at(tokens, "context"), "budget", -1);

    const json& occupancy = obj_at(tokens, "occupancy");
    out.used_tokens = int_or(occupancy, "tokens", -1);
    out.stale = bool_or(occupancy, "stale", false) ||
                int_or(occupancy, "stale", 0) != 0;
    return out;
}

}  // namespace

AgentcloudClient::AgentcloudClient(agentcloud::AuthConfig cfg)
    : auth_(std::move(cfg)) {}

AgentcloudClient::AgentcloudClient(agentcloud::AuthConfig cfg,
                                   agentcloud::Token token)
    : auth_(std::move(cfg), std::move(token)) {}

AgentcloudClient::~AgentcloudClient() = default;

std::string AgentcloudClient::backend_label() const { return "agentcloud"; }

std::string AgentcloudClient::round_trip(const std::string& payload_json,
                                         const std::string& expect_type,
                                         std::string* error, int timeout_secs) {
    const auto fail = [&](const std::string& why) {
        if (error != nullptr) *error = why;
        return std::string();
    };

    const auto& cfg = auth_.config();
    std::string auth_err;
    const auto token = auth_.get(&auth_err);
    if (token.empty()) return fail(auth_err);

    const auto waiterOwned = std::make_shared<Waiter>();
    Waiter& waiter = *waiterOwned;
    const std::string url = "ws://" + cfg.host + "/ws/chat?v=1";

    ws_config wc{};
    wc.url = url.c_str();
    wc.proxy_host = cfg.proxy_host.c_str();
    wc.proxy_port = cfg.proxy_port;
    wc.on_text = on_text_cb;
    wc.on_close = on_close_cb;
    wc.user = &waiter;

    ws_conn* conn = ws_open_owned(&wc, waiterOwned);
    if (conn == nullptr) return fail("could not parse " + url);
    struct Closer { ws_conn* c; ~Closer() { ws_close(c); } } closer{conn};

    // The credential travels IN the command, not as a header: control commands
    // (list/create/attach) must each carry it.
    json cmd = json::parse(payload_json);
    cmd["auth"] = {{"cat", {{"payload", token.value}}}};
    const json envelope = {{"sub", 0}, {"payload", cmd}};
    const std::string wire = envelope.dump();

    if (!ws_send_text(conn, wire.data(), wire.size()))
        return fail("socket closed before the command was sent");

    std::string reply, closed;
    {
        std::unique_lock<std::mutex> lock(waiter.m);
        const bool got =
            waiter.cv.wait_for(lock, std::chrono::seconds(timeout_secs),
                               [&] { return waiter.done; });
        if (!got)
            return fail("timed out after " + std::to_string(timeout_secs) +
                        "s waiting for '" + expect_type + "'");
        reply = waiter.reply;
        closed = waiter.closed_reason;
    }

    if (reply.empty()) return fail("connection closed: " + closed);

    json root = json::parse(reply, nullptr, false);
    if (root.is_discarded()) return fail("reply was not JSON");
    // Server envelope is {"sub":N,"msg":{...}} -- note it is NOT "payload",
    // which is the client direction only.
    if (!root.contains("msg") || !root["msg"].is_object())
        return fail("reply had no msg object");

    const json& msg = root["msg"];
    const std::string type = str_or(msg, "type", "");
    if (type == "error") {
        // A token the server rejects may be one our clock still believes in;
        // drop it so the next call mints fresh rather than retrying the same
        // rejected credential.
        auth_.invalidate();
        return fail("server error: " + str_or(msg, "message", "(no message)"));
    }
    if (type != expect_type)
        return fail("expected '" + expect_type + "', got '" + type + "'");

    return msg.dump();
}

namespace agentcloud {

std::vector<SessionSummary> parse_sessions_reply(const std::string& msg_json) {
    json msg = json::parse(msg_json, nullptr, false);
    if (msg.is_discarded() || !msg.contains("sessions") ||
        !msg["sessions"].is_array())
        return {};

    std::vector<json> rows(msg["sessions"].begin(), msg["sessions"].end());
    std::stable_sort(rows.begin(), rows.end(), by_last_seq_desc);

    // --- children fold into their parent's count, and leave the list ------
    // Every session the server knows is its own row here, spawned sub-agents
    // included, each carrying the id of the thread that spawned it in
    // `parent` (agentcloud's durable parent link, spec 024, projected into
    // the catalog by spec 036 DEC-9 precisely so "a client can badge or group
    // children"). Rendered as-is that puts every sub-agent in the sidebar as
    // a peer of the thread that owns it — which is noise, and not what the
    // reference client does with them.
    //
    // So one pass tallies each child onto its parent, and the child rows are
    // then dropped. Two passes over the reply, no per-row lookup: this runs
    // on a catalog of a couple of thousand rows every poll.
    struct ChildTally {
        int total = 0;
        int running = 0;
    };
    std::unordered_map<std::string, ChildTally> tally;
    for (const json& s : rows) {
        const std::string parent = str_or(s, "parent", "");
        if (parent.empty()) continue;
        // The child's own state, read exactly as a top-level row's would be,
        // so "running" means the same thing in the count as in the glyph.
        SessionSummary child;
        apply_state(s, child);
        ChildTally& t = tally[parent];
        ++t.total;
        if (child.state == ThreadState::Running) ++t.running;
    }

    std::vector<SessionSummary> out;
    out.reserve(rows.size());
    for (const json& s : rows) {
        // A child is counted, not listed. Note this drops a child whose
        // parent is not in this reply too: it is still a sub-agent, and the
        // sidebar showing it as a root thread was the thing being fixed.
        if (!str_or(s, "parent", "").empty()) continue;
        SessionSummary sum = summary_from_row(s);
        if (sum.id.empty()) continue;
        if (auto it = tally.find(sum.id); it != tally.end()) {
            sum.sub_agent_count = it->second.total;
            sum.sub_agent_running_count = it->second.running;
        }
        out.push_back(std::move(sum));
    }
    return out;
}

std::vector<SessionSummary> parse_subagents_reply(const std::string& msg_json,
                                                  std::size_t limit) {
    json msg = json::parse(msg_json, nullptr, false);
    if (msg.is_discarded() || !msg.contains("sessions") ||
        !msg["sessions"].is_array() || limit == 0)
        return {};
    std::vector<json> rows(msg["sessions"].begin(), msg["sessions"].end());
    std::stable_sort(rows.begin(), rows.end(), by_last_seq_desc);
    std::vector<SessionSummary> out;
    out.reserve(std::min(limit, rows.size()));
    for (const json& row : rows) {
        if (str_or(row, "parent", "").empty()) continue;
        SessionSummary child = summary_from_row(row);
        if (child.id.empty()) continue;
        out.push_back(std::move(child));
        if (out.size() == limit) break;
    }
    return out;
}

std::string fork_command_json(const std::string& source_session_id) {
    return json{{"cmd", "fork"}, {"source_session_id", source_session_id}}
        .dump();
}

std::string fork_with_prompt_command_json(const std::string& source_session_id,
                                          const std::string& prompt,
                                          const std::string& title) {
    return json{{"cmd", "fork_with_prompt"},
                {"source_session_id", source_session_id},
                {"prompt", prompt},
                {"title", title}}
        .dump();
}

std::string parse_created_session_id(const std::string& msg_json) {
    const json msg = json::parse(msg_json, nullptr, false);
    if (msg.is_discarded()) return {};
    return str_or(obj_at(msg, "session"), "session_id", "");
}

bool hello_has_capability(const std::string& hello_json,
                          const std::string& capability) {
    const json hello = json::parse(hello_json, nullptr, false);
    if (hello.is_discarded() || !hello.contains("capabilities") ||
        !hello["capabilities"].is_array())
        return false;
    for (const auto& value : hello["capabilities"])
        if (value.is_string() && value.get<std::string>() == capability)
            return true;
    return false;
}

ContextUsage parse_context_usage(const std::string& hello_json) {
    const json hello = json::parse(hello_json, nullptr, false);
    if (hello.is_discarded()) return {};
    return context_usage_from_state(obj_at(hello, "state"));
}

void parse_session_brakes(const std::string& hello_json, Session& out) {
    const json hello = json::parse(hello_json, nullptr, false);
    if (hello.is_discarded()) return;
    apply_brakes_from_state(obj_at(hello, "state"), out);
}

}  // namespace agentcloud

Result<std::vector<SessionSummary>> AgentcloudClient::list_sessions() {
    std::string error;
    const std::string msg_json =
        round_trip(R"({"cmd":"list"})", "sessions", &error);
    if (msg_json.empty())
        return Result<std::vector<SessionSummary>>::failure(error);

    auto rows = agentcloud::parse_sessions_reply(msg_json);
    if (rows.empty())
        return Result<std::vector<SessionSummary>>::failure(
            "sessions reply had no readable sessions");
    return Result<std::vector<SessionSummary>>::success(std::move(rows));
}

Result<std::vector<SessionSummary>> AgentcloudClient::list_subagents(
    std::size_t limit) {
    std::string error;
    const std::string msg_json =
        round_trip(R"({"cmd":"list"})", "sessions", &error);
    if (msg_json.empty())
        return Result<std::vector<SessionSummary>>::failure(error);
    return Result<std::vector<SessionSummary>>::success(
        agentcloud::parse_subagents_reply(msg_json, limit));
}

Result<std::string> AgentcloudClient::fork_session(
    const std::string& session_id) {
    std::string error;
    const std::string reply =
        round_trip(agentcloud::fork_command_json(session_id), "created", &error,
                   kForkTimeoutSecs);
    if (reply.empty()) return Result<std::string>::failure(error);
    const std::string id = agentcloud::parse_created_session_id(reply);
    if (id.empty())
        return Result<std::string>::failure(
            "created reply had no destination session id");
    return Result<std::string>::success(id);
}

Result<std::string> AgentcloudClient::fork_with_prompt(
    const std::string& session_id, const std::string& prompt,
    const std::string& title) {
    std::string capability_error;
    Session source;
    const std::string hello =
        attach_and_page(session_id, 1, &source, &capability_error);
    if (hello.empty()) return Result<std::string>::failure(capability_error);
    if (!agentcloud::hello_has_capability(hello, "fork_with_prompt_v1"))
        return Result<std::string>::failure(
            "this session does not support BTW forks");

    std::string error;
    const std::string reply = round_trip(
        agentcloud::fork_with_prompt_command_json(session_id, prompt, title),
        "created", &error, kForkTimeoutSecs);
    if (reply.empty()) return Result<std::string>::failure(error);
    const std::string id = agentcloud::parse_created_session_id(reply);
    if (id.empty())
        return Result<std::string>::failure(
            "created reply had no destination session id");
    return Result<std::string>::success(id);
}

namespace agentcloud {

namespace {

SessionPlanStep::Status plan_status(const std::string& value) {
    if (value == "pending") return SessionPlanStep::Status::Pending;
    if (value == "in_progress") return SessionPlanStep::Status::InProgress;
    if (value == "completed") return SessionPlanStep::Status::Completed;
    if (value == "cancelled") return SessionPlanStep::Status::Cancelled;
    return SessionPlanStep::Status::Unknown;
}

GoalPhase goal_phase(const std::string& value) {
    if (value == "active") return GoalPhase::Active;
    if (value == "paused") return GoalPhase::Paused;
    if (value == "blocked") return GoalPhase::Blocked;
    if (value == "completed") return GoalPhase::Completed;
    if (value == "cleared") return GoalPhase::Cleared;
    return GoalPhase::Unknown;
}

std::optional<SessionPlan> plan_from_json(const json& value) {
    if (!value.is_object() || !value.contains("steps") ||
        !value.at("steps").is_array() || value.at("steps").empty())
        return std::nullopt;
    SessionPlan plan;
    plan.title = str_or(value, "title", "");
    plan.revision = int_or(value, "revision", 0);
    for (const auto& item : value.at("steps")) {
        if (!item.is_object()) continue;
        SessionPlanStep step;
        step.id = str_or(item, "id", "");
        step.text = str_or(item, "text", str_or(item, "step", ""));
        step.note = str_or(item, "note", "");
        step.status = plan_status(str_or(item, "status", ""));
        plan.steps.push_back(std::move(step));
    }
    if (plan.steps.empty()) return std::nullopt;
    return plan;
}

std::optional<SessionGoal> goal_from_json(const json& value) {
    if (!value.is_object()) return std::nullopt;
    SessionGoal goal;
    goal.objective = str_or(value, "objective", "");
    goal.done_when = str_or(value, "done_when", "");
    goal.phase = goal_phase(str_or(value, "phase", ""));
    goal.note = str_or(value, "note", "");
    goal.set_by = str_or(value, "set_by", "");
    goal.revision = int_or(value, "revision", 0);
    if (goal.objective.empty() && goal.phase == GoalPhase::Unknown)
        return std::nullopt;
    return goal;
}

std::string plan_line(const SessionPlan& plan, const std::string& explanation = "") {
    std::string out;
    if (!explanation.empty()) out = explanation + " — ";
    if (!plan.title.empty()) out += plan.title + " — ";
    out += std::to_string(plan.completed()) + " of " +
           std::to_string(plan.steps.size());
    if (const auto* current = plan.current(); current && !current->text.empty())
        out += " — " + current->text;
    return out;
}

std::string goal_line(const SessionGoal& goal) {
    std::string phase;
    switch (goal.phase) {
        case GoalPhase::Active: break;
        case GoalPhase::Paused: phase = "paused"; break;
        case GoalPhase::Blocked: phase = "blocked"; break;
        case GoalPhase::Completed: phase = "completed"; break;
        case GoalPhase::Cleared: phase = "cleared"; break;
        case GoalPhase::Unknown: phase = "updated"; break;
    }
    if (phase.empty()) return goal.objective;
    if (goal.objective.empty()) return phase;
    return phase + " — " + goal.objective;
}

void apply_plan_goal_state(const json& state, Session& out) {
    out.plan = state.contains("plan") ? plan_from_json(state.at("plan"))
                                      : std::nullopt;
    out.goal = state.contains("goal") ? goal_from_json(state.at("goal"))
                                      : std::nullopt;
}

}  // namespace

void parse_plan_goal_state(const std::string& hello_json, Session& out) {
    const json hello = json::parse(hello_json, nullptr, false);
    if (hello.is_discarded()) return;
    apply_plan_goal_state(obj_at(hello, "state"), out);
}

void parse_pending_asks(const std::string& hello_json, Session& out) {
    const json hello = json::parse(hello_json, nullptr, false);
    if (hello.is_discarded()) {
        out.pending_asks.clear();
        return;
    }
    out.pending_asks =
        elicitation::asks_from_state(obj_at(hello, "state"), out.summary.id);
}

std::vector<Message> parse_page_frames(const std::string& msg_json) {
    json msg = json::parse(msg_json, nullptr, false);
    if (msg.is_discarded() || !msg.contains("frames") ||
        !msg["frames"].is_array())
        return {};

    std::vector<Message> out;
    // tool_result names the SEQ of the tool_intent it answers, and arrives
    // later -- sometimes much later. Remember where each intent's row landed so
    // the result can be folded back into it instead of appended as a stray row.
    std::unordered_map<int64_t, size_t> row_for_intent_seq;
    // Same trick for anything whose OUTCOME lands later and names the seq of
    // the intent that started it: a spawn settling, a peer message being
    // delivered. Seqs are unique across the journal, so one map serves both.
    // child_spawned carries the title and keys by child id instead.
    std::unordered_map<std::string, size_t> row_for_child_id;
    std::unordered_map<int64_t, size_t> row_for_outcome_seq;

    for (const json& f : msg["frames"]) {
        if (!f.is_object()) continue;
        const json& e = obj_at(f, "event");
        const std::string type = str_or(e, "type", "");
        const int64_t seq = int_or(f, "seq", 0);
        // Every history frame carries a real wall-clock stamp, unlike the
        // session list. Milliseconds on the wire, seconds in Message.
        const int64_t created = int_or(f, "created_at_unix_ms", 0) / 1000;

        const auto push = [&](Role role, std::string text) -> Message& {
            Message m;
            m.id = std::to_string(seq);
            m.role = role;
            m.text = std::move(text);
            m.created_at = created;
            out.push_back(std::move(m));
            return out.back();
        };
        // An event row: not speech, so it carries a KIND and a short label
        // rather than an author and a paragraph.
        const auto push_event = [&](EventKind kind, std::string label,
                                    std::string body) -> Message& {
            Message& m = push(Role::System, std::move(body));
            m.kind = kind;
            m.subtitle = std::move(label);
            return m;
        };

        if (type == "user_input") {
            std::string text = str_or(e, "text", "");
            if (text.empty()) continue;
            push(Role::User, std::move(text));
        } else if (type == "block") {
            const json& b = obj_at(e, "block");
            const std::string kind = str_or(b, "kind", "");
            // tool_use blocks are the model ANNOUNCING a call; the call itself
            // arrives as tool_intent with the same call_id. Rendering both
            // would double every tool row, so this side is dropped.
            if (kind == "text") {
                std::string text = str_or(b, "text", "");
                if (!text.empty()) push(Role::Assistant, std::move(text));
            } else if (kind == "thinking") {
                // Reasoning is real content, but it is not the answer. Mark it
                // so the renderer can fold or dim it rather than presenting it
                // as something the assistant said to you.
                std::string text = str_or(b, "text", "");
                if (!text.empty()) {
                    Message& m = push(Role::Assistant, std::move(text));
                    m.kind = EventKind::Thinking;
                    m.subtitle = "thinking";
                }
            }
        } else if (type == "tool_intent") {
            Message& m = push(Role::Tool, readable_tool_input(str_or(e, "input", "")));
            m.kind = EventKind::ToolCall;
            m.subtitle = str_or(e, "tool", "");
            row_for_intent_seq[seq] = out.size() - 1;
        } else if (type == "tool_node_selected") {
            // Which host the call actually ran on, and it arrives as its own
            // frame after the intent -- so like tool_result it is folded back
            // rather than rendered as a row of its own.
            auto it = row_for_intent_seq.find(int_or(e, "intent", 0));
            if (it != row_for_intent_seq.end())
                out[it->second].tool_node = str_or(e, "node_id", "");
        } else if (type == "tool_result") {
            const int64_t intent = int_or(e, "intent", 0);
            auto it = row_for_intent_seq.find(intent);
            if (it == row_for_intent_seq.end()) continue;  // intent off-page
            Message& m = out[it->second];
            const json& outcome = obj_at(e, "outcome");
            m.tool_status = str_or(outcome, "outcome", "") == "success"
                                ? "completed"
                                : "failed";
            m.tool_result = str_or(obj_at(outcome, "content"), "text", "");
            // The row's own stamp stays the intent's: a tool row belongs where
            // the call was made, not where the answer landed.
            if (created > 0 && m.created_at > 0)
                m.tool_duration_ms = (created - m.created_at) * 1000;
        } else if (type == "skill_invoked") {
            // The skill's own body is the whole SKILL.md and is prompt
            // material, not transcript material -- the row says which skill
            // loaded and where it came from.
            push_event(EventKind::Skill, str_or(e, "name", ""),
                       str_or(e, "source", ""));
        } else if (type == "child_spawn_intended") {
            const std::string child = str_or(e, "child", "");
            Message& m = push_event(EventKind::SubAgent, child,
                                    str_or(e, "prompt", ""));
            m.tool_status = "running";
            row_for_outcome_seq[seq] = out.size() - 1;
            if (!child.empty()) row_for_child_id[child] = out.size() - 1;
        } else if (type == "child_spawned") {
            // The title the child was actually given, which is the only
            // human-readable name a spawn ever gets. It arrives AFTER the
            // intent, so it replaces the id the row was holding.
            auto it = row_for_child_id.find(str_or(e, "child", ""));
            const std::string title = str_or(e, "title", "");
            if (it != row_for_child_id.end() && !title.empty())
                out[it->second].subtitle = title;
            else if (it == row_for_child_id.end())
                push_event(EventKind::SubAgent,
                           title.empty() ? str_or(e, "child", "") : title, "")
                    .tool_status = "running";
        } else if (type == "child_spawn_settled") {
            auto it = row_for_outcome_seq.find(int_or(e, "intent", 0));
            if (it == row_for_outcome_seq.end()) continue;  // intent off-page
            out[it->second].tool_status =
                str_or(obj_at(e, "outcome"), "outcome", "");
        } else if (type == "node_attached" || type == "node_detached" ||
                   type == "node_granted" || type == "node_released" ||
                   type == "node_reserved") {
            // The verb, stripped of its prefix: "attached", "detached", …
            push_event(EventKind::Node, str_or(e, "node_id", ""),
                       type.substr(std::string("node_").size()));
        } else if (type == "subscription_delivered") {
            // How everything the platform puts INTO a session arrives: a
            // child settling, a timer firing, a peer session speaking. The
            // key says which; the body is what was delivered.
            push_event(EventKind::Delivery,
                       str_or(obj_at(e, "key"), "kind", "subscription"),
                       str_or(e, "body", ""));
        } else if (type == "message_enqueued") {
            Message& m = push_event(EventKind::Delivery,
                                    "to " + str_or(e, "to", ""),
                                    str_or(e, "body", ""));
            m.tool_status = "running";
            row_for_outcome_seq[seq] = out.size() - 1;
        } else if (type == "message_delivered") {
            auto it = row_for_outcome_seq.find(int_or(e, "intent", 0));
            if (it == row_for_outcome_seq.end()) continue;  // intent off-page
            out[it->second].tool_status =
                str_or(obj_at(e, "outcome"), "outcome", "");
        } else if (type == "notice") {
            push_event(EventKind::Notice, str_or(e, "kind", ""),
                       str_or(e, "message", ""));
        } else if (type == "status_reported") {
            push_event(EventKind::Status, str_or(e, "state", ""),
                       str_or(e, "headline", ""));
        } else if (type == "plan_updated") {
            if (auto plan = plan_from_json(obj_at(e, "plan")))
                push_event(EventKind::Plan, "",
                           plan_line(*plan, str_or(e, "explanation", "")));
        } else if (type == "goal_updated") {
            if (auto goal = goal_from_json(obj_at(e, "goal")))
                push_event(EventKind::Goal, "", goal_line(*goal));
        }
        // Everything else -- run_started, model_call_*, epoch_change_*, noop
        // and the rest of a vocabulary the server says will grow -- folds as
        // nothing on purpose.
    }
    return out;
}

void install_paged_transcript(const std::string& page_json, Session& out) {
    out.messages = parse_page_frames(page_json);
}

}  // namespace agentcloud

namespace agentcloud {

std::string delta_from_accumulated(const std::string& emitted,
                                   const std::string& accumulated) {
    // Live text arrives ACCUMULATED and is installed whole. StreamSink wants
    // increments, so emit only the tail that extends what the sink already
    // has. A payload that is not an extension means a NEW block started (or
    // the text was rewritten), so it is emitted in full rather than diffed --
    // silently emitting a suffix of unrelated text would corrupt the bubble.
    if (accumulated.size() > emitted.size() &&
        accumulated.compare(0, emitted.size(), emitted) == 0)
        return accumulated.substr(emitted.size());
    if (accumulated == emitted) return "";
    return accumulated;
}

// The classifier proper, over an ALREADY-PARSED frame.
//
// The websocket receive loop parses every frame into a json object to read its
// "type", and then used to call classify_live_frame(msg.dump()) -- serialising
// that object back to text so this function could parse it a second time.
// Every frame on the wire was parse -> dump -> parse, at token rate.
//
// MEASURED, 5000 frames, CLOCK_THREAD_CPUTIME_ID:
//   dump() + re-parse            11.6 ms   (2.3 us/frame)
//   read the parsed object        0.096 ms (0.02 us/frame)   -- 121x cheaper
//
// Published (agentcloud_client.h) so the path production actually takes is the
// path a test can drive; .cpp-private would have left the hot path untested.
LiveFrame classify_live_frame_parsed(const json& root, LiveBlocks& blocks) {
    LiveFrame lf;
    if (root.is_discarded() || !root.is_object()) return lf;
    // A retract says a live partial is gone; there is nothing to show for it.
    const std::string frame = str_or(root, "frame", "");
    if (frame == "retract") return lf;

    const json& e = obj_at(root, "event");
    const std::string type = str_or(e, "type", "");

    if (type == "model_call_started") {
        // Block indices are per model call and restart at 0, so a stale map
        // would attribute the next call's reply to the last call's reasoning.
        blocks.clear();
        return lf;
    }
    if (type == "block_delta") {
        // The live increment. `delta` is a tagged union: "start" opens a block,
        // "append" carries new text. Anything else is a shape this build does
        // not know and must not guess at.
        const int index = static_cast<int>(int_or(e, "index", -1));
        const json& d = obj_at(e, "delta");
        const std::string which = str_or(d, "delta", "");
        if (which == "start") {
            const std::string kind = str_or(obj_at(d, "kind"), "kind", "");
            blocks.note(index, kind);
            lf.kind = LiveFrame::Kind::BlockStart;
            lf.payload = kind;
        } else if (which == "append") {
            const std::string kind = blocks.kind_at(index);
            lf.payload = str_or(d, "text", "");
            if (kind == "thinking") {
                lf.kind = LiveFrame::Kind::ThinkingAppend;
            } else if (kind == "tool_use" || kind == "redacted_thinking") {
                // The JSON argument object of a call, arriving a few
                // characters at a time. It is the tool row's content, not the
                // reply's, and the tool row is built from tool_intent.
                lf.kind = LiveFrame::Kind::ToolInputAppend;
                lf.payload.clear();
            } else {
                lf.kind = LiveFrame::Kind::TextAppend;
            }
        }
        return lf;
    }
    if (type == "block") {
        const json& b = obj_at(e, "block");
        const std::string kind = str_or(b, "kind", "");
        blocks.note(static_cast<int>(int_or(e, "index", -1)), kind);
        if (kind == "text") {
            lf.kind = LiveFrame::Kind::Text;
            lf.payload = str_or(b, "text", "");
        } else if (kind == "thinking") {
            lf.kind = LiveFrame::Kind::Thinking;
            lf.payload = str_or(b, "text", "");
        }
        // tool_use is the announcement of a call that tool_intent then makes;
        // showing both would double the affordance, same as in the history fold.
        return lf;
    }
    if (type == "tool_intent") {
        lf.kind = LiveFrame::Kind::ToolCall;
        lf.payload = str_or(e, "tool", "");
        return lf;
    }
    if (type == "session_renamed") {
        lf.kind = LiveFrame::Kind::Title;
        lf.payload = str_or(e, "title", "");
        return lf;
    }
    if (type == "run_finished") {
        lf.kind = LiveFrame::Kind::Finished;
        return lf;
    }
    if (type == "elicitation_requested") {
        lf.kind = LiveFrame::Kind::AskRaised;
        return lf;
    }
    if (type == "elicitation_resolved" || type == "child_elicitation_update" ||
        type == "child_elicitation_notice") {
        lf.kind = LiveFrame::Kind::AskSettled;
        return lf;
    }
    return lf;
}

void LiveBlocks::note(int index, std::string kind) {
    if (index < 0 || kind.empty()) return;
    kind_[index] = std::move(kind);
}

void LiveBlocks::clear() { kind_.clear(); }

std::string LiveBlocks::kind_at(int index) const {
    auto it = kind_.find(index);
    return it == kind_.end() ? std::string() : it->second;
}

bool LiveTurn::feed(const json& msg, const StreamSink& sink) {
    if (final_.created_at == 0) {
        final_.role = Role::Assistant;
        final_.created_at = static_cast<int64_t>(std::time(nullptr));
    }
    if (str_or(msg, "type", "") != "frame") return true;

    const LiveFrame lf = classify_live_frame_parsed(msg, blocks_);
    switch (lf.kind) {
        case LiveFrame::Kind::BlockStart:
            // A fresh block: the per-block buffer restarts, but the assembled
            // reply keeps everything before it.
            emitted_.clear();
            break;
        case LiveFrame::Kind::TextAppend:
            if (!lf.payload.empty()) {
                sink.emit_delta(lf.payload);
                final_.text += lf.payload;
                emitted_ += lf.payload;
            }
            break;
        case LiveFrame::Kind::Text: {
            // The payload is the ACCUMULATED text at this key, installed
            // whole -- so emit only the part the sink has not seen. A
            // shorter payload means a different block started, not a
            // rewind, so start the diff over from there.
            // The whole block: either the settled durable copy of what we
            // just streamed (diff is empty -- do NOT print it twice), or a
            // partial handed to us by attaching mid-turn (diff is the lot).
            const std::string d = delta_from_accumulated(emitted_, lf.payload);
            if (!d.empty()) {
                sink.emit_delta(d);
                final_.text += d;
            }
            emitted_ = lf.payload;
            break;
        }
        case LiveFrame::Kind::ThinkingAppend:
        case LiveFrame::Kind::Thinking:
            sink.emit_event({StreamEventKind::Thinking, lf.payload});
            break;
        case LiveFrame::Kind::ToolCall:
            sink.emit_event({StreamEventKind::ToolCall, lf.payload});
            break;
        case LiveFrame::Kind::Title:
            sink.emit_event({StreamEventKind::TitleUpdate, lf.payload});
            break;
        case LiveFrame::Kind::Finished:
            return false;
        case LiveFrame::Kind::AskRaised: {
            json entry;
            if (elicitation::ask_entry_from_frame(msg.dump(), &entry)) {
                upsert_ask(std::move(entry));
                emit_asks(sink);
            }
            return false;
        }
        case LiveFrame::Kind::AskSettled:
            if (drop_settled_ask(msg.dump()) || fold_child_update(msg.dump()))
                emit_asks(sink);
            break;
        case LiveFrame::Kind::ToolInputAppend:
        case LiveFrame::Kind::Ignore:
            break;
    }
    return true;
}

void LiveTurn::seed_asks(const json& state, const StreamSink& sink) {
    asks_.clear();
    children_ = json::array();
    childCause_.clear();
    if (state.is_object() && state.contains("pending_elicitations") &&
        state.at("pending_elicitations").is_array())
        for (const json& e : state.at("pending_elicitations"))
            if (e.is_object()) asks_.push_back(e);
    if (state.is_object() && state.contains("child_pending_elicitations") &&
        state.at("child_pending_elicitations").is_array())
        children_ = state.at("child_pending_elicitations");
    asksKnown_ = true;
    emit_asks(sink);
}

void LiveTurn::upsert_ask(json entry) {
    const int64_t seq = entry.value("elicitation", int64_t{0});
    for (json& held : asks_)
        if (held.value("elicitation", int64_t{-1}) == seq) {
            held = std::move(entry);
            return;
        }
    asks_.push_back(std::move(entry));
}

bool LiveTurn::drop_settled_ask(const std::string& frame_json) {
    std::uint64_t seq = 0;
    std::string action;
    std::string by;
    if (!elicitation::fold_ask_resolved(frame_json, &seq, &action, &by))
        return false;
    const std::size_t before = asks_.size();
    std::vector<json> kept;
    for (const json& e : asks_) {
        const bool same =
            e.is_object() && e.contains("elicitation") &&
            e.at("elicitation").is_number_integer() &&
            static_cast<std::uint64_t>(e.at("elicitation").get<int64_t>()) ==
                seq;
        if (!same) kept.push_back(e);
    }
    asks_ = std::move(kept);
    return asks_.size() != before;
}

static const json* detail_child_entry(const json& row) {
    if (!row.is_object()) return nullptr;
    const auto it = row.find("elicitation");
    if (it == row.end() || !it->is_object()) return nullptr;
    return &*it;
}

bool LiveTurn::fold_child_update(const std::string& frame_json) {
    std::string session;
    std::uint64_t seq = 0;
    std::uint64_t cause = 0;
    bool pending = false;
    json entry;
    if (!elicitation::fold_child_update(frame_json, &session, &seq, &cause,
                                        &pending, &entry))
        return false;

    const auto key = std::make_pair(session, seq);
    const auto seen = childCause_.find(key);
    if (seen != childCause_.end() && cause <= seen->second) return false;
    childCause_[key] = cause;
    json kept = json::array();
    bool changed = false;
    for (const json& row : children_) {
        const json* held = detail_child_entry(row);
        const bool same =
            row.value("session", std::string()) == session &&
            held != nullptr &&
            static_cast<std::uint64_t>(held->value("elicitation",
                                                   int64_t{0})) == seq;
        if (same) {
            changed = true;
            continue;
        }
        kept.push_back(row);
    }
    if (pending) {
        kept.push_back({{"session", session}, {"elicitation", entry}});
        changed = true;
    }
    children_ = std::move(kept);
    return changed;
}

void LiveTurn::emit_asks(const StreamSink& sink) const {
    if (!asksKnown_) return;
    json own = json::array();
    for (const json& e : asks_) own.push_back(e);
    const json state = {{"pending_elicitations", own},
                        {"child_pending_elicitations", children_}};
    sink.emit_event({StreamEventKind::AsksChanged, state.dump()});
}

// The stateless form, kept for callers that classify one frame in isolation.
// It cannot attribute an append to a block, so it calls every increment text.
LiveFrame classify_live_frame_parsed(const json& root) {
    LiveBlocks scratch;
    return classify_live_frame_parsed(root, scratch);
}

// The published entry point: parse, then classify. Unchanged contract — never
// throws, anything unrecognised is Ignore. tests/unit/test_agentcloud.cpp
// drives the classifier through this overload.
LiveFrame classify_live_frame(const std::string& msg_json) {
    return classify_live_frame_parsed(json::parse(msg_json, nullptr, false));
}

bool fold_session_renamed(const std::string& msg_json, SessionSummary& summary) {
    json root = json::parse(msg_json, nullptr, false);
    if (root.is_discarded() || !root.is_object()) return false;
    const json& e = obj_at(root, "event");
    if (str_or(e, "type", "") != "session_renamed") return false;
    const std::string title = str_or(e, "title", "");
    if (title.empty()) return false;
    summary.title = title;
    return true;
}

}  // namespace agentcloud

std::string AgentcloudClient::attach_and_page(const std::string& id, int limit,
                                              Session* out,
                                              std::string* error) {
    const auto fail = [&](const std::string& why) {
        if (error != nullptr) *error = why;
        return std::string();
    };

    const auto& cfg = auth_.config();
    std::string auth_err;
    const auto token = auth_.get(&auth_err);
    if (token.empty()) return fail(auth_err);

    const auto qOwned = std::make_shared<FrameQueue>();
    FrameQueue& q = *qOwned;
    const std::string url = "ws://" + cfg.host + "/ws/chat?v=1";
    ws_config wc{};
    wc.url = url.c_str();
    wc.proxy_host = cfg.proxy_host.c_str();
    wc.proxy_port = cfg.proxy_port;
    wc.on_text = fq_text_cb;
    wc.on_close = fq_close_cb;
    wc.user = &q;

    ws_conn* conn = ws_open_owned(&wc, qOwned);
    if (conn == nullptr) return fail("could not parse " + url);

    struct Closer {
        ws_conn* c;
        ~Closer() { ws_close(c); }
    } closer{conn};

    // sub 1 is the session channel; attach must carry the credential.
    json attach = {{"cmd", "attach"},
                   {"session_id", id},
                   {"auth", {{"cat", {{"payload", token.value}}}}}};
    const json attach_env = {{"sub", 1}, {"payload", attach}};
    const std::string attach_wire = attach_env.dump();
    if (!ws_send_text(conn, attach_wire.data(), attach_wire.size()))
        return fail("socket closed before attach was sent");

    const json hello = q.wait_for_type("hello", kReplyTimeoutSecs);
    if (hello.is_discarded())
        return fail("no hello for " + id + " (" + q.why_closed() + ")");
    if (str_or(hello, "type", "") == "error") {
        auth_.invalidate();
        return fail("attach refused: " + str_or(hello, "message", "(no message)"));
    }

    // hello.state carries what the session list could not: a real title, and
    // the token accounting behind a context meter.
    const json& state = obj_at(hello, "state");
    const std::string title = str_or(state, "title", "");
    if (!title.empty()) out->summary.title = title;
    apply_state(state, out->summary);
    out->context = context_usage_from_state(state);
    apply_brakes_from_state(state, *out);
    agentcloud::parse_plan_goal_state(hello.dump(), *out);
    agentcloud::parse_pending_asks(hello.dump(), *out);

    // Page BACKWARD from the newest until the server says done.
    //
    // limit == 0 means "the whole transcript" to this app (loader_system's
    // load-older path). It is NOT a wire value: clamping it into 1..500 like a
    // real limit would ask for a single frame and hand back a one-message
    // transcript, which is how this first went wrong.
    const bool want_all = (limit <= 0);
    const int per_page = want_all ? 500 : (limit > 500 ? 500 : limit);
    // A session can be millions of seqs long; "all" has to mean "all we will
    // sit here for". 20 pages of 500 is 10k frames, well past any transcript a
    // person reads, and the caller still learns there is more.
    constexpr int kMaxPages = 20;

    // Pages arrive newest-first, oldest-first WITHIN a page. Collect them and
    // splice in ascending seq order so the fold sees one continuous history --
    // a tool_result must be able to find an intent from an earlier page.
    std::vector<json> pages;
    uint64_t before = std::numeric_limits<uint64_t>::max();
    bool done = false;
    for (int p = 0; p < (want_all ? kMaxPages : 1) && !done; ++p) {
        const json page_env = {{"sub", 1},
                               {"payload",
                                {{"cmd", "page"},
                                 {"before", before},
                                 {"limit", per_page}}}};
        const std::string page_wire = page_env.dump();
        if (!ws_send_text(conn, page_wire.data(), page_wire.size()))
            return fail("socket closed before page was sent");

        const json page = q.wait_for_type("page", kReplyTimeoutSecs);
        if (page.is_discarded())
            return fail("no page for " + id + " (" + q.why_closed() + ")");
        if (str_or(page, "type", "") == "error")
            return fail("page refused: " +
                        str_or(page, "message", "(no message)"));

        const json& frames = page.contains("frames") && page["frames"].is_array()
                                 ? page["frames"]
                                 : json::array();
        if (frames.empty()) { done = true; break; }

        // Next page ends where this one begins.
        const uint64_t oldest =
            static_cast<uint64_t>(int_or(frames.front(), "seq", 0));
        pages.push_back(frames);
        done = page.value("done", true);
        if (oldest == 0) break;  // no usable cursor; stop rather than spin
        before = oldest;
    }

    json all = json::array();
    for (auto it = pages.rbegin(); it != pages.rend(); ++it)
        for (const json& f : *it) all.push_back(f);

    const json combined = {{"type", "page"}, {"frames", all}};
    agentcloud::install_paged_transcript(combined.dump(), *out);
    // Only claim there is more when the server said so and we stopped asking.
    out->has_more_older = !done;
    return hello.dump();
}

void AgentcloudClient::run_turn(const std::string& session_id,
                                const std::string& prompt,
                                const std::string& apply,
                                const StreamSink& sink) {
    const auto& cfg = auth_.config();
    std::string auth_err;
    const auto token = auth_.get(&auth_err);
    if (token.empty()) { sink.emit_error(auth_err); return; }

    const auto qOwned = std::make_shared<FrameQueue>();
    FrameQueue& q = *qOwned;
    const std::string url = "ws://" + cfg.host + "/ws/chat?v=1";
    ws_config wc{};
    wc.url = url.c_str();
    wc.proxy_host = cfg.proxy_host.c_str();
    wc.proxy_port = cfg.proxy_port;
    wc.on_text = fq_text_cb;
    wc.on_close = fq_close_cb;
    wc.user = &q;

    ws_conn* conn = ws_open_owned(&wc, qOwned);
    if (conn == nullptr) { sink.emit_error("could not parse " + url); return; }
    struct Closer { ws_conn* c; ~Closer() { ws_close(c); } } closer{conn};

    const json attach_env = {
        {"sub", 1},
        {"payload",
         {{"cmd", "attach"},
          {"session_id", session_id},
          {"auth", {{"cat", {{"payload", token.value}}}}}}}};
    const std::string attach_wire = attach_env.dump();
    if (!ws_send_text(conn, attach_wire.data(), attach_wire.size())) {
        sink.emit_error("socket closed before attach was sent");
        return;
    }
    const json hello = q.wait_for_type("hello", kReplyTimeoutSecs);
    if (hello.is_discarded()) {
        sink.emit_error("no hello for " + session_id + " (" + q.why_closed() + ")");
        return;
    }
    if (str_or(hello, "type", "") == "error") {
        auth_.invalidate();
        sink.emit_error("attach refused: " +
                        str_or(hello, "message", "(no message)"));
        return;
    }

    // Post-attach commands inherit the principal bound at attach, so no auth
    // here. `apply` is required: the wire has no default.
    const json input_env = {
        {"sub", 1},
        {"payload", {{"cmd", "input"}, {"text", prompt}, {"apply", apply}}}};
    const std::string input_wire = input_env.dump();
    if (!ws_send_text(conn, input_wire.data(), input_wire.size())) {
        sink.emit_error("socket closed before input was sent");
        return;
    }

    // There is no ack. The durable user_input frame IS the acknowledgement,
    // and the reply then arrives as live frames until run_finished.
    //
    // An agent turn can legitimately run for minutes -- tool rounds, model
    // calls -- so the wait is bounded by SILENCE, not by total duration:
    // as long as frames keep arriving we keep reading.
    agentcloud::LiveTurn turn;
    turn.seed_asks(obj_at(hello, "state"), sink);

    const auto deadline_from_now = [] {
        return std::chrono::steady_clock::now() +
               std::chrono::seconds(kTurnIdleTimeoutSecs);
    };
    auto idle_deadline = deadline_from_now();

    for (;;) {
        const json msg = q.wait_for_next(idle_deadline);
        if (msg.is_discarded()) {
            if (q.is_closed()) {
                sink.emit_error("connection closed mid-turn: " + q.why_closed());
                return;
            }
            // Silence for the whole idle window. Hand back what did arrive
            // rather than throwing the turn away -- a partial reply the user
            // can read beats an error that discards it.
            break;
        }
        idle_deadline = deadline_from_now();

        if (str_or(msg, "type", "") == "error") {
            sink.emit_error("server error: " +
                            str_or(msg, "message", "(no message)"));
            return;
        }
        if (!turn.feed(msg, sink)) {
            sink.emit_done(turn.assembled());
            return;
        }
    }
    sink.emit_done(turn.assembled());
}

void AgentcloudClient::send_message_streaming(const std::string& session_id,
                                              const std::string& prompt,
                                              const StreamSink& sink) {
    // after_tool_round is the conventional apply: it lands the message at the
    // next tool boundary rather than cutting the agent off mid-thought.
    run_turn(session_id, prompt, "after_tool_round", sink);
}

Result<Message> AgentcloudClient::send_message(const std::string& session_id,
                                               const std::string& prompt) {
    // The blocking seam, for callers that have not moved to streaming. Same
    // machinery; it simply waits for the turn to finish.
    Result<Message> result = Result<Message>::failure("no reply");
    StreamSink sink;
    sink.on_done = [&](const Message& m) {
        result = Result<Message>::success(m);
    };
    sink.on_error = [&](const std::string& e) {
        result = Result<Message>::failure(e);
    };
    run_turn(session_id, prompt, "after_tool_round", sink);
    return result;
}

Result<Message> AgentcloudClient::steer(const std::string& session_id,
                                        const std::string& prompt) {
    // Steering is not a different endpoint here -- it is the same input with
    // apply set to interrupt, which is the server-side replacement for the
    // whole queue/steer state machine the other backend needs.
    Result<Message> result = Result<Message>::failure("no reply");
    StreamSink sink;
    sink.on_done = [&](const Message& m) {
        result = Result<Message>::success(m);
    };
    sink.on_error = [&](const std::string& e) {
        result = Result<Message>::failure(e);
    };
    run_turn(session_id, prompt, "interrupt", sink);
    return result;
}

Result<std::string> AgentcloudClient::rename_session(
    const std::string& session_id, const std::string& title) {
    const auto fail = [](const std::string& why) {
        return Result<std::string>::failure(why);
    };

    const auto& cfg = auth_.config();
    std::string auth_err;
    const auto token = auth_.get(&auth_err);
    if (token.empty()) return fail(auth_err);

    const auto qOwned = std::make_shared<FrameQueue>();
    FrameQueue& q = *qOwned;
    const std::string url = "ws://" + cfg.host + "/ws/chat?v=1";
    ws_config wc{};
    wc.url = url.c_str();
    wc.proxy_host = cfg.proxy_host.c_str();
    wc.proxy_port = cfg.proxy_port;
    wc.on_text = fq_text_cb;
    wc.on_close = fq_close_cb;
    wc.user = &q;

    ws_conn* conn = ws_open_owned(&wc, qOwned);
    if (conn == nullptr) return fail("could not parse " + url);
    struct Closer { ws_conn* c; ~Closer() { ws_close(c); } } closer{conn};

    const json attach_env = {
        {"sub", 1},
        {"payload",
         {{"cmd", "attach"},
          {"session_id", session_id},
          {"auth", {{"cat", {{"payload", token.value}}}}}}}};
    const std::string attach_wire = attach_env.dump();
    if (!ws_send_text(conn, attach_wire.data(), attach_wire.size()))
        return fail("socket closed before attach was sent");

    const json hello = q.wait_for_type("hello", kReplyTimeoutSecs);
    if (hello.is_discarded())
        return fail("no hello for " + session_id + " (" + q.why_closed() + ")");
    if (str_or(hello, "type", "") == "error") {
        auth_.invalidate();
        return fail("attach refused: " +
                    str_or(hello, "message", "(no message)"));
    }
    // rename_v1 is announced on attach. An announcement list that exists and
    // omits it means this session cannot be renamed; an absent list is a shape
    // we do not know and is not evidence of anything, so it does not block.
    if (hello.contains("capabilities") && hello.at("capabilities").is_array()) {
        bool announced = false;
        for (const json& c : hello.at("capabilities"))
            if (c.is_string() && c.get<std::string>() == "rename_v1")
                announced = true;
        if (!announced) return fail("this session does not accept renames");
    }

    const json rename_env = {{"sub", 1},
                             {"payload", {{"cmd", "rename"}, {"title", title}}}};
    const std::string rename_wire = rename_env.dump();
    if (!ws_send_text(conn, rename_wire.data(), rename_wire.size()))
        return fail("socket closed before rename was sent");

    // The durable session_renamed frame IS the acknowledgement; there is no
    // other ack to wait for, and nothing may be applied before it arrives.
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(kReplyTimeoutSecs);
    for (;;) {
        const json msg = q.wait_for_next(deadline);
        if (msg.is_discarded())
            return fail(q.closed_note("no rename echo for " + session_id));
        if (str_or(msg, "type", "") == "error")
            return fail(str_or(msg, "message", "rename refused"));
        if (str_or(msg, "type", "") != "frame") continue;
        SessionSummary echoed;
        if (agentcloud::fold_session_renamed(msg.dump(), echoed))
            return Result<std::string>::success(echoed.title);
    }
}

Result<std::string> AgentcloudClient::resolve_ask(const std::string& session_id,
                                                  const PendingAsk& ask,
                                                  AskAction action,
                                                  const AskAnswer& answer) {
    const auto fail = [](const std::string& why) {
        return Result<std::string>::failure(why);
    };
    if (ask.kind == AskKind::Form && action == AskAction::Accept &&
        !elicitation::answer_has_content(ask, answer))
        return fail("nothing to submit yet");
    if (action == AskAction::Accept &&
        !elicitation::answer_within_cap(ask, answer))
        return fail("that answer is too long for one reply");

    const auto& cfg = auth_.config();
    std::string auth_err;
    const auto token = auth_.get(&auth_err);
    if (token.empty()) return fail(auth_err);

    const auto qOwned = std::make_shared<FrameQueue>();
    FrameQueue& q = *qOwned;
    const std::string url = "ws://" + cfg.host + "/ws/chat?v=1";
    ws_config wc{};
    wc.url = url.c_str();
    wc.proxy_host = cfg.proxy_host.c_str();
    wc.proxy_port = cfg.proxy_port;
    wc.on_text = fq_text_cb;
    wc.on_close = fq_close_cb;
    wc.user = &q;

    ws_conn* conn = ws_open_owned(&wc, qOwned);
    if (conn == nullptr) return fail("could not parse " + url);
    struct Closer { ws_conn* c; ~Closer() { ws_close(c); } } closer{conn};

    const json attach_env = {
        {"sub", 1},
        {"payload",
         {{"cmd", "attach"},
          {"session_id", session_id},
          {"auth", {{"cat", {{"payload", token.value}}}}}}}};
    const std::string attach_wire = attach_env.dump();
    if (!ws_send_text(conn, attach_wire.data(), attach_wire.size()))
        return fail("socket closed before attach was sent");

    const json hello = q.wait_for_type("hello", kReplyTimeoutSecs);
    if (hello.is_discarded())
        return fail("no hello for " + session_id + " (" + q.why_closed() + ")");
    if (str_or(hello, "type", "") == "error") {
        auth_.invalidate();
        return fail("attach refused: " +
                    str_or(hello, "message", "(no message)"));
    }

    bool still_pending = false;
    for (const PendingAsk& live : elicitation::asks_from_state(
             obj_at(hello, "state"), session_id))
        if (live.id() == ask.id()) still_pending = true;
    if (!still_pending) return fail(elicitation::kAskGoneReason);

    const std::string resolve_payload =
        elicitation::resolve_command_json(ask, action, answer);
    const json resolve_env = {
        {"sub", 1},
        {"payload", json::parse(resolve_payload, nullptr, false)}};
    const std::string resolve_wire = resolve_env.dump();
    if (!ws_send_text(conn, resolve_wire.data(), resolve_wire.size()))
        return fail("socket closed before the answer was sent");

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(ask.child_session.empty() ? kReplyTimeoutSecs
                                                       : kRetractTimeoutSecs);
    for (;;) {
        const json msg = q.wait_for_next(deadline);
        if (msg.is_discarded())
            return fail(q.closed_note("no settlement for this question"));
        if (str_or(msg, "type", "") == "error")
            return fail(str_or(msg, "message", "the answer was refused"));
        if (str_or(msg, "type", "") != "frame") continue;
        const std::string frame = msg.dump();
        if (!ask.child_session.empty()) {
            if (elicitation::fold_child_ask_retracted(frame, ask.child_session,
                                                      ask.seq))
                return Result<std::string>::success(
                    elicitation::action_word(action));
            continue;
        }
        std::uint64_t seq = 0;
        std::string settled;
        std::string by;
        if (elicitation::fold_ask_resolved(frame, &seq, &settled, &by) &&
            seq == ask.seq)
            return Result<std::string>::success(settled);
    }
}

Result<Session> AgentcloudClient::get_session(const std::string& id) {
    return get_session(id, 200);
}
Result<Session> AgentcloudClient::get_session(const std::string& id, int limit) {
    // One socket for the pair: attach binds the principal for this
    // subscription, and page inherits it. Splitting them across two sockets
    // would re-attach for nothing.
    std::string error;
    Session session;
    session.summary.id = id;

    const std::string hello_json = attach_and_page(id, limit, &session, &error);
    if (hello_json.empty()) return Result<Session>::failure(error);
    return Result<Session>::success(std::move(session));
}

}  // namespace api
