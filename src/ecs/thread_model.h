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

// ---- The row's state mark -------------------------------------------------
//
// EVERY session row opens with exactly one mark, and it is not decoration: it
// is the whole of what the list says about that thread's run. Two axes carry
// it — a SHAPE and a TONE — because a status read by colour alone is a status
// half the readers cannot read.
//
// The rule is Puffin's, and it is worth stating as a sentence before it is
// stated as code: THE RUN OWNS THE SLOT. Whatever the thread is doing wins
// the mark; the fold chevron — the only thing here that is about the row
// rather than the run — takes the slot only when the run has nothing left to
// say. Puffin's own source puts it the same way one axis down, where the mark
// was still only a colour: "A live status always wins: attention, awaiting and
// working are what the dot exists to say... Only `idle` — nothing to report —
// reaches for the thread's own colour" (`Views/TabStatus.swift`,
// `rowColor(threadAccent:)`), and `SessionRowView` gives the chevron to
// `childCount > 0` rows only. The reference capture applies that same
// precedence to the SHAPE, which is what this reproduces:
//
//   failed, and the agent said so   -> Cross    alert   (the run died)
//   failed, nothing testified       -> Dot      alert   (only an outcome)
//   wants you (blocked / review)    -> Bang     live    (a decision is owed)
//   a run is live right now         -> Arc      live    (and it spins)
//   working, but no live run        -> Dot      live    (last word, not a run)
//   settled, has sub-agents         -> Chevron  calm    (nothing to say; opens)
//   settled                         -> Dot      calm
//
// Why blocked and review share one mark: they are one question — "does this
// need me?" — and the reference draws them identically, same shape and same
// colour, on all six of its waiting rows. hanabi used to spend a shape on the
// difference and spend the CROSS on it, which said "this run died" about a
// thread that was only waiting for an answer.
//
// Why failure splits by state: the tag says what happened, the state says
// whether the thread itself reported it. `Attention + Failed` is the agent
// testifying that it failed; `Unknown + Failed` is a run that ended badly and
// never said a word, which is a weaker fact and draws the weaker shape. That
// is the same split the reference draws (a cross on the two rows whose agent
// reported the failure, a plain red dot on the diff-gate row that only has an
// outcome), and it needs no new state to say it.
enum class Glyph { Dot, Arc, Bang, Cross, Chevron };

// What the mark is saying, independent of its shape. Kept separate from the
// palette on purpose: this layer is graphics-free, so it names the MEANING and
// the renderer owns the three colours.
//   Live  — the run is alive or the thread wants you (the accent)
//   Alert — the run failed (the danger colour)
//   Calm  — nothing to report (muted)
enum class Tone { Live, Alert, Calm };

struct Mark {
    Glyph shape = Glyph::Dot;
    Tone tone = Tone::Calm;

    bool operator==(const Mark&) const = default;
};

inline Mark mark_for(const api::SessionSummary& s) {
    // Failure first: a dead run is not a pending decision, however the row is
    // otherwise tagged.
    if (s.tag == api::ThreadTag::Failed)
        return {s.state == api::ThreadState::Attention ? Glyph::Cross
                                                       : Glyph::Dot,
                Tone::Alert};
    // Then anything asking for the reader. `Attention` covers the http
    // adapter's title-derived needs-you rows, which carry no tag at all, and
    // `Ready` covers the agentcloud adapter's agent-verified ones.
    if (s.tag == api::ThreadTag::Blocked || s.tag == api::ThreadTag::Review ||
        s.state == api::ThreadState::Attention ||
        s.state == api::ThreadState::Ready)
        return {Glyph::Bang, Tone::Live};
    if (s.state == api::ThreadState::Running) return {Glyph::Arc, Tone::Live};
    if (s.state == api::ThreadState::Working) return {Glyph::Dot, Tone::Live};
    // Settled: the only case where the slot is free for the fold chevron. A
    // childless settled row keeps the dot, so the chevron still means "this
    // one opens" — give every settled row a chevron and a collapsed parent is
    // indistinguishable from a leaf until you click it.
    return {s.sub_agent_count > 0 ? Glyph::Chevron : Glyph::Dot, Tone::Calm};
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
    // A failed thread rides with Blocked rather than getting a shelf of its
    // own: it is still a thread that stopped and wants a person. Puffin's
    // shelf carves the same way — `case .blocked: return live.filter { kind
    // == .blocked || kind == .failed }` in `HomeSessionList.swift` — and the
    // reference's Blocked badge counts six, which is only true if the two
    // failed rows are in it.
    return s.tag == api::ThreadTag::Blocked || s.tag == api::ThreadTag::Failed;
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
