#pragma once

// A code line longer than its block wraps only BETWEEN tokens (the reference,
// 2026-10-05, D123368715: "code in a message now wraps only between tokens").
// Before, Hanabi did not wrap a fence at all: a long line ran past the bubble
// and was cut off. Pure.
//
// Columns are code points (the code font is fixed-width). A break goes at the
// last token boundary that fits -- after a space, or where an identifier
// character meets anything else, preferring after a space; never in front of
// a space or a closing mark (, ; ) ] } :) -- so "compute_total(alpha," never becomes
// "compute_to" / "tal(alpha,". Only a token longer than the whole line is cut
// inside itself, at the column limit, on a code-point boundary. Nothing is
// dropped or added: the pieces concatenate back to the line.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hanabi::code_wrap {

inline bool is_cont(unsigned char c) { return (c & 0xC0) == 0x80; }

// Identifier characters: ASCII letters, digits, '_', and any non-ASCII code
// point (a word in another script is one token too).
inline bool ident(unsigned char c) {
    return c >= 0x80 || c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

inline bool closer(unsigned char c) {
    return c == ' ' || c == ',' || c == ';' || c == ')' || c == ']' || c == '}' || c == ':';
}

inline std::vector<std::string> wrap(std::string_view line, std::size_t maxCols) {
    std::vector<std::string> out;
    if (maxCols == 0) maxCols = 1;
    std::size_t pos = 0;
    while (true) {
        // Walk up to maxCols code points from pos, remembering the last
        // boundary (a byte offset where a break may go) and the last one
        // right after a space, which reads best when it is not too early.
        std::size_t i = pos, cols = 0;
        std::size_t lastBreak = std::string_view::npos, lastSpace = std::string_view::npos, spaceCols = 0;
        while (i < line.size() && cols < maxCols) {
            std::size_t j = i + 1;
            while (j < line.size() && is_cont(static_cast<unsigned char>(line[j]))) ++j;
            ++cols;
            if (j < line.size()) {
                const unsigned char a = static_cast<unsigned char>(line[i]);
                const unsigned char b = static_cast<unsigned char>(line[j]);
                // A row never starts with a space or a closing mark.
                if (!closer(b) && (a == ' ' || !(ident(a) && ident(b)))) {
                    lastBreak = j;
                    if (a == ' ') {
                        lastSpace = j;
                        spaceCols = cols;
                    }
                }
            }
            i = j;
        }
        if (i >= line.size()) {
            out.emplace_back(line.substr(pos));
            return out;
        }
        std::size_t cut = i;
        if (lastSpace != std::string_view::npos && lastSpace > pos && spaceCols * 2 >= maxCols)
            cut = lastSpace;
        else if (lastBreak != std::string_view::npos && lastBreak > pos)
            cut = lastBreak;
        out.emplace_back(line.substr(pos, cut - pos));
        pos = cut;
    }
}

inline std::size_t count(std::string_view line, std::size_t maxCols) {
    return wrap(line, maxCols).size();
}

}  // namespace hanabi::code_wrap
