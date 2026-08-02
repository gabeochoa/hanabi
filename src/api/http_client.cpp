#include "http_client.h"

#include <algorithm>
#include <cstdlib>

#include "../../vendor/nlohmann/json.hpp"

// cpp-httplib is header-only. We keep the implementation macro local to this
// translation unit so the rest of the app never pulls in networking headers.
// TLS support is opt-in at build time (define HANABI_ENABLE_TLS and link
// OpenSSL) so the default build has zero extra dependencies.
#ifdef HANABI_ENABLE_TLS
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include <httplib.h>

namespace api {

using json = nlohmann::json;

namespace {

// Split "https://host:port/prefix" into a scheme+host[:port] origin (for the
// httplib::Client) and a leading path prefix that is prepended to requests.
struct SplitUrl {
    std::string origin;  // scheme://host[:port]
    std::string prefix;  // path prefix, may be empty
};

SplitUrl split_url(const std::string& url) {
    SplitUrl s;
    auto scheme_end = url.find("://");
    size_t host_start = (scheme_end == std::string::npos) ? 0 : scheme_end + 3;
    auto path_start = url.find('/', host_start);
    if (path_start == std::string::npos) {
        s.origin = url;
    } else {
        s.origin = url.substr(0, path_start);
        s.prefix = url.substr(path_start);
        if (s.prefix == "/") s.prefix.clear();
    }
    return s;
}

std::string replace_id(std::string tmpl, const std::string& id) {
    auto pos = tmpl.find("{id}");
    if (pos != std::string::npos) tmpl.replace(pos, 4, id);
    return tmpl;
}

Role parse_role(const std::string& r) {
    if (r == "user") return Role::User;
    if (r == "assistant") return Role::Assistant;
    if (r == "system") return Role::System;
    if (r == "tool") return Role::Tool;
    return Role::Assistant;
}

// Read a string field that might be a string or a number (epoch).
int64_t as_epoch(const json& obj, const std::string& key) {
    if (!obj.contains(key)) return 0;
    const auto& v = obj.at(key);
    if (v.is_number_integer()) return v.get<int64_t>();
    if (v.is_number_float()) return static_cast<int64_t>(v.get<double>());
    return 0;
}

std::string as_string(const json& obj, const std::string& key) {
    if (!obj.contains(key)) return "";
    const auto& v = obj.at(key);
    if (v.is_string()) return v.get<std::string>();
    return "";
}

// Parse one message object using the configured field mapping (shared by the
// transcript reader and the send_message reply reader). Prefers a flat text
// field; falls back to concatenating text-type blocks and notes any non-text
// block type as a subtitle hint.
Message parse_message(const json& e, const Config& cfg) {
    Message m;
    m.id = as_string(e, cfg.field_id);
    m.role = parse_role(as_string(e, cfg.field_role));
    // created_at handled by caller-side normalization below when present.
    m.text = as_string(e, cfg.field_text);
    if (m.text.empty() && e.is_object() && e.contains(cfg.field_blocks) &&
        e.at(cfg.field_blocks).is_array()) {
        std::string joined;
        for (const auto& b : e.at(cfg.field_blocks)) {
            if (!b.is_object()) continue;
            const std::string btype = as_string(b, cfg.field_block_type);
            if (btype == cfg.field_block_text_type) {
                const std::string c = as_string(b, cfg.field_block_content);
                if (!c.empty()) {
                    if (!joined.empty()) joined += "\n\n";
                    joined += c;
                }
            } else if (!btype.empty() && m.subtitle.empty()) {
                m.subtitle = btype;
            }
        }
        m.text = std::move(joined);
    }
    return m;
}

// Read a boolean field that a backend might encode as a JSON bool OR as a
// 0/1 number (both shapes appear in the wild for flags like isPinned).
bool as_bool(const json& obj, const std::string& key) {
    if (!obj.contains(key)) return false;
    const auto& v = obj.at(key);
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number_integer()) return v.get<int64_t>() != 0;
    return false;
}

// The generic UI + fmtutil::relative_time work in epoch SECONDS (the mock
// seeds seconds). Real backends commonly report millisecond epochs, which
// would read as "the far future" (relative_time clamps to "now" forever) and
// break every "2h/3d" age. Normalize: anything that looks like ms (13+ digit
// magnitude, i.e. well past a plausible seconds-epoch) is divided down to
// seconds. This is a magnitude heuristic, not endpoint-specific.
int64_t as_epoch_seconds(const json& obj, const std::string& key) {
    int64_t e = as_epoch(obj, key);
    // ~ Sat Nov 2286 in seconds; any value larger is almost certainly ms.
    constexpr int64_t kSecondsCeiling = 10000000000LL;
    if (e > kSecondsCeiling) e /= 1000;
    return e;
}

// Lowercase a copy for case-insensitive marker matching.
std::string to_lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return s;
}

