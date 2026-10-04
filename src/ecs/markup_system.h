#pragma once

// Mark up a staged picture before it goes (the reference's markup canvas;
// Knots kt-cimg). Clicking a staged image's thumbnail opens this sheet: the
// picture, fitted and never enlarged; Arrow and Box; Undo; Cancel; Done.
// Strokes are recorded in the picture's own pixels (ui/image_markup.h), drawn
// here at canvas scale, and burned into a new PNG at full resolution on Done
// (native_flatten_markup), which then replaces the staged file. Done with no
// marks changes nothing: re-encoding an unmarked picture buys nothing.

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "../native_extras.h"
#include "../ui/image_markup.h"
#include "../ui/inline_image.h"
#include "../ui/overlay_lifecycle.h"
#include "../ui/secondary_surface.h"
#include "components.h"
#include "pane_state.h"
#include "ui_imports.h"

namespace ecs {

struct MarkupSystem : afterhours::System<UIContext<InputAction>> {
    static constexpr float kPanelW = 760.0f;
    static constexpr float kPanelH = 540.0f;
    static constexpr float kCanvasH = 360.0f;

    static void close(AppComponent& app) {
        app.markupOpen = false;
        app.markup = {};
        app.markupDragging = false;
        app.markupError.clear();
    }

    // The staged attachment the sheet was opened on, or null when it has
    // gone (removed, or sent) since.
    static api::Attachment* target(AppComponent& app) {
        auto& state = model::pane_states().touch(
            model::pane_key(app.markupTarget.pane_index, app.markupTarget.draft_key));
        if (app.markupIndex >= state.attachments.size()) return nullptr;
        api::Attachment& a = state.attachments[app.markupIndex];
        return a.path == app.markupPath ? &a : nullptr;
    }

    // Burn the strokes in and swap the staged file for the result.
    static bool apply(AppComponent& app, api::Attachment& a) {
        if (app.markup.empty()) return true;
        namespace mk = hanabi::markup;
        float imgW = 0.0f, imgH = 0.0f;
        hanabi::inline_image::natural_size(a.path, imgW, imgH);
        const auto crop = mk::effective_crop(app.markup.strokes, imgW, imgH);
        std::vector<float> flat;
        for (const auto& s : app.markup.strokes) {
            if (s.tool == mk::Tool::Crop) continue;
            flat.push_back(s.tool == mk::Tool::Box ? 1.0f : s.tool == mk::Tool::Redact ? 2.0f : 0.0f);
            flat.insert(flat.end(), {s.start.x, s.start.y, s.end.x, s.end.y});
        }
        if (flat.empty() && !crop) return true;  // only a crop under 2 px: nothing to do
        const float cropArr[4] = {crop ? crop->x : 0, crop ? crop->y : 0, crop ? crop->w : 0, crop ? crop->h : 0};
        std::error_code ec;
        const auto dir = std::filesystem::temp_directory_path(ec) / "hanabi-markup";
        std::filesystem::create_directories(dir, ec);
        std::string stem = std::filesystem::path(a.name).stem().string();
        if (stem.empty()) stem = "picture";
        const auto out = dir / (stem + "-marked-" + std::to_string(++app.markupSerial) + ".png");
        if (!native_flatten_markup_cropped(a.path.c_str(), flat.empty() ? nullptr : flat.data(),
                                           static_cast<int>(flat.size() / 5), crop ? cropArr : nullptr,
                                           out.string().c_str())) {
            app.markupError = "Could not draw the marks into the picture.";
            return false;
        }
        a.path = out.string();
        a.name = stem + "-marked.png";
        a.media_type = "image/png";
        a.size_bytes = std::filesystem::file_size(out, ec);
        a.file_id.clear();
        return true;
    }

