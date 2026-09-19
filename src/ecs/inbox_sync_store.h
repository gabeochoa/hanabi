#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "../api/inbox_state_wire.h"
#include "../api/types.h"

namespace hanabi::inbox_sync {

using Seconds = api::inbox_state::Seconds;

struct Entry {
    Seconds snoozed_until = 0;
    std::optional<Seconds> snoozed_at;
    bool operator==(const Entry&) const = default;
};

enum class Phase { Unsynced, SignedOut, Unreachable, Malformed, Synced };

struct Intent {
    std::uint64_t ticket = 0;
    std::uint64_t generation = 0;
    std::string session_id;
    std::optional<Seconds> until;
    std::optional<Entry> before;
};

enum class Outcome { Applied, Ignored, RolledBack, Confirmed };

struct Change {
    Outcome outcome = Outcome::Ignored;
    std::string reason;
    bool operator==(const Change&) const = default;
};

class Store {
   public:
    explicit Store(std::uint64_t generation = 1) : generation_(generation) {}

    [[nodiscard]] std::uint64_t generation() const { return generation_; }
    [[nodiscard]] Phase phase() const { return phase_; }
    [[nodiscard]] bool synced() const { return phase_ == Phase::Synced; }
    [[nodiscard]] const std::map<std::string, Entry>& entries() const { return entries_; }
    [[nodiscard]] std::optional<Entry> get(std::string_view id) const {
        auto it = entries_.find(std::string(id));
        if (it == entries_.end()) return std::nullopt;
        return it->second;
    }
    [[nodiscard]] bool pending(std::string_view id) const {
        return pending_.count(std::string(id)) != 0;
    }
    [[nodiscard]] const std::string& last_error() const { return error_; }
    [[nodiscard]] std::uint64_t mutation_epoch() const { return epoch_; }

    void reset(std::uint64_t generation) {
        generation_ = generation;
        phase_ = Phase::Unsynced;
        entries_.clear();
        pending_.clear();
        confirmed_.clear();
        error_.clear();
        epoch_ = 0;
    }

    struct ReadTicket {
        std::uint64_t generation = 0;
        std::uint64_t epoch = 0;
    };
    [[nodiscard]] ReadTicket begin_read() const { return {generation_, epoch_}; }

    Change read_landed(std::uint64_t generation, const api::Result<api::InboxStateRead>& r) {
        return read_landed(ReadTicket{generation, epoch_}, r);
    }

    Change read_landed(ReadTicket t, const api::Result<api::InboxStateRead>& r) {
        if (t.generation != generation_) return {Outcome::Ignored, "another generation"};
        if (!r.ok) {
            phase_ = r.value.signed_out ? Phase::SignedOut : Phase::Unreachable;
            error_ = phase_ == Phase::SignedOut ? "not signed in to the web app"
                                                : "could not reach the web app";
            return {Outcome::Ignored, error_};
        }
        if (r.value.signed_out) {
            phase_ = Phase::SignedOut;
            error_ = "not signed in to the web app";
            return {Outcome::Ignored, error_};
        }
        const auto snap = api::inbox_state::parse_snapshot(r.value.body);
        if (!snap) {
            phase_ = Phase::Malformed;
            error_ = "the web app sent something unexpected";
            return {Outcome::Ignored, error_};
        }
        std::map<std::string, Entry> fresh;
        for (const auto& [id, until] : snap->snoozed_until) {
            Entry e;
            e.snoozed_until = until;
            if (auto at = snap->snoozed_at.find(id); at != snap->snoozed_at.end())
                e.snoozed_at = at->second;
            fresh[id] = e;
        }
        for (const auto& [id, slot] : pending_) {
            (void)slot;
            auto mine = entries_.find(id);
            if (mine == entries_.end()) fresh.erase(id);
            else fresh[id] = mine->second;
        }
        for (const auto& [id, confirmed_at] : confirmed_) {
            if (confirmed_at <= t.epoch) continue;
            auto mine = entries_.find(id);
            if (mine == entries_.end()) fresh.erase(id);
            else fresh[id] = mine->second;
        }
        entries_ = std::move(fresh);
        phase_ = Phase::Synced;
        error_.clear();
        return {Outcome::Applied, ""};
    }

    std::optional<Intent> begin_write(std::string_view session_id, std::optional<Seconds> until,
                                      std::optional<Seconds> optimistic_at) {
        if (phase_ != Phase::Synced) return std::nullopt;
        if (!api::inbox_state::is_valid_session_id(session_id)) return std::nullopt;
        const std::string id(session_id);
        Intent in;
        in.ticket = ++tickets_;
        in.generation = generation_;
        in.session_id = id;
        in.until = until;
        if (pending_.count(id) != 0) return std::nullopt;
        if (auto it = entries_.find(id); it != entries_.end()) in.before = it->second;
        if (until) {
            Entry e;
            e.snoozed_until = *until;
            e.snoozed_at = optimistic_at;
            entries_[id] = e;
        } else {
            entries_.erase(id);
        }
        pending_[id] = in;
        return in;
    }

    Change write_landed(const Intent& in, const api::Result<api::InboxStateWrite>& r) {
        if (in.generation != generation_) return {Outcome::Ignored, "another generation"};
        auto it = pending_.find(in.session_id);
        if (it == pending_.end() || it->second.ticket != in.ticket)
            return {Outcome::Ignored, "superseded"};
        pending_.erase(it);
        if (!r.ok) return rollback(in, explain(r));
        const auto echo = api::inbox_state::parse_echo(r.value.body);
        if (!echo) return rollback(in, "the web app answered without an echo");
        const auto confirmation = api::inbox_state::confirm_snooze(*echo, in.until);
        using api::inbox_state::Confirmation;
        if (confirmation.kind == Confirmation::Unconfirmed)
            return rollback(in, "the web app did not confirm the snooze");
        confirmed_[in.session_id] = ++epoch_;
        if (confirmation.kind == Confirmation::Cleared) {
            entries_.erase(in.session_id);
            return {Outcome::Confirmed, ""};
        }
        Entry e;
        e.snoozed_until = *confirmation.snoozed_until;
        e.snoozed_at = confirmation.snoozed_at;
        entries_[in.session_id] = e;
        return {Outcome::Confirmed, ""};
    }

    static std::string explain(const api::Result<api::InboxStateWrite>& r) {
        switch (r.value.http_status) {
            case 400: return "Snooze refused: outside the allowed window";
            case 401:
            case 403: return "Snooze refused: not signed in to the web app";
            case 409: return "Snooze refused: too many snoozes";
            case 0: return "Snooze not saved: could not reach the web app";
            default: return "Snooze refused: HTTP " + std::to_string(r.value.http_status);
        }
    }

   private:
    Change rollback(const Intent& in, std::string why) {
        if (in.before) entries_[in.session_id] = *in.before;
        else entries_.erase(in.session_id);
        error_ = why;
        return {Outcome::RolledBack, std::move(why)};
    }

    std::uint64_t generation_ = 1;
    std::uint64_t tickets_ = 0;
    std::uint64_t epoch_ = 0;
    Phase phase_ = Phase::Unsynced;
    std::map<std::string, Entry> entries_;
    std::map<std::string, Intent> pending_;
    std::map<std::string, std::uint64_t> confirmed_;
    std::string error_;
};

}
