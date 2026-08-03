#include "http_client.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <thread>

#include "../../vendor/nlohmann/json.hpp"

// cpp-httplib is header-only. We keep the implementation macro local to this
// translation unit so the rest of the app never pulls in networking headers.
// TLS support is opt-in at build time (define HANABI_ENABLE_TLS and link
// OpenSSL) so the default build has zero extra dependencies.
#ifdef HANABI_ENABLE_TLS
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include <httplib.h>

#ifdef HANABI_ENABLE_TLS
#include <mutex>
#include <openssl/crypto.h>
#include <openssl/provider.h>
#include <openssl/ssl.h>
#endif

namespace api {

using json = nlohmann::json;

namespace {

#ifdef HANABI_ENABLE_TLS
// Force a single, thread-safe OpenSSL initialization BEFORE any worker thread
// runs a TLS handshake, AND serialize the handshake itself.
//
// Root cause of the intermittent SIGSEGV on a large real-backend transcript:
// opening a big thread kicks concurrent std::async fetches on separate worker
// threads — the initial newest-N transcript load, the "load older" full re-fetch
// (drive_load_older), and per-tab live (SSE) refetches. Each request builds its
// OWN httplib::Client on the stack, and for an https origin the first TLS
// handshake lazily initializes OpenSSL 3 state (the default provider + the
// EVP/OBJ algorithm-name tables, reached via tls_process_server_hello ->
// int_ctx_new -> evp_pkey_name2type). When TWO worker threads run that first-
// touch init simultaneously, OpenSSL's run-once machinery races and a
// CRYPTO_THREAD lock is read before it is created — a null pthread_rwlock deref
// (EXC_BAD_ACCESS at 0x0). It only reproduces when two fetches overlap, which is
// exactly what the big transcript triggers (hence "fast standalone / never under
// lldb/ASan": those change the timing so the two inits don't collide).
//
// Fix, two layers:
//  (1) std::call_once forces OPENSSL_init_ssl + loading the default provider
//      once, single-threaded, so the lazy tables exist before any handshake.
//  (2) A process-wide mutex serializes each get()/post_json() request (both run
//      on background worker threads, never the UI thread), so two TLS handshakes
//      can never build OpenSSL's lazily-created per-op locks concurrently. This
//      only affects the short JSON fetches; long-lived SSE streaming/subscribe
//      keep their own separate path and are not serialized here.
void ensure_openssl_init() {
    static std::once_flag once;
    std::call_once(once, [] {
        OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS |
                             OPENSSL_INIT_LOAD_CRYPTO_STRINGS,
                         nullptr);
        // Force the default provider (and its property-store locks) to be
        // created now, on one thread, so a concurrent first handshake doesn't
        // race creating it.
        OSSL_PROVIDER_load(nullptr, "default");
    });
}

// Serializes TLS handshakes across the concurrent fetch workers (see above).
std::mutex& http_request_mutex() {
    static std::mutex m;
    return m;
}
#else
inline void ensure_openssl_init() {}
#endif

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

// Build the request target from a SplitUrl + a configured path. Normally a
// path is relative to the base_url's prefix (e.g. base .../api/v1 + "/sessions"
// -> "/api/v1/sessions"). But some endpoints live at a DIFFERENT root than the
// data API (e.g. the Navi chat/send + steer endpoints sit at "/api/chat" while
// sessions/messages are under "/api/v1"). A configured path beginning with "//"
// is treated as ORIGIN-ABSOLUTE: the prefix is skipped and the single leading
// slash of the remainder is kept (so chat_path "//api/chat" -> origin+"/api/chat").
// This keeps every endpoint fully config-driven — nothing product-specific is
// baked into the binary; the local config chooses the roots.
std::string resolve_target(const SplitUrl& s, const std::string& path) {
    if (path.rfind("//", 0) == 0) return path.substr(1);  // origin-absolute
    return s.prefix + path;
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

// Read a signed-integer field, tolerating a JSON int, float, or numeric
// string. Returns `dflt` (default -1 = "unknown") when absent/unparseable.
int64_t as_int(const json& obj, const std::string& key, int64_t dflt = -1) {
    if (!obj.is_object() || !obj.contains(key)) return dflt;
    const auto& v = obj.at(key);
    if (v.is_number_integer()) return v.get<int64_t>();
    if (v.is_number_unsigned()) return static_cast<int64_t>(v.get<uint64_t>());
    if (v.is_number_float()) return static_cast<int64_t>(v.get<double>());
    if (v.is_string()) {
        try {
            return static_cast<int64_t>(std::stoll(v.get<std::string>()));
        } catch (...) {
            return dflt;
        }
    }
    return dflt;
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
    const bool hasBlocks = e.is_object() && e.contains(cfg.field_blocks) &&
                           e.at(cfg.field_blocks).is_array();
    if (m.text.empty() && hasBlocks) {
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
            } else if (!btype.empty() && m.subtitle.empty() &&
                       btype != cfg.field_block_image_type) {
                m.subtitle = btype;
            }
        }
        m.text = std::move(joined);
    }
    // Image blocks are scanned INDEPENDENTLY of the text path — an assistant
    // message commonly has BOTH prose (in field_text) AND an image block, so
    // this must run even when m.text came from field_text directly. Render only
    // a LOCAL path / file:// URL (never block the transcript on a network
    // fetch); a remote http(s) image is left for a future download-to-cache.
    if (m.image_path.empty() && hasBlocks) {
        for (const auto& b : e.at(cfg.field_blocks)) {
            if (!b.is_object()) continue;
            if (as_string(b, cfg.field_block_type) != cfg.field_block_image_type)
                continue;
            std::string url = as_string(b, cfg.field_block_image_url);
            if (url.rfind("file://", 0) == 0) {
                m.image_path = url.substr(7);
                break;
            } else if (!url.empty() && url.front() == '/') {
                m.image_path = url;
                break;
            }
        }
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

// --- Tool-call block splitting ----------------------------------------------
// Real assistant messages are a SEQUENCE of interleaved blocks
// [text, tool_call, tool_result, text, ...]; the backend only uses roles
// user/assistant (tool activity lives INSIDE assistant blocks). To make the
// transcript's rich tool-row renderer (which triggers on Role::Tool messages)
// fire on real data, we split such a message, IN ORDER, into:
//   * runs of text blocks -> a Role::Assistant message (joined text), and
//   * each tool_call (+ its matching tool_result by toolCallId) -> a
//     Role::Tool message carrying real name/command/output/status/duration.
// A message with no blocks (or only text) yields exactly one message, matching
// the old parse_message behavior. Nothing here is endpoint-specific — every
// key comes from Config.
std::vector<Message> split_message_blocks(const json& e, const Config& cfg) {
    // Small local helpers (mirror the anon-namespace ones but usable here).
    auto sfield = [](const json& o, const std::string& k) -> std::string {
        if (o.is_object() && o.contains(k) && o.at(k).is_string())
            return o.at(k).get<std::string>();
        return "";
    };
    auto efield = [](const json& o, const std::string& k) -> int64_t {
        if (!o.is_object() || !o.contains(k)) return 0;
        const auto& v = o.at(k);
        if (v.is_number_integer()) return v.get<int64_t>();
        if (v.is_number_float()) return static_cast<int64_t>(v.get<double>());
        return 0;
    };

    const int64_t msg_created = efield(e, cfg.field_created_at);
    const Role role = [&] {
        const std::string r = sfield(e, cfg.field_role);
        if (r == "user") return Role::User;
        if (r == "assistant") return Role::Assistant;
        if (r == "system") return Role::System;
        if (r == "tool") return Role::Tool;
        return Role::Assistant;
    }();
    const std::string msg_id = sfield(e, cfg.field_id);

    std::vector<Message> out;

    const bool has_blocks = e.is_object() && e.contains(cfg.field_blocks) &&
                            e.at(cfg.field_blocks).is_array();
    if (!has_blocks) {
        // No blocks: single flat-text message (unchanged behavior).
        Message m;
        m.id = msg_id;
        m.role = role;
        m.text = sfield(e, cfg.field_text);
        m.created_at = msg_created;
        out.push_back(std::move(m));
        return out;
    }

    const json& blocks = e.at(cfg.field_blocks);

    // First pass: collect tool_result blocks so a tool_call can find its
    // matching result by toolCallId regardless of block ordering.
    std::vector<const json*> tool_results;
    for (const auto& b : blocks) {
        if (!b.is_object()) continue;
        if (sfield(b, cfg.field_block_type) == cfg.field_block_tool_result_type)
            tool_results.push_back(&b);
    }
    auto find_result = [&](const std::string& call_id) -> const json* {
        for (const json* tr : tool_results) {
            const json& ro = (tr->contains(cfg.field_tool_result_obj) &&
                              tr->at(cfg.field_tool_result_obj).is_object())
                                 ? tr->at(cfg.field_tool_result_obj)
                                 : *tr;
            if (sfield(ro, cfg.field_tool_result_call_id) == call_id)
                return &ro;
        }
        return nullptr;
    };

    // Second pass: walk blocks in order, coalescing text runs and emitting a
    // Role::Tool message for each tool_call.
    std::string text_run;
    int text_seq = 0;  // disambiguates multiple text fragments of ONE message
    auto flush_text = [&] {
        if (text_run.empty()) return;
        Message m;
        // A single parent message can split into SEVERAL text fragments
        // (text, tool, text, tool, text...). They must NOT share an id: the
        // renderer's measure cache is keyed by message id, so identical ids
        // make every fragment collide onto the FIRST fragment's cached height
        // — corrupting the virtualized layout (a 127-block message yields ~10
        // text fragments, all mis-measured → messages render at wrong/zero
        // height and effectively vanish). Suffix each fragment uniquely.
        m.id = msg_id.empty() ? ("txt" + std::to_string(text_seq))
                              : (msg_id + "-t" + std::to_string(text_seq));
        ++text_seq;
        m.role = role;  // usually Assistant.
        m.text = text_run;
        m.created_at = msg_created;
        out.push_back(std::move(m));
        text_run.clear();
    };

    for (const auto& b : blocks) {
        if (!b.is_object()) continue;
        const std::string bt = sfield(b, cfg.field_block_type);
        if (bt == cfg.field_block_text_type) {
            const std::string c = sfield(b, cfg.field_block_content);
            if (!c.empty()) {
                if (!text_run.empty()) text_run += "\n\n";
                text_run += c;
            }
        } else if (bt == cfg.field_block_tool_call_type) {
            flush_text();  // preserve order: text before this tool call.
            const json& tc = (b.contains(cfg.field_tool_call_obj) &&
                              b.at(cfg.field_tool_call_obj).is_object())
                                 ? b.at(cfg.field_tool_call_obj)
                                 : b;
            Message m;
            m.role = Role::Tool;
            const std::string call_id = sfield(tc, cfg.field_tool_id);
            m.id = !call_id.empty() ? call_id : (msg_id + "-tool");
            // Name -> subtitle (the renderer's tool label + wrench icon key).
            m.subtitle = sfield(tc, cfg.field_tool_name);
            // Command line -> text. inputs may be a string (opaque blob) or an
            // object; best-effort surface command (+ node) else the raw blob.
            if (tc.contains(cfg.field_tool_inputs)) {
                const json& in = tc.at(cfg.field_tool_inputs);
                std::string cmd, node;
                if (in.is_object()) {
                    cmd = sfield(in, cfg.field_tool_input_command);
                    node = sfield(in, cfg.field_tool_input_node);
                } else if (in.is_string()) {
                    // Stringified dict: try to parse it, else show truncated.
                    const std::string raw = in.get<std::string>();
                    try {
                        json parsed = json::parse(raw);
                        if (parsed.is_object()) {
                            cmd = sfield(parsed, cfg.field_tool_input_command);
                            node = sfield(parsed, cfg.field_tool_input_node);
                        }
                    } catch (...) {
                    }
                    if (cmd.empty()) {
                        cmd = raw.size() > 200 ? raw.substr(0, 200) + "\xe2\x80\xa6"
                                               : raw;
                    }
                }
                if (!node.empty())
                    m.text = "[" + node + "] " + cmd;
                else
                    m.text = cmd;
                m.tool_node = node;  // dedicated field (renderer prefers this)
            }
            // Timestamps: tool piece uses the call's startedAt when present.
            const int64_t started = efield(tc, cfg.field_tool_started_at);
            m.created_at = started != 0 ? started : msg_created;
            // Match the result (by toolCallId) for output/status/duration.
            if (const json* ro = find_result(call_id)) {
                m.tool_result = sfield(*ro, cfg.field_tool_output);
                m.tool_status = sfield(*ro, cfg.field_tool_status);
                const int64_t completed =
                    efield(*ro, cfg.field_tool_completed_at);
                if (started != 0 && completed != 0 && completed >= started)
                    m.tool_duration_ms = completed - started;
            }
            out.push_back(std::move(m));
        } else if (bt == cfg.field_block_tool_result_type) {
            // consumed via find_result — nothing to emit here.
        } else {
            // Any OTHER block type (error, thinking, etc.): surface its text
            // content if it carries any, so a message whose only block is an
            // `error` renders the error text instead of a blank empty bubble
            // (M1 — an error-only assistant message was showing as an empty
            // grey box). Common text-bearing keys: content, then text.
            std::string c = sfield(b, cfg.field_block_content);
            if (c.empty()) c = sfield(b, "text");
            if (!c.empty()) {
                if (!text_run.empty()) text_run += "\n\n";
                text_run += c;
            }
        }
        // tool_result blocks are consumed via find_result; unknown blocks with
        // no text are ignored (forward-compatible), matching the spec.
    }
    flush_text();  // trailing text run.

    // A message whose blocks produced NOTHING renderable (e.g. an empty/steering
    // block with no text and no tool call) is DROPPED — emitting an empty turn
    // rendered as a blank grey bubble (M1). Only keep a fallback empty message
    // for a message that had NO blocks at all (handled by the has_blocks guard
    // earlier) — here, if blocks existed but yielded nothing, produce nothing.
    return out;
}

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
        ensure_openssl_init();  // thread-safe one-time OpenSSL init (see above)
#ifdef HANABI_ENABLE_TLS
        // Serialize the TLS handshake across concurrent fetch workers so the
        // first-handshake OpenSSL lazy-init can't race (see http_request_mutex).
        std::lock_guard<std::mutex> _tls_lock(http_request_mutex());
#endif
        httplib::Client cli(s.origin.c_str());
        cli.set_connection_timeout(5, 0);
        cli.set_read_timeout(10, 0);
        cli.set_follow_location(true);

        httplib::Headers headers;
        if (!cfg_.token.empty())
            headers.emplace("Authorization", "Bearer " + cfg_.token);
        headers.emplace("Accept", "application/json");

        auto res = cli.Get(resolve_target(s, path).c_str(), headers);
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
        ensure_openssl_init();  // thread-safe one-time OpenSSL init (see above)
#ifdef HANABI_ENABLE_TLS
        // Serialize the TLS handshake across concurrent fetch workers so the
        // first-handshake OpenSSL lazy-init can't race (see http_request_mutex).
        std::lock_guard<std::mutex> _tls_lock(http_request_mutex());
#endif
        httplib::Client cli(s.origin.c_str());
        cli.set_connection_timeout(5, 0);
        cli.set_read_timeout(30, 0);  // a reply may take longer than a GET
        cli.set_follow_location(true);

        httplib::Headers headers;
        if (!cfg_.token.empty())
            headers.emplace("Authorization", "Bearer " + cfg_.token);
        headers.emplace("Accept", "application/json");

        auto res = cli.Post(resolve_target(s, path).c_str(), headers, body,
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
    return get_session(id, 0);  // 0 = no limit (full transcript).
}

// Read user settings/config (feature #4). GET the configured settings_path and
// map the response onto UserSettings. On the real backend this is GET /whoami ->
// {userId, bankId, counts:{sessions, assets, schedules, authoredSkills}}. Fully
// config-driven: a different backend just overrides the field_settings_* names.
Result<UserSettings> HttpClient::get_settings() {
    if (!cfg_.settings_ready())
        return Result<UserSettings>::failure(
            "settings read not configured (set settings_path)");
    auto raw = get(cfg_.settings_path);
    if (!raw.ok) return Result<UserSettings>::failure(raw.error);
    try {
        json j = json::parse(raw.value);
        if (!j.is_object())
            return Result<UserSettings>::failure(
                "unexpected response shape for settings");
        UserSettings s;
        s.ok = true;
        s.raw_json = raw.value;
        s.user_id = as_string(j, cfg_.field_settings_user_id);
        s.bank_id = as_string(j, cfg_.field_settings_bank_id);
        // Counts live under a nested object (…/whoami: "counts":{…}); fall back
        // to the top level so a flatter backend shape still populates them.
        const json& counts = (j.contains(cfg_.field_settings_counts) &&
                              j.at(cfg_.field_settings_counts).is_object())
                                 ? j.at(cfg_.field_settings_counts)
                                 : j;
        s.session_count = as_int(counts, cfg_.field_settings_sessions);
        s.asset_count = as_int(counts, cfg_.field_settings_assets);
        s.schedule_count = as_int(counts, cfg_.field_settings_schedules);
        s.skill_count = as_int(counts, cfg_.field_settings_skills);
        return Result<UserSettings>::success(std::move(s));
    } catch (const std::exception& ex) {
        return Result<UserSettings>::failure(std::string("json parse error: ") +
                                             ex.what());
    }
}

// Append "?limit=N" (or "&limit=N" if the path already has a query) to a path.
static std::string with_limit(const std::string& path, int limit) {
    if (limit <= 0) return path;
    const char sep = (path.find('?') == std::string::npos) ? '?' : '&';
    return path + sep + "limit=" + std::to_string(limit);
}

Result<Session> HttpClient::get_session(const std::string& id, int limit) {
    const std::string path = with_limit(replace_id(cfg_.messages_path, id), limit);
    auto raw = get(path);
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
            // The windowed response is an OBJECT wrapping the array; read the
            // "hasMore" flag so the UI can tell older messages weren't loaded.
            session.has_more_older = as_bool(j, cfg_.field_has_more);
        }
        if (!arr)
            return Result<Session>::failure(
                "unexpected response shape for transcript");

        if (dump_enabled()) dump_transcript(*arr, cfg_, 5);

        for (const auto& e : *arr) {
            // SPLIT interleaved text/tool_call/tool_result blocks into an
            // ordered sequence of Role::Assistant + Role::Tool messages so the
            // transcript's rich tool-row renderer fires on real data. A message
            // with no blocks yields a single message (old behavior).
            std::vector<Message> pieces = split_message_blocks(e, cfg_);
            for (auto& m : pieces) {
                if (m.created_at == 0)
                    m.created_at = as_epoch_seconds(e, cfg_.field_created_at);
                else
                    // Normalize any ms-epoch startedAt to seconds like the rest.
                    if (m.created_at > 10000000000LL) m.created_at /= 1000;
                session.messages.push_back(std::move(m));
            }
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
// Parse a reply/steer response body into the assistant Message it carries.
// Shared by send_message and steer (both POST a {session_id, prompt} body and
// read back the assistant turn the same way). The reply may be: a single
// message object (optionally nested under "message"); a bare array of messages;
// or an object wrapping an array under field_messages. In every case return the
// LAST message that reads as the assistant's reply, matching the "return the
// assistant Message" contract. Config-mapped field names — nothing baked in.
static Result<Message> parse_reply_body(const std::string& raw,
                                        const Config& cfg) {
    try {
        json j = json::parse(raw);
        const json* arr = nullptr;
        if (j.is_array()) {
            arr = &j;
        } else if (j.is_object() && j.contains(cfg.field_messages) &&
                   j.at(cfg.field_messages).is_array()) {
            arr = &j.at(cfg.field_messages);
        }
        if (arr) {
            if (arr->empty())
                return Result<Message>::failure("reply response was empty");
            const json* pick = nullptr;
            for (const auto& e : *arr) {
                if (!e.is_object()) continue;
                if (as_string(e, cfg.field_role) == "assistant") pick = &e;
            }
            if (!pick) pick = &arr->back();
            Message m = parse_message(*pick, cfg);
            m.created_at = as_epoch_seconds(*pick, cfg.field_created_at);
            return Result<Message>::success(std::move(m));
        }
        if (j.is_object()) {
            const json& obj =
                (j.contains("message") && j.at("message").is_object())
                    ? j.at("message")
                    : j;
            Message m = parse_message(obj, cfg);
            m.created_at = as_epoch_seconds(obj, cfg.field_created_at);
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

Result<std::string> HttpClient::create_session(const std::string& prompt) {    if (!cfg_.send_ready())
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

    return parse_reply_body(raw.value, cfg_);
}

// Steer a currently-running agent. POSTs the SAME {session_id, prompt} body as
// send_message, but to the SEPARATE steer_path (routed through post_json, so
// the origin-absolute "//path" convention applies). The reply is parsed with
// the shared parse_reply_body. Requires cfg.steer_ready(); reports a clean
// failure otherwise so an unconfigured http backend never silently no-ops.
Result<Message> HttpClient::steer(const std::string& session_id,
                                  const std::string& prompt) {
    if (!cfg_.steer_ready())
        return Result<Message>::failure(
            "http backend not configured for steering (set HANABI_STEER_PATH)");

    // Body reuses the send mapping: { <field_session_id>: id, <field_prompt>: p }.
    json body;
    body[cfg_.field_session_id] = session_id;
    body[cfg_.field_prompt] = prompt;
    auto raw = post_json(cfg_.steer_path, body.dump());
    if (!raw.ok) return Result<Message>::failure(raw.error);

    return parse_reply_body(raw.value, cfg_);
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
        ensure_openssl_init();  // thread-safe one-time OpenSSL init (see above)
        httplib::Client cli(s.origin.c_str());
        cli.set_connection_timeout(5, 0);
        cli.set_read_timeout(60, 0);  // a streamed reply can run a while.
        cli.set_follow_location(true);

        httplib::Headers headers;
        if (!cfg_.token.empty())
            headers.emplace("Authorization", "Bearer " + cfg_.token);
        headers.emplace("Accept", "text/event-stream");

        auto res = cli.Post(
            resolve_target(s, path).c_str(), headers, bodyStr, "application/json",
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
            ensure_openssl_init();  // thread-safe one-time OpenSSL init (see above)
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

// --- Live events (SSE) parser -----------------------------------------------
//
// Same frame discipline as parse_sse_chunk (blank-line-delimited "data:"
// frames, CRLF-safe, carry across reads) but reads the LIVE-EVENT vocabulary
// (see Config): a top-level {type:"connected"} frame is ignored; every other
// frame's kind is event.type; the pure-telemetry kind (context_usage) is
// ignored; anything else calls sink.on_activity(kind) so the caller coalesces
// + re-fetches. Pure + transport-free (unit-tested against fixture text).
void parse_events_frame(const std::string& bytes, const Config& cfg,
                        const EventSink& sink, std::string& carry) {
    carry += bytes;
    size_t pos = 0;
    for (;;) {
        size_t sep = carry.find("\n\n", pos);
        if (sep == std::string::npos) break;
        std::string frame = carry.substr(pos, sep - pos);
        pos = sep + 2;

        // Concatenate the frame's "data:" line payloads (multi-line data ok).
        std::string data;
        size_t lp = 0;
        while (lp < frame.size()) {
            size_t nl = frame.find('\n', lp);
            std::string line =
                frame.substr(lp, nl == std::string::npos ? std::string::npos
                                                         : nl - lp);
            lp = (nl == std::string::npos) ? frame.size() : nl + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("data:", 0) == 0) {
                std::string payload = line.substr(5);
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
            continue;  // malformed frame: skip, keep the stream alive.
        }
        if (!j.is_object()) continue;

        // Top-level {type:"connected"} => stream opened; ignore.
        const std::string top = as_string(j, cfg.field_event_top_type);
        if (top == cfg.event_type_connected) continue;

        // Otherwise the meaningful kind lives under event.type. Fall back to
        // the top-level type if there's no nested object (forward-compatible).
        std::string kind;
        if (j.contains(cfg.field_events_obj) &&
            j.at(cfg.field_events_obj).is_object())
            kind = as_string(j.at(cfg.field_events_obj), cfg.field_event_type);
        if (kind.empty()) kind = top;

        // Pure telemetry (fires constantly) => ignore, never refetch.
        if (kind == cfg.event_type_ignore) continue;

        // Anything else implies the transcript may have changed: signal the
        // caller (it coalesces/debounces + re-fetches the newest-N). An empty
        // kind (unrecognized shape) is treated as activity too — default to a
        // cheap refetch rather than silently dropping a real change.
        sink.emit_activity(kind);
    }
    carry.erase(0, pos);
}

namespace {

// The concrete http subscription: owns a worker std::thread that opens the
// text/event-stream and feeds parse_events_frame, plus an atomic stop flag the
// worker checks between reads. stop() flips the flag + joins. Reconnects are
// capped so a persistently-failing stream doesn't spin.
class HttpEventSubscription : public EventSubscription {
  public:
    HttpEventSubscription(Config cfg, std::string session_id, EventSink sink)
        : cfg_(std::move(cfg)),
          session_id_(std::move(session_id)),
          sink_(std::move(sink)) {
        worker_ = std::thread([this] { run(); });
    }
    ~HttpEventSubscription() override { stop(); }

    void stop() override {
        stop_.store(true);
        if (worker_.joinable()) worker_.join();
    }

  private:
    void run() {
        SplitUrl s = split_url(cfg_.base_url);
        const std::string path = replace_id(cfg_.events_path, session_id_);

#ifndef HANABI_ENABLE_TLS
        if (s.origin.rfind("https://", 0) == 0) {
            sink_.emit_error(
                "https events require a TLS build (rebuild with HANABI_TLS=1)");
            return;
        }
#endif
        constexpr int kMaxReconnects = 5;
        std::string carry;
        for (int attempt = 0; attempt <= kMaxReconnects && !stop_.load();
             ++attempt) {
            try {
                ensure_openssl_init();  // thread-safe one-time OpenSSL init (see above)
                httplib::Client cli(s.origin.c_str());
                cli.set_connection_timeout(5, 0);
                cli.set_read_timeout(120, 0);  // long-lived stream.
                cli.set_follow_location(true);

                httplib::Headers headers;
                if (!cfg_.token.empty())
                    headers.emplace("Authorization", "Bearer " + cfg_.token);
                headers.emplace("Accept", "text/event-stream");

                auto res = cli.Get(
                    resolve_target(s, path).c_str(), headers,
                    [&](const char* data, size_t len) -> bool {
                        if (stop_.load()) return false;  // tear down.
                        parse_events_frame(std::string(data, len), cfg_, sink_,
                                           carry);
                        return !stop_.load();  // keep receiving until stopped.
                    });
                if (stop_.load()) return;  // clean stop, no error.
                if (!res) {
                    sink_.emit_error("events stream failed (no response)");
                } else if (res->status < 200 || res->status >= 300) {
                    sink_.emit_error("events http status " +
                                     std::to_string(res->status));
                }
            } catch (const std::exception& ex) {
                sink_.emit_error(std::string("events stream failed: ") +
                                 ex.what());
            }
            // Backoff a beat before reconnecting (bounded loop).
            for (int i = 0; i < 10 && !stop_.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    Config cfg_;
    std::string session_id_;
    EventSink sink_;
    std::atomic<bool> stop_{false};
    std::thread worker_;
};

}  // namespace

std::unique_ptr<EventSubscription> HttpClient::subscribe_events(
    const std::string& session_id, EventSink sink) {
    if (!cfg_.events_ready()) {
        // Not configured: a no-op handle (nothing ever fires), consistent with
        // the base-class default.
        return Client::subscribe_events(session_id, std::move(sink));
    }
    return std::make_unique<HttpEventSubscription>(cfg_, session_id,
                                                   std::move(sink));
}

}  // namespace api
