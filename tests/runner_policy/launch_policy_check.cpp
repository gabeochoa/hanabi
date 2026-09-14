// Standalone check of src/launch_policy.h -- the decision both guards in
// main() call. Builds with any C++20 compiler and nothing else from the app:
//   c++ -std=c++20 -I<repo>/src tests/runner_policy/launch_policy_check.cpp -o lp && ./lp
// The same matrix as tests/unit/test_data.cpp's
// test_launch_policy_admits_only_the_headless_e2e_entry, kept here so a lane
// without the app's build can execute the exe-side guard's decision table.
#include <cstdio>
#include <string>

#include "launch_policy.h"

static int failures = 0;
static int checks = 0;
#define CHECK(x)                                                                 \
    do {                                                                         \
        ++checks;                                                                \
        if (!(x)) {                                                              \
            std::fprintf(stderr, "CHECK failed: %s (line %d)\n", #x, __LINE__); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

int main() {
    namespace lp = hanabi::policy;
    using R = lp::EntryRequest;
    const auto e2e = [] { R r; r.e2e_script = true; return r; };
    const auto with = [](R r, auto f) { f(r); return r; };
    // parse_policy: exactly "1" is the policy; unset/empty is no policy; any
    // other spelling is MALFORMED (fail closed).
    CHECK(lp::parse_policy(nullptr) == lp::Policy::Unset);
    CHECK(lp::parse_policy("") == lp::Policy::Unset);
    CHECK(lp::parse_policy("1") == lp::Policy::HeadlessOnly);
    CHECK(lp::parse_policy("0") == lp::Policy::Malformed);
    CHECK(lp::parse_policy("yes") == lp::Policy::Malformed);
    CHECK(lp::parse_policy("HEADLESS_ONLY") == lp::Policy::Malformed);
    CHECK(lp::windowed_armed("1") && lp::windowed_armed("yes") && !lp::windowed_armed("0") &&
          !lp::windowed_armed("") && !lp::windowed_armed(nullptr));
    // Unset x every mode -> admitted (behaviour unchanged).
    {
        R modes[] = {e2e(), with(R{}, [](R& r) { r.screenshot = true; }),
                     with(R{}, [](R& r) { r.atlas_stress = true; }),
                     with(R{}, [](R& r) { r.native_diagnostics = true; }),
                     with(R{}, [](R& r) { r.notify_probe = true; }),
                     with(R{}, [](R& r) { r.chime_probe = true; }),
                     with(R{}, [](R& r) { r.parse_url = true; }),
                     with(R{}, [](R& r) { r.version = true; }),
                     with(R{}, [](R& r) { r.default_app = true; }),
                     with(e2e(), [](R& r) { r.windowed_env_active = true; }),
                     with(e2e(), [](R& r) { r.screenshot = true; })};
        for (const R& r : modes) CHECK(lp::admits(r, lp::Policy::Unset).admitted);
    }
    // HeadlessOnly x {e2e only} -> admitted.
    CHECK(lp::admits(e2e(), lp::Policy::HeadlessOnly).admitted);
    // HeadlessOnly x each refused shape -> refused, reason naming the mode.
    const auto refused_naming = [&](R r, const char* needle) {
        const auto v = lp::admits(r, lp::Policy::HeadlessOnly);
        return !v.admitted && v.reason.find(needle) != std::string::npos;
    };
    CHECK(refused_naming(with(e2e(), [](R& r) { r.windowed_env_active = true; }), "HANABI_E2E_WINDOWED (armed)"));
    CHECK(refused_naming(with(e2e(), [](R& r) { r.screenshot = true; }), "--screenshot"));
    CHECK(refused_naming(with(e2e(), [](R& r) { r.atlas_stress = true; }), "--atlas-stress"));
    CHECK(refused_naming(with(R{}, [](R& r) { r.screenshot = true; }), "--screenshot"));
    CHECK(refused_naming(with(R{}, [](R& r) { r.default_app = true; }), "default app launch"));
    CHECK(refused_naming(with(R{}, [](R& r) { r.atlas_stress = true; }), "--atlas-stress"));
    CHECK(refused_naming(with(R{}, [](R& r) { r.notify_probe = true; }), "--notify-probe"));
    CHECK(refused_naming(with(R{}, [](R& r) { r.chime_probe = true; }), "--chime-probe"));
    CHECK(refused_naming(with(R{}, [](R& r) { r.native_diagnostics = true; }), "--native-diagnostics"));
    // `--e2e ""` is the default launch (main.cpp: an empty script string
    // selects no e2e), so it arrives as default_app and refuses.
    CHECK(refused_naming(with(R{}, [](R& r) { r.default_app = true; }), "no script"));
    // A conflict names BOTH modes.
    {
        const auto v = lp::admits(with(e2e(), [](R& r) { r.screenshot = true; }), lp::Policy::HeadlessOnly);
        CHECK(!v.admitted && v.reason.find("--screenshot") != std::string::npos &&
              v.reason.find("beside --e2e") != std::string::npos);
        const auto w = lp::admits(with(e2e(), [](R& r) { r.screenshot = true; r.windowed_env_active = true; }),
                                  lp::Policy::HeadlessOnly);
        CHECK(!w.admitted && w.reason.find("HANABI_E2E_WINDOWED (armed), --screenshot") != std::string::npos);
    }
    // --version and the URL parsers open no window but are not the test
    // path: refused alone, refused beside a script.
    CHECK(refused_naming(with(R{}, [](R& r) { r.version = true; }), "--version"));
    CHECK(refused_naming(with(R{}, [](R& r) { r.parse_url = true; }), "--parse-thread-url/--parse-settings-url"));
    CHECK(refused_naming(with(e2e(), [](R& r) { r.version = true; }), "--version"));
    CHECK(refused_naming(with(e2e(), [](R& r) { r.parse_url = true; }), "--parse-thread-url/--parse-settings-url"));
    // The pre-window backstop's shape: e2e requested but the window reached.
    CHECK(refused_naming(with(e2e(), [](R& r) { r.default_app = true; }), "default app launch"));
    // Malformed x {e2e only, default} -> refused naming the value.
    for (const char* bad : {"0", "yes", "HEADLESS_ONLY"}) {
        const auto a = lp::admits(e2e(), lp::Policy::Malformed, bad);
        const auto b = lp::admits(with(R{}, [](R& r) { r.default_app = true; }), lp::Policy::Malformed, bad);
        CHECK(!a.admitted && a.reason.find(std::string("'") + bad + "'") != std::string::npos);
        CHECK(!b.admitted && b.reason.find(std::string("'") + bad + "'") != std::string::npos);
    }
    if (failures == 0) std::printf("launch_policy_check: ok (%d checks)\n", checks);
    return failures == 0 ? 0 : 1;
}
