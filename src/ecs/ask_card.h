#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "../api/elicitation.h"
#include "../api/types.h"

namespace hanabi::ask {

inline constexpr float kPad = 10.0f;
inline constexpr float kHeadH = 22.0f;
inline constexpr float kMessageH = 18.0f;
inline constexpr float kPromptH = 20.0f;
inline constexpr float kOptionH = 28.0f;
inline constexpr float kOptionLineH = 16.0f;
inline constexpr int kMaxOptionLines = 3;
inline constexpr int kMaxPromptLines = 3;
inline constexpr float kFieldH = 30.0f;
inline constexpr float kNoteH = 18.0f;
inline constexpr float kActionsGap = 4.0f;
inline constexpr float kButtonsH = 36.0f;
inline constexpr float kQuestionGap = 6.0f;
inline constexpr int kMaxMessageLines = 4;
inline constexpr int kMaxInputLines = 12;
inline constexpr float kMinBodyH = 90.0f;

struct Cursor {
    std::string question;
    std::string option;

    [[nodiscard]] bool set() const { return !question.empty(); }
    void clear() {
        question.clear();
        option.clear();
    }
};

struct State {
    std::map<std::string, api::AskAnswer> answers;
    std::map<std::string, Cursor> cursors;
    std::string busyId;
    std::string errorId;
    std::string errorText;

    std::uint64_t loadSeq = 0;
    std::map<std::string, std::uint64_t> dropStamp;

    std::uint64_t next_load_stamp() { return ++loadSeq; }
    void note_drop(const std::string& session) { dropStamp[session] = loadSeq; }
    [[nodiscard]] bool load_is_stale(const std::string& session,
                                     std::uint64_t stamp) const {
        const auto at = dropStamp.find(session);
        return at != dropStamp.end() && stamp <= at->second;
    }

    api::AskAnswer& answer_for(const std::string& id) { return answers[id]; }
    Cursor& cursor_for(const std::string& id) { return cursors[id]; }

