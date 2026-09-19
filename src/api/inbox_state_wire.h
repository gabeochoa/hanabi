#pragma once

#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "../../vendor/nlohmann/json.hpp"

namespace api::inbox_state {

inline constexpr const char* kRoutePath = "/api/inbox/state";
inline constexpr const char* kCsrfHeader = "x-agentcloud-csrf";
inline constexpr const char* kCsrfHeaderValue = "1";
inline constexpr const char* kAuthCookieName = "intern_cat_token";
inline constexpr const char* kCatVerifier =
    "INTERN_CONTROLLER:XInternNestGraphQLThriftController";
inline constexpr std::size_t kMaxSessionIdLength = 255;

using Seconds = std::int64_t;
using json = nlohmann::json;

struct Snapshot {
    std::set<std::string> read;
    std::set<std::string> archived;
    std::set<std::string> starred;
    std::map<std::string, Seconds> last_seen_at;
    std::map<std::string, Seconds> snoozed_until;
    std::map<std::string, Seconds> snoozed_at;
    std::map<std::string, std::vector<std::string>> labels;
    bool operator==(const Snapshot&) const = default;
};

inline std::optional<Seconds> seconds_of(const json& value) {
    if (value.is_number_integer()) return value.get<std::int64_t>();
    if (value.is_number_unsigned()) {
        const std::uint64_t u = value.get<std::uint64_t>();
        if (u > static_cast<std::uint64_t>(INT64_MAX)) return std::nullopt;
        return static_cast<std::int64_t>(u);
    }
    if (value.is_number_float()) {
        const double d = value.get<double>();
        if (!std::isfinite(d) || d >= 9223372036854775808.0 || d < -9223372036854775808.0)
            return std::nullopt;
        return static_cast<std::int64_t>(std::trunc(d));
    }
    return std::nullopt;
}

inline std::set<std::string> ids_of(const json& parent, const char* key) {
    std::set<std::string> out;
    if (!parent.contains(key) || !parent.at(key).is_array()) return out;
    for (const json& item : parent.at(key))
        if (item.is_string() && !item.get_ref<const std::string&>().empty())
            out.insert(item.get<std::string>());
    return out;
}

inline std::optional<Snapshot> parse_snapshot(std::string_view body) {
    const json root = json::parse(body, nullptr, false);
    if (root.is_discarded() || !root.is_object()) return std::nullopt;
    static constexpr const char* kSuccessKeys[] = {"read", "archived", "starred", "lastSeenAt",
                                                   "snoozes"};
    bool success_shape = false;
    for (const char* key : kSuccessKeys) success_shape = success_shape || root.contains(key);
    if (!success_shape) return std::nullopt;

    Snapshot out;
    out.read = ids_of(root, "read");
    out.archived = ids_of(root, "archived");
    out.starred = ids_of(root, "starred");
    if (root.contains("lastSeenAt") && root.at("lastSeenAt").is_object())
        for (const auto& [id, value] : root.at("lastSeenAt").items())
            if (const auto sec = seconds_of(value)) out.last_seen_at[id] = *sec;
    if (root.contains("snoozes") && root.at("snoozes").is_object()) {
        for (const auto& [id, entry] : root.at("snoozes").items()) {
            if (!entry.is_object()) continue;
            if (entry.contains("snoozedUntil"))
                if (const auto until = seconds_of(entry.at("snoozedUntil")))
                    out.snoozed_until[id] = *until;
            if (entry.contains("snoozedAt"))
                if (const auto at = seconds_of(entry.at("snoozedAt"))) out.snoozed_at[id] = *at;
        }
    }
    if (root.contains("labels") && root.at("labels").is_object()) {
        for (const auto& [id, names] : root.at("labels").items()) {
            if (!names.is_array()) continue;
            std::vector<std::string> usable;
            for (const json& name : names)
                if (name.is_string() && !name.get_ref<const std::string&>().empty())
                    usable.push_back(name.get<std::string>());
            if (!usable.empty()) out.labels[id] = std::move(usable);
        }
    }
    return out;
}

inline bool is_valid_session_id(std::string_view id) {
    return !id.empty() && id.size() <= kMaxSessionIdLength;
}

using SnoozeField = std::optional<std::optional<Seconds>>;

struct Patch {
    std::optional<bool> archived;
    std::optional<bool> starred;
    std::optional<bool> read;
    SnoozeField snoozed_until;

    [[nodiscard]] bool empty() const {
        return !archived && !starred && !read && !snoozed_until;
    }
};

inline std::optional<std::string> body_for(std::string_view session_id, const Patch& patch) {
    if (!is_valid_session_id(session_id) || patch.empty()) return std::nullopt;
    json body = {{"sessionId", std::string(session_id)}};
    if (patch.archived) body["archived"] = *patch.archived;
    if (patch.starred) body["starred"] = *patch.starred;
    if (patch.read) body["read"] = *patch.read;
    if (patch.snoozed_until) {
        if (*patch.snoozed_until) body["snoozedUntil"] = **patch.snoozed_until;
        else body["snoozedUntil"] = nullptr;
    }
    return body.dump();
}

struct Echo {
    bool ok = false;
    std::optional<Seconds> last_seen_at;
    SnoozeField snoozed_at;
    SnoozeField snoozed_until;
    bool operator==(const Echo&) const = default;
};

inline SnoozeField nullable_seconds(const json& parent, const char* key) {
    if (!parent.contains(key)) return std::nullopt;
    const json& value = parent.at(key);
    if (value.is_null()) return std::optional<Seconds>{};
    if (const auto sec = seconds_of(value)) return std::optional<Seconds>{*sec};
    return std::nullopt;
}

inline std::optional<Echo> parse_echo(std::string_view body) {
    const json root = json::parse(body, nullptr, false);
    if (root.is_discarded() || !root.is_object()) return std::nullopt;
    Echo out;
    out.ok = root.contains("ok") && root.at("ok").is_boolean() && root.at("ok").get<bool>();
    if (root.contains("lastSeenAt"))
        if (const auto stamp = seconds_of(root.at("lastSeenAt")); stamp && *stamp > 0)
            out.last_seen_at = stamp;
    out.snoozed_at = nullable_seconds(root, "snoozedAt");
    out.snoozed_until = nullable_seconds(root, "snoozedUntil");
    return out;
}

enum class Confirmation { Confirmed, Cleared, Unconfirmed };

struct SnoozeConfirmation {
    Confirmation kind = Confirmation::Unconfirmed;
    std::optional<Seconds> snoozed_at;
    std::optional<Seconds> snoozed_until;
    bool operator==(const SnoozeConfirmation&) const = default;
};

inline SnoozeConfirmation confirm_snooze(const Echo& echo, std::optional<Seconds> requested_until) {
    if (!echo.ok || !echo.snoozed_at || !echo.snoozed_until) return {};
    const std::optional<Seconds>& at = *echo.snoozed_at;
    const std::optional<Seconds>& until = *echo.snoozed_until;
    if (!requested_until) {
        if (!at && !until) return {Confirmation::Cleared, std::nullopt, std::nullopt};
        return {};
    }
    if (at && until && *until == *requested_until) return {Confirmation::Confirmed, at, until};
    return {};
}

}  // namespace api::inbox_state
