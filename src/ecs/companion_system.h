#pragma once

// The Companion (the reference's Companion window, the parts this build
// carries): a Phabricator diff or a task, read in-app from a D/T link.
//
//   A diff: its title, status, repo and version, its files with their line
//   counts; a file opens to EVERY comparison hunk drawn as one unified diff --
//   context, removals and additions interleaved (the reference's 0.8.9).
//   A task: Overview (status, priority, owner, tags, description) and
//   Comments (the newest ten, author, when, text -- the reference's 0.8.9),
//   the comments in their own read so a miss fails that page, not the card.
//
// Reads go over the client's GraphQL route off the frame (api/companion_wire
// .h); a read that lands for an entity the reader has left is dropped. Open
// in browser is one click away; Escape closes.

#include <chrono>
#include <future>
#include <string>
#include <variant>

#include "../api/companion_wire.h"
#include "../api/session_changes.h"
#include "../ui/link_detect.h"
#include "../ui/overlay_lifecycle.h"
#include "../ui/secondary_surface.h"
#include "../util/format.h"
#include "components.h"
#include "ui_imports.h"

namespace ecs {

struct CompanionSystem : afterhours::System<UIContext<InputAction>> {
    static constexpr float kPanelW = 860.0f;
    static constexpr float kPanelH = 600.0f;
    static constexpr int kMaxLines = 22;

