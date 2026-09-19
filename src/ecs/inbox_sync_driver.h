#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../api/client.h"
#include "inbox_sync_store.h"

namespace hanabi::inbox_sync {

struct SnoozeRequest {
    std::string session_id;
    std::optional<std::int64_t> until;
};

class Driver {
   public:
    Store store{1};
    std::uint64_t client_generation = 1;
    std::optional<SnoozeRequest> request;

    struct Read {
        Store::ReadTicket ticket;
        std::future<api::Result<api::InboxStateRead>> future;
    };
    struct Write {
        Intent intent;
        std::future<api::Result<api::InboxStateWrite>> future;
    };
    std::vector<Read> reads;
    std::vector<Write> writes;

    void on_client_replaced() {
        store.reset(++client_generation);
        request.reset();
    }

    [[nodiscard]] bool read_in_flight_for_current_client() const {
        for (const Read& r : reads)
            if (r.ticket.generation == client_generation) return true;
        return false;
    }
    [[nodiscard]] bool write_in_flight(const std::string& id) const {
        for (const Write& w : writes)
            if (w.intent.session_id == id) return true;
        return false;
    }
    [[nodiscard]] bool busy(const std::string& id) const {
        return store.pending(id) || write_in_flight(id);
    }
    [[nodiscard]] bool available(const api::Client* client) const {
        return client != nullptr && client->supports_inbox_state() && store.synced();
    }

    void launch_read(const std::shared_ptr<api::Client>& client) {
        if (!client || !client->supports_inbox_state() || read_in_flight_for_current_client()) return;
        Read read;
        read.ticket = store.begin_read();
        std::shared_ptr<api::Client> c = client;
        read.future = std::async(std::launch::async, [c] { return c->read_inbox_state(); });
        reads.push_back(std::move(read));
    }

    void drain(const std::shared_ptr<api::Client>& client, std::int64_t now_unix_sec,
               const std::function<void(const std::string&)>& toast) {
        for (auto it = reads.begin(); it != reads.end();) {
            if (!it->future.valid() ||
                it->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                ++it;
                continue;
            }
            store.read_landed(it->ticket, it->future.get());
            it = reads.erase(it);
        }
        if (request) {
            const SnoozeRequest req = *request;
            request.reset();
            if (!client || !client->supports_inbox_state() || !store.synced()) {
                toast("Snooze is unsynced: " +
                      (store.last_error().empty() ? std::string("not connected to the web app")
                                                  : store.last_error()));
            } else if (busy(req.session_id)) {
                toast("Still saving the last snooze for this thread");
            } else if (auto intent = store.begin_write(req.session_id, req.until, now_unix_sec)) {
                std::shared_ptr<api::Client> c = client;
                const std::string id = req.session_id;
                const std::optional<std::int64_t> until = req.until;
                Write w;
                w.intent = *intent;
                w.future = std::async(std::launch::async, [c, id, until] { return c->write_snooze(id, until); });
                writes.push_back(std::move(w));
            }
        }
        for (auto it = writes.begin(); it != writes.end();) {
            if (!it->future.valid() ||
                it->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                ++it;
                continue;
            }
            const Change change = store.write_landed(it->intent, it->future.get());
            if (change.outcome == Outcome::RolledBack) toast(change.reason);
            it = writes.erase(it);
        }
    }
};

}
