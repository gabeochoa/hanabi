#pragma once
#include <algorithm>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "../api/folders_wire.h"
#include "../keys.h"
#include "../settings.h"
#include "../ui/bulk_select.h"
#include "../ui/read_cursor.h"
#include "components.h"
#include "keyboard_focus.h"
#include "thread_model.h"
#include "ui_imports.h"

namespace ecs {

struct BulkSelectSystem : afterhours::System<UIContext<InputAction>> {
    using Action = hanabi::bulk::Action;
    using Outcome = hanabi::bulk::Outcome;
    using Phase = hanabi::bulk::Phase;

    static Outcome classify(const std::string& error) {
        const bool afterSend = error.find("Read") != std::string::npos ||
                               error.find("read timeout") != std::string::npos ||
                               error.find("timed out") != std::string::npos;
        return afterSend ? Outcome::unknown(error) : Outcome::failed(error);
    }

    static Outcome perform(const std::shared_ptr<api::Client>& c, const Action& a, const std::string& id) {
        namespace fo = api::folders;
        switch (a.kind) {
            case Action::Kind::Archive:
            case Action::Kind::Unarchive: {
                const auto r = c->set_archived(id, a.kind == Action::Kind::Archive);
                return r.ok ? Outcome::succeeded() : classify(r.error);
            }
            case Action::Kind::Move: {
                const auto r = c->web_call("POST", fo::kOverlayPath, fo::file_body(id, a.folder));
                if (!r.ok) return classify(r.error);
                if (r.value.status != 200)
                    return Outcome::failed(fo::refusal(r.value.body, "Could not file that thread."));
                return Outcome::succeeded();
            }
            case Action::Kind::MarkRead: {
                const auto r = c->write_read(id);
                return r.ok ? Outcome::succeeded() : classify(r.error);
            }
            case Action::Kind::MarkUnread: {
                const auto r = c->write_unread(id);
                return r.ok ? Outcome::succeeded() : classify(r.error);
            }
        }
        return Outcome::failed("unknown action");
    }

    static const api::SessionSummary* summary_of(const AppComponent& app, const std::string& id) {
        for (const auto& s : app.sessions)
            if (s.id == id) return &s;
        return nullptr;
    }

    static bool already_done(const AppComponent& app, const Action& a, const std::string& id) {
        const api::SessionSummary* s = summary_of(app, id);
        if (s == nullptr) return false;
        switch (a.kind) {
            case Action::Kind::Archive: return model::is_archived(*s);
            case Action::Kind::Unarchive: return !model::is_archived(*s);
            case Action::Kind::Move:
                return a.folder.empty() ? s->web_folder.empty() : s->web_folder == "folder:" + a.folder;
            case Action::Kind::MarkRead:
                return hanabi::read_cursor::unread_count(s->last_seq, Settings::get().get_read_seq(id)) == 0;
            case Action::Kind::MarkUnread:
                return hanabi::read_cursor::unread_count(s->last_seq, Settings::get().get_read_seq(id)) > 0;
        }
        return false;
    }

    static void apply_locally(AppComponent& app, const Action& a, const std::string& id) {
        const api::SessionSummary* s = summary_of(app, id);
        switch (a.kind) {
            case Action::Kind::Archive:
            case Action::Kind::Unarchive: {
                const bool on = a.kind == Action::Kind::Archive;
                for (auto& row : app.sessions)
                    if (row.id == id) row.server_archived_at_ms = on ? 1 : 0;
                for (Pane& p : app.panes)
                    if (p.openSession && p.openSession->summary.id == id)
                        p.openSession->summary.server_archived_at_ms = on ? 1 : 0;
                app.apply_archived_override(id, std::nullopt);
                Settings::get().clear_archived(id);
                break;
            }
            case Action::Kind::Move:
                for (auto& row : app.sessions)
                    if (row.id == id) row.web_folder = a.folder.empty() ? std::string() : "folder:" + a.folder;
                app.mark_session_catalog_changed();
                break;
            case Action::Kind::MarkRead:
                if (s != nullptr && s->last_seq > 0) {
                    Settings::get().set_read_seq(id, s->last_seq);
                    app.readMirrored[id] = s->last_seq;
                }
                break;
            case Action::Kind::MarkUnread:
                if (s != nullptr && s->last_seq > 0) {
                    Settings::get().set_read_seq(id, std::max<std::int64_t>(1, s->last_seq - 1));
                    app.readMirrored.erase(id);
                }
                break;
        }
    }

    static bool list_has_the_keyboard(const AppComponent& app) {
        return !app.rowMenuOpen && !app.messageMenuOpen && !app.paletteOpen && !app.sessionSearchOpen &&
               !any_text_field_focused() && !app.renameOpen && !app.showShortcuts && !app.showBugReport &&
               !app.markupOpen && !app.companion.open && !app.showSettings && !app.showAuth &&
               app.viewerImagePath.empty();
    }

