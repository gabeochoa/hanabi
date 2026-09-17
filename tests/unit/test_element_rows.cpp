// The element fold (src/api/element_rows.h): one row per element instance,
// standing at the instance's FIRST emit and carrying its HIGHEST revision,
// whichever order the emits arrive in -- live, from an older page, or from
// the attach's seed -- and the refetch reconcile that lands a folded server
// copy on the rows already on screen. ADDED, NOT RUN in this lane.
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/api/element_rows.h"
#include "../../src/ecs/transcript_reconcile.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

using api::ElementFacts;
using api::Message;
namespace el = api::elements;
using Fold = el::FoldOutcome::Kind;

static Message row(std::string id, api::Role role, std::string text) {
    Message m;
    m.id = std::move(id);
    m.role = role;
    m.text = std::move(text);
    return m;
}

static ElementFacts table(std::uint64_t revision, std::string projection,
                          std::string title = "Shard health") {
    ElementFacts f;
    f.instance = "shard-health";
    f.revision = revision;
    f.placement = "pinned";
    f.element = "std/Table";
    f.title = std::move(title);
    f.projection = std::move(projection);
    f.run = 1;
    return f;
}

static std::vector<Message> conversation() {
    return {row("10", api::Role::User, "show the table"),
            row("20", api::Role::Assistant, "here"),
            row("40", api::Role::Assistant, "updated")};
}

static std::size_t count_elements(const std::vector<Message>& rows) {
    std::size_t n = 0;
    for (const Message& m : rows)
        if (el::is_element_row(m)) ++n;
    return n;
}

// E1: one emit -> one row at its seq, heading = title, text = projection.
static void test_e1_a_single_emit_is_one_row_at_its_seq() {
    auto rows = conversation();
    const auto out = el::fold_element(rows, table(1, "A ok\nB ok"), 30, 1700000000);
    CHECK(out.kind == Fold::Inserted);
    CHECK(out.index == 2);
    CHECK(rows.size() == 4);
    CHECK(rows[2].id == "element:shard-health");
    CHECK(rows[2].kind == api::EventKind::Element);
    CHECK(rows[2].role == api::Role::System);
    CHECK(rows[2].subtitle == "Shard health");
    CHECK(rows[2].text == "A ok\nB ok");
    CHECK(rows[2].element.anchor_seq == 30);
    CHECK(rows[2].element.revision == 1);
    CHECK(rows[2].created_at == 1700000000);
    CHECK(rows[3].id == "40");
    // The id never collides with a durable row's seq id.
    CHECK(el::order_seq(rows[2]) == 30);
    CHECK(el::row_id("7") != "7");
}

// E2: a re-emit at a higher revision replaces the content where the first
// emit stood and says so in the provenance.
static void test_e2_a_reemit_replaces_content_in_place() {
    auto rows = conversation();
    el::fold_element(rows, table(1, "A ok\nB ok"), 30, 1700000000);
    const auto out = el::fold_element(rows, table(2, "A ok\nB ok\nC ok"), 50, 1700000050);
    CHECK(out.kind == Fold::Replaced);
    CHECK(out.index == 2);
    CHECK(rows.size() == 4);
    CHECK(count_elements(rows) == 1);
    CHECK(rows[2].text == "A ok\nB ok\nC ok");
    CHECK(rows[2].element.revision == 2);
    CHECK(rows[2].element.anchor_seq == 30);
    CHECK(rows[2].created_at == 1700000050);
    CHECK(el::provenance(rows[2].element) == "pinned \xc2\xb7 std/Table \xc2\xb7 rev 2");
    // The unrelated rows neither moved nor changed.
    CHECK(rows[0].id == "10" && rows[1].id == "20" && rows[3].id == "40");
}

// E3: the newest revision arrives first (a live frame at the window's edge),
// then an older page delivers rev 1 at a lower seq and rev 2: ONE row, the
// text stays rev 3, and the row MOVES to rev 1's seq.
static void test_e3_an_older_page_moves_the_anchor_but_never_rolls_content_back() {
    auto rows = conversation();
    el::fold_element(rows, table(3, "rev three"), 45, 0);
    CHECK(rows[3].id == "element:shard-health");
    auto older = el::fold_element(rows, table(1, "rev one"), 15, 0);
    CHECK(older.kind == Fold::Moved);
    CHECK(older.from == 3);
    CHECK(older.index == 1);
    CHECK(rows[1].id == "element:shard-health");
    CHECK(rows[1].text == "rev three");
    CHECK(rows[1].element.revision == 3);
    CHECK(rows[1].element.anchor_seq == 15);
    auto middle = el::fold_element(rows, table(2, "rev two"), 25, 0);
    CHECK(middle.kind == Fold::Unchanged);
    CHECK(rows.size() == 4);
    CHECK(count_elements(rows) == 1);
    CHECK(rows[1].text == "rev three");
    CHECK(rows[0].id == "10" && rows[2].id == "20" && rows[3].id == "40");
}