bool contains_any(const std::string& hay,
                  const std::vector<std::string>& needles) {
    for (const auto& n : needles)
        if (hay.find(n) != std::string::npos) return true;
    return false;
}

// --- Client-side high-signal state derivation (HEURISTICS) ------------------
// The real API reports no ThreadState/ThreadTag; it only carries generic
// primitives (status, isProcessing, subSessionStatus, a free-text title). A
// backend with none of the signals below yields all-calm rows (fine — Home
// simply reads as "caught up" + a Recent list). We derive hanabi's attention
// model from those primitives so real sessions get meaningful triage instead
// of collapsing to Unknown. Everything here is TITLE-TEXT + GENERIC-FIELD
// heuristics — no endpoint, product, or company string is hardcoded.
//
// Signals, in priority order:
//   1. Running   — isProcessing==true OR subSessionStatus=="running": the
//                  agent is actively working. Quiet, count-only signal.
//   2. Attention/Blocked ("waiting on you") — the STRONGEST real signal is the
//      app's own title convention: a leading "[P]" tag (parked, needs you), or
//      any of the plain-language "the ball is in your court" phrases below.
//      This is what lights up the WAITING-ON-YOU digest for real data.
//   3. Attention/Done ("finished since you looked") — a title that reads as
//      resolved (done/landed/shipped/complete/✓) AND has no waiting phrase.
//      Waiting wins over done: a "done, but awaiting your decision" thread is
//      still on you. We deliberately do NOT treat subSessionStatus=="complete"
//      as Done — every finished cron tick reports "complete", so keying on it
//      would flood the digest; only an explicit title marker counts.
//   4. Archived  — status=="archived": retired, low-signal.
//   5. otherwise — calm/Unknown: shows in Recent, never nudges.
void derive_state(SessionSummary& s, const json& e) {
    const bool processing = as_bool(e, "isProcessing");
    const std::string sub = as_string(e, "subSessionStatus");
    const std::string t = to_lower(s.title);

    // Marker vocab kept small, plain, lowercase. These are conservative: only
    // an unambiguous "on you" phrase flips a thread to needs-you.
    static const std::vector<std::string> kWaiting = {
        "on you",   "on gabe",  "waiting", "awaiting",
        "blocked",  "gated on", "needs you", "back to you"};
    static const std::vector<std::string> kDone = {
        "done", " landed", "landed ", "shipped", "concluded",
        "\xe2\x9c\x93" /* ✓ */};
    const bool parked = s.title.rfind("[P]", 0) == 0 ||
                        s.title.rfind("[p]", 0) == 0;

    if (processing || sub == "running") {
        s.state = ThreadState::Running;
    } else if (parked || contains_any(t, kWaiting)) {
        // Parked/needs-you: the actionable "waiting on you" bucket.
        s.state = ThreadState::Attention;
        s.tag = ThreadTag::Blocked;
    } else if (contains_any(t, kDone)) {
        // Reads as resolved with no outstanding ask on the user.
        s.state = ThreadState::Attention;
        s.tag = ThreadTag::Done;
    } else if (s.status == "archived") {
        s.state = ThreadState::Archived;
    }
    // else: leave calm (Unknown / None) — degrades gracefully.
}

