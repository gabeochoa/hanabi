#pragma once

// A diff, task or knot named in a message, drawn as a card under it (the
// reference's LinkPreview / LinkPreviewCardView, kt-ghu5 + kt-lgsh;
// puffin_gaps.md D53).
//
// WHERE A CARD COMES FROM. Diffs and tasks: the web app's /api/graphql root
// field `xfb_gchat_powertools_json_config(command: "getLinkPreview")` -- the
// same server read GChat PowerTools makes, so the viewer's own entitlement
// decides what can be shown (a diff they cannot see comes back "not found",
// and draws no card). Knots: `meta knots.issue locate` (which board the id is
// on) then `meta knots.issue get`, the CLI the knot filer already runs; the
// knots service has no web root field. The URL asked about is BUILT here from
// digits matched here, never taken from the message: a link an agent wrote
// could name any host.
//
// WHAT IT DRAWS: the PowerTools card, field for field -- an id row with the
// id and its badges (a task's or knot's priority before its status), the
// title, then a footer (namespace, type, owner, "Created 3d ago").
//
// Pure: text in, text and JSON out. No fetch runs here.

#include <cctype>
#include <cstdint>
#include <ctime>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "../../vendor/nlohmann/json.hpp"

namespace api::link_preview {

using json = nlohmann::json;

enum class Kind { Diff, Task, Knot };

inline const char* kind_name(Kind k) {
    switch (k) {
        case Kind::Diff: return "diff";
        case Kind::Task: return "task";
        case Kind::Knot: return "knot";
    }
    return "diff";
}

struct Ref {
    Kind kind = Kind::Diff;
    std::string id;  // D117423523, T275363905, kt-lgsh
    bool operator==(const Ref&) const = default;
    bool operator<(const Ref& o) const { return id < o.id; }

    // The URL the server is asked about (never opened as-is for a knot: its
    // card's url comes from `knots.issue get`).
    [[nodiscard]] std::string url() const {
        switch (kind) {
            case Kind::Diff: return "https://www.internalfb.com/diff/" + id;
            case Kind::Task: return "https://www.internalfb.com/intern/tasks/?t=" + id.substr(1);
            case Kind::Knot: return "https://knots.internalmeta.com/";
        }
        return {};
    }
};

// ---- Finding references ----------------------------------------------------

// One byte pass: a reference needs "D<digit>", "T<digit>" or "kt-". Almost no
// message has one, and this is what keeps the scan off an ordinary turn.
inline bool may_hold_reference(std::string_view text) {
    unsigned char older = 0, prev = 0;
    for (unsigned char c : text) {
        if ((prev == 'D' || prev == 'T') && c >= '0' && c <= '9') return true;
        if (older == 'k' && prev == 't' && c == '-') return true;
        older = prev;
        prev = c;
    }
    return false;
}

namespace detail {
inline bool word(unsigned char c) { return std::isalnum(c) != 0 || c == '_'; }
}  // namespace detail

// Every distinct reference in `text`, in the order first written.
//   D + 4..10 digits, T + 4..15 digits: a whole word, not after `[` (already
//   markdown) or `/` (inside a path or URL).
//   kt- + 4 lower-case letters or digits: not after `/`, `-`, `[` or a word
//   character, and not followed by a word character or `-`.
inline std::vector<Ref> refs_in(std::string_view text) {
    std::vector<Ref> out;
    if (!may_hold_reference(text)) return out;
    std::set<std::string> seen;
    const auto add = [&](Kind k, std::string id) {
        if (seen.insert(id).second) out.push_back({k, std::move(id)});
    };
    const std::size_t n = text.size();
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const unsigned char before = i > 0 ? static_cast<unsigned char>(text[i - 1]) : 0;
        if ((c == 'D' || c == 'T') && !detail::word(before) && before != '[' && before != '/') {
            std::size_t j = i + 1;
            while (j < n && std::isdigit(static_cast<unsigned char>(text[j])) != 0) ++j;
            const std::size_t digits = j - i - 1;
            const bool endsWord = j == n || !detail::word(static_cast<unsigned char>(text[j]));
            const std::size_t maxDigits = c == 'D' ? 10 : 15;
            if (digits >= 4 && digits <= maxDigits && endsWord)
                add(c == 'D' ? Kind::Diff : Kind::Task, std::string(text.substr(i, j - i)));
            i = j > i ? j - 1 : i;
            continue;
        }
        if (c == 'k' && i + 7 <= n && text.substr(i, 3) == "kt-" && !detail::word(before) &&
            before != '/' && before != '-' && before != '[') {
            bool ok = true;
            for (std::size_t k = i + 3; k < i + 7; ++k) {
                const unsigned char d = static_cast<unsigned char>(text[k]);
                if (!((d >= 'a' && d <= 'z') || (d >= '0' && d <= '9'))) ok = false;
            }
            if (ok && i + 7 < n) {
                const unsigned char after = static_cast<unsigned char>(text[i + 7]);
                if (detail::word(after) || after == '-') ok = false;
            }
            if (ok) add(Kind::Knot, std::string(text.substr(i, 7)));
        }
    }
    return out;
}

