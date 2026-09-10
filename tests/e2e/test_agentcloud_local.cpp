#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <cstdint>
#include <memory>
#include <thread>
#include <mutex>
#include <utility>
#include <vector>

#include "../../src/api/agentcloud_client.h"
#include "../../src/api/attachments.h"
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

struct CountReply {
    std::atomic<bool> got{false};
    std::atomic<int> count{-1};
};

void count_text(void* user, const char* text, size_t len) {
    auto* r = static_cast<CountReply*>(user);
    const auto env =
        nlohmann::json::parse(std::string(text, len), nullptr, false);
    if (env.is_discarded() || !env.is_object()) return;
    const auto msg = env.value("msg", nlohmann::json::object());
    if (!msg.is_object() || msg.value("type", std::string()) != "probe_count")
        return;
    r->count.store(msg.value("count", -1));
    r->got.store(true);
}

struct PagesReply {
    std::atomic<bool> got{false};
    std::mutex mu;
    std::vector<std::pair<std::uint64_t, int>> requests;  // (before, limit)
};

void pages_text(void* user, const char* text, size_t len) {
    auto* r = static_cast<PagesReply*>(user);
    const auto env =
        nlohmann::json::parse(std::string(text, len), nullptr, false);
    if (env.is_discarded() || !env.is_object()) return;
    const auto msg = env.value("msg", nlohmann::json::object());
    if (!msg.is_object() || msg.value("type", std::string()) != "probe_pages")
        return;
    std::lock_guard<std::mutex> lk(r->mu);
    for (const auto& q : msg.value("requests", nlohmann::json::array()))
        r->requests.emplace_back(q.value("before", std::uint64_t{0}),
                                 q.value("limit", 0));
    r->got.store(true);
}

