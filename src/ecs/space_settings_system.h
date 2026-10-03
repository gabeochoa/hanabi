#pragma once

// Space settings (the reference's SpaceSettingsSheet, kt-h76c / kt-fvjo;
// puffin_gaps.md D10): a sheet over a dimmed backdrop for one Space -- its
// name and icon (one Save each: two Metamate mutations, so a half-failure has
// somewhere honest to be reported), who can see it, the member roster with
// Make admin / Make member / Remove, Add someone by name (the people search;
// a pick IS the write), Leave Space and Archive Space (each behind a second
// click that says what will happen), and Done.
//
// What is OFFERED follows the catalog's admin flag; what is ALLOWED is
// Metamate's, on every write. A refusal is shown in the server's words and
// the control goes back to what the server holds. Every value shown after a
// write is what Metamate STORED, never what was typed. The wire is
// api/space_manage.h; the round trips run in memory_system.h.

#include <string>

#include "../api/space_manage.h"
#include "../ui/edged_field.h"
#include "../ui/inline_image.h"
#include "../ui/overlay_lifecycle.h"
#include "../ui/secondary_surface.h"
#include "ui_imports.h"
#include <afterhours/src/plugins/ui/text_input/text_input.h>

namespace ecs {

// Not a System of its own: every registered system costs the frame loop its
// own traffic whether or not it draws (the allocation gate's home arm saw +6
// allocs/frame for this one), so the rename modal's system runs it -- the two
// are both sheets over a scrim and never up at once.
struct SpaceSettingsSheet {
    static constexpr int kLayer = 13;
    static constexpr float kW = 480.0f;
    static constexpr float kH = 540.0f;
    static constexpr std::size_t kRosterRows = 6;
    static constexpr float kRowH = 26.0f;

