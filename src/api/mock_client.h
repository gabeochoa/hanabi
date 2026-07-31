#pragma once

// Deterministic, offline sample data source. This is the default backend so
// the app is fully functional with no configuration and no network. The data
// below is invented purely to exercise the UI — it mirrors the design mock's
// thread set so smart views, folders, and high-signal rows all have real
// states to render. Nothing here names or encodes any real service.

#include <algorithm>

#include "client.h"

namespace api {

class MockClient : public Client {
  public:
    std::string backend_label() const override { return "mock"; }

    Result<std::vector<SessionSummary>> list_sessions() override {
        auto sessions = seed();
        std::vector<SessionSummary> out;
        out.reserve(sessions.size() + created_.size());
        for (auto& s : sessions) out.push_back(s.summary);
        // Sessions created this run (via the composer) live only in memory.
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

  private:
    // Sessions created via the composer during this run (mock is otherwise
    // stateless). Merged into list_sessions/get_session above.
    std::vector<Session> created_;

    static int64_t hrs_ago(int64_t h) {
        // Fixed reference time so the sample list is stable across runs.
        constexpr int64_t kRef = 1785500000;  // arbitrary fixed epoch
        return kRef - h * 3600;
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
                 "the receipt. Wrote 18 new tests.",
                 hrs_ago(1), ""},
                {"m3", Role::Tool,
                 "buck test \xe2\x86\x92 214 passed, 0 failed \xc2\xb7 lint "
                 "clean \xc2\xb7 type check clean",
                 hrs_ago(0) - 2100, "shell"},
                {"m4", Role::Assistant,
                 "Deployed to staging. Test link is ready - subscribe at Tier "
                 "1, upgrade to Tier 2, and confirm the proration line.",
                 hrs_ago(0) - 1800, ""},
            };
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

        return v;
    }
};

}  // namespace api
