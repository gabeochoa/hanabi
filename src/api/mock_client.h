#pragma once

// Deterministic, offline sample data source. This is the default backend so
// the app is fully functional with no configuration and no network.
//
// The seed is a PORT of the reference client's own catalog fixture, row for
// row: the same twenty conversations, the same words, in the same activity
// order, so a side-by-side comparison of the two apps measures design and not
// wording. Anything that reads as a difference between the two lists is a
// real difference.
//
// Every row is folderless, unstarred and unarchived, because the reference
// list is one flat activity-ordered column with no grouping. The states are
// chosen so the smart views land on the reference's own counts:
// Blocked 6 (the tag), Review 3 (state==Ready), Home 9 (their sum).
//
// Timestamps are now-based (time(nullptr) - N) so the ages the rows show are
// the same on every run.
// The screenshot baseline suite (docs/screenshots/baselines/) depends on this:
// datum and display are measured from the same moving now, so every rendered
// age ("3h") is constant — reseeding with absolute epochs rots every
// time-showing baseline within a day.
// Nothing here names or encodes any real service, product, or company.

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "client.h"

namespace api {

// THE MOCK'S CLOCK, in one place and overridable.
//
// Every timestamp the mock hands out is `now - N` so that the ages the rows
// show are the same on every run — a fixture seeded with absolute epochs rots
// every time-showing screenshot baseline within a day. That is right for a
// capture and wrong for a DIFF: a message twelve hours old lands on a
// different calendar day depending on what time of night the run happened,
// so the transcript's date dividers change, and so does the widget count that
// scripts/soak.sh's diffable report is built on.
//
// Caught by the report itself. Two long-soak runs a minute apart, the same
// binary, the same arms:
//
//     < open widget.date_divider 2        < churn widget.date_divider 2
//     > open widget.date_divider 1        > churn widget.date_divider 1
//
// which is a four-line diff saying nothing about the app.
//
// HANABI_MOCK_NOW=<unix epoch> pins it. UNSET BY DEFAULT, so every capture,
// every screenshot baseline and every ordinary run behave exactly as before;
// the soak scripts set it because they want two runs to be comparable, which
// is a different thing from wanting them to look right.
inline int64_t mock_now() {
    static const int64_t pinned = [] {
        const char* v = std::getenv("HANABI_MOCK_NOW");
        if (v == nullptr || *v == '\0') return static_cast<int64_t>(0);
        const long long parsed = std::atoll(v);
        return parsed > 0 ? static_cast<int64_t>(parsed) : static_cast<int64_t>(0);
    }();
    return pinned != 0 ? pinned : static_cast<int64_t>(std::time(nullptr));
}

class MockClient : public Client {
  public:
    std::string backend_label() const override { return "mock"; }

    // Attach-only fields, stripped from every catalog row.
    //
    // A `SessionSummary` is the shape of BOTH a list row and the summary the
    // attach hands back, and the mock stores one struct per session -- so a
    // field only the attach can carry would otherwise ride the list too, and
    // the fixture would prove a thing the real client can never do. The wire
    // is the authority here: `channel_replies_paused` is a `WireState` key
    // with no `WireSessionSummary` equivalent, so the catalog cannot know it,
    // and a mock that leaks it lets an untested code path look tested.
    //
    // `frozen` and `archived_at_unix_ms` are NOT stripped: those really are
    // summary-row keys.
    static SessionSummary catalog_row(SessionSummary s) {
        s.replies_paused = false;
        // bz6 exists to be a STALE row beside a fresher attach: its freeze is
        // deliberately withheld from the catalog so the only way the UI can
        // learn it is the attach, which is the propagation path under test.
        if (s.id == "bz6") {
            s.frozen = false;
            s.frozen_by.clear();
            s.frozen_reason.clear();
        }
        return s;
    }

    Result<std::vector<SessionSummary>> list_sessions() override {
        const SeedPtr seedRef = seed_ptr();
        const auto& sessions = *seedRef;
        std::vector<SessionSummary> out;
        out.reserve(sessions.size() + created_.size());
        // created_ holds both composer-created sessions AND live overrides of
        // seed rows that have been replied to this run (see find_mutable). A
        // seed row that has an override is skipped here so it isn't listed
        // twice — the override (with the fresher updated_at/preview) wins.
        // sub-agent counts are already folded into the cached seed rows.
        for (const auto& s : sessions) {
            if (is_overridden(s.summary.id)) continue;
            out.push_back(catalog_row(s.summary));
        }
        for (auto& s : created_) {
            fill_sub_agent_counts(s);
            out.push_back(catalog_row(s.summary));
        }
        // Newest first, but pinned (starred) rise to the top within order.
        std::sort(out.begin(), out.end(),
                  [](const SessionSummary& a, const SessionSummary& b) {
                      return a.updated_at > b.updated_at;
                  });
        return Result<std::vector<SessionSummary>>::success(std::move(out));
    }

    Result<Session> get_session(const std::string& id) override {
        for (auto& s : created_) {
            if (s.summary.id == id) {
                fill_sub_agent_counts(s);
                return Result<Session>::success(s);
            }
        }
        const SeedPtr seedRef = seed_ptr();
        for (const auto& s : *seedRef) {
            if (s.summary.id == id) {
                // Already filled by seed_ptr(); copy the one row out.
                return Result<Session>::success(s);
            }
        }
        const auto children = list_subagents(2000);
        if (children.ok) {
            for (const auto& child : children.value) {
                if (child.id != id) continue;
                Session session;
                session.summary = child;
                Message message;
                message.id = id + "-status";
                message.role = Role::Assistant;
                message.text = child.preview.empty()
                                   ? std::string("Sub-agent session")
                                   : child.preview;
                message.created_at = child.updated_at;
                session.messages.push_back(std::move(message));
                return Result<Session>::success(std::move(session));
            }
        }
        return Result<Session>::failure("no such session: " + id);
    }

    // MEMORY-LIGHT windowed fetch: return only the NEWEST `limit` messages
    // (still oldest-first within the window) and set has_more_older when we
    // truncated, so the newest-N loading path is testable deterministically
    // (see the big HANABI_BIG_TRANSCRIPT fixture). limit <= 0 => full
    // transcript (delegates to get_session(id)). This mirrors the verified
    // live API: ?limit=N returns the newest N ascending + hasMore=true.
    Result<Session> get_session(const std::string& id, int limit) override {
        auto r = get_session(id);
        if (!r.ok || limit <= 0) return r;
        auto& msgs = r.value.messages;
        if (static_cast<int>(msgs.size()) > limit) {
            // Keep the LAST `limit` (newest), preserving order; flag older
            // messages exist beyond the window.
            msgs.erase(msgs.begin(),
                       msgs.end() - static_cast<std::ptrdiff_t>(limit));
            r.value.has_more_older = true;
        }
        // else: has_more_older stays at its Session default (false) — a fresh
        // get_session(id) never sets it true on stored/seed sessions.
        return r;
    }

    // Compose a NEW in-memory session from the prompt. Deterministic id; the
    // prompt becomes the title + the first (user) message. Lives only for this
    // process run (the mock is otherwise stateless) — enough to drive the
    // composer end to end without any backend.
    Result<std::string> create_session(const std::string& prompt) override {
        std::string title = prompt.empty() ? "New task" : prompt;
        if (title.size() > 60) title = title.substr(0, 57) + "...";
        std::string id = "new" + std::to_string(created_.size() + 1);
        Session s;
        s.summary = sum(id, title, 0, "active", ThreadState::Running,
                        ThreadTag::None, "recent", false,
                        prompt.empty() ? "" : prompt);
        if (!prompt.empty()) {
            Message m;
            m.id = id + "-m1";
            m.role = Role::User;
            m.text = prompt;
            m.created_at = 0;
            s.messages.push_back(std::move(m));
        }
        created_.push_back(std::move(s));
        return Result<std::string>::success(id);
    }

    bool supports_fork() const override { return true; }

    Result<std::string> fork_session(const std::string& session_id) override {
        auto source = get_session(session_id);
        if (!source.ok) return Result<std::string>::failure(source.error);
        Session fork = std::move(source.value);
        const std::string id = "fork" + std::to_string(++fork_count_);
        fork.summary.id = id;
        fork.summary.forked_from = session_id;
        fork.summary.parent_id.clear();
        fork.summary.updated_at = mock_now();
        fork.summary.status = "idle";
        fork.summary.state = ThreadState::Parked;
        fork.summary.tag = ThreadTag::None;
        fork.summary.preview.clear();
        fork.summary.starred = false;
        fork.summary.archive_override.reset();
        fork.summary.muted = false;
        fork.sub_agents.clear();
        created_.push_back(std::move(fork));
        return Result<std::string>::success(id);
    }

    Result<std::string> fork_with_prompt(const std::string& session_id,
                                         const std::string& prompt,
                                         const std::string& title) override {
        if (prompt.empty())
            return Result<std::string>::failure("a fork needs a prompt");
        auto source = get_session(session_id);
        if (!source.ok) return Result<std::string>::failure(source.error);
        Session fork = std::move(source.value);
        const std::string id = "fork" + std::to_string(++fork_count_);
        fork.summary.id = id;
        fork.summary.title = title;
        fork.summary.forked_from = session_id;
        fork.summary.parent_id.clear();
        fork.summary.updated_at = mock_now();
        fork.summary.status = "active";
        fork.summary.state = ThreadState::Running;
        fork.summary.tag = ThreadTag::None;
        fork.summary.preview = prompt;
        fork.summary.starred = false;
        fork.summary.archive_override.reset();
        fork.summary.muted = false;
        fork.sub_agents.clear();
        Message seed;
        seed.id = id + "-seed";
        seed.role = Role::User;
        seed.text = prompt;
        seed.created_at = mock_now();
        fork.messages.push_back(std::move(seed));
        created_.push_back(std::move(fork));
        return Result<std::string>::success(id);
    }

    bool supports_subagents() const override { return true; }

    Result<std::vector<SessionSummary>> list_subagents(
        std::size_t limit) override {
        std::vector<SessionSummary> out;
        if (limit == 0)
            return Result<std::vector<SessionSummary>>::success(std::move(out));
        const auto append = [&](const Session& parent) {
            for (const auto& sa : parent.sub_agents) {
                if (out.size() == limit) return;
                SessionSummary child;
                child.id = sa.id;
                child.title = sa.title;
                child.preview = sa.note;
                child.parent_id = parent.summary.id;
                if (sa.state == SubAgentState::Running) {
                    child.state = ThreadState::Running;
                    child.status = "working";
                } else if (sa.state == SubAgentState::Failed) {
                    child.state = ThreadState::Attention;
                    child.tag = ThreadTag::Failed;
                    child.status = "failed";
                } else if (sa.state == SubAgentState::Blocked) {
                    child.state = ThreadState::Attention;
                    child.tag = ThreadTag::Blocked;
                    child.status = "blocked";
                } else {
                    child.state = ThreadState::Ready;
                    child.tag = ThreadTag::Done;
                    child.status = "done";
                }
                out.push_back(std::move(child));
            }
        };
        for (const auto& parent : created_) {
            append(parent);
            if (out.size() == limit) break;
        }
        if (out.size() < limit) {
            const SeedPtr seeds = seed_ptr();
            for (const auto& parent : *seeds) {
                if (is_overridden(parent.summary.id)) continue;
                append(parent);
                if (out.size() == limit) break;
            }
        }
        return Result<std::vector<SessionSummary>>::success(std::move(out));
    }