    void run(UIContext<InputAction>& ctx, AppComponent* app) {
        if (!app) return;
        auto& S = app->spaceSettings;
        if (!S.open) return;
        if (app->escape == EscapeIntent::CloseSpaceSettings) {
            S.reset();
            return;
        }
        const api::spaces::Space* space = app->find_space(S.spaceId);
        if (space == nullptr) {  // left, archived, or gone from the catalog
            S.reset();
            return;
        }
        namespace sm = api::space_manage;
        const bool admin = space->canAdmin;
        const bool idle = S.busy.empty();

        Entity& uiRoot = ui_imm::getUIRootEntity();
        const float sw = hanabi::viewport::width();
        const float sh = hanabi::viewport::height();
        const auto panelRect = hanabi::surface::centered(sw, sh, kW, kH);
        auto backdrop = button(ctx, mk(uiRoot, 8300),
                               hanabi::surface::scrim(sw, sh, kLayer - 1).with_debug_name("space_settings_backdrop"));
        if (idle && hanabi::overlay::dismisses(static_cast<bool>(backdrop), ctx.mouse.pos.x, ctx.mouse.pos.y,
                                               panelRect)) {
            S.reset();
            return;
        }
        auto panel = div(ctx, mk(uiRoot, 8310),
                         hanabi::surface::sheet(panelRect, kLayer).with_debug_name("space_settings_panel"));
        Entity& P = panel.ent();
        const float innerW = kW - 2.0f * hanabi::surface::kSheetPadH;

        label(ctx, P, 1, "Space settings", innerW, 24, theme::text_primary(), theme::type::LG,
              "space_settings_title");

        // ---- Name and icon ----
        auto idRow = row(ctx, P, 2, innerW, hanabi::surface::kFieldH + 4, "space_settings_identity");
        field(ctx, idRow.ent(), 1, S.emoji, 52, "space_settings_icon", admin && idle);
        spacer(ctx, idRow.ent(), 2, 8);
        field(ctx, idRow.ent(), 3, S.name, innerW - 60, "space_settings_name", admin && idle);
        auto saveRow = row(ctx, P, 3, innerW, hanabi::surface::kButtonH + 6, "space_settings_saves");
        if (small_button(ctx, saveRow.ent(), 1, S.busy == "name" ? "Saving\xe2\x80\xa6" : "Save name", 96,
                         admin && idle && S.name != space->name && !S.name.empty(), "space_settings_save_name"))
            start(*app, sm::Write::Rename, sm::rename(S.spaceId, S.name), "name");
        spacer(ctx, saveRow.ent(), 2, 8);
        if (small_button(ctx, saveRow.ent(), 3, S.busy == "icon" ? "Saving\xe2\x80\xa6" : "Save icon", 96,
                         admin && idle && S.emoji != space->emoji, "space_settings_save_icon"))
            start(*app, sm::Write::Emoji, sm::set_emoji(S.spaceId, S.emoji), "icon");

        // ---- Visibility ----
        label(ctx, P, 4, "Who can see it", innerW, 18, theme::text_secondary(), theme::type::SM,
              "space_settings_vis_label");
        const bool locked = sm::visibility_locked(*space);
        auto visRow = row(ctx, P, 5, innerW, hanabi::surface::kButtonH + 4, "space_settings_visibility");
        const char* choices[2][2] = {{"PRIVATE", "Private"}, {"PUBLIC", "Public"}};
        for (int k = 0; k < 2; ++k) {
            const bool on = S.visibility == choices[k][0];
            hanabi::control::State st;
            st.selected = on;
            st.disabled = !(admin && idle && !locked);
            auto b = button(ctx, mk(visRow.ent(), 1 + k),
                            hanabi::surface::action_button(88, false, kLayer, st)
                                .with_label(choices[k][1])
                                .with_margin(Margin{.right = pixels(6)})
                                .with_font_size(theme::type::SM)
                                .with_justify_content(JustifyContent::Center)
                                .with_debug_name(std::string("space_settings_vis_") + choices[k][0]));
            if (b && !on && !st.disabled) {
                const std::string previous = S.visibility;
                S.visibility = choices[k][0];
                start(*app, sm::Write::Visibility, sm::set_visibility(S.spaceId, S.visibility), "visibility",
                      previous);
            }
        }
        if (locked)
            label(ctx, P, 6, "Sensitive Spaces cannot be shown to the whole company.", innerW, 18, theme::text_faint(),
                  theme::type::SM, "space_settings_vis_locked");

        // ---- Members ----
        label(ctx, P, 7, "Members (" + std::to_string(S.roster.total) + ")", innerW, 20, theme::text_secondary(),
              theme::type::SM, "space_settings_members_label");
        if (S.rosterFailed)
            label(ctx, P, 8, "This list may be out of date.", innerW, 18, theme::destructive(), theme::type::SM,
                  "space_settings_members_stale");
        auto list = div(ctx, mk(P, 9),
                        preset::ScrollPanel()
                            .with_size(ComponentSize{pixels(innerW), pixels(kRowH * kRosterRows)})
                            .with_flex_wrap(FlexWrap::NoWrap)
                            .with_transparent_bg()
                            .with_render_layer(kLayer)
                            .with_debug_name("space_settings_roster"));
        for (std::size_t i = 0; i < S.roster.members.size(); ++i) {
            const sm::Member& m = S.roster.members[i];
            const bool isAdmin = sm::is_admin_role(m.role);
            auto r = row(ctx, list.ent(), 10 + static_cast<int>(i), innerW - 12, kRowH,
                         "space_member_" + (m.fbid.empty() ? m.id : m.fbid));
            const bool actionable = admin && !m.fbid.empty() && idle;
            // The action column is reserved on every row an admin sees, so the
            // role words line up whether or not a row can be acted on.
            const float actionsW = admin ? 180.0f : 0.0f;
            face(ctx, r.ent(), 0, *app, m.fbid, m.name);
            label(ctx, r.ent(), 1, m.name.empty() ? m.id : m.name, innerW - 12 - 64 - actionsW - kFace - 8, kRowH,
                  theme::text_primary(), theme::type::SM, "space_member_name");
            label(ctx, r.ent(), 2, isAdmin ? "Admin" : "Member", 64, kRowH, theme::text_faint(), theme::type::SM,
                  "space_member_role");
            if (actionable) {
                if (small_button(ctx, r.ent(), 3, isAdmin ? "Make member" : "Make admin", 104, true,
                                 "space_member_role_btn_" + m.fbid))
                    start(*app, sm::Write::Role, sm::set_role(S.spaceId, m.fbid, !isAdmin), "role");
                if (small_button(ctx, r.ent(), 4, "Remove", 70, true, "space_member_remove_" + m.fbid))
                    start(*app, sm::Write::RemoveMember, sm::remove_member(S.spaceId, m.fbid), "remove");
            }
        }
        // Add someone by name: offered to an admin; a pick IS the write.
        if (admin) {
            auto q = field(ctx, P, 30, S.query, innerW, "space_settings_add", idle);
            (void)q;
            if (S.query != lastQuery_) {
                lastQuery_ = S.query;
                S.sinceTyped = 0;
            }
            if (S.query.empty())
                label(ctx, P, 31, "Add someone by name", innerW, 16, theme::text_faint(), theme::type::XS,
                      "space_settings_add_hint");
            if (S.peopleFailed)
                label(ctx, P, 32, "Could not search for people just now.", innerW, 18, theme::destructive(),
                      theme::type::SM, "space_settings_people_failed");
            else if (S.searching)
                label(ctx, P, 32, "Searching\xe2\x80\xa6", innerW, 18, theme::text_faint(), theme::type::SM,
                      "space_settings_people_searching");
            else if (!S.people.empty())
                for (std::size_t i = 0; i < S.people.size(); ++i) {
                    const sm::Person& p = S.people[i];
                    hanabi::control::State st;
                    st.disabled = !idle;
                    auto prow = row(ctx, P, 40 + static_cast<int>(i), innerW, hanabi::surface::kButtonH + 2,
                                    "space_person_row_" + p.fbid);
                    face(ctx, prow.ent(), 0, *app, p.fbid, p.name);
                    auto b = button(ctx, mk(prow.ent(), 1),
                                    hanabi::surface::action_button(innerW - kFace - 8, false, kLayer, st)
                                        .with_label(p.subtitle.empty() ? p.name : p.name + "  \xc2\xb7  " + p.subtitle)
                                        .with_alignment(TextAlignment::Left)
                                        .with_font_size(theme::type::SM)
                                        .with_margin(Margin{.top = pixels(2)})
                                        .with_debug_name("space_person_" + p.fbid));
                    if (b && idle) start(*app, sm::Write::AddMember, sm::add_member(S.spaceId, p.fbid), "add");
                }
            else if (sm::worth_searching(S.query) && S.searchedFor == S.query)
                label(ctx, P, 32, "Nobody matched that name.", innerW, 18, theme::text_faint(), theme::type::SM,
                      "space_settings_people_none");
        }

        if (!S.failure.empty())
            label(ctx, P, 60, S.failure, innerW, 20, theme::destructive(), theme::type::SM, "space_settings_failure");

        // ---- Leave / Archive / Done ----
        auto danger = row(ctx, P, 70, innerW, hanabi::surface::kButtonH + 14, "space_settings_danger");
        if (S.confirmLeave) {
            label(ctx, danger.ent(), 1, "Leave this Space?", 130, hanabi::surface::kButtonH, theme::text_secondary(),
                  theme::type::SM, "space_settings_leave_q");
            if (small_button(ctx, danger.ent(), 2, "Leave", 70, idle, "space_settings_leave_yes")) {
                S.confirmLeave = false;
                start(*app, sm::Write::Leave, sm::remove_member(S.spaceId, S.viewerFbid), "leave");
            }
            if (small_button(ctx, danger.ent(), 3, "Cancel", 70, true, "space_settings_leave_no"))
                S.confirmLeave = false;
        } else if (S.confirmArchive) {
            label(ctx, danger.ent(), 1, "Archive for everyone?", 150, hanabi::surface::kButtonH,
                  theme::text_secondary(), theme::type::SM, "space_settings_archive_q");
            if (small_button(ctx, danger.ent(), 2, "Archive", 74, idle, "space_settings_archive_yes")) {
                S.confirmArchive = false;
                start(*app, sm::Write::Archive, sm::archive(S.spaceId), "archive");
            }
            if (small_button(ctx, danger.ent(), 3, "Cancel", 70, true, "space_settings_archive_no"))
                S.confirmArchive = false;
        } else {
            // Leave names the viewer: with no fbid known it is off rather than
            // sending a write that cannot say who is leaving.
            if (small_button(ctx, danger.ent(), 1, "Leave Space", 100, idle && !S.viewerFbid.empty(),
                             "space_settings_leave"))
                S.confirmLeave = true;
            if (admin && small_button(ctx, danger.ent(), 2, "Archive Space", 112, idle, "space_settings_archive"))
                S.confirmArchive = true;
        }
        spacer(ctx, danger.ent(), 4, std::max(0.0f, innerW - 100 - 112 - 92 - 24));
        auto done = button(ctx, mk(danger.ent(), 5),
                           hanabi::surface::action_button(84, true, kLayer, disabled_if(!idle))
                               .with_label("Done")
                               .with_font_size(theme::type::SM)
                               .with_justify_content(JustifyContent::Center)
                               .with_debug_name("space_settings_done"));
        if (done && idle) S.reset();
    }

