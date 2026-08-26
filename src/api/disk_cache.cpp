#include "disk_cache.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include "../../vendor/nlohmann/json.hpp"
#include "../search/json_field_scan.h"

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

// Resolve the FLAT cache base (no namespace applied), used for one-time
// migration of a pre-namespacing cache into the namespaced dir.
static fs::path flat_cache_base() {
    if (const char* p = std::getenv("HANABI_CACHE_DIR"); p && *p) return p;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return fs::path(xdg) / "hanabi" / "cache";
    if (const char* home = std::getenv("HOME"); home && *home)
        return fs::path(home) / ".config" / "hanabi" / "cache";
    return {};
}

// Defined below with the rest of the cache-size estimate; declared here
// because switching namespace switches DIRECTORY, and an estimate taken in one
// cache says nothing about another.
namespace { void invalidate_cache_size_estimate(); }

void set_namespace(const std::string& key) {
    invalidate_content_index();  // a different namespace is a different corpus
    g_namespace = key.empty() ? std::string() : ns_token(key);
    invalidate_cache_size_estimate();
    if (g_namespace.empty()) return;

    // ONE-TIME MIGRATION: before namespacing, the cache lived flat directly in
    // …/cache/ (sessions.json + tx_*.json). Namespacing moves it to
    // …/cache/<hash>/. Without migrating, the upgrade ORPHANS a working cache —
    // the namespaced dir is empty, so a slow-/failed-network launch has nothing
    // to fall back to and blanks (exactly the regression this fixes). So on the
    // first namespaced run, if our dir has no session list yet but a flat one
    // exists, MOVE the flat cache files into the namespaced dir. Best-effort;
    // any failure just falls back to a cold fetch (never fatal).
    std::error_code ec;
    const fs::path flat = flat_cache_base();
    if (flat.empty()) return;
    const fs::path ns = flat / g_namespace;
    if (fs::exists(ns / "sessions.json", ec)) return;  // already migrated
    if (!fs::exists(flat / "sessions.json", ec)) return;  // nothing to migrate
    fs::create_directories(ns, ec);
    if (ec) return;
    // Move the flat sessions.json + every flat tx_*.json into the namespaced
    // dir. (Only the DIRECT children of flat/ — never recurse into other ns
    // subdirs.) rename() is atomic within a filesystem; ignore per-file errors.
    for (fs::directory_iterator it(flat, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string name = it->path().filename().string();
        const bool isCache = (name == "sessions.json") ||
                             (name.rfind("tx_", 0) == 0 &&
                              name.size() > 4 &&
                              name.substr(name.size() - 5) == ".json");
        if (!isCache) continue;
        std::error_code mv;
        fs::rename(it->path(), ns / name, mv);
    }
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
    // `kind` rides alongside `role`, not instead of it: a cached row that
    // loses its kind comes back as somebody speaking, which is exactly the
    // failure EventKind exists to end. Absent on read = Text, so a file
    // written before this field still loads as what it was.
    return json{{"id", m.id},
                {"role", static_cast<int>(m.role)},
                {"kind", static_cast<int>(m.kind)},
                {"text", m.text},
                {"created_at", m.created_at},
                {"subtitle", m.subtitle},
                // The one dropped field that something READS after a restore:
                // find's `state:` operator resolves through tool_state_of(),
                // which is a lookup on this string. Without it a thread
                // restored from cache answered "no matches" to every state:
                // query, and answered it as a VALID query, so no hint said
                // why. docs/SEARCH.md S4.
                {"tool_status", m.tool_status}};
}

Message message_from_json(const json& j) {
    Message m;
    m.id = j.value("id", "");
    m.role = static_cast<Role>(j.value("role", static_cast<int>(Role::Assistant)));
    m.text = j.value("text", "");
    m.created_at = j.value("created_at", (int64_t)0);
    m.subtitle = j.value("subtitle", "");
    // Absent in files written before this was saved. Empty is what those
    // files behaved as, and it is also what "the backend said nothing"
    // means to tool_state_of, so an old file degrades to the old answer
    // rather than to a wrong one.
    m.tool_status = j.value("tool_status", "");
    m.kind = static_cast<EventKind>(
        j.value("kind", static_cast<int>(EventKind::Text)));
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

// ---- cache-size estimate, so the common case does not touch the disk ------
//
// trim_to_cap() used to call total_bytes() FIRST -- before it knew whether
// anything needed evicting -- and total_bytes() walks the whole cache
// directory with a file_size() stat per entry. The default cap is 1 GiB, so on
// essentially every save the answer is "under cap, nothing to do" and the walk
// was pure cost, on the FRAME THREAD (LoaderSystem::save_and_trim runs in the
// frame loop, not on the fetch's worker).
//
// MEASURED (tools/bench_data_layer.cpp, CLOCK_THREAD_CPUTIME_ID):
//     200 cache files    0.674 ms per save
//    2000 cache files    5.904 ms per save   (~3.2 us/file)
//
// The estimate below is deliberately an OVER-estimate, which is what makes it
// safe: it adds every byte written and never subtracts one. Overwriting a
// large transcript with a small one, or deleting a file outside a trim, both
// make the real total SMALLER than the estimate. So "estimate <= cap" proves
// "real total <= cap", and a trim that is genuinely needed can never be
// skipped. The reverse -- scanning when we did not have to -- is just the old
// behaviour, and it is what happens on the first call and after any eviction.
//
// It also re-scans unconditionally every kForceScanEvery calls, so drift from
// anything that writes to the cache dir behind our back self-corrects.
std::mutex g_size_mu;
std::uint64_t g_scanned_total = 0;     // total_bytes() at the last real scan
std::uint64_t g_written_since = 0;     // bytes written since that scan
bool g_have_scan = false;
unsigned g_since_force = 0;
constexpr unsigned kForceScanEvery = 64;

void note_cache_bytes_written(std::size_t n) {
    std::lock_guard<std::mutex> lk(g_size_mu);
    g_written_since += static_cast<std::uint64_t>(n);
}

void invalidate_cache_size_estimate() {
    std::lock_guard<std::mutex> lk(g_size_mu);
    g_have_scan = false;
    g_written_since = 0;
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
    note_cache_bytes_written(content.size());
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
    // Without has_more_older a cached windowed transcript loses "load older".
    json doc{{"version", 1},
             {"summary", to_json(session.summary)},
             {"messages", std::move(msgs)},
             {"sub_agents", std::move(subs)},
             {"has_more_older", session.has_more_older}};
    write_file(transcript_file(session.summary.id), doc.dump());
    invalidate_content_index();  // the corpus changed under the search memo
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
        // Absent in files written before this was saved; false matches how
        // they behaved, and the next fetch replaces them anyway.
        if (doc.contains("has_more_older") && doc["has_more_older"].is_boolean())
            s.has_more_older = doc["has_more_older"].get<bool>();
        return s;
    } catch (...) {
        return std::nullopt;
    }
}

// ---- owned durable export (local-first idea #4) --------------------------
std::string export_dir() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return "";
    return (fs::path(home) / "hanabi" / "threads").string();
}

