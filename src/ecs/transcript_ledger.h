#pragma once

// ---------------------------------------------------------------------------
// The transcript's layout ledger: one scalar row per message, a prefix index
// over their heights, and the reader's place expressed as a row and an offset.
//
// WHY. The transcript used to keep a vector of measured items and rebuild it
// from message 0 whenever a "global fact" changed -- the pane width first
// among them. Every applied size of a live window resize therefore visited
// every message in the thread, asked its height (a cache hit, mostly), summed
// the heights, walked them again to emit spacers, and walked them a third
// time for the minimap. The text measure was cached; the SHELL around the
// cache was O(N) per frame, and on a long thread that is the freeze.
//
// THE MODEL. A row is a message. Its geometry is three scalars -- a divider
// above it (`before`), its body (`h`), a run-outcome line below it (`after`)
// -- and whether it is hidden (a tool-pile follower, whose lead row spans it;
// a thinking row while reasoning is off). Row kinds, spans, dividers and
// author rows are LOCAL facts of (messages[i-1], messages[i], i == n-1) plus
// one global int (the first unread), so a mutation classifies only the rows
// it touched. Heights live in a Fenwick tree: the y of any row, the row at any
// y and the total are O(log N), and a corrected height is one point update.
//
// EXACT OR ESTIMATED, PER ROW. A row is exact when it was measured under the
// current epoch (font, fold mode, dividers, reasoning) at a column width its
// wrap interval still holds. Anything else is an ESTIMATE -- the last exact
// height when there is one, a kind constant when there is not -- and an
// estimate is never built or hit-tested: it only positions content nobody can
// see. A width change bumps nothing eagerly; a row is re-measured when it
// enters the window, and the delta goes into the tree.
//
// THE ANCHOR. Where the reader is, as (message id, row, pixel offset): the
// viewport's top edge sits `offset` below the top of `row`. The scroll offset
// is DERIVED from the anchor after the window is measured, so a correction
// above the anchor moves the spacer above it and not the reader. The id is the
// identity; the row is a cache of where the id was, re-resolved through the
// id map on every prepend or reset, and the fallback when the id is gone is
// the row clamped into the list.
//
// Pure: no afterhours, no api types. Messages reach it through callbacks
// (classify, measure, id_of), so a test drives it with integers and a fake
// ruler. The transcript system owns one ledger per pane slot.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <deque>
#include <cstdint>
#include <limits>
#include <list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ecs::model {

// What changed in a pane's message list since the last frame, noted by the
// code that changed it (Pane::note_transcript_*). The ledger reads the kind
// and range to classify only the rows the change touched.
enum class TranscriptMutationKind {
    Reset,
    Append,
    Prepend,
    Update,
};

struct TranscriptMutation {
    std::uint64_t base_revision = 0;
    std::uint64_t revision = 0;
    TranscriptMutationKind kind = TranscriptMutationKind::Reset;
    std::size_t first = 0;
    std::size_t count = 0;
};

// The recent mutations, oldest first, so a consumer that last saw revision r
// can apply everything after r in order: two notes in one frame (an append
// and the update of the row it appended, every streaming token) are two
// steps, not a lost base revision and a rebuild. Bounded; a consumer whose
// revision fell off the front rebuilds.
class TranscriptMutationLog {
  public:
    static constexpr std::size_t kKeep = 32;
    void push(const TranscriptMutation& m) {
        log_.push_back(m);
        if (log_.size() > kKeep) log_.pop_front();
    }
    const std::deque<TranscriptMutation>& entries() const { return log_; }
    std::uint64_t revision() const { return log_.empty() ? 0 : log_.back().revision; }
    void clear() { log_.clear(); }

  private:
    std::deque<TranscriptMutation> log_;
};

// Prefix sums over row heights. Fenwick (binary indexed) tree: O(log N)
// point update, prefix, and descend-to-y; O(N) assign. Heights are kept
// beside the tree so a caller can read one back and so an update is a delta.
class HeightIndex {
  public:
    void assign(const std::vector<float>& heights) {
        n_ = heights.size();
        h_ = heights;
        tree_.assign(n_ + 1, 0.0);
        // Linear build: each node takes its own value and passes it to its
        // parent, O(N) rather than N log N.
        for (std::size_t i = 1; i <= n_; ++i) {
            tree_[i] += static_cast<double>(h_[i - 1]);
            const std::size_t parent = i + (i & (~i + 1));
            if (parent <= n_) tree_[parent] += tree_[i];
        }
    }
    void clear() {
        n_ = 0;
        h_.clear();
        tree_.assign(1, 0.0);
    }
    std::size_t size() const { return n_; }
    float get(std::size_t i) const { return h_[i]; }
    // Replace row i's height; returns the delta applied.
    float set(std::size_t i, float h) {
        const float delta = h - h_[i];
        if (delta == 0.0f) return 0.0f;
        h_[i] = h;
        // The tree accumulates in double: a float tree drifts by an ulp per
        // update and a streaming row is updated every frame for minutes.
        const double dd = static_cast<double>(delta);
        for (std::size_t k = i + 1; k <= n_; k += k & (~k + 1)) tree_[k] += dd;
        return delta;
    }
    // Append one row: O(log N). The new node covers (k - lowbit(k), k].
    void push_back(float h) {
        const std::size_t k = n_ + 1;
        const std::size_t low = k - (k & (~k + 1));
        const double covered = prefix_d(n_) - prefix_d(low);
        h_.push_back(h);
        tree_.push_back(covered + static_cast<double>(h));
        n_ = k;
    }
    // Sum of heights of rows [0, i).
    float prefix(std::size_t i) const { return static_cast<float>(prefix_d(i)); }
    float total() const { return prefix(n_); }
    double total_d() const { return prefix_d(n_); }
    // The row containing y: the largest i with prefix(i) <= y, clamped to
    // [0, n-1]. A zero-height row is skipped over, as it has no pixels.
    std::size_t row_at(float y) const {
        if (n_ == 0) return 0;
        if (y <= 0.0f) return 0;
        std::size_t pos = 0;
        std::size_t step = 1;
        while (step * 2 <= n_) step *= 2;
        double acc = 0.0;
        const double yy = static_cast<double>(y);
        for (; step > 0; step /= 2) {
            const std::size_t next = pos + step;
            if (next <= n_ && acc + tree_[next] <= yy) {
                pos = next;
                acc += tree_[next];
            }
        }
        // pos = count of rows whose cumulative height is <= y; that is the
        // index of the row containing y, unless y is past the end.
        return std::min(pos, n_ - 1);
    }

