#pragma once

#include <algorithm>
#include <branding.h>
#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "../api/types.h"

namespace hanabi::spotlight {

inline constexpr std::size_t kMaxCatalogItems = 2000;
inline constexpr std::size_t kMaxTitleBytes = 240;
inline constexpr std::size_t kMaxPreviewBytes = 600;

struct Item {
    std::string id;
    std::string title;
    std::string preview;
    std::string url;
    int64_t updated_at = 0;
};

inline std::string utf8_prefix(std::string value, std::size_t limit) {
    if (value.size() <= limit) return value;
    std::size_t cut = limit;
    while (cut > 0 && (static_cast<unsigned char>(value[cut]) & 0xc0u) == 0x80u)
        --cut;
    value.resize(cut);
    return value;
}

inline std::string path_segment(const std::string& value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value) {
        const bool safe =
            std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
        if (safe) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0f]);
        }
    }
    return out;
}

inline std::vector<Item> make_catalog(
    const std::vector<api::SessionSummary>& sessions) {
    std::vector<const api::SessionSummary*> ordered;
    ordered.reserve(sessions.size());
    for (const auto& session : sessions)
        if (!session.id.empty()) ordered.push_back(&session);
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto* a, const auto* b) {
                         return a->updated_at > b->updated_at;
                     });

    std::vector<Item> result;
    result.reserve(std::min(ordered.size(), kMaxCatalogItems));
    std::unordered_set<std::string> seen;
    seen.reserve(result.capacity());
    for (const auto* session : ordered) {
        if (result.size() == kMaxCatalogItems) break;
        if (!seen.insert(session->id).second) continue;
        Item item;
        item.id = session->id;
        item.title = utf8_prefix(
            session->title.empty() ? "Untitled thread" : session->title,
            kMaxTitleBytes);
        item.preview = utf8_prefix(session->preview, kMaxPreviewBytes);
        item.url = std::string(product_branding::kUrlScheme) + "://thread/" +
                   path_segment(session->id);
        item.updated_at = session->updated_at;
        result.push_back(std::move(item));
    }
    return result;
}

inline std::size_t signature(const std::vector<Item>& items) {
    std::size_t value = items.size() * 1000003u;
    for (const auto& item : items) {
        value = value * 1000003u + std::hash<std::string>{}(item.id);
        value = value * 1000003u + std::hash<std::string>{}(item.title);
        value = value * 1000003u + std::hash<std::string>{}(item.preview);
        value = value * 1000003u + std::hash<std::string>{}(item.url);
    }
    return value;
}

}  // namespace hanabi::spotlight
