#pragma once

#include <cstddef>
#include <string>
#include <string_view>

// The test runner's LAUNCH POLICY as the process sees it. Under
// HANABI_E2E_HEADLESS_ONLY=1 exactly ONE entry is admitted -- `--e2e <script>`
// with no other mode requested and HANABI_E2E_WINDOWED unarmed, the approved
// headless E2E path -- and every other entry is refused before any branch of
// main() initialises anything: a windowed --e2e, --screenshot (a headless
// display mode, but not the approved test path and able to bypass the E2E
// setup), --atlas-stress, the notification and chime probes (they touch the
// notification centre and audio), --native-diagnostics, --version and the
// URL parsers (no window, but not the test path), a bare launch of the app,
// and any COMBINATION of modes (`--e2e x --screenshot y` is a conflict,
// refused naming both). The policy is about which code path may run, not a
// graphics-mode label. A value of the variable other than "1" or unset is
// MALFORMED and refuses too: an unknown spelling of the policy must never
// admit a window. Without the variable nothing here applies and every entry
// behaves as before.
//
// One pure decision, called by both checks in main() (the early-entry
// dominance right after argv is parsed, and the pre-window backstop) and by
// the unit tests, so what the tests execute is what the binary decides with.
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

// What argv asked for -- a SET, not a flag: main.cpp's whole argv surface.
// `default_app` is "none of the above": the plain launch that opens the
// app's window (an `--e2e` with an EMPTY script string counts as default).
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
    bool windowed_env_active = false;  // HANABI_E2E_WINDOWED armed
};

struct Verdict {
    bool admitted = true;
    std::string reason;  // empty when admitted
};

inline Verdict admits(const EntryRequest& r, Policy p, std::string_view policy_value = {}) {
    if (p == Policy::Unset) return {};
    if (p == Policy::Malformed)
        return {false, "HANABI_E2E_HEADLESS_ONLY='" + std::string(policy_value) +
                           "' is not a policy this binary knows (the runner sets exactly \"1\"); "
                           "an unknown spelling never admits a launch"};
    // HeadlessOnly. Name every mode requested other than the admitted one,
    // so a conflict reads as a conflict in the log.
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
    // --version and --parse-*-url open no window, but they are not the
    // intended headless E2E path either and the runner never invokes them
    // under the policy: refused like everything else. The admitted set is
    // exactly {--e2e <script> alone, HANABI_E2E_WINDOWED unarmed}.
    name(r.parse_url, "--parse-thread-url/--parse-settings-url");
    name(r.version, "--version");
    if (modes.empty() && r.e2e_script) return {};
    std::string why = "HANABI_E2E_HEADLESS_ONLY=1 (the runner's headless-only test policy) admits only "
                      "`--e2e <script>` with HANABI_E2E_WINDOWED unarmed; this launch requested ";
    why += modes.empty() ? std::string("nothing the policy admits") : modes;
    if (r.e2e_script) why += " beside --e2e";
    return {false, why};
}

}  // namespace hanabi::policy