namespace {
const char* role_word(Role r) {
    switch (r) {
        case Role::User: return "You";
        case Role::Assistant: return "hanabi";
        case Role::System: return "System";
        case Role::Tool: return "Tool";
    }
    return "hanabi";
}
// A filesystem-safe slug from a title (letters/digits/dash/underscore only).
std::string slugify(const std::string& s, const std::string& fallback) {
    std::string out;
    for (char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9'))
            out += c;
        else if (c == ' ' || c == '-' || c == '_')
            out += '-';
        // else drop
    }
    while (out.size() > 1 && out.back() == '-') out.pop_back();
    if (out.empty()) out = fallback;
    if (out.size() > 80) out = out.substr(0, 80);
    return out;
}
}  // namespace

int export_all_markdown() { return export_all_markdown(export_dir()); }

int export_all_markdown(const std::string& dst) {
    const std::string src = cache_dir();
    if (src.empty() || dst.empty()) return 0;
    std::error_code ec;
    fs::create_directories(dst, ec);
    if (ec) return 0;
    int written = 0;
    for (fs::directory_iterator it(src, ec), end; !ec && it != end;
         it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind("tx_", 0) != 0 || name.size() <= 5 ||
            name.substr(name.size() - 5) != ".json")
            continue;
        // Load the transcript JSON directly (id is embedded in the summary).
        std::ifstream in(it->path());
        if (!in.good()) continue;
        Session s;
        try {
            json doc;
            in >> doc;
            if (!doc.contains("summary")) continue;
            s.summary = summary_from_json(doc["summary"]);
            if (doc.contains("messages") && doc["messages"].is_array())
                for (const auto& e : doc["messages"])
                    s.messages.push_back(message_from_json(e));
        } catch (...) {
            continue;
        }
        // Render Markdown.
        std::string md = "# " + (s.summary.title.empty() ? s.summary.id
                                                          : s.summary.title) +
                         "\n\n";
        for (const auto& m : s.messages) {
            md += "**" + std::string(role_word(m.role)) + "**\n\n";
            md += m.text;
            md += "\n\n";
            if (!m.tool_result.empty())
                md += "```\n" + m.tool_result + "\n```\n\n";
        }
        const std::string fname =
            slugify(s.summary.title, s.summary.id) + ".md";
        std::ofstream out(fs::path(dst) / fname, std::ios::trunc);
        if (out.good()) {
            out << md;
            ++written;
        }
    }
    return written;
}