    // The mock supports replies (this is the demo story): the composer becomes
    // fully functional against it.
    bool supports_send() const override { return true; }

    // The mock supports steering unconditionally (offline demo) so the
    // steer-vs-send decision in the loader can be exercised without a network.
    bool supports_steer() const override { return true; }

    // The mock reads settings offline (feature #4): a deterministic canned
    // UserSettings so the app can exercise "verify setup" with zero config and
    // no network. Mirrors the real /whoami shape the http adapter maps.
    bool supports_settings() const override { return true; }
    Result<UserSettings> get_settings() override {
        UserSettings s;
        s.ok = true;
        s.user_id = "mock-user@example.invalid";
        s.bank_id = "mock-bank";
        s.session_count = static_cast<int64_t>(seed_ptr()->size());
        s.asset_count = 0;
        s.schedule_count = 0;
        s.skill_count = 0;
        s.raw_json =
            R"({"userId":"mock-user@example.invalid","bankId":"mock-bank",)"
            R"("counts":{"sessions":0,"assets":0,"schedules":0,)"
            R"("authoredSkills":0}})";
        return Result<UserSettings>::success(std::move(s));
    }

    // The mock also WRITES settings (zero-config sync target): it simply
    // accepts the pushed UserSettings, stores it in memory, and reports
    // success. This makes the periodic-sync path fully exercisable offline —
    // the web-matches-local story works with no network + no config. The
    // stored copy is inspectable via last_written_settings() for tests.
    bool supports_settings_write() const override { return true; }
    bool update_settings(const UserSettings& s) override {
        last_written_ = s;
        ++write_count_;
        return true;
    }
    // Test/inspection helpers for the in-memory write sink.
    const UserSettings& last_written_settings() const { return last_written_; }
    int settings_write_count() const { return write_count_; }

    // The mock also STREAMS (Phase STREAM): the whole point of the offline demo
    // is a live token-by-token reply with no network. See send_message_streaming
    // and prepare_stream below.
    bool supports_stream() const override { return true; }

    // TEST HOOK — off unless HANABI_MOCK_SEND_FAIL is set, and never true in a
    // normal run. `HANABI_MOCK_SEND_FAIL=N` makes the first N sends (either
    // path) come back as a transport failure, then everything works again.
    //
    // It exists because the offline mock cannot fail, and the one behaviour
    // that only appears on a FAILED send -- the local-first outbox holding the
    // user's prompt and something retrying it -- was therefore unreachable from
    // any test in the repo. That is a large part of why the outbox's read side
    // went unwritten for as long as it did: nothing could exercise it.
    static std::string injected_send_failure() {
        static const int budget = [] {
            const char* v = std::getenv("HANABI_MOCK_SEND_FAIL");
            return (v && *v) ? std::atoi(v) : 0;
        }();
        static int used = 0;
        if (used >= budget) return "";
        ++used;
        return "mock: injected send failure (HANABI_MOCK_SEND_FAIL)";
    }

    // A streamed reply, split into deterministic word/token chunks so the UI
    // fills in incrementally. Two consumers:    //
    //   * send_message_streaming() (below) drives the whole sequence
    //     synchronously (a Thinking event, then every text chunk as a delta,
    //     then Done with the final Message). This is the pure, testable path:
    //     it proves the chunks reassemble into the exact one-shot reply, with
    //     no timers and no network.
    //
    //   * the LOADER wants to drain a few chunks PER FRAME so the bubble fills
    //     over multiple ticks. It calls prepare_stream() to append the turn +
    //     get the chunk vector up front, then feeds chunks to its own sink as
    //     it ticks (no worker thread, no sleep). See loader_system.h.
    //
    // The reply text is identical to synth_reply() (the synchronous mock), so a
    // streamed and a non-streamed send read the same — only the DELIVERY
    // differs. Fully generic: no company/product/service name anywhere.
    struct StreamPlan {
        std::string session_id;      // the session the turn was appended to.
        std::vector<std::string> chunks;  // ordered text deltas (concat = text).
        Message final;               // the fully-assembled assistant Message.
        bool ok = false;
        std::string error;
    };

    // Append the User prompt + a synthetic Assistant reply to the session (the
    // same mutation send_message does), then split that reply into ordered text
    // chunks. Returns the plan so the loader can drain chunks per-frame. On an
    // unknown session returns ok=false with an error (never mutates).
    StreamPlan prepare_stream(const std::string& session_id,
                              const std::string& prompt) {
        StreamPlan plan;
        plan.session_id = session_id;
        Session* target = find_mutable(session_id);
        if (!target) {
            plan.error = "no such session: " + session_id;
            return plan;
        }
        const int64_t now = mock_now();
        const int turn = static_cast<int>(target->messages.size());

        Message user;
        user.id = session_id + "-u" + std::to_string(turn + 1);
        user.role = Role::User;
        user.text = prompt;
        user.created_at = now;
        target->messages.push_back(user);

        Message reply;
        reply.id = session_id + "-a" + std::to_string(turn + 2);
        reply.role = Role::Assistant;
        reply.text = synth_reply(prompt);
        reply.created_at = now + 1;
        target->messages.push_back(reply);

        target->summary.updated_at = now + 1;
        target->summary.preview = one_line(reply.text);

        plan.final = reply;
        plan.chunks = split_chunks(reply.text);
        plan.ok = true;
        return plan;
    }

    // Interface streaming path. Drives the sink with the WHOLE reply in order:
    // a Thinking event, every text chunk as a delta, then Done with the final
    // Message. Deterministic, offline, no timers — the per-frame pacing is the
    // loader's concern (via prepare_stream). This synchronous form is what the
    // test exercises to prove the chunks reassemble to the exact reply.
    void send_message_streaming(const std::string& session_id,
                                const std::string& prompt,
                                const StreamSink& sink) override {
        if (const std::string why = injected_send_failure(); !why.empty()) {
            sink.emit_error(why);
            return;
        }
        StreamPlan plan = prepare_stream(session_id, prompt);
        if (!plan.ok) {
            sink.emit_error(plan.error);
            return;
        }
        sink.emit_event(StreamEvent{StreamEventKind::Thinking, ""});
        for (const auto& c : plan.chunks) sink.emit_delta(c);
        sink.emit_done(plan.final);
    }

    // Continue a session: append the User prompt AND a synthetic, deterministic
    // Assistant reply to that session's transcript, refresh its updated_at +
    // preview so the sidebar reflects the new activity, and return the
    // assistant Message. Fully offline — the reply is generated locally, never
    // fetched. Works on both composer-created sessions (in `created_`) and the
    // static seed cohort (materialized into `created_` on first reply so the
    // appended turn persists for this run).
    Result<Message> send_message(const std::string& session_id,
                                 const std::string& prompt) override {
        if (const std::string why = injected_send_failure(); !why.empty())
            return Result<Message>::failure(why);
        Session* target = find_mutable(session_id);
        if (!target)
            return Result<Message>::failure("no such session: " + session_id);

        const int64_t now = mock_now();
        const int turn = static_cast<int>(target->messages.size());

        // 1) the user's message.
        Message user;
        user.id = session_id + "-u" + std::to_string(turn + 1);
        user.role = Role::User;
        user.text = prompt;
        user.created_at = now;
        target->messages.push_back(user);

        // 2) a synthetic assistant reply. Deterministic, tasteful, and totally
        //    generic — it echoes the ask back so the demo reads naturally,
        //    without naming any product, company, or service.
        Message reply;
        reply.id = session_id + "-a" + std::to_string(turn + 2);
        reply.role = Role::Assistant;
        reply.text = synth_reply(prompt);
        reply.created_at = now + 1;
        target->messages.push_back(reply);

        // 3) reflect the new activity in the summary (sidebar preview + sort).
        target->summary.updated_at = now + 1;
        target->summary.preview = one_line(reply.text);

        return Result<Message>::success(reply);
    }

    // Steer a "running" agent offline. The mock has no real running turn, so it
    // simply appends the user's steering message + a short "(steering) <msg>"
    // acknowledgement and returns the ack — enough for the loader's steer-vs-
    // send routing to be demonstrated with zero network. Deterministic, no
    // company/product names. Mirrors send_message's mutation + preview refresh.
    Result<Message> steer(const std::string& session_id,
                          const std::string& prompt) override {
        Session* target = find_mutable(session_id);
        if (!target)
            return Result<Message>::failure("no such session: " + session_id);

        const int64_t now = mock_now();
        const int turn = static_cast<int>(target->messages.size());

        // 1) the user's steering message.
        Message user;
        user.id = session_id + "-su" + std::to_string(turn + 1);
        user.role = Role::User;
        user.text = prompt;
        user.created_at = now;
        target->messages.push_back(user);

        // 2) a small steering acknowledgement (distinct from a normal reply so
        //    the offline demo reads as an interrupt/redirect, not a fresh turn).
        Message ack;
        ack.id = session_id + "-sa" + std::to_string(turn + 2);
        ack.role = Role::Assistant;
        ack.text = "(steering) " + (prompt.empty() ? std::string("acknowledged")
                                                    : prompt);
        ack.created_at = now + 1;
        target->messages.push_back(ack);

        // 3) reflect the new activity in the summary (sidebar preview + sort).
        target->summary.updated_at = now + 1;
        target->summary.preview = one_line(ack.text);

        return Result<Message>::success(ack);
    }

    bool supports_rename() const override { return true; }

    // Rename offline, echo-shaped: the reply carries the title the "server"
    // settled on (trimmed), so the caller applies the echo rather than its own
    // request. Refusals are real refusals — an empty or over-long title comes
    // back as a failure with the reason, which is what the rename modal shows.
    Result<std::string> rename_session(const std::string& session_id,
                                       const std::string& title) override {
        Session* target = find_mutable(session_id);
        if (!target)
            return Result<std::string>::failure("no such session: " +
                                                session_id);
        const std::string settled = one_line(trimmed(title));
        if (settled.empty())
            return Result<std::string>::failure("title cannot be empty");
        if (settled.size() > kMaxTitleChars)
            return Result<std::string>::failure(
                "title is too long (max " + std::to_string(kMaxTitleChars) +
                " characters)");
        target->summary.title = settled;
        return Result<std::string>::success(settled);
    }

  private:
    static constexpr size_t kMaxTitleChars = 120;

    static std::string trimmed(const std::string& s) {
        const auto is_space = [](unsigned char c) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        };
        size_t b = 0;
        size_t e = s.size();
        while (b < e && is_space(static_cast<unsigned char>(s[b]))) ++b;
        while (e > b && is_space(static_cast<unsigned char>(s[e - 1]))) --e;
        return s.substr(b, e - b);
    }

    // A short, generic acknowledgement. No company/product/service names.
    static std::string synth_reply(const std::string& prompt) {
        if (prompt.empty()) return "Got it \xe2\x80\x94 what would you like me to do?";
        std::string p = one_line(prompt);
        if (p.size() > 80) p = p.substr(0, 77) + "...";
        return "Got it \xe2\x80\x94 working on: " + p +
               ". I'll follow up here as I make progress.";
    }