    static void service(AppComponent& app) {
        using namespace std::chrono_literals;
        CompanionState& c = app.companion;
        const auto ready = [](auto& f) {
            return f.valid() && f.wait_for(0s) == std::future_status::ready;
        };
        if (ready(c.docFuture)) {
            auto r = c.docFuture.get();
            if (c.docGen == c.generation) {
                if (!r.ok) {
                    c.error = "Couldn't read this right now. Try again, or open it in your browser.";
                } else if (c.kind == 'D') {
                    auto p = api::companion::parse_diff(r.value, c.number);
                    if (auto* d = std::get_if<api::companion::Diff>(&p)) c.diff = std::move(*d);
                    else c.error = std::get<std::string>(p);
                } else {
                    auto p = api::companion::parse_task(r.value, c.number);
                    if (auto* t = std::get_if<api::companion::Task>(&p)) c.task = std::move(*t);
                    else c.error = std::get<std::string>(p);
                }
            }
        }
        if (ready(c.fileFuture)) {
            auto r = c.fileFuture.get();
            if (c.fileGen == c.generation && c.diff && c.selectedFile >= 0 &&
                c.selectedFile < static_cast<int>(c.diff->files.size())) {
                if (!r.ok) {
                    c.fileError = "Couldn't read this file right now. Try again.";
                } else {
                    auto p = api::companion::parse_file(
                        r.value, c.diff->versionId,
                        c.diff->files[static_cast<std::size_t>(c.selectedFile)]);
                    if (auto* h = std::get_if<std::vector<api::companion::Hunk>>(&p))
                        c.hunks = std::move(*h);
                    else
                        c.fileError = std::get<std::string>(p);
                }
            }
        }
        if (ready(c.commentsFuture)) {
            auto r = c.commentsFuture.get();
            if (c.commentsGen == c.generation) {
                if (!r.ok) {
                    c.commentsError = "This task's comments are unavailable right now.";
                } else {
                    auto p = api::companion::parse_comments(r.value, c.number);
                    if (auto* v = std::get_if<std::vector<api::companion::Comment>>(&p))
                        c.comments = std::move(*v);
                    else
                        c.commentsError = std::get<std::string>(p);
                }
            }
        }
        if (!app.client) return;
        auto client = app.client;
        const std::int64_t n = std::atoll(c.number.c_str());
        if (c.requestLoad) {
            c.requestLoad = false;
            c.docGen = c.generation;
            const std::string body =
                c.kind == 'D' ? api::companion::diff_body(n) : api::companion::task_body(n);
            c.docFuture = std::async(std::launch::async, [client, body] { return client->graphql(body); });
        }
        if (c.requestFile >= 0 && c.diff && c.requestFile < static_cast<int>(c.diff->files.size())) {
            c.selectedFile = c.requestFile;
            c.requestFile = -1;
            c.hunks.reset();
            c.fileError.clear();
            c.fileGen = c.generation;
            const std::string body = api::companion::file_body(
                c.diff->versionId, c.diff->files[static_cast<std::size_t>(c.selectedFile)].path);
            c.fileFuture = std::async(std::launch::async, [client, body] { return client->graphql(body); });
        }
        if (c.requestComments && !c.commentsAsked) {
            c.requestComments = false;
            c.commentsAsked = true;
            c.commentsGen = c.generation;
            const std::string body = api::companion::comments_body(n);
            c.commentsFuture =
                std::async(std::launch::async, [client, body] { return client->graphql(body); });
        }
    }

    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* app = find_singleton<AppComponent>();
        if (app == nullptr) return;
        service(*app);
        CompanionState& c = app->companion;
        if (!c.open) return;
        if (app->escape == EscapeIntent::CloseCompanion) {
            c.open = false;
            return;
        }
        ctx.theme.background = theme::panel_bg();
        Entity& uiRoot = ui_imm::getUIRootEntity();
        const auto rect = hanabi::surface::centered(hanabi::viewport::width(),
                                                    hanabi::viewport::height(), kPanelW, kPanelH);
        const float w = rect.width - hanabi::surface::kSheetPadH * 2.0f;
        auto backdrop = button(ctx, mk(uiRoot, 8500),
            hanabi::surface::scrim(hanabi::viewport::width(), hanabi::viewport::height(), 10)
                .with_debug_name("companion_backdrop"));
        if (hanabi::overlay::dismisses(static_cast<bool>(backdrop), ctx.mouse.pos.x,
                                       ctx.mouse.pos.y, rect)) {
            c.open = false;
            return;
        }
        auto panel = div(ctx, mk(uiRoot, 8510),
                         hanabi::surface::sheet(rect, 11).with_debug_name("companion_panel"));
        const auto line = [&](Entity& at, int id, const std::string& words, float h,
                              theme::Color ink, float size, const std::string& dbg,
                              float width = -1.0f) {
            div(ctx, mk(at, id),
                ComponentConfig{}
                    .with_label(words.empty() ? std::string(" ") : words)
                    .with_size(ComponentSize{pixels(width < 0 ? w : width), pixels(h)})
                    .with_transparent_bg()
                    .with_custom_text_color(ink)
                    .with_font_size(size)
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(TextOverflow::Ellipsis)
                    .with_roundness(0.0f)
                    .with_render_layer(11)
                    .with_debug_name(dbg));
        };
        const auto chip = [&](Entity& at, int id, const std::string& label, bool on, float bw,
                              const std::string& dbg) {
            return button(ctx, mk(at, id),
                ComponentConfig{}
                    .with_label(label)
                    .with_size(ComponentSize{pixels(bw), pixels(26)})
                    .with_margin(Margin{.right = pixels(8)})
                    .with_custom_background(on ? theme::accent() : theme::panel_bg_2())
                    .with_custom_text_color(on ? theme::Color{255, 255, 255, 255}
                                               : theme::text_primary())
                    .with_border(theme::border_raised(), pixels(1.0f))
                    .with_font_size(theme::type::SM)
                    .with_corner_radius(6.0f)
                    .with_cursor(afterhours::ui::CursorType::Pointer)
                    .with_render_layer(11)
                    .with_debug_name(dbg));
        };
        const auto row = [&](int id, const std::string& dbg, float h = 32.0f) {
            return div(ctx, mk(panel.ent(), id),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(w), pixels(h)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_align_items(AlignItems::Center)
                    .with_transparent_bg()
                    .with_render_layer(11)
                    .with_debug_name(dbg));
        };

        // Header: the entity, and the way out to the browser.
        const std::string ref = std::string(1, c.kind) + c.number;
        auto head = row(1, "companion_head", 34.0f);
        line(head.ent(), 1, ref, 30.0f, theme::text_primary(), theme::type::H1, "companion_ref",
             w - 260.0f);
        if (chip(head.ent(), 2, "Open in browser", false, 140, "companion_open_web")) {
            const std::string url = hanabi::links::url_for(app->trackerBaseUrl, ref);
            if (!url.empty()) hanabi::links::open(url);
        }
        if (chip(head.ent(), 3, "Close", false, 80, "companion_close")) {
            c.open = false;
            return;
        }

