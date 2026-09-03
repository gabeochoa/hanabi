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

// ---- The status glyph, for EVERY surface that shows one -------------------
//
// One function, because "the sidebar and the tab bar disagree about this
// thread" is a bug the reader cannot resolve.
//
// The four-glyph vocabulary is deliberate (Reduce sidebar statuses to four
// glyphs) and is not reopened here. What grows is the set of MEANINGS the wire
// carries and hanabi dropped -- a freeze, a pause, and the half of `waiting`
// that is not a review.
//
// PRECEDENCE, top to bottom. Frozen leads because nothing the reader does
// changes it and it can be true of a running thread at the same time; Paused
// sits above Idle only, because a thread that is blocked AND not answering is
// still blocked first. Puffin ranks `paused` above `working` (IconTable's
// declaration order); hanabi ranks it below, because its Running glyph covers
// both of Puffin's `working` and its dial, and demoting a live run to a pause
// mark would lose the run.
enum class StatusGlyph { Running, Blocked, Waiting, Done, Idle, Frozen, Paused };

inline StatusGlyph status_glyph(const api::SessionSummary& s) {
    if (s.frozen) return StatusGlyph::Frozen;
    if (s.tag == api::ThreadTag::Waiting) return StatusGlyph::Waiting;
    if (s.tag == api::ThreadTag::Blocked || s.tag == api::ThreadTag::Failed ||
        s.state == api::ThreadState::Attention)
        return StatusGlyph::Blocked;
    if (s.state == api::ThreadState::Running ||
        s.state == api::ThreadState::Working)
        return StatusGlyph::Running;
    if (s.tag == api::ThreadTag::Done || s.tag == api::ThreadTag::Review ||
        s.state == api::ThreadState::Ready)
        return StatusGlyph::Done;
    if (s.replies_paused) return StatusGlyph::Paused;
    return StatusGlyph::Idle;
}

// Whether a TAB draws this status at all. An IDLE tab draws nothing -- and
// only an idle one: a settled thread can still be Done, which wears the tick.
// Idle is "nothing to report", which an empty gutter already says, and a mark
// on every tab hides the ones that mean something.
// Puffin's IconTable returns nil on `IconSurface.tab` for `idle` and `unseen`;
// hanabi has no `unseen` glyph, so `Idle` is the whole of the rule here.
inline bool status_shows_on_tab(StatusGlyph g) {
    return g != StatusGlyph::Idle;
}

// What the mark means, in words -- the accessible name every surface gives it.
inline std::string_view status_label(StatusGlyph g) {
    switch (g) {
        case StatusGlyph::Running: return "Working";
        case StatusGlyph::Blocked: return "Blocked";
        case StatusGlyph::Waiting: return "Waiting on you";
        case StatusGlyph::Done: return "Ready for review";
        case StatusGlyph::Frozen: return "Frozen";
        case StatusGlyph::Paused: return "Replies paused";
        case StatusGlyph::Idle: return "Nothing to report";
    }
    return "";
}

// ---- Brakes, as the composer has to say them ------------------------------
//
// Two things stop a thread that its run's own state does not describe, and
// they are not the same brake:
//
//   FROZEN  — held by the server over a containment chain. Nothing the reader
//             does clears it and no message they send will be answered, so the
//             composer refuses input rather than accepting a message that goes
//             nowhere. Rides the catalog row, so the list marks it too.
//   HALTED  — no new run will start until someone resumes it. Input is still
//             worth taking (it queues), so this warns and does not refuse.
//             Attach-only, so the composer is the only surface that can say it.
//
// `by` on either brake is the SESSION at the root of it, never a person: when
// it names something other than this thread the brake was inherited, and the
// line says so rather than printing an id where a name belongs.
struct Brake {
    bool engaged = false;
    bool refuses_input = false;
    std::string caption;

    bool operator==(const Brake&) const = default;
};

inline Brake brake_for(const std::string& id, const api::SessionSummary* sum,
                       const api::Session* open) {
    // `by` is a SESSION ID, never a person and never a name -- so it is not
    // printed. What the reader needs from it is one bit: is this thread's own
    // brake, or one it inherited.
    const auto line = [&](const std::string& head, const std::string& by,
                          const std::string& reason,
                          const std::string& fallback) {
        std::string out = head;
        if (!by.empty() && by != id) out += " by an ancestor thread";
        const std::string& tail = reason.empty() ? fallback : reason;
        if (!tail.empty()) out += " \xe2\x80\x94 " + tail;
        return out;
    };
    // Frozen first: it is the stronger of the two and the one that changes
    // what the composer will accept. A thread carrying both says the stronger.
    if (sum != nullptr && sum->frozen)
        return {true, true,
                line("Frozen", sum->frozen_by, sum->frozen_reason, "")};
    if (open != nullptr && open->halt_engaged())
        return {true, false,
                line("Halted", open->halted_by, open->halted_reason,
                     "no run will start until it is resumed")};
    return {};
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
    //
    // Waiting rides here too, and it MUST: before the mark told the two
    // apart, a `waiting` thread was tagged Blocked and counted here. Splitting
    // the tag without this line would quietly drop those threads out of the
    // only view that collects the ones needing an answer.
    return s.tag == api::ThreadTag::Blocked ||
           s.tag == api::ThreadTag::Waiting || s.tag == api::ThreadTag::Failed;
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
    // The server's own stamp counts as archived-ness the same way the state
    // word does, which is what makes a thread archived from the web or from
    // another Mac read as archived here. The local override still wins when
    // the user has expressed one -- it is the only thing that can say NOT
    // archived about a row the server has stamped, so Unarchive still means
    // something.
    return s.archive_override.value_or(s.state == api::ThreadState::Archived ||
                                       s.server_archived_at_ms > 0);
}
inline bool in_archived_view(const api::SessionSummary& s) {
    return is_archived(s);
}

// The order the sidebar list is in, and it is TWO statements, not one.
//
// The first is the pin. `docs/sidebar-model.md` has said since the model was
// written that Starred is "user-pinned, pinned to top", and for the whole of
// that time nothing consulted `starred` when ordering: a pinned thread sat
// wherever its last activity put it, wearing a 12px star, and Gabe's "pinned
// threads arent there" is that. There are two things in this file called
// pinned -- this flag, and apply_row_order's PINNED PREFIX below, which is the
// drag order -- and the one that has a sort is not the one the user pins.
//
// The second is activity, newest first. The id tie-break is not cosmetic:
// `updated_at` is a whole number of seconds and a real catalog has plenty of
// rows sharing one, so without it the order among tied rows is whatever the
// sort algorithm happens to leave, and the list can reshuffle for no reason a
// person can see.
//
// Written as ONE comparator rather than a partition after the sort, and the
// reason is the sidebar's partial_sort: only the first `limit` members are
// ordered and the tail is left arbitrary, so a partition that pulled starred
// rows out of that tail would order them by nothing at all. A comparator is a
// total order over the whole vector however far the sort runs.
inline bool sidebar_before(const api::SessionSummary* a,
                           const api::SessionSummary* b) {
    if (a->starred != b->starred) return a->starred;
    if (a->updated_at != b->updated_at) return a->updated_at > b->updated_at;
    return a->id < b->id;
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
