#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace hanabi::policy {

enum class Policy { Unset, HeadlessOnly, Malformed };

inline Policy parse_policy(const char* value) {
    if (value == nullptr || *value == '\0') return Policy::Unset;
    if (std::string_view(value) == "1") return Policy::HeadlessOnly;
    return Policy::Malformed;
}

inline bool windowed_armed(const char* value) {
    return value != nullptr && *value != '\0' && std::string_view(value) != "0";
}

struct EntryRequest {
    bool e2e_script = false;
    bool screenshot = false;
    bool atlas_stress = false;
    bool native_diagnostics = false;
    bool notify_probe = false;
    bool chime_probe = false;
    bool parse_url = false;
    bool version = false;
    bool default_app = false;
    bool windowed_env_active = false;
};

struct Verdict {
    bool admitted = true;
    std::string reason;
};

inline Verdict admits(const EntryRequest& r, Policy p, std::string_view policy_value = {}) {
    if (p == Policy::Unset) return {};
    if (p == Policy::Malformed)
        return {false, "HANABI_E2E_HEADLESS_ONLY='" + std::string(policy_value) +
                           "' is not a policy this binary knows (the runner sets exactly \"1\"); "
                           "an unknown spelling never admits a launch"};
    std::string modes;
    const auto name = [&](bool on, const char* label) {
        if (!on) return;
        if (!modes.empty()) modes += ", ";
        modes += label;
    };
    name(r.windowed_env_active, "HANABI_E2E_WINDOWED (armed)");
    name(r.screenshot, "--screenshot");
    name(r.atlas_stress, "--atlas-stress");
    name(r.native_diagnostics, "--native-diagnostics");
    name(r.notify_probe, "--notify-probe");
    name(r.chime_probe, "--chime-probe");
    name(r.default_app, "the default app launch (no script)");
    name(r.parse_url, "--parse-thread-url/--parse-settings-url");
    name(r.version, "--version");
    if (modes.empty() && r.e2e_script) return {};
    std::string why = "HANABI_E2E_HEADLESS_ONLY=1 (the runner's headless-only test policy) admits only "
                      "`--e2e <script>` with HANABI_E2E_WINDOWED unarmed; this launch requested ";
    why += modes.empty() ? std::string("nothing the policy admits") : modes;
    if (r.e2e_script) why += " beside --e2e";
    return {false, why};
}

}
