#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace hanabi::keycaps {

enum class Kind { Modifier, Key, Text };

struct Cap {
    Kind kind = Kind::Key;
    std::string text;
    bool operator==(const Cap&) const = default;
};

inline std::string_view glyph_for(std::string_view token) {
    if (token == "Cmd" || token == "Command") return "\xe2\x8c\x98";
    if (token == "Shift") return "\xe2\x87\xa7";
    if (token == "Opt" || token == "Option" || token == "Alt") return "\xe2\x8c\xa5";
    if (token == "Ctrl" || token == "Control") return "\xe2\x8c\x83";
    return {};
}

inline std::string key_label(std::string_view token) {
    if (token == "Left") return "\xe2\x86\x90";
    if (token == "Right") return "\xe2\x86\x92";
    if (token == "Up") return "\xe2\x86\x91";
    if (token == "Down") return "\xe2\x86\x93";
    if (token == "Backspace") return "\xe2\x8c\xab";
    if (token == "Delete") return "\xe2\x8c\xa6";
    if (token == "Return" || token == "Enter") return "\xe2\x86\xa9";
    if (token == "Tab") return "\xe2\x87\xa5";
    if (token == "Esc" || token == "Escape") return "esc";
    if (token == "Space") return "\xe2\x90\xa3";
    if (token == "Home") return "\xe2\x86\x96";
    if (token == "End") return "\xe2\x86\x98";
    if (token == "Page") return "Page";
    return std::string(token);
}

inline bool is_key_word(std::string_view token) {
    if (token.empty()) return false;
    const char c = token.front();
    if (token.size() == 1) return !(c >= 'a' && c <= 'z');
    if (c >= 'A' && c <= 'Z') return true;
    if (c == 'F' || (c >= '0' && c <= '9')) return true;
    return false;
}

inline void push_token(std::vector<Cap>& out, std::string_view token) {
    if (token.empty()) return;
    if (const std::string_view g = glyph_for(token); !g.empty()) {
        out.push_back(Cap{Kind::Modifier, std::string(g)});
        return;
    }
    if (token == "+" || token == "/") {
        out.push_back(Cap{Kind::Text, std::string(token)});
        return;
    }
    if (const std::size_t slash = token.find('/'); slash != std::string_view::npos) {
        push_token(out, token.substr(0, slash));
        out.push_back(Cap{Kind::Text, "/"});
        push_token(out, token.substr(slash + 1));
        return;
    }
    if (is_key_word(token)) {
        out.push_back(Cap{Kind::Key, key_label(token)});
        return;
    }
    if (!out.empty() && out.back().kind == Kind::Text) {
        out.back().text += " ";
        out.back().text += token;
        return;
    }
    out.push_back(Cap{Kind::Text, std::string(token)});
}

inline std::vector<Cap> split(std::string_view display) {
    std::vector<Cap> out;
    if (display.empty() || display == "Unassigned") return out;
    std::size_t i = 0;
    while (i < display.size()) {
        while (i < display.size() && display[i] == ' ') ++i;
        const std::size_t start = i;
        while (i < display.size() && display[i] != ' ') ++i;
        push_token(out, display.substr(start, i - start));
    }
    return out;
}

inline std::string joined(const std::vector<Cap>& caps) {
    std::string out;
    for (const Cap& c : caps) {
        if (!out.empty()) out += " ";
        out += c.text;
    }
    return out;
}

}
