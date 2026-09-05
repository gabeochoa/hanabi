#pragma once

#include <cstdint>
#include <ctime>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
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
    api::AskAction errorAction = api::AskAction::Accept;

    std::map<std::string, int64_t> seenAt;
    std::uint64_t loadSeq = 0;
    // Drop authority is per ASK, not per session.
    //
    // It used to be per session: resolving one ask stamped the whole thread,
    // and load_is_stale() then threw away every load already in flight for it.
    // But a load in flight during a live turn is exactly the one carrying the
    // NEXT ask the agent raised, so answering the first question discarded the
    // second -- it never reached the card, its draft never existed, and the
    // thread was left looking answered. The authority was (session, loadSeq);
    // the thing actually authorised was "ask A is resolved".
    //
    // Keyed by ask id it says only what it knows. An older load may not bring
    // THIS ask back; everything else it carries is none of this stamp's
    // business.
    std::map<std::string, std::uint64_t> dropStamp;

    std::map<std::string, std::uint64_t> bornStamp;
    std::string shownId;

    std::uint64_t next_load_stamp() { return ++loadSeq; }

    // The stamp of the load that CARRIED this ask -- not the value of the
    // global counter when that load happened to land.
    //
    // It used to be the latter: note_born() read loadSeq, which is one counter
    // shared by every session's refresh and every turn. A load's stamp is
    // minted when it is REQUESTED and it lands seconds later, so by then the
    // counter has moved on by however many polls other threads happened to
    // fire in between. The number recorded had nothing to do with this ask.
    //
    // What it broke is keep_newer_asks(), whose whole job is "a load may only
    // retire an ask it could have known about". With an inflated stamp an ask
    // outranked loads that were genuinely newer than the one that brought it,
    // so an ask resolved elsewhere was pushed back onto the card by the very
    // snapshot that reported it gone -- and whether that happened depended on
    // how busy an unrelated thread's polling was.
    //
    // Recorded as the carrying stamp, born_after()'s strict > is exactly the
    // rule: older load lands late, it cannot speak for this ask and the ask
    // survives; newer load lands without it, that is authoritative.
    void note_born(const std::string& id, std::uint64_t stamp) {
        bornStamp.emplace(id, stamp);
    }
    [[nodiscard]] bool born_after(const std::string& id,
                                  std::uint64_t stamp) const {
        const auto at = bornStamp.find(id);
        return at != bornStamp.end() && at->second > stamp;
    }
    void note_drop(const std::string& askId) { dropStamp[askId] = loadSeq; }
    [[nodiscard]] bool ask_is_stale(const std::string& askId,
                                    std::uint64_t stamp) const {
        const auto at = dropStamp.find(askId);
        return at != dropStamp.end() && stamp <= at->second;
    }

    api::AskAnswer& answer_for(const std::string& id) { return answers[id]; }
    Cursor& cursor_for(const std::string& id) { return cursors[id]; }

    void adopt(const std::vector<api::PendingAsk>& live,
               const std::vector<api::PendingAsk>& known,
               std::uint64_t stamp) {
        std::set<std::string> alive;
        for (const auto& a : live) alive.insert(a.id());
        for (const auto& a : known)
            if (alive.count(a.id()) == 0) forget(a.id());
        for (const auto& a : live) note_born(a.id(), stamp);
        const int64_t now = static_cast<int64_t>(std::time(nullptr));
        for (const auto& a : live) {
            if (a.child_session.empty())
                seenAt.emplace(a.id(), now);
            else
                seenAt.insert_or_assign(a.id(), now);
        }
    }

    void forget(const std::string& id) {
        answers.erase(id);
        cursors.erase(id);
        seenAt.erase(id);
        bornStamp.erase(id);
        if (shownId == id) shownId.clear();
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
    bool arity_stacked = false;
    std::vector<int> option_lines;
};

inline int clamp_prompt_lines(int measured) {
    if (measured < 1) return 1;
    if (measured > kMaxPromptLines) return kMaxPromptLines;
    return measured;
}

