#pragma once

// What a folder header can say beyond a bare member count.
//
// A header that reads "4" says the same thing whether the four are the whole
// folder, four the reader has already dealt with, four out of a catalog the
// last poll could not refresh, or four from the disk while the first live
// read is still out. The derived state below is pure so the rule can be
// asserted without a frame, and the header renders it and nothing else.

#include <string>
#include <vector>

#include "../api/types.h"
#include "thread_model.h"

namespace ecs::model {

// The catalog's read state as a header can honestly report it. A FAILED read
// outranks a pending one (a failure is an answer; a pending read is not), and
// both outrank the count: the count is what the last successful read left
// behind, and a header must not present it as current when it is not.
enum class CatalogRead { Loading, Failed, Fresh };

inline CatalogRead catalog_read(bool liveListSeen,
                               const std::string& listError) {
    if (!listError.empty()) return CatalogRead::Failed;
    return liveListSeen ? CatalogRead::Fresh : CatalogRead::Loading;
}

// "Unread" in this sidebar's vocabulary: a row that wants the reader. The
// same rule Home's badge and the Blocked and Review smart views count by, so
// a folder never claims a different number of waiting threads than the views
// that hold them.
inline bool wants_reader(const api::SessionSummary& s) {
    return in_blocked_view(s) || s.state == api::ThreadState::Ready;
}

struct FolderState {
    int total = 0;
    int attention = 0;
    int hidden = 0;
    CatalogRead read = CatalogRead::Fresh;

    [[nodiscard]] bool empty() const { return total == 0; }
};

inline FolderState folder_state(
    const std::vector<const api::SessionSummary*>& members, int hidden,
    CatalogRead read) {
    FolderState f;
    f.total = static_cast<int>(members.size());
    f.hidden = hidden;
    f.read = read;
    for (const api::SessionSummary* s : members)
        if (s != nullptr && wants_reader(*s)) ++f.attention;
    return f;
}

// The words in the header's count slot, one arm per state so no two states
// share a rendering.
inline std::string folder_count_label(const FolderState& f) {
    switch (f.read) {
        case CatalogRead::Failed: return "could not be read";
        case CatalogRead::Loading: return "loading\xe2\x80\xa6";
        case CatalogRead::Fresh: break;
    }
    if (f.total == 0)
        return f.hidden > 0 ? std::to_string(f.hidden) + " hidden" : "empty";
    if (f.attention > 0)
        return std::to_string(f.attention) + " need you \xc2\xb7 " +
               std::to_string(f.total);
    return std::to_string(f.total);
}

}  // namespace ecs::model
