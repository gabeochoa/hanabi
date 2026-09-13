#pragma once
// ---------------------------------------------------------------------------
// Whether a WINDOWED e2e run is allowed to start at all.
//
// HANABI_E2E_WINDOWED=1 runs a script inside the real window and lets its
// `native_*` commands post real NSEvents. Real input into a real window can
// reach a real backend: a send lands in somebody's live thread, a click
// closes a tab in the person's own saved state. The runner's own refusal
// (`native_mode on` fails on a non-mock backend) comes AFTER app_init has
// read the config file, adopted a stored token -- which alone flips the
// backend to http (main.cpp, "Phase AUTH") -- built the client and opened the
// stores. Too late to be the guard: by then the process has touched what the
// guard exists to keep it away from.
//
// So the decision is made from the ENVIRONMENT alone, before app_init, on
// the knobs the scripted-UI runner already sets for every script
// (scripts/run_ui_tests.sh): the backend must be named mock EXPLICITLY (an
// unset HANABI_BACKEND is "whatever the config file and the token store
// say", which is the person's real account); HOME must be a private root --
// an existing directory that, with symlinks and `..` resolved, is neither
// the login user's home nor inside it; and every store the app reads or
// writes -- HANABI_CONFIG, HANABI_CACHE_DIR, HANABI_TOKEN_FILE -- must
// resolve to a path INSIDE that root (the leaf may not exist: a config path
// that points at nothing is the runner's own idiom and the safest value).
// A private HOME with a token file pointing back at the person's Library
// would otherwise pass, and the token is what selects the http backend. A
// refusal names the first knob that is wrong and exits 3 before a window, a
// socket or a file is opened.
//
// The rules are pure over already-canonical strings (refusal), so
// tests/unit/test_native_e2e_guard.cpp holds them without a process; the
// canonicalisation is the one filesystem step (from_process), and a path
// that cannot be resolved refuses rather than passes.
// ---------------------------------------------------------------------------
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include <pwd.h>
#include <unistd.h>

namespace hanabi::native_e2e {

struct Env {
    std::string backend;     // HANABI_BACKEND, trimmed
    std::string home;        // HOME, canonical ("" when unset or unresolvable)
    bool home_is_dir = false;
    std::string login_home;  // the login user's home, canonical ("" when unknown)
    std::string config;      // HANABI_CONFIG, weakly canonical ("" when unset)
    std::string cache_dir;   // HANABI_CACHE_DIR, weakly canonical
    std::string token_file;  // HANABI_TOKEN_FILE, weakly canonical
    bool resolve_failed = false;  // a set path that could not be canonicalised
    std::string resolve_failed_key;
};

inline std::string trim(std::string s) {
    const auto ws = " \t\r\n";
    const auto b = s.find_first_not_of(ws);
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

inline std::string env_or_empty(const char* key) {
    const char* v = std::getenv(key);
    return v ? trim(std::string(v)) : std::string();
}

// `path` is `root` itself or below it, by whole components.
inline bool inside(std::string_view root, std::string_view path) {
    if (root.empty() || path.empty()) return false;
    while (root.size() > 1 && root.back() == '/') root.remove_suffix(1);
    if (path == root) return true;
    return path.size() > root.size() && path.substr(0, root.size()) == root &&
           path[root.size()] == '/';
}

inline Env from_process() {
    namespace fs = std::filesystem;
    Env e;
    e.backend = env_or_empty("HANABI_BACKEND");
    std::error_code ec;
    if (const passwd* pw = getpwuid(getuid()); pw != nullptr && pw->pw_dir != nullptr) {
        const fs::path c = fs::weakly_canonical(fs::path(pw->pw_dir), ec);
        e.login_home = ec ? std::string(pw->pw_dir) : c.string();
    }
    const auto resolve = [&](const char* key, std::string& out) {
        const std::string raw = env_or_empty(key);
        if (raw.empty()) return;
        ec.clear();
        const fs::path c = fs::weakly_canonical(fs::path(raw), ec);
        if (ec || c.empty()) {
            e.resolve_failed = true;
            if (e.resolve_failed_key.empty()) e.resolve_failed_key = key;
            return;
        }
        out = c.string();
    };
    resolve("HOME", e.home);
    if (!e.home.empty()) e.home_is_dir = fs::is_directory(fs::path(e.home), ec) && !ec;
    resolve("HANABI_CONFIG", e.config);
    resolve("HANABI_CACHE_DIR", e.cache_dir);
    resolve("HANABI_TOKEN_FILE", e.token_file);
    return e;
}

// Empty when the run may start; otherwise the reason it may not, in the
// words printed to stderr.
inline std::optional<std::string> refusal(const Env& e) {
    if (e.backend != "mock")
        return "HANABI_E2E_WINDOWED needs HANABI_BACKEND=mock, explicitly: native "
               "input into a real window must not be able to reach a real "
               "backend (got '" + e.backend + "').";
    if (e.resolve_failed)
        return "HANABI_E2E_WINDOWED could not resolve " + e.resolve_failed_key +
               " to a canonical path; refusing rather than guessing.";
    if (e.home.empty() || !e.home_is_dir)
        return "HANABI_E2E_WINDOWED needs HOME set to an existing private fixture "
               "directory.";
    if (!e.login_home.empty() && inside(e.login_home, e.home))
        return "HANABI_E2E_WINDOWED refuses a HOME at or under the login user's home "
               "(" + e.home + "): point HOME at a private fixture directory under "
               "/tmp or $TMPDIR, the way scripts/run_ui_tests.sh does.";
    const auto must_be_inside_home = [&](const char* key, const std::string& p)
        -> std::optional<std::string> {
        if (p.empty())
            return std::string("HANABI_E2E_WINDOWED needs ") + key +
                   " set to a path inside the private HOME.";
        if (!inside(e.home, p))
            return std::string("HANABI_E2E_WINDOWED needs ") + key +
                   " inside the private HOME (" + e.home + "), not " + p +
                   ": every store the app touches must be the fixture's own.";
        return std::nullopt;
    };
    if (auto why = must_be_inside_home("HANABI_CONFIG", e.config)) return why;
    if (auto why = must_be_inside_home("HANABI_CACHE_DIR", e.cache_dir)) return why;
    if (auto why = must_be_inside_home("HANABI_TOKEN_FILE", e.token_file)) return why;
    return std::nullopt;
}

}  // namespace hanabi::native_e2e
