#pragma once

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "../native_menu.h"
#include "../ui/artifact_models.h"
#include "../ui/icons.h"
#include "../ui/inflate.h"
#include "../ui/inline_image.h"
#include "../ui/secondary_surface.h"
#include "components.h"
#include "ui_imports.h"

namespace ecs {

struct ArtifactViewerSystem : afterhours::System<UIContext<InputAction>> {
    static constexpr float kMargin = 48.0f;
    static constexpr float kCaptionH = 24.0f;
    static constexpr int kBackdropId = 8300;

    struct Preview {
        std::string path;
        std::string kind;
        std::optional<hanabi::artifact_models::Table> table;
        std::set<std::size_t> numeric;
        std::vector<float> widths;
        std::optional<hanabi::artifact_models::Listing> listing;
        std::vector<hanabi::artifact_models::OutlineRow> outline;
        std::set<std::size_t> collapsed;
        std::string failure;
    };
    std::optional<Preview> preview_;

    static std::vector<std::uint8_t> read_file(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    static float cell_px(const std::string& s) {
        return theme::text_px(s.c_str(), theme::type::SM);
    }

    static Preview parse(const std::string& path, const std::string& kind) {
        namespace am = hanabi::artifact_models;
        Preview p;
        p.path = path;
        p.kind = kind;
        std::vector<std::uint8_t> bytes = read_file(path);
        if (kind == "csv") {
            p.table = am::table(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            if (!p.table) {
                p.failure = "This file has no table in it.";
                return p;
            }
            p.numeric = am::numeric_columns(*p.table);
            p.widths.assign(p.table->header.size(), 0.0f);
            for (std::size_t c = 0; c < p.table->header.size(); ++c) p.widths[c] = cell_px(p.table->header[c]) + 4.0f;
            const std::size_t sample = std::min(p.table->rows.size(), am::kNumericSampleRows);
            for (std::size_t r = 0; r < sample; ++r)
                for (std::size_t c = 0; c < p.table->rows[r].size() && c < p.widths.size(); ++c)
                    p.widths[c] = std::max(p.widths[c], cell_px(p.table->rows[r][c]) + 4.0f);
            for (float& w : p.widths) w = std::clamp(w, 48.0f, 260.0f);
            return p;
        }
        if (am::looks_like_zip(bytes)) {
            p.listing = am::zip_listing(bytes);
        } else if (am::looks_like_gzip(bytes)) {
            hanabi::inflate::Result r = hanabi::inflate::gunzip(bytes, am::kInflateCap);
            if (r.bytes.empty()) {
                p.failure = "This gzip could not be unpacked.";
                return p;
            }
            p.listing = am::tar_listing(r.bytes, "tar in gzip");
            if (p.listing) p.listing->stoppedAtInflateCap = r.stoppedAtCap;
            if (!p.listing) p.failure = "The gzip holds a single file, not an archive.";
        } else if (am::looks_like_tar(bytes)) {
            p.listing = am::tar_listing(bytes, "tar");
        }
        if (!p.listing) {
            if (p.failure.empty()) p.failure = "This file is not an archive this app can list.";
            return p;
        }
        p.outline = am::archive_outline(p.listing->entries);
        return p;
    }

    static void close(AppComponent& app, UIContext<InputAction>& ctx) {
        app.viewerImagePath.clear();
        app.viewerImageName.clear();
        app.viewerPreviewKind.clear();
        app.viewerPreviewBytes = 0;
        const long long before = app.viewerFocusBefore;
        const long long opener = app.viewerOpenerEntity;
        const long long backdropEntity = app.viewerBackdropEntity;
        app.viewerFocusBefore = -1;
        app.viewerOpenerEntity = -1;
        app.viewerBackdropEntity = -1;
        const bool exists =
            before >= 0 &&
            afterhours::ui::UICollectionHolder::getEntityForID(
                static_cast<afterhours::EntityID>(before))
                .valid();
        const long long to = hanabi::native_menu::focus_to_restore(
            before, backdropEntity, static_cast<long long>(ctx.focus_id),
            static_cast<long long>(ctx.ROOT), exists, opener);
        if (to >= 0) ctx.set_focus(static_cast<afterhours::EntityID>(to));
    }

    static bool still_shown(const AppComponent& app, const std::string& path) {
        for (std::size_t i = 0; i < app.active_pane_count(); ++i) {
            const Pane& pane = app.panes[i];
            if (!pane.openSession) continue;
            for (const api::Message& m : pane.openSession->messages)
                if (m.kind == api::EventKind::Artifact && !m.artifact.hidden &&
                    (m.image_path == path || m.artifact.local_path == path))
                    return true;
                // A markdown picture in a message's text (ui/md_image.h)
                // opens here too, and stays while its message is shown.
                else if (m.kind == api::EventKind::Text && m.text.find(path) != std::string::npos)
                    return true;
        }
        return false;
    }

    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* app = find_singleton<AppComponent>();
        if (!app || app->viewerImagePath.empty()) return;
        Entity& uiRoot = ui_imm::getUIRootEntity();
        if (app->escape == EscapeIntent::CloseArtifactViewer ||
            app->paletteOpen || app->sessionSearchOpen || app->renameOpen ||
            app->showShortcuts || app->showBugReport || app->markupOpen || app->companion.open ||
            app->showAuth ||
            !app->requestOpenTab.empty() ||
            app->requestNewThread) {
            app->escape = EscapeIntent::None;
            close(*app, ctx);
            return;
        }
        const std::string path = app->viewerImagePath;
        const bool isPreview = !app->viewerPreviewKind.empty();
        if ((!isPreview && !hanabi::inline_image::available(path)) || !still_shown(*app, path)) {
            close(*app, ctx);
            return;
        }
        if (!isPreview) preview_.reset();

        const float sw = hanabi::viewport::width();
        const float sh = hanabi::viewport::height();

        auto backdrop = button(ctx, mk(uiRoot, kBackdropId),
                               hanabi::surface::scrim(sw, sh, 12)
                                   .with_debug_name("artifact_viewer_backdrop"));
        app->viewerBackdropEntity = static_cast<long long>(backdrop.ent().id);
        if (backdrop) {
            close(*app, ctx);
            return;
        }
        if (isPreview) {
            render_preview(*app, ctx, uiRoot, sw, sh);
            return;
        }

        float natW = 0.0f;
        float natH = 0.0f;
        hanabi::inline_image::natural_size(path, natW, natH);
        if (natW <= 0.0f || natH <= 0.0f) return;
        const float boxW = std::max(64.0f, sw - kMargin * 2.0f);
        const float boxH = std::max(64.0f, sh - kMargin * 2.0f - kCaptionH);
        const float scale = std::min({1.0f, boxW / natW, boxH / natH});
        const float drawW = natW * scale;
        const float drawH = natH * scale;
        const float x = (sw - drawW) * 0.5f;
        const float y = (sh - kCaptionH - drawH) * 0.5f;

        div(ctx, mk(uiRoot, 8310),
            ComponentConfig{}
                .with_label(" ")
                .with_size(ComponentSize{pixels(drawW), pixels(drawH)})
                .with_absolute_position()
                .with_translate(x, y)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_render_layer(13)
                .with_on_draw_fg([path, drawW, drawH](RectangleType r) {
                    hanabi::inline_image::draw(path, r.x, r.y, drawW, drawH);
                })
                .with_debug_name("artifact_viewer_image"));

        const std::string caption =
            app->viewerImageName + "   " + std::to_string(static_cast<int>(natW)) +
            " \xc3\x97 " + std::to_string(static_cast<int>(natH)) +
            "   \xc2\xb7   Esc closes";
        div(ctx, mk(uiRoot, 8320),
            ComponentConfig{}
                .with_label(caption)
                .with_size(ComponentSize{pixels(sw), pixels(kCaptionH)})
                .with_absolute_position()
                .with_translate(0.0f, y + drawH + 8.0f)
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_render_layer(13)
                .with_debug_name("artifact_viewer_caption"));
    }

    static constexpr float kPreviewRowH = 22.0f;
    static constexpr float kPreviewTitleH = 34.0f;
    static constexpr float kPreviewPad = 12.0f;
    static constexpr float kSizeColW = 90.0f;
    static constexpr float kIndentPx = 16.0f;

    void render_preview(AppComponent& app, UIContext<InputAction>& ctx, Entity& uiRoot, float sw, float sh) {
        namespace am = hanabi::artifact_models;
        if (!preview_ || preview_->path != app.viewerImagePath || preview_->kind != app.viewerPreviewKind)
            preview_ = parse(app.viewerImagePath, app.viewerPreviewKind);
        Preview& p = *preview_;
        const float boxW = std::max(320.0f, sw - kMargin * 2.0f);
        const float boxH = std::max(200.0f, sh - kMargin * 2.0f);
        const float x = (sw - boxW) * 0.5f;
        const float y = (sh - boxH) * 0.5f;
        auto card = div(ctx, mk(uiRoot, 8330),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(boxW), pixels(boxH)})
                .with_absolute_position()
                .with_translate(x, y)
                .with_flex_direction(FlexDirection::Column)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_custom_background(theme::chrome::raised())
                .with_border(theme::border(), pixels(1.0f))
                .with_roundness(0.08f)
                .with_render_layer(13)
                .with_debug_name("artifact_preview"));
        std::string what;
        if (p.kind == "csv" && p.table) {
            what = std::string(p.table->delimiter == '\t' ? "tab-separated" : "comma-separated") + "  \xc2\xb7  " +
                   std::to_string(p.table->rows.size()) + (p.table->rows.size() == 1 ? " row" : " rows") +
                   (p.table->truncated ? " shown" : "");
        } else if (p.listing) {
            what = p.listing->container + "  \xc2\xb7  " + std::to_string(p.listing->entries.size()) +
                   (p.listing->entries.size() == 1 ? " entry" : " entries");
        }
        const std::string title = app.viewerImageName + (what.empty() ? "" : "   " + what) +
                                  (app.viewerPreviewBytes > 0 ? "   \xc2\xb7   " + am::byte_label(app.viewerPreviewBytes) : "") +
                                  "   \xc2\xb7   Esc closes";
        div(ctx, mk(card.ent(), 1),
            ComponentConfig{}
                .with_label(title)
                .with_size(ComponentSize{percent(1.0f), pixels(kPreviewTitleH)})
                .with_padding(Padding{.top = pixels(0), .right = pixels(kPreviewPad), .bottom = pixels(0),
                                      .left = pixels(kPreviewPad)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_secondary())
                .with_font_size(theme::type::SM)
                .with_text_overflow(TextOverflow::Ellipsis)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_render_layer(13)
                .with_debug_name("artifact_preview_title"));
        if (!p.failure.empty()) {
            div(ctx, mk(card.ent(), 2),
                ComponentConfig{}
                    .with_label(p.failure)
                    .with_size(ComponentSize{percent(1.0f), pixels(kPreviewRowH)})
                    .with_padding(Padding{.top = pixels(0), .right = pixels(kPreviewPad), .bottom = pixels(0),
                                          .left = pixels(kPreviewPad)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_primary())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_render_layer(13)
                    .with_debug_name("artifact_preview_failure"));
            return;
        }
        std::string footer;
        if (p.table && p.table->truncated)
            footer = "Showing the first " + std::to_string(am::kTableRowCap) + " rows and " +
                     std::to_string(am::kTableColumnCap) + " columns.";
        if (p.listing && p.listing->truncated)
            footer = "Showing the first " + std::to_string(am::kEntryCap) + " entries.";
        if (p.listing && p.listing->stoppedAtInflateCap)
            footer = "Too large to unpack in full: only the first " + am::byte_label(am::kInflateCap) + " is listed.";
        const float footerH = footer.empty() ? 0.0f : kPreviewRowH;
        const float bodyH = boxH - kPreviewTitleH - footerH - kPreviewPad;
        auto body = div(ctx, mk(card.ent(), 2),
            preset::ScrollPanel()
                .with_size(ComponentSize{percent(1.0f), pixels(bodyH)})
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_padding(Padding{.top = pixels(0), .right = pixels(kPreviewPad), .bottom = pixels(0),
                                      .left = pixels(kPreviewPad)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_render_layer(13)
                .with_debug_name("artifact_preview_scroll"));
        if (p.table) render_table(ctx, body.ent(), p, boxW - 2.0f * kPreviewPad);
        else render_outline(ctx, body.ent(), p, boxW - 2.0f * kPreviewPad);
        if (!footer.empty())
            div(ctx, mk(card.ent(), 3),
                ComponentConfig{}
                    .with_label(footer)
                    .with_size(ComponentSize{percent(1.0f), pixels(kPreviewRowH)})
                    .with_padding(Padding{.top = pixels(0), .right = pixels(kPreviewPad), .bottom = pixels(0),
                                          .left = pixels(kPreviewPad)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_render_layer(13)
                    .with_debug_name("artifact_preview_footer"));
    }

    void render_table(UIContext<InputAction>& ctx, Entity& body, const Preview& p, float availW) {
        const auto& t = *p.table;
        float rowW = 0.0f;
        for (float w : p.widths) rowW += w + 8.0f;
        rowW = std::max(rowW, availW);
        const auto line = [&](int id, const std::vector<std::string>& cells, bool header) {
            auto row = div(ctx, mk(body, id),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(rowW), pixels(kPreviewRowH)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_align_items(AlignItems::Center)
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_render_layer(13)
                    .with_debug_name(header ? "csv_header" : "csv_row"));
            for (std::size_t c = 0; c < p.widths.size(); ++c) {
                const std::string& cell = c < cells.size() ? cells[c] : std::string();
                const bool numeric = p.numeric.count(c) > 0;
                div(ctx, mk(row.ent(), static_cast<int>(c) + 1),
                    ComponentConfig{}
                        .with_label(cell.empty() ? std::string(" ") : cell)
                        .with_size(ComponentSize{pixels(p.widths[c]), pixels(kPreviewRowH)})
                        .with_margin(Margin{.right = pixels(8.0f)})
                        .with_transparent_bg()
                        .with_custom_text_color(header ? theme::text_primary() : theme::text_secondary())
                        .with_font_size(theme::type::SM)
                        .with_text_overflow(TextOverflow::Ellipsis)
                        .with_alignment(numeric ? TextAlignment::Right : TextAlignment::Left)
                        .with_roundness(0.0f)
                        .with_render_layer(13)
                        .with_debug_name(std::string(header ? "csv_head_" : "csv_cell_") +
                                         (numeric ? "num_" : "text_") + std::to_string(c)));
            }
        };
        line(1, t.header, true);
        for (std::size_t r = 0; r < t.rows.size(); ++r) line(static_cast<int>(r) + 2, t.rows[r], false);
    }

    void render_outline(UIContext<InputAction>& ctx, Entity& body, Preview& p, float availW) {
        namespace am = hanabi::artifact_models;
        const float nameW = std::max(120.0f, availW - kSizeColW - 8.0f);
        const auto header = div(ctx, mk(body, 1),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(nameW + kSizeColW + 8.0f), pixels(kPreviewRowH)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_render_layer(13)
                .with_debug_name("archive_header"));
        div(ctx, mk(header.ent(), 1),
            ComponentConfig{}
                .with_label(std::string(am::kArchiveNameTitle))
                .with_size(ComponentSize{pixels(nameW), pixels(kPreviewRowH)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_render_layer(13)
                .with_debug_name("archive_name_title"));
        div(ctx, mk(header.ent(), 2),
            ComponentConfig{}
                .with_label(std::string(am::kArchiveSizeTitle))
                .with_size(ComponentSize{pixels(kSizeColW), pixels(kPreviewRowH)})
                .with_transparent_bg()
                .with_custom_text_color(theme::text_primary())
                .with_font_size(theme::type::SM)
                .with_alignment(TextAlignment::Right)
                .with_roundness(0.0f)
                .with_render_layer(13)
                .with_debug_name("archive_size_title"));
        const std::vector<std::size_t> shown = am::visible_outline(p.outline, p.collapsed);
        int id = 2;
        for (std::size_t index : shown) {
            const am::OutlineRow& row = p.outline[index];
            const bool folder = row.entry.isDirectory;
            const bool folded = folder && p.collapsed.count(index) > 0;
            auto line = div(ctx, mk(body, id++),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(nameW + kSizeColW + 8.0f), pixels(kPreviewRowH)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(FlexWrap::NoWrap)
                    .with_align_items(AlignItems::Center)
                    .with_transparent_bg()
                    .with_custom_hover_bg(folder ? theme::hover_over(theme::chrome::raised()) : theme::chrome::raised())
                    .with_cursor(folder ? afterhours::ui::CursorType::Pointer : afterhours::ui::CursorType::Default)
                    .with_roundness(0.0f)
                    .with_render_layer(13)
                    .with_debug_name((folder ? (folded ? "archive_folder_folded_" : "archive_folder_") : "archive_file_") +
                                     std::to_string(index)));
            const float indent = kIndentPx * static_cast<float>(row.depth);
            div(ctx, mk(line.ent(), 1),
                ComponentConfig{}
                    .with_label(" ")
                    .with_size(ComponentSize{pixels(indent + 14.0f), pixels(kPreviewRowH)})
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_render_layer(13)
                    .with_on_draw_fg([folder, folded, indent](RectangleType r) {
                        if (!folder) return;
                        RectangleType slot{r.x + indent, r.y, 14.0f, r.height};
                        hanabi::glyph::chevron(slot, folded, theme::text_faint(), 3.2f);
                    })
                    .with_debug_name("archive_chevron"));
            div(ctx, mk(line.ent(), 2),
                ComponentConfig{}
                    .with_label(row.name)
                    .with_size(ComponentSize{pixels(std::max(40.0f, nameW - indent - 14.0f)), pixels(kPreviewRowH)})
                    .with_transparent_bg()
                    .with_custom_text_color(folder ? theme::text_primary() : theme::text_secondary())
                    .with_font_size(theme::type::SM)
                    .with_text_overflow(TextOverflow::Ellipsis)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_render_layer(13)
                    .with_debug_name("archive_name"));
            div(ctx, mk(line.ent(), 3),
                ComponentConfig{}
                    .with_label(am::archive_size_cell(row.entry))
                    .with_size(ComponentSize{pixels(kSizeColW), pixels(kPreviewRowH)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::text_faint())
                    .with_font_size(theme::type::SM)
                    .with_alignment(TextAlignment::Right)
                    .with_roundness(0.0f)
                    .with_render_layer(13)
                    .with_debug_name("archive_size"));
            if (folder) {
                line.ent().addComponentIfMissing<afterhours::ui::HasClickListener>([](Entity&) {});
                if (line.ent().get<afterhours::ui::HasClickListener>().down) {
                    if (folded) p.collapsed.erase(index);
                    else p.collapsed.insert(index);
                }
            }
        }
    }
};

}