  private:
    double prefix_d(std::size_t i) const {
        double s = 0.0;
        for (std::size_t k = std::min(i, n_); k > 0; k -= k & (~k + 1)) s += tree_[k];
        return s;
    }
    std::size_t n_ = 0;
    std::vector<float> h_;
    std::vector<double> tree_{0.0};
};

// The scalar geometry of one message row. ~40 bytes.
struct RowGeom {
    enum Kind : std::uint8_t {
        Bubble,
        ToolPile,
        ToolBlock,
        Spawn,
        Thinking,
        Event,
        Delivery,
        Compaction,
        System,
        KindCount
    };
    float before = 0.0f;  // date / new divider above the row
    float h = 0.0f;       // the row's own body
    float after = 0.0f;   // run-outcome line below it
    // Column widths h is exact at, as the measure reported them (a wrap
    // interval translated to column units; -inf..+inf when width-free).
    float validLo = std::numeric_limits<float>::infinity();
    float validHi = -std::numeric_limits<float>::infinity();
    float measuredW = -1.0f;  // the column width h was measured at
    std::uint32_t epoch = 0;  // ledger epoch h was measured under
    std::uint32_t span = 1;   // a pile lead spans [row, row + span)
    // Hidden-run navigation, kept on both ends so neither direction steps
    // through a run one row at a time: a hidden row knows how far back the
    // visible row before it is (or row+1 when there is none); a visible row
    // knows how many hidden rows follow it. A pile's followers and a run of
    // hidden thinking rows are the same shape to this.
    std::uint32_t leadDistance = 0;
    std::uint32_t hiddenAfter = 0;
    Kind kind = Bubble;
    bool exact = false;
    bool hidden = false;      // follower of a pile, or a hidden kind: 0 px
    bool showAuthor = true;
    bool live = false;        // the streaming row: never exact
    bool everMeasured = false;
    bool counted = false;     // its kind is in the ledger's kind counts
    std::uint32_t measuredPass = 0;  // the materialize pass that last measured it
    float hint = -1.0f;       // the caller's estimate hint, kept for recalibration

    float total() const { return hidden ? 0.0f : before + h + after; }
};

// What classify() reports for one row: the width-free facts.
struct RowClass {
    RowGeom::Kind kind = RowGeom::Bubble;
    std::uint32_t span = 1;
    bool hidden = false;
    bool showAuthor = true;
    bool live = false;
    float before = 0.0f;
    float after = 0.0f;
    // A cheap guess at the body height for a never-measured row (from the
    // text length, say); negative = use the kind constant.
    float estimate = -1.0f;
};

// What measure() reports: the body height and where it holds.
struct RowMeasure {
    float h = 0.0f;
    float validLo = -std::numeric_limits<float>::infinity();
    float validHi = std::numeric_limits<float>::infinity();
};

struct LedgerFacts {
    float column_w = 0.0f;
    bool show_date_dividers = false;
    bool show_reasoning = false;
    bool fold_long_messages = false;
    int tool_fold_mode = 0;
    int unread_first = -1;
    std::string find_query;
    bool find_open = false;
    unsigned font_epoch = 0;
    bool streaming = false;
    std::size_t live_index = 0;
    int stream_phase = 0;
};

struct Anchor {
    std::string id;       // message id; empty = none
    std::size_t row = 0;  // where the id was last seen
    float offset = 0.0f;  // viewport top = top_of(row) + offset ...
    // ... or, when `bottom`, viewport bottom = bottom_of(row) + offset: the
    // follow anchor, resolved AFTER the row's height is measured this frame,
    // so a token that grows the live row keeps its bottom on the edge.
    bool bottom = false;
};

struct LedgerWindow {
    std::size_t first = 0;  // inclusive
    std::size_t last = 0;   // exclusive
    float top = 0.0f;       // y of `first`
    float viewport_top = 0.0f;  // clamped, the same number scroll_for gives
    bool filled = false;    // every row intersecting the viewport is exact
    int passes = 0;
};