// --- Diagnostic dump (dev aid, OFF by default) ------------------------------
// When HANABI_DUMP is set in the environment, print the SHAPE of the real
// backend's data to stderr so we can see what fields real objects carry that
// the adapter/mock don't yet model. This never runs unless the flag is set,
// never touches the UI, and never logs the auth token (only response bodies).
bool dump_enabled() {
    const char* v = std::getenv("HANABI_DUMP");
    return v && *v && std::string(v) != "0";
}

// Render a single JSON value compactly for a one-line dump (strings truncated).
std::string brief(const json& v, size_t max = 48) {
    std::string s;
    if (v.is_string()) {
        s = v.get<std::string>();
    } else {
        s = v.dump();
    }
    if (s.size() > max) s = s.substr(0, max) + "\xe2\x80\xa6";
    // Keep it single-line.
    for (char& c : s)
        if (c == '\n' || c == '\r') c = ' ';
    return s;
}

// Dump the parsed summary fields + the RAW top-level keys (and a sample of any
// keys the adapter currently ignores) for the first `limit` sessions.
void dump_session_list(const json& arr, const Config& cfg, size_t limit) {
    fprintf(stderr, "[HANABI_DUMP] session list: %zu objects\n",
            arr.size());
    // Which keys does the adapter actually consume?
    const std::vector<std::string> known = {
        cfg.field_id, cfg.field_title, cfg.field_updated_at, cfg.field_status,
        cfg.field_preview};
    size_t n = 0;
    for (const auto& e : arr) {
        if (n >= limit) break;
        SessionSummary s;
        s.id = as_string(e, cfg.field_id);
        s.title = as_string(e, cfg.field_title);
        s.updated_at = as_epoch(e, cfg.field_updated_at);
        s.status = as_string(e, cfg.field_status);
        fprintf(stderr,
                "[HANABI_DUMP]  #%zu parsed: id=%s title=\"%s\" status=%s "
                "updated_at=%lld state=Unknown tag=None folder=\"\" starred=0\n",
                n, brief(s.id, 24).c_str(), brief(s.title, 32).c_str(),
                brief(s.status, 16).c_str(),
                static_cast<long long>(s.updated_at));
        if (e.is_object()) {
            // All top-level keys present on this object.
            std::string allkeys;
            for (auto it = e.begin(); it != e.end(); ++it) {
                if (!allkeys.empty()) allkeys += ",";
                allkeys += it.key();
            }
            fprintf(stderr, "[HANABI_DUMP]  #%zu raw keys: %s\n", n,
                    allkeys.c_str());
            // Keys the adapter IGNORES today, with a sample value each — this
            // is the material for making the mock resemble reality (e.g.
            // isProcessing / subSessionStatus / isPinned / workspaceId / model).
            std::string ignored;
            for (auto it = e.begin(); it != e.end(); ++it) {
                if (std::find(known.begin(), known.end(), it.key()) !=
                    known.end())
                    continue;
                if (!ignored.empty()) ignored += " ";
                ignored += it.key() + "=" + brief(it.value());
            }
            if (!ignored.empty())
                fprintf(stderr, "[HANABI_DUMP]  #%zu ignored: %s\n", n,
                        ignored.c_str());
        }
        ++n;
    }
    fflush(stderr);
}

