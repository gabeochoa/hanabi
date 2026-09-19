#pragma once

#include <deque>

namespace hanabi {

enum class EditVerb : int { Undo, Redo, Cut, Copy, Paste, SelectAll, Count };

class EditVerbQueue {
   public:
    void push(int verb) {
        if (verb < 0 || verb >= static_cast<int>(EditVerb::Count)) return;
        verbs_.push_back(verb);
    }
    bool take(int* verb) {
        if (verb == nullptr || verbs_.empty()) return false;
        *verb = verbs_.front();
        verbs_.pop_front();
        return true;
    }
    std::size_t size() const { return verbs_.size(); }

   private:
    std::deque<int> verbs_;
};

}
