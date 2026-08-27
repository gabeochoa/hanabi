#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../api/types.h"
#include "../ui/find_operators.h"
#include "../util/textscan.h"

namespace hanabi::find_memo {

inline constexpr std::size_t kMaxCachedMessages = 16384;

enum class CaseFoldPolicy { Ascii };

struct PaintPolicy {
    float wrap_width = 0.0f;
    bool fold_long_messages = false;
    bool show_reasoning = false;
    std::uint64_t fold_revision = 0;
    CaseFoldPolicy case_fold = CaseFoldPolicy::Ascii;
};

struct Match {
    int msg = 0;
    std::size_t line = 0;
    std::size_t off = 0;
};

struct Stats {
    std::size_t result_hits = 0;
    std::size_t result_misses = 0;
    std::size_t rows_visited = 0;
    std::size_t message_work = 0;
    std::size_t normalized = 0;
    std::size_t normalized_reused = 0;
};

inline bool message_is_paintable(const api::Message& m) {
    if (m.kind != api::EventKind::Text) return false;
    if (m.role != api::Role::User && m.role != api::Role::Assistant) return false;
    return !(m.role == api::Role::Assistant && m.subtitle == "thinking");
}

inline bool same_query(const find_ops::Query& a, const find_ops::Query& b) {
    if (a.text != b.text || a.invalid != b.invalid ||
        a.terms.size() != b.terms.size())
        return false;
    for (std::size_t i = 0; i < a.terms.size(); ++i) {
        if (a.terms[i].kind != b.terms[i].kind ||
            a.terms[i].role != b.terms[i].role ||
            a.terms[i].value != b.terms[i].value)
            return false;
    }
    return true;
}

inline bool same_policy(const PaintPolicy& a, const PaintPolicy& b) {
    return a.wrap_width == b.wrap_width &&
           a.fold_long_messages == b.fold_long_messages &&
           a.show_reasoning == b.show_reasoning &&
           a.fold_revision == b.fold_revision && a.case_fold == b.case_fold;
}

class Memo {
  public:
    template <class Normalize>
    const std::vector<Match>& collect(const api::Session& session,
                                      std::uint64_t content_version,
                                      const find_ops::Query& query,
                                      const PaintPolicy& policy,
                                      Normalize&& normalize) {
        const std::string& thread = session.summary.id;
        if (thread != thread_) reset_thread(thread);
        const bool semantic_change = !have_query_ || !same_query(query_, query) ||
                                     !same_policy(policy_, policy);
        if (have_result_ && !semantic_change &&
            result_content_version_ == content_version) {
            ++stats_.result_hits;
            return matches_;
        }
        ++stats_.result_misses;
        if (semantic_change) {
            query_ = query;
            policy_ = policy;
            have_query_ = true;
            ++query_generation_;
        }
        if (query_.invalid || query_.text.empty()) {
            matches_.clear();
            row_paintable_.assign(session.messages.size(), false);
            row_has_match_.assign(session.messages.size(), false);
            result_content_version_ = content_version;
            have_result_ = true;
            return matches_;
        }
        if (!have_corpus_ || corpus_content_version_ != content_version) {
            sync(session, std::forward<Normalize>(normalize));
            corpus_content_version_ = content_version;
            have_corpus_ = true;
        }
        rebuild_matches(session, std::forward<Normalize>(normalize));
        result_content_version_ = content_version;
        have_result_ = true;
        return matches_;
    }

    const find_ops::Query& query() const { return query_; }

    bool row_is_paintable(std::size_t index) const {
        return index < row_paintable_.size() && row_paintable_[index];
    }

    bool message_has_match(std::size_t index) const {
        return index < row_has_match_.size() && row_has_match_[index];
    }

    std::size_t entries() const { return entries_.size(); }
    static constexpr std::size_t capacity() { return kMaxCachedMessages; }
    const Stats& stats() const { return stats_; }

    void clear() {
        thread_.clear();
        entries_.clear();
        sequence_.clear();
        matches_.clear();
        row_paintable_.clear();
        row_has_match_.clear();
        have_corpus_ = false;
        have_query_ = false;
        have_result_ = false;
        corpus_content_version_ = 0;
        result_content_version_ = 0;
        ++query_generation_;
    }

  private:
    struct Entry {
        std::string id;
        std::string text;
        std::string subtitle;
        std::string tool_status;
        api::Role role = api::Role::Assistant;
        api::EventKind kind = api::EventKind::Text;
        std::vector<std::string> lines;
        std::vector<std::pair<std::size_t, std::size_t>> hits;
        std::uint64_t query_generation = 0;
        std::uint64_t turn_signature = 0;
        std::uint64_t seen = 0;
    };

    static std::string identity(const api::Message& m, std::size_t index) {
        if (!m.id.empty()) return std::string("id\x1f") + m.id;
        return std::string("index\x1f") + std::to_string(index);
    }

