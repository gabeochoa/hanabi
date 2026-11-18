#pragma once

#include <algorithm>

// Sidebar search operators (the reference's 0.8.4: "Sidebar search by
// `last_active:` today, yesterday, or a date").
//
//   last_active:today        threads whose last activity is today (local time)
//   last_active:yesterday    ... yesterday
//   last_active:2026-09-30   ... on that calendar day
//
// The operator is pulled out of the query and the rest of the words are
// matched as before (title, then the content index). A value this build does
// not understand leaves the whole token as ordinary search text, so a typo
// narrows the list to nothing rather than silently widening it.
//
// Pure: the clock and the time zone come in as arguments, so the unit test
// decides what "today" is.
//
// The reference's wider grammar, mirrored where this client's rows can
// answer it (kt-lpat):
//
//   space:<id>      the thread is filed in that Space. Rows carry only the
//                   Space's id (`space_group` is "space:<fbid>"), never its
//                   name, so a name typed here matches nothing — documented
//                   divergence from the reference, which resolves names.
//   is:needs-me     the thread wants the viewer: its tag is Blocked,
//                   Waiting or Failed, or it sits in Review (state Ready).
//                   The reference reads the same three shelves as one word.
//   is:mine         every catalog row is the viewer's own, so this admits
//                   every row — a parse-level no-op, kept so a query copied
//                   from the reference means the same thing here.
//   within:<N>d     last activity today or in the N-1 whole local days
//                   before it (`within:7d`, `within:30 days`). The same
//                   window `last_active:` names one day at a time.
//   has:files       a message carries a non-picture file. A list row holds
//                   no file list, so the fact is unknown on this surface:
//                   the filter parses, is recorded, and neither admits nor
//                   refuses — the reference's held-back rule. NOT a silent
//                   match-everything: `filters` names it, and a surface
//                   that can fill the fact in gets a real answer.
//
// A leading `-` excludes (`-space:x`, `-is:needs-me`) and commas inside one
// value list alternatives of which any one admits (`space:a,b`,
// `-space:a,b` excludes either). Tokens still AND. A `-` not glued to a
// known operator is a plain word, a comma inside quotes is part of the
// value, and a known operator with a value this grammar cannot read stays
// ordinary search text — nothing typed is ever dropped.

#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hanabi::sidebar_query {

struct Query {
    std::string text;          // the words left after the operator, trimmed
    bool hasDay = false;
    std::int64_t dayStart = 0;  // unix seconds, inclusive
    std::int64_t dayEnd = 0;    // unix seconds, exclusive

    enum class FilterKind { Space, NeedsMe, Mine, Within, HasFiles };
    struct Filter {
        FilterKind kind = FilterKind::Mine;
        bool negated = false;
        std::vector<std::string> values;  // space: alternatives, lowered
        // within: one [start, end) span per alternative, unix seconds.
        std::vector<std::pair<std::int64_t, std::int64_t>> spans;
        // is: one entry per alternative (`is:needs-me,mine`); empty means
        // the single `kind` above. `mine` inside a list narrows rather
        // than widens (the reference's ruling), so it drops out when any
        // other alternative is present.
        std::vector<FilterKind> isValues;
    };
    std::vector<Filter> filters;

    // What one list row can say to the typed filters. `hasFiles` stays
    // disengaged until a row carries a file list (see the header note).
    struct Facts {
        std::int64_t updatedAt = 0;
        std::string spaceGroup;  // "space:<fbid>", "" when unfiled
        bool needsMe = false;
        std::optional<bool> hasFiles;
    };

    [[nodiscard]] bool admits(std::int64_t updatedAt) const {
        if (hasDay && (updatedAt < dayStart || updatedAt >= dayEnd)) return false;
        for (const Filter& f : filters) {
            if (f.kind != FilterKind::Within) continue;
            bool in = false;
            for (const auto& span : f.spans)
                if (updatedAt >= span.first && updatedAt < span.second) in = true;
            if (f.negated ? in : !in) return false;
        }
        return true;
    }

    // Whether the typed filters admit this row. An unknown fact (has:files
    // on a list row) neither admits nor refuses, negated or not.
    [[nodiscard]] bool admitsRow(const Facts& facts) const {
        if (!admits(facts.updatedAt)) return false;
        for (const Filter& f : filters) {
            std::optional<bool> held;
            if (!f.isValues.empty()) {
                std::vector<FilterKind> effective = f.isValues;
                if (effective.size() > 1) {
                    std::vector<FilterKind> narrowed;
                    for (FilterKind k : effective)
                        if (k != FilterKind::Mine) narrowed.push_back(k);
                    if (!narrowed.empty()) effective = std::move(narrowed);
                }
                bool any = false;
                for (FilterKind k : effective)
                    if (k == FilterKind::Mine || (k == FilterKind::NeedsMe && facts.needsMe))
                        any = true;
                held = any;
            } else
            switch (f.kind) {
                case FilterKind::Within:
                case FilterKind::Mine: held = true; break;
                case FilterKind::NeedsMe: held = facts.needsMe; break;
                case FilterKind::HasFiles: held = facts.hasFiles; break;
                case FilterKind::Space: {
                    bool any = false;
                    for (const std::string& v : f.values) {
                        std::string group = facts.spaceGroup;
                        for (char& c : group)
                            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
                        const std::string id =
                            group.rfind("space:", 0) == 0 ? group.substr(6) : group;
                        if (!v.empty() && (id == v || group == v)) any = true;
                    }
                    held = any;
                    break;
                }
            }
            if (!held.has_value()) continue;
            if (f.negated ? *held : !*held) return false;
        }
        return true;
    }
};

