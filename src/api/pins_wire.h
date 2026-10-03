#pragma once

// The viewer's PINS as the web app keeps them (the reference's
// SessionOverlayPins + InboxSetSync; Knots kt-if8e, the read-back half):
// membership on the session overlay row, order in the synced preferences.
//
//   GET /api/session-overlay  -> { overlays: [{ sessionId, isPinned, ... }] }
//   GET /api/user/preferences -> { preferences: { pinnedSessionOrderIds: [...] }, synced: {...} }
//   PUT /api/user/preferences <- { pinnedSessionOrderIds: [...] }   (PUT, not POST: POST is a 405)
//
// The pin WRITE is Client::set_pinned. Pure: the policy that adopts an answer
// (reconcile) and the order rules are ports of the web's own, so two clients
// show the same pins in the same order.

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "../../vendor/nlohmann/json.hpp"

namespace api::pins {

using json = nlohmann::json;

inline constexpr const char* kOverlayPath = "/api/session-overlay";
inline constexpr const char* kPreferencesPath = "/api/user/preferences";
inline constexpr const char* kOrderKey = "pinnedSessionOrderIds";
// lib/ordered-id-list.ts
inline constexpr std::size_t kMaxOrderedIds = 256;
inline constexpr std::size_t kMaxOrderedIdLength = 255;
inline constexpr std::size_t kMaxOrderedBytes = 20 * 1024;

// One poll: either half may be missing (its route failed); each half is
// adopted or ignored on its own -- an unread ORDER additionally forbids
// writing one, since pushing a list never read would overwrite the reader's
// web arrangement with a guess.
struct Snapshot;
struct Read;

// The overlay rows reduced to what a pin needs. `known` is every session a
// row names, pinned or not: a row saying isPinned:false is the server stating
// a position, which is what lets an unpin elsewhere be told apart from a pin
// it was never told about.
struct Snapshot {
    std::set<std::string> pinned;
    std::set<std::string> known;
};

struct Read {
    std::optional<Snapshot> overlays;
    std::optional<std::vector<std::string>> order;
};

// Nil when the body is not an object carrying `overlays` (a login page must
// not read as "nothing pinned"); an empty array is a real, empty answer.
inline std::optional<Snapshot> parse_overlays(const std::string& body) {
    const json root = json::parse(body, nullptr, false);
    if (root.is_discarded() || !root.is_object() || !root.contains("overlays") ||
        !root["overlays"].is_array())
        return std::nullopt;
    Snapshot out;
    for (const json& row : root["overlays"]) {
        if (!row.is_object() || !row.contains("sessionId") || !row["sessionId"].is_string()) continue;
        const std::string id = row["sessionId"].get<std::string>();
        if (id.empty()) continue;
        out.known.insert(id);
        if (row.contains("isPinned") && row["isPinned"].is_boolean() && row["isPinned"].get<bool>())
            out.pinned.insert(id);
    }
    return out;
}

// normalizeOrderedIdList: empties, over-long ids and duplicates dropped one by
// one, capped at 256 ids and 20 KiB serialized -- the server applies this to
// what is sent, so applying it here means what is sent is what reads back.
inline std::vector<std::string> normalize(const std::vector<std::string>& ids) {
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    std::size_t bytes = 2;  // "[]"
    for (const std::string& id : ids) {
        if (id.empty() || id.size() > kMaxOrderedIdLength || seen.count(id) != 0) continue;
        const std::size_t add = json(id).dump().size() + (out.empty() ? 0 : 1);
        if (bytes + add > kMaxOrderedBytes) continue;
        bytes += add;
        seen.insert(id);
        out.push_back(id);
        if (out.size() == kMaxOrderedIds) break;
    }
    return out;
}

// The arranged order: `preferences` first (read-time defaults put the key
// there, so its absence means unreadable), then the raw `synced` doc. A doc
// without the key is "never arranged" -- an empty list.
inline std::optional<std::vector<std::string>> parse_order(const std::string& body) {
    const json root = json::parse(body, nullptr, false);
    if (root.is_discarded() || !root.is_object()) return std::nullopt;
    for (const char* key : {"preferences", "synced"}) {
        if (!root.contains(key) || !root[key].is_object()) continue;
        const json& doc = root[key];
        if (!doc.contains(kOrderKey) || !doc[kOrderKey].is_array()) return std::vector<std::string>{};
        std::vector<std::string> ids;
        for (const json& v : doc[kOrderKey])
            if (v.is_string()) ids.push_back(v.get<std::string>());
        return normalize(ids);
    }
    return std::nullopt;
}

inline std::string order_body(const std::vector<std::string>& ids) {
    return json{{kOrderKey, normalize(ids)}}.dump();
}

// effectivePinnedSessionOrder + orderPinnedSessions' tail rule: UNARRANGED
// pins first in their incoming order, then the arranged ones in list order;
// stored ids no longer pinned are ignored; anything the cap could not hold
// keeps its incoming place at the end (never silently unpinned).
inline std::vector<std::string> arranged(const std::vector<std::string>& available,
                                         const std::vector<std::string>& preferred) {
    const std::vector<std::string> avail = normalize(available);
    const std::unordered_set<std::string> availSet(avail.begin(), avail.end());
    std::vector<std::string> listed;
    for (const std::string& id : normalize(preferred))
        if (availSet.count(id) != 0) listed.push_back(id);
    std::vector<std::string> out;
    if (listed.empty()) {
        out = avail;
    } else {
        const std::unordered_set<std::string> listedSet(listed.begin(), listed.end());
        for (const std::string& id : avail)
            if (listedSet.count(id) == 0) out.push_back(id);
        out.insert(out.end(), listed.begin(), listed.end());
    }
    if (out.size() != available.size()) {
        const std::unordered_set<std::string> have(out.begin(), out.end());
        for (const std::string& id : available)
            if (have.count(id) == 0) out.push_back(id);
    }
    return out;
}

// pinnedSessionOrderAfterPin: an EMPTY list stays empty (nobody arranged
// anything; seeding one would freeze today's recency); otherwise the fresh pin
// goes to the front.
inline std::vector<std::string> after_pin(const std::vector<std::string>& preferred,
                                          const std::string& id) {
    if (preferred.empty() || preferred.front() == id) return preferred;
    std::vector<std::string> next{id};
    for (const std::string& p : preferred)
        if (p != id) next.push_back(p);
    return normalize(next);
}

inline std::vector<std::string> after_unpin(const std::vector<std::string>& preferred,
                                            const std::string& id) {
    std::vector<std::string> next;
    for (const std::string& p : preferred)
        if (p != id) next.push_back(p);
    return next;
}

// InboxSetSync.reconcile, ported. What does an ABSENCE from the answer mean?
// Known to the server (it named the id before, or a write landed, or a row
// names it now) and absent: really unpinned elsewhere -- dropped. Never known:
// pinned here before syncing existed -- kept, counted as local-only. An id
// whose own write has not landed (`unlanded`) is decided by that intent, over
// an answer computed before the gesture. Only ids that were in the question
// (`asked`) can be dropped: one pinned while the read was in flight survives.
struct Reconciliation {
    std::vector<std::string> order;  // survivors in place, then arrivals sorted
    std::set<std::string> server_known;
    std::vector<std::string> arrived;
    std::vector<std::string> dropped;
    std::vector<std::string> local_only;
};

inline Reconciliation reconcile(const std::vector<std::string>& current,
                                const std::set<std::string>& asked,
                                const std::set<std::string>& server,
                                const std::set<std::string>& server_known,
                                const std::set<std::string>& seen,
                                const std::map<std::string, bool>& unlanded) {
    Reconciliation r;
    std::vector<std::string> survivors;
    for (const std::string& id : current) {
        if (const auto u = unlanded.find(id); u != unlanded.end()) {
            if (u->second) survivors.push_back(id);
            continue;
        }
        const bool answerable =
            asked.count(id) != 0 && (server_known.count(id) != 0 || seen.count(id) != 0);
        if (!answerable || server.count(id) != 0) survivors.push_back(id);
    }
    const std::unordered_set<std::string> surviving(survivors.begin(), survivors.end());
    for (const std::string& id : server) {  // std::set: already sorted
        if (surviving.count(id) != 0) continue;
        if (const auto u = unlanded.find(id); u != unlanded.end() && !u->second) continue;
        r.arrived.push_back(id);
    }
    r.order = survivors;
    r.order.insert(r.order.end(), r.arrived.begin(), r.arrived.end());
    r.server_known = seen;
    r.server_known.insert(server.begin(), server.end());
    const std::unordered_set<std::string> held(current.begin(), current.end());
    for (const std::string& id : server_known)
        if (asked.count(id) == 0 && held.count(id) != 0) r.server_known.insert(id);
    for (const std::string& id : current)
        if (unlanded.count(id) == 0 && surviving.count(id) == 0) r.dropped.push_back(id);
    for (const std::string& id : survivors)
        if (server.count(id) == 0) r.local_only.push_back(id);
    return r;
}

}  // namespace api::pins