struct LedgerCounters {
    std::size_t rows_classified = 0;
    std::size_t rows_measured = 0;
    std::size_t rows_visited = 0;
    std::size_t index_updates = 0;
    std::size_t reindexed = 0;  // O(N) events: reset, prepend, a toggle
    std::size_t recalibrated = 0;  // O(N) estimate rescales, on those frames
    std::size_t unfilled = 0;   // materialize ended with a visible estimate
    void reset() { *this = LedgerCounters{}; }
};

class TranscriptLedger {
  public:
    // Bring the ledger up to date with the message list. `classify(row)`
    // returns the width-free facts of row `row` (kind, span, dividers, author
    // row, live). `id_of(row)` returns the message id. Only the rows a
    // mutation touched are classified; a global-fact change bumps the epoch.
    template <class Classify, class IdOf>
    void sync(std::size_t message_count, const TranscriptMutationLog& log,
              const LedgerFacts& facts, Classify&& classify, IdOf&& id_of) {
        const bool first = !initialized_;
        bool reset = first;
        lastShift_ = 0;
        if (!first && log.revision() != revision_) {
            // Every step after the revision this ledger last saw, in order.
            const auto& entries = log.entries();
            std::size_t k = 0;
            while (k < entries.size() && entries[k].base_revision != revision_) ++k;
            if (k == entries.size()) {
                reset = true;  // history fell off the log
            } else {
                for (; k < entries.size() && !reset; ++k) {
                    const TranscriptMutation& m = entries[k];
                    if (m.base_revision != revision_) { reset = true; break; }
                    switch (m.kind) {
                        case TranscriptMutationKind::Reset: reset = true; break;
                        case TranscriptMutationKind::Prepend:
                            prepend(m.count, rows_.size() + m.count, classify, id_of);
                            break;
                        case TranscriptMutationKind::Append:
                            append(std::min(message_count, m.first + m.count), classify, id_of);
                            break;
                        case TranscriptMutationKind::Update:
                            update(m.first, m.count, classify, id_of);
                            break;
                    }
                    revision_ = m.revision;
                }
            }
        }
        if (!reset && message_count != rows_.size()) reset = true;  // the notes and the list disagree
        if (reset) rebuild(message_count, classify, id_of);
        revision_ = log.revision();
        apply_facts(facts, classify);
        initialized_ = true;
    }
    // One mutation, for callers and tests that have no log.
    template <class Classify, class IdOf>
    void sync(std::size_t message_count, const TranscriptMutation& mutation,
              const LedgerFacts& facts, Classify&& classify, IdOf&& id_of) {
        TranscriptMutationLog one;
        one.push(mutation);
        // A single entry whose base is not what we saw is a rebuild, as before.
        sync(message_count, one, facts, classify, id_of);
    }

    // ---- reading -----------------------------------------------------------
    std::size_t rows() const { return rows_.size(); }
    float total() const { return index_.total(); }
    float top_of(std::size_t row) const { return index_.prefix(row); }
    std::size_t row_at(float y) const { return index_.row_at(y); }
    const RowGeom& geom(std::size_t row) const { return rows_[row]; }
    std::uint32_t epoch() const { return epoch_; }
    const LedgerFacts& facts() const { return facts_; }
    // Visible rows of each kind, kept as rows are classified: what the
    // thread is made of, without a walk to find out.
    std::size_t kind_count(RowGeom::Kind k) const { return kindCount_[k]; }
    std::size_t visible_rows() const { return visibleRows_; }
    LedgerCounters& counters() { return counters_; }
    const LedgerCounters& counters() const { return counters_; }

    // Whether row `row`'s height can be trusted at the current facts.
    bool is_exact(std::size_t row) const {
        const RowGeom& g = rows_[row];
        if (g.hidden) return true;
        if (g.live || !g.exact || g.epoch != epoch_) return false;
        const float w = facts_.column_w;
        return g.measuredW == w || (g.validLo <= w && w < g.validHi);
    }

    // ---- writing -----------------------------------------------------------
    // Forget row `row`'s exactness; its height stays as the estimate.
    void mark_dirty(std::size_t row) {
        if (row >= rows_.size()) return;
        rows_[row].exact = false;
        const RowGeom& g = rows_[row];
        if (g.hidden && g.kind == RowGeom::ToolPile && g.leadDistance <= row)
            rows_[row - g.leadDistance].exact = false;  // the pile's height is the run's
    }
    void mark_all_dirty() { ++epoch_; }

    // Record a measurement for row `row` at the current facts.
    void set_measure(std::size_t row, const RowMeasure& m) {
        ++counters_.rows_visited;
        RowGeom& g = rows_[row];
        g.h = m.h;
        g.validLo = m.validLo;
        g.validHi = m.validHi;
        g.measuredW = facts_.column_w;
        g.epoch = epoch_;
        g.exact = !g.live;
        if (!g.everMeasured) {
            // First measurement of this row: what the hint was worth.
            Calib& c = calib_[g.kind];
            c.sumExact += static_cast<double>(m.h);
            c.sumHint += static_cast<double>(g.hint >= 0.0f ? g.hint : estimate(g.kind));
            ++c.count;
        }
        g.everMeasured = true;
        g.measuredPass = pass_;
        ++counters_.rows_measured;
        commit(row);
    }

