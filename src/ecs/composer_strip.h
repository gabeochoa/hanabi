#pragma once
// ---------------------------------------------------------------------------
// The composer's control strip: what each chip SAYS, by pane width.
//
// The reference client, 0.7.1 (the user-supplied screenshot of 2026-09-12), draws the strip as a row
// of glyph+count chips whose words leave as the pane narrows -- StripFit.swift
// and its rungs, ChildRow.label, NodeAttachment.chipLabel, SkillUse.chipLabel,
// PlanChipText.label. The thresholds and the wording here are theirs. The
// reference's two panes are ~690 wide, the counts-only rung, which is why it
// reads "137", "2", "1 running" and "0 of 4".
//
// Pure: no UI, no graphics, so tests/unit/test_composer_strip.cpp can hold
// every rule to the source.
// ---------------------------------------------------------------------------
#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "../api/types.h"

namespace ecs::model {

// The rung a width buys is the best whose threshold it meets (StripFit.swift:
// full 902, chipGlyphs 777, countsOnly 533, bareCounts 490, plainModel 414);
// each flag below is "the rung is BETTER than the one that sheds it", so the
// unit survives down to 777, the state word to 533, the model's detail to 490.
struct StripFit {
    bool counts_keep_unit;        // "2 nodes" rather than "2"        (>= 777)
    bool counts_keep_state_word;  // "1 running" rather than "1"      (>= 533)
    bool model_keeps_detail;      // "(High)" beside the model         (>= 490)
};

inline StripFit strip_fit(float paneW) {
    return StripFit{paneW >= 777.0f, paneW >= 533.0f, paneW >= 490.0f};
}

// ChildRow.label: the running count with its state word while any child
// runs, the total otherwise; singular on both branches.
inline std::string sub_agents_chip_label(int total, int running,
                                         const StripFit& fit) {
    if (running > 0) {
        if (!fit.counts_keep_unit)
            return fit.counts_keep_state_word
                       ? std::to_string(running) + " running"
                       : std::to_string(running);
        return running == 1 ? "1 sub-agent running"
                            : std::to_string(running) + " sub-agents running";
    }
    if (!fit.counts_keep_unit) return std::to_string(total);
    return total == 1 ? "1 sub-agent" : std::to_string(total) + " sub-agents";
}

// NodeAttachment.chipLabel.
inline std::string nodes_chip_label(std::size_t n, const StripFit& fit) {
    if (!fit.counts_keep_unit) return std::to_string(n);
    return n == 1 ? "1 node" : std::to_string(n) + " nodes";
}

// SkillUse.chipLabel over the distinct skills this transcript loaded.
inline std::string skills_chip_label(std::size_t distinct, const StripFit& fit) {
    if (!fit.counts_keep_unit) return std::to_string(distinct);
    return distinct == 1 ? "1 skill" : std::to_string(distinct) + " skills";
}

// Distinct skills loaded into a transcript: its Skill events, counted by
// text (the skill's name), the way the reference counts `invokedSkills`.
inline std::size_t distinct_skills_loaded(const api::Session& s) {
    std::vector<std::string_view> seen;
    for (const api::Message& m : s.messages) {
        if (m.kind != api::EventKind::Skill) continue;
        const std::string_view name = m.text;
        if (std::find(seen.begin(), seen.end(), name) == seen.end())
            seen.push_back(name);
    }
    return seen.size();
}

}  // namespace ecs::model