inline constexpr std::string_view kLastActive = "last_active:";

// Local midnight of the day `offsetDays` from the day containing `now`.
inline std::int64_t local_midnight(std::int64_t now, int offsetDays) {
    std::time_t t = static_cast<std::time_t>(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_mday += offsetDays;
    tm.tm_isdst = -1;
    return static_cast<std::int64_t>(std::mktime(&tm));
}

inline bool parse_date(std::string_view v, std::int64_t* start) {
    if (v.size() != 10 || v[4] != '-' || v[7] != '-') return false;
    for (std::size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u})
        if (v[i] < '0' || v[i] > '9') return false;
    const auto num = [&](std::size_t at, std::size_t n) {
        int x = 0;
        for (std::size_t i = 0; i < n; ++i) x = x * 10 + (v[at + i] - '0');
        return x;
    };
    std::tm tm{};
    tm.tm_year = num(0, 4) - 1900;
    tm.tm_mon = num(5, 2) - 1;
    tm.tm_mday = num(8, 2);
    if (tm.tm_mon < 0 || tm.tm_mon > 11 || tm.tm_mday < 1 || tm.tm_mday > 31) return false;
    tm.tm_isdst = -1;
    const int wantDay = tm.tm_mday;
    const std::time_t t = std::mktime(&tm);
    if (t == static_cast<std::time_t>(-1) || tm.tm_mday != wantDay) return false;  // Feb 30
    *start = static_cast<std::int64_t>(t);
    return true;
}

