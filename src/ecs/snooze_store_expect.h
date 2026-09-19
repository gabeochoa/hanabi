#pragma once
#include <cstdint>
#include <optional>
#include <string>

#include "inbox_sync_driver.h"

namespace hanabi::snooze_store_expect {

struct Expectation {
    std::string session_id;
    bool absent = false;
    std::int64_t until_unix_sec = 0;
    std::uint64_t epoch = 0;
};

struct Observed {
    bool available = false;
    bool pending = false;
    bool write_in_flight = false;
    std::optional<inbox_sync::Entry> entry;
    std::uint64_t epoch = 0;
};

inline Observed observe(const inbox_sync::Driver& inbox, bool available, const std::string& id) {
    Observed o;
    o.available = available;
    o.pending = inbox.store.pending(id);
    o.write_in_flight = inbox.write_in_flight(id);
    o.entry = inbox.store.get(id);
    o.epoch = inbox.store.mutation_epoch();
    return o;
}

inline bool satisfied(const Expectation& want, const Observed& have) {
    if (!have.available) return false;
    if (have.pending || have.write_in_flight) return false;
    if (have.epoch != want.epoch) return false;
    if (want.absent) return !have.entry.has_value();
    if (!have.entry) return false;
    return have.entry->snoozed_until == want.until_unix_sec && have.entry->snoozed_at.has_value();
}

inline std::string describe(const Observed& have) {
    std::string out = "available=" + std::string(have.available ? "yes" : "no");
    out += " pending=" + std::string(have.pending ? "yes" : "no");
    out += " in_flight=" + std::string(have.write_in_flight ? "yes" : "no");
    out += " epoch=" + std::to_string(have.epoch);
    if (!have.entry) return out + " entry=absent";
    out += " until=" + std::to_string(have.entry->snoozed_until);
    out += " at=" + (have.entry->snoozed_at ? std::to_string(*have.entry->snoozed_at) : std::string("none"));
    return out;
}

inline std::optional<Expectation> parse(const std::string& id, const std::string& kind,
                                        const std::string& value, const std::string& epoch) {
    const auto whole_number = [](const std::string& s) {
        if (s.empty()) return false;
        for (const char c : s)
            if (c < '0' || c > '9') return false;
        return true;
    };
    Expectation e;
    e.session_id = id;
    if (id.empty() || !whole_number(epoch)) return std::nullopt;
    e.epoch = static_cast<std::uint64_t>(std::stoull(epoch));
    if (kind == "absent") {
        if (!value.empty()) return std::nullopt;
        e.absent = true;
        return e;
    }
    if (kind != "confirmed" || !whole_number(value)) return std::nullopt;
    e.until_unix_sec = static_cast<std::int64_t>(std::stoll(value));
    return e;
}

}
