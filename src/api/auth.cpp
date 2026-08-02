#include "auth.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <random>

#include "../../vendor/nlohmann/json.hpp"

namespace api {

using json = nlohmann::json;

namespace {
// Read a string field; returns "" when absent. Defensive against a numeric.
std::string as_string(const json& obj, const std::string& key) {
    if (!obj.is_object() || !obj.contains(key)) return "";
    const auto& v = obj.at(key);
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_integer()) return std::to_string(v.get<int64_t>());
    return "";
}
}  // namespace

// RFC-4122 v4 UUID via std::random_device (no external dependency). 32 hex
// digits with the version (4) and variant (10xx) bits set, formatted
// 8-4-4-4-12.
std::string DeviceCodeFlow::make_uuid_v4() {
    std::random_device rd;
    std::mt19937_64 gen((static_cast<uint64_t>(rd()) << 32) ^ rd());
    std::uniform_int_distribution<uint32_t> dist(0, 255);
    std::array<uint8_t, 16> b{};
    for (auto& x : b) x = static_cast<uint8_t>(dist(gen));
    b[6] = static_cast<uint8_t>((b[6] & 0x0F) | 0x40);  // version 4
    b[8] = static_cast<uint8_t>((b[8] & 0x3F) | 0x80);  // variant 10xx
    char out[37];
    std::snprintf(
        out, sizeof(out),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10],
        b[11], b[12], b[13], b[14], b[15]);
    return std::string(out);
}

std::string DeviceCodeFlow::device_request_body() const {
    json b = json::object();
    // The client mints its own device code (already stored in device_code_);
    // the body carries it plus the client type. No client_id/secret exists.
    b[cfg_.field_device_code] = device_code_;
    b[cfg_.field_client_type] = cfg_.auth_client_type;
    return b.dump();
}

std::string DeviceCodeFlow::poll_query() const {
    // GET {auth_token_path}?<field_poll_query>=<deviceCode>. The deviceCode is
    // a UUID so needs no percent-encoding; keep it simple + explicit.
    return cfg_.field_poll_query + "=" + device_code_;
}

DeviceCodeFlow::State DeviceCodeFlow::begin(int64_t now) {
    if (!cfg_.auth_ready()) {
        state_ = State::Failed;
        error_ = "auth not configured";
        return state_;
    }
    // Mint the device code the CLIENT owns for this flow.
    device_code_ = make_uuid_v4();

    state_ = State::RequestingCode;
    AuthResponse res =
        transport_("POST", cfg_.auth_device_path, "", device_request_body());
    if (!res.ok) {
        state_ = State::Failed;
        error_ = res.error.empty() ? "device-code request failed" : res.error;
        return state_;
    }
    // A non-2xx on the code request is a hard failure (400 = bad deviceCode).
    if (res.status != 0 && (res.status < 200 || res.status >= 300)) {
        state_ = State::Failed;
        error_ = "device-code request failed (http " +
                 std::to_string(res.status) + ")";
        return state_;
    }
    json j;
    try {
        j = json::parse(res.body);
    } catch (...) {
        state_ = State::Failed;
        error_ = "malformed device-code response";
        return state_;
    }

    user_code_ = as_string(j, cfg_.field_user_code);
    verification_uri_ = as_string(j, cfg_.field_auth_url);

    interval_ = cfg_.auth_poll_interval > 0 ? cfg_.auth_poll_interval : 2;
    deadline_ = (cfg_.auth_expires_in > 0) ? now + cfg_.auth_expires_in : 0;
    next_poll_at_ = now + interval_;

    if (user_code_.empty()) {
        state_ = State::Failed;
        error_ = "device-code response missing user code";
        return state_;
    }
    state_ = State::AwaitingUser;
    return state_;
}

DeviceCodeFlow::State DeviceCodeFlow::poll_step(int64_t now) {
    if (state_ != State::AwaitingUser && state_ != State::Polling)
        return state_;

    // Expiry check first: past the deadline, the device code is dead.
    if (deadline_ > 0 && now >= deadline_) {
        state_ = State::Expired;
        error_ = "device code expired";
        return state_;
    }

    // Rate-limit: only poll once per interval.
    if (now < next_poll_at_) return state_;
    next_poll_at_ = now + interval_;

    state_ = State::Polling;
    AuthResponse res = transport_("GET", cfg_.auth_token_path, poll_query(), "");
    if (!res.ok) {
        // Transport-level failure: treat as a hard failure so the user can
        // retry / fall back rather than spinning forever.
        state_ = State::Failed;
        error_ = res.error.empty() ? "token poll failed" : res.error;
        return state_;
    }
    // A non-2xx poll is a hard failure.
    if (res.status != 0 && (res.status < 200 || res.status >= 300)) {
        state_ = State::Failed;
        error_ = "poll failed (http " + std::to_string(res.status) + ")";
        return state_;
    }

    json j;
    try {
        j = json::parse(res.body);
    } catch (...) {
        state_ = State::Failed;
        error_ = "malformed poll response";
        return state_;
    }

    const std::string status = as_string(j, cfg_.field_auth_status);
    if (status == cfg_.auth_status_authorized) {
        const std::string tok = as_string(j, cfg_.field_token);
        if (tok.empty()) {
            state_ = State::Failed;
            error_ = "authorized response missing token";
            return state_;
        }
        token_ = tok;
        // Capture the refresh token if the backend returned one (optional) so
        // refresh() can renew the ~30-day bearer without re-running the flow.
        refresh_token_ = as_string(j, cfg_.field_refresh_token);
        state_ = State::Success;
        error_.clear();
        return state_;
    }
    if (status == cfg_.auth_status_pending) {
        state_ = State::AwaitingUser;  // still waiting on the user
        return state_;
    }

    // Any other status is a hard failure (denied / expired server-side / etc).
    state_ = State::Failed;
    error_ = status.empty() ? "authorization failed"
                            : ("authorization " + status);
    return state_;
}

// Renew the bearer using the stored refresh token (POST auth_refresh_path with
// {field_refresh_token: <refresh_token_>}). Best-effort: on success updates
// token_ (and rotates refresh_token_ if the response carries a new one) and
// returns true; on any failure returns false and leaves state untouched so the
// caller can fall back to re-running the device-code flow. Does NOT change the
// flow State (refresh is orthogonal to the login state machine).
bool DeviceCodeFlow::refresh(int64_t /*now*/) {
    if (cfg_.auth_refresh_path.empty() || refresh_token_.empty()) return false;
    json body;
    body[cfg_.field_refresh_token] = refresh_token_;
    AuthResponse res =
        transport_("POST", cfg_.auth_refresh_path, "", body.dump());
    if (!res.ok) return false;
    if (res.status != 0 && (res.status < 200 || res.status >= 300)) return false;
    json j;
    try {
        j = json::parse(res.body);
    } catch (...) {
        return false;
    }
    const std::string tok = as_string(j, cfg_.field_token);
    if (tok.empty()) return false;
    token_ = tok;
    // Rotate the refresh token if the backend issued a new one.
    const std::string rt = as_string(j, cfg_.field_refresh_token);
    if (!rt.empty()) refresh_token_ = rt;
    return true;
}

}  // namespace api
