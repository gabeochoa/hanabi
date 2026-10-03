#pragma once

#include <ctime>

// Services Settings > Memory's requests (memory_page.h) once a frame.

#include "components.h"
#include "memory_page.h"

namespace ecs {

struct MemorySystem : afterhours::System<AppComponent> {
    void for_each_with(afterhours::Entity&, AppComponent& app, float) override {
        service_memory(app.memory, app.client);
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
                    auto p = c->web_call("GET", api::pins::kPreferencesPath, "");
                    if (p.ok && p.value.status == 200) r.order = api::pins::parse_order(p.value.body);
                    return r;
                });
            }
            if (ps.future.valid() && ps.future.wait_for(0s) == std::future_status::ready) {
                api::pins::Read r = ps.future.get();
                bool changed = false;
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
            for (auto& [pin, id, on] : app.overlayWriteQueue) {
                AppComponent::OverlayWrite w;
                w.pin = pin;
                w.id = id;
                w.on = on;
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
            if (it->pin) {
                auto& ps = app.pinSync;
                if (r.ok) {
                    if (const auto u = ps.unlanded.find(it->id);
                        u != ps.unlanded.end() && u->second == it->on)
                        ps.unlanded.erase(u);
                    if (it->on) ps.serverKnown.insert(it->id);
                    else ps.serverKnown.erase(it->id);
                    ps.failToasted.erase(it->id);
                }
            }
            // A refused pin is retried on the next poll; it says so once.
            const bool sayIt = !r.ok && (!it->pin || app.pinSync.failToasted.insert(it->id).second);
            if (sayIt)
                app.raise_toast(std::string(it->pin ? (it->on ? "Pinned" : "Unpinned")
                                                    : (it->on ? "Archived" : "Unarchived")) +
                                    " on this Mac; the server did not take it (" + r.error + ")",
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
