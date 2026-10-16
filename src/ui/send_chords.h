#pragma once

#include <string>
#include <vector>

namespace hanabi::send_chords {

enum class Chord { None, QueueThisOne };

inline Chord classify(bool cmd, bool option) {
    return (cmd && option) ? Chord::QueueThisOne : Chord::None;
}

struct HintRow {
    std::string chord;
    std::string meaning;
};

inline std::vector<HintRow> hint_rows(bool running) {
    std::vector<HintRow> rows;
    if (!running) {
        rows.push_back({"Return", "Send"});
        return rows;
    }
    rows.push_back({"Return", "Steer this one"});
    rows.push_back({"Cmd Opt Return", "Queue this one"});
    return rows;
}

}
