#pragma once

// How a refetched transcript lands on the one already on screen.
//
// A refetch used to REPLACE the pane's messages and reset the transcript, so
// every reconnect and every live refetch put the reader back at the newest
// window: rows they had scrolled to were gone if they were older than the
// window, and the scroll anchor started over. But a refetch of a thread that
// is already open is almost always the same rows plus a tail, and the
// transcript has always known how to take a tail (Append) and a changed row
// (Update) without moving the reader. This is the rule that says which it is.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "../api/element_rows.h"
#include "../api/types.h"

namespace ecs::model {

struct ReconcileOutcome {
    enum class Kind { Unchanged, Appended, Updated, Reset };
    Kind kind = Kind::Unchanged;
    // Appended: first index of the new tail, and its length.
    // Updated: the index range that changed in place.
    std::size_t first = 0;
    std::size_t count = 0;
};

inline bool same_row(const api::Message& a, const api::Message& b) {
    return a.role == b.role && a.kind == b.kind && a.text == b.text &&
           a.subtitle == b.subtitle && a.tool_status == b.tool_status &&
           a.tool_result == b.tool_result && a.tool_node == b.tool_node &&
           a.tool_duration_ms == b.tool_duration_ms &&
           a.attachments.size() == b.attachments.size() &&
           a.artifact.hidden == b.artifact.hidden &&
           a.artifact.version == b.artifact.version &&
           a.artifact.file_count == b.artifact.file_count &&
           a.run_outcome == b.run_outcome && a.run_note == b.run_note;
}

inline void carry_artifact_state(const api::Message& from, api::Message& to) {
    if (to.kind != api::EventKind::Artifact || from.artifact.version != to.artifact.version) return;
    to.artifact.local_path = from.artifact.local_path;
    to.artifact.fetch = from.artifact.fetch;
    to.artifact.unavailable_reason = from.artifact.unavailable_reason;
    if (to.artifact.media_type.empty()) to.artifact.media_type = from.artifact.media_type;
    if (to.artifact.file.empty()) to.artifact.file = from.artifact.file;
    to.image_path = from.image_path;
}

// The newest durable seq the transcript holds, or 0 when its ids are not
// seqs (the mock's "m1", a locally minted row): the cursor a resume asks
// the server to read from.
inline std::uint64_t newest_seq(const std::vector<api::Message>& messages) {
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (it->id.empty() || it->sync != api::SyncState::None) continue;
        std::uint64_t seq = 0;
        for (const char c : it->id) {
            if (c < '0' || c > '9') return 0;
            seq = seq * 10 + static_cast<std::uint64_t>(c - '0');
        }
        return seq;
    }
    return 0;
}

// Folds `fresh` into `existing`. Rows `fresh` shares with `existing` (by id)
// are refreshed in place when they changed -- a tool call that settled, a
// spawn that landed. Rows `existing` has never seen are appended in order.
// Rows only `existing` has are kept: `fresh` is a window, and a row falling
// out of the window is not the server retracting it. The one case that
// resets is a `fresh` that shares nothing with a non-empty `existing`: two
// histories with no common row cannot be spliced, and the reader is shown
// the server's.
inline ReconcileOutcome reconcile_transcript(
    std::vector<api::Message>& existing, std::vector<api::Message> fresh) {
    ReconcileOutcome out;
    if (existing.empty()) {
        existing = std::move(fresh);
        out.kind = ReconcileOutcome::Kind::Reset;
        out.count = existing.size();
        return out;
    }
    std::unordered_map<std::string, std::size_t> at;
    at.reserve(existing.size());
    for (std::size_t i = 0; i < existing.size(); ++i)
        if (!existing[i].id.empty()) at.emplace(existing[i].id, i);

    bool overlap = false;
    for (const api::Message& f : fresh)
        if (!f.id.empty() && at.count(f.id) != 0) {
            overlap = true;
            break;
        }
    if (!overlap) {
        existing = std::move(fresh);
        out.kind = ReconcileOutcome::Kind::Reset;
        out.count = existing.size();
        return out;
    }

    std::size_t updatedLo = existing.size();
    std::size_t updatedHi = 0;
    const std::size_t tailStart = existing.size();
    bool reshaped = false;
    const auto reindex = [&] {
        at.clear();
        for (std::size_t i = 0; i < existing.size(); ++i)
            if (!existing[i].id.empty()) at.emplace(existing[i].id, i);
    };
    for (api::Message& f : fresh) {
        auto hit = f.id.empty() ? at.end() : at.find(f.id);
        if (hit == at.end()) {
            if (api::elements::is_element_row(f)) {
                const api::elements::FoldOutcome folded =
                    api::elements::fold_element_row(existing, f);
                if (folded.index + 1 != existing.size()) reshaped = true;
                reindex();
                continue;
            }
            existing.push_back(std::move(f));
            continue;
        }
        if (api::elements::is_element_row(f) &&
            api::elements::is_element_row(existing[hit->second])) {
            const api::elements::FoldOutcome folded =
                api::elements::fold_element_row(existing, f);
            if (!folded.changed()) continue;
            if (folded.moved()) {
                reshaped = true;
                reindex();
            }
            updatedLo = std::min(updatedLo, folded.index);
            updatedHi = std::max(updatedHi, folded.index + 1);
            continue;
        }
        api::Message& mine = existing[hit->second];
        if (same_row(mine, f)) continue;
        // A locally minted row keeps its sync mark; the server's copy of the
        // same row does not know it was ever local.
        const api::SyncState sync = mine.sync;
        const std::string local_id = mine.local_id;
        const api::Message before = mine;
        mine = std::move(f);
        mine.sync = sync;
        mine.local_id = local_id;
        carry_artifact_state(before, mine);
        updatedLo = std::min(updatedLo, hit->second);
        updatedHi = std::max(updatedHi, hit->second + 1);
    }
    if (reshaped) {
        out.kind = ReconcileOutcome::Kind::Reset;
        out.count = existing.size();
        return out;
    }
    if (existing.size() > tailStart) {
        out.kind = ReconcileOutcome::Kind::Appended;
        out.first = tailStart;
        out.count = existing.size() - tailStart;
        return out;
    }
    if (updatedHi > updatedLo) {
        out.kind = ReconcileOutcome::Kind::Updated;
        out.first = updatedLo;
        out.count = updatedHi - updatedLo;
        return out;
    }
    return out;
}

}  // namespace ecs::model