    void end(AppComponent::Bulk& b) {
        b.pending.clear();
        b.inflight.clear();
        b.phase = Phase::Idle;
        b.action.reset();
        b.selecting = false;
        b.selected.clear();
        b.anchor.clear();
        b.outcomes.clear();
        b.moveMenuOpen = false;
        b.request.reset();
        b.requestRecheck = false;
        b.requestEnd = false;
    }

    void start(AppComponent& app, const Action& a, std::vector<std::string> ids) {
        auto& b = app.bulk;
        b.action = a;
        b.phase = Phase::Running;
        for (const std::string& id : ids) b.outcomes.erase(id);
        b.pending.assign(ids.begin(), ids.end());
    }

    void settle(AppComponent& app) {
        auto& b = app.bulk;
        const Action a = *b.action;
        b.selected = hanabi::bulk::remaining(b.selected, b.outcomes);
        b.phase = Phase::Idle;
        if (a.kind == Action::Kind::Move) app.webFoldersAt = -1.0;
        if (a.kind == Action::Kind::Archive) {
            std::vector<std::string> filed;
            for (const auto& [id, o] : b.outcomes)
                if (o.kind == Outcome::Kind::Succeeded) filed.push_back(id);
            if (!filed.empty()) {
                b.undoIds = filed;
                app.raise_toast(hanabi::bulk::archived_toast(static_cast<int>(filed.size())), "bulk-archive",
                                AppComponent::ToastUndo::BulkArchive);
            }
        } else if (a.kind == Action::Kind::Unarchive) {
            b.undoIds.clear();
        }
        b.action.reset();
    }

    void for_each_with(Entity&, UIContext<InputAction>&, float) override {
        auto* app = find_singleton<AppComponent>();
        if (!app) return;
        auto& b = app->bulk;
        if (b.selecting && hanabi::keys::pressed(hanabi::keys::kEscape) && list_has_the_keyboard(*app) &&
            !b.moveMenuOpen)
            b.requestEnd = true;
        if (b.requestEnd) {
            end(b);
            return;
        }
        if (b.request && b.phase == Phase::Idle) {
            const Action a = *b.request;
            b.request.reset();
            std::vector<std::string> ids;
            if (a.kind == Action::Kind::Unarchive) {
                ids = b.undoIds;
                b.outcomes.clear();
            } else {
                ids = hanabi::bulk::actionable(b.selected, app->bulkVisibleOrder);
            }
            if (!ids.empty() && app->client) start(*app, a, std::move(ids));
        }
        if (b.requestRecheck && b.phase == Phase::Idle && b.action == std::nullopt) {
            b.requestRecheck = false;
            const auto unresolved = hanabi::bulk::unresolved(b.outcomes, b.selected);
            if (!unresolved.empty()) {
                b.phase = Phase::Reconciling;
                app->requestListRefresh = true;
                recheckFramesLeft_ = 180;
                recheckIds_.assign(unresolved.begin(), unresolved.end());
            }
        }
        if (b.phase == Phase::Reconciling) {
            --recheckFramesLeft_;
            const bool listLanded = !app->listFuture.valid() && !app->requestListRefresh;
            if (listLanded || recheckFramesLeft_ <= 0) {
                for (const std::string& id : recheckIds_) {
                    const auto it = b.outcomes.find(id);
                    if (it == b.outcomes.end()) continue;
                    const Action wanted = lastAction_;
                    if (already_done(*app, wanted, id)) it->second = Outcome::succeeded();
                    else if (listLanded) it->second = Outcome::failed("not applied");
                }
                recheckIds_.clear();
                b.selected = hanabi::bulk::remaining(b.selected, b.outcomes);
                b.phase = Phase::Idle;
            }
            return;
        }
        if (b.phase != Phase::Running || !b.action) return;
        lastAction_ = *b.action;
        while (b.inflight.size() < static_cast<std::size_t>(AppComponent::Bulk::kWidth) && !b.pending.empty()) {
            const std::string id = b.pending.front();
            b.pending.pop_front();
            if (already_done(*app, *b.action, id)) {
                b.outcomes[id] = Outcome::unchanged();
                continue;
            }
            AppComponent::Bulk::InFlight f;
            f.id = id;
            std::shared_ptr<api::Client> c = app->client;
            const Action a = *b.action;
            f.future = std::async(std::launch::async, [c, a, id] { return perform(c, a, id); });
            b.inflight.push_back(std::move(f));
        }
        for (auto it = b.inflight.begin(); it != b.inflight.end();) {
            if (!it->future.valid() || it->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                ++it;
                continue;
            }
            const Outcome o = it->future.get();
            if (o.kind == Outcome::Kind::Succeeded) apply_locally(*app, *b.action, it->id);
            b.outcomes[it->id] = o;
            it = b.inflight.erase(it);
        }
        if (b.pending.empty() && b.inflight.empty()) settle(*app);
    }

    int recheckFramesLeft_ = 0;
    std::vector<std::string> recheckIds_;
    Action lastAction_;
};

}  // namespace ecs