    // Split a reply into ordered, whitespace-preserving chunks for streaming.
    // Each chunk is a run up to and INCLUDING the following space, so
    // concatenating every chunk reproduces the source text EXACTLY (the
    // streaming test asserts this). Deterministic: same text -> same chunks,
    // no timers, no randomness. An empty reply yields no chunks.
    static std::vector<std::string> split_chunks(const std::string& text) {
        std::vector<std::string> out;
        size_t i = 0;
        const size_t n = text.size();
        while (i < n) {
            size_t sp = text.find(' ', i);
            if (sp == std::string::npos) {
                out.push_back(text.substr(i));
                break;
            }
            out.push_back(text.substr(i, sp - i + 1));  // include the space.
            i = sp + 1;
        }
        return out;
    }

    // Collapse to a single line for preview/echo use (no embedded newlines).
    static std::string one_line(std::string s) {
        for (char& c : s)
            if (c == '\n' || c == '\r') c = ' ';
        return s;
    }

    // The sidebar's sub-agent count, derived from the transcript's own
    // children so the two surfaces can never disagree. "Running" is the only
    // live state; Done and Blocked have both stopped, and a blocked child is
    // exactly the one a settled-coloured count should not claim is working.
    static void fill_sub_agent_counts(Session& s) {
        s.summary.sub_agent_count = static_cast<int>(s.sub_agents.size());
        int live = 0;
        for (const auto& a : s.sub_agents)
            if (a.state == SubAgentState::Running) ++live;
        s.summary.sub_agent_running_count = live;
    }

    // True when a created_ entry shadows a seed row of the same id (a replied-
    // to seed session). Used by list_sessions to avoid listing it twice.
    bool is_overridden(const std::string& id) const {
        for (const auto& s : created_)
            if (s.summary.id == id) return true;
        return false;
    }

    // Find a session by id that we can mutate. Composer-created sessions live
    // in created_ already. A seed session is copied into created_ on first
    // touch so the appended turn persists for the rest of this run (the seed
    // catalog is shared and const — see seed_ptr() — so it cannot hold per-run
    // state itself).
    Session* find_mutable(const std::string& id) {
        for (auto& s : created_)
            if (s.summary.id == id) return &s;
        const SeedPtr seedRef = seed_ptr();
        for (const auto& s : *seedRef) {
            if (s.summary.id == id) {
                created_.push_back(s);
                return &created_.back();
            }
        }
        return nullptr;
    }

    // Sessions created via the composer during this run (mock is otherwise
    // stateless). Merged into list_sessions/get_session above.
    std::vector<Session> created_;
    std::size_t fork_count_ = 0;

    // In-memory sink for the settings-write path (see update_settings). Lets
    // the periodic-sync story run + be asserted offline with zero config.
    UserSettings last_written_;
    int write_count_ = 0;

    static int64_t hrs_ago(int64_t h) {
        // Now-based reference so the sidebar time buckets (Today / This Week /
        // Earlier) always populate relative to when the app is actually run.
        // Deterministic within a run; the RELATIVE ordering of the seed is
        // fixed, which is all the sort/bucket logic (and the tests) rely on.
        const int64_t now = mock_now();
        return now - h * 3600;
    }

    // Convenience: days-ago in the same now-based frame.
    static int64_t days_ago(int64_t d) { return hrs_ago(d * 24); }

    // A transcript row that is an EVENT rather than speech. Not an aggregate
    // literal like the messages above: EventKind sits last in Message (so the
    // literals keep meaning what they meant), and the fields these rows use
    // are the label and the body, not the author.
    static Message event_row(EventKind kind, std::string label,
                             std::string body, int64_t at) {
        Message m;
        m.role = Role::System;
        m.kind = kind;
        m.subtitle = std::move(label);
        m.text = std::move(body);
        m.created_at = at;
        return m;
    }

    // Local noon today. A fixture that wants messages on distinct CALENDAR
    // days cannot subtract 86400 from "now": run at 00:30 and "a day ago" is
    // still yesterday evening, but run at 23:30 and two stamps 25 hours apart
    // can land on the same day either side of a DST shift. Anchoring at noon
    // leaves twelve hours of slack in both directions.
    static int64_t local_noon_today() {
        const std::time_t now = static_cast<std::time_t>(mock_now());
        std::tm tm{};
        if (localtime_r(&now, &tm) == nullptr) return static_cast<int64_t>(now);
        tm.tm_hour = 12;
        tm.tm_min = 0;
        tm.tm_sec = 0;
        tm.tm_isdst = -1;
        return static_cast<int64_t>(std::mktime(&tm));
    }

    // Small helper to build a summary with the full high-signal model.
    static SessionSummary sum(std::string id, std::string title, int64_t h,
                              std::string status, ThreadState state,
                              ThreadTag tag, std::string folder, bool starred,
                              std::string preview) {
        SessionSummary s;
        s.id = std::move(id);
        s.title = std::move(title);
        s.updated_at = hrs_ago(h);
        s.status = std::move(status);
        s.state = state;
        s.tag = tag;
        s.folder = std::move(folder);
        s.starred = starred;
        s.preview = std::move(preview);
        return s;
    }

    // Realistic-cohort summary: shaped like a live backend row. No folder, no
    // tag, not starred; state defaults to Unknown (calm) unless a Running /
    // Archived is passed to model isProcessing / archived. Takes an ABSOLUTE
    // epoch (built from hrs_ago/days_ago) rather than an hours offset, so the
    // caller can spread rows across the time buckets naturally.
    static SessionSummary calm(std::string id, std::string title,
                               int64_t updated_at, std::string status,
                               ThreadState state, std::string preview) {
        SessionSummary s;
        s.id = std::move(id);
        s.title = std::move(title);
        s.updated_at = updated_at;
        s.status = std::move(status);
        s.state = state;             // Unknown / Running / Archived only
        s.tag = ThreadTag::None;     // real rows carry no high-signal tag
        s.folder = "";               // real rows are folderless
        s.starred = false;           // see isPinned note in seed_ptr()
        s.preview = std::move(preview);
        return s;
    }

    // Minutes before now, in the same moving frame as hrs_ago/days_ago. The
    // catalog is seeded in minutes because the rows it mirrors are, and the
    // relative ages ("6m", "5h") have to stay constant across runs.
    static int64_t mins_ago(double m) {
        const int64_t now = mock_now();
        return now - static_cast<int64_t>(m * 60.0 + 0.5);
    }

    // A catalog row, in the shape the reference fixture states one: id, the
    // text the sidebar shows, an age in minutes, and the state/tag pair the
    // glyph and the smart views read. No folder, never starred, never
    // archived — the reference list is one flat activity-ordered column.
    static SessionSummary pf(std::string id, std::string title,
                             double minutesAgo, std::string status,
                             ThreadState state, ThreadTag tag,
                             std::string preview) {
        SessionSummary s;
        s.id = std::move(id);
        s.title = std::move(title);
        s.updated_at = mins_ago(minutesAgo);
        s.status = std::move(status);
        s.state = state;
        s.tag = tag;
        s.folder = "";
        s.starred = false;
        s.preview = std::move(preview);
        return s;
    }

    // THE CATALOG, BUILT ONCE.
    //
    // This used to be `static std::vector<Session> seed()` returning BY VALUE,
    // and it is called from list_sessions(), get_session(), get_settings() and
    // find_mutable(). Every one of those rebuilt the entire fixture --  every
    // Session, every Message, every std::string in them -- and three of the
    // four then threw all but a sliver of it away. get_settings() built the
    // whole catalog to read .size() off it.
    //
    // MEASURED at a 2020-row catalog (CLOCK_THREAD_CPUTIME_ID):
    //     get_session(one id)   6.76 ms   -> 0.056 ms
    //     get_settings()        6.56 ms   -> 0.000 ms
    //     list_sessions()       6.95 ms   -> 0.53 ms
    //
    // The fixture is deterministic and nothing mutates it (fill_sub_agent_
    // counts derives from the row's own sub_agents, so it is folded in here
    // once; find_mutable copies a row into created_ before touching it). So it
    // is built once and handed out by const reference.
    //
    // The cache is keyed on EVERY environment variable the fixture reads, not
    // just the catalog size. This is not defensive coding, it is a bug that
    // already happened: the e2e runner loads a whole DIRECTORY of scripts into
    // ONE process (main.cpp, load_scripts_from_directory) and applies each
    // script's own `# env:` line before running it. Keying on
    // HANABI_STRESS_SESSIONS alone froze the first script's fixture and served
    // it to every later script, so thinking_disclosure (HANABI_THINKING_DEMO=1,
    // HANABI_OPEN=rthink) and tool_fold_persists (HANABI_FOLD_DEMO=1,
    // HANABI_OPEN=rfold) opened threads that did not exist in the cached
    // catalog and timed out waiting for text that was never going to appear.
    //
    // It is ORDER-DEPENDENT, which is why it passed a full suite run before it
    // failed one: whichever script runs first decides what everybody gets.
    //
    // Anything added to build_seed() that reads the environment MUST be added
    // here too. That coupling is the price of the cache; the alternative was
    // rebuilding a 2000-row catalog on every get_session().
    static constexpr const char* kFixtureEnv[] = {
        "HANABI_STRESS_SESSIONS", "HANABI_MD_DEMO",   "HANABI_THINKING_DEMO",
        "HANABI_FOLD_DEMO",       "HANABI_CODE_DEMO", "HANABI_DATES_DEMO",
        "HANABI_LONGMSG_DEMO",    "HANABI_BIG_TRANSCRIPT", "HANABI_BIG_TURNS",
        "HANABI_BIG_EVENTS",       "HANABI_FOLDER_DEMO",
        "HANABI_STRESS_PINNED",   "HANABI_STRESS_ARCHIVED",
        "HANABI_BRAKES_DEMO",
    };
    // ONE TURN OF A SYNTHETIC THREAD, in the shape a real one has.
    //
    // WHY THIS IS NOT ONE ASK AND ONE PARAGRAPH. It was, and that made every
    // measurement ever taken against a big catalog a measurement of the
    // CHEAPEST render path this app has. The hand-written twenty carry tool
    // rows, thinking rows, code fences and failed runs; the synthetic two
    // thousand carried a user line and one paragraph of prose, so
    // `HANABI_STRESS_SESSIONS=2000` scaled up the part of the transcript that
    // costs nothing and left out the part that costs everything.
    //
    // Puffin hit this first and its PERFORMANCE.md opens on it: everything it
    // had measured came from a mock with "20 fixture rows" that "hides every
    // problem below". Its fix, Tests/StressFixtures.swift, generates a turn as
    // thinking + tool_use + tool intent + tool result + text, with a failure
    // every seventh turn and a sub-agent delivery every fourth. This is the
    // same shape in hanabi's own message model, and the ratios are Puffin's
    // rather than invented so the two apps' stress catalogs are comparable.
    //
    // DETERMINISTIC FROM (session, turn) ALONE — no clock, no randomness, no
    // counter. Two runs generate identical bytes, which is what
    // scripts/soak.sh's diffable report and every scaling ratio depend on.
    static std::vector<Message> stress_turn(int k, int t) {
        static const char* kNouns2[] = {
            "the quota shard", "row 212's ledger", "the retry queue",
            "oncall handoff", "the ranking config",
            "a cohort that will not converge", "the nightly export"};
        static const char* kTools[] = {"bash", "read_file", "grep", "python",
                                       "edit_file"};
        const std::string sid = "s" + std::to_string(k);
        const std::string tid = sid + "t" + std::to_string(t);
        const bool failed = (t % 7 == 6);
        std::vector<Message> out;

        Message u;
        u.id = tid + "u";
        u.role = Role::User;
        u.text = "turn " + std::to_string(t) + ": " + kNouns2[(k + t) % 7] +
                 " is still moving, what changed?";
        u.created_at = mins_ago(720 - t * 3);
        out.push_back(std::move(u));

        // A thinking row. Folded by default, so it costs the fold machinery
        // and the disclosure chip rather than a wall of text -- which is
        // exactly the cost a real thread carries and the old fixture had none
        // of.
        Message th;
        th.id = tid + "k";
        th.role = Role::Assistant;
        th.subtitle = "thinking";
        th.text = "Two readings. The cheap one is a stale cache. The honest "
                  "one is that the derivation runs per frame, and " +
                  std::string(kNouns2[(k + t + 3) % 7]) +
                  " is downstream of it, so the same walk happens twice.";
        th.created_at = mins_ago(720 - t * 3);
        out.push_back(std::move(th));

        // A tool row WITH ITS OUTPUT, its status and the node it ran on. The
        // nested output sub-row is a second wrapped text block per turn and
        // the status drives a glyph; neither existed in the old fixture.
        Message tool;
        tool.id = tid + "c";
        tool.role = Role::Tool;
        tool.subtitle = kTools[t % 5];
        tool.text = std::string(kTools[t % 5]) + " " +
                    (t % 5 == 2 ? "-rn \"retry\" worker/" : "worker/queue.rs");
        tool.tool_result =
            failed ? "error: no such file or directory (os error 2)\n"
                     "  while reading worker/queue.rs at revision " +
                         std::to_string(1000 + t)
                   : "worker/queue.rs:" + std::to_string(40 + t) +
                         ":    let delay = full_jitter(base, attempt);\n"
                         "worker/queue.rs:" + std::to_string(58 + t) +
                         ":    // CAP is 30s; see the incident writeup\n"
                         "3 hits in 2 files";
        tool.tool_status = failed ? "failed" : "completed";
        tool.tool_duration_ms = 40 + (t * 17) % 900;
        tool.tool_node = (t % 3 == 0) ? "cli:aspen" : "devvm4827";
        tool.created_at = mins_ago(720 - t * 3);
        out.push_back(std::move(tool));

        // The reply. Every third one carries a fenced code block, because a
        // fence is the single most expensive thing a reply can contain in this
        // renderer and the old fixture never produced one.
        Message a;
        a.id = tid + "a";
        a.role = Role::Assistant;
        a.text = "Two readings of turn " + std::to_string(t) +
                 ". The cheap one is a stale cache; the honest one is that the "
                 "derivation runs per frame, which is the shape every finding "
                 "here has taken so far.";
        if (t % 3 == 0)
            a.text += "\n\n```rust\nlet delay = full_jitter(base, attempt)\n"
                      "    .min(Duration::from_secs(30));\n```\n";
        a.created_at = mins_ago(719 - t * 3);
        // The run's own word for how it ended, which draws a rule under the
        // message. A seventh of runs fail -- Puffin's ratio.
        a.run_outcome = failed ? "failed" : "completed";
        out.push_back(std::move(a));

        // A sub-agent delivery every fourth turn, which is a user-origin
        // message the transcript renders differently.
        if (t % 4 == 3) {
            Message d;
            d.id = tid + "d";
            d.role = Role::User;
            d.text = "subagent " + sid + "-" + std::to_string(t) +
                     ": 14/14 green, handed back";
            d.created_at = mins_ago(719 - t * 3);
            out.push_back(std::move(d));
        }
        return out;
    }