// Dump the parsed message fields + raw block types for the first `limit`
// messages of a transcript.
void dump_transcript(const json& arr, const Config& cfg, size_t limit) {
    fprintf(stderr, "[HANABI_DUMP] transcript: %zu messages\n", arr.size());
    size_t n = 0;
    for (const auto& e : arr) {
        if (n >= limit) break;
        const std::string role = as_string(e, cfg.field_role);
        std::string text = as_string(e, cfg.field_text);
        std::string subtitle;
        std::string blockTypes;
        if (e.is_object() && e.contains(cfg.field_blocks) &&
            e.at(cfg.field_blocks).is_array()) {
            for (const auto& b : e.at(cfg.field_blocks)) {
                if (!b.is_object()) continue;
                const std::string bt = as_string(b, cfg.field_block_type);
                if (!blockTypes.empty()) blockTypes += ",";
                blockTypes += bt;
                if (bt == cfg.field_block_text_type && text.empty())
                    text = as_string(b, cfg.field_block_content);
                else if (bt != cfg.field_block_text_type && subtitle.empty())
                    subtitle = bt;
            }
        }
        fprintf(stderr,
                "[HANABI_DUMP]  msg#%zu role=%s text=\"%s\" subtitle=%s "
                "blockTypes=[%s]\n",
                n, brief(role, 12).c_str(), brief(text, 60).c_str(),
                brief(subtitle, 20).c_str(), blockTypes.c_str());
        ++n;
    }
    fflush(stderr);
}

}  // namespace

Result<std::string> HttpClient::get(const std::string& path) {
    if (!cfg_.http_ready())
        return Result<std::string>::failure(
            "http backend not configured (set HANABI_API_BASE_URL)");

    SplitUrl s = split_url(cfg_.base_url);

    // Guard the https-without-TLS case explicitly: without a TLS build,
    // httplib's client throws std::invalid_argument on an https origin. Catch
    // it (and any other transport error) and return a clean failure Result so
    // the app degrades to an error / mock fallback instead of aborting.
#ifndef HANABI_ENABLE_TLS
    if (s.origin.rfind("https://", 0) == 0)
        return Result<std::string>::failure(
            "https backend requires a TLS build (rebuild with HANABI_TLS=1)");
#endif

    try {
        httplib::Client cli(s.origin.c_str());
        cli.set_connection_timeout(5, 0);
        cli.set_read_timeout(10, 0);
        cli.set_follow_location(true);

        httplib::Headers headers;
        if (!cfg_.token.empty())
            headers.emplace("Authorization", "Bearer " + cfg_.token);
        headers.emplace("Accept", "application/json");

        auto res = cli.Get((s.prefix + path).c_str(), headers);
        if (!res)
            return Result<std::string>::failure("request failed (no response)");
        if (res->status < 200 || res->status >= 300)
            return Result<std::string>::failure(
                "http status " + std::to_string(res->status));
        return Result<std::string>::success(res->body);
    } catch (const std::exception& ex) {
        return Result<std::string>::failure(std::string("request failed: ") +
                                            ex.what());
    }
}

// Authenticated JSON POST. Mirrors get()'s TLS guard + timeout + error
// handling; only the verb + body + content-type differ. Nothing about any
// endpoint is baked in — the caller supplies the path (from cfg.chat_path).
Result<std::string> HttpClient::post_json(const std::string& path,
                                          const std::string& body) {
    if (!cfg_.http_ready())
        return Result<std::string>::failure(
            "http backend not configured (set HANABI_API_BASE_URL)");

    SplitUrl s = split_url(cfg_.base_url);

#ifndef HANABI_ENABLE_TLS
    if (s.origin.rfind("https://", 0) == 0)
        return Result<std::string>::failure(
            "https backend requires a TLS build (rebuild with HANABI_TLS=1)");
#endif

    try {
        httplib::Client cli(s.origin.c_str());
        cli.set_connection_timeout(5, 0);
        cli.set_read_timeout(30, 0);  // a reply may take longer than a GET
        cli.set_follow_location(true);

        httplib::Headers headers;
        if (!cfg_.token.empty())
            headers.emplace("Authorization", "Bearer " + cfg_.token);
        headers.emplace("Accept", "application/json");

        auto res = cli.Post((s.prefix + path).c_str(), headers, body,
                            "application/json");
        if (!res)
            return Result<std::string>::failure("request failed (no response)");
        if (res->status < 200 || res->status >= 300)
            return Result<std::string>::failure(
                "http status " + std::to_string(res->status));
        return Result<std::string>::success(res->body);
    } catch (const std::exception& ex) {
        return Result<std::string>::failure(std::string("request failed: ") +
                                            ex.what());
    }
}

