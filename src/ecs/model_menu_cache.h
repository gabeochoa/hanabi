#pragma once

// The deployment's model menu as the app holds it: ONE snapshot of the
// `models` control command, bound to the client it came from.
//
// The rules, each with a reason a test pins:
//   * A request is launched by an explicit open of the model panel, never by
//     a frame, and at most once per open: `request_if_stale` returns true only
//     when a launch is warranted, and the panel calls it from the chip's press.
//   * A failed or unsupported answer arms a BACKOFF (the reference's menu is
//     asked again "after a while", not on every open): the next launch waits
//     `kRetryAfter` from the failure. A good answer is fresh for
//     `kRefreshAge` (the reference's two hours).
//   * Every launch and every landing carry the identity of the client they
//     were for (the shared_ptr's address is the deployment for this process:
//     a new backend is a new client object). A landing for a client that has
//     since been replaced is DISCARDED -- not one stale row reaches the panel
//     -- and `bind(client)` on replacement clears the snapshot, the error and
//     the backoff, so the new backend is asked at once and shows its own
//     menu, never the old one's.
//   * Last-good data survives only a FAILED REFRESH from the SAME client; the
//     row for a harness the menu does not carry is `nullptr`, never another
//     harness's row (that would advertise another runtime's models).

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "../api/client.h"

namespace ecs::model {

struct ModelMenuCache {
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::minutes kRefreshAge{120};
    static constexpr std::chrono::seconds kRetryAfter{30};
    // The most asks that may be outstanding at once, the live one and the
    // retired ones together: a timeout bounds each ask's LIFETIME, not how
    // many rapid backend switches can pile up inside one. At capacity a new
    // client's ask is DEFERRED (its cache stays empty and says so) until a
    // slot drains; no pending future is ever destroyed to make room.
    static constexpr std::size_t kMaxOutstanding = 4;

    api::ModelMenu menu;
    bool loaded = false;            // the bound client answered at least once
    std::string error;              // the last failure's words, "" = none
    Clock::time_point fetchedAt{};  // of `menu`
    Clock::time_point failedAt{};   // of `error`
    bool failedOnce = false;
    std::future<api::Result<api::ModelMenu>> inFlight;
    // Identity is the client's OWNERSHIP (weak_ptr control block), never its
    // address: a client destroyed and another allocated at the same address
    // is a different deployment, and must never read as the same one.
    std::weak_ptr<const api::Client> inFlightFor;
    std::weak_ptr<const api::Client> boundTo;
    bool bound = false;
    static bool same(const std::weak_ptr<const api::Client>& a,
                     const std::weak_ptr<const api::Client>& b) {
        return !a.owner_before(b) && !b.owner_before(a);
    }
    // Asks for clients since replaced: their futures are kept here until
    // they finish, because a std::async future's destructor BLOCKS until its
    // task ends -- overwriting or dropping one would stall the frame. Their
    // values are never landed (identity already says they are not ours).
    std::vector<std::future<api::Result<api::ModelMenu>>> orphans;
    // ONE deferred refresh, retained for the bound client while its panel
    // stays open: an open that found the cap full leaves this set, and the
    // next reap that frees a slot (poll) hands it back through the same gate
    // as one launch -- not a per-frame retry, and never inside the failure
    // backoff. Cleared when the panel closes or the client is replaced.
    bool deferredWanted = false;
    int launches = 0;  // for tests: how many asks actually went out

    // Called whenever the app's client may have changed (every frame is
    // fine: a no-op when it is the same). A different client is a different
    // deployment: everything held is that deployment's and goes. With the
    // panel OPEN at the switch, the new deployment is asked without a close
    // and reopen: the one retained request is armed and the loader's
    // resume path launches it through the gate on this same frame.
    void bind(const std::shared_ptr<const api::Client>& client, bool panelOpen = false) {
        const std::weak_ptr<const api::Client> next = client;
        if (bound && same(next, boundTo)) return;
        bound = true;
        boundTo = next;
        menu = {};
        loaded = false;
        error.clear();
        failedOnce = false;
        fetchedAt = {};
        failedAt = {};
        deferredWanted = panelOpen;
        // An ask in flight for the old client is an orphan from this moment:
        // parked (never destroyed unfinished), reaped by poll, value dropped.
        if (inFlight.valid()) orphans.push_back(std::move(inFlight));
        inFlightFor.reset();
    }

    [[nodiscard]] bool stale(Clock::time_point now) const {
        if (!loaded) return true;
        return now - fetchedAt > kRefreshAge;
    }
    [[nodiscard]] bool in_backoff(Clock::time_point now) const {
        return failedOnce && now - failedAt < kRetryAfter;
    }

