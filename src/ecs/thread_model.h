#pragma once

// Pure, graphics-free thread-model decision logic.
//
// This centralizes the HIGH-SIGNAL decisions the UI makes about a thread so
// they can be (a) shared by the render systems and (b) unit/e2e tested
// headlessly with no window, no UIContext, and no graphics backend. Nothing
// here draws — it only classifies. The sidebar/main-pane systems delegate to
// these so the tested logic IS the shipped logic (no duplicated copy).

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include "../api/types.h"

namespace ecs::model {

// The single notion the UI uses to decide whether a row "shouts". Only the
// Attention state earns a dot+bold in the sidebar.
inline bool is_attention(api::ThreadState s) {
    return s == api::ThreadState::Attention;
}

// Shape-per-status glyph shown at the left of an attention-worthy sidebar row.
// Status is readable by SHAPE, not color alone:
//   Blocked / needs-you  -> Triangle (red,   most urgent)
//   Review (agent-verified) -> Diamond (green)
//   Done                 -> Dot      (blue)
//   working / parked / archived / calm -> None
enum class Glyph { None, Triangle, Diamond, Dot };

// Precedence mirrors the design mock's ordering: blocked, then review, then
// done, then a bare Attention state (waiting-on-you) also earns the triangle.
inline Glyph glyph_for(const api::SessionSummary& s) {
    if (s.tag == api::ThreadTag::Blocked) return Glyph::Triangle;
    if (s.tag == api::ThreadTag::Review) return Glyph::Diamond;
    if (s.tag == api::ThreadTag::Done) return Glyph::Dot;
    if (s.state == api::ThreadState::Attention) return Glyph::Triangle;
    return Glyph::None;
}

// ---- Sub-agent count, as the row draws it ---------------------------------
//
// Semantics are Puffin's (`ChildActivity.label`), deliberately, because the
// two clients are read side by side and a row that means a different thing in
// each is worse than a row that shows nothing:
//
//   no children            -> "" (the column is not drawn at all)
//   all children live      -> "3"    in the live colour
//   some children live     -> "1/3"  in the live colour
//   no child live          -> "3"    in the settled colour
//
// The denominator earns its place only in the mixed case: when every child is
// working, "3/3" says nothing "3" did not. A bare number is therefore
// ambiguous between "all live" and "none live" BY DESIGN — the colour, not
// the digits, carries that, which is what keeps the column narrow enough to
// sit on a crowded row.
inline std::string sub_agent_label(const api::SessionSummary& s) {
    if (s.sub_agent_count <= 0) return "";
    const int total = s.sub_agent_count;
    const int live = s.sub_agent_running_count;
    if (live > 0 && live < total)
        return std::to_string(live) + "/" + std::to_string(total);
    return std::to_string(total);
}

// Whether the count should read as live (some child is still working) rather
// than settled. Drives the colour, which is the only thing distinguishing the
// two bare-number cases above.
inline bool sub_agents_live(const api::SessionSummary& s) {
    return s.sub_agent_count > 0 && s.sub_agent_running_count > 0;
}

// ---- Smart-view membership predicates ----
// Home is a digest (not a simple filter) so it has no single predicate; the
// three filterable smart views do:
inline bool in_blocked_view(const api::SessionSummary& s) {
    return s.tag == api::ThreadTag::Blocked;
}
inline bool in_review_view(const api::SessionSummary& s) {
    return s.state == api::ThreadState::Ready;  // agent-verified
}
inline bool in_starred_view(const api::SessionSummary& s) {
    return s.starred;
}
// Archived-ness as the whole app should ask it: the user's machine-local
// overlay when they have expressed one, the backend's own state otherwise.
inline bool is_archived(const api::SessionSummary& s) {
    return s.archive_override.value_or(s.state == api::ThreadState::Archived);
}
inline bool in_archived_view(const api::SessionSummary& s) {
    return is_archived(s);
}

// ---- Manual sidebar row order (drag-to-reorder) ---------------------------
//
// The sidebar list is sorted by ACTIVITY — newest-touched first, recomputed
// every refresh. Dragging a row is therefore NOT a re-sort: there is no key to
// sort by. It is a second, standing statement about a handful of rows:
//
//   A manual order is a PINNED PREFIX of one folder. The rows the user has
//   arranged sit at the top of that folder in the order they were left in;
//   every row never touched keeps flowing underneath in activity order, and a
//   pinned row does not lose its place when something else gets busy.
//
// The prefix is capped so the concept stays a hand-curated shelf rather than a
// shadow copy of the list: a folder holding thousands of sessions persists at
// most kRowOrderMax ids, its rank map is that size, and each member costs one
// hash probe. Rows past the cap are not draggable — see the sidebar's drag
// gate — so the feature never silently declines to move something.
inline constexpr size_t kRowOrderMax = 64;

// Reorder `members` in place so the ids named in `order` come first, in that
// order, and everything else keeps the incoming (activity) order behind them.
//
// Deliberately NOT a comparison sort of the whole vector: a partition is one
// linear pass plus a sort of the pinned head, which is bounded by kRowOrderMax
// however long the member list is.
inline void apply_row_order(std::vector<const api::SessionSummary*>& members,
                            const std::vector<std::string>& order) {
    if (order.empty() || members.size() < 2) return;
    std::unordered_map<std::string, size_t> rank;
    rank.reserve(order.size());
    for (size_t i = 0; i < order.size(); ++i) rank.emplace(order[i], i);

    auto pinnedEnd = std::stable_partition(
        members.begin(), members.end(),
        [&](const api::SessionSummary* s) { return rank.count(s->id) != 0; });
    std::sort(members.begin(), pinnedEnd,
              [&](const api::SessionSummary* a, const api::SessionSummary* b) {
                  return rank[a->id] < rank[b->id];
              });
}

// Which slot of a rendered row band the cursor is over. The band is contiguous
// (every row is the same height, no margin between them), so the answer is a
// subtraction rather than a hit test against each row — which is what keeps
// the gesture O(1) in a folder holding thousands of rows. Mirrors the tab
// strip's compute_drop_index one pane over.
inline size_t compute_row_drop_index(float cursorY, float firstRowY,
                                     float rowH, size_t count) {
    if (count <= 1 || rowH <= 0.0f) return 0;
    const float rel = (cursorY - firstRowY) / rowH;
    long idx = static_cast<long>(std::floor(rel));
    if (idx < 0) idx = 0;
    if (idx > static_cast<long>(count) - 1) idx = static_cast<long>(count) - 1;
    return static_cast<size_t>(idx);
}

// The folder's new pinned prefix after `movedId` is dropped at `toIndex` among
// `visible` (the group's rows in the order they are currently rendered).
// Truncated to kRowOrderMax, so what gets persisted is bounded no matter how
// many rows the group has.
inline std::vector<std::string> reorder_rows(
    const std::vector<std::string>& visible, const std::string& movedId,
    size_t toIndex) {
    // A row that is not one of these leaves the order alone. Asked up front,
    // because the truncation below can stop before ever reaching it.
    if (std::find(visible.begin(), visible.end(), movedId) == visible.end())
        return visible;
    std::vector<std::string> out;
    out.reserve(std::min(visible.size(), kRowOrderMax));
    for (const auto& id : visible) {
        if (id == movedId) continue;
        if (out.size() >= kRowOrderMax - 1) break;
        out.push_back(id);
    }
    out.insert(out.begin() +
                   static_cast<long>(std::min(toIndex, out.size())),
               movedId);
    return out;
}

}  // namespace ecs::model
