// A refetch lands on the transcript the reader is looking at: a tail is an
// Append, a settled tool call is an Update, the same rows are nothing, and
// only two histories with no row in common reset the view.
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/ecs/transcript_reconcile.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

using api::Message;
using ecs::model::ReconcileOutcome;
using Kind = ReconcileOutcome::Kind;

static Message row(std::string id, api::Role role, std::string text) {
    Message m;
    m.id = std::move(id);
    m.role = role;
    m.text = std::move(text);
    return m;
}

static std::vector<Message> three() {
    return {row("10", api::Role::User, "hello"),
            row("11", api::Role::Assistant, "hi"),
            row("12", api::Role::Tool, "ls")};
}

static void test_a_tail_is_appended_and_nothing_moves() {
    auto mine = three();
    std::vector<Message> fresh = {row("12", api::Role::Tool, "ls"),
                                  row("13", api::Role::Assistant, "done"),
                                  row("14", api::Role::User, "thanks")};
    const ReconcileOutcome out =
        ecs::model::reconcile_transcript(mine, std::move(fresh));
    CHECK(out.kind == Kind::Appended);
    CHECK(out.first == 3);
    CHECK(out.count == 2);
    CHECK(mine.size() == 5);
    // The rows the window no longer covers are still here, in place.
    CHECK(mine[0].id == "10" && mine[0].text == "hello");
    CHECK(mine[3].id == "13");
    CHECK(mine[4].id == "14");
}

static void test_a_settled_call_is_updated_in_place() {
    auto mine = three();
    mine[2].tool_status = "running";
    std::vector<Message> fresh = {row("12", api::Role::Tool, "ls")};
    fresh[0].tool_status = "completed";
    fresh[0].tool_result = "total 0";
    const ReconcileOutcome out =
        ecs::model::reconcile_transcript(mine, std::move(fresh));
    CHECK(out.kind == Kind::Updated);
    CHECK(out.first == 2);
    CHECK(out.count == 1);
    CHECK(mine.size() == 3);
    CHECK(mine[2].tool_status == "completed");
    CHECK(mine[2].tool_result == "total 0");
}

static void test_the_same_window_changes_nothing() {
    auto mine = three();
    const ReconcileOutcome out =
        ecs::model::reconcile_transcript(mine, three());
    CHECK(out.kind == Kind::Unchanged);
    CHECK(mine.size() == 3);
}

static void test_no_common_row_resets() {
    auto mine = three();
    std::vector<Message> fresh = {row("40", api::Role::User, "elsewhere"),
                                  row("41", api::Role::Assistant, "indeed")};
    const ReconcileOutcome out =
        ecs::model::reconcile_transcript(mine, std::move(fresh));
    CHECK(out.kind == Kind::Reset);
    CHECK(mine.size() == 2);
    CHECK(mine[0].id == "40");

    std::vector<Message> empty;
    const ReconcileOutcome first =
        ecs::model::reconcile_transcript(empty, three());
    CHECK(first.kind == Kind::Reset);
    CHECK(empty.size() == 3);
}

static void test_a_local_row_keeps_its_sync_mark() {
    auto mine = three();
    mine[0].sync = api::SyncState::Synced;
    mine[0].local_id = "local-7";
    std::vector<Message> fresh = {row("10", api::Role::User, "hello edited")};
    const ReconcileOutcome out =
        ecs::model::reconcile_transcript(mine, std::move(fresh));
    CHECK(out.kind == Kind::Updated);
    CHECK(mine[0].text == "hello edited");
    CHECK(mine[0].sync == api::SyncState::Synced);
    CHECK(mine[0].local_id == "local-7");
}

static void test_the_cursor_is_the_newest_durable_seq() {
    auto mine = three();
    CHECK(ecs::model::newest_seq(mine) == 12);
    // A locally minted row is not a server seq and is skipped.
    Message local = row("", api::Role::User, "typing");
    local.sync = api::SyncState::LocalOnly;
    mine.push_back(local);
    CHECK(ecs::model::newest_seq(mine) == 12);
    // The mock's ids are not seqs: no cursor, a plain refetch.
    std::vector<Message> mock = {row("m1", api::Role::User, "x")};
    CHECK(ecs::model::newest_seq(mock) == 0);
    CHECK(ecs::model::newest_seq({}) == 0);
}