// E4: the same revision delivered twice (seed then live frame; a page re-fed
// on reconnect) is a no-op, not a duplicate and not a rewrite.
static void test_e4_an_equal_revision_is_a_no_op() {
    auto rows = conversation();
    el::fold_element(rows, table(2, "same"), 30, 1700000000);
    const auto again = el::fold_element(rows, table(2, "would differ"), 30, 1700009999);
    CHECK(again.kind == Fold::Unchanged);
    CHECK(again.index == 2);
    CHECK(rows.size() == 4);
    CHECK(rows[2].text == "same");
    CHECK(rows[2].created_at == 1700000000);
    // Higher seq for the same revision: nothing to move to, nothing newer.
    const auto later = el::fold_element(rows, table(2, "same"), 60, 0);
    CHECK(later.kind == Fold::Unchanged);
    CHECK(rows[2].element.anchor_seq == 30);
}

// E5: a seed with two instances at their anchors, no frames; then a live
// revision for one replaces its content and keeps its position.
static void test_e5_seed_then_live_converge_on_one_row_per_instance() {
    auto rows = conversation();
    ElementFacts other;
    other.instance = "run-summary";
    other.revision = 1;
    other.placement = "inline";
    other.element = "std/Card";
    other.projection = "12 passed";
    el::fold_element(rows, table(2, "seeded table"), 15, 0);
    el::fold_element(rows, other, 35, 0);
    CHECK(rows.size() == 5);
    CHECK(rows[1].id == "element:shard-health");
    CHECK(rows[3].id == "element:run-summary");
    CHECK(rows[4].id == "40");
    const auto live = el::fold_element(rows, table(3, "live table"), 55, 1700000055);
    CHECK(live.kind == Fold::Replaced);
    CHECK(rows.size() == 5);
    CHECK(rows[1].text == "live table");
    CHECK(rows[1].element.anchor_seq == 15);
    CHECK(rows[3].text == "12 passed");
    // The live frame for a seeded row at the SAME revision changes nothing.
    CHECK(el::fold_element(rows, other, 35, 1700000035).kind == Fold::Unchanged);
}

// E6 (reader side): the validation the parser applies before the fold.
static void test_e6_the_reader_rejects_malformed_and_accepts_sparse() {
    CHECK(el::facts_are_readable(table(1, "x")));
    ElementFacts zero = table(0, "x");
    CHECK(!el::facts_are_readable(zero));
    ElementFacts nameless = table(1, "x");
    nameless.instance.clear();
    CHECK(!el::facts_are_readable(nameless));
    ElementFacts long_name = table(1, "x");
    long_name.instance.assign(121, 'a');
    CHECK(!el::facts_are_readable(long_name));
    long_name.instance.assign(120, 'a');
    CHECK(el::facts_are_readable(long_name));
    ElementFacts unplaced = table(1, "x");
    unplaced.placement.clear();
    CHECK(!el::facts_are_readable(unplaced));
    ElementFacts huge = table(1, std::string(16 * 1024 + 1, 'p'));
    CHECK(!el::facts_are_readable(huge));
    ElementFacts sparse = table(1, "", "");
    CHECK(el::facts_are_readable(sparse));
    CHECK(el::row_text(sparse) == "std/Table");
}

// E7: provenance by placement.
static void test_e7_provenance_names_placement_and_artifact_handles() {
    ElementFacts inline_first = table(1, "x");
    inline_first.placement = "inline";
    inline_first.title.clear();
    CHECK(el::provenance(inline_first).empty());
    ElementFacts pinned = table(1, "x");
    pinned.title.clear();
    CHECK(el::provenance(pinned) == "pinned");
    ElementFacts with_handle = table(1, "x");
    with_handle.placement = "artifact";
    with_handle.artifact_id = "art_9";
    with_handle.title.clear();
    CHECK(el::provenance(with_handle) == "artifact art_9");
    ElementFacts no_handle = with_handle;
    no_handle.artifact_id.clear();
    CHECK(el::provenance(no_handle) == "artifact (no handle)");
    ElementFacts future_word = table(1, "x");
    future_word.placement = "dock";
    future_word.title.clear();
    CHECK(el::provenance(future_word) == "dock");
}

