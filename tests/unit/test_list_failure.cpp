#include <cstdio>
#include <string>

#include "../../src/api/list_failure.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace lf = api::list_failure;

int main() {
    // Credential failures are signed out; transport failures are not.
    CHECK(lf::is_signed_out("the server refused this client's credential (401)"));
    CHECK(lf::is_signed_out("mint returned HTTP 403"));
    CHECK(lf::is_signed_out("agentcloud not configured (HANABI_AC_* unset)"));
    CHECK(lf::is_signed_out("CAT mint rejected: HTTP 401. No valid OIDC login; sign in again"));
    CHECK(!lf::is_signed_out("the list timed out after 20 s"));
    CHECK(!lf::is_signed_out("mint unreachable via proxy localhost:8082"));
    // Signed out never prints a status code; it names the move.
    const auto so = lf::view_for("mint returned HTTP 401", "Hanabi");
    CHECK(so.signedOut && so.headline.find("signed in") != std::string::npos);
    CHECK(so.detail.find("401") == std::string::npos && so.headline.find("401") == std::string::npos);
    CHECK(so.detail.find("Hanabi") == 0);
    // Anything else keeps the transport's words.
    const auto t = lf::view_for("the list timed out after 20 s", "Hanabi");
    CHECK(!t.signedOut && t.detail == "the list timed out after 20 s");
    CHECK(!lf::view_for("", "Hanabi").detail.empty());
    // The stale-list notice.
    CHECK(lf::stale_notice("credential (401)").find("401") == std::string::npos);
    CHECK(lf::stale_notice("timed out") == "Couldn't refresh the thread list (timed out). Showing the saved list.");
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    return 1;
}
