#pragma once

// Display math in assistant messages (kt-heop): the reference recognises
// \[...\] and $$...$$ display equations, typesets supported LaTeX as
// selectable aligned text, and preserves unsupported source instead of
// rendering broken markup (the reference, 2026-10-14). Hanabi has no TeX
// engine and draws no JS/SVG, so this mirrors that behaviour with the
// typeset engine inline math already uses (ui/inline_math.h): commands
// become their Unicode characters, scripts become super/subscripts, and
// the whole equation is set as one centred block. Two divergences from
// the reference, both forced by the engines Hanabi has: the reference
// aligns multi-line equations in two columns at their relations with a
// serif face, while Hanabi sets each typeset line as one centred line
// in the prose face (left and right of the relation stay in source
// order on that line), and an equation this table cannot set is shown
// as its source lines in monospace rather than as a single wrapping
// monospaced paragraph. \(...\) is not display math in the reference
// and stays literal here, as does inline $...$ handling in prose.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "inline_math.h"

namespace hanabi::display_math {

enum class Fence { Dollars, Brackets };

struct Line {
    std::string left;
    std::string right;
    bool operator==(const Line&) const = default;
    std::string text() const { return left + right; }
};

// A closed display-math block found at a line start: the source between
// its delimiters, and the offset just past the closing line.
struct Found {
    std::string source;
    std::size_t after = 0;
};

namespace detail {

inline std::string_view trimmed(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

inline std::string_view opener(Fence f) { return f == Fence::Dollars ? "$$" : "\\["; }
inline std::string_view closer(Fence f) { return f == Fence::Dollars ? "$$" : "\\]"; }

inline std::optional<Fence> opener_of(std::string_view line) {
    const std::string_view t = trimmed(line);
    if (t == "$$") return Fence::Dollars;
    if (t == "\\[") return Fence::Brackets;
    return std::nullopt;
}

inline bool is_closer(std::string_view line, Fence f) { return trimmed(line) == closer(f); }

// One line that carries its whole equation: $$x^2$$ or \[x^2\].
inline std::optional<std::string> enclosed(std::string_view line) {
    const std::string_view t = trimmed(line);
    for (const Fence f : {Fence::Brackets, Fence::Dollars}) {
        const std::string_view o = opener(f), c = closer(f);
        if (t.size() > o.size() + c.size() && t.substr(0, o.size()) == o &&
            t.substr(t.size() - c.size()) == c)
            return std::string(t.substr(o.size(), t.size() - o.size() - c.size()));
    }
    return std::nullopt;
}

}  // namespace detail

// The block at `start` (an offset at a line start in `body`), or nothing:
// not a display-math opener, or a fence with no closing line. An unclosed
// fence is not a block, so a settled message shows its source as prose,
// as the reference does; code fences are the caller's to skip first.
inline std::optional<Found> block_at(std::string_view body, std::size_t start) {
    if (start > body.size()) return std::nullopt;
    const std::size_t nl = body.find('\n', start);
    const std::size_t end = nl == std::string_view::npos ? body.size() : nl;
    const std::string_view line = body.substr(start, end - start);
    if (const auto in = detail::enclosed(line)) {
        return Found{*in, nl == std::string_view::npos ? body.size() : nl + 1};
    }
    const auto fence = detail::opener_of(line);
    if (!fence) return std::nullopt;
    std::string source;
    std::size_t p = nl == std::string_view::npos ? body.size() + 1 : nl + 1;
    bool first = true;
    while (p <= body.size()) {
        const std::size_t n2 = body.find('\n', p);
        const std::size_t e2 = n2 == std::string_view::npos ? body.size() : n2;
        const std::string_view cl = body.substr(p, e2 - p);
        if (detail::is_closer(cl, *fence)) {
            return Found{source, n2 == std::string_view::npos ? body.size() : n2 + 1};
        }
        if (!first) source.push_back('\n');
        source.append(cl);
        first = false;
        if (n2 == std::string_view::npos) break;
        p = n2 + 1;
    }
    return std::nullopt;
}

// The typeset lines of a display equation, split at alignment marks and
// relations as the reference splits them, or nothing when any side holds
// something the typeset table cannot set.
inline std::optional<std::vector<Line>> lines(
    std::string_view latex, const inline_math::GlyphCheck& glyph = inline_math::every_glyph) {
    std::string source(latex);
    for (const char* env : {"aligned", "align", "align*", "split"}) {
        for (const char* tag : {"\\begin{", "\\end{"}) {
            const std::string needle = std::string(tag) + env + "}";
            std::size_t at = 0;
            while ((at = source.find(needle, at)) != std::string::npos) {
                source.replace(at, needle.size(), "\n");
                at += 1;
            }
        }
    }
    // Physical lines: \\ and newlines both end a line.
    std::vector<std::string> physical;
    std::string cur;
    for (std::size_t i = 0; i <= source.size(); ++i) {
        if (i == source.size() || source[i] == '\n' ||
            (source[i] == '\\' && i + 1 < source.size() && source[i + 1] == '\\')) {
            physical.push_back(cur);
            cur.clear();
            if (i < source.size() && source[i] == '\\') ++i;
            continue;
        }
        cur.push_back(source[i]);
    }
    std::vector<Line> out;
    for (const std::string& raw : physical) {
        const std::string_view line = detail::trimmed(raw);
        if (line.empty()) continue;
        std::string_view left_part, right_part;
        if (const std::size_t amp = line.find('&'); amp != std::string_view::npos) {
            left_part = line.substr(0, amp);
            right_part = line.substr(amp + 1);
        } else if (const std::size_t eq = line.find('='); eq != std::string_view::npos) {
            const std::size_t rel = (eq > 0 && std::string_view("<>!").find(line[eq - 1]) !=
                                                   std::string_view::npos)
                                        ? eq - 1
                                        : eq;
            left_part = line.substr(0, rel);
            right_part = line.substr(rel);
        } else {
            right_part = line;
        }
        Line rendered;
        for (const auto [part, dest] :
             {std::pair{left_part, &rendered.left}, std::pair{right_part, &rendered.right}}) {
            if (detail::trimmed(part).empty()) continue;
            std::string set = inline_math::typeset(part, glyph, true);
            if (set.empty()) return std::nullopt;
            *dest = std::move(set);
        }
        out.push_back(std::move(rendered));
    }
    if (out.empty()) return std::nullopt;
    return out;
}

// What the block draws, one string per line: the typeset equation when it
// can be set, else the source lines preserved verbatim. Never empty, and
// never the delimiters, so math no longer renders as raw dollar signs.
inline std::vector<std::string> rendered_lines(
    std::string_view source, const inline_math::GlyphCheck& glyph = inline_math::every_glyph,
    bool* typeset_out = nullptr) {
    if (const auto set = lines(source, glyph)) {
        if (typeset_out) *typeset_out = true;
        std::vector<std::string> out;
        for (const Line& l : *set) out.push_back(l.text());
        return out;
    }
    if (typeset_out) *typeset_out = false;
    std::vector<std::string> out;
    std::string cur;
    for (std::size_t i = 0; i <= source.size(); ++i) {
        if (i == source.size() || source[i] == '\n') {
            out.push_back(cur);
            cur.clear();
            continue;
        }
        cur.push_back(source[i]);
    }
    if (out.empty()) out.push_back("");
    return out;
}

}  // namespace hanabi::display_math