// E8: heading = title when there is one (and the key rides in provenance),
// else the registry key.
static void test_e8_heading_is_the_title_else_the_element_key() {
    ElementFacts titled = table(1, "x");
    CHECK(el::heading(titled) == "Shard health");
    CHECK(el::provenance(titled) == "pinned \xc2\xb7 std/Table");
    ElementFacts untitled = table(1, "x", "");
    CHECK(el::heading(untitled) == "std/Table");
    CHECK(el::provenance(untitled) == "pinned");
    // A re-emit without a title keeps the title the row already has.
    std::vector<Message> rows;
    el::fold_element(rows, titled, 3, 0);
    el::fold_element(rows, table(2, "y", ""), 5, 0);
    CHECK(rows.size() == 1);
    CHECK(rows[0].subtitle == "Shard health");
    CHECK(rows[0].element.title == "Shard health");
    CHECK(rows[0].text == "y");
}

// E9: the export block's fence is longer than any backtick run in the text,
// so a projection holding ``` still closes.
static void test_e9_the_fence_outruns_every_backtick_run() {
    CHECK(el::fence_enclosing("plain") == "```");
    CHECK(el::fence_enclosing("a `tick`") == "```");
    CHECK(el::fence_enclosing("a ``two``") == "```");
    CHECK(el::fence_enclosing("has ``` inside") == "````");
    CHECK(el::fence_enclosing("has ````` five") == "``````");
    std::vector<Message> rows;
    ElementFacts f = table(2, "Quote as ```sql ... ``` in the doc; **stars literal**", "");
    el::fold_element(rows, f, 3, 0);
    const std::string want =
        "### **Element: std/Table** (pinned \xc2\xb7 rev 2)\n\n"
        "````text\n"
        "Quote as ```sql ... ``` in the doc; **stars literal**\n"
        "````\n\n";
    const std::string got = el::export_block(rows[0]);
    if (got != want) std::printf("got:\n%s\n--- want:\n%s\n", got.c_str(), want.c_str());
    CHECK(got == want);
    // An empty projection exports the heading as the body.
    std::vector<Message> sparse_rows;
    el::fold_element(sparse_rows, table(1, "", ""), 3, 0);
    CHECK(el::export_block(sparse_rows[0]) ==
          "### **Element: std/Table** (pinned)\n\n```text\nstd/Table\n```\n\n");
}

// A seq this fold cannot place (0) goes to the tail; rows whose ids are not
// seqs (a locally minted row, the mock's "m1") are skipped when ordering.
static void test_unordered_rows_do_not_confuse_the_insertion_point() {
    std::vector<Message> rows = {row("m1", api::Role::User, "a"),
                                 row("m2", api::Role::Assistant, "b")};
    el::fold_element(rows, table(1, "x"), 5, 0);
    CHECK(rows.size() == 3 && rows[2].kind == api::EventKind::Element);
    std::vector<Message> mixed = {row("10", api::Role::User, "a"),
                                  row("local-1", api::Role::User, "pending")};
    mixed[1].sync = api::SyncState::LocalOnly;
    el::fold_element(mixed, table(1, "x"), 5, 0);
    CHECK(mixed[0].kind == api::EventKind::Element);
    CHECK(mixed[1].id == "10");
    std::vector<Message> unknown = conversation();
    el::fold_element(unknown, table(1, "x"), 0, 0);
    CHECK(unknown.back().kind == api::EventKind::Element);
}

// The refetch reconcile folds the server's element copy instead of
// replacing it: a window's later anchor cannot move the row, an older
// revision cannot roll it back, a newer one refreshes it in place.
static void test_reconcile_folds_element_rows_instead_of_replacing_them() {
    using ecs::model::ReconcileOutcome;
    auto mine = conversation();
    el::fold_element(mine, table(2, "rev two"), 15, 0);
    // A refetched window that starts after the first emit: same revision,
    // later anchor -- nothing changes, nothing duplicates.
    std::vector<Message> window = {row("40", api::Role::Assistant, "updated")};
    el::fold_element(window, table(2, "rev two"), 35, 0);
    ReconcileOutcome out = ecs::model::reconcile_transcript(mine, window);
    CHECK(out.kind == ReconcileOutcome::Kind::Unchanged);
    CHECK(mine.size() == 4);
    CHECK(mine[1].id == "element:shard-health");
    CHECK(mine[1].element.anchor_seq == 15);
    // A newer revision in the window refreshes the row where it stands.
    std::vector<Message> newer = {row("40", api::Role::Assistant, "updated")};
    el::fold_element(newer, table(3, "rev three"), 55, 0);
    out = ecs::model::reconcile_transcript(mine, newer);
    CHECK(out.kind == ReconcileOutcome::Kind::Updated);
    CHECK(out.first == 1 && out.count == 1);
    CHECK(mine[1].text == "rev three");
    CHECK(mine[1].element.anchor_seq == 15);
    CHECK(mine.size() == 4);
    // An older revision in a later window cannot roll it back.
    std::vector<Message> stale = {row("40", api::Role::Assistant, "updated")};
    el::fold_element(stale, table(1, "rev one"), 35, 0);
    out = ecs::model::reconcile_transcript(mine, stale);
    CHECK(out.kind == ReconcileOutcome::Kind::Unchanged);
    CHECK(mine[1].text == "rev three");
}

