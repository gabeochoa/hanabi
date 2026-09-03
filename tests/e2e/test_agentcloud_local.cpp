#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "../../src/api/agentcloud_client.h"
#include "../../src/ws_socket.h"
#include "../../vendor/nlohmann/json.hpp"

namespace {

struct Sentinel {
    std::atomic<bool> entered{false};
    std::atomic<bool> left{false};
    std::atomic<int> parkMs{600};
    std::shared_ptr<std::atomic<bool>> done =
        std::make_shared<std::atomic<bool>>(false);
    ~Sentinel() { magic = 0; }
    std::uint32_t magic = 0x5eed5eed;
};

void sentinel_text(void* user, const char*, size_t) {
    auto* s = static_cast<Sentinel*>(user);
    s->entered.store(true);
    const auto done = s->done;
    std::this_thread::sleep_for(
        std::chrono::milliseconds(s->parkMs.load()));
    if (s->magic != 0x5eed5eed) std::abort();
    s->left.store(true);
    done->store(true);
}

void sentinel_close(void*, const char*) {}

int close_waits_for_a_running_callback(const std::string& host,
                                       int parkMs, bool expectWait) {
    auto ownedHolder = std::make_shared<Sentinel>();
    Sentinel& sentinel = *ownedHolder;
    const std::shared_ptr<std::atomic<bool>> watchDone = ownedHolder->done;
    sentinel.parkMs.store(parkMs);
    const std::string url = "ws://" + host + "/ws/chat?v=1";
    ws_config wc{};
    wc.url = url.c_str();
    wc.proxy_host = "";
    wc.proxy_port = 0;
    wc.on_text = sentinel_text;
    wc.on_close = sentinel_close;
    wc.user = ownedHolder.get();

    ws_conn* conn = ws_open_owned(&wc, ownedHolder);
    if (conn == nullptr) return -1;
    const std::string wire = R"({"sub":0,"payload":{"cmd":"list"}})";
    if (!ws_send_text(conn, wire.data(), wire.size())) {
        ws_close(conn);
        return -1;
    }
    for (int i = 0; i < 400 && !sentinel.entered.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (!sentinel.entered.load()) {
        ws_close(conn);
        return -1;
    }

    const auto t0 = std::chrono::steady_clock::now();
    ws_close(conn);
    const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
    const bool finished = sentinel.left.load();
    ws_close(conn);
    if (!expectWait) {
        if (finished) {
            std::fprintf(stderr,
                         "a callback parked past the bound should still be "
                         "running when ws_close returns\n");
            return 1;
        }
        if (waited > 8000) {
            std::fprintf(stderr, "ws_close waited %lldms; the bound is 5s\n",
                         static_cast<long long>(waited));
            return 1;
        }
        const auto watch = watchDone;
        ownedHolder.reset();
        for (int i = 0; i < 500 && !watch->load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (!watch->load()) {
            std::fprintf(stderr,
                         "the parked callback never finished after the "
                         "caller let go of its target\n");
            return 1;
        }
        return 0;
    }
    if (!finished) {
        std::fprintf(stderr, "ws_close returned before the callback finished\n");
        return 1;
    }
    if (waited < 200) {
        std::fprintf(stderr,
                     "ws_close returned in %lldms without waiting for a "
                     "callback parked for 600ms\n",
                     static_cast<long long>(waited));
        return 1;
    }
    return 0;
}

}  // namespace

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
    if (own.id() != "ask-local/#41" ||
        child.id() != "ask-local/kid-local#41" ||
        own.answering_session() != "ask-local" ||
        child.answering_session() != "kid-local") {
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

    for (const api::AskAction action :
         {api::AskAction::Accept, api::AskAction::Decline,
          api::AskAction::Cancel}) {
        const auto stale = client.resolve_ask("gone-local", own, action, answer);
        if (stale.ok || stale.error != api::elicitation::kAskGoneReason) {
            std::fprintf(stderr,
                         "a stale %s was not refused with the drop reason: "
                         "ok=%d %s\n",
                         api::elicitation::action_word(action),
                         static_cast<int>(stale.ok), stale.error.c_str());
            return 1;
        }
    }

    const int quiescent = close_waits_for_a_running_callback(
        std::string("127.0.0.1:") + port, 600, /*expectWait=*/true);
    if (quiescent != 0) {
        if (quiescent < 0)
            std::fprintf(stderr,
                         "the quiescence arm never delivered a message\n");
        return 1;
    }

    api::PendingAsk gone = own;
    gone.owner_session = "skew-local";
    gone.child_session.clear();
    gone.seq = 77;
    const auto vanished =
        client.resolve_ask("skew-local", gone, api::AskAction::Accept, answer);
    if (vanished.ok || vanished.error != api::elicitation::kAskGoneReason) {
        std::fprintf(stderr,
                     "a hello that omits the pending list means the ask is "
                     "gone; got ok=%d %s\n",
                     static_cast<int>(vanished.ok), vanished.error.c_str());
        return 1;
    }

    std::vector<std::string> turnAsks;
    api::StreamSink sink;
    sink.on_event = [&turnAsks](const api::StreamEvent& ev) {
        if (ev.kind == api::StreamEventKind::AsksChanged)
            turnAsks.push_back(ev.payload);
    };
    std::string streamed;
    sink.on_delta = [&streamed](const std::string& d) { streamed += d; };

    const auto before = std::chrono::steady_clock::now();
    client.send_message_streaming("turn-local", "reconcile it", sink);
    const auto spent = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::steady_clock::now() - before)
                           .count();
    if (turnAsks.empty()) {
        std::fprintf(stderr, "the turn reported no pending-ask state\n");
        return 1;
    }
    if (spent > 30) {
        std::fprintf(stderr,
                     "a parked run held the turn for %llds; it must settle "
                     "on the raise, not on the idle timeout\n",
                     static_cast<long long>(spent));
        return 1;
    }
    {
        const auto state =
            nlohmann::json::parse(turnAsks.back(), nullptr, false);
        if (state.is_discarded() || !state.is_object()) {
            std::fprintf(stderr, "the raise did not reach the reader: %s\n",
                         turnAsks.back().c_str());
            return 1;
        }
        const auto folded =
            api::elicitation::asks_from_state(state, "turn-local");
        if (folded.size() != 1 || folded[0].questions.size() != 1 ||
            folded[0].id() != "turn-local/#71") {
            std::fprintf(stderr, "the raised ask did not fold into a card\n");
            return 1;
        }
    }

    std::vector<std::string> settledAsks;
    api::StreamSink settleSink;
    settleSink.on_event = [&settledAsks](const api::StreamEvent& ev) {
        if (ev.kind == api::StreamEventKind::AsksChanged)
            settledAsks.push_back(ev.payload);
    };
    client.send_message_streaming("turn-settled", "and now?", settleSink);
    if (settledAsks.empty()) {
        std::fprintf(stderr, "a turn over a parked session reported nothing\n");
        return 1;
    }
    {
        const auto first =
            nlohmann::json::parse(settledAsks.front(), nullptr, false);
        const auto seeded =
            api::elicitation::asks_from_state(first, "turn-settled");
        if (seeded.size() != 1 || seeded[0].seq != 71) {
            std::fprintf(stderr, "the turn did not seed from its own hello\n");
            return 1;
        }
    }

    std::vector<std::string> keepAsks;
    api::StreamSink keepSink;
    keepSink.on_event = [&keepAsks](const api::StreamEvent& ev) {
        if (ev.kind == api::StreamEventKind::AsksChanged)
            keepAsks.push_back(ev.payload);
    };
    client.send_message_streaming("turn-child", "carry on", keepSink);
    if (keepAsks.empty()) {
        std::fprintf(stderr, "a turn over a child-parked session said nothing\n");
        return 1;
    }
    for (const std::string& reported : keepAsks) {
        const auto state = nlohmann::json::parse(reported, nullptr, false);
        const auto folded =
            api::elicitation::asks_from_state(state, "turn-child");
        bool child = false;
        for (const auto& a : folded)
            if (a.child_session == "kid-local" && a.seq == 42) child = true;

        if (!child) {
            std::fprintf(stderr,
                         "a parent turn dropped the child's pending ask: %s\n",
                         reported.c_str());
            return 1;
        }
    }

    {
        const auto kid = client.get_session("turn-child", 1);
        if (!kid.ok) {
            std::fprintf(stderr, "child probe attach failed: %s\n",
                         kid.error.c_str());
            return 1;
        }
        bool sawChild = false;
        for (const auto& a : kid.value.pending_asks) {
            if (a.child_session != "kid-local" || a.seq != 41) continue;
            sawChild = true;
            int text = 0, file = 0;
            for (const auto& q : a.questions) {
                if (q.control == api::AskControl::Text) ++text;
                if (q.control == api::AskControl::File) ++file;
            }
            if (text != 1 || file != 1) {
                std::fprintf(stderr,
                             "a child's prose question was not recovered: "
                             "%d text, %d file\n",
                             text, file);
                return 1;
            }
        }
        if (!sawChild) {
            std::fprintf(stderr, "the child ask never reached the parent\n");
            return 1;
        }
    }

    std::vector<std::string> retractAsks;
    api::StreamSink retractSink;
    retractSink.on_event = [&retractAsks](const api::StreamEvent& ev) {
        if (ev.kind == api::StreamEventKind::AsksChanged)
            retractAsks.push_back(ev.payload);
    };
    client.send_message_streaming("turn-retract", "done with it", retractSink);
    if (retractAsks.size() < 2) {
        std::fprintf(stderr,
                     "a child retract produced no second report (%zu)\n",
                     retractAsks.size());
        return 1;
    }
    {
        const auto seeded =
            nlohmann::json::parse(retractAsks.front(), nullptr, false);
        const auto after =
            nlohmann::json::parse(retractAsks.back(), nullptr, false);
        const auto before =
            api::elicitation::asks_from_state(seeded, "turn-retract");
        const auto after_asks =
            api::elicitation::asks_from_state(after, "turn-retract");
        if (before.size() != 2) {
            std::fprintf(stderr, "the child seed did not carry two asks: %zu\n",
                         before.size());
            return 1;
        }
        if (after_asks.size() != 1 || after_asks[0].seq != 42 ||
            after_asks[0].child_session != "kid-local") {
            std::fprintf(stderr,
                         "retracting kid-local#41 did not leave exactly "
                         "kid-local#42: %zu remain\n",
                         after_asks.size());
            for (const auto& a : after_asks)
                std::fprintf(stderr, "  still: %s\n", a.id().c_str());
            return 1;
        }
    }

    const int bounded = close_waits_for_a_running_callback(
        std::string("127.0.0.1:") + port, 7000, /*expectWait=*/false);
    if (bounded != 0) {
        if (bounded < 0)
            std::fprintf(stderr, "the bound arm never delivered a message\n");
        return 1;
    }

    std::printf("OK\n");
    return 0;
}