Result<std::vector<SessionSummary>> HttpClient::list_sessions() {    auto raw = get(cfg_.sessions_path);
    if (!raw.ok)
        return Result<std::vector<SessionSummary>>::failure(raw.error);

    std::vector<SessionSummary> out;
    try {
        json j = json::parse(raw.value);
        // Accept either a bare array or an object wrapping an array under a
        // common key ("sessions" / "data" / "items").
        const json* arr = nullptr;
        if (j.is_array()) {
            arr = &j;
        } else if (j.is_object()) {
            for (const char* k : {"sessions", "data", "items"}) {
                if (j.contains(k) && j.at(k).is_array()) { arr = &j.at(k); break; }
            }
        }
        if (!arr)
            return Result<std::vector<SessionSummary>>::failure(
                "unexpected response shape for session list");

        if (dump_enabled()) dump_session_list(*arr, cfg_, 10);

        for (const auto& e : *arr) {
            SessionSummary s;
            s.id = as_string(e, cfg_.field_id);
            s.title = as_string(e, cfg_.field_title);
            s.updated_at = as_epoch_seconds(e, cfg_.field_updated_at);
            s.status = as_string(e, cfg_.field_status);
            s.preview = as_string(e, cfg_.field_preview);
            s.starred = as_bool(e, "isPinned");
            // Real folder membership: the backend groups sessions by workspace.
            // Parse the workspace NAME into s.folder so the sidebar can group by
            // the real folder tree (empty => unfoldered, lands in the catch-all).
            // (As of 2026-08-02 the /api/v1 list returns workspace=null for all
            // rows; a backend PR to populate it is in flight. This wiring means
            // real folders appear automatically once the field is populated.)
            s.folder = as_string(e, "workspace");
            // Derive hanabi's high-signal attention model (state/tag) from the
            // generic real primitives so real sessions get meaningful triage
            // instead of all landing Unknown. See derive_state() for the full
            // heuristic set — all title-text + generic-field, nothing about any
            // specific endpoint is assumed, and a signal-less backend degrades
            // to an all-calm list.
            if (e.is_object()) derive_state(s, e);
            out.push_back(std::move(s));
        }
    } catch (const std::exception& ex) {
        return Result<std::vector<SessionSummary>>::failure(
            std::string("json parse error: ") + ex.what());
    }
    return Result<std::vector<SessionSummary>>::success(std::move(out));
}

Result<Session> HttpClient::get_session(const std::string& id) {
    auto raw = get(replace_id(cfg_.messages_path, id));
    if (!raw.ok) return Result<Session>::failure(raw.error);

    Session session;
    session.summary.id = id;
    try {
        json j = json::parse(raw.value);
        const json* arr = nullptr;
        if (j.is_array()) {
            arr = &j;
        } else if (j.is_object() && j.contains(cfg_.field_messages) &&
                   j.at(cfg_.field_messages).is_array()) {
            arr = &j.at(cfg_.field_messages);
        }
        if (!arr)
            return Result<Session>::failure(
                "unexpected response shape for transcript");

        if (dump_enabled()) dump_transcript(*arr, cfg_, 5);

        for (const auto& e : *arr) {
            Message m = parse_message(e, cfg_);
            m.created_at = as_epoch_seconds(e, cfg_.field_created_at);
            session.messages.push_back(std::move(m));
        }
    } catch (const std::exception& ex) {
        return Result<Session>::failure(
            std::string("json parse error: ") + ex.what());
    }
    return Result<Session>::success(std::move(session));
}

