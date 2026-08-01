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

Result<std::vector<SessionSummary>> HttpClient::list_sessions() {
    auto raw = get(cfg_.sessions_path);
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
            Message m;
            m.id = as_string(e, cfg_.field_id);
            m.role = parse_role(as_string(e, cfg_.field_role));
            m.created_at = as_epoch_seconds(e, cfg_.field_created_at);
            // Prefer a flat text field; if the message instead carries a blocks
            // array, concatenate the content of its text-type blocks. Also note
            // any non-text block (e.g. a tool call) as a subtitle hint.
            m.text = as_string(e, cfg_.field_text);
            if (m.text.empty() && e.contains(cfg_.field_blocks) &&
                e.at(cfg_.field_blocks).is_array()) {
                std::string joined;
                for (const auto& b : e.at(cfg_.field_blocks)) {
                    if (!b.is_object()) continue;
                    const std::string btype = as_string(b, cfg_.field_block_type);
                    if (btype == cfg_.field_block_text_type) {
                        const std::string c =
                            as_string(b, cfg_.field_block_content);
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
            session.messages.push_back(std::move(m));
        }
    } catch (const std::exception& ex) {
        return Result<Session>::failure(
            std::string("json parse error: ") + ex.what());
    }
    return Result<Session>::success(std::move(session));
}

}  // namespace api
