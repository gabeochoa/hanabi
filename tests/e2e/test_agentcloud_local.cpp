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

    const auto asked = client.get_session("ask-local", 200);
    if (!asked.ok || asked.value.pending_asks.size() != 2) {
        std::fprintf(stderr, "pending asks not parsed off the attach\n");
        return 1;
    }
    const api::PendingAsk& own = asked.value.pending_asks[0];
    const api::PendingAsk& child = asked.value.pending_asks[1];
    if (own.id() != "#41" || child.id() != "kid-local#41") {
        std::fprintf(stderr, "ask identity is not the (session, seq) pair\n");
        return 1;
    }
    if (own.questions.size() != 4 || own.answerable_questions() != 3 ||
        !own.has_file_question()) {
        std::fprintf(stderr, "schema did not fold into the expected form\n");
        return 1;
    }

    api::AskAnswer answer;
    answer.picks["q1"] = {"promo"};
    answer.picks["q2"] = {"rows", "credits"};
    answer.text["q1_other"] = "  or the bank feed  ";
    answer.text["q3"] = "check the promo ledger first";
    answer.text["q4"] = "this must never reach the wire";
    const auto accepted =
        client.resolve_ask("ask-local", own, api::AskAction::Accept, answer);
    if (!accepted.ok || accepted.value != "accept") {
        std::fprintf(stderr, "resolve_elicitation failed: %s\n",
                     accepted.error.c_str());
        return 1;
    }

    const auto declined = client.resolve_ask("ask-local", child,
                                             api::AskAction::Decline, answer);
    if (!declined.ok || declined.value != "decline") {
        std::fprintf(stderr, "child decline failed: %s\n",
                     declined.error.c_str());
        return 1;
    }

    api::AskAnswer nothing;
    const auto empty =
        client.resolve_ask("ask-local", own, api::AskAction::Accept, nothing);
    if (empty.ok) {
        std::fprintf(stderr, "an empty accept must not reach the wire\n");
        return 1;
    }

    std::printf("OK\n");
    return 0;
}
