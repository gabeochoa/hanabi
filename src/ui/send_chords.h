#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace hanabi::send_chords {

enum class Chord { None, QueueThisOne, SendNow };

inline Chord classify(bool cmd, bool option, bool cmdReturnIsSendKey) {
    if (!cmd) return Chord::None;
    if (option) return Chord::QueueThisOne;
    return cmdReturnIsSendKey ? Chord::None : Chord::SendNow;
}

struct HintRow {
    std::string chord;
    std::string meaning;
};

inline std::vector<HintRow> hint_rows(bool running, bool queueDefault) {
    std::vector<HintRow> rows;
    if (!running) {
        rows.push_back({"Return", "Send"});
        return rows;
    }
    rows.push_back({"Return", queueDefault ? "Queue for the end of the turn" : "Steer the running turn"});
    rows.push_back({"Cmd Opt Return", queueDefault ? "Steer this one instead" : "Queue this one instead"});
    rows.push_back({"Cmd Return", "Send now"});
    return rows;
}

}