// ---- local full-text search (local-first idea #3) -----------------------

// The answer, remembered, because the caller asks this EVERY FRAME.
//
// The sidebar's filter calls content_matches for every session whose title did
// not already match, on every frame a query is live. The body below opens a
// file, reads it whole, lowercases every byte of it and scans it. At a
// 2020-session catalog that was two thousand file opens per frame -- and it
// stays two thousand when nothing is cached, because a failed open is still a
// syscall. Nobody noticed because the mock catalog had twenty rows in it and a
// person types for a second and then stops, and the frames after they stop are
// most of the frames.
//
// Two things make the memo cheap AND correct:
//
//   * The key is the whole question -- (id, query) -- so a remembered answer
//     cannot be a wrong answer for a different one.
//   * The map is dropped when the CORPUS changes, which is exactly when a
//     transcript is written, wiped or trimmed. Those three bump a generation
//     counter and the memo checks it. Nothing else can change what a
//     transcript file says.
//
// And typing gets cheaper as you type. If the new query CONTAINS the old one
// as a substring, then any file that did not contain the old cannot contain
// the new -- so every `false` from the previous query is still `false` and
// only the previous `true`s need re-reading. Narrowing a search re-reads the
// hits, not the catalog.
namespace {
std::uint64_t g_corpus_gen = 0;   // bumped when any transcript file changes
std::string g_match_query;        // the query g_match_cache holds answers for
std::uint64_t g_match_gen = 0;    // the generation those answers were taken at
std::unordered_map<std::string, bool> g_match_cache;

// Does `id`'s cached transcript contain `lowerQuery`? The uncached body.
bool scan_transcript(const std::string& id, const std::string& lowerQuery) {
    const std::string path = transcript_file(id);
    if (path.empty()) return false;
    std::ifstream in(path);
    if (!in.good()) return false;
    // Search the message TEXT, not the document.
    //
    // This used to lowercase the whole file and call find() on it, with the
    // comment "message bodies and tool output are all in there". Half of that
    // was wrong and the other half was the bug. Tool output is not in there —
    // to_json(const Message&) writes {id, role, kind, text, created_at,
    // subtitle, tool_status} and nothing else. And the field names ARE in
    // there, so `state`, `tag`, `preview`, `folder`, `subtitle`, `version`,
    // `messages`, `has_more_older` and every session id matched every thread
    // that had ever been cached. Title matching runs first, so the false hit
    // only fired once the title had missed — which is precisely when it reads
    // as a legitimate deep hit (docs/SEARCH.md S3).
    //
    // json_field_contains looks inside the values of `text` and nowhere else,
    // in one pass over the same bytes this was already reading, so the memo
    // above absorbs the same cost it always did.
    const std::string blob((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    return hanabi::search::json_field_contains(blob, "text", lowerQuery);
}
}  // namespace

void invalidate_content_index() { ++g_corpus_gen; }

bool content_matches(const std::string& id, const std::string& lowerQuery) {
    if (lowerQuery.empty()) return false;

    if (lowerQuery != g_match_query || g_corpus_gen != g_match_gen) {
        const bool narrowing = g_corpus_gen == g_match_gen &&
                               !g_match_query.empty() &&
                               lowerQuery.find(g_match_query) !=
                                   std::string::npos;
        if (narrowing) {
            // Keep the misses (still misses), re-ask the hits.
            for (auto it = g_match_cache.begin();
                 it != g_match_cache.end();) {
                if (it->second) it = g_match_cache.erase(it);
                else ++it;
            }
        } else {
            g_match_cache.clear();
        }
        g_match_query = lowerQuery;
        g_match_gen = g_corpus_gen;
    }

    if (auto it = g_match_cache.find(id); it != g_match_cache.end())
        return it->second;
    const bool hit = scan_transcript(id, lowerQuery);
    g_match_cache.emplace(id, hit);
    return hit;
}

namespace {
// True for a file this cache owns (sessions.json or a tx_*.json transcript).
// Used to bound total_bytes()/wipe_all() to OUR files, never anything else a
// user might have dropped in the dir.
bool is_cache_file(const std::string& name) {
    if (name == "sessions.json") return true;
    return name.rfind("tx_", 0) == 0 && name.size() > 5 &&
           name.substr(name.size() - 5) == ".json";
}
}  // namespace

std::uint64_t total_bytes() {
    const std::string dir = cache_dir();
    if (dir.empty()) return 0;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return 0;
    std::uint64_t total = 0;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        if (!is_cache_file(it->path().filename().string())) continue;
        std::error_code sz;
        auto n = fs::file_size(it->path(), sz);
        if (!sz) total += static_cast<std::uint64_t>(n);
    }
    return total;
}

std::size_t wipe_all() {
    invalidate_content_index();  // the corpus changed under the search memo
    const std::string dir = cache_dir();
    if (dir.empty()) return 0;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return 0;
    // Collect first, then delete — deleting during iteration is undefined.
    std::vector<fs::path> victims;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        if (is_cache_file(it->path().filename().string()))
            victims.push_back(it->path());
    }
    std::size_t removed = 0;
    for (const auto& p : victims) {
        std::error_code rm;
        if (fs::remove(p, rm) && !rm) ++removed;
    }
    // The estimate counts bytes written and never bytes removed, so a wipe
    // leaves it wildly high. Drop it; the next trim_to_cap does a real scan.
    invalidate_cache_size_estimate();
    return removed;
}

