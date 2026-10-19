#pragma once
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hanabi::links {

struct Link {
    size_t off = 0;
    size_t len = 0;
    std::string id;
};

inline bool is_prefix(char c) { return c == 'D' || c == 'T' || c == 'S'; }

inline constexpr size_t kMinDigits = 3;

inline bool binds(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '/' ||
           c == '_' || c == '.' || c == '-';
}

inline constexpr std::string_view kProjectPrefix = "PRJ";
inline constexpr size_t kProjectMinDigits = 4;

inline std::vector<Link> find(const std::string& text) {
    std::vector<Link> out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text.compare(i, kProjectPrefix.size(), kProjectPrefix) == 0 && !(i > 0 && binds(text[i - 1]))) {
            size_t j = i + kProjectPrefix.size();
            while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j])) != 0) ++j;
            const size_t digits = j - i - kProjectPrefix.size();
            if (digits >= kProjectMinDigits &&
                !(j < text.size() && std::isalnum(static_cast<unsigned char>(text[j])) != 0)) {
                out.push_back(Link{i, j - i, text.substr(i, j - i)});
                i = j - 1;
                continue;
            }
        }
        if (!is_prefix(text[i])) continue;
        if (i > 0 && binds(text[i - 1])) continue;
        size_t j = i + 1;
        while (j < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[j])) != 0)
            ++j;
        const size_t digits = j - i - 1;
        if (digits < kMinDigits) continue;
        if (j < text.size() &&
            std::isalnum(static_cast<unsigned char>(text[j])) != 0)
            continue;
        out.push_back(Link{i, j - i, text.substr(i, j - i)});
        i = j - 1;
    }
    return out;
}

}  // namespace hanabi::links
