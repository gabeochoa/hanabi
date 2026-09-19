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
    CHECK(lp::parse_policy(nullptr) == lp::Policy::Unset);
    CHECK(lp::parse_policy("") == lp::Policy::Unset);
    CHECK(lp::parse_policy("1") == lp::Policy::HeadlessOnly);
    CHECK(lp::parse_policy("0") == lp::Policy::Malformed);
    CHECK(lp::parse_policy("yes") == lp::Policy::Malformed);
    CHECK(lp::parse_policy("HEADLESS_ONLY") == lp::Policy::Malformed);
    CHECK(lp::windowed_armed("1") && lp::windowed_armed("yes") && !lp::windowed_armed("0") &&
          !lp::windowed_armed("") && !lp::windowed_armed(nullptr));
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
    CHECK(lp::admits(e2e(), lp::Policy::HeadlessOnly).admitted);
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
    CHECK(refused_naming(with(R{}, [](R& r) { r.default_app = true; }), "no script"));
    {
        const auto v = lp::admits(with(e2e(), [](R& r) { r.screenshot = true; }), lp::Policy::HeadlessOnly);
        CHECK(!v.admitted && v.reason.find("--screenshot") != std::string::npos &&
              v.reason.find("beside --e2e") != std::string::npos);
        const auto w = lp::admits(with(e2e(), [](R& r) { r.screenshot = true; r.windowed_env_active = true; }),
                                  lp::Policy::HeadlessOnly);
        CHECK(!w.admitted && w.reason.find("HANABI_E2E_WINDOWED (armed), --screenshot") != std::string::npos);
    }
    CHECK(refused_naming(with(R{}, [](R& r) { r.version = true; }), "--version"));
    CHECK(refused_naming(with(R{}, [](R& r) { r.parse_url = true; }), "--parse-thread-url/--parse-settings-url"));
    CHECK(refused_naming(with(e2e(), [](R& r) { r.version = true; }), "--version"));
    CHECK(refused_naming(with(e2e(), [](R& r) { r.parse_url = true; }), "--parse-thread-url/--parse-settings-url"));
    CHECK(refused_naming(with(e2e(), [](R& r) { r.default_app = true; }), "default app launch"));
    for (const char* bad : {"0", "yes", "HEADLESS_ONLY"}) {
        const auto a = lp::admits(e2e(), lp::Policy::Malformed, bad);
        const auto b = lp::admits(with(R{}, [](R& r) { r.default_app = true; }), lp::Policy::Malformed, bad);
        CHECK(!a.admitted && a.reason.find(std::string("'") + bad + "'") != std::string::npos);
        CHECK(!b.admitted && b.reason.find(std::string("'") + bad + "'") != std::string::npos);
    }
    if (failures == 0) std::printf("launch_policy_check: ok (%d checks)\n", checks);
    return failures == 0 ? 0 : 1;
}