    static bool same_message(const Entry& e, const api::Message& m) {
        return e.id == m.id && e.text == m.text && e.subtitle == m.subtitle &&
               e.tool_status == m.tool_status && e.role == m.role &&
               e.kind == m.kind;
    }

    static void assign_message(Entry& e, const api::Message& m) {
        e.id = m.id;
        e.text = m.text;
        e.subtitle = m.subtitle;
        e.tool_status = m.tool_status;
        e.role = m.role;
        e.kind = m.kind;
    }

    static std::uint64_t mix(std::uint64_t h, std::string_view value) {
        for (unsigned char c : value) {
            h ^= c;
            h *= 1099511628211ULL;
        }
        h ^= value.size();
        h *= 1099511628211ULL;
        return h;
    }

    static std::uint64_t turn_signature(const api::Session& session,
                                        std::size_t index) {
        const auto [lo, hi] = find_ops::turn_bounds(session, index);
        std::uint64_t h = 1469598103934665603ULL;
        if (lo >= session.messages.size()) return h;
        for (std::size_t i = lo; i <= hi; ++i) {
            const api::Message& m = session.messages[i];
            h ^= static_cast<std::uint64_t>(m.role);
            h *= 1099511628211ULL;
            h ^= static_cast<std::uint64_t>(m.kind);
            h *= 1099511628211ULL;
            h = mix(h, m.tool_status);
        }
        return h;
    }

    void reset_thread(const std::string& thread) {
        clear();
        thread_ = thread;
    }

    template <class Normalize>
    void sync(const api::Session& session, Normalize&& normalize) {
        ++sync_generation_;
        const bool cacheable = session.messages.size() <= kMaxCachedMessages;
        sequence_.assign(cacheable ? session.messages.size() : 0, nullptr);
        if (!cacheable) entries_.clear();
        for (std::size_t i = 0; i < session.messages.size(); ++i) {
            const api::Message& m = session.messages[i];
            if (!message_is_paintable(m)) continue;
            if (!cacheable) continue;
            const std::string key = identity(m, i);
            auto [it, inserted] = entries_.try_emplace(key);
            Entry& e = it->second;
            if (inserted || !same_message(e, m)) {
                assign_message(e, m);
                e.lines = std::invoke(normalize, m,
                                      m.role == api::Role::Assistant);
                e.query_generation = 0;
                ++stats_.normalized;
            } else {
                ++stats_.normalized_reused;
            }
            e.seen = sync_generation_;
            sequence_[i] = &e;
        }
        if (cacheable) {
            for (auto it = entries_.begin(); it != entries_.end();) {
                if (it->second.seen != sync_generation_)
                    it = entries_.erase(it);
                else
                    ++it;
            }
        }
    }

    template <class Normalize>
    void rebuild_matches(const api::Session& session, Normalize&& normalize) {
        matches_.clear();
        row_paintable_.assign(session.messages.size(), false);
        row_has_match_.assign(session.messages.size(), false);
        const bool cacheable = sequence_.size() == session.messages.size();
        for (std::size_t i = 0; i < session.messages.size(); ++i) {
            ++stats_.rows_visited;
            const api::Message& m = session.messages[i];
            if (!message_is_paintable(m)) continue;
            row_paintable_[i] = find_ops::row_matches(session, i, query_);
            if (!row_paintable_[i]) continue;
            Entry transient;
            Entry* e = cacheable ? sequence_[i] : &transient;
            if (e == nullptr) continue;
            if (!cacheable) {
                assign_message(*e, m);
                e->lines = std::invoke(normalize, m,
                                       m.role == api::Role::Assistant);
                ++stats_.normalized;
            }
            const std::uint64_t turn = turn_signature(session, i);
            if (e->query_generation != query_generation_ ||
                e->turn_signature != turn) {
                ++stats_.message_work;
                e->hits.clear();
                if (!query_.invalid && !query_.text.empty()) {
                    for (std::size_t line = 0; line < e->lines.size(); ++line) {
                        for (std::size_t off :
                             textscan::occurrences(e->lines[line], query_.text))
                            e->hits.emplace_back(line, off);
                    }
                }
                e->query_generation = query_generation_;
                e->turn_signature = turn;
            }
            if (!e->hits.empty()) row_has_match_[i] = true;
            for (const auto& hit : e->hits)
                matches_.push_back(
                    Match{static_cast<int>(i), hit.first, hit.second});
        }
    }

    std::string thread_;
    std::unordered_map<std::string, Entry> entries_;
    std::vector<Entry*> sequence_;
    std::vector<Match> matches_;
    std::vector<bool> row_paintable_;
    std::vector<bool> row_has_match_;
    find_ops::Query query_;
    PaintPolicy policy_;
    Stats stats_;
    std::uint64_t sync_generation_ = 0;
    std::uint64_t query_generation_ = 0;
    std::uint64_t corpus_content_version_ = 0;
    std::uint64_t result_content_version_ = 0;
    bool have_corpus_ = false;
    bool have_query_ = false;
    bool have_result_ = false;
};

}  // namespace hanabi::find_memo
