#pragma once

// Settings > Memory's state and its one service loop (the reference's
// AgentMemoryStore, reduced to what this page does: list a folder, open a
// file, save an edit, create a file).
//
// The page writes REQUESTS into MemoryPage; MemorySystem turns each into a
// Client call on a worker thread and folds the answer back when it lands.
// One call at a time: a request made while another is in flight waits, so an
// answer can never land on a folder the reader has already left. A dirty
// draft is never replaced by a listing or another file -- the page refuses to
// navigate until the edit is saved or discarded, and says so.

#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../api/client.h"
#include "../api/memory_wire.h"

namespace ecs {

struct MemoryPage {
    // What the page shows.
    std::string path;  // the folder listed
    std::vector<api::memory::File> files;
    std::vector<std::string> folders;
    bool listed = false;
    std::optional<api::memory::Document> doc;  // the open file
    std::string draft;                          // its text as edited
    std::string error;                          // one plain sentence
    std::string notice;                         // "Saved." / "Created."

    // What the page asked for (one-shot; MemorySystem clears them).
    bool requestList = false;
    std::string requestListPath;
    std::string requestOpenKey;
    bool requestSave = false;
    std::string requestCreateKey;

    // In flight.
    enum class Op { None, List, Read, Write };
    Op inFlight = Op::None;
    std::string inFlightPath;
    std::string inFlightKey;
    bool inFlightCreate = false;
    std::string inFlightContent;
    std::future<api::Result<api::memory::Listing>> listFuture;
    std::future<api::Result<api::memory::Document>> docFuture;

    [[nodiscard]] bool busy() const { return inFlight != Op::None; }
    [[nodiscard]] bool dirty() const { return doc && draft != doc->content; }
    [[nodiscard]] bool can_save() const {
        return doc && dirty() && !doc->linked && !doc->version.empty() &&
               draft.size() <= api::memory::kMaxBytes && !busy();
    }
};

// What the reader is told when an action failed: one plain sentence per
// action; the service's own words go to the log, not the page.
inline const char* memory_failure(MemoryPage::Op op, bool create) {
    switch (op) {
        case MemoryPage::Op::List:
            return "Couldn't load your memory files right now. Try again in a moment.";
        case MemoryPage::Op::Read:
            return "Couldn't open that file right now. Try again in a moment.";
        case MemoryPage::Op::Write:
            return create ? "Couldn't create that file right now. Try again in a moment."
                          : "The save didn't go through. Your edits are still here \xe2\x80\x94 "
                            "try again in a moment.";
        case MemoryPage::Op::None:
            break;
    }
    return "";
}

// One frame of the service loop, separated from the ECS so a unit test can
// drive it with a client of its own.
inline void service_memory(MemoryPage& m, const std::shared_ptr<api::Client>& client,
                           bool wait = false) {
    using namespace std::chrono_literals;
    // Land what finished.
    if (m.inFlight == MemoryPage::Op::List && m.listFuture.valid() &&
        (wait || m.listFuture.wait_for(0s) == std::future_status::ready)) {
        auto r = m.listFuture.get();
        m.inFlight = MemoryPage::Op::None;
        if (r.ok) {
            m.path = r.value.path;
            m.files = std::move(r.value.files);
            m.folders = std::move(r.value.folders);
            m.listed = true;
            m.doc.reset();
            m.draft.clear();
            m.error.clear();
        } else {
            const char* why = api::memory::sentence(api::memory::classify_read(r.error));
            m.error = why != nullptr ? why : memory_failure(MemoryPage::Op::List, false);
        }
    }
    if ((m.inFlight == MemoryPage::Op::Read || m.inFlight == MemoryPage::Op::Write) &&
        m.docFuture.valid() &&
        (wait || m.docFuture.wait_for(0s) == std::future_status::ready)) {
        auto r = m.docFuture.get();
        const MemoryPage::Op op = m.inFlight;
        m.inFlight = MemoryPage::Op::None;
        if (r.ok) {
            const bool keepEdits = op == MemoryPage::Op::Write && m.draft != m.inFlightContent;
            if (op == MemoryPage::Op::Write && m.inFlightCreate) {
                bool known = false;
                for (const auto& f : m.files) known = known || f.key == r.value.key;
                if (!known)
                    m.files.push_back({r.value.key,
                                       static_cast<std::int64_t>(r.value.content.size()), false});
            }
            m.doc = std::move(r.value);
            // An edit typed while the save was in flight stays in the box.
            if (!keepEdits) m.draft = m.doc->content;
            m.error.clear();
            m.notice = op == MemoryPage::Op::Write ? (m.inFlightCreate ? "Created." : "Saved.")
                                                   : "";
        } else {
            // A cause a retry cannot fix says what to do instead (stale
            // version, name taken, read-only, sign-in gone, no access);
            // anything else keeps the action's own "try again" sentence. The
            // draft is untouched either way, so the edits can be copied.
            const char* why = api::memory::sentence(op == MemoryPage::Op::Write
                                                        ? api::memory::classify_write(r.error)
                                                        : api::memory::classify_read(r.error));
            m.error = why != nullptr ? why : memory_failure(op, m.inFlightCreate);
            m.notice.clear();
        }
    }
    if (m.busy() || !client) return;

    // Start the next request. A dirty draft holds the page where it is.
    if (m.requestSave) {
        m.requestSave = false;
        if (m.can_save()) {
            m.inFlight = MemoryPage::Op::Write;
            m.inFlightCreate = false;
            m.inFlightContent = m.draft;
            const std::string path = m.path, key = m.doc->key, content = m.draft,
                              version = m.doc->version;
            m.docFuture = std::async(std::launch::async, [client, path, key, content, version] {
                return client->memory_write(path, key, content, version);
            });
            return;
        }
    }
    if (!m.requestCreateKey.empty()) {
        const std::string key = m.requestCreateKey;
        m.requestCreateKey.clear();
        if (m.dirty()) {
            m.error = "Save or discard this edit first.";
        } else if (!api::memory::valid_key(key)) {
            m.error = "Use a file name without '/', empty segments, '.', '..', or control characters.";
        } else {
            m.inFlight = MemoryPage::Op::Write;
            m.inFlightCreate = true;
            m.inFlightContent.clear();
            const std::string path = m.path;
            m.docFuture = std::async(std::launch::async, [client, path, key] {
                return client->memory_write(path, key, "", "");
            });
            return;
        }
    }
    if (!m.requestOpenKey.empty()) {
        const std::string key = m.requestOpenKey;
        m.requestOpenKey.clear();
        if (m.dirty()) {
            m.error = "Save or discard this edit first.";
        } else {
            m.inFlight = MemoryPage::Op::Read;
            m.notice.clear();
            const std::string path = m.path;
            m.docFuture = std::async(std::launch::async,
                                     [client, path, key] { return client->memory_read(path, key); });
            return;
        }
    }
    if (m.requestList) {
        m.requestList = false;
        if (m.dirty()) {
            m.error = "Save or discard this edit first.";
        } else {
            m.inFlight = MemoryPage::Op::List;
            m.notice.clear();
            const std::string path = m.requestListPath;
            m.listFuture = std::async(std::launch::async,
                                      [client, path] { return client->memory_list(path); });
        }
    }
}

}  // namespace ecs
