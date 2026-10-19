#pragma once

#include <cctype>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hanabi::inline_math {

using GlyphCheck = std::function<bool(std::uint32_t)>;

inline bool every_glyph(std::uint32_t) { return true; }

namespace detail {

inline const std::unordered_map<std::string, std::string>& symbols() {
    static const std::unordered_map<std::string, std::string> table = {
        {"alpha", "α"}, {"beta", "β"}, {"gamma", "γ"}, {"delta", "δ"}, {"epsilon", "ε"},
        {"varepsilon", "ε"}, {"zeta", "ζ"}, {"eta", "η"}, {"theta", "θ"}, {"vartheta", "ϑ"},
        {"iota", "ι"}, {"kappa", "κ"}, {"lambda", "λ"}, {"mu", "μ"}, {"nu", "ν"}, {"xi", "ξ"},
        {"pi", "π"}, {"rho", "ρ"}, {"sigma", "σ"}, {"tau", "τ"}, {"upsilon", "υ"}, {"phi", "φ"},
        {"varphi", "φ"}, {"chi", "χ"}, {"psi", "ψ"}, {"omega", "ω"}, {"Gamma", "Γ"},
        {"Delta", "Δ"}, {"Theta", "Θ"}, {"Lambda", "Λ"}, {"Xi", "Ξ"}, {"Pi", "Π"}, {"Sigma", "Σ"},
        {"Phi", "Φ"}, {"Psi", "Ψ"}, {"Omega", "Ω"}, {"times", "×"}, {"cdot", "·"}, {"div", "÷"},
        {"pm", "±"}, {"mp", "∓"}, {"le", "≤"}, {"leq", "≤"}, {"ge", "≥"}, {"geq", "≥"},
        {"ne", "≠"}, {"neq", "≠"}, {"approx", "≈"}, {"equiv", "≡"}, {"sim", "∼"}, {"propto", "∝"},
        {"infty", "∞"}, {"partial", "∂"}, {"nabla", "∇"}, {"sum", "∑"}, {"prod", "∏"},
        {"int", "∫"}, {"oint", "∮"}, {"in", "∈"}, {"notin", "∉"}, {"subset", "⊂"},
        {"subseteq", "⊆"}, {"cup", "∪"}, {"cap", "∩"}, {"emptyset", "∅"}, {"forall", "∀"},
        {"exists", "∃"}, {"neg", "¬"}, {"land", "∧"}, {"lor", "∨"}, {"to", "→"},
        {"rightarrow", "→"}, {"leftarrow", "←"}, {"Rightarrow", "⇒"}, {"Leftarrow", "⇐"},
        {"iff", "⇔"}, {"mapsto", "↦"}, {"ldots", "…"}, {"cdots", "⋯"}, {"dots", "…"},
        {"log", "log"}, {"ln", "ln"}, {"exp", "exp"}, {"sin", "sin"}, {"cos", "cos"},
        {"tan", "tan"}, {"max", "max"}, {"min", "min"}, {"lim", "lim"}, {"deg", "°"},
        {"circ", "∘"}, {"ell", "ℓ"}, {"hbar", "ℏ"}, {"langle", "⟨"}, {"rangle", "⟩"},
        {"lfloor", "⌊"}, {"rfloor", "⌋"}, {"lceil", "⌈"}, {"rceil", "⌉"}, {"quad", " "},
        {"qquad", "  "}, {"mid", "∣"},
    };
    return table;
}

inline const std::unordered_map<char, std::string>& superscripts() {
    static const std::unordered_map<char, std::string> table = {
        {'0', "⁰"}, {'1', "¹"}, {'2', "²"}, {'3', "³"}, {'4', "⁴"}, {'5', "⁵"}, {'6', "⁶"},
        {'7', "⁷"}, {'8', "⁸"}, {'9', "⁹"}, {'+', "⁺"}, {'-', "⁻"}, {'=', "⁼"}, {'(', "⁽"},
        {')', "⁾"}, {'n', "ⁿ"}, {'i', "ⁱ"},
    };
    return table;
}

inline const std::unordered_map<char, std::string>& subscripts() {
    static const std::unordered_map<char, std::string> table = {
        {'0', "₀"}, {'1', "₁"}, {'2', "₂"}, {'3', "₃"}, {'4', "₄"}, {'5', "₅"}, {'6', "₆"},
        {'7', "₇"}, {'8', "₈"}, {'9', "₉"}, {'+', "₊"}, {'-', "₋"}, {'=', "₌"}, {'(', "₍"},
        {')', "₎"}, {'a', "ₐ"}, {'e', "ₑ"}, {'o', "ₒ"}, {'x', "ₓ"}, {'i', "ᵢ"}, {'j', "ⱼ"},
        {'k', "ₖ"}, {'n', "ₙ"}, {'m', "ₘ"}, {'t', "ₜ"},
    };
    return table;
}

inline bool is_letter(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }
inline bool is_digit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
inline bool is_space(char c) { return c == ' ' || c == '\t'; }
inline bool is_word(char c) {
    return is_letter(c) || is_digit(c) || c == '_' || static_cast<unsigned char>(c) >= 0x80;
}

inline bool allowed_source_char(char c) {
    if (is_letter(c) || is_digit(c)) return true;
    static constexpr std::string_view allowed = "+-=<>()[],.;:!'|/* ^_{}\\";
    return allowed.find(c) != std::string_view::npos;
}

inline std::uint32_t first_codepoint(std::string_view s) {
    const auto b0 = static_cast<unsigned char>(s[0]);
    if (b0 < 0x80) return b0;
    const int len = (b0 >= 0xF0) ? 4 : (b0 >= 0xE0) ? 3 : 2;
    std::uint32_t cp = (len == 4) ? (b0 & 0x07u) : (len == 3) ? (b0 & 0x0Fu) : (b0 & 0x1Fu);
    for (int i = 1; i < len && i < static_cast<int>(s.size()); ++i)
        cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3Fu);
    return cp;
}