// ---- crash-safe draft / queue persistence --------------------------------
// A single drafts.json in the active namespace holds every key's in-progress
// draft + unsent queue:
//   { "version": 1, "drafts": { "<key>": { "text": "...",
//                                            "queue": ["p1", "p2"] }, ... } }
// It is DELIBERATELY not an is_cache_file() (above): a settings "clear cache"
// reclaims transcript disk but must NOT throw away work the user is mid-typing,
// so drafts.json survives wipe_all()/trim_to_cap(). It's cleared per-key via
// clear_draft() when a draft is actually sent.
namespace {
std::string drafts_file() {
    const std::string dir = cache_dir();
    return dir.empty() ? "" : (fs::path(dir) / "drafts.json").string();
}

json load_drafts_doc() {
    const std::string path = drafts_file();
    if (path.empty()) return json::object();
    std::ifstream in(path);
    if (!in.good()) return json::object();
    try {
        json doc;
        in >> doc;
        if (doc.is_object() && doc.contains("drafts") &&
            doc["drafts"].is_object())
            return doc;
    } catch (...) {
    }
    return json::object();
}

// Read-modify-write the per-key entry, then rewrite drafts.json atomically.
// mutate() receives the key's entry object (created if absent) to edit; after
// it runs, an entry with neither text nor a non-empty queue is dropped so the
// file stays tidy (empty drafts leave no residue).
template <typename Fn>
void update_draft_entry(const std::string& key, Fn&& mutate) {
    if (!ensure_dir(cache_dir())) return;
    json doc = load_drafts_doc();
    if (!doc.contains("drafts") || !doc["drafts"].is_object())
        doc["drafts"] = json::object();
    json& entry = doc["drafts"][key];
    if (!entry.is_object()) entry = json::object();
    mutate(entry);
    // Prune an empty entry (no text and no queued prompts).
    const bool hasText =
        entry.contains("text") && entry["text"].is_string() &&
        !entry["text"].get<std::string>().empty();
    const bool hasQueue = entry.contains("queue") &&
                          entry["queue"].is_array() &&
                          !entry["queue"].empty();
    if (!hasText && !hasQueue) doc["drafts"].erase(key);
    doc["version"] = 1;
    write_file(drafts_file(), doc.dump());
}
}  // namespace

