#pragma once

#include <string>
#include <unordered_map>

namespace hanabi::tab_find {

struct Entry {
    bool open = false;
    std::string query;
    int index = 0;
    bool operator==(const Entry&) const = default;
};

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
    int index = 0;
    std::string syncedId;
    Entry synced;

    Entry current() const { return Entry{open, query, index}; }
    void adopt(const Entry& e) {
        open = e.open;
        query = e.query;
        index = e.index;
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

inline void close_bar(PaneFind& pane) {
    pane.open = false;
    pane.index = 0;
}

template <typename Pane>
void sync_pane(Store& store, Pane& p, const std::string& shownId) {
    PaneFind view{p.findOpen, p.findQuery, p.findIndex, p.findSyncedId, p.findSynced};
    sync(store, view, shownId);
    p.findOpen = view.open;
    p.findQuery = view.query;
    p.findIndex = view.index;
    p.findSyncedId = view.syncedId;
    p.findSynced = view.synced;
}

}