    // "N marks — burned into the picture when you press Done.", with the crop
    // named when there is one ("Cropped to 120 × 80"), and a redaction said
    // for what it is: the pixels under it do not go.
    static std::string status_line(const AppComponent& app, float imgW, float imgH) {
        namespace mk = hanabi::markup;
        if (app.markup.empty()) return "Drag on the picture to draw.";
        const std::size_t n = mk::painted_count(app.markup.strokes);
        const auto crop = mk::effective_crop(app.markup.strokes, imgW, imgH);
        bool redacts = false;
        for (const auto& s : app.markup.strokes)
            if (s.tool == mk::Tool::Redact) redacts = true;
        std::string out;
        if (n > 0) out = std::to_string(n) + (n == 1 ? " mark" : " marks");
        if (crop) {
            const std::string c = "cropped to " + std::to_string(static_cast<int>(crop->w)) + " \xc3\x97 " +
                                  std::to_string(static_cast<int>(crop->h));
            out = out.empty() ? std::string("C") + c.substr(1) : out + ", " + c;
        }
        if (out.empty()) return "Drag on the picture to draw.";
        out += " \xe2\x80\x94 burned into the picture when you press Done.";
        if (redacts) out += " Redacted areas are not sent.";
        return out;
    }

    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* app = find_singleton<AppComponent>();
        if (app == nullptr || !app->markupOpen) return;
        if (app->escape == EscapeIntent::CloseMarkup) {
            close(*app);
            return;
        }
        api::Attachment* att = target(*app);
        if (att == nullptr) {
            close(*app);
            return;
        }

