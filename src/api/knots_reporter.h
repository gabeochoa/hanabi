#pragma once

// Filing a Knots issue from inside the app (the reference's KnotsReporter:
// `/knot` in the composer and the bug report), as the person at the keyboard.
//
// WHY THE CLI. `meta knots.issue create` resolves the caller's identity
// server-side, so the issue is created BY the reader -- no token to mint, no
// service identity to borrow. The app's own agentcloud credential is an
// endpoint credential and cannot stand in for a person.
//
// THE SHAPE is the agentcloud web capture's (`/api/knots/capture`): line 1 is
// the title (capped at 500, overflow carried into the body), lines 2+ the body
// (capped at 10,000), type `task`, description in the order detail, context,
// provenance; a thread-linked knot carries `sessionId` in its metadata and a
// `Session: <id>` line in its description, the two spellings the session DAG
// reads.
//
// SAFETY. A real `meta` run files a real issue under a real person's name. So
// the process runner is a seam (`knots::runner()`): scripted UI, unit tests and
// the mock backend install a fake, and the real runner refuses to spawn at all
// in an E2E build (see knots_runner.h). Nothing here runs a process.
//
// Pure: argv, text and JSON only.

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../../vendor/nlohmann/json.hpp"

namespace api::knots {

using json = nlohmann::json;

// Which board a report lands on. The app's own board is the owner's
// (Hanabi is his private client), labelled `hanabi`; the backend board is the
// agentcloud team's, the one the web capture files to. Either can be replaced
// with HANABI_KNOTS_NAMESPACE (app) / HANABI_KNOTS_BACKEND_NAMESPACE.
enum class Board { App, Backend };

inline constexpr const char* kDefaultAppNamespace = "gabeochoa/manager";
inline constexpr const char* kDefaultBackendNamespace = "agentcloud/client";
inline constexpr std::size_t kMaxTitle = 500;
inline constexpr std::size_t kMaxBody = 10000;
inline constexpr const char* kSourceTag = "hanabi";
inline constexpr const char* kUntriaged = "triage:untriaged";

inline std::string trim(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\n' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n' || s[e - 1] == '\r'))
        --e;
    return std::string(s.substr(b, e - b));
}

struct Capture {
    std::string title;
    std::string detail;  // "" = none
};

// Line 1 is the title; an over-long first line carries its tail into the body
// rather than losing it. nullopt for text that is empty once trimmed.
inline std::optional<Capture> capture(std::string_view raw) {
    const std::string t = trim(raw);
    if (t.empty()) return std::nullopt;
    const std::size_t nl = t.find('\n');
    std::string first = trim(nl == std::string::npos ? std::string_view(t)
                                                     : std::string_view(t).substr(0, nl));
    const std::string rest = nl == std::string::npos ? std::string() : trim(t.substr(nl + 1));
    Capture c;
    // Cut on a UTF-8 boundary.
    std::size_t cut = std::min(first.size(), kMaxTitle);
    while (cut > 0 && cut < first.size() &&
           (static_cast<unsigned char>(first[cut]) & 0xC0) == 0x80)
        --cut;
    c.title = first.substr(0, cut);
    std::string overflow = trim(first.substr(cut));
    std::string detail = overflow;
    if (!rest.empty()) detail += (detail.empty() ? "" : "\n") + rest;
    if (detail.size() > kMaxBody) detail.resize(kMaxBody);
    c.detail = detail;
    return c;
}

struct Context {
    std::string version;   // app version
    std::string os;        // "macOS 15.6"
    std::string surface;   // "composer", "menu", ...
    std::string reporter;  // the unixname, as the app knows it ("" = unknown)
    std::string sessionId; // "" = not linked to a thread
};

inline std::vector<std::string> context_lines(const Context& c) {
    std::vector<std::string> out;
    out.push_back("Hanabi " + c.version);
    if (!c.os.empty()) out.push_back(c.os);
    out.push_back("Filed from: " + c.surface);
    if (!c.sessionId.empty()) out.push_back("Session: " + c.sessionId);
    return out;
}

// A window capture travels as a LINK: Knots renders no images in a
// description (it strips the `!` off `![alt](url)` on purpose), and it does
// turn a markdown link into a link. Or, when the upload failed, one line
// saying so in the place the image would have been -- the report still files.
inline std::string attachment_line(const std::string& name, const std::string& url,
                                   const std::string& failure) {
    if (!failure.empty()) return "Screenshot: not attached (" + failure + ").";
    return "Screenshot: [" + name + "](" + url + ")";
}

