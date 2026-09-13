// The composer strip's chip labels follow the reference's StripFit rungs by pane
// width: units leave the counts under 777, the sub-agents state word under
// 533, the model's detail under 490. The rules live on MainPaneSystem as
// statics; this exercises them at the boundaries the user's 0.7.1 reference
// sits between (two panes ~690 wide read "137", "2", "1 running", "0 of 4").
#include <cstdio>
#include <string>

#include "../../src/ecs/composer_strip.h"

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);        \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

int main() {
    namespace S = ecs::model;
    const auto full = S::strip_fit(902.0f);
    const auto counts = S::strip_fit(690.0f);   // the reference's panes
    const auto bare = S::strip_fit(500.0f);
    const auto plain = S::strip_fit(480.0f);

    CHECK(full.counts_keep_unit && full.counts_keep_state_word && full.model_keeps_detail);
    CHECK(!counts.counts_keep_unit && counts.counts_keep_state_word && counts.model_keeps_detail);
    CHECK(!bare.counts_keep_unit && !bare.counts_keep_state_word && bare.model_keeps_detail);
    CHECK(!plain.model_keeps_detail);
    // The thresholds are inclusive at the rung's own number (StripFit.swift).
    CHECK(S::strip_fit(777.0f).counts_keep_unit);
    CHECK(!S::strip_fit(776.0f).counts_keep_unit);
    CHECK(S::strip_fit(533.0f).counts_keep_state_word);
    CHECK(!S::strip_fit(532.0f).counts_keep_state_word);

    // Sub-agents: the running count with its state word while any child
    // runs, the total otherwise; singular on both branches.
    CHECK(S::sub_agents_chip_label(137, 0, full) == "137 sub-agents");
    CHECK(S::sub_agents_chip_label(1, 0, full) == "1 sub-agent");
    CHECK(S::sub_agents_chip_label(137, 0, counts) == "137");
    CHECK(S::sub_agents_chip_label(5, 1, full) == "1 sub-agent running");
    CHECK(S::sub_agents_chip_label(5, 2, full) == "2 sub-agents running");
    CHECK(S::sub_agents_chip_label(5, 1, counts) == "1 running");
    CHECK(S::sub_agents_chip_label(5, 1, bare) == "1");

    // Nodes and skills shed their unit at the same rung.
    CHECK(S::nodes_chip_label(2, full) == "2 nodes");
    CHECK(S::nodes_chip_label(1, full) == "1 node");
    CHECK(S::nodes_chip_label(0, full) == "0 nodes");
    CHECK(S::nodes_chip_label(2, counts) == "2");
    CHECK(S::skills_chip_label(8, full) == "8 skills");
    CHECK(S::skills_chip_label(1, full) == "1 skill");
    CHECK(S::skills_chip_label(21, counts) == "21");

    // Skills loaded: distinct by name, over the transcript's Skill events.
    api::Session s;
    for (const char* name : {"presto-query", "meta-cli", "presto-query"}) {
        api::Message m;
        m.kind = api::EventKind::Skill;
        m.text = name;
        s.messages.push_back(m);
    }
    api::Message plainText;
    plainText.text = "presto-query";
    s.messages.push_back(plainText);
    CHECK(S::distinct_skills_loaded(s) == 2);

    if (failures == 0) std::printf("OK\n");
    return failures == 0 ? 0 : 1;
}