// The one reconcile case that reshapes: the rows on screen came from a
// window (anchor at the window's edge) and the refetch carries the seed's
// true anchor. The row moves and the ledger is told to re-read everything.
static void test_reconcile_moves_a_row_whose_true_anchor_arrives_later() {
    using ecs::model::ReconcileOutcome;
    std::vector<Message> mine = {row("20", api::Role::Assistant, "here"),
                                 row("40", api::Role::Assistant, "updated")};
    el::fold_element(mine, table(3, "rev three"), 45, 0);
    CHECK(mine[2].id == "element:shard-health");
    std::vector<Message> fresh = {row("20", api::Role::Assistant, "here"),
                                  row("40", api::Role::Assistant, "updated")};
    el::fold_element(fresh, table(3, "rev three"), 15, 0);
    const ReconcileOutcome out = ecs::model::reconcile_transcript(mine, fresh);
    CHECK(out.kind == ReconcileOutcome::Kind::Reset);
    CHECK(mine.size() == 3);
    CHECK(mine[0].id == "element:shard-health");
    CHECK(mine[0].element.anchor_seq == 15);
    CHECK(mine[1].id == "20" && mine[2].id == "40");
    // A refetch that appends a tail AND carries an element the screen has
    // never seen, anchored BEFORE a row already on screen: inserted at its
    // anchor, and a reset because indices shifted under the ledger.
    ElementFacts card;
    card.instance = "run-summary";
    card.revision = 1;
    card.placement = "inline";
    card.element = "std/Card";
    card.projection = "12 passed";
    std::vector<Message> tail = {row("40", api::Role::Assistant, "updated"),
                                 row("60", api::Role::User, "thanks")};
    el::fold_element(tail, card, 30, 0);
    const ReconcileOutcome grown = ecs::model::reconcile_transcript(mine, tail);
    CHECK(grown.kind == ReconcileOutcome::Kind::Reset);
    CHECK(mine.size() == 5);
    CHECK(mine[2].id == "element:run-summary");
    CHECK(mine[3].id == "40");
    CHECK(mine[4].id == "60");
    // The same unseen element anchored INSIDE the new tail is just part of
    // the tail: an Append, in seq order, no reset.
    std::vector<Message> screen = {row("20", api::Role::Assistant, "here"),
                                   row("40", api::Role::Assistant, "updated")};
    std::vector<Message> grows = {row("40", api::Role::Assistant, "updated"),
                                  row("60", api::Role::User, "thanks")};
    el::fold_element(grows, card, 50, 0);
    const ReconcileOutcome appended = ecs::model::reconcile_transcript(screen, grows);
    CHECK(appended.kind == ReconcileOutcome::Kind::Appended);
    CHECK(appended.first == 2 && appended.count == 2);
    CHECK(screen.size() == 4);
    CHECK(screen[2].id == "element:run-summary");
    CHECK(screen[3].id == "60");
    // The plain tail case is still an Append, untouched by any of this.
    auto plain = conversation();
    std::vector<Message> more = {row("40", api::Role::Assistant, "updated"),
                                 row("50", api::Role::User, "ok")};
    CHECK(ecs::model::reconcile_transcript(plain, more).kind ==
          ReconcileOutcome::Kind::Appended);
}

int main() {
    std::printf("== test_element_rows ==\n");
    test_e1_a_single_emit_is_one_row_at_its_seq();
    test_e2_a_reemit_replaces_content_in_place();
    test_e3_an_older_page_moves_the_anchor_but_never_rolls_content_back();
    test_e4_an_equal_revision_is_a_no_op();
    test_e5_seed_then_live_converge_on_one_row_per_instance();
    test_e6_the_reader_rejects_malformed_and_accepts_sparse();
    test_e7_provenance_names_placement_and_artifact_handles();
    test_e8_heading_is_the_title_else_the_element_key();
    test_e9_the_fence_outruns_every_backtick_run();
    test_unordered_rows_do_not_confuse_the_insertion_point();
    test_reconcile_folds_element_rows_instead_of_replacing_them();
    test_reconcile_moves_a_row_whose_true_anchor_arrives_later();
    if (failures == 0) std::printf("OK\n");
    else std::printf("%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
