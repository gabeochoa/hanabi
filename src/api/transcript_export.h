#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "types.h"

namespace hanabi::api::transcript_export {

inline constexpr std::string_view kSchema = "puffin.transcript.v1";

struct Record {
    std::string kind;
    std::optional<std::uint64_t> seq;
    std::string role;
    std::string text;
    std::string markdown;
    std::optional<std::int64_t> created_at_unix_ms;
};

struct Header {
    std::string session_id;
    std::string title;
    bool complete = true;
    std::string seq_axis;
    std::string contents = "messages";
};

struct Document {
    Header header;
    std::vector<Record> records;
};

enum class Outcome { Written, RefusedSeqOrder, Empty };

inline std::optional<std::uint64_t> seq_of(const std::string& id) {
    if (id.empty()) return std::nullopt;
    std::uint64_t seq = 0;
    for (const char c : id) {
        if (c < '0' || c > '9') return std::nullopt;
        seq = seq * 10 + static_cast<std::uint64_t>(c - '0');
    }
    return seq;
}

inline std::string role_name(::api::Role r) {
    switch (r) {
        case ::api::Role::User: return "user";
        case ::api::Role::Assistant: return "assistant";
        case ::api::Role::System: return "system";
        case ::api::Role::Tool: return "tool";
    }
    return "assistant";
}

inline std::string plain_text(const std::string& in) {
    const auto strip_paired = [](std::string s, const std::string& d) {
        std::string r;
        r.reserve(s.size());
        std::size_t i = 0;
        while (i < s.size()) {
            if (s.compare(i, d.size(), d) == 0) {
                const std::size_t close = s.find(d, i + d.size());
                if (close != std::string::npos && close > i + d.size()) {
                    const std::string inner = s.substr(i + d.size(), close - (i + d.size()));
                    if (inner.find('\n') == std::string::npos) {
                        r += inner;
                        i = close + d.size();
                        continue;
                    }
                }
            }
            r += s[i++];
        }
        return r;
    };
    std::string out = strip_paired(in, "**");
    out = strip_paired(out, "__");
    return strip_paired(out, "`");
}

inline bool is_message(const ::api::Message& m) {
    return m.kind == ::api::EventKind::Text && m.subtitle != "thinking";
}

inline Outcome build(const ::api::Session& s, Document& out) {
    out = Document{};
    out.header.session_id = s.summary.id;
    out.header.title = s.summary.title;
    out.header.complete = !s.has_more_older;
    bool anySeq = false;
    bool anyMissing = false;
    std::optional<std::uint64_t> last;
    for (const ::api::Message& m : s.messages) {
        if (!is_message(m)) continue;
        Record r;
        r.kind = "message";
        r.seq = seq_of(m.id);
        if (r.seq) {
            anySeq = true;
            if (last && *r.seq <= *last) return Outcome::RefusedSeqOrder;
            last = r.seq;
        } else {
            anyMissing = true;
        }
        r.role = role_name(m.role);
        r.markdown = m.text;
        r.text = plain_text(m.text);
        if (m.created_at > 0) r.created_at_unix_ms = m.created_at * 1000;
        out.records.push_back(std::move(r));
    }
    out.header.seq_axis = !anySeq ? "absent" : (anyMissing ? "partial" : "valid");
    return out.records.empty() ? Outcome::Empty : Outcome::Written;
}

inline nlohmann::json metadata_json(const Header& h) {
    return nlohmann::json{{"schema", std::string(kSchema)},
                          {"session_id", h.session_id},
                          {"title", h.title},
                          {"complete", h.complete},
                          {"seq", h.seq_axis},
                          {"contents", h.contents},
                          {"source", "local_cache"}};
}

inline nlohmann::json record_json(const Record& r) {
    nlohmann::json j{{"kind", r.kind}, {"role", r.role}, {"text", r.text}, {"markdown", r.markdown}};
    if (r.seq) j["seq"] = *r.seq;
    if (r.created_at_unix_ms) j["created_at_unix_ms"] = *r.created_at_unix_ms;
    return j;
}

inline std::string to_json(const Document& d) {
    nlohmann::json records = nlohmann::json::array();
    for (const Record& r : d.records) records.push_back(record_json(r));
    const nlohmann::json j{{"metadata", metadata_json(d.header)}, {"records", std::move(records)}};
    return j.dump(2) + "\n";
}

inline std::string to_jsonl(const Document& d) {
    const nlohmann::json first{{"kind", "export_metadata"}, {"metadata", metadata_json(d.header)}};
    std::string out = first.dump() + "\n";
    for (const Record& r : d.records) out += record_json(r).dump() + "\n";
    return out;
}

inline std::string markdown_provenance(const Header& h) {
    if (h.complete) return std::string();
    return "_Exported from this Mac's local cache window; older history is not included._\n\n";
}

}
