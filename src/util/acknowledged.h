#pragma once
// ---------------------------------------------------------------------------
// "I have seen that one": a list of ids, and the three things anyone needs
// to do with it.
//
// Pulled out of Settings so it can be tested without a settings file, a
// config path or a filesystem -- the rules are what matter, and they are the
// kind that go wrong quietly. An empty id treated as acknowledged silences a
// record nobody ever saw; a duplicate grows a list that is written to disk
// on every acknowledgement.
//
// Settings owns the storage and calls these. Nothing here reads or writes
// anything.

#include <string>
#include <vector>

namespace hanabi::model {

// An id with nothing in it is never acknowledged. This fails OPEN on
// purpose: an unidentifiable record keeps being offered rather than being
// silently swallowed.
[[nodiscard]] inline bool acknowledged(const std::vector<std::string>& list,
                                       const std::string& id) {
    if (id.empty()) return false;
    for (const auto& e : list)
        if (e == id) return true;
    return false;
}

// Returns whether the list CHANGED, so a caller that persists on change can
// skip the write when nothing happened.
inline bool acknowledge(std::vector<std::string>& list,
                        const std::string& id) {
    if (id.empty() || acknowledged(list, id)) return false;
    list.push_back(id);
    return true;
}

}  // namespace hanabi::model
