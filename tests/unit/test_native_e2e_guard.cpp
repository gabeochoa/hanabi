// The windowed e2e entry guard (src/native_e2e_guard.h): every rule, held
// without a process, plus the one filesystem step (canonicalisation) held
// against a real symlink in a temp directory. The runner's own refusal
// (`native_mode on` on a non-mock backend) comes after app_init; this one is
// what keeps a mis-set run from reading the person's config, adopting their
// token or opening their stores at all.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include <unistd.h>

#include "../../src/native_e2e_guard.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

using hanabi::native_e2e::Env;
using hanabi::native_e2e::inside;
using hanabi::native_e2e::refusal;

static Env isolated() {
    Env e;
    e.backend = "mock";
    e.home = "/private/tmp/hanabi_uitest_home.abc/native_send";
    e.home_is_dir = true;
    e.login_home = "/Users/someone";
    e.config = e.home + "/no-such-config.json";
    e.cache_dir = e.home + "/cache/native_send";
    e.token_file = e.home + "/token.json";
    e.pasteboard_name = "hanabi-e2e-native_send-4242-1789380000";
    return e;
}

static bool mentions(const std::optional<std::string>& why, const char* word) {
    return why.has_value() && why->find(word) != std::string::npos;
}

static void test_rules() {
    std::printf("test_rules\n");
    // The runner's own environment (scripts/run_ui_tests.sh) is allowed.
    CHECK(!refusal(isolated()).has_value());

    // The backend must be mock, and must be SAID: unset means the config file
    // and the token store decide, which is the person's real account.
    { Env e = isolated(); e.backend = "";     CHECK(mentions(refusal(e), "HANABI_BACKEND=mock")); }
    { Env e = isolated(); e.backend = "http"; CHECK(mentions(refusal(e), "'http'")); }
    { Env e = isolated(); e.backend = "Mock"; CHECK(refusal(e).has_value()); }
    // Checked first: a real-backend run is refused for that reason even when
    // everything else is also wrong.
    { Env e; e.backend = "http"; CHECK(mentions(refusal(e), "HANABI_BACKEND=mock")); }

    // HOME: set, a directory, and not the login home or under it.
    { Env e = isolated(); e.home = "";                    CHECK(mentions(refusal(e), "HOME")); }
    { Env e = isolated(); e.home_is_dir = false;          CHECK(mentions(refusal(e), "existing")); }
    { Env e = isolated(); e.home = e.login_home;          CHECK(mentions(refusal(e), "login user's home")); }
    { Env e = isolated(); e.home = e.login_home + "/w/x"; CHECK(mentions(refusal(e), "login user's home")); }
    // A sibling that merely shares a prefix is not "under".
    { Env e = isolated(); e.home = "/Users/someone-else/fixture";
      e.config = e.home + "/c.json"; e.cache_dir = e.home + "/cache"; e.token_file = e.home + "/t.json";
      CHECK(!refusal(e).has_value()); }
    // An unknown login home (no passwd entry) does not block a private HOME.
    { Env e = isolated(); e.login_home = ""; CHECK(!refusal(e).has_value()); }

    // Every store must be INSIDE the private HOME: a private HOME with a token
    // file pointing back at the person's Library would otherwise pass, and the
    // token is what selects the http backend.
    { Env e = isolated(); e.config = "";     CHECK(mentions(refusal(e), "HANABI_CONFIG")); }
    { Env e = isolated(); e.cache_dir = "";  CHECK(mentions(refusal(e), "HANABI_CACHE_DIR")); }
    { Env e = isolated(); e.token_file = ""; CHECK(mentions(refusal(e), "HANABI_TOKEN_FILE")); }
    { Env e = isolated(); e.pasteboard_name = ""; CHECK(mentions(refusal(e), "HANABI_PASTEBOARD_NAME")); }
    // Only the runner's namespace is accepted: the general board under any of
    // its spellings, the other system boards, and an arbitrary name are all
    // refused -- the rule is "ours", not "not that one".
    for (const char* system : {"Apple CFPasteboard general", "general", "Apple CFPasteboard find",
                               "Apple CFPasteboard ruler", "Apple CFPasteboard font",
                               "Apple CFPasteboard drag", "com.apple.pasteboard.general",
                               "hanabi-e2e", "hanabi-e2e", "my-board"}) {
        Env e = isolated();
        e.pasteboard_name = system;
        CHECK(mentions(refusal(e), "HANABI_PASTEBOARD_NAME"));
    }
    { Env e = isolated(); e.pasteboard_name = "hanabi-e2e-x-1-2"; CHECK(!refusal(e).has_value()); }
    CHECK(hanabi::native_e2e::is_private_pasteboard_name("hanabi-e2e-script-42-1789380000"));
    CHECK(!hanabi::native_e2e::is_private_pasteboard_name("hanabi-e2e-"));
    CHECK(!hanabi::native_e2e::is_private_pasteboard_name("Hanabi-E2E-x"));
    { Env e = isolated(); e.token_file = "/Users/someone/Library/Application Support/hanabi/token.json";
      CHECK(mentions(refusal(e), "HANABI_TOKEN_FILE inside the private HOME")); }
    { Env e = isolated(); e.cache_dir = "/private/tmp/other/cache"; CHECK(mentions(refusal(e), "HANABI_CACHE_DIR inside")); }
    { Env e = isolated(); e.config = "/private/tmp/hanabi_uitest_home.abc/native_sendX/c.json";
      CHECK(mentions(refusal(e), "HANABI_CONFIG inside")); }  // prefix, not a child
    { Env e = isolated(); e.config = e.home; CHECK(!refusal(e).has_value()); }  // the root itself counts

    // A path that could not be canonicalised refuses rather than passes.
    { Env e = isolated(); e.resolve_failed = true; e.resolve_failed_key = "HANABI_CACHE_DIR";
      CHECK(mentions(refusal(e), "could not resolve HANABI_CACHE_DIR")); }

    // inside(): whole components, trailing slash on the root tolerated.
    CHECK(inside("/a/b", "/a/b"));
    CHECK(inside("/a/b/", "/a/b/c"));
    CHECK(!inside("/a/b", "/a/bc"));
    CHECK(!inside("/a/b", "/a"));
    CHECK(!inside("", "/a"));
}

