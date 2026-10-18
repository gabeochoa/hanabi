#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "../../vendor/nlohmann/json.hpp"

namespace api::drafts {

inline constexpr const char* kPath = "/api/inbox/drafts";

struct ServerDraft {
    std::string id;
    std::string body;
    std::string session_id;
    std::string updated_at;
    bool operator==(const ServerDraft&) const = default;
};

struct Page {
    std::vector<ServerDraft> drafts;
    int limit = 0;
};

inline std::optional<Page> parse(const std::string& body) {
    const nlohmann::json root = nlohmann::json::parse(body, nullptr, false);
    if (root.is_discarded() || !root.is_object()) return std::nullopt;
    if (!root.contains("drafts") || !root["drafts"].is_array()) return std::nullopt;
    Page page;
    if (root.contains("limit") && root["limit"].is_number_integer()) page.limit = root["limit"].get<int>();
    for (const nlohmann::json& d : root["drafts"]) {
        if (!d.is_object()) continue;
        ServerDraft row;
        if (d.contains("id") && d["id"].is_string()) row.id = d["id"].get<std::string>();
        if (d.contains("body") && d["body"].is_string()) row.body = d["body"].get<std::string>();
        if (d.contains("sessionId") && d["sessionId"].is_string()) row.session_id = d["sessionId"].get<std::string>();
        if (d.contains("updatedAt") && d["updatedAt"].is_string()) row.updated_at = d["updatedAt"].get<std::string>();
        if (row.id.empty() || row.body.empty()) continue;
        page.drafts.push_back(std::move(row));
    }
    return page;
}

inline std::string first_line(const std::string& body, std::size_t max = 72) {
    std::size_t b = 0;
    while (b < body.size() && (body[b] == ' ' || body[b] == '\n' || body[b] == '\r' || body[b] == '\t')) ++b;
    std::size_t e = body.find('\n', b);
    if (e == std::string::npos) e = body.size();
    std::string line = body.substr(b, e - b);
    if (line.size() > max) {
        std::size_t cut = max;
        while (cut > 0 && (static_cast<unsigned char>(line[cut]) & 0xC0) == 0x80) --cut;
        line.resize(cut);
        line += "\xe2\x80\xa6";
    }
    return line;
}

inline std::string status_note(int status) {
    switch (status) {
        case 200: return "";
        case 401:
        case 403: return "the web app did not accept this Mac's sign-in";
        case 404: return "the web app has no saved drafts route";
        default: return "the web app answered " + std::to_string(status);
    }
}

}