void save_draft(const std::string& key, const std::string& text) {
    update_draft_entry(key, [&](json& e) { e["text"] = text; });
}

std::string load_draft(const std::string& key) {
    json doc = load_drafts_doc();
    if (!doc.contains("drafts") || !doc["drafts"].is_object())
        return "";
    const auto& drafts = doc["drafts"];
    if (!drafts.contains(key) || !drafts[key].is_object()) return "";
    return drafts[key].value("text", "");
}

void save_queue(const std::string& key,
                const std::vector<std::string>& prompts) {
    update_draft_entry(key, [&](json& e) {
        json arr = json::array();
        for (const auto& p : prompts) arr.push_back(p);
        e["queue"] = std::move(arr);
    });
}

std::vector<std::string> load_queue(const std::string& key) {
    std::vector<std::string> out;
    json doc = load_drafts_doc();
    if (!doc.contains("drafts") || !doc["drafts"].is_object()) return out;
    const auto& drafts = doc["drafts"];
    if (!drafts.contains(key) || !drafts[key].is_object()) return out;
    const auto& e = drafts[key];
    if (e.contains("queue") && e["queue"].is_array())
        for (const auto& p : e["queue"])
            if (p.is_string()) out.push_back(p.get<std::string>());
    return out;
}

void clear_draft(const std::string& key) {
    const std::string path = drafts_file();
    if (path.empty()) return;
    json doc = load_drafts_doc();
    if (!doc.contains("drafts") || !doc["drafts"].is_object()) return;
    if (!doc["drafts"].contains(key)) return;
    doc["drafts"].erase(key);
    doc["version"] = 1;
    write_file(path, doc.dump());
}

// ---- local-first outbox --------------------------------------------------
// Reuses the per-key queue store under an "ob:" key namespace so an outbox
// entry never collides with a session's composer draft-queue. Crash-safe: the
// prompt is persisted before the network send and removed on server confirm.
namespace {
std::string outbox_key(const std::string& id) { return "ob:" + id; }
}  // namespace

void outbox_add(const std::string& id, const std::string& prompt) {
    if (prompt.empty()) return;
    auto v = load_queue(outbox_key(id));
    v.push_back(prompt);
    save_queue(outbox_key(id), v);
}

void outbox_remove(const std::string& id, const std::string& prompt) {
    auto v = load_queue(outbox_key(id));
    for (auto it = v.begin(); it != v.end(); ++it) {
        if (*it == prompt) {
            v.erase(it);
            break;  // remove only the first match
        }
    }
    save_queue(outbox_key(id), v);
}

std::vector<std::string> outbox_list(const std::string& id) {
    return load_queue(outbox_key(id));
}

// ---- cache cap / eviction (feature #C) -----------------------------------
void touch_transcript(const std::string& id) {
    const std::string path = transcript_file(id);
    if (path.empty()) return;
    std::error_code ec;
    if (!fs::exists(path, ec)) return;
    // Bump mtime to "now" without rewriting the file, so LRU eviction can order
    // by last-opened. (last_write_time takes a file_clock time_point.)
    fs::last_write_time(path, fs::file_time_type::clock::now(), ec);
}

namespace {
// One transcript file on disk, with the metadata eviction needs.
struct TxEntry {
    fs::path path;
    std::uint64_t size = 0;
    fs::file_time_type mtime{};  // last-opened proxy (see touch_transcript)
    bool archived = false;       // evict archived first
    std::size_t msg_count = 0;   // to decide trim-vs-delete
};

// Peek a transcript file's summary.state + message count WITHOUT fully
// materializing the Session (we only need archived-ness and how many messages
// there are). Best-effort: a parse failure returns {false, 0}.
std::pair<bool, std::size_t> peek_archived_and_count(const fs::path& p) {
    std::ifstream in(p);
    if (!in.good()) return {false, 0};
    try {
        json doc;
        in >> doc;
        bool archived = false;
        if (doc.contains("summary") && doc["summary"].is_object()) {
            const int st = doc["summary"].value(
                "state", static_cast<int>(ThreadState::Unknown));
            archived = (st == static_cast<int>(ThreadState::Archived));
        }
        std::size_t n = 0;
        if (doc.contains("messages") && doc["messages"].is_array())
            n = doc["messages"].size();
        return {archived, n};
    } catch (...) {
        return {false, 0};
    }
}

// Rewrite a transcript file keeping only its NEWEST keep_tail messages (tail),
// preserving summary + sub_agents. Returns true on success. Messages are stored
// oldest→newest (append order), so the tail is the last keep_tail entries.
bool trim_transcript_file(const fs::path& p, std::size_t keep_tail) {
    std::ifstream in(p);
    if (!in.good()) return false;
    json doc;
    try {
        in >> doc;
    } catch (...) {
        return false;
    }
    if (!doc.contains("messages") || !doc["messages"].is_array()) return false;
    json& msgs = doc["messages"];
    const std::size_t n = msgs.size();
    if (n <= keep_tail) return false;  // nothing to trim (caller deletes)
    json tail = json::array();
    for (std::size_t i = n - keep_tail; i < n; ++i) tail.push_back(msgs[i]);
    doc["messages"] = std::move(tail);
    in.close();
    return write_file(p.string(), doc.dump());
}
}  // namespace

