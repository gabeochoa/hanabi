#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hanabi::reply_quote {

inline constexpr std::size_t kOpenerCap = 56;
inline constexpr std::string_view kMarker = "> ";
inline constexpr std::string_view kSeparator = "\n\n";
inline constexpr std::string_view kEllipsis = "\xe2\x80\xa6";
inline constexpr std::string_view kHint = "Quote this message in the composer";

inline std::size_t sequence_length(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

inline std::uint32_t code_point_at(std::string_view s, std::size_t at,
                                   std::size_t* length) {
    const auto lead = static_cast<unsigned char>(s[at]);
    std::size_t len = sequence_length(lead);
    if (at + len > s.size()) len = 1;
    *length = len;
    if (len == 1) return lead;
    std::uint32_t cp = lead & (0xFFu >> (len + 1));
    for (std::size_t i = 1; i < len; ++i)
        cp = (cp << 6) | (static_cast<unsigned char>(s[at + i]) & 0x3Fu);
    return cp;
}

inline bool is_inkless(std::uint32_t cp) {
    switch (cp) {
        case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x20:
        case 0x85: case 0xA0: case 0x1680: case 0x2028: case 0x2029:
        case 0x202F: case 0x205F: case 0x3000: case 0x200B:
            return true;
        default:
            return cp >= 0x2000 && cp <= 0x200A;
    }
}

inline bool has_ink(std::string_view text) {
    for (std::size_t at = 0; at < text.size();) {
        std::size_t len = 0;
        const std::uint32_t cp = code_point_at(text, at, &len);
        if (!is_inkless(cp)) return true;
        at += len;
    }
    return false;
}

inline bool is_offered(std::string_view text, bool has_attachments) {
    return has_attachments || has_ink(text);
}

inline std::string trimmed(std::string_view line) {
    std::size_t begin = 0;
    while (begin < line.size()) {
        std::size_t len = 0;
        if (!is_inkless(code_point_at(line, begin, &len))) break;
        begin += len;
    }
    std::size_t end = line.size();
    while (end > begin) {
        std::size_t back = end - 1;
        while (back > begin &&
               (static_cast<unsigned char>(line[back]) & 0xC0) == 0x80)
            --back;
        std::size_t len = 0;
        if (!is_inkless(code_point_at(line, back, &len))) break;
        end = back;
    }
    return std::string(line.substr(begin, end - begin));
}

inline std::string collapsing_whitespace(std::string_view line) {
    std::string out;
    out.reserve(line.size());
    bool pending_space = false;
    for (std::size_t at = 0; at < line.size();) {
        std::size_t len = 0;
        const std::uint32_t cp = code_point_at(line, at, &len);
        if (is_inkless(cp)) {
            pending_space = !out.empty();
            at += len;
            continue;
        }
        if (pending_space) {
            out.push_back(' ');
            pending_space = false;
        }
        out.append(line.substr(at, len));
        at += len;
    }
    return out;
}

inline bool is_rule(std::string_view line) {
    std::string bare;
    for (char c : line)
        if (c != ' ') bare.push_back(c);
    if (bare.size() < 3) return false;
    const char first = bare.front();
    if (first != '-' && first != '*' && first != '_') return false;
    for (char c : bare)
        if (c != first) return false;
    return true;
}

inline std::optional<std::size_t> heading_prefix(std::string_view line) {
    std::size_t hashes = 0;
    while (hashes < line.size() && line[hashes] == '#') ++hashes;
    if (hashes < 1 || hashes > 6) return std::nullopt;
    if (hashes >= line.size()) return std::nullopt;
    const char after = line[hashes];
    if (after != ' ' && after != '\t') return std::nullopt;
    return hashes;
}

inline std::optional<std::size_t> bullet_prefix(std::string_view line) {
    if (line.empty()) return std::nullopt;
    const char first = line.front();
    if (first == '-' || first == '*' || first == '+') {
        if (line.size() > 1 && line[1] == ' ') return std::size_t{1};
        return std::nullopt;
    }
    std::size_t digits = 0;
    while (digits < line.size() && line[digits] >= '0' && line[digits] <= '9')
        ++digits;
    if (digits < 1 || digits >= line.size()) return std::nullopt;
    const char delimiter = line[digits];
    if (delimiter != '.' && delimiter != ')') return std::nullopt;
    if (digits + 1 >= line.size() || line[digits + 1] != ' ')
        return std::nullopt;
    return digits + 1;
}

inline std::string without_block_markers(std::string_view line) {
    std::string_view rest = line;
    bool peeled = true;
    while (peeled) {
        peeled = false;
        if (rest.starts_with(">")) {
            rest.remove_prefix(1);
            peeled = true;
        } else if (const auto heading = heading_prefix(rest)) {
            rest.remove_prefix(*heading);
            peeled = true;
        } else if (const auto bullet = bullet_prefix(rest)) {
            rest.remove_prefix(*bullet);
            peeled = true;
        } else if (rest.starts_with("|")) {
            rest.remove_prefix(1);
            peeled = true;
        }
        while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
            rest.remove_prefix(1);
    }
    const std::string collapsed = collapsing_whitespace(rest);
    return is_rule(collapsed) ? std::string() : collapsed;
}

inline std::string bounded(std::string_view line) {
    std::vector<std::size_t> starts;
    for (std::size_t at = 0; at < line.size();) {
        std::size_t len = 0;
        code_point_at(line, at, &len);
        starts.push_back(at);
        at += len;
    }
    if (starts.size() <= kOpenerCap) return std::string(line);

    const std::size_t head_bytes = starts[kOpenerCap];
    const std::string_view head = line.substr(0, head_bytes);
    std::string_view cut = head;
    const std::size_t space = head.find_last_of(' ');
    if (space != std::string_view::npos) {
        std::size_t chars_before = 0;
        while (chars_before < starts.size() && starts[chars_before] < space)
            ++chars_before;
        if (chars_before >= kOpenerCap / 2) cut = head.substr(0, space);
    }
    return trimmed(cut) + std::string(kEllipsis);
}

template <typename Take>
inline std::optional<std::string> first_line(std::string_view text,
                                             Take take) {
    std::string line;
    for (char c : text) {
        if (c == '\n') {
            if (auto answer = take(line)) return answer;
            line.clear();
            continue;
        }
        line.push_back(c);
    }
    return take(line);
}

inline bool opens_fence(std::string_view line) {
    return line.starts_with("```") || line.starts_with("~~~");
}

inline std::optional<std::string> first_rendered_line(std::string_view text) {
    bool inside_fence = false;
    return first_line(
        text, [&inside_fence](std::string_view raw)
                  -> std::optional<std::string> {
            const std::string line = trimmed(raw);
            if (opens_fence(line)) {
                inside_fence = !inside_fence;
                return std::nullopt;
            }
            if (inside_fence || line.empty()) return std::nullopt;
            const std::string stripped = without_block_markers(line);
            if (stripped.empty()) return std::nullopt;
            return stripped;
        });
}

inline std::optional<std::string> first_ink_line(std::string_view text) {
    return first_line(text,
                      [](std::string_view raw) -> std::optional<std::string> {
                          const std::string line = trimmed(raw);
                          if (line.empty() || opens_fence(line))
                              return std::nullopt;
                          const std::string collapsed =
                              collapsing_whitespace(line);
                          if (collapsed.empty()) return std::nullopt;
                          return collapsed;
                      });
}

inline std::optional<std::string> first_non_empty_line(std::string_view text) {
    return first_line(text,
                      [](std::string_view raw) -> std::optional<std::string> {
                          const std::string collapsed =
                              collapsing_whitespace(trimmed(raw));
                          if (collapsed.empty()) return std::nullopt;
                          return collapsed;
                      });
}

inline std::optional<std::string> opener(std::string_view text) {
    std::optional<std::string> line = first_rendered_line(text);
    if (!line) line = first_ink_line(text);
    if (!line) line = first_non_empty_line(text);
    if (!line) return std::nullopt;
    return bounded(*line);
}

inline std::optional<std::string> quoted_line(
    std::string_view text, const std::vector<std::string>& attachments) {
    if (auto open = opener(text)) return open;
    std::string handles;
    for (const std::string& name : attachments) {
        if (name.empty()) continue;
        if (!handles.empty()) handles += ", ";
        handles += name;
    }
    if (handles.empty()) return std::nullopt;
    return bounded(handles);
}

inline std::optional<std::string> seed(
    std::string_view text, const std::vector<std::string>& attachments = {}) {
    const auto line = quoted_line(text, attachments);
    if (!line) return std::nullopt;
    return std::string(kMarker) + *line + std::string(kSeparator);
}

}  // namespace hanabi::reply_quote