// --- Send: kickoff + reply (Phase SEND) -------------------------------------
// Both POST to the single configurable chat path. A body WITHOUT a session id
// is a kickoff (response carries a new session id); a body WITH one is a reply
// (response carries the assistant message(s)). Field names come from Config —
// nothing about any endpoint is baked in.
//
// SIMPLIFICATION (noted per spec): if the real backend streams the reply over
// SSE, this synchronous "POST returns the created message(s)" shape is the
// adapter simplification for this phase. SSE streaming of the live reply stays
// a separate deferred item; the seam (send_message returning a Message) is
// unchanged when it lands — only the transport underneath swaps.
Result<std::string> HttpClient::create_session(const std::string& prompt) {
    if (!cfg_.send_ready())
        return Result<std::string>::failure(
            "http backend not configured for sending (set HANABI_CHAT_PATH)");

    // Body: { <field_prompt>: "<prompt>" }  (no session id => kickoff).
    json body;
    body[cfg_.field_prompt] = prompt;
    auto raw = post_json(cfg_.chat_path, body.dump());
    if (!raw.ok) return Result<std::string>::failure(raw.error);

    try {
        json j = json::parse(raw.value);
        // The new id may be at the top level or nested under a "session" object.
        if (j.is_object() && j.contains(cfg_.field_id))
            return Result<std::string>::success(as_string(j, cfg_.field_id));
        if (j.is_object() && j.contains("session") &&
            j.at("session").is_object() &&
            j.at("session").contains(cfg_.field_id))
            return Result<std::string>::success(
                as_string(j.at("session"), cfg_.field_id));
        return Result<std::string>::failure(
            "kickoff response missing session id field");
    } catch (const std::exception& ex) {
        return Result<std::string>::failure(std::string("json parse error: ") +
                                            ex.what());
    }
}

Result<Message> HttpClient::send_message(const std::string& session_id,
                                         const std::string& prompt) {
    if (!cfg_.send_ready())
        return Result<Message>::failure(
            "http backend not configured for sending (set HANABI_CHAT_PATH)");

    // Body: { <field_session_id>: "<id>", <field_prompt>: "<prompt>" }.
    json body;
    body[cfg_.field_session_id] = session_id;
    body[cfg_.field_prompt] = prompt;
    auto raw = post_json(cfg_.chat_path, body.dump());
    if (!raw.ok) return Result<Message>::failure(raw.error);

    try {
        json j = json::parse(raw.value);
        // The reply may be: a single message object; an array of messages; or
        // an object wrapping an array under field_messages. In every case we
        // return the LAST message that reads as the assistant's reply (the new
        // turn), matching send_message's "return the assistant Message"
        // contract. The loader is free to append it to the open transcript.
        const json* arr = nullptr;
        if (j.is_array()) {
            arr = &j;
        } else if (j.is_object() && j.contains(cfg_.field_messages) &&
                   j.at(cfg_.field_messages).is_array()) {
            arr = &j.at(cfg_.field_messages);
        }
        if (arr) {
            if (arr->empty())
                return Result<Message>::failure("reply response was empty");
            // Prefer the last assistant message; fall back to the last message.
            const json* pick = nullptr;
            for (const auto& e : *arr) {
                if (!e.is_object()) continue;
                if (as_string(e, cfg_.field_role) == "assistant") pick = &e;
            }
            if (!pick) pick = &arr->back();
            Message m = parse_message(*pick, cfg_);
            m.created_at = as_epoch_seconds(*pick, cfg_.field_created_at);
            return Result<Message>::success(std::move(m));
        }
        if (j.is_object()) {
            // Single message object (possibly nested under "message").
            const json& obj =
                (j.contains("message") && j.at("message").is_object())
                    ? j.at("message")
                    : j;
            Message m = parse_message(obj, cfg_);
            m.created_at = as_epoch_seconds(obj, cfg_.field_created_at);
            if (m.text.empty() && m.id.empty())
                return Result<Message>::failure(
                    "reply response missing message fields");
            return Result<Message>::success(std::move(m));
        }
        return Result<Message>::failure("unexpected reply response shape");
    } catch (const std::exception& ex) {
        return Result<Message>::failure(std::string("json parse error: ") +
                                        ex.what());
    }
}

