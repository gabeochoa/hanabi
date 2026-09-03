#pragma once

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
inline constexpr float kFieldH = 30.0f;
inline constexpr float kNoteH = 18.0f;
inline constexpr float kButtonsH = 36.0f;
inline constexpr float kQuestionGap = 6.0f;
inline constexpr int kMaxMessageLines = 2;

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

inline float question_h(const api::AskQuestion& q) {
    float h = kPromptH;
    switch (q.control) {
        case api::AskControl::Single:
        case api::AskControl::Multi:
            h += kOptionH * static_cast<float>(q.options.size());
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

inline float card_h(const api::PendingAsk& ask, int messageLines,
                    bool showNote) {
    float h = kPad * 2.0f + kHeadH + kButtonsH;
    if (!ask.message.empty())
        h += kMessageH * static_cast<float>(clamp_message_lines(messageLines));
    if (ask.kind == api::AskKind::Approval) {
        if (!ask.input.empty()) h += kNoteH;
    } else if (ask.schema_unreadable || ask.questions.empty()) {
        h += kNoteH;
    } else {
        for (const auto& q : ask.questions) h += question_h(q);
    }
    if (showNote) h += kNoteH;
    return h;
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
