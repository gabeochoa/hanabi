#pragma once
// ---------------------------------------------------------------------------
// A parent's children, under it, in the list.
//
// The reference nests a thread's sub-agents beneath it and lets the row's
// leading glyph fold them away (SessionRowView.leadingGlyph: with children,
// an attention state gives a marked fold, `unseen`/`idle` give a plain
// chevron -- so the glyph either says what the thread wants or says how to
// fold it, never both). hanabi had the children somewhere else entirely: a
// separate sidebar MODE with its own list, which answers "show me every
// sub-agent" and not "what is under this thread".
//
// This is the model half, kept pure so the decisions are testable without a
// window: who is whose child, whether a row folds, and what its glyph slot
// is for. The rendering reads these and nothing else.
//
// Membership comes from the sub-agent catalog the app already keeps
// (AppComponent::subagentSessions, each carrying `parent_id`), so nothing
// new is fetched and no pass over the main catalog is added: the index is
// rebuilt only when that catalog's revision changes.

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../api/types.h"

namespace ecs::model {

// What a row's leading glyph is FOR, once children are in the picture.
enum class RowGlyphRole {
    // No children: the glyph says what the thread is doing, as it always did.
    Status,
    // Children, and the thread wants something: the glyph keeps its mark and
    // folds as well, because a parent that cannot be seen to fold is worse
    // than a mark that shares its slot.
    MarkedFold,
    // Children and nothing urgent: the glyph is the fold, plain.
    Fold,
};

[[nodiscard]] inline RowGlyphRole row_glyph_role(bool hasChildren,
                                                 bool wantsAttention) {
    if (!hasChildren) return RowGlyphRole::Status;
    return wantsAttention ? RowGlyphRole::MarkedFold : RowGlyphRole::Fold;
}

// The children of each parent, in catalog order, rebuilt only when the
// sub-agent catalog changes.
class ChildIndex {
   public:
    // `revision` is any value that changes when `children` does -- the app
    // bumps one on every replace_subagents.
    void sync(const std::vector<api::SessionSummary>& children,
              std::uint64_t revision) {
        if (revision == revision_ && !byParent_.empty()) return;
        revision_ = revision;
        byParent_.clear();
        for (const auto& child : children) {
            if (child.parent_id.empty()) continue;
            byParent_[child.parent_id].push_back(&child);
        }
        ++rebuilds_;
    }

    [[nodiscard]] const std::vector<const api::SessionSummary*>* find(
        const std::string& parentId) const {
        const auto it = byParent_.find(parentId);
        return it == byParent_.end() ? nullptr : &it->second;
    }

    [[nodiscard]] std::size_t count_for(const std::string& parentId) const {
        const auto* kids = find(parentId);
        return kids == nullptr ? 0u : kids->size();
    }

    [[nodiscard]] std::size_t rebuilds() const { return rebuilds_; }
    void clear() {
        byParent_.clear();
        revision_ = 0;
        rebuilds_ = 0;
    }

   private:
    std::unordered_map<std::string, std::vector<const api::SessionSummary*>>
        byParent_;
    std::uint64_t revision_ = 0;
    std::size_t rebuilds_ = 0;
};

// Whether a parent's children are showing. Expansion is remembered by the
// PARENT'S ID, not by its position: a list that reorders under the reader --
// and this one reorders on every new message -- must not fold a different
// thread than the one they opened.
[[nodiscard]] inline bool children_shown(
    const std::vector<std::string>& expanded, const std::string& parentId) {
    for (const auto& id : expanded)
        if (id == parentId) return true;
    return false;
}

inline void toggle_children(std::vector<std::string>& expanded,
                            const std::string& parentId) {
    for (std::size_t i = 0; i < expanded.size(); ++i)
        if (expanded[i] == parentId) {
            expanded.erase(expanded.begin() + static_cast<long>(i));
            return;
        }
    expanded.push_back(parentId);
}

// One entry of the list AS DRAWN: a parent, or one of its children while the
// parent is unfolded. Everything that reasons about rows -- the virtual
// window, the spacers, the scroll extent, where a drop lands -- reads THIS
// sequence, so a child is a row to all of them at once rather than a
// surprise to each in turn.
struct VisibleRow {
    const api::SessionSummary* s = nullptr;
    bool child = false;
    // For a parent: its index in the folder's member order (the thing a
    // drag reorders). For a child: the parent's.
    int parentIndex = 0;
};

// Parents [0, limit) with their unfolded children beneath them. A child
// that is ALSO a member of this folder (a sub-agent the catalog lists in
// its own right) is drawn once, as the member, never twice. O(limit +
// children of unfolded parents); the caller caches by revision.
inline void flatten_visible(
    const std::vector<const api::SessionSummary*>& members, int limit,
    const ChildIndex& index, const std::vector<std::string>& expanded,
    std::vector<VisibleRow>& out) {
    out.clear();
    if (limit > static_cast<int>(members.size()))
        limit = static_cast<int>(members.size());
    std::unordered_map<std::string_view, bool> memberIds;
    memberIds.reserve(static_cast<std::size_t>(limit));
    for (int i = 0; i < limit; ++i) memberIds.emplace(members[i]->id, true);
    for (int i = 0; i < limit; ++i) {
        const api::SessionSummary* p = members[static_cast<std::size_t>(i)];
        out.push_back(VisibleRow{p, false, i});
        if (!children_shown(expanded, p->id)) continue;
        const auto* kids = index.find(p->id);
        if (kids == nullptr) continue;
        for (const api::SessionSummary* c : *kids) {
            if (memberIds.count(c->id)) continue;
            out.push_back(VisibleRow{c, true, i});
        }
    }
}

// A drop between two VISIBLE rows, as an index into the PARENT order. Rows
// above the gap that are parents count; children do not -- they are not
// members, and a folder is never reordered by a child's id. A gap inside a
// parent's open children lands after that parent.
[[nodiscard]] inline int parent_drop_index(const std::vector<VisibleRow>& visible,
                                           int visualGap) {
    if (visualGap < 0) visualGap = 0;
    if (visualGap > static_cast<int>(visible.size()))
        visualGap = static_cast<int>(visible.size());
    int parents = 0;
    for (int i = 0; i < visualGap; ++i)
        if (!visible[static_cast<std::size_t>(i)].child) ++parents;
    return parents;
}

}  // namespace ecs::model