static void test_a_hidden_artifact_row_is_refreshed_and_keeps_its_adopted_bytes() {
    auto mine = three();
    Message art = row("13", api::Role::System, "image/png  \xc2\xb7  4 KB");
    art.kind = api::EventKind::Artifact;
    art.subtitle = "price-tiers.png";
    art.artifact.id = "art-1";
    art.artifact.version = "v1";
    art.artifact.file = "price-tiers.png";
    art.artifact.media_type = "image/png";
    art.artifact.local_path = "/cache/artifacts/art-1-v1-price-tiers.png";
    art.artifact.fetch = api::ArtifactFetch::Ready;
    art.image_path = art.artifact.local_path;
    mine.push_back(art);

    Message fresh_hidden = art;
    fresh_hidden.artifact.hidden = true;
    fresh_hidden.artifact.local_path.clear();
    fresh_hidden.artifact.fetch = api::ArtifactFetch::Idle;
    fresh_hidden.image_path.clear();
    ReconcileOutcome out = ecs::model::reconcile_transcript(mine, {fresh_hidden});
    CHECK(out.kind == Kind::Updated);
    CHECK(mine[3].artifact.hidden);
    CHECK(mine[3].artifact.local_path == "/cache/artifacts/art-1-v1-price-tiers.png");
    CHECK(mine[3].artifact.fetch == api::ArtifactFetch::Ready);
    CHECK(mine[3].image_path == mine[3].artifact.local_path);

    out = ecs::model::reconcile_transcript(mine, {fresh_hidden});
    CHECK(out.kind == Kind::Unchanged);
    CHECK(mine[3].artifact.fetch == api::ArtifactFetch::Ready);

    Message fresh_v2 = art;
    fresh_v2.artifact.version = "v2";
    fresh_v2.artifact.local_path.clear();
    fresh_v2.artifact.fetch = api::ArtifactFetch::Idle;
    fresh_v2.image_path.clear();
    out = ecs::model::reconcile_transcript(mine, {fresh_v2});
    CHECK(out.kind == Kind::Updated);
    CHECK(mine[3].artifact.version == "v2" && !mine[3].artifact.hidden);
    CHECK(mine[3].artifact.local_path.empty() && mine[3].image_path.empty());
    CHECK(mine[3].artifact.fetch == api::ArtifactFetch::Idle);
}

static void test_a_run_terminal_arriving_on_an_existing_row_lands() {
    auto mine = three();
    Message fresh = mine[2];
    fresh.run_outcome = "failed";
    fresh.run_note = "2 of 3 declared calls did not run";
    const ReconcileOutcome out = ecs::model::reconcile_transcript(mine, {fresh});
    CHECK(out.kind == Kind::Updated);
    CHECK(mine[2].run_outcome == "failed");
    CHECK(mine[2].run_note == "2 of 3 declared calls did not run");
    Message bare;
    bare.id = "run_finished:40";
    bare.role = api::Role::System;
    bare.kind = api::EventKind::RunOutcome;
    bare.run_outcome = "completed";
    bare.run_note = "1 of 1 declared call did not run";
    const ReconcileOutcome out2 = ecs::model::reconcile_transcript(mine, {fresh, bare});
    CHECK(out2.kind == Kind::Appended);
    CHECK(mine.size() == 4 && mine[3].id == "run_finished:40");
}

int main() {
    std::printf("=== test_transcript_reconcile ===\n");
    test_a_tail_is_appended_and_nothing_moves();
    test_a_settled_call_is_updated_in_place();
    test_the_same_window_changes_nothing();
    test_no_common_row_resets();
    test_a_local_row_keeps_its_sync_mark();
    test_the_cursor_is_the_newest_durable_seq();
    test_a_hidden_artifact_row_is_refreshed_and_keeps_its_adopted_bytes();
    test_a_run_terminal_arriving_on_an_existing_row_lands();
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
