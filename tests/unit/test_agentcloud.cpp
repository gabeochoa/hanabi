// Unit tests for the agentcloud transport slice: query encoding and the
// env-driven config gate. No network — mint_token and the socket are covered
// by running the real thing, not by a test that would need a proxy.
//
// percent_encode earns a test because it fails SILENTLY: the verifier is
// `SERVICE_IDENTITY:<name>`, and an unescaped colon comes back as an HTTP 400
// with an opaque body, which reads like an auth problem rather than a typo.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../../src/api/agentcloud_auth.h"

static int g_failures = 0;
#define CHECK(cond)                                                 \
    do {                                                            \
        if (!(cond)) {                                              \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++g_failures;                                           \
        }                                                           \
    } while (0)

using api::agentcloud::auth_config_from_env;
using api::agentcloud::percent_encode;

static void test_percent_encode_escapes_the_colon() {
    // The one that matters: the verifier's separator.
    CHECK(percent_encode("SERVICE_IDENTITY:a.b.c") ==
          "SERVICE_IDENTITY%3Aa.b.c");
}

static void test_percent_encode_keeps_unreserved() {
    // RFC 3986 unreserved: ALPHA / DIGIT / - . _ ~ pass through untouched.
    CHECK(percent_encode("azAZ09-._~") == "azAZ09-._~");
}

static void test_percent_encode_escapes_the_rest() {
    CHECK(percent_encode("a b") == "a%20b");
    CHECK(percent_encode("a/b") == "a%2Fb");
    CHECK(percent_encode("a&b=c") == "a%26b%3Dc");
    CHECK(percent_encode("") == "");
    // High bytes must not sign-extend into a negative index.
    CHECK(percent_encode("\xC3\xA9") == "%C3%A9");
}

static void test_unconfigured_by_default() {
    unsetenv("HANABI_AC_MINT_HOST");
    unsetenv("HANABI_AC_VERIFIER");
    unsetenv("HANABI_AC_HOST");
    const auto cfg = auth_config_from_env();
    // Nothing set means "run the mock", not "fail at startup" — this is what
    // keeps a machine with no proxy working out of the box.
    CHECK(!cfg.configured());
    CHECK(cfg.proxy_host == "127.0.0.1");
    CHECK(cfg.proxy_port == 10054);
    CHECK(cfg.validity_secs == 10800);
}

static void test_configured_needs_all_three() {
    setenv("HANABI_AC_MINT_HOST", "mint.example", 1);
    CHECK(!auth_config_from_env().configured());
    setenv("HANABI_AC_VERIFIER", "SERVICE_IDENTITY:x", 1);
    CHECK(!auth_config_from_env().configured());
    setenv("HANABI_AC_HOST", "orch.example", 1);
    CHECK(auth_config_from_env().configured());
}

static void test_bad_numbers_keep_the_default() {
    setenv("HANABI_AC_PROXY_PORT", "not-a-port", 1);
    // A typo should not silently dial port 0.
    CHECK(auth_config_from_env().proxy_port == 10054);
    setenv("HANABI_AC_PROXY_PORT", "0", 1);
    CHECK(auth_config_from_env().proxy_port == 10054);
    setenv("HANABI_AC_PROXY_PORT", "9999", 1);
    CHECK(auth_config_from_env().proxy_port == 9999);
    unsetenv("HANABI_AC_PROXY_PORT");
}

static void test_empty_env_reads_as_unset() {
    setenv("HANABI_AC_PROXY_HOST", "", 1);
    // `FOO= ./hanabi` means "leave it alone", not "set it to nothing".
    CHECK(auth_config_from_env().proxy_host == "127.0.0.1");
    unsetenv("HANABI_AC_PROXY_HOST");
}

int main() {
    std::printf("== test_agentcloud (transport config + encoding) ==\n");
    test_percent_encode_escapes_the_colon();
    test_percent_encode_keeps_unreserved();
    test_percent_encode_escapes_the_rest();
    test_unconfigured_by_default();
    test_configured_needs_all_three();
    test_bad_numbers_keep_the_default();
    test_empty_env_reads_as_unset();
    if (g_failures == 0) std::printf("OK\n");
    else std::printf("%d FAILURES\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