  private:
    std::string lastQuery_;

    // A person's face, 20pt: the photo when one has arrived (masked to a
    // circle by a ring in the sheet's colour -- afterhours has no clip), else
    // the monogram (the first letter in a filled circle). The name never waits
    // for the face: the row draws at once and the photo replaces the letter.
    static constexpr float kFace = 20.0f;
    static void face(UIContext<InputAction>& ctx, Entity& parent, int id, AppComponent& app,
                     const std::string& fbid, const std::string& name) {
        app.peoplePhotos.want(fbid);
        const std::string* path = app.peoplePhotos.path_for(fbid);
        const std::string photo = path ? *path : std::string();
        const std::string letter = api::space_manage::initial_of(name);
        div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_label(photo.empty() ? letter : std::string(" "))
                .with_size(ComponentSize{pixels(kFace), pixels(kFace)})
                .with_margin(Margin{.right = pixels(8)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::XS)
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_render_layer(kLayer)
                .with_on_draw_fg([photo](RectangleType r) {
                    const float cx = r.x + r.width * 0.5f, cy = r.y + r.height * 0.5f, rad = r.width * 0.5f;
                    if (!photo.empty() && hanabi::inline_image::available(photo)) {
                        hanabi::inline_image::draw(photo, r.x, r.y, r.width, r.height);
                        afterhours::draw_ring_segment(cx, cy, rad, rad * 1.5f, 0.0f, 360.0f, 48, theme::panel_bg());
                    }
                })
                .with_on_draw_bg([photo](RectangleType r) {
                    if (!photo.empty() && hanabi::inline_image::available(photo)) return;
                    afterhours::draw_ring_segment(r.x + r.width * 0.5f, r.y + r.height * 0.5f, 0.0f, r.width * 0.5f,
                                                  0.0f, 360.0f, 32, theme::over(theme::text_faint(), theme::panel_bg()));
                })
                .with_debug_name(photo.empty() ? "person_monogram_" + fbid : "person_photo_" + fbid));
    }

