#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

namespace hanabi::tab_find {

struct MatchKey {
    std::string msg;
    std::size_t line = 0;
    std::size_t off = 0;
    bool empty() const { return msg.empty(); }
    bool operator==(const MatchKey&) const = default;
};

struct Entry {
    bool open = false;
    std::string query;
    MatchKey current;
    bool operator==(const Entry&) const = default;
};

// Where a search with no live current match starts: the oldest match, or the
// newest when the reader asked for that (Settings, "Start from the newest
// match"). Matches are ordered oldest first.
inline int fresh_index(int count, bool newestFirst) {
    return newestFirst && count > 0 ? count - 1 : 0;
}

template <typename Matches, typename IdOf>
int resolve(const Matches& matches, MatchKey& current, int index, IdOf&& idOf,
            bool newestFirst = false) {
    const int n = static_cast<int>(matches.size());
    if (n == 0) {
        current = {};
        return 0;
    }
    if (!current.empty()) {
        for (int i = 0; i < n; ++i) {
            const auto& m = matches[static_cast<std::size_t>(i)];
            if (m.line == current.line && m.off == current.off && idOf(m.msg) == current.msg) return i;
        }
        index = fresh_index(n, newestFirst);
    }
    if (index < 0 || index >= n) index = fresh_index(n, newestFirst);
    const auto& m = matches[static_cast<std::size_t>(index)];
    current = MatchKey{idOf(m.msg), m.line, m.off};
    return index;
}

inline bool worth_keeping(const Entry& e) { return e.open || !e.query.empty(); }

class Store {
   public:
    const Entry* entry(const std::string& sessionId) const {
        const auto it = entries_.find(sessionId);
        return it == entries_.end() ? nullptr : &it->second;
    }
    void set(const std::string& sessionId, const Entry& e) {
        if (sessionId.empty()) return;
        if (worth_keeping(e)) entries_[sessionId] = e;
        else entries_.erase(sessionId);
    }
    void forget(const std::string& sessionId) { entries_.erase(sessionId); }
    std::size_t size() const { return entries_.size(); }

   private:
    std::unordered_map<std::string, Entry> entries_;
};

struct PaneFind {
    bool open = false;
    std::string query;
    MatchKey match;
    std::string syncedId;
    Entry synced;

    Entry current() const { return Entry{open, query, match}; }
    void adopt(const Entry& e) {
        open = e.open;
        query = e.query;
        match = e.current;
        synced = e;
    }
};

inline void sync(Store& store, PaneFind& pane, const std::string& shownId) {
    const Entry now = pane.current();
    if (pane.syncedId == shownId) {
        if (now != pane.synced) {
            store.set(shownId, now);
            pane.synced = now;
        }
        return;
    }
    if (!pane.syncedId.empty() && now != pane.synced) store.set(pane.syncedId, now);
    const bool firstFrame = pane.syncedId.empty();
    pane.syncedId = shownId;
    const Entry* held = shownId.empty() ? nullptr : store.entry(shownId);
    if (held != nullptr) {
        pane.adopt(*held);
    } else if (firstFrame && !shownId.empty() && worth_keeping(now)) {
        store.set(shownId, now);
        pane.synced = now;
    } else {
        pane.adopt(Entry{});
    }
}

template <typename Pane>
void sync_pane(Store& store, Pane& p, const std::string& shownId) {
    PaneFind view{p.findOpen, p.findQuery, p.findCurrent, p.findSyncedId, p.findSynced};
    sync(store, view, shownId);
    p.findOpen = view.open;
    p.findQuery = view.query;
    p.findCurrent = view.match;
    p.findSyncedId = view.syncedId;
    p.findSynced = view.synced;
}

}