inline bool drawable(std::string_view s, const GlyphCheck& glyph) {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto b0 = static_cast<unsigned char>(s[i]);
        const std::size_t len = (b0 < 0x80) ? 1 : (b0 >= 0xF0) ? 4 : (b0 >= 0xE0) ? 3 : 2;
        if (!glyph(first_codepoint(s.substr(i)))) return false;
        i += len;
    }
    return true;
}

struct Setter {
    std::string_view src;
    std::size_t i = 0;
    bool displayMinus = false;
    const GlyphCheck& glyph;

    bool minus(std::string& out) {
        if (!displayMinus) {
            out.push_back('-');
            return true;
        }
        if (!glyph(0x2212)) return false;
        out += "−";
        return true;
    }

    bool render(std::string& out, char until, bool hasUntil) {
        while (i < src.size()) {
            const char c = src[i];
            if (hasUntil && c == until) {
                ++i;
                return true;
            }
            switch (c) {
                case '\\': {
                    ++i;
                    std::string name;
                    while (i < src.size() && is_letter(src[i])) name.push_back(src[i++]);
                    if (name.empty()) {
                        if (i >= src.size()) return false;
                        const char sym = src[i];
                        if (std::string_view("{}_^,;! ").find(sym) == std::string_view::npos) return false;
                        ++i;
                        if (sym == '{' || sym == '}' || sym == '_' || sym == '^') out.push_back(sym);
                        if (sym == ',' || sym == ';' || sym == ' ') out.push_back(' ');
                        continue;
                    }
                    if (name == "frac") {
                        std::string top, bottom;
                        if (!group(top) || !group(bottom)) return false;
                        out += wrapped(top) + "/" + wrapped(bottom);
                        continue;
                    }
                    if (name == "sqrt") {
                        std::string body;
                        if (!group(body)) return false;
                        if (!glyph(0x221A)) return false;
                        out += "√" + wrapped(body);
                        continue;
                    }
                    if (name == "mathrm" || name == "text" || name == "mathit" || name == "mathbf" ||
                        name == "operatorname") {
                        std::string body;
                        const bool keep = displayMinus;
                        if (name == "text") displayMinus = false;
                        const bool ok = group(body);
                        displayMinus = keep;
                        if (!ok) return false;
                        out += body;
                        continue;
                    }
                    if (name == "left" || name == "right" || name == "displaystyle") continue;
                    const auto it = symbols().find(name);
                    if (it == symbols().end()) return false;
                    if (!drawable(it->second, glyph)) return false;
                    out += it->second;
                    break;
                }
                case '^':
                case '_': {
                    ++i;
                    std::string body;
                    if (!group(body)) return false;
                    const auto& table = c == '^' ? superscripts() : subscripts();
                    std::string script;
                    bool all = !body.empty();
                    for (const char b : body) {
                        const auto it = table.find(b);
                        if (it == table.end() || !drawable(it->second, glyph)) {
                            all = false;
                            break;
                        }
                        script += it->second;
                    }
                    if (all) {
                        out += script;
                    } else {
                        out.push_back(c);
                        out += body.size() == 1 ? body : "(" + body + ")";
                    }
                    break;
                }
                case '{': {
                    ++i;
                    std::string inner;
                    if (!render(inner, '}', true)) return false;
                    out += inner;
                    break;
                }
                case '}':
                    return false;
                default:
                    ++i;
                    if (c == '-') {
                        if (!minus(out)) return false;
                    } else {
                        out.push_back(c);
                    }
                    break;
            }
        }
        return !hasUntil;
    }

    bool group(std::string& out) {
        if (i >= src.size()) return false;
        if (src[i] == '{') {
            ++i;
            return render(out, '}', true);
        }
        if (src[i] == '\\') {
            std::size_t probe = i + 1;
            std::string name;
            while (probe < src.size() && is_letter(src[probe])) name.push_back(src[probe++]);
            const auto it = symbols().find(name);
            if (it == symbols().end() || !drawable(it->second, glyph)) return false;
            i = probe;
            out += it->second;
            return true;
        }
        const char c = src[i++];
        if (c == '-') return minus(out);
        out.push_back(c);
        return true;
    }