    void forget(const std::string& id) {
        answers.erase(id);
        cursors.erase(id);
        if (errorId == id) {
            errorId.clear();
            errorText.clear();
        }
        if (busyId == id) busyId.clear();
    }
};

inline int clamp_message_lines(int measured) {
    if (measured < 1) return 1;
    if (measured > kMaxMessageLines) return kMaxMessageLines;
    return measured;
}

struct QuestionMetrics {
    int prompt_lines = 1;
    std::vector<int> option_lines;
};

inline int clamp_prompt_lines(int measured) {
    if (measured < 1) return 1;
    if (measured > kMaxPromptLines) return kMaxPromptLines;
    return measured;
}

inline int clamp_option_lines(int measured) {
    if (measured < 1) return 1;
    if (measured > kMaxOptionLines) return kMaxOptionLines;
    return measured;
}

inline float option_row_h(int lines) {
    const int n = clamp_option_lines(lines);
    return kOptionH + kOptionLineH * static_cast<float>(n - 1);
}

inline float prompt_row_h(int lines) {
    return kPromptH + kOptionLineH * static_cast<float>(
                                        clamp_prompt_lines(lines) - 1);
}

inline float question_h(const api::AskQuestion& q,
                        const QuestionMetrics& metrics) {
    float h = prompt_row_h(metrics.prompt_lines);
    switch (q.control) {
        case api::AskControl::Single:
        case api::AskControl::Multi:
            for (std::size_t i = 0; i < q.options.size(); ++i)
                h += option_row_h(i < metrics.option_lines.size()
                                      ? metrics.option_lines[i]
                                      : 1);
            if (!q.free_text_key.empty()) h += kFieldH + kNoteH;
            break;
        case api::AskControl::Text:
            h += kFieldH;
            break;
        case api::AskControl::File:
            h += kNoteH;
            break;
    }
    return h + kQuestionGap;
}

inline bool submit_blocked(const api::PendingAsk& ask,
                           const api::AskAnswer& answer) {
    if (ask.kind == api::AskKind::Approval) return false;
    return !api::elicitation::answer_has_content(ask, answer);
}

inline std::string blocked_reason(const api::PendingAsk& ask) {
    if (ask.answerable_questions() > 1) return "Answer any one of these to submit";
    return "Answer to submit";
}

inline int clamp_input_lines(int measured) {
    if (measured < 1) return 1;
    if (measured > kMaxInputLines) return kMaxInputLines;
    return measured;
}

inline float chrome_h(const api::PendingAsk& ask, int messageLines,
                      bool showNote) {
    float h = kPad * 2.0f + kHeadH + kButtonsH;
    if (!ask.message.empty() && messageLines > 0)
        h += kMessageH * static_cast<float>(clamp_message_lines(messageLines));
    if (showNote) h += kNoteH;
    return h;
}

inline float body_h(const api::PendingAsk& ask, int inputLines,
                    const std::vector<QuestionMetrics>& metrics) {
    if (ask.kind == api::AskKind::Approval)
        return ask.input.empty()
                   ? 0.0f
                   : kNoteH * static_cast<float>(clamp_input_lines(inputLines));
    if (ask.schema_unreadable || ask.questions.empty()) return kNoteH;
    float h = 0.0f;
    static const QuestionMetrics kFallback;
    for (std::size_t i = 0; i < ask.questions.size(); ++i)
        h += question_h(ask.questions[i],
                        i < metrics.size() ? metrics[i] : kFallback);
    return h;
}

inline bool body_too_short(float view, float natural) {
    return natural > view && view < kOptionH;
}

inline float body_view_h(float natural, float budget) {
    if (budget < 0.0f) budget = 0.0f;
    const float view = natural > budget ? budget : natural;
    if (!body_too_short(view, natural)) return view;
    return budget < kNoteH ? 0.0f : kNoteH;
}

inline int message_lines_for(const api::PendingAsk& ask, int messageLines,
                             bool showNote, float budget) {
    if (budget <= 0.0f) return messageLines;
    int lines = messageLines;
    while (lines > 0 && chrome_h(ask, lines, showNote) + kMinBodyH > budget)
        --lines;
    return lines;
}

inline float irreducible_h(const api::PendingAsk& ask) {
    return chrome_h(ask, 0, false);
}

inline float card_h(const api::PendingAsk& ask, int messageLines,
                    bool showNote, int inputLines,
                    const std::vector<QuestionMetrics>& metrics,
                    float budget) {
    const float body = body_h(ask, inputLines, metrics);
    if (budget <= 0.0f) return chrome_h(ask, messageLines, showNote) + body;
    const float chrome = chrome_h(
        ask, message_lines_for(ask, messageLines, showNote, budget), showNote);
    return chrome + body_view_h(body, budget - chrome);
}

inline const api::PendingAsk* first_pending(
    const std::vector<api::PendingAsk>& asks) {
    return asks.empty() ? nullptr : &asks.front();
}

inline std::string head_text(const api::PendingAsk& ask) {
    const bool child = !ask.child_session.empty();
    if (ask.kind == api::AskKind::Approval)
        return child ? "A sub-agent needs approval" : "The agent needs approval";
    return child ? "A sub-agent is asking" : "The agent is asking";
}

inline std::vector<std::pair<std::string, std::string>> option_run(
    const api::PendingAsk& ask) {
    std::vector<std::pair<std::string, std::string>> run;
    for (const auto& q : ask.questions) {
        if (q.control != api::AskControl::Single &&
            q.control != api::AskControl::Multi)
            continue;
        for (const auto& o : q.options) run.emplace_back(q.key, o.value);
    }
    return run;
}

inline void move_cursor(const api::PendingAsk& ask, Cursor* cursor, int delta) {
    const auto run = option_run(ask);
    if (run.empty()) {
        cursor->clear();
        return;
    }
    if (!cursor->set()) {
        const auto& pick = delta < 0 ? run.back() : run.front();
        cursor->question = pick.first;
        cursor->option = pick.second;
        return;
    }
    int at = -1;
    for (int i = 0; i < static_cast<int>(run.size()); ++i)
        if (run[static_cast<std::size_t>(i)].first == cursor->question &&
            run[static_cast<std::size_t>(i)].second == cursor->option)
            at = i;
    if (at < 0) {
        cursor->question = run.front().first;
        cursor->option = run.front().second;
        return;
    }
    int next = at + delta;
    if (next < 0) next = 0;
    if (next >= static_cast<int>(run.size()))
        next = static_cast<int>(run.size()) - 1;
    cursor->question = run[static_cast<std::size_t>(next)].first;
    cursor->option = run[static_cast<std::size_t>(next)].second;
}

inline int option_index(const api::PendingAsk& ask, const Cursor& cursor) {
    for (const auto& q : ask.questions) {
        if (q.key != cursor.question) continue;
        for (std::size_t i = 0; i < q.options.size(); ++i)
            if (q.options[i].value == cursor.option) return static_cast<int>(i);
    }
    return 0;
}

inline void toggle(const api::PendingAsk& ask, const std::string& key,
                   const std::string& value, api::AskAnswer* answer) {
    for (const auto& q : ask.questions) {
        if (q.key != key) continue;
        if (q.control == api::AskControl::Single) {
            if (answer->picked(key, value)) answer->picks.erase(key);
            else answer->picks[key] = {value};
            return;
        }
        if (q.control != api::AskControl::Multi) return;
        auto& picks = answer->picks[key];
        const auto at = std::find(picks.begin(), picks.end(), value);
        if (at != picks.end()) picks.erase(at);
        else picks.push_back(value);
        if (picks.empty()) answer->picks.erase(key);
        return;
    }
}

inline bool toggle_at_cursor(const api::PendingAsk& ask, const Cursor& cursor,
                             api::AskAnswer* answer) {
    if (!cursor.set()) return false;
    const auto run = option_run(ask);
    for (const auto& entry : run)
        if (entry.first == cursor.question && entry.second == cursor.option) {
            toggle(ask, cursor.question, cursor.option, answer);
            return true;
        }
    return false;
}

enum class ReturnIntent {
    Submit,
    PickAtCursor,
    Ignore,
};

inline ReturnIntent return_intent(const api::PendingAsk& ask,
                                  const api::AskAnswer& answer,
                                  const Cursor& cursor) {
    if (ask.kind == api::AskKind::Approval) return ReturnIntent::Submit;
    if (!submit_blocked(ask, answer)) return ReturnIntent::Submit;
    if (cursor.set()) return ReturnIntent::PickAtCursor;
    return ReturnIntent::Ignore;
}

}  // namespace hanabi::ask
