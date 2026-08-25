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
        {
            std::lock_guard<std::mutex> lock(m);
            if (done) return;  // first outcome wins
            reply = std::move(text);
            closed_reason = std::move(reason);
            done = true;
        }
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
        {
            std::lock_guard<std::mutex> lock(m);
            frames.push_back(std::move(f));
        }
        cv.notify_all();
    }
    void close(std::string reason) {
        {
            std::lock_guard<std::mutex> lock(m);
            if (closed) return;
            closed = true;
            closed_reason = std::move(reason);
        }
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
            out.state = ThreadState::Attention;
            out.tag = attention == "review" ? ThreadTag::Review : ThreadTag::Blocked;
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

AgentcloudClient::~AgentcloudClient() = default;

std::string AgentcloudClient::backend_label() const { return "agentcloud"; }

std::string AgentcloudClient::round_trip(const std::string& payload_json,
                                         const std::string& expect_type,
                                         std::string* error) {
    const auto fail = [&](const std::string& why) {
        if (error != nullptr) *error = why;
        return std::string();
    };

    const auto& cfg = auth_.config();
    std::string auth_err;
    const auto token = auth_.get(&auth_err);
    if (token.empty()) return fail(auth_err);

    Waiter waiter;
    const std::string url = "ws://" + cfg.host + "/ws/chat?v=1";

    ws_config wc{};
    wc.url = url.c_str();
    wc.proxy_host = cfg.proxy_host.c_str();
    wc.proxy_port = cfg.proxy_port;
    wc.on_text = on_text_cb;
    wc.on_close = on_close_cb;
    wc.user = &waiter;

    ws_conn* conn = ws_open(&wc);
    if (conn == nullptr) return fail("could not parse " + url);

    // The credential travels IN the command, not as a header: control commands
    // (list/create/attach) must each carry it.
    json cmd = json::parse(payload_json);
    cmd["auth"] = {{"cat", {{"payload", token.value}}}};
    const json envelope = {{"sub", 0}, {"payload", cmd}};
    const std::string wire = envelope.dump();

    if (!ws_send_text(conn, wire.data(), wire.size())) {
        ws_close(conn);
        return fail("socket closed before the command was sent");
    }

    std::string reply, closed;
    {
        std::unique_lock<std::mutex> lock(waiter.m);
        const bool got = waiter.cv.wait_for(
            lock, std::chrono::seconds(kReplyTimeoutSecs),
            [&] { return waiter.done; });
        if (!got) {
            lock.unlock();
            ws_close(conn);
            return fail("timed out after " + std::to_string(kReplyTimeoutSecs) +
                        "s waiting for '" + expect_type + "'");
        }
        reply = waiter.reply;
        closed = waiter.closed_reason;
    }
    ws_close(conn);

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
        SessionSummary sum;
        sum.id = str_or(s, "session_id", "");
        if (sum.id.empty()) continue;  // unaddressable; nothing could open it
        // 40 of 2066 live rows have title:null. A blank row is unclickable in
        // practice -- the reader cannot tell one from the next -- so fall back
        // to the model's own subject line, then to a marker.
        sum.title = str_or(s, "title", "");
        if (sum.title.empty())
            sum.title = str_or(obj_at(s, "status"), "subject", "");
        if (sum.title.empty()) sum.title = "(untitled)";
        // NO TIMESTAMP EXISTS ON THIS WIRE. Not "we do not read it" -- the row
        // carries last_seq, a monotonic sequence, and nothing clock-like at
        // all. updated_at stays 0, which the UI already reads as "unknown", so
        // rows sort correctly by recency but cannot say "40m ago". Fixing that
        // needs either a server field or a timestamp mined from the session's
        // own events at attach.
        sum.updated_at = 0;
        // Same field name the other backend uses for folders -- but NOT the
        // same meaning. Most workspaces here are scratch directories the
        // server made per session, named after the session: 2054 of 2066 live
        // rows carry their own id in the path, so grouping on it verbatim
        // yields ~2055 folders of one and a useless sidebar. A path containing
        // its session's id is machine-generated, not a place someone chose.
        const std::string workspace = str_or(s, "workspace", "");
        sum.folder = workspace.find(sum.id) == std::string::npos ? workspace : "";
        apply_state(s, sum);
        if (auto it = tally.find(sum.id); it != tally.end()) {
            sum.sub_agent_count = it->second.total;
            sum.sub_agent_running_count = it->second.running;
        }
        out.push_back(std::move(sum));
    }
    return out;
}

