#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace hanabi::read_cursor {

inline std::int64_t seq_of(std::string_view id) {
    if (id.empty() || id.size() > 18) return 0;
    for (char c : id)
        if (c < '0' || c > '9') return 0;
    return std::strtoll(std::string(id).c_str(), nullptr, 10);
}

inline bool ids_are_seqs(std::string_view firstId, std::string_view lastId) {
    return seq_of(firstId) > 0 && seq_of(lastId) > 0;
}

inline int unread_count(std::int64_t lastSeq, std::int64_t readSeq) {
    if (lastSeq <= 0 || readSeq <= 0) return 0;
    if (readSeq >= lastSeq) return 0;
    const std::int64_t n = lastSeq - readSeq;
    return n > 999 ? 999 : static_cast<int>(n);
}

inline std::string unread_label(int n) {
    if (n <= 0) return {};
    return n >= 999 ? "999+" : std::to_string(n);
}

template <typename Messages>
int first_unread_by_seq(const Messages& messages, std::int64_t readSeq) {
    if (readSeq <= 0) return -1;
    for (std::size_t i = 0; i < messages.size(); ++i) {
        const std::int64_t s = seq_of(messages[i].id);
        if (s > 0 && s > readSeq) return static_cast<int>(i);
    }
    return -1;
}

template <typename Messages>
std::int64_t stamp_at_or_before_seq(const Messages& messages, std::int64_t readSeq,
                                    std::int64_t fallbackStamp) {
    if (readSeq <= 0) return fallbackStamp;
    std::int64_t best = 0;
    bool anySeq = false;
    for (const auto& m : messages) {
        const std::int64_t s = seq_of(m.id);
        if (s <= 0) continue;
        anySeq = true;
        if (s <= readSeq && m.created_at > best) best = m.created_at;
    }
    if (!anySeq) return fallbackStamp;
    return best > 0 ? best : fallbackStamp;
}

template <typename Messages>
std::int64_t newest_seq(const Messages& messages) {
    std::int64_t best = 0;
    for (const auto& m : messages) {
        const std::int64_t s = seq_of(m.id);
        if (s > best) best = s;
    }
    return best;
}

}