    // Whether an ask should go out now for `client`. True at most once per
    // in-flight window and never inside the backoff; the caller launches.
    [[nodiscard]] bool request_if_stale(const std::shared_ptr<api::Client>& client,
                                        Clock::time_point now) {
        if (!client) return false;
        // A press is itself the open path: bind plain (an old client's
        // retained flag must not carry over), then decide below.
        bind(client);
        if (!client->supports_model_menu()) return false;
        // An ask in flight is by construction this client's (bind orphans a
        // replaced client's); it is the one to wait for.
        if (inFlight.valid()) return false;
        if (!stale(now)) return false;
        if (in_backoff(now)) return false;
        // Capacity: the live ask plus the retired ones. Full = defer: the
        // one retained request is set and the next reap will resume it.
        if (outstanding() >= kMaxOutstanding) {
            deferredWanted = true;
            return false;
        }
        deferredWanted = false;
        return true;
    }

    // The panel closed: a refresh it was waiting for is no longer wanted.
    void panel_closed() { deferredWanted = false; }

    // After a reap freed a slot: is the retained refresh due, through the
    // same gate an open uses? True at most once per free slot; the caller
    // launches exactly as the panel's press would.
    [[nodiscard]] bool take_deferred(const std::shared_ptr<api::Client>& client,
                                     Clock::time_point now) {
        if (!deferredWanted) return false;
        if (!client || !bound || !same(std::weak_ptr<const api::Client>(client), boundTo)) {
            deferredWanted = false;
            return false;
        }
        if (!request_if_stale(client, now)) return false;
        deferredWanted = false;
        return true;
    }

    [[nodiscard]] std::size_t outstanding() const {
        return orphans.size() + (inFlight.valid() ? 1u : 0u);
    }
    // Whether a fetch this client wants is being held back by capacity: the
    // panel's note says "waiting for room", not "asking", when no ask of its
    // own exists.
    [[nodiscard]] bool deferred_for_capacity() const {
        return !inFlight.valid() && outstanding() >= kMaxOutstanding;
    }

    // The caller launched an ask for `client`; remember whose it is. A
    // still-pending ask for a replaced client is moved aside, not destroyed.
    void launched(const std::shared_ptr<const api::Client>& client,
                  std::future<api::Result<api::ModelMenu>> f) {
        if (inFlight.valid()) orphans.push_back(std::move(inFlight));
        inFlight = std::move(f);
        inFlightFor = client;
        ++launches;
    }

    // Land a finished ask. Returns true when it changed what the panel shows;
    // a result for a client that is not the bound one is discarded whole and
    // changes nothing (false).
    bool land(std::weak_ptr<const api::Client> forClient, api::Result<api::ModelMenu> r,
              Clock::time_point now) {
        // By VALUE: poll hands the member over, and the member is cleared
        // here -- a reference would alias the very thing being reset and
        // every answer would compare as another client's.
        inFlightFor.reset();
        if (!bound || !same(forClient, boundTo)) return false;
        if (r.ok) {
            menu = std::move(r.value);
            loaded = true;
            error.clear();
            failedOnce = false;
            fetchedAt = now;
            return true;
        }
        // A failed refresh: the last good snapshot (if any) stays; the
        // failure is recorded and arms the backoff.
        error = r.error;
        failedOnce = true;
        failedAt = now;
        return true;
    }

    // Poll the in-flight ask; land it when ready. Reap finished orphans
    // (their values dropped). Frame-loop safe: wait_for(0) throughout.
    // Returns whether what the panel shows changed.
    bool poll(Clock::time_point now) {
        for (std::size_t i = 0; i < orphans.size();) {
            if (orphans[i].wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                (void)orphans[i].get();
                orphans.erase(orphans.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
        if (!inFlight.valid()) return false;
        if (inFlight.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
        auto r = inFlight.get();
        std::weak_ptr<const api::Client> who = std::move(inFlightFor);
        return land(std::move(who), std::move(r), now);
    }

    // The menu row for THIS harness, or nullptr: never another harness's row.
    // An empty harness token (the attach did not say) matches nothing.
    [[nodiscard]] const api::ModelMenuHarness* row_for(std::string_view harness) const {
        if (!loaded || harness.empty()) return nullptr;
        return menu.row_for(harness);
    }

    // The model that RUNS for a session on `harness`: what the attach
    // resolved (its pin, or its harness default) when it named one; else the
    // menu row's `default` marker (the model the server runs when a create
    // omits one); else "" -- nothing is guessed.
    [[nodiscard]] std::string resolved_model(std::string_view harness,
                                             std::string_view attachResolved) const {
        if (!attachResolved.empty()) return std::string(attachResolved);
        if (const auto* row = row_for(harness))
            for (const auto& m : row->models)
                if (m.is_default) return m.key;
        return {};
    }
};

}  // namespace ecs::model