inline std::vector<std::string> upload_args(const std::string& path, const std::string& name) {
    return {"phabricator.file", "upload", "--path", "file://" + path, "--name", name,
            "--output", "json"};
}

// {"f_handle":"F1994521010", ..., "url":"https://www.internalfb.com/F1994521010"}
inline std::optional<std::string> parse_upload_url(const std::string& raw) {
    const json j = json::parse(raw, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("f_handle") ||
        !j["f_handle"].is_string() || j["f_handle"].get<std::string>().empty())
        return std::nullopt;
    if (j.contains("url") && j["url"].is_string() && !j["url"].get<std::string>().empty())
        return j["url"].get<std::string>();
    return "https://www.internalfb.com/" + j["f_handle"].get<std::string>();
}

inline std::string description(const std::string& detail, const Context& c,
                               const std::string& attachment = {}) {
    std::string out;
    if (!detail.empty()) out += detail + "\n\n";
    if (!attachment.empty()) out += attachment + "\n\n";
    const auto lines = context_lines(c);
    for (std::size_t i = 0; i < lines.size(); ++i) out += (i ? "\n" : "") + lines[i];
    out += "\n\nFiled from Hanabi for macOS" +
           (c.reporter.empty() ? std::string(".") : " by " + c.reporter + ".");
    return out;
}

// Fixed key order, so two reports from one build produce one string.
inline std::string metadata(const Context& c, Board board) {
    std::string out = "{";
    const auto add = [&](const char* k, const std::string& v) {
        if (out.size() > 1) out += ",";
        out += json(k).dump() + ":" + json(v).dump();
    };
    if (!c.reporter.empty()) add("capturedBy", c.reporter);
    add("source", std::string(kSourceTag) + "-in-app");
    add("version", c.version);
    if (!c.os.empty()) add("os", c.os);
    add("surface", c.surface);
    add("board", board == Board::App ? "app" : "backend");
    if (!c.sessionId.empty()) add("sessionId", c.sessionId);
    return out + "}";
}

inline std::vector<std::string> labels(const Context& c) {
    return {std::string("source:") + kSourceTag, "version:" + c.version, "surface:" + c.surface,
            kUntriaged};
}

inline std::vector<std::string> create_args(const std::string& ns, const std::string& title,
                                            const std::string& desc, const std::string& meta,
                                            const std::vector<std::string>& labs) {
    std::vector<std::string> args{"knots.issue", "create", "--namespace", ns,  "--title", title,
                                  "--description", desc, "--type", "task", "--metadata", meta,
                                  "--output", "json"};
    if (!labs.empty()) {
        std::string joined;
        for (std::size_t i = 0; i < labs.size(); ++i) joined += (i ? "," : "") + labs[i];
        args.push_back("--label");
        args.push_back(joined);
    }
    return args;
}

inline std::vector<std::string> join_args(const std::string& ns) {
    return {"knots.namespace", "join", "--name=" + ns};
}

struct Filed {
    std::string id;
    std::string url;
    std::string rejectedLabels;
};

// `meta knots.issue create --output json` answers with a FLAT object:
// {"id":"kt-f541", ..., "labels_failed":"", "url":"https://knots..."}.
inline std::optional<Filed> parse_created(const std::string& raw, const std::string& ns) {
    const json j = json::parse(raw, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("id") || !j["id"].is_string())
        return std::nullopt;
    Filed f;
    f.id = j["id"].get<std::string>();
    if (f.id.empty()) return std::nullopt;
    if (j.contains("url") && j["url"].is_string()) f.url = j["url"].get<std::string>();
    if (f.url.empty()) f.url = "https://knots.internalmeta.com/" + ns + "/issues/" + f.id;
    if (j.contains("labels_failed") && j["labels_failed"].is_string())
        f.rejectedLabels = j["labels_failed"].get<std::string>();
    return f;
}

// Knots fails closed: "not a member" and "no such namespace" are one refusal.
inline bool is_permission_refusal(std::string_view text) {
    std::string low(text);
    for (char& ch : low)
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    for (const char* p : {"permission denied", "permission_denied", "not a member",
                          "not authorized", "unauthorized", "forbidden", "access denied"})
        if (low.find(p) != std::string::npos) return true;
    return false;
}

inline std::string not_a_member(const std::string& ns) {
    return "Not a member of the " + ns + " Knots namespace";
}

}  // namespace api::knots
