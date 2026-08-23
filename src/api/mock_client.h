#pragma once

// Deterministic, offline sample data source. This is the default backend so
// the app is fully functional with no configuration and no network.
//
// The seed has two intentional cohorts:
//
//   1. A small set of RICH demo threads (t1..t14) with folders, blocked /
//      review / done states, and sub-agents. These tell the ideal-UX story:
//      what the app looks like when the backend reports the full high-signal
//      model. Smart views, folders, and the digest all have real states to
//      render from these.
//
//   2. A larger REALISTIC cohort (r1..r20) shaped like what a live backend
//      actually returns: NO folder, NO explicit state (state=Unknown), a
//      plain active/archived status, mostly-calm rows, freeform titles (some
//      plain, some with "[P]" prefixes or "on you"/"DONE"/D-number markers),
//      and updatedAt values spread across Today / This Week / weeks-and-
//      months-ago. This is the cohort that proves the UI degrades gracefully
//      on real-shaped data instead of only looking good on a tidy mock.
//
// Timestamps are now-based (time(nullptr) - N) so the sidebar time buckets
// (Today / This Week / Earlier) always populate whenever the app is run.
// The screenshot baseline suite (docs/screenshots/baselines/) depends on this:
// datum and display are measured from the same moving now, so every rendered
// age ("3h") is constant — reseeding with absolute epochs rots every
// time-showing baseline within a day.
// Nothing here names or encodes any real service, product, or company.

#include <algorithm>
#include <ctime>

#include "client.h"

namespace api {

class MockClient : public Client {
  public:
    std::string backend_label() const override { return "mock"; }

    Result<std::vector<SessionSummary>> list_sessions() override {
        auto sessions = seed();
        std::vector<SessionSummary> out;
        out.reserve(sessions.size() + created_.size());
        // created_ holds both composer-created sessions AND live overrides of
        // seed rows that have been replied to this run (see find_mutable). A
        // seed row that has an override is skipped here so it isn't listed
        // twice — the override (with the fresher updated_at/preview) wins.
        for (auto& s : sessions) {
            if (is_overridden(s.summary.id)) continue;
            out.push_back(s.summary);
        }
        for (auto& s : created_) out.push_back(s.summary);
        // Newest first, but pinned (starred) rise to the top within order.
        std::sort(out.begin(), out.end(),
                  [](const SessionSummary& a, const SessionSummary& b) {
                      return a.updated_at > b.updated_at;
                  });
        return Result<std::vector<SessionSummary>>::success(std::move(out));
    }

