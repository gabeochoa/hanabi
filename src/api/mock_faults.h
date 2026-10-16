#pragma once

#include <cstdlib>
#include <mutex>
#include <set>
#include <string>
#include <string_view>

namespace api::mock_faults {

inline constexpr const char* kPageDelayEnv = "HANABI_MOCK_PAGE_DELAY_MS";
inline constexpr const char* kOpenFailOnceEnv = "HANABI_MOCK_OPEN_FAIL_ONCE";

inline int page_delay_ms() {
    const char* v = std::getenv(kPageDelayEnv);
    if (v == nullptr || *v == '\0') return 0;
    const int n = std::atoi(v);
    return n < 0 ? 0 : n;
}

inline std::string open_fail_once_session(std::string_view spec) {
    const std::size_t colon = spec.find(':');
    return std::string(colon == std::string_view::npos ? spec : spec.substr(0, colon));
}

inline std::string open_fail_once_why(std::string_view spec) {
    const std::size_t colon = spec.find(':');
    if (colon == std::string_view::npos || colon + 1 >= spec.size()) return "the backend did not answer the open";
    return std::string(spec.substr(colon + 1));
}

template <typename Consumed>
bool consume_open_failure(std::string_view id, std::string_view spec, Consumed& consumed, std::string* why) {
    if (spec.empty() || id != open_fail_once_session(spec)) return false;
    if (!consumed.insert(std::string(spec)).second) return false;
    if (why != nullptr) *why = open_fail_once_why(spec);
    return true;
}

inline bool consume_open_failure_from_env(const std::string& id, std::string* why) {
    const char* v = std::getenv(kOpenFailOnceEnv);
    if (v == nullptr || *v == '\0') return false;
    static std::mutex m;
    static std::set<std::string> consumed;
    std::lock_guard<std::mutex> lock(m);
    return consume_open_failure(id, std::string_view(v), consumed, why);
}

}