inline std::string lowered_str(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

// A value with its surrounding quotes and escapes removed.
inline std::string unquote(std::string_view raw) {
    if (raw.size() < 2 || raw.front() != '"') return std::string(raw);
    std::string out;
    bool escaping = false;
    for (std::size_t i = 1; i < raw.size(); ++i) {
        const char c = raw[i];
        if (escaping) {
            out.push_back(c);
            escaping = false;
        } else if (c == '\\') {
            escaping = true;
        } else if (c == '"') {
            break;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

// Split on commas outside quotes; each part unquoted, empties dropped.
inline std::vector<std::string> split_alternatives(std::string_view raw) {
    if (raw.find(',') == std::string_view::npos) {
        std::string one = unquote(raw);
        return one.empty() ? std::vector<std::string>{} : std::vector<std::string>{one};
    }
    std::vector<std::string> parts;
    std::string current;
    bool quoted = false, escaping = false;
    for (char c : raw) {
        if (quoted) {
            current.push_back(c);
            if (escaping) escaping = false;
            else if (c == '\\') escaping = true;
            else if (c == '"') quoted = false;
        } else if (c == ',') {
            parts.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(c);
            if (c == '"') quoted = true;
        }
    }
    parts.push_back(std::move(current));
    std::vector<std::string> out;
    for (const std::string& p : parts) {
        std::string v = unquote(p);
        if (!v.empty()) out.push_back(std::move(v));
    }
    return out;
}

// Whitespace-separated words; a `"` opens a run whitespace does not split.
inline std::vector<std::string> lex_words(std::string_view q) {
    std::vector<std::string> words;
    std::size_t i = 0;
    while (i < q.size()) {
        while (i < q.size() && (q[i] == ' ' || q[i] == '\t')) ++i;
        if (i >= q.size()) break;
        const std::size_t start = i;
        bool quoted = false;
        while (i < q.size()) {
            const char c = q[i];
            if (quoted) {
                if (c == '\\' && i + 1 < q.size()) { i += 2; continue; }
                if (c == '"') quoted = false;
            } else {
                if (c == ' ' || c == '\t') break;
                if (c == '"') quoted = true;
            }
            ++i;
        }
        words.emplace_back(q.substr(start, i - start));
    }
    return words;
}

// A `within:` value as a day count: `7d`, `30 days`, `30-days`; 1..3650.
// -1 when it names no window (another unit, a date word, a negative).
inline int within_days(std::string_view value) {
    const std::string phrase = lowered_str(value);
    std::string compact;
    for (char c : phrase)
        if (c != ' ' && c != '\t' && c != '_') compact.push_back(c);
    std::size_t digits = 0;
    while (digits < compact.size() && compact[digits] >= '0' && compact[digits] <= '9')
        ++digits;
    if (digits == 0) return -1;
    int days = 0;
    for (std::size_t i = 0; i < digits; ++i) {
        days = days * 10 + (compact[i] - '0');
        if (days > 3650) return -1;
    }
    std::string unit = compact.substr(digits);
    if (unit.rfind("-", 0) == 0) unit = unit.substr(1);
    if (days < 1 || (unit != "d" && unit != "day" && unit != "days")) return -1;
    return days;
}

// `q` is the sidebar query as typed (any case).
inline Query parse(std::string_view q, std::int64_t now) {
    Query out;
    std::string rest;
    for (const std::string& word : lex_words(q)) {
        bool consumed = false;
        if (!out.hasDay && word.size() > kLastActive.size() &&
            lowered_str(std::string_view(word).substr(0, kLastActive.size())) == kLastActive) {
            const std::string value = lowered_str(unquote(
                std::string_view(word).substr(kLastActive.size())));
            std::int64_t start = 0;
            if (value == "today") {
                start = local_midnight(now, 0);
                consumed = true;
            } else if (value == "yesterday") {
                start = local_midnight(now, -1);
                consumed = true;
            } else if (parse_date(value, &start)) {
                consumed = true;
            }
            if (consumed) {
                out.hasDay = true;
                out.dayStart = start;
                out.dayEnd = local_midnight(start, 1);
            }
        }
        if (!consumed) {
            // The reference's operators: optional `-`, then `op:value`.
            const bool negated = word.size() > 1 && word[0] == '-';
            const std::string body = negated ? word.substr(1) : word;
            const std::size_t colon = body.find(':');
            if (colon != std::string::npos && colon > 0) {
                const std::string op = lowered_str(std::string_view(body).substr(0, colon));
                const std::string_view rawValue = std::string_view(body).substr(colon + 1);
                if (op == "space" || op == "is" || op == "within" || op == "has") {
                    const std::vector<std::string> alts = split_alternatives(rawValue);
                    Query::Filter filter;
                    filter.negated = negated;
                    bool ok = !alts.empty();
                    if (op == "space") {
                        filter.kind = Query::FilterKind::Space;
                        for (const std::string& a : alts) filter.values.push_back(lowered_str(a));
                    } else if (op == "is") {
                        for (const std::string& a : alts) {
                            const std::string v = lowered_str(a);
                            if (v == "needs-me") filter.isValues.push_back(Query::FilterKind::NeedsMe);
                            else if (v == "mine") filter.isValues.push_back(Query::FilterKind::Mine);
                            else ok = false;
                        }
                        filter.kind = filter.isValues.empty() ? Query::FilterKind::Mine
                                                              : filter.isValues.front();
                    } else if (op == "within") {
                        filter.kind = Query::FilterKind::Within;
                        for (const std::string& a : alts) {
                            const int days = within_days(a);
                            if (days < 1) { ok = false; break; }
                            filter.spans.emplace_back(local_midnight(now, -(days - 1)),
                                                      local_midnight(now, 1));
                        }
                    } else {  // has:
                        filter.kind = Query::FilterKind::HasFiles;
                        for (const std::string& a : alts) {
                            const std::string v = lowered_str(a);
                            if (v != "files" && v != "file") ok = false;
                        }
                    }
                    if (ok) {
                        bool dup = false;
                        for (const Query::Filter& f : out.filters)
                            if (f.kind == filter.kind && f.negated == filter.negated &&
                                f.values == filter.values && f.spans == filter.spans &&
                                f.isValues == filter.isValues)
                                dup = true;
                        if (!dup) out.filters.push_back(std::move(filter));
                        consumed = true;
                    }
                }
            }
        }
        if (!consumed) {
            if (!rest.empty()) rest += ' ';
            rest += word;
        }
    }
    out.text = std::move(rest);
    return out;
}

// The `last_active:` value in a query as typed ("today", "yesterday",
// "2026-10-02"), lower-cased; "" when there is none. The list options menu
// reads it to tick the row that is in force.
inline std::string last_active_value(std::string_view q) {
    std::size_t i = 0;
    while (i < q.size()) {
        while (i < q.size() && q[i] == ' ') ++i;
        std::size_t j = i;
        while (j < q.size() && q[j] != ' ') ++j;
        if (j == i) break;
        std::string word(q.substr(i, j - i));
        for (char& c : word)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (word.size() > kLastActive.size() && word.rfind(kLastActive, 0) == 0)
            return word.substr(kLastActive.size());
        i = j;
    }
    return {};
}

// The query with every `last_active:` token removed and, when `value` is not
// empty, one `last_active:<value>` appended -- what the menu's rows write, so
// the menu and a typed token are the same filter. The rest of the query is
// kept word for word.
inline std::string with_last_active(std::string_view q, std::string_view value) {
    std::string out;
    std::size_t i = 0;
    while (i < q.size()) {
        while (i < q.size() && q[i] == ' ') ++i;
        std::size_t j = i;
        while (j < q.size() && q[j] != ' ') ++j;
        if (j == i) break;
        const std::string_view word = q.substr(i, j - i);
        std::string head(word.substr(0, std::min(word.size(), kLastActive.size())));
        for (char& c : head)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (!(word.size() > kLastActive.size() && head == kLastActive)) {
            if (!out.empty()) out += ' ';
            out.append(word);
        }
        i = j;
    }
    if (!value.empty()) {
        if (!out.empty()) out += ' ';
        out += std::string(kLastActive) + std::string(value);
    }
    return out;
}

}  // namespace hanabi::sidebar_query