        Entity& uiRoot = ui_imm::getUIRootEntity();
        const auto rect = hanabi::surface::centered(hanabi::viewport::width(),
                                                    hanabi::viewport::height(), kPanelW, kPanelH);
        const float w = rect.width - hanabi::surface::kSheetPadH * 2.0f;
        auto backdrop = button(ctx, mk(uiRoot, 8500),
            hanabi::surface::scrim(hanabi::viewport::width(), hanabi::viewport::height(), 10)
                .with_debug_name("markup_backdrop"));
        if (hanabi::overlay::dismisses(static_cast<bool>(backdrop), ctx.mouse.pos.x,
                                       ctx.mouse.pos.y, rect)) {
            close(*app);
            return;
        }
        auto panel = div(ctx, mk(uiRoot, 8510),
                         hanabi::surface::sheet(rect, 11).with_debug_name("markup_panel"));
        div(ctx, mk(panel.ent(), 1),
            ComponentConfig{}
                .with_label("Mark up " + att->name)
                .with_size(ComponentSize{pixels(w), pixels(30)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::H1)
                .with_alignment(TextAlignment::Left)
                .with_text_overflow(TextOverflow::Ellipsis)
                .with_render_layer(11)
                .with_debug_name("markup_title"));

        // The tools: Arrow / Box, then Undo.
        auto tools = div(ctx, mk(panel.ent(), 2),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), pixels(32)})
                .with_margin(Margin{.top = pixels(6)})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_render_layer(11)
                .with_debug_name("markup_tools"));
        const auto tool_button = [&](int id, const char* label, bool on, bool disabled,
                                     const char* dbg) {
            return static_cast<bool>(button(ctx, mk(tools.ent(), id),
                ComponentConfig{}
                    .with_label(label)
                    .with_size(ComponentSize{pixels(80), pixels(28)})
                    .with_margin(Margin{.right = pixels(8)})
                    .with_custom_background(on ? theme::accent_soft() : theme::panel_bg_2())
                    .with_custom_text_color(disabled ? theme::text_faint() : theme::text_primary())
                    .with_border(on ? theme::accent() : theme::border_raised(), pixels(1.0f))
                    .with_corner_radius(6.0f)
                    .with_font_size(theme::type::SM)
                    .with_disabled(disabled)
                    .with_render_layer(11)
                    .with_debug_name(dbg)));
        };
        using hanabi::markup::Tool;
        if (tool_button(1, "Arrow", app->markupTool == Tool::Arrow, false, "markup_tool_arrow"))
            app->markupTool = Tool::Arrow;
        if (tool_button(2, "Box", app->markupTool == Tool::Box, false, "markup_tool_box"))
            app->markupTool = Tool::Box;
        if (tool_button(4, "Redact", app->markupTool == Tool::Redact, false, "markup_tool_redact"))
            app->markupTool = Tool::Redact;
        if (tool_button(5, "Crop", app->markupTool == Tool::Crop, false, "markup_tool_crop"))
            app->markupTool = Tool::Crop;
        if (tool_button(3, "Undo", false, app->markup.empty(), "markup_undo")) app->markup.undo();

        // The canvas: the picture fitted, every mark, and the drag in flight.
        float imgW = 0.0f, imgH = 0.0f;
        hanabi::inline_image::natural_size(att->path, imgW, imgH);
        auto canvas = div(ctx, mk(panel.ent(), 3),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), pixels(kCanvasH)})
                .with_margin(Margin{.top = pixels(8)})
                .with_custom_background(theme::panel_bg_2())
                .with_roundness(0.0f)
                .with_render_layer(11)
                .with_debug_name("markup_canvas"));
        const RectangleType cr = canvas.ent().has<afterhours::ui::UIComponent>()
                                     ? canvas.ent().get<afterhours::ui::UIComponent>().rect()
                                     : RectangleType{0, 0, 0, 0};
        const bool inside = ctx.mouse.pos.x >= cr.x && ctx.mouse.pos.x <= cr.x + cr.width &&
                            ctx.mouse.pos.y >= cr.y && ctx.mouse.pos.y <= cr.y + cr.height;
        const hanabi::markup::Point here{ctx.mouse.pos.x - cr.x, ctx.mouse.pos.y - cr.y};
        if (!app->markupDragging && ctx.mouse.just_pressed && inside && imgW > 0) {
            app->markupDragging = true;
            app->markupDragStart = here;
        }
        if (app->markupDragging) app->markupDragNow = here;
        if (app->markupDragging && !ctx.mouse.left_down) {
            app->markupDragging = false;
            const hanabi::markup::Stroke done{
                app->markupTool,
                hanabi::markup::image_point(app->markupDragStart, imgW, imgH, cr.width, cr.height),
                hanabi::markup::image_point(here, imgW, imgH, cr.width, cr.height)};
            if (hanabi::markup::worth_keeping(app->markupDragStart, here) && hanabi::markup::has_extent(done))
                app->markup.add(done);
        }
        std::vector<hanabi::markup::Stroke> shown = app->markup.strokes;
        if (app->markupDragging && hanabi::markup::worth_keeping(app->markupDragStart, here))
            shown.push_back({app->markupTool,
                             hanabi::markup::image_point(app->markupDragStart, imgW, imgH,
                                                         cr.width, cr.height),
                             hanabi::markup::image_point(here, imgW, imgH, cr.width, cr.height)});
        const std::string path = att->path;
        div(ctx, mk(canvas.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), percent(1.0f)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_render_layer(11)
                .with_on_draw_fg([path, shown, imgW, imgH](RectangleType r) {
                    namespace mk = hanabi::markup;
                    const mk::Rect fit = mk::fitted_rect(imgW, imgH, r.width, r.height);
                    if (fit.w <= 0) return;
                    hanabi::inline_image::draw(path, r.x + fit.x, r.y + fit.y, fit.w, fit.h);
                    const float scale = fit.w / imgW;
                    const float width = mk::stroke_width(imgW, imgH) * scale;
                    const auto to = [&](mk::Point p) -> Vector2Type {
                        const mk::Point c = mk::canvas_point(p, imgW, imgH, r.width, r.height);
                        return {r.x + c.x, r.y + c.y};
                    };
                    const auto paint = [&](const mk::Stroke& s, float wpx, const float* rgba) {
                        const theme::Color ink{static_cast<unsigned char>(rgba[0] * 255),
                                               static_cast<unsigned char>(rgba[1] * 255),
                                               static_cast<unsigned char>(rgba[2] * 255),
                                               static_cast<unsigned char>(rgba[3] * 255)};
                        if (s.tool == mk::Tool::Box) {
                            const auto a = to(s.start), b = to(s.end);
                            const float x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x);
                            const float y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
                            afterhours::draw_line_ex({x0, y0}, {x1, y0}, wpx, ink);
                            afterhours::draw_line_ex({x1, y0}, {x1, y1}, wpx, ink);
                            afterhours::draw_line_ex({x1, y1}, {x0, y1}, wpx, ink);
                            afterhours::draw_line_ex({x0, y1}, {x0, y0}, wpx, ink);
                            return;
                        }
                        const auto head = mk::arrow_head(s.start, s.end, width / scale);
                        const auto a = to(s.start);
                        if (head.size() != 3) return;
                        const auto t = to(head[0]), b1 = to(head[1]), b2 = to(head[2]);
                        afterhours::draw_line_ex(a, {(b1.x + b2.x) / 2, (b1.y + b2.y) / 2}, wpx,
                                                 ink);
                        afterhours::draw_triangle(t, b1, b2, ink);
                        afterhours::draw_triangle(t, b2, b1, ink);
                    };
                    for (const auto& s : shown) {
                        if (s.tool == mk::Tool::Crop) continue;
                        if (s.tool == mk::Tool::Redact) {
                            const auto a = to(s.start), b = to(s.end);
                            afterhours::draw_rectangle(
                                RectangleType{std::min(a.x, b.x), std::min(a.y, b.y), std::fabs(b.x - a.x),
                                              std::fabs(b.y - a.y)},
                                theme::Color{0, 0, 0, 255});
                            continue;
                        }
                        paint(s, width * mk::kCasingRatio, mk::kCasing);
                        paint(s, width, mk::kInk);
                    }
                    // The crop: what falls outside it is dimmed, and its frame
                    // drawn, so the reader sees what will be sent.
                    if (const auto crop = mk::effective_crop(shown, imgW, imgH)) {
                        const auto a = to({crop->x, crop->y});
                        const auto b = to({crop->x + crop->w, crop->y + crop->h});
                        const float fx0 = r.x + fit.x, fy0 = r.y + fit.y, fx1 = fx0 + fit.w, fy1 = fy0 + fit.h;
                        const theme::Color dim{0, 0, 0, 150};
                        afterhours::draw_rectangle(RectangleType{fx0, fy0, fit.w, a.y - fy0}, dim);
                        afterhours::draw_rectangle(RectangleType{fx0, b.y, fit.w, fy1 - b.y}, dim);
                        afterhours::draw_rectangle(RectangleType{fx0, a.y, a.x - fx0, b.y - a.y}, dim);
                        afterhours::draw_rectangle(RectangleType{b.x, a.y, fx1 - b.x, b.y - a.y}, dim);
                        const theme::Color edge{255, 255, 255, 230};
                        afterhours::draw_line_ex({a.x, a.y}, {b.x, a.y}, 1.5f, edge);
                        afterhours::draw_line_ex({b.x, a.y}, {b.x, b.y}, 1.5f, edge);
                        afterhours::draw_line_ex({b.x, b.y}, {a.x, b.y}, 1.5f, edge);
                        afterhours::draw_line_ex({a.x, b.y}, {a.x, a.y}, 1.5f, edge);
                    }
                })
                .with_debug_name("markup_picture"));

        div(ctx, mk(panel.ent(), 4),
            ComponentConfig{}
                .with_label(!app->markupError.empty() ? app->markupError : status_line(*app, imgW, imgH))
                .with_size(ComponentSize{pixels(w), pixels(22)})
                .with_margin(Margin{.top = pixels(6)})
                .with_transparent_bg()
                .with_custom_text_color(app->markupError.empty() ? theme::text_secondary()
                                                                 : theme::ask_caveat_ink())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_render_layer(11)
                .with_debug_name("markup_status"));

        auto actions = div(ctx, mk(panel.ent(), 5),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), pixels(34)})
                .with_margin(Margin{.top = pixels(8)})
                .with_flex_direction(FlexDirection::Row)
                .with_justify_content(JustifyContent::FlexEnd)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_render_layer(11)
                .with_debug_name("markup_actions"));
        if (button(ctx, mk(actions.ent(), 1),
                   ComponentConfig{}
                       .with_label("Cancel")
                       .with_size(ComponentSize{pixels(90), pixels(28)})
                       .with_margin(Margin{.right = pixels(8)})
                       .with_transparent_bg()
                       .with_border(theme::border_raised(), pixels(1.0f))
                       .with_custom_text_color(theme::text_primary())
                       .with_font_size(theme::type::SM)
                       .with_corner_radius(6.0f)
                       .with_cursor(afterhours::ui::CursorType::Pointer)
                       .with_render_layer(11)
                       .with_debug_name("markup_cancel"))) {
            close(*app);
            return;
        }
        if (button(ctx, mk(actions.ent(), 2),
                   ComponentConfig{}
                       .with_label("Done")
                       .with_size(ComponentSize{pixels(90), pixels(28)})
                       .with_custom_background(theme::accent())
                       .with_custom_text_color(theme::Color{255, 255, 255, 255})
                       .with_font_size(theme::type::SM)
                       .with_corner_radius(6.0f)
                       .with_cursor(afterhours::ui::CursorType::Pointer)
                       .with_render_layer(11)
                       .with_debug_name("markup_done"))) {
            if (apply(*app, *att)) close(*app);
        }
    }
};

}  // namespace ecs