// --- SSE streaming (Phase STREAM) -------------------------------------------
//
// Pure parser: split accumulated bytes into complete "data: {json}\n\n" frames
// and drive the sink for each. Config-mapped field names + type values, so
// nothing about any backend's event naming is baked in. Transport-free and
// unit-tested against fixture text (see tests/unit/test_stream.cpp).
bool parse_sse_chunk(const std::string& bytes, const Config& cfg,
                     const StreamSink& sink, std::string& carry,
                     std::string& assembled) {
    carry += bytes;
    bool done = false;

    // Frames are separated by a blank line ("\n\n"). Process every complete
    // frame; keep the trailing partial in `carry` for the next call.
    size_t pos = 0;
    for (;;) {
        size_t sep = carry.find("\n\n", pos);
        if (sep == std::string::npos) break;
        std::string frame = carry.substr(pos, sep - pos);
        pos = sep + 2;

        // A frame may hold several "data:" lines (SSE allows multi-line data);
        // concatenate their payloads. Ignore comment lines (":" prefix) and any
        // non-data field lines (event:/id:/retry:) — the type lives in the JSON.
        std::string data;
        size_t lp = 0;
        while (lp < frame.size()) {
            size_t nl = frame.find('\n', lp);
            std::string line =
                frame.substr(lp, nl == std::string::npos ? std::string::npos
                                                         : nl - lp);
            lp = (nl == std::string::npos) ? frame.size() : nl + 1;
            // Strip a trailing CR (CRLF line endings).
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("data:", 0) == 0) {
                std::string payload = line.substr(5);
                // A single optional leading space after "data:" is stripped.
                if (!payload.empty() && payload.front() == ' ')
                    payload.erase(0, 1);
                data += payload;
            }
        }
        if (data.empty()) continue;

        json j;
        try {
            j = json::parse(data);
        } catch (...) {
            continue;  // malformed frame: skip it, keep streaming.
        }
        if (!j.is_object()) continue;

        const std::string type = as_string(j, cfg.field_event_type);
        if (type == cfg.event_type_text) {
            const std::string t = as_string(j, cfg.field_event_text);
            assembled += t;
            sink.emit_delta(t);
        } else if (type == cfg.event_type_thinking) {
            sink.emit_event(
                StreamEvent{StreamEventKind::Thinking,
                            as_string(j, cfg.field_event_text)});
        } else if (type == cfg.event_type_tool_call) {
            sink.emit_event(
                StreamEvent{StreamEventKind::ToolCall,
                            as_string(j, cfg.field_event_text)});
        } else if (type == cfg.event_type_title_update) {
            sink.emit_event(
                StreamEvent{StreamEventKind::TitleUpdate,
                            as_string(j, cfg.field_event_title)});
        } else if (type == cfg.event_type_done) {
            sink.emit_event(StreamEvent{StreamEventKind::Done, ""});
            done = true;
        }
        // Unknown type: ignored (forward-compatible).
    }

    carry.erase(0, pos);
    return done;
}

// Stream a reply over SSE. POSTs to the configured stream path (with {id}
// substituted) and feeds the response body through parse_sse_chunk. Uses
// httplib's content-receiver so frames are parsed as they arrive; TLS-guarded
// exactly like post_json/get. On any transport failure the sink's on_error is
// invoked and the base-class fallback is NOT used (supports_stream() gated it).
void HttpClient::send_message_streaming(const std::string& session_id,
                                        const std::string& prompt,
                                        const StreamSink& sink) {
    if (!cfg_.stream_ready()) {
        // Should be unreachable (supports_stream() gates the caller), but stay
        // honest: fall back to the synchronous path.
        Client::send_message_streaming(session_id, prompt, sink);
        return;
    }

    SplitUrl s = split_url(cfg_.base_url);
    const std::string path = replace_id(cfg_.stream_path, session_id);

#ifndef HANABI_ENABLE_TLS
    if (s.origin.rfind("https://", 0) == 0) {
        sink.emit_error(
            "https backend requires a TLS build (rebuild with HANABI_TLS=1)");
        return;
    }
#endif

    // Request body mirrors send_message: { <field_session_id>: id,
    // <field_prompt>: prompt }.
    json body;
    body[cfg_.field_session_id] = session_id;
    body[cfg_.field_prompt] = prompt;
    const std::string bodyStr = body.dump();

    std::string carry;
    std::string assembled;
    bool sawDone = false;

    try {
        httplib::Client cli(s.origin.c_str());
        cli.set_connection_timeout(5, 0);
        cli.set_read_timeout(60, 0);  // a streamed reply can run a while.
        cli.set_follow_location(true);

        httplib::Headers headers;
        if (!cfg_.token.empty())
            headers.emplace("Authorization", "Bearer " + cfg_.token);
        headers.emplace("Accept", "text/event-stream");

        auto res = cli.Post(
            (s.prefix + path).c_str(), headers, bodyStr, "application/json",
            [&](const char* data, size_t len) -> bool {
                bool d = parse_sse_chunk(std::string(data, len), cfg_, sink,
                                         carry, assembled);
                if (d) sawDone = true;
                return true;  // keep receiving.
            });
        if (!res) {
            sink.emit_error("stream request failed (no response)");
            return;
        }
        if (res->status < 200 || res->status >= 300) {
            sink.emit_error("http status " + std::to_string(res->status));
            return;
        }
    } catch (const std::exception& ex) {
        sink.emit_error(std::string("stream request failed: ") + ex.what());
        return;
    }

    // Assemble the final Message from the accumulated text deltas and hand it
    // to on_done (the parser reported the Done EVENT; finalization is ours).
    (void)sawDone;
    Message final;
    final.role = Role::Assistant;
    final.text = assembled;
    sink.emit_done(final);
}

