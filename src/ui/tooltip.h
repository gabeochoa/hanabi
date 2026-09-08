#pragma once

#include <string>
#include <string_view>

namespace hanabi::tip {

inline constexpr float kRevealSeconds = 0.5f;

inline constexpr float kTipHeight = 24.0f;
inline constexpr float kTipPadH = 10.0f;
inline constexpr float kTipCharW = 6.0f;

inline std::string_view text_for(std::string_view control) {
    if (control.ends_with("_2")) control.remove_suffix(2);
    if (control == "composer_attach") return "Attach a file";
    if (control == "composer_send") return "Send this message";
    if (control == "code_block_copy") return "Copy this code block";
    if (control == "find_close") return "Close find";
    if (control == "jump_to_bottom") return "Jump to the newest message";
    if (control == "msg_quote_btn")
        return "Quote this message in the composer";
    if (control == "row_star") return "Star this conversation";
    if (control == "split_close") return "Close this pane";
    if (control == "tab_close") return "Close this tab";
    if (control == "tab_pin") return "Keep this tab open";
    return {};
}

inline float width_for(std::string_view text) {
    return kTipPadH * 2.0f + static_cast<float>(text.size()) * kTipCharW;
}

class Timer {
   public:
    void hold(std::string_view name, float dt) {
        if (name.empty()) {
            leave();
            return;
        }
        if (name != target_) {
            target_.assign(name);
            held_ = 0.0f;
            suppressed_ = false;
        }
        if (dt > 0.0f) held_ += dt;
    }

    void leave() {
        target_.clear();
        held_ = 0.0f;
        suppressed_ = false;
    }

    void force_show(std::string_view name) {
        target_.assign(name);
        held_ = kRevealSeconds;
        suppressed_ = false;
    }

    void interrupt() {
        held_ = 0.0f;
        suppressed_ = true;
    }

    bool visible() const {
        return !suppressed_ && !target_.empty() && held_ >= kRevealSeconds;
    }

    bool awaiting_reveal() const {
        return !suppressed_ && !target_.empty() && held_ < kRevealSeconds;
    }

    std::string_view shown() const {
        return visible() ? std::string_view(target_) : std::string_view();
    }

    std::string_view pending() const { return target_; }
    float held() const { return held_; }
    bool suppressed() const { return suppressed_; }

   private:
    std::string target_;
    float held_ = 0.0f;
    bool suppressed_ = false;
};

}  // namespace hanabi::tip