    Result<Session> get_session(const std::string& id) override {
        for (auto& s : created_) {
            if (s.summary.id == id) return Result<Session>::success(s);
        }
        for (auto& s : seed()) {
            if (s.summary.id == id)
                return Result<Session>::success(s);
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
        s.session_count = static_cast<int64_t>(seed().size());
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

    // A streamed reply, split into deterministic word/token chunks so the UI
    // fills in incrementally. Two consumers:
    //
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
        const int64_t now = static_cast<int64_t>(std::time(nullptr));
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
        Session* target = find_mutable(session_id);
        if (!target)
            return Result<Message>::failure("no such session: " + session_id);

        const int64_t now = static_cast<int64_t>(std::time(nullptr));
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

        const int64_t now = static_cast<int64_t>(std::time(nullptr));
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
    // itself is rebuilt fresh on every seed() call and can't hold state).
    Session* find_mutable(const std::string& id) {
        for (auto& s : created_)
            if (s.summary.id == id) return &s;
        for (auto& s : seed()) {
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

    // In-memory sink for the settings-write path (see update_settings). Lets
    // the periodic-sync story run + be asserted offline with zero config.
    UserSettings last_written_;
    int write_count_ = 0;

    static int64_t hrs_ago(int64_t h) {
        // Now-based reference so the sidebar time buckets (Today / This Week /
        // Earlier) always populate relative to when the app is actually run.
        // Deterministic within a run; the RELATIVE ordering of the seed is
        // fixed, which is all the sort/bucket logic (and the tests) rely on.
        const int64_t now = static_cast<int64_t>(std::time(nullptr));
        return now - h * 3600;
    }

    // Convenience: days-ago in the same now-based frame.
    static int64_t days_ago(int64_t d) { return hrs_ago(d * 24); }

    // Local noon today. A fixture that wants messages on distinct CALENDAR
    // days cannot subtract 86400 from "now": run at 00:30 and "a day ago" is
    // still yesterday evening, but run at 23:30 and two stamps 25 hours apart
    // can land on the same day either side of a DST shift. Anchoring at noon
    // leaves twelve hours of slack in both directions.
    static int64_t local_noon_today() {
        const std::time_t now = std::time(nullptr);
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
        s.starred = false;           // see isPinned note in seed()
        s.preview = std::move(preview);
        return s;
    }

    static std::vector<Session> seed() {
        std::vector<Session> v;

        // --- Attention: waiting on you / done (dot + bold) ---
        {
            Session s;
            s.summary = sum("t1", "Multi-tier pricing rollout", 0, "active",
                            ThreadState::Attention, ThreadTag::Blocked,
                            "stars", true, "waiting on you \xc2\xb7 8m");
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
                 hrs_ago(1), "shell"},
                {"m4", Role::Assistant,
                 "All CI green, shadow matched. Nothing else is blocking. I "
                 "need your approval to land.",
                 hrs_ago(0) - 720, ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = sum("t2", "Stars payout reconciliation", 0, "active",
                            ThreadState::Attention, ThreadTag::Blocked,
                            "stars", false, "waiting on you \xc2\xb7 22m");
            s.messages = {
                {"m1", Role::System,
                 "Task: reconcile this cycle's Stars payouts against the "
                 "ledger before the batch runs.",
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
            s.summary = sum("t3", "Creator welcome QP copy", 1, "active",
                            ThreadState::Attention, ThreadTag::Done, "stars",
                            false, "done \xc2\xb7 1h");
            s.messages = {
                {"m1", Role::System,
                 "Task: draft welcome QP copy for new Stars creators. 3 "
                 "variants, brand-voice compliant.",
                 hrs_ago(2), ""},
                {"m2", Role::Assistant,
                 "Three variants, all passing the brand-voice check:\n"
                 "  A (warmest): Welcome - your fans can now support you "
                 "directly. Let's set up Stars.\n"
                 "  B (clearest): Stars are on. Turn on receiving to start "
                 "earning from your fans.\n"
                 "  C (shortest): You're in. Set up Stars.",
                 hrs_ago(1), ""},
                {"m3", Role::Assistant,
                 "B scored highest on clarity; A is warmest. Which ships "
                 "Monday?",
                 hrs_ago(1), ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = sum("t11", "Churn query (Q3 cohort)", 0, "active",
                            ThreadState::Attention, ThreadTag::Done, "stars",
                            true, "done \xc2\xb7 12m");
            s.messages = {
                {"m1", Role::System,
                 "Task: pull 90-day churn for the Q3 subscriber cohort.",
                 hrs_ago(1), ""},
                {"m2", Role::Tool,
                 "query returned 41,208 rows \xc2\xb7 exported to results.csv",
                 hrs_ago(0) - 840, "sql"},
                {"m3", Role::Assistant,
                 "Done - 90-day churn came in at 6.2%, down 0.8pt from Q2. "
                 "Results attached; want a breakdown by tier?",
                 hrs_ago(0) - 720, ""},
            };
            v.push_back(std::move(s));
        }

        // --- Ready for review (agent-verified) ---
        {
            Session s;
            s.summary = sum("t4", "Tier upgrade flow", 0, "active",
                            ThreadState::Ready, ThreadTag::Review,
                            "experiments", false, "ready for review \xc2\xb7 30m");
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
                 hrs_ago(0) - 1800, ""},
            };
            // Give the single tool call captured output so it's expandable
            // (click to reveal) in the transcript — mirrors a real tool_result.
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
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = sum("t5", "Payout worker race fix", 2, "active",
                            ThreadState::Ready, ThreadTag::Review, "oncall",
                            true, "ready for review \xc2\xb7 2h");
            s.messages = {
                {"m1", Role::System,
                 "Task: root-cause and fix intermittent double-writes in the "
                 "payout worker.",
                 hrs_ago(6), ""},
                {"m2", Role::Assistant,
                 "Root cause: two workers could claim the same payout row "
                 "between the read and the lock. Fixed with a conditional "
                 "update guard.",
                 hrs_ago(3), ""},
                {"m3", Role::Tool,
                 "stress_run(500) \xe2\x86\x92 0 double writes \xc2\xb7 CI "
                 "green",
                 hrs_ago(2), "shell"},
                {"m4", Role::Assistant,
                 "Confident it's fixed. Ready for you to verify on the test "
                 "tenant.",
                 hrs_ago(2), ""},
            };
            v.push_back(std::move(s));
        }

        // --- Self-running (dimmed, calm — no dot, no bold) ---
        {
            Session s;
            s.summary = sum("t6", "Backfill entitlement table", 0, "active",
                            ThreadState::Running, ThreadTag::None,
                            "experiments", false, "self-running \xc2\xb7 61%");
            s.messages = {
                {"m1", Role::System,
                 "Task: backfill the entitlement table for legacy "
                 "subscribers.",
                 hrs_ago(2), ""},
                {"m2", Role::Assistant,
                 "Backfill in progress - 61% through 2.1M rows. No action "
                 "needed; I'll surface it when done or if I hit a snag.",
                 hrs_ago(0) - 300, ""},
            };
            s.sub_agents = {
                {"t6s1", "Chunk 1\xe2\x80\x93""500k", SubAgentState::Done,
                 "500k rows written"},
                {"t6s2", "Chunk 500k\xe2\x80\x93""1M", SubAgentState::Running,
                 "at row 812k"},
                {"t6s3", "Row validator", SubAgentState::Running,
                 "checksums matching so far"},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = sum("t7", "Tier schema migration", 0, "active",
                            ThreadState::Running, ThreadTag::None,
                            "experiments", false, "self-running \xc2\xb7 tests");
            s.messages = {
                {"m1", Role::Assistant,
                 "Running the migration test suite before applying. Quiet "
                 "until there's something to decide.",
                 hrs_ago(0) - 540, ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = sum("t8", "Nightwatch: D948213", 0, "active",
                            ThreadState::Running, ThreadTag::None, "oncall",
                            false, "self-running \xc2\xb7 landing");
            s.messages = {
                {"m1", Role::Assistant,
                 "CI green on all signals. Landing the diff now - will report "
                 "the SHA when it's in.",
                 hrs_ago(0) - 240, ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = sum("t9", "Weekly metrics digest", 0, "active",
                            ThreadState::Running, ThreadTag::None, "oncall",
                            false, "self-running");
            s.messages = {
                {"m1", Role::Assistant,
                 "Assembling the weekly subs metrics digest. Nothing for you "
                 "yet.",
                 hrs_ago(0) - 60, ""},
            };
            v.push_back(std::move(s));
        }

        // --- Parked / muted (greyed, never nudges) ---
        {
            Session s;
            s.summary = sum("t10", "Old A/B: paywall color", 500, "idle",
                            ThreadState::Parked, ThreadTag::None,
                            "experiments", false, "parked");
            s.messages = {
                {"m1", Role::System,
                 "Muted. Experiment concluded - kept for reference.",
                 hrs_ago(504), ""},
                {"m2", Role::Assistant,
                 "Result was flat. Parked this thread; it won't ask for "
                 "anything.",
                 hrs_ago(504), ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = sum("t12", "Docs: onboarding runbook", 168, "idle",
                            ThreadState::Parked, ThreadTag::None, "recent",
                            false, "parked");
            s.messages = {
                {"m1", Role::Assistant,
                 "Muted reference thread. No attention needed.", hrs_ago(168),
                 ""},
            };
            v.push_back(std::move(s));
        }

        // --- Archived (low-signal, greyed) ---
        {
            Session s;
            s.summary = sum("t13", "Legacy gifting migration", 1440,
                            "archived", ThreadState::Archived, ThreadTag::None,
                            "", false, "archived \xc2\xb7 2mo");
            s.messages = {
                {"m1", Role::System,
                 "Task: migrate legacy gifting rows to the new ledger.",
                 hrs_ago(1440), ""},
                {"m2", Role::Assistant,
                 "Migration completed and reconciled. Archiving - nothing "
                 "left to do here.",
                 hrs_ago(1440), ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = sum("t14", "2024 pricing experiment writeup", 3600,
                            "archived", ThreadState::Archived, ThreadTag::None,
                            "", false, "archived \xc2\xb7 5mo");
            s.messages = {
                {"m1", Role::Assistant,
                 "Final writeup shipped. Archived for reference.",
                 hrs_ago(3600), ""},
            };
            v.push_back(std::move(s));
        }

        // ------------------------------------------------------------------
        // REALISTIC COHORT (r1..r20)
        //
        // Shaped like a live backend dump: folder="" (no folder), no explicit
        // state (Unknown), no tag (None), not starred — just a plain
        // active/archived status and an updatedAt. A handful are "Running"
        // (the isProcessing-equivalent: dimmed, quiet, never nudges). Titles
        // are freeform the way real ones are — some plain, some with a "[P]"
        // prefix, some carrying "on you" / "DONE" / a D-number marker — but
        // the STRUCTURED fields stay neutral, exactly like real rows that
        // carry no high-signal model. Timestamps fan out across Today / This
        // Week / weeks-and-months-ago so every sidebar time bucket populates.
        //
        // NOTE on isPinned: real rows can be pinned, but the e2e suite asserts
        // an exact starred count over the whole mock (the 3 rich threads), so
        // this cohort intentionally leaves starred=false to avoid breaking it.
        // The Running state stands in for isProcessing. (See report / caveats.)
        // ------------------------------------------------------------------

        // -- Today (hours ago) --
        {
            Session s;
            s.summary = calm("r1", "[P] Tidy up the retry backoff in the sync worker",
                             hrs_ago(1), "active", ThreadState::Unknown,
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
                 hrs_ago(1), ""},
            };
            // Real node on the tool calls so the collapsed pile header reads
            // "[cli:aspen] cmd" (demonstrates tool_node prefix; renderer shows
            // nothing when this is empty, so no fabricated node elsewhere).
            for (auto& mm : s.messages)
                if (mm.role == Role::Tool) mm.tool_node = "cli:aspen";
            // Sample captured output + status + duration so expanding a tool
            // reveals real DETAILS (Gabe: "we are missing tool details").
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
            s.summary = calm("r2", "investigate flaky checkout integration test",
                             hrs_ago(3), "active", ThreadState::Unknown,
                             "reproduced locally, looks like a fixture race");
            s.messages = {
                {"m1", Role::User,
                 "checkout_flow_test fails ~1 in 20 on CI. can you figure out "
                 "why?",
                 hrs_ago(4), ""},
                {"m2", Role::Assistant,
                 "Pulled the last 40 CI runs \xe2\x80\x94 **6 failures**, all on the same "
                 "assertion (`cart.total` off by one line item). Smells like a "
                 "fixture setup race in `seed_cart()`. Running it in a tight "
                 "loop locally.",
                 hrs_ago(4), ""},
                {"m3", Role::Tool,
                 "for i in $(seq 1 50); do run checkout_flow_test; done \xe2\x86\x92 "
                 "3/50 failed",
                 hrs_ago(3), "shell"},
                {"m4", Role::Assistant,
                 "Reproduced. The seed fixture and the test both write to the "
                 "cart before a barrier; the test occasionally reads mid-write. "
                 "I'll add an explicit await on fixture-ready. Writing the fix.",
                 hrs_ago(3), ""},
            };
            // Seed a sync state on the user messages (gap #28 probe + demoes the
            // local-first WhatsApp-style sync indicator).
            for (auto& mm : s.messages)
                if (mm.role == Role::User) mm.sync = api::SyncState::Synced;
            v.push_back(std::move(s));
        }
        // Two AUTOMATED (scheduled/cron) rows: titles shaped like a live
        // backend's recurring jobs ("Schedule: ..." / "*-tick") so the sidebar
        // renders them muted with the "automated" (repeat) atlas glyph in the
        // status slot instead of a peer conversation dot.
        {
            Session s;
            s.summary = calm("r2b", "Schedule: nightly dependency audit",
                             hrs_ago(2), "active", ThreadState::Unknown,
                             "ran 02:00 \xc2\xb7 no new advisories");
            s.messages = {
                {"m1", Role::System,
                 "Scheduled job: audit dependencies for known advisories, "
                 "nightly at 02:00.",
                 hrs_ago(2), ""},
                {"m2", Role::Assistant,
                 "Scanned 214 dependencies against the advisory DB. No new "
                 "advisories since the last run. Nothing to do.",
                 hrs_ago(2), ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = calm("r2c", "metrics-digest-tick",
                             hrs_ago(5), "active", ThreadState::Unknown,
                             "posted the 9am digest \xc2\xb7 3 deltas");
            s.messages = {
                {"m1", Role::System,
                 "Recurring: compile the morning metrics digest and post it.",
                 hrs_ago(5), ""},
                {"m2", Role::Assistant,
                 "Compiled the digest: DAU +1.8%, p95 latency flat, 3 metrics "
                 "moved beyond threshold. Posted to the team channel.",
                 hrs_ago(5), ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = calm("r3", "on you: pick the icon set for the settings redesign",
                             hrs_ago(4), "active", ThreadState::Unknown,
                             "narrowed to 2 options, need a call");
            s.messages = {
                {"m1", Role::User,
                 "we need a consistent icon set for the settings panel. "
                 "outline or filled?",
                 hrs_ago(5), ""},
                {"m2", Role::Assistant,
                 "I mocked both against the current panel. Outline reads "
                 "cleaner at 16px and matches the toolbar; filled has more "
                 "presence but fights the text weight. My lean is outline. "
                 "It's your call before I wire the whole set \xe2\x80\x94 which do you "
                 "want?",
                 hrs_ago(4), ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = calm("r4", "Draft release notes for 4.2", hrs_ago(6),
                             "active", ThreadState::Unknown,
                             "first pass done, grouped by area");
            s.messages = {
                {"m1", Role::User, "draft release notes for the 4.2 cut",
                 hrs_ago(7), ""},
                {"m2", Role::Assistant,
                 "Pulled the 62 landed changes since 4.1 and grouped them: "
                 "Performance (9), Bug fixes (31), New (7), Internal (15). "
                 "First draft is written \xe2\x80\x94 want it terser, or is this the "
                 "right level of detail?",
                 hrs_ago(6), ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = calm("r5", "why is the dashboard p95 latency up",
                             hrs_ago(8), "active", ThreadState::Running,
                             "pulling traces");
            s.messages = {
                {"m1", Role::User,
                 "dashboard p95 jumped from 240ms to 900ms overnight. dig in.",
                 hrs_ago(9), ""},
                {"m2", Role::Assistant,
                 "On it. Pulling the trace sample for the regressed window and "
                 "diffing against yesterday. I'll surface the top offender when "
                 "the sample lands \xe2\x80\x94 no action needed yet.",
                 hrs_ago(8), ""},
                {"m3", Role::Tool,
                 "fetch traces window=6h sample=2000 \xe2\x86\x92 in progress",
                 hrs_ago(8) + 120, "shell"},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = calm("r6", "clean up dead feature flags", hrs_ago(11),
                             "active", ThreadState::Running,
                             "scanning for stale flags");
            s.messages = {
                {"m1", Role::User,
                 "find feature flags that have been at 100% for 90+ days and "
                 "list the ones safe to remove",
                 hrs_ago(12), ""},
                {"m2", Role::Assistant,
                 "Scanning the flag registry against rollout history now. I'll "
                 "come back with a list plus the call sites for each \xe2\x80\x94 "
                 "running quietly until then.",
                 hrs_ago(11), ""},
            };
            s.sub_agents = {
                {"r6s1", "Registry scan", SubAgentState::Done,
                 "412 flags enumerated"},
                {"r6s2", "Call-site trace", SubAgentState::Running,
                 "resolving usages for the 90-day set"},
            };
            v.push_back(std::move(s));
        }

        // -- This week (days ago) --
        {
            Session s;
            s.summary = calm("r7", "[P] Migrate the auth middleware off the deprecated API",
                             days_ago(1), "active", ThreadState::Unknown,
                             "PR up, one reviewer nit left");
            s.messages = {
                {"m1", Role::User,
                 "the old session API is deprecated end of quarter. move the "
                 "auth middleware to the new one.",
                 days_ago(1) - 3600, ""},
                {"m2", Role::Assistant,
                 "Swapped all 14 call sites to the new session client, kept "
                 "the old shim behind a flag for one release so we can roll "
                 "back. Tests pass. PR is up.",
                 days_ago(1) - 1800, ""},
                {"m3", Role::Tool,
                 "run auth suite \xe2\x86\x92 138 passed, 0 failed", days_ago(1) - 1200,
                 "shell"},
                {"m4", Role::User, "looks good, one nit on the shim comment",
                 days_ago(1), ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = calm("r8", "Add pagination to the events endpoint",
                             days_ago(2), "active", ThreadState::Unknown,
                             "DONE \xc2\xb7 cursor-based, shipped");
            s.messages = {
                {"m1", Role::User,
                 "the /events endpoint returns everything and times out for "
                 "big accounts. add pagination.",
                 days_ago(2) - 7200, ""},
                {"m2", Role::Assistant,
                 "Added cursor-based pagination (opaque cursor, default page "
                 "100, max 500) and kept the old unpaginated behavior behind a "
                 "one-release deprecation header. Docs updated.",
                 days_ago(2) - 3600, ""},
                {"m3", Role::Assistant,
                 "Shipped. Big-account fetch went from a 30s timeout to ~180ms "
                 "per page. Marking this done.",
                 days_ago(2), ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = calm("r9", "look into the memory growth in the ingest service",
                             days_ago(3), "active", ThreadState::Unknown,
                             "heap dump analyzed, found the leak");
            s.messages = {
                {"m1", Role::User,
                 "ingest service RSS climbs ~2GB/day and only OOM-restarts "
                 "reset it. leak?",
                 days_ago(3) - 5400, ""},
                {"m2", Role::Assistant,
                 "Grabbed a heap dump at hour 6 and hour 18 and diffed them. "
                 "The growth is almost entirely one structure, so here's the "
                 "full breakdown:\n\n"
                 "1. ParserCache holds 1.7GB of the 1.9GB total delta.\n"
                 "2. It's keyed by the FULL request payload (not a hash), so "
                 "every unique body is a distinct entry.\n"
                 "3. There is no eviction path at all \xe2\x80\x94 entries are "
                 "inserted on parse and never removed.\n"
                 "4. Ingest sees ~40k unique payloads/hour, so the map grows "
                 "unbounded until the OOM killer resets the process.\n\n"
                 "Contributing factors I ruled OUT:\n"
                 "- Not the connection pool (steady at 64 sockets).\n"
                 "- Not the metrics buffer (flushes every 10s, flat).\n"
                 "- Not goroutine/thread leak (count is stable at ~120).\n"
                 "- Not fragmentation (RSS tracks live-heap closely in the "
                 "dump).\n\n"
                 "The fix I'm applying:\n"
                 "a. Replace the unbounded map with an LRU capped at 50k "
                 "entries (~120MB at the observed avg entry size).\n"
                 "b. Key by a 16-byte content hash instead of the full payload "
                 "\xe2\x80\x94 drops per-entry overhead ~8x.\n"
                 "c. Add a metric (parser_cache.entries + .evictions) so we "
                 "can alert if it ever saturates again.\n"
                 "d. Backfill a regression test that parses 200k unique "
                 "payloads and asserts the cache stays bounded.\n\n"
                 "Expected result: steady-state RSS drops from ~climbing-2GB/day "
                 "to a flat ~400MB. Rolling it behind a flag so we can shadow "
                 "it for a day before making it the default.",
                 days_ago(3) - 1800, ""},
                {"m3", Role::Tool,
                 "heap diff \xe2\x86\x92 ParserCache 1.7GB / 1.9GB total growth",
                 days_ago(3), "shell"},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = calm("r10", "review D551840 for me", days_ago(3),
                             "active", ThreadState::Unknown,
                             "read the diff, left 3 comments");
            s.messages = {
                {"m1", Role::User, "can you review D551840 before I stamp it",
                 days_ago(3) - 2400, ""},
                {"m2", Role::Assistant,
                 "Read all 240 lines. Logic is sound. Three things worth a "
                 "comment: the new retry has no jitter (thundering herd), one "
                 "error path swallows the cause, and the test only covers the "
                 "happy path. None are blockers \xe2\x80\x94 want me to draft the "
                 "comments?",
                 days_ago(3), ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = calm("r11", "prototype the bulk-export button", days_ago(4),
                             "active", ThreadState::Unknown,
                             "clickable prototype, needs backend");
            s.messages = {
                {"m1", Role::User,
                 "users keep asking to export their whole history as CSV. "
                 "prototype the UI so we can test the flow.",
                 days_ago(4) - 6000, ""},
                {"m2", Role::Assistant,
                 "Built a clickable prototype: an Export button that opens a "
                 "range picker, shows a progress toast, and drops a signed "
                 "download link when ready. The generation is stubbed for now. "
                 "Ready for you to click through.",
                 days_ago(4), ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = calm("r12", "Update the onboarding checklist copy",
                             days_ago(5), "active", ThreadState::Unknown,
                             "rewrote 6 steps, tone check passed");
            s.messages = {
                {"m1", Role::User,
                 "the onboarding checklist reads like a legal doc. make it "
                 "friendlier without losing the steps.",
                 days_ago(5) - 1800, ""},
                {"m2", Role::Assistant,
                 "Rewrote all six steps in a warmer, second-person voice and "
                 "cut the average step from 34 words to 16. Meaning is "
                 "unchanged. Before/after is in the thread.",
                 days_ago(5), ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = calm("r13", "[P] wire up the new empty-state illustration",
                             days_ago(6), "active", ThreadState::Unknown,
                             "asset in, positioned, dark-mode variant left");
            s.messages = {
                {"m1", Role::User,
                 "design dropped the new empty-state art. wire it into the "
                 "list view when there are zero items.",
                 days_ago(6) - 3600, ""},
                {"m2", Role::Assistant,
                 "Imported the asset, added the empty state with the "
                 "illustration centered above the copy and CTA. Light mode is "
                 "done. Need the dark-mode variant of the art before I finish "
                 "\xe2\x80\x94 is there one, or should I tint the existing one?",
                 days_ago(6), ""},
            };
            v.push_back(std::move(s));
        }

        // -- Earlier (weeks and months ago) --
        {
            Session s;
            s.summary = calm("r14", "spike: is it worth caching the config layer",
                             days_ago(12), "active", ThreadState::Unknown,
                             "wrote up findings, marginal win");
            s.messages = {
                {"m1", Role::User,
                 "config reads show up hot in profiles. worth adding a cache?",
                 days_ago(12) - 3600, ""},
                {"m2", Role::Assistant,
                 "Benchmarked it. Config reads are 4% of request time but the "
                 "values change often enough that a cache buys ~1.5% at the "
                 "cost of invalidation complexity. My rec: not worth it yet; "
                 "revisit if reads grow. Wrote up the numbers.",
                 days_ago(12), ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = calm("r15", "fix the timezone bug in the scheduler",
                             days_ago(18), "active", ThreadState::Unknown,
                             "DONE \xc2\xb7 was assuming server-local time");
            s.messages = {
                {"m1", Role::User,
                 "recurring jobs fire an hour off for some users after DST. "
                 "fix it.",
                 days_ago(18) - 5400, ""},
                {"m2", Role::Assistant,
                 "The scheduler stored wall-clock times and re-derived the run "
                 "instant in server-local time, so DST shifts drifted it. "
                 "Switched to storing the user's IANA zone + local time and "
                 "computing the UTC instant per-fire. Added DST-boundary tests.",
                 days_ago(18), ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = calm("r16", "draft the incident writeup for last week's outage",
                             days_ago(21), "active", ThreadState::Unknown,
                             "timeline + 3 action items drafted");
            s.messages = {
                {"m1", Role::User,
                 "we need a blameless writeup for the 40-min outage. draft it "
                 "from the timeline in the channel.",
                 days_ago(21) - 3600, ""},
                {"m2", Role::Assistant,
                 "Drafted: root cause (a bad config push dropped the connection "
                 "pool to 1), a minute-by-minute timeline, and three action "
                 "items (staged config rollout, pool floor alarm, faster "
                 "rollback). Kept it blameless. Ready for your review.",
                 days_ago(21), ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = calm("r17", "Refactor the notification templating",
                             days_ago(34), "archived", ThreadState::Archived,
                             "archived \xc2\xb7 landed last month");
            s.messages = {
                {"m1", Role::User,
                 "the notification templates are copy-pasted across 5 channels. "
                 "consolidate.",
                 days_ago(34) - 7200, ""},
                {"m2", Role::Assistant,
                 "Pulled the shared structure into one template engine with "
                 "per-channel overrides. Deleted ~600 lines of duplication. "
                 "Landed and verified across all five channels. Archiving.",
                 days_ago(34), ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = calm("r18", "one-off: pull the top 50 slow queries",
                             days_ago(47), "archived", ThreadState::Archived,
                             "archived \xc2\xb7 report delivered");
            s.messages = {
                {"m1", Role::User,
                 "give me the 50 slowest queries from the last week with call "
                 "counts",
                 days_ago(47) - 1800, ""},
                {"m2", Role::Tool,
                 "query slow_log order by total_time desc limit 50 \xe2\x86\x92 50 rows",
                 days_ago(47) - 900, "sql"},
                {"m3", Role::Assistant,
                 "Report delivered. The top 3 alone account for 38% of DB "
                 "time; all three are missing the same composite index. Noted "
                 "in the thread. Nothing else needed \xe2\x80\x94 archiving.",
                 days_ago(47), ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = calm("r19", "old spike: evaluate the graph library swap",
                             days_ago(72), "archived", ThreadState::Archived,
                             "archived \xc2\xb7 decided against");
            s.messages = {
                {"m1", Role::Assistant,
                 "Evaluated the alternative graph lib. It's faster on huge "
                 "graphs but our graphs are small and the migration cost is "
                 "high. Recommended staying put. Archiving for reference.",
                 days_ago(72), ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = calm("r20", "[P] investigate customer-reported data mismatch",
                             days_ago(96), "archived", ThreadState::Archived,
                             "archived \xc2\xb7 root-caused + fixed");
            s.messages = {
                {"m1", Role::User,
                 "a customer says their totals don't match between the app and "
                 "the export. can you reproduce?",
                 days_ago(96) - 9000, ""},
                {"m2", Role::Assistant,
                 "Reproduced with their account shape. The export summed "
                 "pre-refund amounts while the app summed post-refund. Aligned "
                 "the export to post-refund and backfilled the affected "
                 "exports. Confirmed with the customer's numbers.",
                 days_ago(96) - 3600, ""},
                {"m3", Role::Tool,
                 "verify export totals vs app for acct sample \xe2\x86\x92 match",
                 days_ago(96), "shell"},
            };
            s.sub_agents = {
                {"r20s1", "Repro on sample account", SubAgentState::Done,
                 "mismatch reproduced"},
                {"r20s2", "Backfill affected exports", SubAgentState::Done,
                 "1,204 exports corrected"},
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
                           "wc -l cache/evict.rs", hrs_ago(2), "shell"};
            shortA.tool_result = "182 cache/evict.rs";
            shortA.tool_status = "completed";
            shortA.tool_duration_ms = 300;
            Message shortB{"fd3", Role::Tool, "head -3 cache/evict.rs",
                           hrs_ago(2), "shell"};
            shortB.tool_result = "use crate::clock;";
            shortB.tool_status = "completed";
            shortB.tool_duration_ms = 200;
            // Over kAutoResultChars, so Auto keeps this pile shut.
            Message longA{"fd5", Role::Tool, "cargo test -p cache",
                          hrs_ago(1), "shell"};
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
            Message longB{"fd6", Role::Tool, "cargo bench -p cache",
                          hrs_ago(1), "shell"};
            longB.tool_result = "bench evict/lru  1.20 us";
            longB.tool_status = "completed";
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

        // PERF FIXTURE: a deliberately LONG transcript for frame-time
        // measurement + virtualization testing. Only seeded when
        // HANABI_BIG_TRANSCRIPT is set (a headless perf run), so it never
        // pollutes the normal mock list. ~120 messages: alternating user /
        // long-assistant prose, interleaved tool runs (which pile), plus
        // sub-agents — the exact shape that made the per-frame rebuild ~15-20ms.
        if (const char* big = std::getenv("HANABI_BIG_TRANSCRIPT");
            big && *big && std::string(big) != "0") {
            Session s;
            s.summary = calm("rbig", "PERF: long transcript for frame timing",
                             days_ago(1), "active", ThreadState::Unknown,
                             "120-message perf fixture");
            const int64_t base = days_ago(1) - 200000;
            for (int k = 0; k < 40; ++k) {
                Message u;
                u.id = "b_u" + std::to_string(k);
                u.role = Role::User;
                u.text = "Follow-up question #" + std::to_string(k) +
                         ": can you dig into the " +
                         (k % 2 ? "latency" : "memory") +
                         " regression and report what you find?";
                u.created_at = base + k * 4000;
                s.messages.push_back(std::move(u));

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
            }
            s.sub_agents = {
                {"bs1", "Heap analysis", SubAgentState::Done, "leak found"},
                {"bs2", "Regression test", SubAgentState::Running, "writing"},
                {"bs3", "Shadow rollout", SubAgentState::Blocked, "needs flag"},
            };
            v.push_back(std::move(s));
        }

        return v;
    }
};

}  // namespace api