// from_process(): HOME through a symlink resolves to where it points, so an
// alias of the login home is refused, and `..` inside a store path cannot
// walk out of the fixture.
static void test_canonicalisation() {
    std::printf("test_canonicalisation\n");
    namespace fs = std::filesystem;
    char tmpl[] = "/tmp/hanabi_guard_test.XXXXXX";
    const char* dir = mkdtemp(tmpl);
    CHECK(dir != nullptr);
    if (dir == nullptr) return;
    const fs::path root = fs::weakly_canonical(fs::path(dir));
    const fs::path fixture = root / "fixture";
    fs::create_directories(fixture / "cache");
    std::error_code ec;
    fs::create_directory_symlink(fixture, root / "link", ec);
    CHECK(!ec);

    setenv("HANABI_BACKEND", " mock ", 1);
    setenv("HOME", (root / "link").c_str(), 1);
    setenv("HANABI_CONFIG", (root / "link" / "no-such-config.json").c_str(), 1);
    setenv("HANABI_CACHE_DIR", (root / "link" / "cache").c_str(), 1);
    setenv("HANABI_TOKEN_FILE", (root / "link" / "token.json").c_str(), 1);
    // The process fixture without a private pasteboard name is refused for
    // that alone; with one, the path rules decide.
    unsetenv("HANABI_PASTEBOARD_NAME");
    CHECK(mentions(refusal(hanabi::native_e2e::from_process()), "HANABI_PASTEBOARD_NAME"));
    setenv("HANABI_PASTEBOARD_NAME", "Apple CFPasteboard general", 1);
    CHECK(mentions(refusal(hanabi::native_e2e::from_process()), "HANABI_PASTEBOARD_NAME"));
    const std::string board = "hanabi-e2e-unit-" + std::to_string(getpid());
    setenv("HANABI_PASTEBOARD_NAME", board.c_str(), 1);
    Env e = hanabi::native_e2e::from_process();
    CHECK(e.pasteboard_name == board);
    CHECK(e.backend == "mock");                 // trimmed
    CHECK(e.home == fixture.string());          // the symlink resolved
    CHECK(e.home_is_dir);
    CHECK(e.config == (fixture / "no-such-config.json").string());  // leaf need not exist
    CHECK(!refusal(e).has_value());

    // A store whose PARENT does not exist yet (the runner creates the cache
    // directory later) still resolves lexically and is still inside.
    setenv("HANABI_CACHE_DIR", (root / "link" / "not-yet" / "cache" / "script").c_str(), 1);
    e = hanabi::native_e2e::from_process();
    CHECK(!e.resolve_failed);
    CHECK(e.cache_dir == (fixture / "not-yet" / "cache" / "script").string());
    CHECK(!refusal(e).has_value());
    setenv("HANABI_CACHE_DIR", (root / "link" / "cache").c_str(), 1);

    // `..` walking out of the fixture is caught after canonicalisation.
    setenv("HANABI_TOKEN_FILE", (root / "link" / ".." / "elsewhere" / "token.json").c_str(), 1);
    e = hanabi::native_e2e::from_process();
    CHECK(e.token_file == (root / "elsewhere" / "token.json").string());
    CHECK(mentions(refusal(e), "HANABI_TOKEN_FILE inside the private HOME"));

    // A HOME that is a symlink to the login home is the login home.
    if (!e.login_home.empty()) {
        fs::create_directory_symlink(fs::path(e.login_home), root / "homelink", ec);
        if (!ec) {
            setenv("HOME", (root / "homelink").c_str(), 1);
            e = hanabi::native_e2e::from_process();
            CHECK(e.home == e.login_home);
            CHECK(mentions(refusal(e), "login user's home"));
        }
    }
    fs::remove_all(root, ec);
}

int main() {
    std::printf("test_native_e2e_guard\n");
    test_rules();
    test_canonicalisation();
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