    static hanabi::control::State disabled_if(bool d) {
        hanabi::control::State st;
        st.disabled = d;
        return st;
    }

    static void start(AppComponent& app, api::space_manage::Write kind, api::space_manage::Request req,
                      const char* busy, std::string arg = {}) {
        auto& S = app.spaceSettings;
        if (!S.busy.empty() || !app.client) return;
        S.busy = busy;
        S.failure.clear();
        auto c = app.client;
        const std::string body = req.body;
        const char* field = req.field;
        AppComponent::SpaceSettings::Pending p{kind, std::move(arg),
                                               std::async(std::launch::async, [c, body, field] {
                                                   return c->graphql_field(body, field);
                                               })};
        S.write.emplace(std::move(p));
    }

    static afterhours::ui::imm::ElementResult row(UIContext<InputAction>& ctx, Entity& parent, int id, float w,
                                                  float h, const std::string& name) {
        return div(ctx, mk(parent, id),
                   ComponentConfig{}
                       .with_size(ComponentSize{pixels(w), pixels(h)})
                       .with_flex_direction(FlexDirection::Row)
                       .with_flex_wrap(FlexWrap::NoWrap)
                       .with_align_items(AlignItems::Center)
                       .with_margin(Margin{.top = pixels(4)})
                       .with_transparent_bg()
                       .with_roundness(0.0f)
                       .with_render_layer(kLayer)
                       .with_debug_name(name));
    }
    static void spacer(UIContext<InputAction>& ctx, Entity& parent, int id, float w) {
        div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), pixels(1)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_render_layer(kLayer));
    }
    static void label(UIContext<InputAction>& ctx, Entity& parent, int id, const std::string& text, float w, float h,
                      theme::Color c, float px, const std::string& name) {
        div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_label(text)
                .with_size(ComponentSize{pixels(w), pixels(h)})
                .with_transparent_bg()
                .with_custom_text_color(c)
                .with_font_size(px)
                .with_alignment(TextAlignment::Left)
                .with_text_overflow(TextOverflow::Ellipsis)
                .with_roundness(0.0f)
                .with_render_layer(kLayer)
                .with_debug_name(name));
    }
    static bool small_button(UIContext<InputAction>& ctx, Entity& parent, int id, const std::string& text, float w,
                             bool enabled, const std::string& name) {
        auto b = button(ctx, mk(parent, id),
                        hanabi::surface::action_button(w, false, kLayer, disabled_if(!enabled))
                            .with_label(text)
                            .with_margin(Margin{.right = pixels(6)})
                            .with_font_size(theme::type::SM)
                            .with_justify_content(JustifyContent::Center)
                            .with_debug_name(name));
        return static_cast<bool>(b) && enabled;
    }
    static afterhours::ui::imm::ElementResult field(UIContext<InputAction>& ctx, Entity& parent, int id,
                                                    std::string& value, float w, const std::string& name,
                                                    bool enabled) {
        std::string scratch = value;
        auto f = hanabi::ui::edged_text_input(ctx, mk(parent, id), enabled ? value : scratch,
                                              ComponentConfig{}
                                                  .with_size(ComponentSize{pixels(w), pixels(hanabi::surface::kFieldH)})
                                                  .with_margin(Margin{.top = pixels(4)})
                                                  .with_custom_background(theme::panel_bg_2())
                                                  .with_border(theme::border(), pixels(1.0f))
                                                  .with_corner_radius(hanabi::surface::kControlCorner)
                                                  .with_render_layer(kLayer),
                                              name, hanabi::surface::kFieldH * hanabi::surface::kFieldFontRatio);
        return f;
    }
};

}  // namespace ecs