// --- Device-code auth transport (Phase AUTH) --------------------------------
// The real HTTP calls behind DeviceCodeFlow. The auth endpoints are SIBLINGS
// of the API (e.g. /api/cli/auth/* is NOT under base_url's /api/v1 prefix), so
// this uses the ORIGIN (scheme+host) — cfg.auth_base_url when set, else
// base_url's origin (its path prefix is dropped). Nothing about any HOST is
// hardcoded. TLS-guarded exactly like get(): an https origin without a TLS
// build fails cleanly (no abort), surfaced by the flow as a Failed state.
// `method` is "GET" (poll; `query` appended as "?...") or "POST" (code; JSON
// `body`).
AuthTransport make_http_auth_transport(const Config& cfg) {
    return [cfg](const std::string& method, const std::string& path,
                 const std::string& query,
                 const std::string& body) -> AuthResponse {
        AuthResponse out;
        // Auth base: explicit auth_base_url, else the ORIGIN of base_url (drop
        // any path prefix — the auth paths are siblings of the API prefix).
        const std::string base =
            !cfg.auth_base_url.empty() ? cfg.auth_base_url : cfg.base_url;
        if (base.empty()) {
            out.error = "auth transport: no base URL configured";
            return out;
        }
        SplitUrl s = split_url(base);
        // Deliberately ignore s.prefix: auth endpoints are origin-relative.

#ifndef HANABI_ENABLE_TLS
        if (s.origin.rfind("https://", 0) == 0) {
            out.error =
                "https auth requires a TLS build (rebuild with HANABI_TLS=1)";
            return out;
        }
#endif
        try {
            httplib::Client cli(s.origin.c_str());
            cli.set_connection_timeout(5, 0);
            cli.set_read_timeout(15, 0);
            cli.set_follow_location(true);

            httplib::Headers headers;
            headers.emplace("Accept", "application/json");

            std::string full = path;
            if (method == "GET" && !query.empty()) full += "?" + query;

            httplib::Result res =
                (method == "GET")
                    ? cli.Get(full.c_str(), headers)
                    : cli.Post(path.c_str(), headers, body, "application/json");
            if (!res) {
                out.error = "auth request failed (no response)";
                return out;
            }
            out.status = res->status;
            out.body = res->body;
            // Hand the status + body back and let DeviceCodeFlow interpret it
            // (it treats a non-2xx as a hard failure). Only a genuinely
            // empty/absent response is a transport error (handled above).
            out.ok = true;
            return out;
        } catch (const std::exception& ex) {
            out.error = std::string("auth request failed: ") + ex.what();
            return out;
        }
    };
}

}  // namespace api
