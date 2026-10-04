#pragma once
#include <filesystem>
#include <branding.h>
#include "attachment_intake_system.h"
#include "../native_extras.h"
#include "../ui/screen_capture.h"

#include <chrono>

#include <ctime>

// Services Settings > Memory's requests (memory_page.h) once a frame.

#include "components.h"
#include "memory_page.h"

namespace ecs {

// Link-preview cards (D53): scan the newest rows of each open thread when
// they change (and once a minute, for expiry), then fetch what is unknown or
// stale -- at most kFetchesPerPass at once, off the frame. Nothing on the
// draw path starts a fetch; the transcript only reads `entries`.
inline void service_link_previews(AppComponent& app) {
    namespace lp = api::link_preview;
    auto& L = app.linkPreviews;
    if (!app.client || !app.client->supports_link_previews()) return;
    const auto now = static_cast<std::int64_t>(std::time(nullptr));
    // Land what finished.
    for (auto it = L.pending.begin(); it != L.pending.end();) {
        if (it->f.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }
        auto [r, ttl] = it->f.get();
        std::optional<lp::Entry> old;
        if (const auto e = L.entries.find(it->ref.id); e != L.entries.end()) old = e->second;
        const std::optional<lp::Card> card = r.ok ? r.value : std::nullopt;
        const lp::Entry next = lp::merged(old, card, now, ttl);
        if (!old || old->card != next.card) ++L.revision;
        L.entries[it->ref.id] = next;
        it = L.pending.erase(it);
    }
    // Scan.
    const double t = static_cast<double>(now);
    const bool timer = t >= L.rescanAt;
    if (timer) L.rescanAt = t + 60.0;
    for (std::size_t p = 0; p < app.panes.size(); ++p) {
        const Pane& pane = app.panes[p];
        if (!pane.openSession) continue;
        const auto& msgs = pane.openSession->messages;
        const std::string sig = pane.openSession->summary.id + "|" + std::to_string(msgs.size()) +
                                "|" + (msgs.empty() ? std::string() : msgs.back().id) + "|" +
                                std::to_string(msgs.empty() ? 0 : msgs.back().text.size());
        std::string& seen = L.scanned[std::to_string(p)];
        if (seen == sig && !timer) continue;
        seen = sig;
        const std::size_t from = msgs.size() > lp::kRowsScanned ? msgs.size() - lp::kRowsScanned : 0;
        for (std::size_t i = from; i < msgs.size(); ++i) {
            const auto& m = msgs[i];
            if (m.role == api::Role::Tool || m.role == api::Role::System) continue;
            for (const auto& ref : lp::refs_in(m.text)) {
                const auto e = L.entries.find(ref.id);
                if (e != L.entries.end() && e->second.fresh(now)) continue;
                bool queued = false;
                for (const auto& w : L.wanted) queued = queued || w.id == ref.id;
                for (const auto& q : L.pending) queued = queued || q.ref.id == ref.id;
                if (!queued) L.wanted.push_back(ref);
            }
        }
    }
    // Fetch.
    while (!L.wanted.empty() && L.pending.size() < lp::kFetchesPerPass) {
        const lp::Ref ref = L.wanted.front();
        L.wanted.erase(L.wanted.begin());
        std::shared_ptr<api::Client> c = app.client;
        L.pending.push_back({ref, std::async(std::launch::async, [c, ref] {
                                 int ttl = lp::kFallbackTtl;
                                 auto r = c->fetch_link_preview(ref, &ttl);
                                 return std::make_pair(std::move(r), ttl);
                             })});
    }
}

// Space settings: the roster and the viewer's fbid on open, the people search
// a beat after typing stops, and one write at a time; every answer lands here,
// off the frame. A write Metamate confirmed re-reads the Spaces list (and the
// roster, for member writes); a refusal is shown in the server's words and
// the control goes back to what the server holds.
inline void service_space_settings(AppComponent& app) {
    namespace sm = api::space_manage;
    using namespace std::chrono_literals;
    auto& S = app.spaceSettings;
    // HANABI_TEST_SPACE_SETTINGS=<id>: open that Space's sheet once the
    // catalog has it (screenshot captures; the mock only).
    static bool testOpened = false;
    if (!testOpened && app.backend_label == "mock")
        if (const char* t = std::getenv("HANABI_TEST_SPACE_SETTINGS"); t != nullptr && *t && app.find_space(t)) {
            testOpened = true;
            app.open_space_settings(t);
        }
    if (!S.open || !app.client) return;
    const auto ready = [](auto& f) { return f.valid() && f.wait_for(0s) == std::future_status::ready; };
    auto c = app.client;
    if (!S.rosterAsked) {
        S.rosterAsked = true;
        const std::string body = sm::roster_body(S.spaceId);
        S.rosterFuture = std::async(std::launch::async, [c, body] { return c->graphql_field(body, "xfb_metamate_project"); });
    }
    if (!S.viewerAsked) {
        S.viewerAsked = true;
        S.viewerFuture = std::async(std::launch::async, [c] { return c->graphql_field(sm::viewer_body(), "viewer_intern_user"); });
    }
    if (ready(S.rosterFuture)) {
        auto r = S.rosterFuture.get();
        const auto parsed = r.ok ? sm::parse_roster(nlohmann::json{{"xfb_metamate_project", r.value}}) : std::nullopt;
        if (parsed) {
            S.roster = *parsed;
            S.rosterLoaded = true;
            S.rosterFailed = false;
        } else {
            S.rosterFailed = true;  // the rows stay what they were: unreadable is not empty
        }
    }
    if (ready(S.viewerFuture)) {
        auto r = S.viewerFuture.get();
        if (r.ok) S.viewerFbid = sm::parse_viewer(nlohmann::json{{"viewer_intern_user", r.value}});
    }
    // People search, ~300 ms (18 frames) after the last keystroke.
    ++S.sinceTyped;
    if (!sm::worth_searching(S.query)) {
        S.people.clear();
        S.searching = false;
        S.peopleFailed = false;
        S.searchedFor.clear();
    } else if (S.query != S.searchedFor && !S.peopleFuture.valid() && S.sinceTyped >= 18) {
        S.searchedFor = S.query;
        S.searching = true;
        const std::string body = sm::people_body(S.query);
        S.peopleFuture = std::async(std::launch::async, [c, body] { return c->graphql_field(body, "intern_typeahead_query"); });
    }
    if (ready(S.peopleFuture)) {
        auto r = S.peopleFuture.get();
        S.searching = false;
        const auto parsed = r.ok ? sm::parse_people(nlohmann::json{{"intern_typeahead_query", r.value}}) : std::nullopt;
        if (S.searchedFor == S.query) {  // an answer for an older query is not shown
            S.peopleFailed = !parsed;
            S.people = parsed ? *parsed : std::vector<sm::Person>{};
        }
    }
    // A write landed.
    if (S.write && ready(S.write->f)) {
        auto r = S.write->f.get();
        const sm::Write kind = S.write->kind;
        const std::string arg = S.write->arg;
        S.write.reset();
        S.busy.clear();
        const auto got = r.ok ? sm::stored(kind, r.value) : std::nullopt;
        if (!got) {
            S.failure = r.ok ? sm::fallback(kind) : sm::refusal(kind, r.error);
            if (kind == sm::Write::Visibility) S.visibility = arg;  // put back what the server holds
        } else {
            S.failure.clear();
            switch (kind) {
                case sm::Write::Rename: S.name = *got; break;  // what Metamate stored, never what was typed
                case sm::Write::Emoji: S.emoji = *got; break;
                case sm::Write::Visibility: S.visibility = *got; break;
                case sm::Write::AddMember:
                    S.query.clear();
                    S.people.clear();
                    [[fallthrough]];
                case sm::Write::RemoveMember:
                case sm::Write::Role: S.rosterAsked = false; break;  // re-read
                case sm::Write::Leave:
                case sm::Write::Archive:
                    app.raise_toast(kind == sm::Write::Leave ? "You left the Space" : "Space archived",
                                    std::string(), AppComponent::ToastUndo::None);
                    S.reset();
                    break;
            }
            app.spacesRequested = false;  // the catalog says what changed
        }
    }
}

// People's photos: land what finished, start what was wanted (four at once).
inline void service_people_photos(AppComponent& app) {
    using namespace std::chrono_literals;
    auto& P = app.peoplePhotos;
    if (!app.client) return;
    for (auto it = P.pending.begin(); it != P.pending.end();) {
        if (it->second.wait_for(0s) != std::future_status::ready) {
            ++it;
            continue;
        }
        auto r = it->second.get();
        if (r.ok) P.ready[it->first] = r.value;
        else {
            P.none.insert(it->first);
            if (r.error != "no picture") std::fprintf(stderr, "[photos] %s: %s\n", it->first.c_str(), r.error.c_str());
        }
        it = P.pending.erase(it);
    }
    while (!P.wanted.empty() && P.pending.size() < 4) {
        const std::string fbid = P.wanted.front();
        P.wanted.erase(P.wanted.begin());
        auto c = app.client;
        const std::string dir = api::disk_cache::cache_dir();
        P.pending.emplace(fbid, std::async(std::launch::async, [c, fbid, dir] { return c->fetch_person_photo(fbid, dir); }));
    }
}

// Screenshot to composer (Knots kt-8uce): a press becomes a capture off the
// frame, and the shot is staged where the reader is looking -- or refused in
// a sentence that names the move (ui/screen_capture.h).
extern "C" void metal_activate_app(void);
// Brings the app forward -- never from a script (a headless run must not take
// focus from whoever is at this Mac).
inline void capture_activate() {
    if (std::getenv("HANABI_TEST_CAPTURE") == nullptr) metal_activate_app();
}

inline void service_screen_capture(AppComponent& app) {
    namespace sc = hanabi::screen_capture;
    using namespace std::chrono_literals;
    if (app.requestCapture && !app.captureFuture.valid()) {
        const auto press = *app.requestCapture;
        app.requestCapture.reset();
        std::error_code ec;
        const auto dir = std::filesystem::temp_directory_path(ec) / "hanabi-capture";
        std::filesystem::create_directories(dir, ec);
        static int serial = 0;
        app.capturePath = (dir / (std::to_string(++serial) + "-" + sc::file_name(press.app))).string();
        app.captureApp = press.app;
        const std::string out = app.capturePath;
        const int pid = press.pid;
        app.captureFuture = std::async(std::launch::async, [pid, out] {
            char err[256] = {};
            const int code = native_capture_window(pid, out.c_str(), err, sizeof(err));
            return std::make_pair(code, std::string(err));
        });
    }
    if (!app.captureFuture.valid() || app.captureFuture.wait_for(0s) != std::future_status::ready) return;
    const auto [code, detail] = app.captureFuture.get();
    const std::string name = product_branding::kAppName;
    if (code != 0) {
        const auto f = static_cast<sc::Failure>(code - 1);
        const bool activate = sc::activates_for_refusal(app.captureRefusedOnce);
        app.captureRefusedOnce = true;
        std::fprintf(stderr, "[shot] no attachment (code %d)\n", code);
        if (activate) {
            capture_activate();
            if (sc::fixable_by_granting(f)) native_open_screen_recording_settings();
        }
        app.raise_toast(sc::message(f, name, detail), std::string(), AppComponent::ToastUndo::None);
        return;
    }
    const bool threadOnScreen = app.view == SmartView::Chat && !app.pane().selectedId.empty() &&
                                !model::is_surface_tab(app.pane().selectedId);
    switch (sc::destination_for(threadOnScreen, app.client && app.client->supports_attachments())) {
        case sc::Destination::Unsupported:
            app.raise_toast(sc::kUnsupported, std::string(), AppComponent::ToastUndo::None);
            return;
        case sc::Destination::NewConversation:
            app.requestNewTask = true;  // the new-conversation composer is the __kickoff__ draft
            break;
        case sc::Destination::Session: break;
    }
    capture_activate();
    // Named for what it shows ("<app>-window.png"), staged into the composer
    // the reader is looking at -- or the new conversation's.
    std::error_code ec;
    const auto named = std::filesystem::path(app.capturePath).parent_path() /
                       sc::file_name(app.captureApp);
    std::filesystem::rename(app.capturePath, named, ec);
    AttachmentIntakeSystem::add(app, (ec ? app.capturePath : named.string()).c_str());
}

struct MemorySystem : afterhours::System<AppComponent> {
    void for_each_with(afterhours::Entity&, AppComponent& app, float) override {
        service_memory(app.memory, app.client);
        service_space_settings(app);
        if (app.requestCapture || app.captureFuture.valid()) service_screen_capture(app);
        if (!app.peoplePhotos.pending.empty() || !app.peoplePhotos.wanted.empty()) service_people_photos(app);
        // The Spaces list for the @ picker: asked once, off the frame; a
        // failure leaves the picker with threads only.
        using namespace std::chrono_literals;
        if (!app.spacesRequested && app.client && app.client->supports_spaces()) {
            app.spacesRequested = true;
            auto c = app.client;
            app.spacesFuture = std::async(std::launch::async, [c] { return c->list_spaces(); });
        }
        // The web-app folders: read both halves off the frame (launch, every
        // ten minutes, and after each write lands); a failed read keeps the
        // last carve.
        {
            const double now = static_cast<double>(std::time(nullptr));
            const auto read_carve = [](std::shared_ptr<api::Client> c) {
                auto f = c->web_call("GET", api::folders::kFoldersPath, "");
                if (!f.ok) return api::Result<api::folders::Carve>::failure(f.error);
                auto o = c->web_call("GET", api::folders::kOverlayPath, "");
                if (!o.ok) return api::Result<api::folders::Carve>::failure(o.error);
                if (f.value.status != 200 || o.value.status != 200)
                    return api::Result<api::folders::Carve>::failure("folders HTTP " +
                                                                    std::to_string(f.value.status));
                auto folders = api::folders::parse_folders(f.value.body);
                auto members = api::folders::parse_membership(o.value.body);
                if (!folders || !members)
                    return api::Result<api::folders::Carve>::failure("folders unreadable");
                return api::Result<api::folders::Carve>::success(
                    api::folders::carve(std::move(*folders), *members));
            };
            if (!app.webFoldersFuture.valid() && app.client && app.client->supports_web_routes() &&
                (app.webFoldersAt < 0.0 || now - app.webFoldersAt >= 600.0)) {
                app.webFoldersAt = now;
                auto c = app.client;
                app.webFoldersFuture = std::async(std::launch::async, [c, read_carve] { return read_carve(c); });
            }
            if (app.webFoldersFuture.valid() &&
                app.webFoldersFuture.wait_for(0s) == std::future_status::ready) {
                auto r = app.webFoldersFuture.get();
                if (r.ok && !(r.value == app.webFolders)) {
                    app.webFolders = std::move(r.value);
                    app.apply_web_folders(app.sessions);
                    app.mark_session_catalog_changed();
                }
            }
            if (!app.webFolderQueue.empty() && app.client && app.client->supports_web_routes()) {
                for (auto& op : app.webFolderQueue) {
                    AppComponent::WebFolderInFlight w;
                    w.op = op;
                    auto c = app.client;
                    w.future = std::async(std::launch::async, [c, op]() -> api::Result<std::string> {
                        using K = AppComponent::WebFolderOp::Kind;
                        namespace fo = api::folders;
                        const auto check = [](const api::Result<api::Client::WebReply>& r,
                                              const char* fallback) -> std::string {
                            if (!r.ok) return r.error;
                            if (r.value.status != 200) return fo::refusal(r.value.body, fallback);
                            return {};
                        };
                        switch (op.kind) {
                            case K::Create: {
                                auto r = c->web_call("POST", fo::kFoldersPath, fo::name_body(op.name));
                                if (std::string why = check(r, "Could not make that folder."); !why.empty())
                                    return api::Result<std::string>::success(why);
                                const auto made = fo::parse_created(r.value.body);
                                if (!made) return api::Result<std::string>::success("Could not make that folder.");
                                std::vector<std::string> file = op.members;
                                if (!op.session.empty()) file.push_back(op.session);
                                // One write per thread, in sequence (the overlay route takes
                                // one session; no batch on this lane).
                                for (const std::string& sid : file) {
                                    auto f = c->web_call("POST", fo::kOverlayPath, fo::file_body(sid, made->id));
                                    if (std::string why = check(f, "Could not file that thread."); !why.empty())
                                        return api::Result<std::string>::success(why);
                                }
                                return api::Result<std::string>::success(std::string());
                            }
                            case K::Rename:
                                return api::Result<std::string>::success(check(
                                    c->web_call("PATCH", std::string(fo::kFoldersPath) + "/" + op.id,
                                                fo::name_body(op.name)),
                                    "Could not rename that folder."));
                            case K::Delete:
                                return api::Result<std::string>::success(check(
                                    c->web_call("DELETE", std::string(fo::kFoldersPath) + "/" + op.id, ""),
                                    "Could not delete that folder."));
                            case K::File:
                                return api::Result<std::string>::success(check(
                                    c->web_call("POST", fo::kOverlayPath, fo::file_body(op.session, op.id)),
                                    "Could not file that thread."));
                        }
                        return api::Result<std::string>::success(std::string());
                    });
                    app.webFolderOps.push_back(std::move(w));
                }
            }
            app.webFolderQueue.clear();
            for (auto it = app.webFolderOps.begin(); it != app.webFolderOps.end();) {
                if (!it->future.valid() || it->future.wait_for(0s) != std::future_status::ready) {
                    ++it;
                    continue;
                }
                auto r = it->future.get();
                if (!r.value.empty()) {
                    app.raise_toast(r.value, std::string(), AppComponent::ToastUndo::None);
                } else if (it->op.kind == AppComponent::WebFolderOp::Kind::Delete) {
                    app.folderUndo = AppComponent::DeletedFolder{it->op.name, it->op.members};
                    app.raise_toast("Deleted folder \xe2\x80\x9c" + it->op.name + "\xe2\x80\x9d",
                                    "folder-undo", AppComponent::ToastUndo::FolderDelete);
                }
                app.webFoldersAt = -1.0;  // re-read: the server's answer is the sidebar's
                it = app.webFolderOps.erase(it);
            }
        }
        service_link_previews(app);
        // A newer build on disk (D44): a stat every 30 s. HANABI_TEST_UPDATE_READY
        // stands in for a replaced binary in scripted runs.
        {
            const double now = static_cast<double>(std::time(nullptr));
            auto& w = app.updateWatch;
            if (w.path.empty()) {
                w.path = hanabi::update_ready::running_executable();
                w.at_launch = hanabi::update_ready::identity_of(w.path);
            }
            if (app.updateCheckedAt < 0.0 || now - app.updateCheckedAt >= 30.0) {
                app.updateCheckedAt = now;
                hanabi::update_ready::FileId cur = hanabi::update_ready::identity_of(w.path);
                if (const char* t = std::getenv("HANABI_TEST_UPDATE_READY"); t != nullptr && *t == '1') {
                    cur = w.at_launch;
                    cur.mtime_ns += 1;
                }
                app.updatePending = w.pending(cur);
                if (app.updatePending) w.pending_id = cur;
            }
        }
        // A move between Spaces (refile).
        if (!app.requestRefileId.empty()) {
            if (app.client && app.client->supports_refile()) {
                AppComponent::RefileInFlight r;
                r.id = app.requestRefileId;
                r.space = app.requestRefileSpace;
                auto c = app.client;
                const std::string sid = r.id, sp = r.space;
                r.future = std::async(std::launch::async, [c, sid, sp] {
                    return c->refile_session(sid, sp);
                });
                app.refiles.push_back(std::move(r));
            }
            app.requestRefileId.clear();
            app.requestRefileSpace.clear();
        }
        for (auto it = app.refiles.begin(); it != app.refiles.end();) {
            if (!it->future.valid() || it->future.wait_for(0s) != std::future_status::ready) {
                ++it;
                continue;
            }
            auto r = it->future.get();
            if (r.ok) {
                if (it->space.empty())
                    app.sessionSpace.erase(it->id);
                else
                    app.sessionSpace[it->id] = it->space;
                app.apply_space_filing(app.sessions);
                app.mark_session_catalog_changed();
                std::string name = "Personal";
                for (const auto& sp : app.spaces)
                    if (sp.id == it->space) name = sp.name;
                app.raise_toast((it->space.empty() ? std::string("Moved out of its Space")
                                                   : "Moved to " + name),
                                std::string(), AppComponent::ToastUndo::None);
            } else {
                app.raise_toast("Not moved: " + r.error, std::string(),
                                AppComponent::ToastUndo::None);
            }
            it = app.refiles.erase(it);
        }
        // Pins read back from the web, once a minute (kt-if8e).
        {
            auto& ps = app.pinSync;
            const double now = static_cast<double>(std::time(nullptr));
            if (!ps.future.valid() && app.client && app.client->supports_web_routes() &&
                (ps.at < 0.0 || now - ps.at >= 60.0)) {
                ps.at = now;
                const auto& st = Settings::get().get_starred();
                ps.asked = std::set<std::string>(st.begin(), st.end());
                // Retry pins whose write never landed and is not in flight.
                for (const auto& [id, on] : ps.unlanded) {
                    bool flying = false;
                    for (const auto& w : app.overlayWrites)
                        if (w.pin && w.id == id) flying = true;
                    for (const auto& q : app.overlayWriteQueue)
                        if (std::get<0>(q) && std::get<1>(q) == id) flying = true;
                    if (!flying) app.overlayWriteQueue.emplace_back(true, id, on);
                }
                auto c = app.client;
                ps.future = std::async(std::launch::async, [c] {
                    api::pins::Read r;
                    auto o = c->web_call("GET", api::pins::kOverlayPath, "");
                    if (o.ok && o.value.status == 200) r.overlays = api::pins::parse_overlays(o.value.body);
                    r.status = o.ok ? o.value.status : 0;
                    r.reach = api::pins::reach_of(o.ok, o.error, r.status, r.overlays.has_value());
                    auto p = c->web_call("GET", api::pins::kPreferencesPath, "");
                    if (p.ok && p.value.status == 200) r.order = api::pins::parse_order(p.value.body);
                    return r;
                });
            }
            if (ps.future.valid() && ps.future.wait_for(0s) == std::future_status::ready) {
                api::pins::Read r = ps.future.get();
                bool changed = false;
                ps.reach = r.reach;
                ps.reachStatus = r.status;
                // The one-shot carry of this Mac's pins to the web's column,
                // on the first synced read and before the reconcile (kt-ubwg):
                // queued as pin writes that have not landed, so the reconcile
                // keeps them and a refused one is retried with the rest.
                if (r.overlays && !ps.migrationAsked &&
                    Settings::get().get_pins_migrated_version() < api::pins::kPinsMigrationVersion) {
                    ps.migrationAsked = true;
                    const auto missing =
                        api::pins::migration_ids(Settings::get().get_starred(), r.overlays->pinned,
                                                 r.overlays->known);
                    if (missing.empty()) {
                        Settings::get().set_pins_migrated_version(api::pins::kPinsMigrationVersion);
                    } else {
                        for (const auto& id : missing) {
                            ps.migrating.insert(id);
                            ps.unlanded[id] = true;
                            app.overlayWriteQueue.emplace_back(true, id, true);
                        }
                    }
                }
                if (r.overlays) {
                    const std::vector<std::string> current = Settings::get().get_starred();
                    const auto rec = api::pins::reconcile(current, ps.asked, r.overlays->pinned,
                                                          ps.serverKnown, r.overlays->known,
                                                          ps.unlanded);
                    for (const auto& id : rec.dropped) {
                        app.apply_starred(id, false);
                        Settings::get().set_starred(id, false);
                        changed = true;
                    }
                    for (const auto& id : rec.arrived) {
                        app.apply_starred(id, true);
                        Settings::get().set_starred(id, true);
                        changed = true;
                    }
                    ps.serverKnown = rec.server_known;
                    ps.localOnly = rec.local_only.size();
                }
                if (r.order) {
                    ps.orderRead = true;
                    if (!ps.orderPending) ps.order = *r.order;
                }
                if ((changed || r.order) && ps.orderRead && !ps.order.empty()) {
                    const auto next = api::pins::arranged(Settings::get().get_starred(), ps.order);
                    auto it = app.rowOrder.find("pinned");
                    if (it == app.rowOrder.end() || it->second != next) {
                        app.rowOrder["pinned"] = next;
                        ++app.rowOrderRevision;
                        Settings::get().set_row_order("pinned", next);
                    }
                }
            }
            if (ps.orderPending && !ps.orderFuture.valid() && app.client &&
                app.client->supports_web_routes()) {
                auto c = app.client;
                const std::string body = api::pins::order_body(*ps.orderPending);
                ps.orderPending.reset();
                ps.orderFuture = std::async(std::launch::async, [c, body]() -> api::Result<bool> {
                    auto r = c->web_call("PUT", api::pins::kPreferencesPath, body);
                    if (!r.ok) return api::Result<bool>::failure(r.error);
                    if (r.value.status != 200)
                        return api::Result<bool>::failure("the web app answered " +
                                                          std::to_string(r.value.status));
                    return api::Result<bool>::success(true);
                });
            }
            if (ps.orderFuture.valid() && ps.orderFuture.wait_for(0s) == std::future_status::ready) {
                auto r = ps.orderFuture.get();
                if (!r.ok)
                    app.raise_toast("Pin order kept on this Mac; the web app did not take it (" +
                                        r.error + ")",
                                    std::string(), AppComponent::ToastUndo::None);
            }
        }
        // Pins and archives on their way to the server (kt-if8e).
        if (!app.overlayWriteQueue.empty() && app.client && app.client->supports_overlay_writes()) {
            const auto archive_in_flight = [&app](const std::string& sid) {
                for (const auto& w : app.overlayWrites)
                    if (!w.pin && w.id == sid) return true;
                return false;
            };
            for (auto& [pin, id, on] : app.overlayWriteQueue) {
                std::optional<bool> prior;
                if (!pin) {
                    if (const auto p = app.archivePrior.find(id); p != app.archivePrior.end()) {
                        prior = p->second;
                        app.archivePrior.erase(p);
                    }
                    if (archive_in_flight(id)) {
                        // Wait; a newer intent replaces an older waiting one
                        // and keeps the OLDEST prior (what to restore to).
                        auto it = app.archiveWaiting.find(id);
                        if (it == app.archiveWaiting.end())
                            app.archiveWaiting[id] = {on, prior};
                        else
                            it->second.first = on;
                        continue;
                    }
                }
                AppComponent::OverlayWrite w;
                w.pin = pin;
                w.id = id;
                w.on = on;
                w.prior = prior;
                auto c = app.client;
                const bool isPin = pin;
                const std::string sid = id;
                const bool value = on;
                w.future = std::async(std::launch::async, [c, isPin, sid, value] {
                    return isPin ? c->set_pinned(sid, value) : c->set_archived(sid, value);
                });
                app.overlayWrites.push_back(std::move(w));
            }
        }
        app.overlayWriteQueue.clear();
        for (auto it = app.overlayWrites.begin(); it != app.overlayWrites.end();) {
            if (!it->future.valid() || it->future.wait_for(0s) != std::future_status::ready) {
                ++it;
                continue;
            }
            auto r = it->future.get();
            if (!it->pin) {
                const std::string sid = it->id;
                const bool on = it->on;
                const auto waiting = app.archiveWaiting.find(sid);
                if (r.ok && waiting == app.archiveWaiting.end()) {
                    // Landed and nothing newer is waiting: the server's state
                    // rules from here, so a later archive or unarchive on the
                    // web reaches this thread on the next list fetch (the
                    // reference's D123051693). The row carries the stamp until
                    // that fetch says otherwise.
                    for (auto& s : app.sessions)
                        if (s.id == sid) s.server_archived_at_ms = on ? 1 : 0;
                    for (Pane& p : app.panes)
                        if (p.openSession && p.openSession->summary.id == sid)
                            p.openSession->summary.server_archived_at_ms = on ? 1 : 0;
                    app.apply_archived_override(sid, std::nullopt);
                    Settings::get().clear_archived(sid);
                } else if (!r.ok && waiting == app.archiveWaiting.end()) {
                    // Refused, nothing newer waiting: the overlay goes back to
                    // what it was before the gesture (none for a thread only
                    // the web archived) -- not a stale archive left standing.
                    app.apply_archived_override(sid, it->prior);
                    if (it->prior) Settings::get().set_archived(sid, *it->prior);
                    else Settings::get().clear_archived(sid);
                }
                if (waiting != app.archiveWaiting.end()) {
                    // The newer intent goes now (if it still differs from
                    // what landed); its prior is the older one's.
                    const bool next = waiting->second.first;
                    const std::optional<bool> prior = it->prior ? it->prior : waiting->second.second;
                    app.archiveWaiting.erase(waiting);
                    if (!r.ok || next != on) {
                        app.archivePrior[sid] = prior;
                        app.overlayWriteQueue.emplace_back(false, sid, next);
                    } else {
                        for (auto& s : app.sessions)
                            if (s.id == sid) s.server_archived_at_ms = on ? 1 : 0;
                        app.apply_archived_override(sid, std::nullopt);
                        Settings::get().clear_archived(sid);
                    }
                }
            }
            if (it->pin) {
                auto& ps = app.pinSync;
                if (r.ok) {
                    if (const auto u = ps.unlanded.find(it->id);
                        u != ps.unlanded.end() && u->second == it->on)
                        ps.unlanded.erase(u);
                    if (it->on) ps.serverKnown.insert(it->id);
                    else ps.serverKnown.erase(it->id);
                    ps.failToasted.erase(it->id);
                    if (ps.localOnly > 0 && it->on) --ps.localOnly;  // the web has it now
                } else if (ps.migrating.count(it->id) != 0) {
                    ps.migrationFailed = true;
                }
                // The carry is done when every pin it wrote has answered; it
                // is MARKED done only when every one landed.
                if (ps.migrating.erase(it->id) != 0 && ps.migrating.empty()) {
                    if (!ps.migrationFailed)
                        Settings::get().set_pins_migrated_version(api::pins::kPinsMigrationVersion);
                    else
                        std::fprintf(stderr, "[pins] the carry of this Mac's pins to the web did not all "
                                             "land; not marked done, it runs again next launch\n");
                }
            }
            // A refused pin is retried on the next poll; it says so once. A
            // refused archive was put back above, and says so.
            if (!r.ok && it->pin && app.pinSync.failToasted.insert(it->id).second)
                app.raise_toast(std::string(it->on ? "Pinned" : "Unpinned") +
                                    " on this Mac; the server did not take it (" + r.error + ")",
                                std::string(), AppComponent::ToastUndo::None);
            if (!r.ok && !it->pin)
                app.raise_toast(std::string(it->on ? "Not archived" : "Not unarchived") +
                                    ": the server did not take it (" + r.error + ")",
                                std::string(), AppComponent::ToastUndo::None);
            it = app.overlayWrites.erase(it);
        }
        // The Sensitive-mode gate: asked once; anything but a clear admission
        // is a refusal (the control is then not offered at all).
        if (!app.sensitiveGateAsked && app.client && app.client->supports_graphql()) {
            app.sensitiveGateAsked = true;
            auto c = app.client;
            app.sensitiveGateFuture = std::async(std::launch::async, [c] {
                return c->graphql(api::companion::sensitive_gate_body());
            });
        }
        if (app.sensitiveGateFuture.valid() &&
            app.sensitiveGateFuture.wait_for(0s) == std::future_status::ready) {
            auto r = app.sensitiveGateFuture.get();
            app.sensitiveGate = r.ok && api::companion::sensitive_gate_admits(r.value)
                                    ? AppComponent::Gate::Admitted
                                    : AppComponent::Gate::Denied;
        }
        // The session->Space index: a bounded paged walk off the frame, every
        // ten minutes; a failed walk leaves the last filing in place.
        {
            const double now = static_cast<double>(std::time(nullptr));
            if (!app.spaceIndexFuture.valid() && app.client && app.client->supports_graphql() &&
                (app.spaceIndexAt < 0.0 || now - app.spaceIndexAt >= 600.0)) {
                app.spaceIndexAt = now;
                auto c = app.client;
                app.spaceIndexFuture = std::async(std::launch::async, [c] {
                    using Filed = std::vector<std::pair<std::string, std::string>>;
                    Filed all;
                    std::string after;
                    for (int page = 0; page < api::spaces::kIndexMaxPages; ++page) {
                        auto r = c->graphql(api::spaces::index_body(after));
                        if (!r.ok)
                            return page == 0 ? api::Result<Filed>::failure(r.error)
                                             : api::Result<Filed>::success(std::move(all));
                        auto p = api::spaces::parse_index(r.value);
                        all.insert(all.end(), p.filed.begin(), p.filed.end());
                        if (p.next.empty()) break;
                        after = p.next;
                    }
                    return api::Result<Filed>::success(std::move(all));
                });
            }
            if (app.spaceIndexFuture.valid() &&
                app.spaceIndexFuture.wait_for(0s) == std::future_status::ready) {
                auto r = app.spaceIndexFuture.get();
                if (r.ok) {
                    std::unordered_map<std::string, std::string> next(r.value.begin(), r.value.end());
                    if (next != app.sessionSpace) {
                        app.sessionSpace = std::move(next);
                        app.apply_space_filing(app.sessions);
                        app.mark_session_catalog_changed();
                    }
                }
            }
        }
        if (app.spacesFuture.valid() &&
            app.spacesFuture.wait_for(0s) == std::future_status::ready) {
            auto r = app.spacesFuture.get();
            if (r.ok) {
                app.spaces = std::move(r.value);
                ++app.spacesRevision;
            }
        }
    }
};

}  // namespace ecs