inline std::string draft_text_of(const api::PendingAsk& ask,
                                 const api::AskAnswer& answer) {
    std::string out;
    const auto add = [&out](const std::string& line) {
        if (line.empty()) return;
        if (!out.empty()) out += "\n";
        out += line;
    };
    for (const auto& q : ask.questions) {
        const auto picked = answer.picks.find(q.key);
        if (picked != answer.picks.end() && !picked->second.empty()) {
            std::string joined;
            for (const auto& value : picked->second) {
                if (!joined.empty()) joined += ", ";
                joined += value;
            }
            add(q.prompt.empty() ? joined : q.prompt + ": " + joined);
        }
        const auto typed = answer.text.find(q.key);
        if (typed != answer.text.end()) {
            const std::string t = api::elicitation::trimmed(typed->second);
            if (!t.empty()) add(q.prompt.empty() ? t : q.prompt + ": " + t);
        }
        const auto other = answer.text.find(q.free_text_key);
        if (!q.free_text_key.empty() && other != answer.text.end()) {
            const std::string t = api::elicitation::trimmed(other->second);
            if (!t.empty()) add(t);
        }
    }
    return out;
}

inline constexpr std::size_t kMaxRescuedSessions = 32;

struct RescuedDrafts {
    std::map<std::string, std::string> bySession;
    std::vector<std::string> order;

    void keep(const std::string& session, const std::string& text) {
        if (session.empty() || text.empty()) return;
        auto at = bySession.find(session);
        if (at == bySession.end()) {
            bySession.emplace(session, text);
            order.push_back(session);
        } else {
            at->second += "\n";
            at->second += text;
            return;
        }
        while (order.size() > kMaxRescuedSessions) {
            bySession.erase(order.front());
            order.erase(order.begin());
        }
    }

    [[nodiscard]] const std::string* find(const std::string& session) const {
        const auto at = bySession.find(session);
        return at == bySession.end() ? nullptr : &at->second;
    }

    void clear(const std::string& session) {
        bySession.erase(session);
        order.erase(std::remove(order.begin(), order.end(), session),
                    order.end());
    }

    [[nodiscard]] bool empty() const { return bySession.empty(); }
};

inline std::string metrics_key(const api::PendingAsk& ask) {
    std::string key = ask.id();
    key += '#';
    key += std::to_string(ask.questions.size());
    for (const auto& q : ask.questions) {
        key += '|';
        key += q.key;
        key += ':';
        key += std::to_string(static_cast<int>(q.control));
    }
    return key;
}