    // Re-estimate every never-measured row from what the measured rows of
    // its kind turned out to be. O(N) scalar, no measure: for the frames
    // that already index the whole thread (an open, a load-older), so the
    // rail and the scrollbar are proportioned by the thread's own shape
    // rather than by a cold constant. Counted under reindexed.
    void recalibrate_estimates() {
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            RowGeom& g = rows_[i];
            if (g.everMeasured || g.hidden) continue;
            const float h = calibrated_estimate(g.kind, g.hint);
            if (h != g.h) {
                g.h = h;
                commit(i);
            }
        }
        counters_.recalibrated += rows_.size();
    }
    // Exact, or the live row measured in this materialize pass: what the
    // window may be built from. A live row is re-measured every frame, so
    // "measured this pass" is as good as exact for this frame.
    bool covered(std::size_t row) const {
        if (is_exact(row)) return true;
        const RowGeom& g = rows_[row];
        return g.live && g.everMeasured && g.measuredPass == pass_;
    }

    // ---- anchor -------------------------------------------------------------
    // The visible row containing y: a hidden follower has no pixels, so a y
    // that lands on one belongs to the next visible row, reached by its lead
    // and span in O(1) rather than by stepping.
    std::size_t visible_row_at(float y) const {
        std::size_t r = index_.row_at(std::max(0.0f, y));  // above row 0 is row 0
        r = skip_hidden_forward(r);
        return std::min(r, rows_.empty() ? 0 : rows_.size() - 1);
    }
    std::size_t skip_hidden_forward(std::size_t r) const {
        while (r < rows_.size() && rows_[r].hidden) {
            const RowGeom& g = rows_[r];
            if (g.leadDistance <= r) {
                const std::size_t lead = r - g.leadDistance;
                r = lead + 1 + rows_[lead].hiddenAfter;
            } else {
                ++r;  // hidden from row 0: no visible row before it
            }
        }
        return r;  // may be rows_.size(): past the end
    }
    // The visible row at or before r (r itself when visible); when the run
    // reaches row 0, the first visible row after it instead.
    std::size_t skip_hidden_backward(std::size_t r) const {
        if (r < rows_.size() && rows_[r].hidden) {
            const RowGeom& g = rows_[r];
            if (g.leadDistance <= r) return r - g.leadDistance;
            const std::size_t after = skip_hidden_forward(0);
            return std::min(after, rows_.empty() ? 0 : rows_.size() - 1);
        }
        return r;
    }
    // The row at the viewport's top edge and the offset into it. When that
    // row is still an estimate (a page or a jump landed beyond the measured
    // rows), the anchor is the nearest EXACT row below it instead, with the
    // distance as a negative offset: the rows between are then measured
    // upward from something whose position is known, so the viewport lands
    // exactly where the gesture asked in exact space rather than where an
    // estimate happened to put a row. Bounded search: a jump into deep
    // history finds no exact row nearby and anchors on the estimate.
    template <class IdOf>
    Anchor anchor_at(float scrollY, IdOf&& id_of) const {
        Anchor a;
        if (rows_.empty()) return a;
        a.row = visible_row_at(scrollY);
        if (!is_exact(a.row)) {
            constexpr int kSearch = 64;
            std::size_t r = a.row;
            for (int k = 0; k < kSearch && r < rows_.size(); ++k) {
                if (is_exact(r)) { a.row = r; break; }
                r = next_visible(r);
            }
        }
        a.offset = scrollY - index_.prefix(a.row);
        a.id = id_of(a.row);
        return a;
    }
    // Where `a` is now: by id, else by its remembered row shifted by what the
    // last sync prepended and clamped into the list.
    std::size_t resolve(const Anchor& a) const {
        if (rows_.empty()) return 0;
        if (!a.id.empty()) {
            auto it = indexOf_.find(a.id);
            if (it != indexOf_.end()) return it->second;
        }
        return std::min(a.row + lastShift_, rows_.size() - 1);
    }
    // The viewport top `a` asks for, before clamping.
    float wanted_top(const Anchor& a, std::size_t row, float viewH) const {
        const float top = index_.prefix(row);
        if (a.bottom) return top + rows_[row].total() + a.offset - viewH;
        return top + a.offset;
    }
    // The scroll view's content is the rows plus whatever the caller lays
    // out below them (a trailing pad, a row in flight): `slack_below` is
    // that, so the clamp agrees with the view's own to the pixel.
    void set_slack_below(float px) { slackBelow_ = std::max(0.0f, px); }
    float slack_below() const { return slackBelow_; }
    // ... and above them (a rollup panel at the head of the column): the
    // viewport may sit that far above row 0.
    void set_slack_above(float px) { slackAbove_ = std::max(0.0f, px); }
    float slack_above() const { return slackAbove_; }
    float clamp_top(float y, float viewH) const {
        const float maxY = std::max(-slackAbove_, index_.total() + slackBelow_ - viewH);
        return std::clamp(y, -slackAbove_, maxY);
    }
    // The row the anchor lands on for geometry: a follower has no pixels of
    // its own, so its lead stands for it (the same row materialize uses).
    std::size_t anchor_row(const Anchor& a) const {
        return skip_hidden_backward(resolve(a));
    }
    float scroll_for(const Anchor& a, float viewH) const {
        if (rows_.empty()) return 0.0f;
        return clamp_top(wanted_top(a, anchor_row(a), viewH), viewH);
    }

    // ---- the window ---------------------------------------------------------
    // Materialize the rows the viewport needs. The viewport top is what
    // scroll_for will return -- the anchor's ask, clamped into the content --
    // and every row intersecting [top, top + viewH) is measured until exact
    // before anything is built. Measuring moves the total and so the clamp,
    // so the pass repeats (at most kPasses) until the top settles; `filled`
    // reports whether the final viewport is covered by exact rows, and is
    // false rather than assumed when it is not. Then the overscan above and
    // below is measured until `overscan_px` each side is exact or `budget`
    // rows have been measured. The window returned holds only exact rows;
    // anything past it is spacer at estimated height. Hidden followers are
    // crossed by their lead's span, never one at a time.
    template <class Measure>
    LedgerWindow materialize(const Anchor& anchor, float viewH,
                             float overscan_px, std::size_t budget,
                             Measure&& measure) {
        LedgerWindow w;
        if (rows_.empty()) return w;
        constexpr int kPasses = 4;
        ++pass_;
        const std::size_t aRow = anchor_row(anchor);
        std::size_t lo = aRow;
        std::size_t hi = aRow;
        float top = 0.0f;
        for (int pass = 0; pass < kPasses; ++pass) {
            ++w.passes;
            // A bottom anchor needs its row's height first.
            ensure(aRow, measure);
            const float wanted = wanted_top(anchor, aRow, viewH);
            const float maxTop = std::max(-slackAbove_, index_.total() + slackBelow_ - viewH);
            if (wanted >= maxTop) {
                // Clamped against the end: the viewport is the LAST viewH
                // pixels (the slack below the rows included), whatever they
                // turn out to be. Measure backward from the last row until a
                // viewport is covered by exact rows; then the clamp is exact
                // by construction.
                std::size_t r = skip_hidden_backward(rows_.size() - 1);
                float covered = slackBelow_;
                while (true) {
                    ensure(r, measure);
                    covered += rows_[r].total();
                    lo = std::min(lo, r);
                    if (covered >= viewH || r == 0) break;
                    r = skip_hidden_backward(r - 1);
                }
                hi = rows_.size();
                top = std::max(-slackAbove_, index_.total() + slackBelow_ - viewH);
            } else if (wanted <= -slackAbove_) {
                // Clamped against the start: the viewport is the FIRST viewH
                // pixels (the slack above the rows included). Measure forward
                // from row 0 until covered.
                std::size_t r = skip_hidden_forward(0);
                float covered = slackAbove_;
                while (r < rows_.size() && covered < viewH) {
                    ensure(r, measure);
                    covered += rows_[r].total();
                    r = next_visible(r);
                }
                lo = 0;
                hi = std::max(hi, r);
                top = -slackAbove_;
            } else {
                // Outward from the anchor, in anchor-relative distances, so
                // a row measured above the anchor moves the anchor and the
                // viewport together and changes nothing about which rows are
                // in view. `rel` is the viewport top relative to the anchor
                // row's top (negative: rows above the anchor are visible).
                const float rel = wanted - index_.prefix(aRow);
                // Up: rows above the anchor whose bottom is below the top.
                float stackUp = 0.0f;
                std::size_t r = aRow;
                while (r > 0 && stackUp < -rel) {
                    r = skip_hidden_backward(r - 1);
                    ensure(r, measure);
                    stackUp += rows_[r].total();
                    if (r == 0) break;
                }
                lo = std::min(lo, r);
                // Down: the anchor and the rows below it until the viewport
                // bottom (rel + viewH below the anchor's top) is covered.
                float stackDown = 0.0f;
                r = aRow;
                while (r < rows_.size() && stackDown < rel + viewH) {
                    ensure(r, measure);
                    stackDown += rows_[r].total();
                    r = next_visible(r);
                }
                hi = std::max(hi, r);
                top = std::max(-slackAbove_, wanted_top(anchor, aRow, viewH));
            }
            const float settled = clamp_top(wanted_top(anchor, aRow, viewH), viewH);
            if (std::fabs(settled - top) < 0.01f) {
                top = settled;
                break;
            }
            top = settled;
        }
        // Coverage of the final viewport, checked rather than assumed.
        w.filled = true;
        {
            std::size_t r = visible_row_at(top);
            float y = index_.prefix(r);
            while (r < rows_.size() && y < top + viewH) {
                if (!covered(r)) { w.filled = false; break; }
                y += rows_[r].total();
                r = next_visible(r);
            }
        }
        if (!w.filled) ++counters_.unfilled;
        // Overscan, budgeted, alternating below and above. Bounds are the
        // contiguous exact run around the anchor row, not what earlier passes
        // happened to touch.
        lo = aRow;
        hi = next_visible(aRow);
        while (lo > 0) {
            const std::size_t prev = skip_hidden_backward(lo - 1);
            if (!covered(prev)) break;
            lo = prev;
            if (index_.prefix(lo) <= top - overscan_px) break;
        }
        while (hi < rows_.size() && covered(hi) &&
               index_.prefix(hi) < top + viewH + overscan_px)
            hi = next_visible(hi);
        std::size_t spent = 0;
        bool more_down = true, more_up = true;
        while ((more_down || more_up) && spent < budget) {
            if (more_down) {
                if (hi >= rows_.size() || index_.prefix(hi) >= top + viewH + overscan_px) {
                    more_down = false;
                } else {
                    if (!is_exact(hi)) ++spent;
                    ensure(hi, measure);
                    hi = next_visible(hi);
                }
            }
            if (more_up && spent < budget) {
                if (lo == 0 || index_.prefix(lo) <= top - overscan_px) {
                    more_up = false;
                } else {
                    const std::size_t prev = skip_hidden_backward(lo - 1);
                    if (!is_exact(prev)) ++spent;
                    ensure(prev, measure);
                    lo = prev;
                }
            }
        }
        // Measuring above the anchor moved it and the viewport together: the
        // top the caller must adopt is the anchor's, read now.
        top = clamp_top(wanted_top(anchor, aRow, viewH), viewH);
        w.first = lo;
        w.last = hi;
        w.top = index_.prefix(lo);
        w.viewport_top = top;
        return w;
    }

    void clear() {
        kindCount_.fill(0);
        visibleRows_ = 0;
        rows_.clear();
        keys_.clear();
        index_.clear();
        indexOf_.clear();
        initialized_ = false;
        revision_ = 0;
    }

  private:
    // The visible row after r: a pile lead is crossed by its span.
    std::size_t next_visible(std::size_t r) const {
        return skip_hidden_forward(r + 1 + rows_[r].hiddenAfter);
    }
    template <class Measure>
    void ensure(std::size_t row, Measure&& measure) {
        if (is_exact(row) || rows_[row].hidden) {
            ++counters_.rows_visited;
            return;
        }
        set_measure(row, measure(row));  // counts the visit itself
    }

    void commit(std::size_t row) {
        const float t = rows_[row].total();
        if (index_.set(row, t) != 0.0f) ++counters_.index_updates;
    }

    template <class Classify>
    void classify_row(std::size_t row, Classify&& classify) {
        const RowClass c = classify(row);
        RowGeom& g = rows_[row];
        const bool kindChanged = g.kind != c.kind || g.hidden != c.hidden;
        const bool wasHidden = g.hidden;
        const std::uint32_t oldDistance = g.leadDistance;
        if (g.counted && !g.hidden) { --kindCount_[g.kind]; --visibleRows_; }
        if (!c.hidden) { ++kindCount_[c.kind]; ++visibleRows_; }
        g.counted = true;
        g.kind = c.kind;
        g.span = c.span;
        g.hidden = c.hidden;
        g.showAuthor = c.showAuthor;
        g.live = c.live;
        g.before = c.before;
        g.after = c.after;
        if (c.hidden) {
            g.leadDistance = 1;
            if (row > 0 && rows_[row - 1].hidden)
                g.leadDistance = rows_[row - 1].leadDistance + 1;
            else if (row == 0)
                g.leadDistance = 1;  // points at -1: no visible row before
            if (g.leadDistance <= row) {
                RowGeom& lead = rows_[row - g.leadDistance];
                lead.hiddenAfter = std::max(lead.hiddenAfter, g.leadDistance);
            }
            g.hiddenAfter = 0;
        } else {
            if (wasHidden) {
                // Became visible: the visible row before it now has fewer
                // hidden rows after it; the rows after this one are
                // re-classified by the caller's cascade.
                if (oldDistance <= row && oldDistance > 0)
                    rows_[row - oldDistance].hiddenAfter = oldDistance - 1;
                g.hiddenAfter = 0;
            }
            g.leadDistance = 0;
        }
        g.hint = c.estimate;
        if (!g.everMeasured && (kindChanged || c.estimate >= 0.0f))
            g.h = calibrated_estimate(c.kind, c.estimate);
        if (kindChanged) g.exact = false;
        ++counters_.rows_classified;
        commit(row);
    }

    struct Calib {
        double sumExact = 0.0;
        double sumHint = 0.0;
        std::size_t count = 0;
    };
    // The hint (or the kind constant) scaled by how the measured rows of the
    // kind compared to theirs, once enough have been measured to say.
    float calibrated_estimate(RowGeom::Kind k, float hint) const {
        const float base = hint >= 0.0f ? hint : estimate(k);
        const Calib& c = calib_[k];
        if (c.count < 4 || c.sumHint <= 0.0) return base;
        const double ratio = c.sumExact / c.sumHint;
        return static_cast<float>(std::clamp(static_cast<double>(base) * ratio, 8.0, 100000.0));
    }

    static float estimate(RowGeom::Kind k) {
        switch (k) {
            case RowGeom::Event: return 28.0f;
            case RowGeom::ToolBlock: return 44.0f;
            case RowGeom::ToolPile: return 60.0f;
            case RowGeom::Spawn: return 72.0f;
            case RowGeom::Thinking: return 40.0f;
            case RowGeom::Delivery: return 40.0f;
            case RowGeom::Compaction: return 36.0f;
            case RowGeom::System: return 38.0f;
            default: return 96.0f;
        }
    }

    // Every row's classification again, in order, keeping measurements.
    template <class Classify>
    void reclassify_all(Classify&& classify) {
        for (RowGeom& g : rows_) g.hiddenAfter = 0;
        for (std::size_t i = 0; i < rows_.size(); ++i) classify_row(i, classify);
        counters_.reindexed += rows_.size();
    }

    // The row's id changed (an optimistic local id replaced by the server's
    // on reconcile): drop the old key, insert the new. O(1), by the key the
    // row remembers -- never a scan of the map.
    void rekey(std::size_t i, std::string id) {
        if (keys_[i] == id) return;
        auto old = indexOf_.find(keys_[i]);
        if (old != indexOf_.end() && old->second == i) indexOf_.erase(old);
        indexOf_[id] = i;
        keys_[i] = std::move(id);
    }

    template <class Classify, class IdOf>
    void rebuild(std::size_t n, Classify&& classify, IdOf&& id_of) {
        rows_.assign(n, RowGeom{});
        kindCount_.fill(0);
        visibleRows_ = 0;
        keys_.assign(n, std::string());
        indexOf_.clear();
        indexOf_.reserve(n);
        std::vector<float> heights(n, 0.0f);
        index_.assign(heights);
        for (std::size_t i = 0; i < n; ++i) {
            classify_row(i, classify);
            keys_[i] = id_of(i);
            indexOf_.emplace(keys_[i], i);
        }
        counters_.reindexed += n;
    }

    template <class Classify, class IdOf>
    void append(std::size_t n, Classify&& classify, IdOf&& id_of) {
        const std::size_t old = rows_.size();
        for (std::size_t i = old; i < n; ++i) {
            rows_.push_back(RowGeom{});
            index_.push_back(0.0f);
            keys_.push_back(id_of(i));
            indexOf_[keys_.back()] = i;
            classify_row(i, classify);
            RowGeom& g = rows_[i];
            if (g.hidden && g.kind == RowGeom::ToolPile && g.leadDistance <= i) {
                // A pile grew by one: the lead's span and height change,
                // nothing else in the run does. O(1), not a walk of the run.
                const std::size_t leadRow = i - g.leadDistance;
                if (rows_[leadRow].kind != RowGeom::ToolPile || rows_[leadRow].hidden) {
                    // A single tool block just became the lead of two.
                    classify_row(leadRow, classify);
                }
                RowGeom& lead = rows_[leadRow];
                lead.span = static_cast<std::uint32_t>(g.leadDistance + 1);
                lead.exact = false;
                continue;
            }
            if (g.hidden) continue;  // a hidden non-pile row (thinking): navigation set above
            // The row before: its outcome line, or its becoming a pile lead
            // now that a tool row follows it, are local to the boundary.
            if (i > 0) {
                const RowGeom::Kind wasKind = rows_[i - 1].kind;
                const bool wasHidden = rows_[i - 1].hidden;
                classify_row(i - 1, classify);
                // A single tool block that just became a lead of two: the new
                // row is now its follower.
                if ((rows_[i - 1].kind != wasKind || rows_[i - 1].hidden != wasHidden) &&
                    rows_[i - 1].kind == RowGeom::ToolPile && !rows_[i - 1].hidden)
                    classify_row(i, classify);
            }
        }
    }

    template <class Classify, class IdOf>
    void prepend(std::size_t count, std::size_t n, Classify&& classify,
                 IdOf&& id_of) {
        if (rows_.size() + count != n) {
            rebuild(n, classify, id_of);
            return;
        }
        // Shift: heights and exactness survive, indices move by `count`.
        std::vector<RowGeom> shifted(n, RowGeom{});
        std::vector<float> heights(n, 0.0f);
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            shifted[i + count] = rows_[i];
            heights[i + count] = rows_[i].total();
        }
        rows_.swap(shifted);
        index_.assign(heights);
        keys_.assign(n, std::string());
        indexOf_.clear();
        indexOf_.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            keys_[i] = id_of(i);
            indexOf_[keys_[i]] = i;
        }
        for (std::size_t i = 0; i < count; ++i) classify_row(i, classify);
        // The old first row: its author row / divider depend on the new
        // row before it.
        if (count < n) reclassify_run(count, classify);
        counters_.reindexed += n;
        lastShift_ = count;
    }

    template <class Classify, class IdOf>
    void update(std::size_t first, std::size_t count, Classify&& classify, IdOf&& id_of) {
        // An update for rows the ledger has not appended yet (the notes are
        // applied in order; a later append brings them) is nothing to do.
        if (first >= rows_.size()) return;
        const std::size_t end = std::min(rows_.size(), first + std::max<std::size_t>(count, 1));
        bool runChanged = false;
        for (std::size_t i = first; i < end; ++i) {
            const RowGeom::Kind wasKind = rows_[i].kind;
            const bool wasHidden = rows_[i].hidden;
            classify_row(i, classify);
            rows_[i].exact = false;
            rekey(i, id_of(i));
            if (rows_[i].hidden && rows_[i].kind == RowGeom::ToolPile &&
                rows_[i].leadDistance <= i)
                rows_[i - rows_[i].leadDistance].exact = false;  // the pile's height is the run's
            if (rows_[i].kind != wasKind || rows_[i].hidden != wasHidden) runChanged = true;
        }
        if (first > 0) reclassify_run(first - 1, classify);
        if (end < rows_.size()) reclassify_run(end, classify);
        // A member that changed kind splits or joins a run: the rows after
        // it in the run need their lead and distance again. Bounded by the
        // run and taken only when a kind actually moved, so a status update
        // on a pile row is O(1).
        if (runChanged) cascade_hidden_from(end, classify);
    }

    // Re-classify `row` and, when it is a pile follower, its lead (the lead's
    // span and height are the run's). O(1): no walk of the run.
    template <class Classify>
    void reclassify_run(std::size_t row, Classify&& classify) {
        const bool wasHidden = rows_[row].hidden;
        classify_row(row, classify);
        const RowGeom& g = rows_[row];
        if (g.hidden && g.kind == RowGeom::ToolPile && g.leadDistance <= row)
            classify_row(row - g.leadDistance, classify);
        // A row that joined or left a hidden run: the rows after it in the
        // run carry distances to the wrong visible row until re-classified.
        if (g.hidden != wasHidden) cascade_hidden_from(row + 1, classify);
    }

    // Re-classify rows from `from` while they are hidden (plus the first
    // visible one, whose hiddenAfter the run feeds). Bounded by the run and
    // taken only when a row's hidden state actually moved.
    template <class Classify>
    void cascade_hidden_from(std::size_t from, Classify&& classify) {
        std::size_t i = from;
        while (i < rows_.size()) {
            const bool hidden = rows_[i].hidden;
            classify_row(i, classify);
            if (!hidden && !rows_[i].hidden) break;
            ++i;
        }
    }

    template <class Classify>
    void apply_facts(const LedgerFacts& f, Classify&& classify) {
        if (!initialized_) {
            facts_ = f;
            return;
        }
        // Width alone bumps nothing: rows carry their own validity.
        const bool epochChange =
            f.font_epoch != facts_.font_epoch ||
            f.fold_long_messages != facts_.fold_long_messages ||
            f.tool_fold_mode != facts_.tool_fold_mode ||
            f.find_open != facts_.find_open || f.find_query != facts_.find_query;
        if (epochChange) ++epoch_;
        // Reasoning and date dividers are CLASSIFICATION facts (a thinking
        // row's hidden bit, a row's `before`), so a toggle re-classifies
        // every row -- scalar, no measure, counted under reindexed -- rather
        // than leaving stale bits until each row is visited.
        if (f.show_reasoning != facts_.show_reasoning ||
            f.show_date_dividers != facts_.show_date_dividers) {
            facts_ = f;  // classify reads the new facts through the caller
            reclassify_all(classify);
        }
        if (f.unread_first != facts_.unread_first) {
            if (facts_.unread_first >= 0 &&
                static_cast<std::size_t>(facts_.unread_first) < rows_.size())
                classify_row(static_cast<std::size_t>(facts_.unread_first), classify);
            if (f.unread_first >= 0 &&
                static_cast<std::size_t>(f.unread_first) < rows_.size())
                classify_row(static_cast<std::size_t>(f.unread_first), classify);
        }
        if (f.streaming != facts_.streaming || f.live_index != facts_.live_index ||
            f.stream_phase != facts_.stream_phase) {
            if (facts_.streaming && facts_.live_index < rows_.size())
                classify_row(facts_.live_index, classify);
            if (f.streaming && f.live_index < rows_.size())
                classify_row(f.live_index, classify);
        }
        facts_ = f;
    }

    std::vector<RowGeom> rows_;
    std::vector<std::string> keys_;  // each row's id as the map knows it
    std::array<std::size_t, RowGeom::KindCount> kindCount_{};
    std::array<Calib, RowGeom::KindCount> calib_{};
    std::size_t visibleRows_ = 0;
    HeightIndex index_;
    std::unordered_map<std::string, std::size_t> indexOf_;
    LedgerFacts facts_;
    std::uint32_t epoch_ = 1;
    std::uint32_t pass_ = 0;
    float slackBelow_ = 0.0f;
    float slackAbove_ = 0.0f;
    std::uint64_t revision_ = 0;
    bool initialized_ = false;
    std::size_t lastShift_ = 0;  // rows the last sync prepended
    LedgerCounters counters_;
};

