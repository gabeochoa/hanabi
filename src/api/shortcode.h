#pragma once

// A session SHORTCODE (`ACS1042`), the alias a person shares for a thread, and
// the read that turns one back into a session id (the reference's
// SessionShortcode, kt-qvid / kt-8a2f). A link to `<web app>/ACS1042` used to
// open the browser even for a thread this Mac holds: the socket has no resolve
// verb, but the web app resolves a code over the intern graph --
// `xfb_agentcloud_catalog_session_from_reference` -- as the viewer, which is the
// same GraphQL route the Companion already reads. Pure.

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#include "../../vendor/nlohmann/json.hpp"

namespace api::shortcode {

inline constexpr std::string_view kPrefix = "ACS";
inline constexpr std::size_t kMaxDigits = 19;  // a TAO-only code is its int64 fbid

// The canonical spelling of a shortcode-shaped string, or nothing. Syntax only,
// like the web's SHORTCODE_PATTERN `/^ACS\d{1,19}$/i`: the prefix in either
// ASCII case, then one to nineteen ASCII digits. Whether it names a session is
// the server's answer.
inline std::optional<std::string> normalized(std::string_view t) {
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.remove_prefix(1);
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.remove_suffix(1);
    if (t.size() <= kPrefix.size() || t.size() > kPrefix.size() + kMaxDigits) return std::nullopt;
    for (std::size_t i = 0; i < kPrefix.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(t[i]);
        if (c >= 0x80 || std::toupper(c) != kPrefix[i]) return std::nullopt;
    }
    for (std::size_t i = kPrefix.size(); i < t.size(); ++i)
        if (t[i] < '0' || t[i] > '9') return std::nullopt;
    return std::string(kPrefix) + std::string(t.substr(kPrefix.size()));
}

// The web's query, field for field.
inline constexpr const char* kDoc =
    "query HanabiSessionFromShortcode($reference: String!) { "
    "xfb_agentcloud_catalog_session_from_reference(reference: $reference) { session_id } }";

inline std::string body(const std::string& code) {
    return nlohmann::json{{"query_text", kDoc},
                          {"variables", nlohmann::json{{"reference", code}}.dump()}}
        .dump();
}

// The session id in a reply's `data`, or "". A null node is the server's one
// answer for both "does not exist" and "not yours", and stays so.
inline std::string session_id(const nlohmann::json& data) {
    if (!data.is_object()) return {};
    const auto it = data.find("xfb_agentcloud_catalog_session_from_reference");
    if (it == data.end() || !it->is_object()) return {};
    const auto id = it->find("session_id");
    if (id == it->end() || !id->is_string()) return {};
    return id->get<std::string>();
}

}  // namespace api::shortcode