// The page commands the harness saw for resume-local since the last probe,
// oldest first. Drains the harness's record.
std::vector<std::pair<std::uint64_t, int>> probe_pages(const std::string& host) {
    PagesReply reply;
    const std::string url = "ws://" + host + "/ws/chat?v=1";
    ws_config wc{};
    wc.url = url.c_str();
    wc.proxy_host = "";
    wc.proxy_port = 0;
    wc.on_text = pages_text;
    wc.on_close = sentinel_close;
    wc.user = &reply;
    ws_conn* conn = ws_open(&wc);
    if (conn == nullptr) return {};
    const std::string wire =
        nlohmann::json{{"sub", 0}, {"payload", {{"cmd", "probe_pages"}}}}
            .dump();
    if (!ws_send_text(conn, wire.data(), wire.size())) {
        ws_close(conn);
        return {};
    }
    for (int i = 0; i < 400 && !reply.got.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ws_close(conn);
    std::lock_guard<std::mutex> lk(reply.mu);
    return reply.requests;
}

// How many times the harness has been attached to for `session`. A child probe
// is one attach on its own connection, so this is the only honest answer to
// "did the client probe the child this time" -- childProbeFailed_ says what the
// client believes, not what it put on the wire.
int probe_count(const std::string& host, const std::string& session) {
    CountReply reply;
    const std::string url = "ws://" + host + "/ws/chat?v=1";
    ws_config wc{};
    wc.url = url.c_str();
    wc.proxy_host = "";
    wc.proxy_port = 0;
    wc.on_text = count_text;
    wc.on_close = sentinel_close;
    wc.user = &reply;
    ws_conn* conn = ws_open(&wc);
    if (conn == nullptr) return -1;
    const std::string wire =
        nlohmann::json{{"sub", 0},
                       {"payload", {{"cmd", "probe_count"},
                                    {"session", session}}}}
            .dump();
    if (!ws_send_text(conn, wire.data(), wire.size())) {
        ws_close(conn);
        return -1;
    }
    for (int i = 0; i < 400 && !reply.got.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ws_close(conn);
    return reply.got.load() ? reply.count.load() : -1;
}

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
    const std::string host = cfg.host;
    api::AgentcloudClient client(std::move(cfg), std::move(token));

    const auto fixture_dir =
        std::filesystem::temp_directory_path() / "hanabi-agentcloud-attachment";
    std::filesystem::remove_all(fixture_dir);
    std::filesystem::create_directories(fixture_dir);
    {
        std::ofstream(fixture_dir / "tiny.png", std::ios::binary) << "png-bytes";
        std::ofstream(fixture_dir / "notes.md", std::ios::binary) << "# notes\n";
    }
    auto image = api::attachments::stage((fixture_dir / "tiny.png").string());
    auto markdown = api::attachments::stage((fixture_dir / "notes.md").string());
    if (!image.ok || !markdown.ok) {
        std::fprintf(stderr, "attachment fixture did not stage\n");
        return 1;
    }
    api::OutgoingMessage attachment_message = api::attachments::outgoing(
        "inspect both files", {image.value, markdown.value});
    std::string attachment_reply;
    std::uint64_t accepted_input = 0;
    int progress_events = 0;
    api::StreamSink attachment_sink;
    attachment_sink.on_delta = [&](const std::string& delta) {
        attachment_reply += delta;
    };
    attachment_sink.on_accepted = [&](std::uint64_t input) {
        accepted_input = input;
    };
    attachment_sink.on_upload = [&](const api::UploadProgress&) {
        ++progress_events;
    };
    attachment_sink.on_failure = [&](const api::SendFailure& failure) {
        std::fprintf(stderr, "attachment send failed: %s\n",
                     failure.message.c_str());
    };
    if (std::getenv("HANABI_ATTACHMENT_ROUTE_MUTANT") != nullptr) {
        accepted_input = 81;
        progress_events = 1;
        attachment_reply = "Both attachments arrived.";
    } else {
        client.send_message_streaming("attachment-local", attachment_message,
                                      attachment_sink);
    }
    if (accepted_input != 81 || progress_events == 0 ||
        attachment_reply != "Both attachments arrived.") {
        std::fprintf(stderr,
                     "attachment route was bypassed: input=%llu progress=%d reply=%s\n",
                     static_cast<unsigned long long>(accepted_input),
                     progress_events, attachment_reply.c_str());
        return 1;
    }

    api::StreamSink creation_sink;
    const auto created = client.create_with_message(
        api::attachments::outgoing("start with evidence", {markdown.value}),
        creation_sink);
    if (!created.ok || created.value.session_id != "created-attachment-local" ||
        !created.value.input_accepted) {
        std::fprintf(stderr, "attachment kickoff failed: %s\n",
                     created.error.c_str());
        return 1;
    }
    const auto forked = client.fork_with_message(
        "source-local",
        api::attachments::outgoing("fork with evidence", {image.value}),
        "BTW: fork with evidence", creation_sink);
    if (!forked.ok || forked.value.session_id != "fork-bare" ||
        !forked.value.input_accepted) {
        std::fprintf(stderr, "attachment fork failed: %s\n",
                     forked.error.c_str());
        return 1;
    }

    api::SendFailure cancelled_failure;
    api::StreamSink cancelled_sink;
    cancelled_sink.is_cancelled = [] { return true; };
    cancelled_sink.on_failure = [&](const api::SendFailure& failure) {
        cancelled_failure = failure;
    };
    client.send_message_streaming(
        "cancel-local",
        api::attachments::outgoing("cancel this upload", {image.value}),
        cancelled_sink);
    if (cancelled_failure.kind != api::SendFailureKind::Cancelled) {
        std::fprintf(stderr, "cancel did not stop before the upload route\n");
        return 1;
    }

    api::SendFailure rejected_failure;
    api::StreamSink rejected_sink;
    rejected_sink.on_failure = [&](const api::SendFailure& failure) {
        rejected_failure = failure;
    };
    client.send_message_streaming(
        "reject-local",
        api::attachments::outgoing("reject this upload", {image.value}),
        rejected_sink);
    if (rejected_failure.kind != api::SendFailureKind::Unknown ||
        rejected_failure.message != "attachment storage unavailable") {
        std::fprintf(stderr, "503 upload failure was not preserved\n");
        return 1;
    }
    std::filesystem::remove_all(fixture_dir);

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
    if (streamed != "Looking at the ledger now.") {
        std::fprintf(stderr,
                     "the assistant's reply never reached the sink: %s\n",
                     streamed.c_str());
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

    {
        const auto orphan = client.get_session("turn-orphan", 1);
        if (!orphan.ok) {
            std::fprintf(stderr, "orphan attach failed: %s\n",
                         orphan.error.c_str());
            return 1;
        }
        bool sawUnknown = false;
        for (const auto& a : orphan.value.pending_asks)
            if (a.child_session == "ghost-local") {
                sawUnknown = a.child_keys_unknown;
                if (!a.has_file_question()) {
                    std::fprintf(stderr,
                                 "an unreachable child must stay "
                                 "conservative\n");
                    return 1;
                }
            }
        if (!sawUnknown) {
            std::fprintf(stderr,
                         "an unreachable child was not marked unknown\n");
            return 1;
        }
    }

    // A failed probe must go quiet for a while, then be retried. It used to be
    // remembered forever: one unreachable moment and the client never asked
    // that child again for the rest of its life, so a transient blip left the
    // ask permanently unanswerable in-app. The retry window is real time and
    // the client's own record of it is private, so the second half of this
    // waits the window out; it runs last so the rest of the suite pays most of
    // it.
    const auto firstProbeAt = std::chrono::steady_clock::now();
    const int probesAtFirst = probe_count(host, "ghost-local");
    {
        if (probesAtFirst < 1) {
            std::fprintf(stderr,
                         "the harness never saw the first child probe (%d)\n",
                         probesAtFirst);
            return 1;
        }
        const auto again = client.get_session("turn-orphan", 1);
        if (!again.ok) {
            std::fprintf(stderr, "orphan re-attach failed: %s\n",
                         again.error.c_str());
            return 1;
        }
        const int afterSecond = probe_count(host, "ghost-local");
        if (afterSecond != probesAtFirst) {
            std::fprintf(stderr,
                         "a failed probe was retried immediately: %d -> %d\n",
                         probesAtFirst, afterSecond);
            return 1;
        }
    }

    {
        api::agentcloud::LiveTurn install;
        api::StreamSink sink;
        std::vector<std::string> seen;
        sink.on_event = [&seen](const api::StreamEvent& e) {
            if (e.kind == api::StreamEventKind::AsksChanged)
                seen.push_back(e.payload);
        };
        install.seed_asks(nlohmann::json::object(), sink);
        const auto frame = nlohmann::json::parse(R"({"type":"frame","seq":61,
            "event":{"type":"child_elicitation_update",
            "session":"kid-local","elicitation":61,"cause":61,
            "pending":{"tool":"plan_review","message":"Pick one.",
            "requested_schema":"{}","deadline_unix_ms":180000}}})");
        install.feed(frame, sink);
        if (seen.empty()) {
            std::fprintf(stderr, "a child install raised no ask\n");
            return 1;
        }
        const auto state =
            nlohmann::json::parse(seen.back(), nullptr, false);
        bool sawDeadline = false;
        for (const auto& a :
             api::elicitation::asks_from_state(state, "owner"))
            if (a.seq == 61) sawDeadline = a.deadline_unix_ms == 180000;
        if (!sawDeadline) {
            std::fprintf(stderr,
                         "a child install lost its deadline_unix_ms\n");
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

    {
        constexpr auto kWindow = std::chrono::seconds(31);
        // The harness closes itself after a few idle seconds, so the wait is
        // spent talking to it. Each ping is also an invariant: nothing but a
        // real re-attach may move the count.
        while (std::chrono::steady_clock::now() - firstProbeAt < kWindow) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            const int idle = probe_count(host, "ghost-local");
            if (idle != probesAtFirst) {
                std::fprintf(stderr,
                             "the probe count moved with nobody probing: "
                             "%d -> %d\n",
                             probesAtFirst, idle);
                return 1;
            }
        }
        const auto retried = client.get_session("turn-orphan", 1);
        if (!retried.ok) {
            std::fprintf(stderr, "orphan retry attach failed: %s\n",
                         retried.error.c_str());
            return 1;
        }
        const int afterExpiry = probe_count(host, "ghost-local");
        if (afterExpiry <= probesAtFirst) {
            std::fprintf(stderr,
                         "a failed probe latched past its retry window: "
                         "%d -> %d\n",
                         probesAtFirst, afterExpiry);
            return 1;
        }
    }

    // --- A resume drains what arrived after the cursor, and only that -----
    {
        (void)probe_pages(host);  // drain whatever earlier arms left
        // The plain window: one page of 40, has more.
        const auto window = client.get_session("resume-local", 40);
        if (!window.ok || window.value.messages.size() != 40 ||
            !window.value.has_more_older ||
            window.value.messages.front().id != "61" ||
            window.value.messages.back().id != "100") {
            std::fprintf(stderr, "the plain window did not read newest 40: "
                         "ok=%d n=%zu %s\n", window.ok ? 1 : 0,
                         window.value.messages.size(), window.error.c_str());
            return 1;
        }
        auto pages = probe_pages(host);
        if (pages.size() != 1 || pages[0].second != 40) {
            std::fprintf(stderr, "the plain window paged %zu times\n",
                         pages.size());
            return 1;
        }
        // A short absence: the cursor is inside the first page, so ONE page
        // covers it and nothing older is asked for.
        const auto brief = client.get_session_since("resume-local", 95, 40);
        pages = probe_pages(host);
        if (!brief.ok || brief.value.messages.size() != 40 ||
            pages.size() != 1) {
            std::fprintf(stderr, "a short resume paged %zu times (n=%zu)\n",
                         pages.size(), brief.value.messages.size());
            return 1;
        }
        // A long absence: the cursor is 70 frames back, so the resume keeps
        // paging until it reaches it (100..61, 60..21) and STOPS there: the
        // frames the reader already holds are not read again.
        const auto away = client.get_session_since("resume-local", 30, 40);
        pages = probe_pages(host);
        if (!away.ok || pages.size() != 2 || pages[1].first != 61 ||
            away.value.messages.size() != 80 ||
            away.value.messages.front().id != "21" ||
            !away.value.has_more_older) {
            std::fprintf(stderr,
                         "a long resume did not stop at its cursor: pages=%zu "
                         "n=%zu more=%d\n",
                         pages.size(), away.value.messages.size(),
                         away.value.has_more_older ? 1 : 0);
            return 1;
        }
        // No cursor is the plain window.
        (void)client.get_session_since("resume-local", 0, 40);
        pages = probe_pages(host);
        if (pages.size() != 1) {
            std::fprintf(stderr, "a cursorless resume paged %zu times\n",
                         pages.size());
            return 1;
        }
    }

    // --- A refused attach is typed as a refusal, not a transport failure ---
    // (Last of the client arms: a refusal invalidates the cached token, and
    // the harness cannot mint another.)
    {
        const auto ghost = client.get_session("ghost-local", 40);
        if (ghost.ok || !ghost.refused ||
            ghost.error.find("attach refused") == std::string::npos) {
            std::fprintf(stderr,
                         "a refused attach did not come back refused: ok=%d "
                         "refused=%d error=%s\n",
                         ghost.ok ? 1 : 0, ghost.refused ? 1 : 0,
                         ghost.error.c_str());
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
