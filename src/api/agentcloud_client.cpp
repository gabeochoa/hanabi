#include "agentcloud_client.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
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
        } else if (state == "working") {
            out.state = ThreadState::Running;
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

    std::vector<SessionSummary> out;
    out.reserve(rows.size());
    for (const json& s : rows) {
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
        out.push_back(std::move(sum));
    }
    return out;
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

Result<Session> AgentcloudClient::get_session(const std::string&) {
    // Deliberately not faked. Reading a transcript means attach + the keyed
    // fold, which is the next slice; an empty Session here would render as a
    // real but blank conversation.
    return Result<Session>::failure(
        "agentcloud: reading a transcript is not implemented yet");
}

}  // namespace api