    using SeedPtr = std::shared_ptr<const std::vector<Session>>;

    // The catalog, as a value the caller OWNS for as long as it reads it.
    //
    // This returned `const std::vector<Session>&` and took a mutex around the
    // rebuild. The lock was released at the return, so it serialised rebuild
    // against rebuild and left rebuild against READ -- which is the race the
    // comment claimed to fix and the only one that can happen here.
    // list_sessions() runs under std::async and iterates the reference while
    // the main thread can be in get_session(); one `s_cache = build_seed()`
    // reallocates the buffer under that live iterator. Found by the commit
    // audit, docs/COMMIT_AUDIT.md.
    //
    // A shared_ptr to an IMMUTABLE vector removes the race by construction
    // rather than by timing: a rebuild publishes a NEW vector and the old one
    // stays alive until the last reader drops it. Copying the pointer is one
    // atomic increment and allocates nothing, so scripts/alloc_gate.sh does
    // not move.
    static SeedPtr seed_ptr() {
        static std::mutex mu;
        static std::string s_key;
        static SeedPtr s_cache;
        std::string key;
        for (const char* name : kFixtureEnv) {
            const char* v = std::getenv(name);
            key += (v != nullptr) ? v : "";
            key += '\x1f';  // a separator no env value will contain
        }
        std::lock_guard<std::mutex> lk(mu);
        if (!s_cache || key != s_key) {
            auto fresh = std::make_shared<std::vector<Session>>(build_seed());
            for (auto& s : *fresh) fill_sub_agent_counts(s);
            s_cache = std::move(fresh);
            s_key = key;
        }
        return s_cache;
    }

