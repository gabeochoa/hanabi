#pragma once

// Deterministic, offline sample data source. This is the default backend so
// the app is fully functional with no configuration and no network. The data
// below is invented purely to exercise the UI.

#include <algorithm>

#include "client.h"

namespace api {

class MockClient : public Client {
  public:
    std::string backend_label() const override { return "mock"; }

    Result<std::vector<SessionSummary>> list_sessions() override {
        auto sessions = seed();
        std::vector<SessionSummary> out;
        out.reserve(sessions.size());
        for (auto& s : sessions) out.push_back(s.summary);
        std::sort(out.begin(), out.end(),
                  [](const SessionSummary& a, const SessionSummary& b) {
                      return a.updated_at > b.updated_at;
                  });
        return Result<std::vector<SessionSummary>>::success(std::move(out));
    }

    Result<Session> get_session(const std::string& id) override {
        for (auto& s : seed()) {
            if (s.summary.id == id)
                return Result<Session>::success(s);
        }
        return Result<Session>::failure("no such session: " + id);
    }

  private:
    static int64_t hrs_ago(int64_t h) {
        // Fixed reference time so the sample list is stable across runs.
        constexpr int64_t kRef = 1785500000;  // arbitrary fixed epoch
        return kRef - h * 3600;
    }

    static std::vector<Session> seed() {
        std::vector<Session> v;

        {
            Session s;
            s.summary = {"s-welcome", "Welcome to Hanabi", hrs_ago(1),
                         "active",
                         "A native desktop client for your conversations."};
            s.messages = {
                {"m1", Role::User, "what is this app?", hrs_ago(1) - 120, ""},
                {"m2", Role::Assistant,
                 "Hanabi is a small native desktop client for browsing your "
                 "conversation sessions. It loads your threads locally and "
                 "renders them in a fast, minimal interface.",
                 hrs_ago(1) - 90, ""},
                {"m3", Role::User, "does it need a network connection?",
                 hrs_ago(1) - 60, ""},
                {"m4", Role::Assistant,
                 "Not by default. It ships with sample data (the mock "
                 "backend) so it runs standalone. Point it at a real backend "
                 "only by setting environment variables at runtime.",
                 hrs_ago(1) - 30, ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = {"s-diffs", "Reviewing a pull request", hrs_ago(5),
                         "idle", "Walked through the diff and left notes."};
            s.messages = {
                {"m1", Role::User, "can you review the changes on my branch?",
                 hrs_ago(5) - 300, ""},
                {"m2", Role::Tool, "git diff --stat", hrs_ago(5) - 280,
                 "shell"},
                {"m3", Role::Assistant,
                 "The diff looks focused. One suggestion: extract the parsing "
                 "loop into its own function so it can be unit-tested "
                 "independently. Otherwise it reads clean.",
                 hrs_ago(5) - 250, ""},
            };
            v.push_back(std::move(s));
        }
        {
            Session s;
            s.summary = {"s-planning", "Sprint planning notes", hrs_ago(26),
                         "archived", "Broke the milestone into tasks."};
            s.messages = {
                {"m1", Role::User,
                 "help me break this milestone into small tasks",
                 hrs_ago(26) - 600, ""},
                {"m2", Role::Assistant,
                 "Here's a breakdown into independently shippable pieces:\n"
                 "1. Data model + adapter interface\n"
                 "2. Session list view\n"
                 "3. Transcript view\n"
                 "4. Live backend wiring (behind config)\n"
                 "Each can land on its own without blocking the others.",
                 hrs_ago(26) - 540, ""},
            };
            v.push_back(std::move(s));
        }

        return v;
    }
};

}  // namespace api