        if (!c.error.empty()) {
            line(panel.ent(), 2, c.error, 22.0f, theme::destructive(), theme::type::SM,
                 "companion_error");
            return;
        }
        if (c.kind == 'D') {
            if (!c.diff) {
                line(panel.ent(), 2, "Reading the diff\xe2\x80\xa6", 22.0f, theme::text_secondary(),
                     theme::type::SM, "companion_loading");
                return;
            }
            const auto& d = *c.diff;
            line(panel.ent(), 2, d.title, 24.0f, theme::text_primary(), theme::type::BODY,
                 "companion_title");
            std::string meta = d.status;
            if (!d.repo.empty()) meta += (meta.empty() ? "" : "  \xc2\xb7  ") + d.repo;
            if (!d.versionLabel.empty()) meta += "  \xc2\xb7  " + d.versionLabel;
            meta += "  \xc2\xb7  " + std::to_string(d.totalFiles) +
                    (d.totalFiles == 1 ? " file" : " files");
            line(panel.ent(), 3, meta, 20.0f, theme::text_secondary(), theme::type::SM,
                 "companion_meta");
            // The files: each a chip-row; the open one is drawn selected.
            for (std::size_t i = 0; i < d.files.size() && i < 8; ++i) {
                const auto& f = d.files[i];
                std::string label = f.path + "   " + api::changes::counts(f.added, f.deleted);
                if (f.change == "TYPE_ADD") label += "   new";
                if (f.change == "TYPE_DELETE") label += "   deleted";
                auto b = button(ctx, mk(panel.ent(), 10 + static_cast<int>(i)),
                    hanabi::surface::option_row(w, 24.0f,
                                                static_cast<int>(i) == c.selectedFile, 11)
                        .with_label(label)
                        .with_font_size(theme::type::SM)
                        .with_alignment(TextAlignment::Left)
                        .with_text_overflow(TextOverflow::Ellipsis)
                        .with_render_layer(11)
                        .with_debug_name("companion_file_" + std::to_string(i)));
                if (b) c.requestFile = static_cast<int>(i);
            }
            if (d.files.size() > 8)
                line(panel.ent(), 19, std::to_string(d.files.size() - 8) + " more files", 18.0f,
                     theme::text_faint(), theme::type::SM, "companion_more_files");
            if (c.selectedFile < 0) {
                line(panel.ent(), 20, "Pick a file to read its changes.", 20.0f,
                     theme::text_faint(), theme::type::SM, "companion_pick");
                return;
            }
            if (!c.fileError.empty()) {
                line(panel.ent(), 20, c.fileError, 20.0f, theme::destructive(), theme::type::SM,
                     "companion_file_error");
                return;
            }
            if (!c.hunks) {
                line(panel.ent(), 20, "Reading the file\xe2\x80\xa6", 20.0f, theme::text_secondary(),
                     theme::type::SM, "companion_file_loading");
                return;
            }
            // EVERY hunk, one unified diff: a range header, then context,
            // removals and additions interleaved.
            int drawn = 0, id = 100, total = 0;
            for (const auto& h : *c.hunks) {
                const auto lines = api::changes::diff_lines(h.oldText, h.newText);
                total += 1 + static_cast<int>(lines.size());
                if (drawn >= kMaxLines) continue;
                const int hunkId = id++;
                line(panel.ent(), hunkId,
                     "@@ lines " + std::to_string(h.newOffset) + "\xe2\x80\x93" +
                         std::to_string(h.newOffset + std::max(0, h.newLength - 1)) + " @@",
                     17.0f, theme::text_faint(), theme::type::SM,
                     "companion_hunk_" + std::to_string(hunkId));
                ++drawn;
                for (const auto& l : lines) {
                    if (drawn >= kMaxLines) break;
                    const char* sign = l.side == api::changes::Side::Added
                                           ? "+ "
                                           : (l.side == api::changes::Side::Removed ? "\xe2\x88\x92 " : "  ");
                    const theme::Color ink = l.side == api::changes::Side::Added
                                                 ? theme::status_review()
                                                 : (l.side == api::changes::Side::Removed
                                                        ? theme::status_blocked()
                                                        : theme::text_primary());
                    line(panel.ent(), id++, sign + l.text, 17.0f, ink, theme::type::SM,
                         "companion_line_" + std::to_string(drawn));
                    ++drawn;
                }
            }
            if (total > drawn)
                line(panel.ent(), id++, std::to_string(total - drawn) +
                                            " more lines \xe2\x80\x94 open it in your browser",
                     18.0f, theme::text_faint(), theme::type::SM, "companion_more_lines");
            return;
        }