inline std::string with_ellipsis(std::string line) {
    while (!line.empty() && line.back() == ' ') line.pop_back();
    if (line.size() > 1) {
        const auto at = line.find_last_of(' ');
        if (at != std::string::npos && at >= line.size() / 2)
            line.erase(at);
    }
    return line + "\u2026";
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

inline float prompt_row_h(int lines, bool arityStacked = false) {
    return kPromptH +
           kOptionLineH * static_cast<float>(clamp_prompt_lines(lines) - 1) +
           (arityStacked ? kOptionLineH : 0.0f);
}

inline float question_h(const api::AskQuestion& q,
                        const QuestionMetrics& metrics) {
    float h = prompt_row_h(metrics.prompt_lines, metrics.arity_stacked);
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

struct AskShown {
    bool chosen = false;
    bool has_draft = false;
};

inline std::size_t shown_index(const std::vector<AskShown>& asks) {
    for (std::size_t i = 0; i < asks.size(); ++i)
        if (asks[i].chosen) return i;
    for (std::size_t i = 0; i < asks.size(); ++i)
        if (asks[i].has_draft) return i;
    return 0;
}

inline bool expired_at(std::int64_t timeout_ms, std::int64_t seen_at,
                       std::int64_t now, std::int64_t deadline_unix_ms = 0) {
    if (deadline_unix_ms > 0) return now * 1000 > deadline_unix_ms;
    if (timeout_ms <= 0) return false;
    return (now - seen_at) * 1000 > timeout_ms;
}

inline bool has_draft(const api::PendingAsk& ask,
                      const api::AskAnswer& answer) {
    for (const auto& q : ask.questions) {
        const auto pick = answer.picks.find(q.key);
        if (pick != answer.picks.end() && !pick->second.empty()) return true;
        const auto at = answer.text.find(q.key);
        if (at != answer.text.end() &&
            !api::elicitation::trimmed(at->second).empty())
            return true;
        if (q.free_text_key.empty()) continue;
        const auto other = answer.text.find(q.free_text_key);
        if (other != answer.text.end() &&
            !api::elicitation::trimmed(other->second).empty())
            return true;
    }
    return false;
}

inline bool submit_blocked(const api::PendingAsk& ask,
                           const api::AskAnswer& answer) {
    if (ask.kind == api::AskKind::Approval) return false;
    return !api::elicitation::answer_has_content(ask, answer);
}

inline constexpr const char* kFileDeferralNote =
    "the file question will be left unanswered";

inline std::string with_file_caveat(const api::PendingAsk& ask,
                                    std::string note) {
    if (!ask.has_file_question()) return note;
    if (note.empty()) {
        std::string one = kFileDeferralNote;
        one[0] = static_cast<char>(std::toupper(one[0]));
        return one;
    }
    return note + " — " + kFileDeferralNote;
}

inline std::string blocked_reason(const api::PendingAsk& ask) {
    if (ask.answerable_questions() == 0)
        return "Nothing here can be answered from hanabi";
    if (ask.answerable_questions() > 1) return "Answer any one of these to submit";
    return "Answer to submit";
}

inline constexpr int kMaxNoteLines = 8;

inline int clamp_note_lines(int measured) {
    if (measured < 1) return 1;
    if (measured > kMaxNoteLines) return kMaxNoteLines;
    return measured;
}

inline float chrome_h(const api::PendingAsk& ask, int messageLines,
                      bool showNote, int noteLines) {
    float h = kPad * 2.0f + kHeadH + kButtonsH;
    if (!ask.message.empty() && messageLines > 0)
        h += kMessageH * static_cast<float>(clamp_message_lines(messageLines));
    if (showNote) h += kNoteH * static_cast<float>(clamp_note_lines(noteLines));
    return h;
}

inline float body_h(const api::PendingAsk& ask, int inputLines,
                    const std::vector<QuestionMetrics>& metrics) {
    if (ask.kind == api::AskKind::Approval)
        return ask.input.empty()
                   ? 0.0f
                   : kNoteH * static_cast<float>(inputLines < 1 ? 1
                                                                : inputLines);
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
                             bool showNote, int noteLines, float budget) {
    if (budget <= 0.0f) return messageLines;
    int lines = messageLines;
    while (lines > 0 &&
           chrome_h(ask, lines, showNote, noteLines) + kMinBodyH > budget)
        --lines;
    return lines;
}

struct KeyOwnership {
    bool cardFocused = false;
    bool modalSheet = false;   // rename, composer, shortcuts, settings, auth
    bool transientUi = false;  // find bar, menus, popovers, session search
    bool recordingShortcut = false;
};

inline bool input_live(const KeyOwnership& own) {
    return !own.modalSheet && !own.transientUi && !own.recordingShortcut;
}

inline bool keys_live(const KeyOwnership& own) {
    return own.cardFocused && input_live(own);
}

inline float irreducible_h(const api::PendingAsk& ask, int noteLines) {
    return chrome_h(ask, 0, true, noteLines);
}

inline float card_h(const api::PendingAsk& ask, int messageLines,
                    bool showNote, int noteLines, int inputLines,
                    const std::vector<QuestionMetrics>& metrics,
                    float budget) {
    const float body = body_h(ask, inputLines, metrics);
    if (budget <= 0.0f)
        return chrome_h(ask, messageLines, showNote, noteLines) + body;
    const float chrome = chrome_h(
        ask, message_lines_for(ask, messageLines, showNote, noteLines, budget),
        showNote, noteLines);
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

inline bool cursor_picks_new(const api::PendingAsk& ask,
                             const api::AskAnswer& answer,
                             const Cursor& cursor) {
    if (!cursor.set()) return false;
    if (answer.picked(cursor.question, cursor.option)) return false;
    for (const auto& entry : option_run(ask))
        if (entry.first == cursor.question && entry.second == cursor.option)
            return true;
    return false;
}

inline ReturnIntent return_intent(const api::PendingAsk& ask,
                                  const api::AskAnswer& answer,
                                  const Cursor& cursor) {
    if (ask.kind == api::AskKind::Approval) return ReturnIntent::Submit;
    if (cursor_picks_new(ask, answer, cursor))
        return ReturnIntent::PickAtCursor;
    if (!submit_blocked(ask, answer)) return ReturnIntent::Submit;
    if (cursor.set()) return ReturnIntent::PickAtCursor;
    return ReturnIntent::Ignore;
}

}  // namespace hanabi::ask
