#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "../../src/api/agentcloud_client.h"

int main() {
    const char* port = std::getenv("HANABI_AC_LOCAL_PORT");
    if (port == nullptr || *port == '\0') return 2;
    api::agentcloud::AuthConfig cfg;
    cfg.proxy_host.clear();
    cfg.proxy_port = 0;
    cfg.mint_host = "local";
    cfg.verifier = "local";
    cfg.host = std::string("127.0.0.1:") + port;
    api::agentcloud::Token token;
    token.value = "local-test-token";
    token.expires_at = static_cast<int64_t>(std::time(nullptr)) + 3600;
    api::AgentcloudClient client(std::move(cfg), std::move(token));

    const auto fork = client.fork_with_prompt("source-local", "why local?",
                                              "BTW: why local?");
    if (!fork.ok || fork.value != "fork-local") {
        std::fprintf(stderr, "fork failed: %s\n", fork.error.c_str());
        return 1;
    }
    const auto bare = client.fork_session("source-local");
    if (!bare.ok || bare.value != "fork-bare") {
        std::fprintf(stderr, "bare fork failed: %s\n", bare.error.c_str());
        return 1;
    }
    const auto children = client.list_subagents(2000);
    if (!children.ok || children.value.size() != 1 ||
        children.value[0].id != "child-local" ||
        children.value[0].parent_id != "source-local") {
        std::fprintf(stderr, "sub-agent catalog failed\n");
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