        // A task.
        if (!c.task) {
            line(panel.ent(), 2, "Reading the task\xe2\x80\xa6", 22.0f, theme::text_secondary(),
                 theme::type::SM, "companion_loading");
            return;
        }
        const auto& t = *c.task;
        line(panel.ent(), 2, t.title, 24.0f, theme::text_primary(), theme::type::BODY,
             "companion_title");
        auto tabs = row(3, "companion_pages");
        if (chip(tabs.ent(), 1, "Overview", c.page == 0, 110, "companion_page_overview"))
            c.page = 0;
        if (chip(tabs.ent(), 2, "Comments", c.page == 1, 110, "companion_page_comments")) {
            c.page = 1;
            c.requestComments = true;
        }
        if (c.page == 0) {
            std::string meta = t.status;
            if (!t.priority.empty()) meta += "  \xc2\xb7  priority " + t.priority;
            if (!t.owner.empty()) meta += "  \xc2\xb7  owner " + t.owner;
            line(panel.ent(), 4, meta, 20.0f, theme::text_secondary(), theme::type::SM,
                 "companion_meta");
            std::string when;
            if (t.updated > 0) when = "Updated " + fmtutil::relative_time(t.updated) + " ago";
            if (!t.creator.empty()) when += (when.empty() ? "" : "  \xc2\xb7  ") + ("filed by " + t.creator);
            if (!when.empty())
                line(panel.ent(), 5, when, 20.0f, theme::text_faint(), theme::type::SM,
                     "companion_when");
            if (!t.tags.empty()) {
                std::string tags;
                for (const auto& g : t.tags) tags += (tags.empty() ? "" : ", ") + g;
                line(panel.ent(), 6, "Tags: " + tags, 20.0f, theme::text_secondary(),
                     theme::type::SM, "companion_tags");
            }
            int id = 20;
            const auto paras = api::changes::split_lines(t.description);
            for (std::size_t i = 0; i < paras.size() && i < 14; ++i)
                line(panel.ent(), id++, paras[i], 18.0f, theme::text_primary(), theme::type::SM,
                     "companion_desc_" + std::to_string(i));
            if (paras.empty())
                line(panel.ent(), id++, "No description.", 18.0f, theme::text_faint(),
                     theme::type::SM, "companion_desc_empty");
            return;
        }
        // Comments: newest first.
        if (!c.commentsError.empty()) {
            line(panel.ent(), 4, c.commentsError, 20.0f, theme::destructive(), theme::type::SM,
                 "companion_comments_error");
            return;
        }
        if (!c.comments) {
            line(panel.ent(), 4, "Reading the comments\xe2\x80\xa6", 20.0f, theme::text_secondary(),
                 theme::type::SM, "companion_comments_loading");
            return;
        }
        if (c.comments->empty()) {
            line(panel.ent(), 4, "No comments yet.", 20.0f, theme::text_faint(), theme::type::SM,
                 "companion_comments_empty");
            return;
        }
        int id = 30;
        for (std::size_t i = 0; i < c.comments->size(); ++i) {
            const auto& m = (*c.comments)[i];
            std::string who = m.author;
            if (m.created > 0) who += "  \xc2\xb7  " + fmtutil::relative_time(m.created) + " ago";
            line(panel.ent(), id++, who, 18.0f, theme::text_secondary(), theme::type::SM,
                 "companion_comment_author_" + std::to_string(i));
            line(panel.ent(), id++, m.text, 20.0f, theme::text_primary(), theme::type::SM,
                 "companion_comment_text_" + std::to_string(i));
        }
    }
};

}  // namespace ecs
