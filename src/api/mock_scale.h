#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "types.h"

namespace api::mock_scale {

inline constexpr int kMax = 1000;

inline int parse(const char* value) {
    if (value == nullptr || *value == '\0') return 1;
    const int n = std::atoi(value);
    if (n < 1) return 1;
    return n > kMax ? kMax : n;
}

inline int from_env() { return parse(std::getenv("HANABI_MOCK_SCALE")); }

inline std::string clone_id(const std::string& base, int copy) {
    return base + "x" + std::to_string(copy);
}

inline std::int64_t clone_last_seq(std::size_t baseMessages, int scale) {
    return static_cast<std::int64_t>(baseMessages) * static_cast<std::int64_t>(scale);
}

inline Session clone(const Session& base, int copy, int scale, std::int64_t now) {
    Session out = base;
    out.summary.id = clone_id(base.summary.id, copy);
    out.summary.title = base.summary.title + " \xc3\x97" + std::to_string(copy);
    out.summary.updated_at = base.summary.updated_at - static_cast<std::int64_t>(copy) * 61;
    out.summary.starred = false;
    out.summary.folder.clear();
    out.summary.web_folder.clear();
    out.sub_agents.clear();
    out.has_more_older = false;
    out.messages.clear();
    const std::size_t n = base.messages.size();
    out.messages.reserve(n * static_cast<std::size_t>(scale));
    std::int64_t seq = 0;
    const std::int64_t total = clone_last_seq(n, scale);
    for (int rep = 0; rep < scale; ++rep) {
        for (const Message& m : base.messages) {
            Message c = m;
            ++seq;
            c.id = std::to_string(seq);
            c.created_at = now - (total - seq) * 30;
            if (!c.attachments.empty())
                for (Attachment& a : c.attachments)
                    if (!a.file_id.empty()) a.file_id += "-" + std::to_string(copy) + "-" + std::to_string(rep);
            out.messages.push_back(std::move(c));
        }
    }
    out.summary.last_seq = total;
    out.summary.preview = out.messages.empty() ? std::string() : out.messages.back().text.substr(0, 80);
    return out;
}

template <typename Sessions>
void apply(Sessions& v, int scale, std::int64_t now) {
    if (scale <= 1) return;
    const std::size_t baseCount = v.size();
    v.reserve(baseCount * static_cast<std::size_t>(scale));
    for (int copy = 1; copy < scale; ++copy)
        for (std::size_t i = 0; i < baseCount; ++i) v.push_back(clone(v[i], copy, scale, now));
}

struct Totals {
    std::size_t sessions = 0;
    std::size_t messages = 0;
    std::size_t tool_rows = 0;
    std::size_t attachments = 0;
};

template <typename Sessions>
Totals totals(const Sessions& v) {
    Totals t;
    t.sessions = v.size();
    for (const auto& s : v) {
        t.messages += s.messages.size();
        for (const auto& m : s.messages) {
            if (m.role == Role::Tool) ++t.tool_rows;
            t.attachments += m.attachments.size();
        }
    }
    return t;
}

}