    static std::vector<Session> build_seed() {
        std::vector<Session> v;

        // --- running ---
        {
            Session s;
            s.summary = pf("r5", "profiling the disk", 0, "active",
                           ThreadState::Running, ThreadTag::None,
                           "walking the tree by size");
            s.messages = {
                {"m1", Role::User,
                 "the build box is at 96% disk. work out what is eating it.",
                 mins_ago(9), ""},
                {"m2", Role::Assistant,
                 "Profiling the disk now, biggest directories first. Nothing "
                 "to decide yet - I'll come back with the top ten and what is "
                 "safe to drop.",
                 mins_ago(6), ""},
                {"m3", Role::Tool,
                 "du -sh /* | sort -h \xe2\x86\x92 in progress", mins_ago(1),
                 "shell"},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = pf("r6", "two workers still out", 2, "active",
                           ThreadState::Running, ThreadTag::None,
                           "1 of 3 workers has reported");
            s.messages = {
                {"m1", Role::User,
                 "find feature flags that have been at 100% for 90+ days and "
                 "list the ones safe to remove",
                 mins_ago(20), ""},
                {"m2", Role::Assistant,
                 "Registry scan is done - 412 flags enumerated. Two workers "
                 "are still resolving call sites for the 90-day set. Running "
                 "quietly until they land.",
                 mins_ago(2), ""},
            };
            s.sub_agents = {
                {"r6s1", "Call-site trace", SubAgentState::Running,
                 "resolving usages for the 90-day set"},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = pf("t9", "kicker-tick", 3, "active",
                           ThreadState::Running, ThreadTag::None,
                           "draining the queue");
            s.messages = {
                {"m1", Role::System,
                 "Recurring: kick the pending queue every ten minutes.",
                 mins_ago(13), ""},
                {"m2", Role::Assistant,
                 "Kicked 41 pending items. Draining what came back; nothing "
                 "for you yet.",
                 mins_ago(3), ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = pf("t7", "coordinating 3 shard workers", 6, "active",
                           ThreadState::Running, ThreadTag::None,
                           "1 of 3 shards landed");
            s.messages = {
                {"m1", Role::System,
                 "Task: migrate the quota shards, one worker per shard.",
                 mins_ago(40), ""},
                {"m2", Role::Assistant,
                 "Three workers out, one per shard. Legacy tenants are done; "
                 "the active-tenant shard is mid-copy and the canary cohort "
                 "is waiting on an approval before it touches anything.",
                 mins_ago(6), ""},
            };
            s.sub_agents = {
                {"t7s1", "shard 1/3 \xe2\x80\x94 legacy tenants",
                 SubAgentState::Done, "copied and verified"},
                {"t7s2", "shard 2/3 \xe2\x80\x94 active tenants",
                 SubAgentState::Running, "at row 812k"},
                {"t7s3", "shard 3/3 \xe2\x80\x94 canary cohort",
                 SubAgentState::Blocked,
                 "needs approval before touching the canary cohort"},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            // Working, NOT Running: the reference's own fixture has this row
            // as testimony that says "working" with no live run
            // (`mock-working-1`, `resolvedKind: "testimony"`, running absent),
            // which is why Puffin draws it a steady dot where the four rows
            // above it get the spinner.
            s.summary = pf("t8", "triaging row 212", 6.1, "active",
                           ThreadState::Working, ThreadTag::None,
                           "reading the row's history");
            s.messages = {
                {"m1", Role::System,
                 "Continuous triage: pick up the oldest untriaged row and work "
                 "out who owns it.",
                 mins_ago(24), ""},
                {"m2", Role::Assistant,
                 "Row 212 is the one with no owner and two conflicting "
                 "repro reports. Reading its history before I route it.",
                 mins_ago(6), ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            // Working, not Running, for the same reason as the row above:
            // `mock-renamed-1` is testimony ("working") on a thread with no
            // live run.
            s.summary = pf("t6", "SKU backfill \xe2\x80\x94 my name for it", 8,
                           "active", ThreadState::Working, ThreadTag::None,
                           "sweeping 412 rows for missing SKUs");
            s.messages = {
                {"m1", Role::System,
                 "Task: backfill the entitlement table for legacy "
                 "subscribers.",
                 mins_ago(120), ""},
                {"m2", Role::Assistant,
                 "Backfill in progress - 61% through 2.1M rows. No action "
                 "needed; I'll surface it when done or if I hit a snag.",
                 mins_ago(8), ""},
            };
            v.push_back(std::move(s));
        }

        // --- waiting on you ---
        {
            Session s;
            s.summary = pf("t1",
                           "stickers broke \xe2\x80\x94 concluded, D113637134 "
                           "on you",
                           11, "active", ThreadState::Attention,
                           ThreadTag::Blocked,
                           "needs a decision before landing");
            s.messages = {
                {"m1", Role::System,
                 "Task: land the multi-tier pricing config once CI is green.",
                 hrs_ago(3), ""},
                {"m2", Role::Assistant,
                 "Built the config diff D948120 adding Tier 1/2/3 price "
                 "points. Running the shadow comparison against prod now.",
                 hrs_ago(2), ""},
                {"m3", Role::Tool,
                 "shadow_compare \xe2\x86\x92 4,812 accounts \xc2\xb7 max "
                 "delta 0.3% \xc2\xb7 within tolerance",
                 hrs_ago(1), ""},
                {"m4", Role::Assistant,
                 "All CI green, shadow matched. Nothing else is blocking. I "
                 "need your approval to land.",
                 mins_ago(11), ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            // Failed, not Blocked: the run DIED (two shards lost to an OOM).
            // The reference draws it a red cross where its blocked rows get a
            // bang, and the fixture it copies says the same thing — a testimony
            // of "failed" rather than one of "blocked".
            s.summary = pf("r7", "two shards died", 12, "active",
                           ThreadState::Attention, ThreadTag::Failed,
                           "the run failed, two shards lost");
            s.messages = {
                {"m1", Role::System,
                 "Task: fan the reindex out over eight shards and report.",
                 hrs_ago(2), ""},
                {"m2", Role::Assistant,
                 "Six shards finished clean. Two died partway with the same "
                 "OOM and did not retry, so the reindex is incomplete. I have "
                 "the logs; I can rerun just those two on a bigger worker, or "
                 "you can look first.",
                 mins_ago(12), ""},
            };
            s.sub_agents = {
                {"r7s1", "the shard that died", SubAgentState::Failed,
                 "OOM at 4.1GB, no retry"},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = pf("t2", "needs a decision before it can go on", 14,
                           "active", ThreadState::Attention,
                           ThreadTag::Blocked, "two accounts do not reconcile");
            // t2 is the thread the visual-parity capture opens, and it opens
            // on a USER turn on purpose. Puffin's own reference frame
            // (docs/visual-parity/ref/01_home.png) is a transcript whose first
            // row is right-aligned — an avatar and a shrink-to-fit bubble —
            // and a fixture that opens on a centred System line is not the
            // same picture no matter how well the bubbles below it are drawn.
            // Every other thread here still opens on a System task line, so
            // the centred meta row keeps its fixture coverage.
            s.messages = {
                {"m1", Role::User,
                 "Reconcile this cycle's Stars payouts against the ledger "
                 "before the batch runs.",
                 hrs_ago(5), ""},
                {"m2", Role::Assistant,
                 "Reconciled 4,812 accounts. 4,810 match to the cent. Two "
                 "don't:\n"
                 "  acct 8842 - ledger $128.60, computed $116.20 (delta "
                 "$12.40)\n"
                 "  acct 1097 - ledger $54.10, computed $51.00 (delta $3.10)",
                 hrs_ago(0) - 2400, ""},
                {"m3", Role::Assistant,
                 "Both look like a rounding artifact in a promo credit, but "
                 "proceeding commits the batch - it's irreversible for this "
                 "cycle. Hold, or proceed and I'll flag the two for manual "
                 "follow-up?",
                 hrs_ago(0) - 1320, ""},
            };
            s.sub_agents = {
                {"t2s1", "Ledger diff scan", SubAgentState::Running,
                 "comparing 4,812 rows against the ledger"},
                {"t2s2", "Duplicate-payout check", SubAgentState::Done,
                 "no duplicates found"},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = pf("t4", "finished, and wants you to read it", 16,
                           "active", ThreadState::Ready, ThreadTag::Review,
                           "wrote the report, ready for a look");
            s.messages = {
                {"m1", Role::System,
                 "Task: implement in-app upgrade from Tier 1 -> Tier 2 with "
                 "proration.",
                 hrs_ago(4), ""},
                {"m2", Role::Assistant,
                 "Implemented the upgrade path with a proration line item on "
                 "the receipt. The core of it:\n"
                 "```ts\n"
                 "function prorate(from: Tier, to: Tier, daysLeft: number) {\n"
                 "  const delta = to.price - from.price;\n"
                 "  return Math.round(delta * (daysLeft / 30) * 100) / 100;\n"
                 "}\n"
                 "```\n"
                 "Wrote 18 new tests.",
                 hrs_ago(1), ""},
                {"m3", Role::Tool,
                 "make test \xe2\x86\x92 214 passed, 0 failed \xc2\xb7 lint "
                 "clean \xc2\xb7 type check clean",
                 hrs_ago(0) - 2100, "shell"},
                {"m4", Role::Assistant,
                 "Deployed to staging. Test link is ready - subscribe at Tier "
                 "1, upgrade to Tier 2, and confirm the proration line.",
                 mins_ago(16), ""},
            };
            for (auto& mm : s.messages) {
                if (mm.id == "m3") {
                    mm.tool_result =
                        "Running 214 tests across 18 suites\xe2\x80\xa6\n"
                        "  subscriptions/upgrade ... ok (26 tests)\n"
                        "  billing/proration ...... ok (18 tests)\n"
                        "  receipts/lineitems ..... ok (12 tests)\n"
                        "214 passed, 0 failed in 26.0s\n"
                        "lint: clean  \xc2\xb7  typecheck: clean";
                    mm.tool_status = "completed";
                    mm.tool_duration_ms = 26000;
                }
            }
            s.sub_agents = {
                {"t4s1", "wrote the report", SubAgentState::Done,
                 "18 tests, all green"},
            };
            v.push_back(std::move(s));
        }

        // --- settled, and the long tail ---
        {
            Session s;
            s.summary = pf("r8", "quota migration, week 3", 24, "active",
                           ThreadState::Unknown, ThreadTag::None,
                           "week 3 of 6, on schedule");
            s.messages = {
                {"m1", Role::User,
                 "where is the quota migration up to?", mins_ago(40), ""},
                {"m2", Role::Assistant,
                 "Week three of six. Two of the five tenant classes are fully "
                 "on the new quota service, the third is shadowing, and the "
                 "last two are scheduled for weeks five and six. Nothing has "
                 "needed a rollback.",
                 mins_ago(24), ""},
            };
            // The event classes a real session emits and this thread is the
            // mock's sample of: a node, a skill, a spawn, a delivery, a
            // status. Built field-by-field rather than as literals because
            // EventKind is the LAST member of Message and these rows are
            // about the kind, not the author.
            s.messages.push_back(
                event_row(EventKind::Node, "od-4471.quota", "attached",
                          mins_ago(39)));
            s.messages.push_back(
                event_row(EventKind::Skill, "presto-query", "platform",
                          mins_ago(38)));
            api::Message spawned =
                event_row(EventKind::SubAgent, "tenant class 4 dry-run",
                          "Shadow class 4 against the new quota service for "
                          "an hour and report the divergences",
                          mins_ago(36));
            spawned.tool_status = "completed";
            s.messages.push_back(std::move(spawned));
            s.messages.push_back(
                event_row(EventKind::Delivery, "child",
                          "child session settled: completed\n\nNo divergences "
                          "over 61 minutes and 1.2M requests. Class 4 is safe "
                          "to cut over in week five.",
                          mins_ago(28)));
            s.messages.push_back(
                event_row(EventKind::Status, "working",
                          "shadowing class 3", mins_ago(26)));
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = pf("r1", "auto-stars \xe2\x80\x94 stars decision on you",
                           42, "active", ThreadState::Attention,
                           ThreadTag::Blocked,
                           "looked at the jittered backoff, one edge case left");
            s.messages = {
                {"m1", Role::User,
                 "the sync worker retries too aggressively when the upstream "
                 "is flapping. can you add jittered backoff?",
                 hrs_ago(2), ""},
                {"m2", Role::Assistant,
                 "Sure. I'll switch the fixed 200ms retry to full-jitter "
                 "exponential backoff capped at 30s and add a circuit breaker "
                 "after 5 consecutive failures. Let me find the retry loop.",
                 hrs_ago(2), ""},
                {"m3", Role::Tool, "grep -rn \"retry\" worker/ \xe2\x86\x92 11 hits",
                 hrs_ago(2), "shell"},
                {"m4", Role::Tool,
                 "edit worker/sync_loop.rs \xc2\xb7 +34 -8", hrs_ago(1), "editor"},
                {"m4b", Role::Tool,
                 "Verify the backoff math + add a property test for the jitter "
                 "bounds across 10k attempts",
                 hrs_ago(1), "spawn_agent"},
                {"m5", Role::Assistant,
                 "Backoff is in. One edge case: on a cold start the breaker "
                 "opens immediately if the very first call fails. I can seed "
                 "it half-open instead \xe2\x80\x94 want that, or leave it strict?"
                 "\n\nHere's the retry schedule I landed on:\n\n"
                 "| Attempt | Delay | Jitter |\n"
                 "|---------|-------|--------|\n"
                 "| 1 | 200ms | \xc2\xb1 50ms |\n"
                 "| 2 | 800ms | \xc2\xb1 200ms |\n"
                 "| 3 | 3.2s | \xc2\xb1 800ms |\n"
                 "| 4+ | 30s cap | full |\n",
                 mins_ago(42), ""},
            };
            for (auto& mm : s.messages)
                if (mm.role == Role::Tool) mm.tool_node = "cli:aspen";
            for (auto& mm : s.messages) {
                if (mm.role != Role::Tool) continue;
                if (mm.id == "m3") {
                    mm.tool_result =
                        "worker/sync_loop.rs:42  retry_backoff(attempt)\n"
                        "worker/sync_loop.rs:58  const MAX_RETRIES = 5\n"
                        "worker/breaker.rs:11    fn should_open()\n"
                        "\xe2\x80\xa6 8 more matches";
                    mm.tool_status = "completed";
                    mm.tool_duration_ms = 1200;
                } else if (mm.id == "m4") {
                    mm.tool_result =
                        "@@ worker/sync_loop.rs\n"
                        "- let delay = Duration::from_millis(200);\n"
                        "+ let delay = full_jitter(base, attempt).min(CAP);\n"
                        "+ if breaker.should_open() { return Err(Tripped); }\n"
                        "34 insertions(+), 8 deletions(-)";
                    mm.tool_status = "completed";
                    mm.tool_duration_ms = 3400;
                }
            }
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = pf("r4", "oncall sweep finished \xe2\x80\x94 3 rows need "
                                 "a look",
                           51, "active", ThreadState::Ready, ThreadTag::Review,
                           "first pass done, grouped by area");
            s.messages = {
                {"m1", Role::User, "draft release notes for the 4.2 cut",
                 hrs_ago(7), ""},
                {"m2", Role::Assistant,
                 "Pulled the 62 landed changes since 4.1 and grouped them: "
                 "Performance (9), Bug fixes (31), New (7), Internal (15). "
                 "First draft is written \xe2\x80\x94 want it terser, or is this the "
                 "right level of detail?",
                 mins_ago(51), ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = pf("t5", "watchdog clean for 6h", 96, "active",
                           ThreadState::Ready, ThreadTag::Review,
                           "six hours clean, nothing to do");
            s.messages = {
                {"m1", Role::System,
                 "Watchdog: poll the dev CLI every five minutes and report a "
                 "sustained failure.",
                 hrs_ago(8), ""},
                {"m2", Role::Assistant,
                 "Six hours of polling, every probe green. No restarts, no "
                 "timeouts, no degraded window. There is nothing to fix here - "
                 "read it and close it.",
                 mins_ago(96), ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = pf("t3", "style guide written", 140, "idle",
                           ThreadState::Unknown, ThreadTag::Done,
                           "three variants, brand-voice clean");
            s.messages = {
                {"m1", Role::System,
                 "Task: draft welcome QP copy for new Stars creators. 3 "
                 "variants, brand-voice compliant.",
                 hrs_ago(4), ""},
                {"m2", Role::Assistant,
                 "Three variants, all passing the brand-voice check:\n"
                 "  A (warmest): Welcome - your fans can now support you "
                 "directly. Let's set up Stars.\n"
                 "  B (clearest): Stars are on. Turn on receiving to start "
                 "earning from your fans.\n"
                 "  C (shortest): You're in. Set up Stars.",
                 hrs_ago(3), ""},
                {"m3", Role::Assistant,
                 "Wrote the style guide up from those three. B scored highest "
                 "on clarity; A is warmest. Done here.",
                 mins_ago(140), ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            // The other testified failure (`mock-failed-1`: state "failed" AND
            // a failed outcome), and the reference's other red cross.
            s.summary = pf("r2", "import failed twice", 200, "active",
                           ThreadState::Attention, ThreadTag::Failed,
                           "reproduced locally, looks like a fixture race");
            s.messages = {
                {"m1", Role::User,
                 "checkout_flow_test fails ~1 in 20 on CI. can you figure out "
                 "why?",
                 hrs_ago(6), ""},
                {"m2", Role::Assistant,
                 "Pulled the last 40 CI runs \xe2\x80\x94 **6 failures**, all on the same "
                 "assertion (`cart.total` off by one line item). Smells like a "
                 "fixture setup race in `seed_cart()`. Running it in a tight "
                 "loop locally.",
                 hrs_ago(5), ""},
                {"m3", Role::Tool,
                 "for i in $(seq 1 50); do run checkout_flow_test; done \xe2\x86\x92 "
                 "3/50 failed",
                 hrs_ago(4), "shell"},
                {"m4", Role::Assistant,
                 "Reproduced. The seed fixture and the test both write to the "
                 "cart before a barrier; the test occasionally reads mid-write. "
                 "The import failed twice on the way through. I'll add an "
                 "explicit await on fixture-ready \xe2\x80\x94 say go and I'll write it.",
                 mins_ago(200), ""},
            };
            for (auto& mm : s.messages)
                if (mm.role == Role::User) mm.sync = api::SyncState::Synced;
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = pf("r10", "Navi PRs: oak + juno", 260, "idle",
                           ThreadState::Unknown, ThreadTag::None,
                           "read the diff, left 3 comments");
            s.messages = {
                {"m1", Role::User, "can you review the oak and juno PRs before I stamp them",
                 hrs_ago(6), ""},
                {"m2", Role::Assistant,
                 "Read all 240 lines. Logic is sound. Three things worth a "
                 "comment: the new retry has no jitter (thundering herd), one "
                 "error path swallows the cause, and the test only covers the "
                 "happy path. None are blockers \xe2\x80\x94 want me to draft the "
                 "comments?",
                 mins_ago(260), ""},
            };
            s.sub_agents = {
                {"r10s1", "verify oak", SubAgentState::Done, "clean"},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            // Failure with NOTHING testified: the reference's `mock-outcome-2`
            // carries a failed outcome and no status bag at all, and Puffin
            // draws it the red DOT rather than the red cross — the weaker
            // shape for the weaker fact. `Unknown` is exactly that here: the
            // tag is all that is known about this thread.
            s.summary = pf("r9", "row 133 banyan diff gate", 300, "active",
                           ThreadState::Unknown, ThreadTag::Failed,
                           "the gate failed, nothing landed");
            // The transcript is PORTED, not invented: it is Puffin's
            // `mock-outcome-2` (`Mock/MockBackend.swift:752`) turn for turn —
            // the same question, the same reply, the same fenced two lines,
            // the same relative stamps, and the same `failed` outcome closing
            // the run. The sidebar half of this fixture has mirrored Puffin's
            // catalog row for row for some time, and the transcript being
            // hanabi's own words is what made the main pane unmeasurable: two
            // panes holding different sentences of different lengths cannot be
            // compared, so every pixel of difference read as content and none
            // of it as design. With the words shared, what is left is design.
            s.messages = {
                {"m1", Role::User,
                 "why is the banyan diff gate red on row 133",
                 mins_ago(320), ""},
                {"m2", Role::Assistant,
                 "The gate is not red for a code reason:\n"
                 "\n"
                 "```\n"
                 "error: no signing identity matched 'fbmacos-apps-inhouse'\n"
                 "exit 65\n"
                 "```\n"
                 "\n"
                 "That is the Autograph key, which row 133 does not have. "
                 "Nothing in the diff can fix it.",
                 mins_ago(310), ""},
            };
            // Puffin's third row is a `runFinished(outcome: "failed")`, which
            // it draws as a rule with the word centred in it. hanabi has no
            // event row, so the outcome rides on the message the run ended on
            // (api::Message::run_outcome).
            s.messages[1].run_outcome = "failed";
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = pf("t10", "parent \xe2\x80\x94 nothing to report", 300.5,
                           "idle", ThreadState::Unknown, ThreadTag::None,
                           "concluded, kept for reference");
            s.messages = {
                {"m1", Role::System,
                 "Experiment concluded - kept for reference.", hrs_ago(9), ""},
                {"m2", Role::Assistant,
                 "Result was flat. The helper I spawned finished long ago and "
                 "agreed. Nothing to report.",
                 mins_ago(300), ""},
            };
            s.sub_agents = {
                {"t10s1", "done long ago", SubAgentState::Done,
                 "agreed with the read"},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = pf("r11", "PSC daily post generator", 420, "idle",
                           ThreadState::Unknown, ThreadTag::None,
                           "posted this morning, no deltas");
            s.messages = {
                {"m1", Role::System,
                 "Recurring: assemble the daily post and put it up.",
                 hrs_ago(9), ""},
                {"m2", Role::Assistant,
                 "Assembled and posted. Nothing moved beyond threshold, so the "
                 "post is the short form today.",
                 mins_ago(420), ""},
            };
            v.push_back(std::move(s));
        }
        // MARKDOWN FIXTURE: one reply whose structure is carried by headings,
        // seeded only under HANABI_MD_DEMO so the ordinary mock list is
        // unchanged. Every level 1-4 appears once, plus a "#42" line that must
        // stay body text.
        if (const char* md = std::getenv("HANABI_MD_DEMO");
            md && *md && std::string(md) != "0") {
            Session s;
            s.summary = calm("rmd", "structured findings with headings",
                             hrs_ago(2), "active", ThreadState::Unknown,
                             "headings fixture");
            s.messages = {
                {"md1", Role::User, "write up what you found, with sections",
                 hrs_ago(3), ""},
                {"md2", Role::Assistant,
                 "# Release readiness\n"
                 "The rollout is ready behind the flag.\n"
                 "\n"
                 "## Findings\n"
                 "Three call sites re-parse the same payload per event.\n"
                 "\n"
                 "### Rollback plan\n"
                 "Flip the flag; no migration to undo.\n"
                 "\n"
                 "#### Owner\n"
                 "Checkout team holds the pager for this one.\n"
                 "\n"
                 "#42 is the follow-up ticket.",
                 hrs_ago(2), ""},
            };
            v.push_back(std::move(s));
        }

        // THINKING FIXTURE: a reply whose reasoning arrives as its own block,
        // marked the way the agentcloud adapter marks it (subtitle
        // "thinking"). Seeded only under HANABI_THINKING_DEMO so the ordinary
        // mock list is unchanged.
        if (const char* tk = std::getenv("HANABI_THINKING_DEMO");
            tk && *tk && std::string(tk) != "0") {
            Session s;
            s.summary = calm("rthink", "why the retry budget ran out",
                             hrs_ago(1), "active", ThreadState::Unknown,
                             "thinking fixture");
            Message reasoning{"tk2", Role::Assistant,
                              "The backoff doubles from 200ms and the budget "
                              "is 5s, so the fifth attempt is already past it. "
                              "Worth checking whether the ceiling is per-call "
                              "or per-request before saying which.",
                              hrs_ago(1), ""};
            reasoning.subtitle = "thinking";
            s.messages = {
                {"tk1", Role::User, "why did the retry budget run out?",
                 hrs_ago(2), ""},
                reasoning,
                {"tk3", Role::Assistant,
                 "The budget is per-request, and exponential backoff spends it "
                 "before the fifth attempt.",
                 hrs_ago(1), ""},
            };
            v.push_back(std::move(s));
        }

        // TOOL-FOLD FIXTURE: two piles whose captured output differs in size,
        // so Auto has something to decide. Seeded only under HANABI_FOLD_DEMO
        // so the ordinary mock list is unchanged.
        if (const char* fd = std::getenv("HANABI_FOLD_DEMO");
            fd && *fd && std::string(fd) != "0") {
            Session s;
            s.summary = calm("rfold", "audit the cache eviction path",
                             hrs_ago(1), "active", ThreadState::Unknown,
                             "tool fold fixture");
            Message shortA{"fd2", Role::Tool,
                           "wc -l cache/evict.rs", hrs_ago(2), "bash"};
            shortA.tool_result = "182 cache/evict.rs";
            shortA.tool_status = "completed";
            shortA.tool_duration_ms = 300;
            Message shortB{
                "fd3", Role::Tool,
                "sed -n '1,240p' cache/evict.rs && printf '%s\\n' cache-audit-complete",
                hrs_ago(2), "read"};
            shortB.tool_status = "completed";
            shortB.tool_duration_ms = 200;
            // Over kAutoResultChars, so Auto keeps this pile shut.
            Message longA{"fd5", Role::Tool, "cargo test -p cache",
                          hrs_ago(1), "test"};
            longA.tool_result =
                "running 6 tests\n"
                "test evict::lru_drops_the_oldest_entry_first ... ok\n"
                "test evict::lru_keeps_an_entry_touched_this_tick ... ok\n"
                "test evict::cap_of_zero_evicts_everything_at_once ... ok\n"
                "test evict::a_pinned_entry_survives_a_full_sweep ... ok\n"
                "test evict::eviction_is_stable_across_equal_stamps ... ok\n"
                "SWEEPBUDGET exhausted after 4096 entries\n";
            longA.tool_status = "completed";
            longA.tool_duration_ms = 9100;
            Message longB{
                "fd6", Role::Tool,
                "cargo bench -p cache --features stress-testing -- --sample-size 1000 --measurement-time 30",
                hrs_ago(1), "bench"};
            longB.tool_status = "failed";
            longB.tool_duration_ms = 40000;
            s.messages = {
                {"fd1", Role::User, "how big is the eviction path?", hrs_ago(2),
                 ""},
                shortA,
                shortB,
                {"fd4", Role::Assistant,
                 "Small — one file, and the tests cover it.", hrs_ago(2), ""},
                longA,
                longB,
                {"fd7", Role::Assistant, "Green, with one budget warning.",
                 hrs_ago(1), ""},
            };
            for (auto& mm : s.messages)
                if (mm.role == Role::Tool) mm.tool_node = "cli:aspen";
            v.push_back(std::move(s));
        }

        // CODE FIXTURE: one reply carrying fenced blocks in several
        // languages, seeded only under HANABI_CODE_DEMO so the ordinary mock
        // list is unchanged. Each block is chosen to exercise a different part
        // of the scanner: a Python docstring and a # comment, a C++ block
        // comment that spans two lines, a shell pipeline, and JSON literals.
        if (const char* cd = std::getenv("HANABI_CODE_DEMO");
            cd && *cd && std::string(cd) != "0") {
            Session s;
            s.summary = calm("rcode", "show me the retry helper",
                             hrs_ago(1), "active", ThreadState::Unknown,
                             "code highlighting fixture");
            s.messages = {
                {"cd1", Role::User, "show me the retry helper, and how to run it",
                 hrs_ago(2), ""},
                {"cd2", Role::Assistant,
                 "Here it is.\n"
                 "\n"
                 "```python\n"
                 "def retry(attempt, base=200):\n"
                 "    \"\"\"Full jitter, capped at 30 seconds.\n"
                 "    Deliberately multi-line, to hold the docstring open.\"\"\"\n"
                 "    # the ceiling is per request, not per call\n"
                 "    delay = min(base * 2 ** attempt, 30000)\n"
                 "    return random.uniform(0, delay)\n"
                 "```\n"
                 "\n"
                 "The C++ side is the same shape:\n"
                 "\n"
                 "```cpp\n"
                 "/* the cap is a constant here,\n"
                 "   because the config lands next week */\n"
                 "int backoff(int attempt) {\n"
                 "  const int cap = 30000;\n"
                 "  return std::min(200 << attempt, cap);\n"
                 "}\n"
                 "```\n"
                 "\n"
                 "Run it with:\n"
                 "\n"
                 "```bash\n"
                 "# one shard at a time\n"
                 "for shard in 1 2 3; do\n"
                 "  cargo test -p retry -- --shard \"$shard\"\n"
                 "done\n"
                 "```\n"
                 "\n"
                 "and the knobs are:\n"
                 "\n"
                 "```json\n"
                 "{ \"base_ms\": 200, \"cap_ms\": 30000, \"jitter\": true }\n"
                 "```\n",
                 hrs_ago(1), ""},
                // Its own message, deliberately: a body over 40 lines folds,
                // and a block whose lines are BEHIND the fold renders empty —
                // which would make "an unknown language colours nothing" pass
                // for the wrong reason.
                {"cd3", Role::Assistant,
                 "The generated stub is in a language I do not know:\n"
                 "\n"
                 "```wat\n"
                 "(func $retry (param i32) (result i32))\n"
                 "```\n",
                 hrs_ago(1), ""},
            };
            v.push_back(std::move(s));
        }

        // DATE FIXTURE: a thread worked across three calendar days, seeded
        // only under HANABI_DATES_DEMO so the ordinary mock list is unchanged.
        // Stamps are anchored to local NOON so a run just after midnight (or
        // in any zone) still puts these on three distinct days.
        if (const char* dd = std::getenv("HANABI_DATES_DEMO");
            dd && *dd && std::string(dd) != "0") {
            const int64_t noonToday = local_noon_today();
            Session s;
            s.summary = calm("rdates", "rolling migration across three days",
                             noonToday, "active", ThreadState::Unknown,
                             "date dividers fixture");
            s.messages = {
                {"d1", Role::User, "kick off the migration when you can",
                 noonToday - 2 * 86400, ""},
                {"d2", Role::Assistant,
                 "Started. First shard is copying; I will report as each one "
                 "lands.",
                 noonToday - 2 * 86400 + 600, ""},
                {"d3", Role::Assistant,
                 "Shards two and three are done. Verification is running "
                 "overnight.",
                 noonToday - 86400, ""},
                {"d4", Role::User, "how did the verification go?",
                 noonToday - 1800, ""},
                {"d5", Role::Assistant,
                 "Clean — every row count matches and the old tables are ready "
                 "to drop.",
                 noonToday, ""},
            };
            v.push_back(std::move(s));
        }

        // LONG-MESSAGE FIXTURE: one reply longer than the fold threshold
        // (kFoldLines = 40 wrapped lines), so the fold affordance and the
        // preference that governs it can be driven by a test. Seeded only
        // under HANABI_LONGMSG_DEMO so the ordinary mock list is unchanged.
        if (const char* lm = std::getenv("HANABI_LONGMSG_DEMO");
            lm && *lm && std::string(lm) != "0") {
            Session s;
            s.summary = calm("rlong", "the migration checklist, in full",
                             hrs_ago(1), "active", ThreadState::Unknown,
                             "long-message fixture");
            std::string body = "Every step, in order:\n";
            for (int k = 1; k <= 55; ++k)
                body += "Step " + std::to_string(k) +
                        " of the migration checklist is done.\n";
            body += "Signed off by the migration owner.";
            s.messages = {
                {"lm1", Role::User, "give me the whole checklist",
                 hrs_ago(2), ""},
                {"lm2", Role::Assistant, body, hrs_ago(1), ""},
            };
            v.push_back(std::move(s));
        }

        // PERF FIXTURE: a deliberately LONG transcript for frame-time
        // measurement + virtualization testing. Only seeded when
        // HANABI_BIG_TRANSCRIPT is set (a headless perf run), so it never
        // pollutes the normal mock list. ~120 messages: alternating user /
        // long-assistant prose, interleaved tool runs (which pile), plus
        // sub-agents.
        //
        // This comment used to end "— the exact shape that made the per-frame
        // rebuild ~15-20ms". That number is UNVERIFIED and stale: it has no
        // method recorded next to it and nothing in the tree still reproduces
        // it. It is removed rather than replaced, because swapping one
        // unsourced number for another — measured on a box at load average 28,
        // while the transcript render path is actively being changed by
        // somebody else — would only re-arm the same trap. What the per-frame
        // transcript rebuild costs today belongs in docs/perf/TRANSCRIPT.md
        // with its method beside it, measured by whoever owns that path.
        if (const char* big = std::getenv("HANABI_BIG_TRANSCRIPT");
            big && *big && std::string(big) != "0") {
            Session s;
            s.summary = calm("rbig", "PERF: long transcript for frame timing",
                             days_ago(1), "active", ThreadState::Unknown,
                             "120-message perf fixture");
            const int64_t base = days_ago(1) - 200000;
            // Turn count is a knob so frame time can be plotted AGAINST
            // transcript length. One number is not a finding: cost that is
            // flat in the message count and cost that is linear in it are
            // different bugs with different fixes, and only a curve tells
            // them apart. Default 40 (=160 messages), the shape the fixture
            // was written for, so every existing caller is unchanged.
            int turns = 40;
            if (const char* t = std::getenv("HANABI_BIG_TURNS"); t && *t) {
                const int parsed = std::atoi(t);
                if (parsed > 0) turns = parsed;
            }
            // HANABI_BIG_EVENTS=1 gives every turn the EVENT rows a real
            // thread carries. Off by default, and the default is the whole
            // reason it is a knob: this fixture is what
            // scripts/perf_transcript_slope.sh and the alloc gate's
            // thread480 arm measure, and their ceilings were set against the
            // four-row turn below. Flipping the shape under them would move
            // every one of those numbers in the same commit that claims to
            // measure a regression, and nobody could tell the two apart.
            //
            // WHY IT HAD TO EXIST. feat/event-model added six row kinds --
            // thinking, tool call, sub-agent, delivery, node, skill -- and
            // NOTHING in this repo could render one of them at scale. The
            // synthetic catalog (stress_turn) leaves Message::kind at its
            // Text default; this fixture emits User / Assistant / Tool / Tool
            // and no thinking row at all. So the per-message gate ran over a
            // transcript with zero of the new kinds in it, and read exactly
            // the same number before and after the merge. That is
            // docs/perf/STRESS.md's own finding -- "the synthetic stress
            // catalog rendered the cheapest path this app has" -- arriving a
            // second time, at a different fixture, for a different reason.
            //
            // THE MIX IS OBSERVED, NOT INVENTED. One real agentcloud session
            // read 32 Text, 13 Thinking, 68 ToolCall, 6 SubAgent, 2 Delivery
            // over 121 rows. Taking the two Text rows a turn as the unit,
            // that is 16 turns' worth: 0.8 thinking, 4.25 tool, 0.4
            // sub-agent and 0.125 delivery per turn. Below: a thinking row
            // and four tool rows every turn, a sub-agent every third and a
            // delivery every eighth, which lands within a few percent of
            // each of those and stays a whole number per turn.
            //
            // NODE / SKILL / STATUS are the exception and are deliberately
            // rarer than the rest -- one of each per sixteen turns. The
            // observed session had none, so no ratio argues for them; they
            // are here because all three go through render_event_row, that
            // is the one-line-event path feat/event-model added, and a path
            // no fixture reaches is a path no gate can see. Which is the
            // sentence this whole block exists to stop being true again.
            const bool bigEvents = [] {
                const char* e = std::getenv("HANABI_BIG_EVENTS");
                return e != nullptr && *e != '\0' && std::string(e) != "0";
            }();
            for (int k = 0; k < turns; ++k) {
                Message u;
                u.id = "b_u" + std::to_string(k);
                u.role = Role::User;
                u.text = "Follow-up question #" + std::to_string(k) +
                         ": can you dig into the " +
                         (k % 2 ? "latency" : "memory") +
                         " regression and report what you find?";
                u.created_at = base + k * 4000;
                s.messages.push_back(std::move(u));

                // The model's reasoning, folded. Its own Item kind and its
                // own renderer, and until this line no long-thread fixture
                // produced one.
                if (bigEvents) {
                    Message th;
                    th.id = "b_k" + std::to_string(k);
                    th.role = Role::Assistant;
                    th.kind = EventKind::Thinking;
                    th.subtitle = "thinking";
                    th.text =
                        "Two readings of #" + std::to_string(k) +
                        ". The cheap one is that the cache is stale. The "
                        "honest one is that the derivation runs per frame and "
                        "the same walk happens twice, which is the shape "
                        "every finding on this thread has taken.";
                    th.created_at = base + k * 4000 + 500;
                    s.messages.push_back(std::move(th));
                }

                Message a;
                a.id = "b_a" + std::to_string(k);
                a.role = Role::Assistant;
                a.text =
                    "Here's the breakdown for step " + std::to_string(k) +
                    ":\n\n"
                    "1. Pulled the trace and diffed it against the baseline.\n"
                    "2. The hot path is `handle_request` calling into "
                    "`parser_cache.entries` on every event.\n"
                    "3. Under load that's ~40k calls/sec, each allocating.\n"
                    "4. The fix caps the cache and hashes the key.\n\n"
                    "Ruled out:\n"
                    "- connection pool (steady)\n"
                    "- metrics buffer (flat)\n\n"
                    "Applying the LRU cap now and adding a regression test so "
                    "this stays bounded going forward. Expected steady-state "
                    "drop is significant.";
                a.created_at = base + k * 4000 + 1000;
                s.messages.push_back(std::move(a));

                Message t1;
                t1.id = "b_t" + std::to_string(k) + "a";
                t1.role = Role::Tool;
                t1.subtitle = "shell";
                t1.text = "grep -rn \"parser_cache\" src/ \xe2\x86\x92 " +
                          std::to_string(3 + k % 9) + " hits";
                t1.created_at = base + k * 4000 + 1500;
                s.messages.push_back(std::move(t1));

                Message t2;
                t2.id = "b_t" + std::to_string(k) + "b";
                t2.role = Role::Tool;
                t2.subtitle = "sql";
                t2.text = "SELECT count(*) FROM cache_entries \xe2\x86\x92 " +
                          std::to_string(40000 + k * 137);
                t2.created_at = base + k * 4000 + 2000;
                s.messages.push_back(std::move(t2));

                if (!bigEvents) continue;

                // Two more tool rows. The observed session ran 4.25 tool
                // calls to every two lines of prose and this fixture ran two;
                // a tool pile's cost is per row inside it, so the count is
                // the thing rather than the pile.
                for (int j = 0; j < 2; ++j) {
                    Message tx;
                    tx.id = "b_t" + std::to_string(k) +
                            std::string(1, static_cast<char>('c' + j));
                    tx.role = Role::Tool;
                    tx.kind = EventKind::ToolCall;
                    tx.subtitle = j ? "python" : "read_file";
                    tx.text = j ? "python -c 'print(sum(rows))'"
                                : "worker/queue.rs:" + std::to_string(40 + k);
                    tx.tool_status = (k % 7 == 6 && j == 1) ? "failed"
                                                            : "completed";
                    tx.tool_duration_ms = 40 + (k * 17 + j * 53) % 900;
                    tx.created_at = base + k * 4000 + 2500 + j * 100;
                    s.messages.push_back(std::move(tx));
                }

                // A sub-agent every third turn: its own inline card, and the
                // kind the reader asked for by name ("you are missing
                // subagents").
                if (k % 3 == 0) {
                    Message sa = event_row(
                        EventKind::SubAgent, "shadow #" + std::to_string(k),
                        "Shadow the new path for an hour and report the "
                        "divergences",
                        base + k * 4000 + 2700);
                    sa.id = "b_s" + std::to_string(k);
                    sa.tool_status = (k % 6 == 0) ? "completed" : "running";
                    s.messages.push_back(std::move(sa));
                }

                // A delivery every eighth. COLLAPSED, which is the state a
                // reader scrolling past sees and therefore the one worth
                // measuring: an expanded delivery costs a rich body and is
                // the same cost as the assistant bubble above it.
                if (k % 8 == 7) {
                    Message d = event_row(
                        EventKind::Delivery, "child",
                        "child session settled: completed\n\nNo divergences "
                        "over 61 minutes and 1.2M requests.",
                        base + k * 4000 + 2800);
                    d.id = "b_d" + std::to_string(k);
                    d.tool_status = "completed";
                    s.messages.push_back(std::move(d));
                }

                // The one-line events, one of each per sixteen turns. See the
                // block comment above for why these are here at a rate no
                // observation set.
                if (k % 16 == 4) {
                    Message nd = event_row(EventKind::Node,
                                           "od-" + std::to_string(4400 + k),
                                           "attached",
                                           base + k * 4000 + 2900);
                    nd.id = "b_n" + std::to_string(k);
                    s.messages.push_back(std::move(nd));
                }
                if (k % 16 == 9) {
                    Message sk = event_row(EventKind::Skill, "presto-query",
                                           "platform",
                                           base + k * 4000 + 2950);
                    sk.id = "b_l" + std::to_string(k);
                    s.messages.push_back(std::move(sk));
                }
                if (k % 16 == 13) {
                    Message st = event_row(EventKind::Status, "working",
                                           "shadowing class 3",
                                           base + k * 4000 + 3000);
                    st.id = "b_w" + std::to_string(k);
                    s.messages.push_back(std::move(st));
                }
            }
            s.sub_agents = {
                {"bs1", "Heap analysis", SubAgentState::Done, "leak found"},
                {"bs2", "Regression test", SubAgentState::Running, "writing"},
                {"bs3", "Shadow rollout", SubAgentState::Blocked, "needs flag"},
            };
            v.push_back(std::move(s));
        }

        // STRESS FIXTURE: a catalog the size of a real one.
        //
        // hanabi's mock has twenty rows, and twenty rows is why nothing in
        // this project has ever run under load: every list is short enough
        // that an O(n^2) walk looks instant and every cache is small enough
        // that never evicting looks like a design. Puffin's own stress
        // fixtures generate hundreds of sessions with 120 turns each
        // (Tests/StressFixtures.swift) for exactly this reason.
        //
        // HANABI_STRESS_SESSIONS=<n> appends n synthetic rows. Off by default,
        // so the parity captures and the scripted suite still see the
        // hand-written twenty in their known order -- a fixture that changed
        // size would move every coordinate-pinned test and every reference
        // score, which is a much worse trade than typing an env var.
        //
        // The rows are DETERMINISTIC and varied: state, tag, sub-agent counts
        // and title length all cycle on the index, because a thousand
        // identical rows measure one row a thousand times. Titles are long
        // enough to exercise the ellipsizer (gap #79) on roughly a third.
        // A 0..100 percentage from the environment.
        const auto stressPct = [](const char* name) {
            const char* v = std::getenv(name);
            int pct = (v != nullptr && *v != '\0') ? std::atoi(v) : 0;
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            return pct;
        };
        const int pinnedPct = stressPct("HANABI_STRESS_PINNED");
        const int archivedPct = stressPct("HANABI_STRESS_ARCHIVED");
        if (const char* n = std::getenv("HANABI_STRESS_SESSIONS");
            n != nullptr && *n != '\0') {
            const int count = std::atoi(n);
            static const char* kVerbs[] = {
                "profiling", "migrating", "sweeping", "reindexing", "draining",
                "backfilling", "reconciling", "compacting"};
            static const char* kNouns[] = {
                "the quota shard", "row 212's ledger", "the retry queue",
                "oncall handoff", "the ranking config",
                "a cohort that will not converge", "the nightly export"};
            static const ThreadState kStates[] = {
                ThreadState::Running, ThreadState::Ready,
                ThreadState::Attention, ThreadState::Parked,
                ThreadState::Working, ThreadState::Unknown};
            static const ThreadTag kTags[] = {
                ThreadTag::None, ThreadTag::Blocked, ThreadTag::Review,
                ThreadTag::None};
            for (int k = 0; k < count; ++k) {
                Session s;
                std::string title = std::string(kVerbs[k % 8]) + " " +
                                    kNouns[(k * 3) % 7];
                // Every third title is long enough to need ellipsizing.
                if (k % 3 == 0)
                    title += " \xe2\x80\x94 and reporting back on what moved";
                s.summary = pf("s" + std::to_string(k), title, k % 4,
                               (k % 5 == 0) ? "idle" : "active",
                               kStates[k % 6], kTags[k % 4],
                               "synthetic stress row " + std::to_string(k));
                s.summary.updated_at = mins_ago(k % 720);
                // Starred and Archived are two of the five screens and the
                // synthetic catalog put NOTHING in either of them, at any
                // size -- so `HANABI_VIEW=starred HANABI_STRESS_SESSIONS=2000`
                // measured an empty state and read as a pass. Both digest
                // views that DO fill up (Blocked, Review) fill from the tag
                // cycle above; these two have no cycle to fill from because
                // starring and archiving are user acts, not backend states.
                //
                // Opt-in and off by default, for the reason the whole block
                // is: an existing script that sets HANABI_STRESS_SESSIONS is
                // pinned to the row counts it produces, and archiving a
                // fraction of them would move every one of those numbers.
                // A percentage rather than a count, so it tracks whatever
                // catalog size the caller asked for.
                if (k % 100 < pinnedPct) s.summary.starred = true;
                // ThreadState::Archived, not status="archived": is_archived
                // reads the state (under the user's local override), and the
                // status string is a free-form field nothing derives from.
                if (k % 100 < archivedPct) {
                    s.summary.status = "archived";
                    s.summary.state = ThreadState::Archived;
                }
                // A tenth carry sub-agents, the way a real catalog does.
                if (k % 10 == 0) {
                    s.sub_agents = {
                        {"ss1", "shard 1", SubAgentState::Done, "converted"},
                        {"ss2", "shard 2", SubAgentState::Running, "in flight"},
                    };
                }
                // Transcript length cycles: most threads are short, a few are
                // the long ones that actually cost something to lay out.
                const int turns = (k % 17 == 0) ? 60 : (k % 5 == 0 ? 12 : 3);
                for (int t = 0; t < turns; ++t) {
                    for (Message& m : stress_turn(k, t))
                        s.messages.push_back(std::move(m));
                }
                v.push_back(std::move(s));
            }
        }

        if (const char* folders = std::getenv("HANABI_FOLDER_DEMO");
            folders != nullptr && *folders != '\0' && v.size() >= 3) {
            v[0].summary.folder = "/work/subscriptions";
            v[1].summary.folder = "/work/subscriptions";
            v[2].summary.folder = "/work/monetization";
        }

        // THE BRAKES AND THE TWO NEW MARKS, on demand.
        //
        // Env-gated rather than seeded, because these rows exist to be
        // PHOTOGRAPHED and asserted on: adding them to the default catalog
        // would move every baseline that shows the list, for states most
        // sessions never reach. The knob is in kFixtureEnv above, which is what
        // keeps the catalog cache honest across a suite that runs many scripts
        // in one process.
        if (const char* brakes = std::getenv("HANABI_BRAKES_DEMO");
            brakes != nullptr && *brakes != '\0') {
            {
                Session s;
                s.summary = pf("bz1", "canary cohort rollout", 4, "active",
                               ThreadState::Running, ThreadTag::None,
                               "held while the cohort is under review");
                s.summary.frozen = true;
                s.summary.frozen_by = "bz1";
                s.summary.frozen_reason = "under review by the canary owner";
                s.messages = {
                    {"m1", Role::User,
                     "roll the new pricing copy to the canary cohort.",
                     mins_ago(9), ""},
                    {"m2", Role::Assistant,
                     "Staged and ready. Waiting on the cohort owner before "
                     "anything touches production.",
                     mins_ago(4), ""},
                };
                v.push_back(std::move(s));
            }
            {
                Session s;
                s.summary = pf("bz2", "nightly digest", 5, "idle",
                               ThreadState::Parked, ThreadTag::None,
                               "replies are off until morning");
                // Paused arrives on the ATTACH, never on a catalog row (see
                // types.h). The mock's get_session hands the whole Session, so
                // setting it here is the same route the real client takes:
                // attach -> apply_attach_brakes -> the list overlay.
                s.summary.replies_paused = true;
                s.messages = {
                    {"m1", Role::System,
                     "Recurring: assemble the nightly digest.", mins_ago(40),
                     ""},
                };
                v.push_back(std::move(s));
            }
            {
                Session s;
                s.summary = pf("bz3", "which region first?", 7, "active",
                               ThreadState::Attention, ThreadTag::Waiting,
                               "waiting for you to pick a region");
                s.messages = {
                    {"m1", Role::User, "start the regional backfill.",
                     mins_ago(12), ""},
                    {"m2", Role::Assistant,
                     "Ready to start. Which region should go first - eu-west "
                     "or us-east? Nothing runs until you say.",
                     mins_ago(7), ""},
                };
                v.push_back(std::move(s));
            }
            {
                Session s;
                s.summary = pf("bz4", "filed from the web", 6, "idle",
                               ThreadState::Ready, ThreadTag::Done,
                               "archived somewhere that is not this Mac");
                s.summary.server_archived_at_ms = 1781520000000;
                s.messages = {
                    {"m1", Role::Assistant, "Filed. Nothing left to do here.",
                     mins_ago(55), ""},
                };
                v.push_back(std::move(s));
            }
            {
                // A freeze the CATALOG has not caught up with: the row is
                // stale (the poll that fetched it predates the freeze) and the
                // attach carries the truth. `catalog_row` does not strip
                // `frozen` -- it is a real summary-row key -- so this fixture
                // makes the row honestly unfrozen and lets the attach be the
                // only source, which is the race a live client hits whenever a
                // freeze lands between two polls.
                Session s;
                s.summary = pf("bz6", "frozen since the last poll", 3,
                               "active", ThreadState::Running, ThreadTag::None,
                               "the row still thinks this is running");
                s.summary.frozen = true;
                s.summary.frozen_by = "bz6";
                s.summary.frozen_reason = "frozen after the catalog was read";
                s.messages = {
                    {"m1", Role::User, "keep the shard rebalance going.",
                     mins_ago(8), ""},
                    {"m2", Role::Assistant,
                     "Rebalancing. Two shards moved, four to go.", mins_ago(3),
                     ""},
                };
                v.push_back(std::move(s));
            }
            {
                // Halt is attach-only, so it lives on the DETAIL rather than
                // on the row: this thread's list mark is ordinary and the
                // composer is the only place that says it.
                Session s;
                s.summary = pf("bz5", "queue drain, paused by an ancestor", 11,
                               "active", ThreadState::Working, ThreadTag::None,
                               "no run will start until it is resumed");
                // Containment, not an own halt: `halted` stays false and the
                // brake comes from the ancestor chain, which is the exact
                // combination the wire sends to a descendant.
                s.halted = false;
                s.halt_contained = true;
                s.halted_by = "bz0";
                s.halted_reason = "the parent halted the whole subtree";
                s.messages = {
                    {"m1", Role::User, "drain the retry queue.", mins_ago(18),
                     ""},
                    {"m2", Role::Assistant,
                     "Drained 212 of 940 before the halt landed.", mins_ago(11),
                     ""},
                };
                v.push_back(std::move(s));
            }
        }

        return v;
    }
};

}  // namespace api
