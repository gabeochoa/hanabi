#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace api::agentcloud_hosts {

inline constexpr const char* kStandardOrchestratorHost =
    "agentcloud-orchestrator-prod.playground.x2p.facebook.net";
inline constexpr const char* kRcOrchestratorHost =
    "agentcloud-orchestrator-rc.playground.x2p.facebook.net";
inline constexpr const char* kVipDomain = "mm.internalmeta.com";
inline constexpr const char* kWebOrigin = "agentcloud.internalmeta.com";

inline std::string canonical_host(std::string_view host) {
    std::string out;
    out.reserve(host.size());
    for (char c : host) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (!out.empty() && out.front() == '[') return out;
    const auto colon = out.find(':');
    if (colon != std::string::npos && out.find(':', colon + 1) == std::string::npos)
        out.erase(colon);
    return out;
}

inline bool is_standard_orchestrator(std::string_view host) {
    const std::string h = canonical_host(host);
    return !h.empty() &&
           (h == kStandardOrchestratorHost || h == kRcOrchestratorHost || h == kVipDomain);
}

}
