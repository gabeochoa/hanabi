#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace hanabi::slash_extras {

inline constexpr int kAutocompactMin = 25;
inline constexpr int kAutocompactMax = 90;

struct AutocompactArg {
    bool ok = false;
    int pct = 0;
    std::string why;
};

inline AutocompactArg parse_autocompact(std::string_view arg) {
    std::size_t b = 0, e = arg.size();
    while (b < e && arg[b] == ' ') ++b;
    while (e > b && (arg[e - 1] == ' ' || arg[e - 1] == '%')) --e;
    const std::string_view s = arg.substr(b, e - b);
    if (s == "off" || s == "auto" || s == "default" || s == "0") return {true, 0, ""};
    if (s.empty() || s.size() > 3) return {false, 0, "Say a percentage from 25 to 90, or default."};
    int v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return {false, 0, "Say a percentage from 25 to 90, or default."};
        v = v * 10 + (c - '0');
    }
    if (v < kAutocompactMin || v > kAutocompactMax)
        return {false, 0, "The boundary is between 25% and 90%."};
    return {true, v, ""};
}

inline std::string autocompact_notice(int pending, int account) {
    if (pending > 0) return "The next new thread compacts at " + std::to_string(pending) + "%.";
    if (account > 0) return "New threads compact at " + std::to_string(account) + "% (your account's boundary).";
    return "New threads compact on Auto. /autocompact 60 sets the next one.";
}

inline std::string session_autocompact_notice(int sessionPct, int account) {
    if (sessionPct > 0) return "This thread compacts at " + std::to_string(sessionPct) + "%.";
    if (account > 0)
        return "This thread compacts at your account's " + std::to_string(account) +
               "%. /autocompact 60 sets its own.";
    return "This thread compacts on Auto. /autocompact 60 sets its own.";
}

struct DraftArgs {
    std::string name;
    std::string text;
};

inline DraftArgs split_draft(std::string_view args) {
    std::size_t b = 0;
    while (b < args.size() && args[b] == ' ') ++b;
    std::size_t e = b;
    while (e < args.size() && args[e] != ' ') ++e;
    DraftArgs out;
    out.name = std::string(args.substr(b, e - b));
    while (e < args.size() && args[e] == ' ') ++e;
    out.text = std::string(args.substr(e));
    while (!out.text.empty() && out.text.back() == ' ') out.text.pop_back();
    return out;
}

template <typename Templates>
std::string drafts_notice(const Templates& templates) {
    if (templates.empty()) return "No saved drafts yet. /draft <name> <text> saves one; /<name> brings it back.";
    std::string out = "Saved drafts:";
    std::size_t n = 0;
    for (const auto& t : templates) {
        out += n++ == 0 ? " /" : ", /";
        out += t.name;
    }
    out += " \xe2\x80\x94 type one to bring it back.";
    return out;
}

}
