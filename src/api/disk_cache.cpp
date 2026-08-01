#include "disk_cache.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "../../vendor/nlohmann/json.hpp"

namespace api::disk_cache {

namespace fs = std::filesystem;
using json = nlohmann::json;

// Per-backend cache namespace (see set_namespace). Empty = flat layout.
namespace {
std::string g_namespace;

// Short, filesystem-safe, stable token derived from an arbitrary key (e.g. a
// base URL). A tiny FNV-1a hash rendered hex — enough to separate distinct
// backends without leaking the URL into a path or colliding in practice.
std::string ns_token(const std::string& key) {
    std::uint64_t h = 1469598103934665603ULL;  // FNV-1a offset basis
    for (unsigned char c : key) {
        h ^= c;
        h *= 1099511628211ULL;  // FNV prime
    }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(h));
    return std::string(buf);
}
}  // namespace

void set_namespace(const std::string& key) {
    g_namespace = key.empty() ? std::string() : ns_token(key);
}

// ---- path resolution (mirrors config.cpp / token_store.cpp) --------------
std::string cache_dir() {
    fs::path base;
    // Explicit override (used by tests to isolate from a real user cache).
    if (const char* p = std::getenv("HANABI_CACHE_DIR"); p && *p)
        base = p;
    else if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        base = fs::path(xdg) / "hanabi" / "cache";
    else if (const char* home = std::getenv("HOME"); home && *home)
        base = fs::path(home) / ".config" / "hanabi" / "cache";
    else
        return "";
    // Scope to the active backend when a namespace is set, so distinct backends
    // never share (and pollute) each other's cache files.
    if (!g_namespace.empty()) base /= g_namespace;
    return base.string();
}

namespace {

bool ensure_dir(const std::string& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    fs::create_directories(dir, ec);
    return !ec;
}

// A session id may contain characters unsafe for a filename; keep only
// filename-safe chars and fall back to a short hash-ish tail otherwise.
std::string safe_name(const std::string& id) {
    std::string out;
    out.reserve(id.size());
    for (char c : id) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_')
            out.push_back(c);
        else
            out.push_back('_');
    }
    if (out.empty()) out = "unnamed";
    if (out.size() > 120) out = out.substr(0, 120);
    return out;
}

// ---- (de)serialization ---------------------------------------------------
json to_json(const SessionSummary& s) {
    return json{{"id", s.id},
                {"title", s.title},
                {"updated_at", s.updated_at},
                {"status", s.status},
                {"preview", s.preview},
                {"state", static_cast<int>(s.state)},
                {"tag", static_cast<int>(s.tag)},
                {"folder", s.folder},
                {"starred", s.starred}};
}

SessionSummary summary_from_json(const json& j) {
    SessionSummary s;
    s.id = j.value("id", "");
    s.title = j.value("title", "");
    s.updated_at = j.value("updated_at", (int64_t)0);
    s.status = j.value("status", "");
    s.preview = j.value("preview", "");
    s.state = static_cast<ThreadState>(
        j.value("state", static_cast<int>(ThreadState::Unknown)));
    s.tag = static_cast<ThreadTag>(
        j.value("tag", static_cast<int>(ThreadTag::None)));
    s.folder = j.value("folder", "");
    s.starred = j.value("starred", false);
    return s;
}

json to_json(const Message& m) {
    return json{{"id", m.id},
                {"role", static_cast<int>(m.role)},
                {"text", m.text},
                {"created_at", m.created_at},
                {"subtitle", m.subtitle}};
}

Message message_from_json(const json& j) {
    Message m;
    m.id = j.value("id", "");
    m.role = static_cast<Role>(j.value("role", static_cast<int>(Role::Assistant)));
    m.text = j.value("text", "");
    m.created_at = j.value("created_at", (int64_t)0);
    m.subtitle = j.value("subtitle", "");
    return m;
}

json to_json(const SubAgent& a) {
    return json{{"id", a.id},
                {"title", a.title},
                {"state", static_cast<int>(a.state)},
                {"note", a.note}};
}

SubAgent subagent_from_json(const json& j) {
    SubAgent a;
    a.id = j.value("id", "");
    a.title = j.value("title", "");
    a.state = static_cast<SubAgentState>(
        j.value("state", static_cast<int>(SubAgentState::Running)));
    a.note = j.value("note", "");
    return a;
}

std::string sessions_file() {
    const std::string dir = cache_dir();
    return dir.empty() ? "" : (fs::path(dir) / "sessions.json").string();
}

std::string transcript_file(const std::string& id) {
    const std::string dir = cache_dir();
    return dir.empty() ? ""
                       : (fs::path(dir) / ("tx_" + safe_name(id) + ".json"))
                             .string();
}

// Atomic-ish write: write to a temp file then rename, so a crash mid-write
// never leaves a half-written cache file that fails to parse.
bool write_file(const std::string& path, const std::string& content) {
    if (path.empty()) return false;
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out.good()) return false;
        out << content;
        if (!out.good()) return false;
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

}  // namespace

// ---- session list --------------------------------------------------------
void save_sessions(const std::vector<SessionSummary>& sessions) {
    if (!ensure_dir(cache_dir())) return;
    json arr = json::array();
    for (const auto& s : sessions) arr.push_back(to_json(s));
    json doc{{"version", 1}, {"sessions", std::move(arr)}};
    write_file(sessions_file(), doc.dump());
}

std::optional<std::vector<SessionSummary>> load_sessions() {
    const std::string path = sessions_file();
    if (path.empty()) return std::nullopt;
    std::ifstream in(path);
    if (!in.good()) return std::nullopt;
    try {
        json doc;
        in >> doc;
        if (!doc.contains("sessions") || !doc["sessions"].is_array())
            return std::nullopt;
        std::vector<SessionSummary> out;
        for (const auto& e : doc["sessions"]) out.push_back(summary_from_json(e));
        return out;
    } catch (...) {
        return std::nullopt;
    }
}

// ---- transcripts ---------------------------------------------------------
void save_transcript(const Session& session) {
    if (session.summary.id.empty()) return;
    if (!ensure_dir(cache_dir())) return;
    json msgs = json::array();
    for (const auto& m : session.messages) msgs.push_back(to_json(m));
    json subs = json::array();
    for (const auto& a : session.sub_agents) subs.push_back(to_json(a));
    json doc{{"version", 1},
             {"summary", to_json(session.summary)},
             {"messages", std::move(msgs)},
             {"sub_agents", std::move(subs)}};
    write_file(transcript_file(session.summary.id), doc.dump());
}

std::optional<Session> load_transcript(const std::string& id) {
    const std::string path = transcript_file(id);
    if (path.empty()) return std::nullopt;
    std::ifstream in(path);
    if (!in.good()) return std::nullopt;
    try {
        json doc;
        in >> doc;
        if (!doc.contains("summary")) return std::nullopt;
        Session s;
        s.summary = summary_from_json(doc["summary"]);
        if (doc.contains("messages") && doc["messages"].is_array())
            for (const auto& e : doc["messages"])
                s.messages.push_back(message_from_json(e));
        if (doc.contains("sub_agents") && doc["sub_agents"].is_array())
            for (const auto& e : doc["sub_agents"])
                s.sub_agents.push_back(subagent_from_json(e));
        return s;
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace api::disk_cache
