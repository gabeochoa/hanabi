#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <afterhours/src/plugins/clipboard.h>

namespace hanabi::clipboard {

#ifdef AFTER_HOURS_ENABLE_E2E_TESTING
struct Probe {
    std::uint64_t generation = 0;
    std::string text;
};

inline Probe& test_probe() {
    static Probe probe;
    return probe;
}

inline void reset_test_probe() { test_probe() = {}; }
#endif

struct OsCalls {
    std::uint64_t reads = 0;
    std::uint64_t writes = 0;
};

inline OsCalls& os_calls() {
    static OsCalls calls;
    return calls;
}

inline constexpr bool kIsolated =
#ifdef AFTER_HOURS_ENABLE_E2E_TESTING
    true;
#else
    false;
#endif

inline void set_text(std::string_view text) {
#ifdef AFTER_HOURS_ENABLE_E2E_TESTING
    auto& probe = test_probe();
    ++probe.generation;
    probe.text.assign(text);
#else
    ++os_calls().writes;
    afterhours::clipboard::set_text(text);
#endif
}

inline std::string get_text() {
#ifdef AFTER_HOURS_ENABLE_E2E_TESTING
    return test_probe().text;
#else
    ++os_calls().reads;
    return afterhours::clipboard::get_text();
#endif
}

}  // namespace hanabi::clipboard