// One ledger per pane slot, LRU-bounded like the render cache.
class TranscriptLedgers {
  public:
    static constexpr std::size_t kMaxSlots = 4;
    TranscriptLedger& slot(const std::string& key) {
        auto found = slots_.find(key);
        if (found != slots_.end()) {
            order_.erase(found->second.pos);
            order_.push_front(key);
            found->second.pos = order_.begin();
            return found->second.ledger;
        }
        if (slots_.size() >= kMaxSlots) {
            slots_.erase(order_.back());
            order_.pop_back();
        }
        order_.push_front(key);
        Slot s;
        s.pos = order_.begin();
        return slots_.emplace(key, std::move(s)).first->second.ledger;
    }
    void mark_all_dirty() {
        for (auto& kv : slots_) kv.second.ledger.mark_all_dirty();
    }
    void mark_dirty(const std::string& key, std::size_t row) {
        auto found = slots_.find(key);
        if (found != slots_.end()) found->second.ledger.mark_dirty(row);
    }
    std::size_t slots() const { return slots_.size(); }
    std::size_t total_rows() const {
        std::size_t n = 0;
        for (const auto& kv : slots_) n += kv.second.ledger.rows();
        return n;
    }
    void clear() {
        slots_.clear();
        order_.clear();
    }

  private:
    struct Slot {
        TranscriptLedger ledger;
        std::list<std::string>::iterator pos;
    };
    std::unordered_map<std::string, Slot> slots_;
    std::list<std::string> order_;
};

inline TranscriptLedgers& transcript_ledgers() {
    static TranscriptLedgers l;
    return l;
}

}  // namespace ecs::model