    static std::string wrapped(const std::string& part) {
        bool plain = true;
        for (const char c : part)
            if (!is_letter(c) && !is_digit(c)) plain = false;
        return part.size() == 1 || plain ? part : "(" + part + ")";
    }
};

inline std::string escaped(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 4);
    for (const char c : text) {
        if (std::string_view("\\`*_[]<>").find(c) != std::string_view::npos) out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

inline std::size_t code_span_end(std::string_view s, std::size_t start) {
    std::size_t run = start;
    while (run < s.size() && s[run] == '`') ++run;
    const std::size_t width = run - start;
    std::size_t scan = run;
    while (scan < s.size()) {
        if (s[scan] != '`') {
            ++scan;
            continue;
        }
        std::size_t close = scan;
        while (close < s.size() && s[close] == '`') ++close;
        if (close - scan == width) return close;
        scan = close;
    }
    return run;
}

}  // namespace detail

inline std::size_t closing_dollar(std::string_view s, std::size_t opening) {
    if (opening + 1 >= s.size() || detail::is_space(s[opening + 1]) || s[opening + 1] == '\n' ||
        s[opening + 1] == '$')
        return std::string_view::npos;
    if (opening > 0 && detail::is_word(s[opening - 1])) return std::string_view::npos;
    std::size_t scan = opening + 1;
    while (scan < s.size()) {
        const char c = s[scan];
        if (c == '\n') return std::string_view::npos;
        if (c == '\\') {
            scan += 2;
            continue;
        }
        if (c == '$') {
            if (detail::is_space(s[scan - 1])) return std::string_view::npos;
            if (scan + 1 < s.size()) {
                const char next = s[scan + 1];
                if (detail::is_digit(next) || detail::is_letter(next) || next == '{' || next == '_' ||
                    next == '$' || static_cast<unsigned char>(next) >= 0x80)
                    return std::string_view::npos;
            }
            return scan;
        }
        ++scan;
    }
    return std::string_view::npos;
}

inline std::string typeset(std::string_view source, const GlyphCheck& glyph = every_glyph,
                           bool displayMinus = false) {
    if (source.find("__") != std::string_view::npos) return {};
    for (const char c : source)
        if (!detail::allowed_source_char(c)) return {};
    detail::Setter setter{source, 0, displayMinus, glyph};
    std::string out;
    if (!setter.render(out, '\0', false)) return {};
    return out;
}

inline std::string rewrite_line(std::string_view line, const GlyphCheck& glyph) {
    std::string out;
    out.reserve(line.size());
    std::size_t i = 0;
    while (i < line.size()) {
        const char c = line[i];
        if (c == '`') {
            const std::size_t end = detail::code_span_end(line, i);
            out.append(line.substr(i, end - i));
            i = end;
            continue;
        }
        if (c == '\\' && i + 1 < line.size()) {
            out.push_back(c);
            out.push_back(line[i + 1]);
            i += 2;
            continue;
        }
        if (c == '$') {
            if (i + 1 < line.size() && line[i + 1] == '$') {
                std::size_t end = i;
                while (end < line.size() && line[end] == '$') ++end;
                out.append(line.substr(i, end - i));
                i = end;
                continue;
            }
            const std::size_t close = closing_dollar(line, i);
            if (close != std::string_view::npos) {
                const std::string set = typeset(line.substr(i + 1, close - i - 1), glyph);
                if (!set.empty()) {
                    out.push_back('*');
                    out += detail::escaped(set);
                    out.push_back('*');
                    i = close + 1;
                    continue;
                }
            }
        }
        out.push_back(c);
        ++i;
    }
    return out;
}

inline std::string rewritten(const std::string& markdown, const GlyphCheck& glyph = every_glyph) {
    if (markdown.find('$') == std::string::npos) return markdown;
    std::string out;
    out.reserve(markdown.size());
    bool fenced = false;
    std::size_t pos = 0;
    while (pos <= markdown.size()) {
        const std::size_t nl = markdown.find('\n', pos);
        const std::string_view line(markdown.data() + pos,
                                    (nl == std::string::npos ? markdown.size() : nl) - pos);
        std::string_view lead = line;
        while (!lead.empty() && (lead.front() == ' ' || lead.front() == '\t')) lead.remove_prefix(1);
        const bool fence = lead.rfind("```", 0) == 0 || lead.rfind("~~~", 0) == 0;
        if (fence) fenced = !fenced;
        if (fenced || fence || line.find('$') == std::string_view::npos)
            out.append(line);
        else
            out += rewrite_line(line, glyph);
        if (nl == std::string::npos) break;
        out.push_back('\n');
        pos = nl + 1;
    }
    return out;
}

}  // namespace hanabi::inline_math
