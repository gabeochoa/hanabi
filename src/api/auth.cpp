#include "auth.h"

#include "../../vendor/nlohmann/json.hpp"

namespace api {

using json = nlohmann::json;

namespace {
// Read a string field that a backend might send as a string or (defensively)
// a number. Returns "" when absent.
std::string as_string(const json& obj, const std::string& key) {
    if (!obj.is_object() || !obj.contains(key)) return "";
    const auto& v = obj.at(key);
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_integer()) return std::to_string(v.get<int64_t>());
    return "";
}

// Read an integer field (seconds); backends sometimes send it as a string.
int64_t as_int(const json& obj, const std::string& key, int64_t fallback) {
    if (!obj.is_object() || !obj.contains(key)) return fallback;
    const auto& v = obj.at(key);
    if (v.is_number_integer()) return v.get<int64_t>();
    if (v.is_number_float()) return static_cast<int64_t>(v.get<double>());
    if (v.is_string()) {
        try {
            return std::stoll(v.get<std::string>());
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}
}  // namespace

std::string DeviceCodeFlow::device_request_body() const {
    json b = json::object();
    if (!cfg_.auth_client_id.empty()) b["client_id"] = cfg_.auth_client_id;
    if (!cfg_.auth_scope.empty()) b["scope"] = cfg_.auth_scope;
    return b.dump();
}

std::string DeviceCodeFlow::token_request_body() const {
    json b = json::object();
    // Generic RFC 8628 grant type + the device code we were issued. The field
    // NAME the backend expects for the device code is configurable; the value
    // is what step 1 returned.
    b["grant_type"] = "urn:ietf:params:oauth:grant-type:device_code";
    b[cfg_.field_device_code] = device_code_;
    if (!cfg_.auth_client_id.empty()) b["client_id"] = cfg_.auth_client_id;
    return b.dump();
}

DeviceCodeFlow::State DeviceCodeFlow::begin(int64_t now) {
    if (!cfg_.auth_ready()) {
        state_ = State::Failed;
        error_ = "auth not configured";
        return state_;
    }
    state_ = State::RequestingCode;
    AuthResponse res = transport_(cfg_.auth_device_path, device_request_body());
    if (!res.ok) {
        state_ = State::Failed;
        error_ = res.error.empty() ? "device-code request failed" : res.error;
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

    device_code_ = as_string(j, cfg_.field_device_code);
    user_code_ = as_string(j, cfg_.field_user_code);
    verification_uri_ = as_string(j, cfg_.field_verification_uri);
    interval_ = as_int(j, cfg_.field_interval, 5);
    if (interval_ < 1) interval_ = 1;
    const int64_t expires_in = as_int(j, cfg_.field_expires_in, 0);
    deadline_ = (expires_in > 0) ? now + expires_in : 0;
    next_poll_at_ = now + interval_;

    if (device_code_.empty() || user_code_.empty()) {
        state_ = State::Failed;
        error_ = "device-code response missing required fields";
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
    AuthResponse res = transport_(cfg_.auth_token_path, token_request_body());
    if (!res.ok) {
        // Transport-level failure: treat as a hard failure so the user can
        // retry / fall back rather than spinning forever.
        state_ = State::Failed;
        error_ = res.error.empty() ? "token poll failed" : res.error;
        return state_;
    }

    json j;
    try {
        j = json::parse(res.body);
    } catch (...) {
        state_ = State::Failed;
        error_ = "malformed token response";
        return state_;
    }

    // Success: a token is present.
    const std::string tok = as_string(j, cfg_.field_access_token);
    if (!tok.empty()) {
        token_ = tok;
        refresh_token_ = as_string(j, cfg_.field_refresh_token);
        state_ = State::Success;
        error_.clear();
        return state_;
    }

    // Pending sentinel: keep waiting.
    const std::string err = as_string(j, cfg_.field_auth_error);
    if (err == cfg_.auth_pending_value) {
        state_ = State::AwaitingUser;  // still waiting on the user
        return state_;
    }

    // Any other error is a hard failure (e.g. access_denied, invalid_grant).
    state_ = State::Failed;
    error_ = err.empty() ? "authorization failed" : err;
    return state_;
}

}  // namespace api
