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
                                if (!op.session.empty()) {
                                    auto f = c->web_call("POST", fo::kOverlayPath, fo::file_body(op.session, made->id));
                                    return api::Result<std::string>::success(check(f, "Could not file that thread."));
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
                if (!r.value.empty())
                    app.raise_toast(r.value, std::string(), AppComponent::ToastUndo::None);
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
            if (!r.ok)
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