ContextUsage parse_context_usage(const std::string& hello_json) {
    const json hello = json::parse(hello_json, nullptr, false);
    if (hello.is_discarded()) return {};
    return context_usage_from_state(obj_at(hello, "state"));
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

namespace agentcloud {

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
                if (!text.empty()) push(Role::Assistant, std::move(text)).subtitle =
                    "thinking";
            }
        } else if (type == "tool_intent") {
            Message& m = push(Role::Tool, readable_tool_input(str_or(e, "input", "")));
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
        }
        // Everything else -- run_started, model_call_*, epoch_change_*, noop,
        // status_reported and the rest of a vocabulary the server says will
        // grow -- folds as nothing on purpose.
    }
    return out;
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
LiveFrame classify_live_frame_parsed(const json& root) {
    LiveFrame lf;
    if (root.is_discarded() || !root.is_object()) return lf;
    // A retract says a live partial is gone; there is nothing to show for it.
    const std::string frame = str_or(root, "frame", "");
    if (frame == "retract") return lf;

    const json& e = obj_at(root, "event");
    const std::string type = str_or(e, "type", "");

    if (type == "block_delta") {
        // The live increment. `delta` is a tagged union: "start" opens a block,
        // "append" carries new text. Anything else is a shape this build does
        // not know and must not guess at.
        const json& d = obj_at(e, "delta");
        const std::string which = str_or(d, "delta", "");
        if (which == "start") {
            lf.kind = LiveFrame::Kind::BlockStart;
        } else if (which == "append") {
            lf.kind = LiveFrame::Kind::TextAppend;
            lf.payload = str_or(d, "text", "");
        }
        return lf;
    }
    if (type == "block") {
        const json& b = obj_at(e, "block");
        const std::string kind = str_or(b, "kind", "");
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
    return lf;
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

    FrameQueue q;
    const std::string url = "ws://" + cfg.host + "/ws/chat?v=1";
    ws_config wc{};
    wc.url = url.c_str();
    wc.proxy_host = cfg.proxy_host.c_str();
    wc.proxy_port = cfg.proxy_port;
    wc.on_text = fq_text_cb;
    wc.on_close = fq_close_cb;
    wc.user = &q;

    ws_conn* conn = ws_open(&wc);
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
        return fail("no hello for " + id + " (" + q.closed_reason + ")");
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
            return fail("no page for " + id + " (" + q.closed_reason + ")");
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
    out->messages = agentcloud::parse_page_frames(combined.dump());
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

    FrameQueue q;
    const std::string url = "ws://" + cfg.host + "/ws/chat?v=1";
    ws_config wc{};
    wc.url = url.c_str();
    wc.proxy_host = cfg.proxy_host.c_str();
    wc.proxy_port = cfg.proxy_port;
    wc.on_text = fq_text_cb;
    wc.on_close = fq_close_cb;
    wc.user = &q;

    ws_conn* conn = ws_open(&wc);
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
        sink.emit_error("no hello for " + session_id + " (" + q.closed_reason + ")");
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
    Message final;
    final.role = Role::Assistant;
    final.created_at = static_cast<int64_t>(std::time(nullptr));
    std::string emitted;  // what the sink has already been told

    const auto deadline_from_now = [] {
        return std::chrono::steady_clock::now() +
               std::chrono::seconds(kTurnIdleTimeoutSecs);
    };
    auto idle_deadline = deadline_from_now();

    for (;;) {
        const json msg = q.wait_for_next(idle_deadline);
        if (msg.is_discarded()) {
            if (q.closed) {
                sink.emit_error("connection closed mid-turn: " + q.closed_reason);
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
        if (str_or(msg, "type", "") != "frame") continue;

        const agentcloud::LiveFrame lf =
            agentcloud::classify_live_frame_parsed(msg);
        switch (lf.kind) {
            case agentcloud::LiveFrame::Kind::BlockStart:
                // A fresh block: the per-block buffer restarts, but the
                // assembled reply keeps everything before it.
                emitted.clear();
                break;
            case agentcloud::LiveFrame::Kind::TextAppend:
                if (!lf.payload.empty()) {
                    sink.emit_delta(lf.payload);
                    final.text += lf.payload;
                    emitted += lf.payload;
                }
                break;
            case agentcloud::LiveFrame::Kind::Text: {
                // The payload is the ACCUMULATED text at this key, installed
                // whole -- so emit only the part the sink has not seen. A
                // shorter payload means a different block started, not a
                // rewind, so start the diff over from there.
                // The whole block: either the settled durable copy of what we
                // just streamed (diff is empty -- do NOT print it twice), or a
                // partial handed to us by attaching mid-turn (diff is the lot).
                const std::string d =
                    agentcloud::delta_from_accumulated(emitted, lf.payload);
                if (!d.empty()) {
                    sink.emit_delta(d);
                    final.text += d;
                }
                emitted = lf.payload;
                break;
            }
            case agentcloud::LiveFrame::Kind::Thinking:
                sink.emit_event({StreamEventKind::Thinking, lf.payload});
                break;
            case agentcloud::LiveFrame::Kind::ToolCall:
                sink.emit_event({StreamEventKind::ToolCall, lf.payload});
                break;
            case agentcloud::LiveFrame::Kind::Title:
                sink.emit_event({StreamEventKind::TitleUpdate, lf.payload});
                break;
            case agentcloud::LiveFrame::Kind::Finished:
                sink.emit_done(final);
                return;
            case agentcloud::LiveFrame::Kind::Ignore:
                break;
        }
    }
    sink.emit_done(final);
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

    FrameQueue q;
    const std::string url = "ws://" + cfg.host + "/ws/chat?v=1";
    ws_config wc{};
    wc.url = url.c_str();
    wc.proxy_host = cfg.proxy_host.c_str();
    wc.proxy_port = cfg.proxy_port;
    wc.on_text = fq_text_cb;
    wc.on_close = fq_close_cb;
    wc.user = &q;

    ws_conn* conn = ws_open(&wc);
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
        return fail("no hello for " + session_id + " (" + q.closed_reason + ")");
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
            return fail(q.closed ? "connection closed before the rename echo: " +
                                       q.closed_reason
                                 : "no rename echo for " + session_id);
        if (str_or(msg, "type", "") == "error")
            return fail(str_or(msg, "message", "rename refused"));
        if (str_or(msg, "type", "") != "frame") continue;
        SessionSummary echoed;
        if (agentcloud::fold_session_renamed(msg.dump(), echoed))
            return Result<std::string>::success(echoed.title);
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