std::uint64_t trim_to_cap(std::uint64_t cap_bytes, std::size_t keep_tail) {
    if (cap_bytes == 0) return 0;  // unlimited — never evict
    invalidate_content_index();  // the corpus may change under the search memo
    const std::string dir = cache_dir();
    if (dir.empty()) return 0;

    // Can we prove we are under cap without touching the disk? The estimate
    // over-counts (see note_cache_bytes_written), so "estimate <= cap" implies
    // "real <= cap" and a needed trim can never be skipped.
    {
        std::lock_guard<std::mutex> lk(g_size_mu);
        if (g_have_scan && g_since_force < kForceScanEvery &&
            g_scanned_total + g_written_since <= cap_bytes) {
            ++g_since_force;
            return 0;
        }
    }

    const std::uint64_t before = total_bytes();
    {
        std::lock_guard<std::mutex> lk(g_size_mu);
        g_scanned_total = before;
        g_written_since = 0;
        g_have_scan = true;
        g_since_force = 0;
    }
    if (before <= cap_bytes) return 0;  // already under cap

    // Gather all transcript files (never sessions.json) with eviction metadata.
    std::error_code ec;
    std::vector<TxEntry> txs;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string name = it->path().filename().string();
        if (name == "sessions.json") continue;      // spine — never evict
        if (!is_cache_file(name)) continue;          // only our tx_*.json
        TxEntry e;
        e.path = it->path();
        std::error_code sz;
        auto n = fs::file_size(it->path(), sz);
        e.size = sz ? 0 : static_cast<std::uint64_t>(n);
        std::error_code mt;
        e.mtime = fs::last_write_time(it->path(), mt);
        auto meta = peek_archived_and_count(it->path());
        e.archived = meta.first;
        e.msg_count = meta.second;
        txs.push_back(std::move(e));
    }

    // Eviction order: archived-first, then least-recently-opened (oldest mtime)
    // first within each group. That empties the threads least likely to be
    // reopened before touching anything the user is actively working in.
    std::sort(txs.begin(), txs.end(), [](const TxEntry& a, const TxEntry& b) {
        if (a.archived != b.archived) return a.archived;  // archived first
        return a.mtime < b.mtime;                          // oldest first
    });

    std::uint64_t total = before;
    for (const auto& e : txs) {
        if (total <= cap_bytes) break;
        std::uint64_t reclaimed = 0;
        if (e.msg_count > keep_tail && trim_transcript_file(e.path, keep_tail)) {
            // Trimmed in place — recompute the shrunken file's size.
            std::error_code sz;
            auto after = fs::file_size(e.path, sz);
            const std::uint64_t newSize =
                sz ? 0 : static_cast<std::uint64_t>(after);
            reclaimed = (e.size > newSize) ? (e.size - newSize) : 0;
        } else {
            // At/under keep_tail messages (or trim failed): the whole file is
            // the eviction unit — remove it.
            std::error_code rm;
            if (fs::remove(e.path, rm) && !rm) reclaimed = e.size;
        }
        total = (total > reclaimed) ? (total - reclaimed) : 0;
    }

    const std::uint64_t after = total_bytes();
    {
        std::lock_guard<std::mutex> lk(g_size_mu);
        g_scanned_total = after;
        g_written_since = 0;
        g_have_scan = true;
        g_since_force = 0;
    }
    return (before > after) ? (before - after) : 0;
}

}  // namespace api::disk_cache
