#include <cstdio>
#include <string>

#include "../../src/api/agentcloud_hosts.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace h = api::agentcloud_hosts;

int main() {
    std::printf("=== test_agentcloud_hosts ===\n");
    CHECK(h::is_standard_orchestrator("agentcloud-orchestrator-prod.playground.x2p.facebook.net"));
    CHECK(h::is_standard_orchestrator("agentcloud-orchestrator-rc.playground.x2p.facebook.net"));
    CHECK(h::is_standard_orchestrator("AGENTCLOUD-ORCHESTRATOR-PROD.playground.x2p.facebook.net"));
    CHECK(h::is_standard_orchestrator("agentcloud-orchestrator-prod.playground.x2p.facebook.net:443"));
    CHECK(h::is_standard_orchestrator("mm.internalmeta.com"));
    CHECK(h::is_standard_orchestrator("agentcloud.mm.internalmeta.com"));
    CHECK(h::is_standard_orchestrator("Orchestrator.MM.internalmeta.com:8443"));
    CHECK(!h::is_standard_orchestrator("devvm48270.atn0.facebook.com"));
    CHECK(!h::is_standard_orchestrator("devvm48270.atn0.facebook.com:8080"));
    CHECK(!h::is_standard_orchestrator("USER:gabeochoa-orchestrator.playground.x2p.facebook.net"));
    CHECK(!h::is_standard_orchestrator("127.0.0.1:9000"));
    CHECK(!h::is_standard_orchestrator("[::1]:9000"));
    CHECK(!h::is_standard_orchestrator("notmm.internalmeta.com"));
    CHECK(!h::is_standard_orchestrator("mm.internalmeta.com.evil.example"));
    CHECK(!h::is_standard_orchestrator(""));
    CHECK(h::canonical_host("Host:1234") == "host");
    CHECK(h::canonical_host("[::1]:9000") == "[::1]:9000");
    if (failures == 0) { std::printf("OK\n"); return 0; }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
