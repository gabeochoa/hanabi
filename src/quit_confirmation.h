#pragma once

#include <string>
#include <string_view>

namespace hanabi::quit_confirmation {

enum class Invocation { QuitShortcut, Explicit };
enum class Answer { Quit, Cancel };

inline Invocation invocation(bool isKeyDown, bool commandDown, std::string_view charactersIgnoringModifiers) {
    if (!isKeyDown || !commandDown) return Invocation::Explicit;
    if (charactersIgnoringModifiers != "q" && charactersIgnoringModifiers != "Q") return Invocation::Explicit;
    return Invocation::QuitShortcut;
}

inline bool should_ask(bool enabled, Invocation inv) { return enabled && inv == Invocation::QuitShortcut; }

inline bool should_disable(Answer answer, bool suppressionOn) { return answer == Answer::Quit && suppressionOn; }

inline constexpr int kButtonCount = 2;

inline Answer answer_for_button(int offset) {
    if (offset == 0) return Answer::Quit;
    return Answer::Cancel;
}

inline std::string title(std::string_view appName) { return "Quit " + std::string(appName) + "?"; }

inline std::string body(std::string_view appName) {
    return "Are you sure you want to quit " + std::string(appName) + "? All its windows will close.";
}

inline constexpr std::string_view kQuitButton = "Quit";
inline constexpr std::string_view kCancelButton = "Cancel";
inline constexpr std::string_view kSuppressionTitle = "Don't ask again";

}