// ---- A card ----------------------------------------------------------------

enum class Tone { Settled, Active, Wrong, Quiet };

struct Card {
    Kind kind = Kind::Diff;
    std::string id;
    std::string title;
    std::string url;
    std::string owner;
    std::int64_t created_unix = 0;
    std::string status;    // the server's own word
    std::string priority;  // tasks and knots
    std::string ns;        // knots: the board
    std::string issue_type;  // knots: bug / feature / task
    bool operator==(const Card&) const = default;
};

namespace detail {
inline std::string lower_spaced(std::string_view raw) {
    std::string v;
    v.reserve(raw.size());
    for (char ch : raw) {
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        v += (c == '_' || c == '-') ? ' ' : c;
    }
    return v;
}
inline bool has(const std::string& v, std::string_view s) { return v.find(s) != std::string::npos; }
// `in preparation` -> `In Preparation`, leaving the rest of each word alone.
inline std::string title_words(std::string_view raw) {
    std::string out;
    bool start = true;
    for (char ch : raw) {
        const char c = ch == '_' ? ' ' : ch;
        if (c == ' ') {
            if (!out.empty() && out.back() != ' ') out += ' ';
            start = true;
            continue;
        }
        out += start ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        start = false;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}
}  // namespace detail

// The words, ported from PowerTools' linkPreviewManager.js through the
// reference's LinkPreviewText, so the same diff wears the same badge.
inline std::string diff_status(std::string_view raw) {
    std::string v;
    for (char ch : raw) v += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return detail::title_words(v);
}
inline Tone diff_tone(std::string_view raw) {
    const std::string v = detail::lower_spaced(raw);
    using detail::has;
    if (has(v, "landed") || has(v, "accepted") || has(v, "closed")) return Tone::Settled;
    if (has(v, "needs review") || has(v, "in review") || has(v, "landing") || has(v, "ai approved"))
        return Tone::Active;
    if (has(v, "needs revision") || has(v, "changes") || has(v, "amended") || has(v, "rejected") ||
        has(v, "failed") || has(v, "waiting for author") || has(v, "abandoned"))
        return Tone::Wrong;
    return Tone::Quiet;
}
inline std::string task_status(std::string_view raw) {
    const std::string v = detail::lower_spaced(raw);
    using detail::has;
    if (has(v, "in progress")) return "In Progress";
    if (has(v, "no progress")) return "Open";
    if (has(v, "resolved")) return "Resolved";
    if (has(v, "blocked")) return "Blocked";
    if (has(v, "planned")) return "Planned";
    if (has(v, "backlog")) return "Backlog";
    return detail::title_words(raw);
}
inline Tone task_tone(std::string_view raw) {
    const std::string v = detail::lower_spaced(raw);
    using detail::has;
    if (has(v, "resolved") || has(v, "done") || has(v, "complete")) return Tone::Settled;
    if (has(v, "blocked")) return Tone::Wrong;
    if (has(v, "in progress") || has(v, "started") || has(v, "no progress") || has(v, "open"))
        return Tone::Active;
    return Tone::Quiet;
}
inline std::string task_priority(std::string_view raw) {
    const std::string v = detail::lower_spaced(raw);
    using detail::has;
    if (v == "none") return {};
    if (has(v, "unbreak") || v == "p0") return "UBN!";
    if (v == "p1" || has(v, "high") || has(v, "hi pri")) return "High";
    if (v == "p2" || has(v, "mid") || has(v, "medium")) return "Mid";
    if (v == "p3" || has(v, "low") || has(v, "lo pri")) return "Low";
    if (v == "p4" || has(v, "wish")) return "Wish";
    return std::string(raw);
}
inline Tone task_priority_tone(std::string_view raw) {
    const std::string v = detail::lower_spaced(raw);
    using detail::has;
    if (has(v, "unbreak") || v == "p0" || v == "p1" || has(v, "high") || has(v, "hi pri"))
        return Tone::Wrong;
    if (v == "p2" || has(v, "mid") || has(v, "medium")) return Tone::Active;
    return Tone::Quiet;
}
inline std::string knot_status(std::string_view raw) { return detail::title_words(raw); }
inline Tone knot_tone(std::string_view raw) {
    const std::string v = detail::lower_spaced(raw);
    if (v == "closed" || v == "done" || v == "resolved") return Tone::Settled;
    if (v == "blocked") return Tone::Wrong;
    if (v == "open" || detail::has(v, "in progress") || v == "review") return Tone::Active;
    return Tone::Quiet;
}
// Knots priorities are 0 (highest) to 4, written as a number.
inline std::string knot_priority(std::string_view raw) {
    std::string v = detail::lower_spaced(raw);
    while (!v.empty() && v.front() == ' ') v.erase(v.begin());
    while (!v.empty() && v.back() == ' ') v.pop_back();
    if (!v.empty() && v.front() == 'p') v.erase(v.begin());
    if (v.size() != 1 || v[0] < '0' || v[0] > '4') return {};
    return std::string("P") + v[0];
}
inline Tone knot_priority_tone(std::string_view raw) {
    const std::string p = knot_priority(raw);
    if (p == "P0" || p == "P1") return Tone::Wrong;
    if (p == "P2") return Tone::Active;
    return Tone::Quiet;
}

inline std::string status_label(const Card& c) {
    if (c.status.empty()) return {};
    switch (c.kind) {
        case Kind::Diff: return diff_status(c.status);
        case Kind::Task: return task_status(c.status);
        case Kind::Knot: return knot_status(c.status);
    }
    return {};
}
inline Tone status_tone(const Card& c) {
    switch (c.kind) {
        case Kind::Diff: return diff_tone(c.status);
        case Kind::Task: return task_tone(c.status);
        case Kind::Knot: return knot_tone(c.status);
    }
    return Tone::Quiet;
}
inline std::string priority_label(const Card& c) {
    if (c.priority.empty()) return {};
    return c.kind == Kind::Knot ? knot_priority(c.priority) : task_priority(c.priority);
}
inline Tone priority_tone(const Card& c) {
    return c.kind == Kind::Knot ? knot_priority_tone(c.priority) : task_priority_tone(c.priority);
}

// "Created 3d ago"'s second half.
inline std::string age(std::int64_t created_unix, std::int64_t now) {
    if (created_unix <= 0 || now < created_unix) return {};
    const std::int64_t e = now - created_unix;
    if (e < 60) return "just now";
    if (e < 3600) return std::to_string(e / 60) + "m ago";
    if (e < 86400) return std::to_string(e / 3600) + "h ago";
    return std::to_string(e / 86400) + "d ago";
}

// "agentcloud/puffin · feature · Gabe Ochoa · Created 3d ago", whichever parts
// are known ("" when none).
inline std::string footer(const Card& c, std::int64_t now) {
    std::vector<std::string> parts;
    if (!c.ns.empty()) parts.push_back(c.ns);
    if (!c.issue_type.empty()) parts.push_back(c.issue_type);
    if (!c.owner.empty()) parts.push_back(c.owner);
    if (const std::string a = age(c.created_unix, now); !a.empty()) parts.push_back("Created " + a);
    std::string out;
    for (const auto& p : parts) {
        if (!out.empty()) out += " \xc2\xb7 ";
        out += p;
    }
    return out;
}

// "13 diffs", "4 tasks", "2 knots", "13 references" when mixed.
inline std::string count_label(const std::vector<Card>& cards) {
    std::set<Kind> kinds;
    for (const auto& c : cards) kinds.insert(c.kind);
    const bool one = cards.size() == 1;
    std::string noun = one ? "reference" : "references";
    if (kinds.size() == 1) {
        switch (*kinds.begin()) {
            case Kind::Diff: noun = one ? "diff" : "diffs"; break;
            case Kind::Task: noun = one ? "task" : "tasks"; break;
            case Kind::Knot: noun = one ? "knot" : "knots"; break;
        }
    }
    return std::to_string(cards.size()) + " " + noun;
}

// ---- The wire --------------------------------------------------------------

inline constexpr const char* kDocument =
    "query HanabiLinkPreview($params: String!) { "
    "xfb_gchat_powertools_json_config(command: \"getLinkPreview\", params: $params) "
    "{ success data error } }";

// The web app's /api/graphql body: the command is fixed in the document and
// only the URL travels, as a variable.
inline std::string request_body(const Ref& r) {
    const std::string params = json{{"url", r.url()}}.dump();
    return json{{"query_text", kDocument}, {"variables", json{{"params", params}}.dump()}}.dump();
}

// The root field's object ({success, data, error}) -> a card, or nullopt for
// any answer that is not a preview of the kind asked for. `data` is JSON in a
// string; a card cannot be built without a title.
inline std::optional<Card> decode(const json& field, const Ref& r) {
    if (!field.is_object() || !field.value("success", false) || !field.contains("data") ||
        !field["data"].is_string())
        return std::nullopt;
    const json root = json::parse(field["data"].get<std::string>(), nullptr, false);
    if (!root.is_object() || !root.value("success", false) ||
        root.value("link_type", std::string()) != kind_name(r.kind) || !root.contains("common_data") ||
        !root["common_data"].is_object())
        return std::nullopt;
    const json& common = root["common_data"];
    const std::string title = common.value("title", std::string());
    if (title.empty()) return std::nullopt;
    Card c;
    c.kind = r.kind;
    c.id = r.id;
    c.title = title;
    c.url = common.contains("url") && common["url"].is_string() ? common["url"].get<std::string>()
                                                                 : r.url();
    if (common.contains("authorName") && common["authorName"].is_string())
        c.owner = common["authorName"].get<std::string>();
    if (common.contains("creationTime") && common["creationTime"].is_number())
        c.created_unix = common["creationTime"].get<std::int64_t>();
    if (root.contains("type_data") && root["type_data"].is_object()) {
        const json& t = root["type_data"];
        if (t.contains("status") && t["status"].is_string()) c.status = t["status"].get<std::string>();
        if (t.contains("priority") && t["priority"].is_string())
            c.priority = t["priority"].get<std::string>();
    }
    return c;
}

inline int ttl_seconds(const json& field, int fallback) {
    if (!field.is_object() || !field.contains("data") || !field["data"].is_string()) return fallback;
    const json root = json::parse(field["data"].get<std::string>(), nullptr, false);
    if (root.is_object() && root.contains("ttl_seconds") && root["ttl_seconds"].is_number_integer())
        return root["ttl_seconds"].get<int>();
    return fallback;
}

// The CLI argv for a knot.
inline std::vector<std::string> knot_locate_args(const Ref& r) {
    return {"knots.issue", "locate", "--id=" + r.id, "--output=json"};
}
inline std::vector<std::string> knot_get_args(const Ref& r, const std::string& ns) {
    return {"knots.issue", "get", "--namespace=" + ns, "--id=" + r.id,
            "--columns=id,title,status,priority,type,url", "--output=json"};
}
inline std::string knot_namespace(const std::string& locateOut) {
    const json j = json::parse(locateOut, nullptr, false);
    if (!j.is_object() || !j.contains("namespace") || !j["namespace"].is_string()) return {};
    return j["namespace"].get<std::string>();
}
// `locate` (where) + `get` (what) -> a card, or nullopt.
inline std::optional<Card> decode_knot(const std::string& locateOut, const std::string& issueOut,
                                       const Ref& r) {
    if (r.kind != Kind::Knot) return std::nullopt;
    const json place = json::parse(locateOut, nullptr, false);
    const json f = json::parse(issueOut, nullptr, false);
    if (!place.is_object() || !f.is_object()) return std::nullopt;
    const std::string title = f.value("title", std::string());
    if (title.empty() || f.value("id", r.id) != r.id) return std::nullopt;
    Card c;
    c.kind = Kind::Knot;
    c.id = r.id;
    c.title = title;
    if (f.contains("url") && f["url"].is_string()) c.url = f["url"].get<std::string>();
    else if (place.contains("url") && place["url"].is_string()) c.url = place["url"].get<std::string>();
    else c.url = r.url();
    if (f.contains("status") && f["status"].is_string()) c.status = f["status"].get<std::string>();
    if (f.contains("priority")) {
        if (f["priority"].is_string()) c.priority = f["priority"].get<std::string>();
        else if (f["priority"].is_number_integer()) c.priority = std::to_string(f["priority"].get<int>());
    }
    c.ns = place.value("namespace", std::string());
    c.issue_type = f.value("type", std::string());
    return c;
}

// ---- The cache rules -------------------------------------------------------

inline constexpr int kFailureTtl = 600;     // a blip, not a verdict
inline constexpr int kFallbackTtl = 3600;   // when the reply names none
inline constexpr int kKnotTtl = 900;
inline constexpr std::size_t kFetchesPerPass = 8;
inline constexpr std::size_t kRowsScanned = 200;

struct Entry {
    std::optional<Card> card;  // nullopt = this viewer gets no card (a result, cached)
    std::int64_t fetched = 0;
    int ttl = 0;
    [[nodiscard]] bool fresh(std::int64_t now) const { return now < fetched + ttl; }
};

// A failure that already has a good card keeps it, on the failure's short
// clock, so a proxy blip does not blank a card on screen.
inline Entry merged(const std::optional<Entry>& old, const std::optional<Card>& fetched,
                    std::int64_t now, int ttl) {
    if (!fetched && old && old->card) return {old->card, now, kFailureTtl};
    return {fetched, now, fetched ? ttl : kFailureTtl};
}

}  // namespace api::link_preview
