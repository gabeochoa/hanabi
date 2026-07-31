#include <argh.h>

#include <chrono>
#include <memory>
#include <unistd.h>

#include <afterhours/src/logging.h>

#include "preload.h"
#include "rl.h"
#include "settings.h"
#include "ui_context.h"

#include "../vendor/afterhours/src/ecs.h"

#include "api/client.h"
#include "ecs/components.h"
#include "ecs/layout_system.h"
#include "ecs/loader_system.h"
#include "ecs/main_pane_system.h"
#include "ecs/sidebar_system.h"
#include "ecs/status_bar_system.h"
#include "ecs/tab_bar_system.h"
#include "ui/theme.h"

// A no-op render system so begin/clear happen in app_frame.
struct MainRenderSystem : afterhours::System<> {
    void once(float) override {}
};

namespace app_state {
afterhours::SystemManager* systemManager = nullptr;
std::chrono::high_resolution_clock::time_point startTime;
}  // namespace app_state

// Build the app's entities + state (client, layout, restored session).
static void setup_app_state() {
    using namespace afterhours;

    Settings::get().auto_save_enabled = false;
    Settings::get().load_save_file();

    // Apply persisted theme (dark default).
    theme::set_mode(Settings::get().get_theme() == "light" ? theme::Mode::Light
                                                           : theme::Mode::Dark);

    ui_imm::initUIContext(Settings::get().get_window_width(),
                          Settings::get().get_window_height());

    auto& styling = afterhours::ui::imm::UIStylingDefaults::get();
    styling.set_default_font(afterhours::ui::UIComponent::DEFAULT_FONT,
                             afterhours::ui::pixels(14.0f));
    styling.set_scaling_mode(afterhours::ui::ScalingMode::Adaptive);

    // App singleton: build the client from the environment (mock by default).
    auto& appEntity = EntityHelper::createEntity();
    auto& app = appEntity.addComponent<ecs::AppComponent>();
    api::Config cfg = api::Config::from_env();
    app.client = api::make_client(cfg);
    app.backend_label = app.client ? app.client->backend_label() : "none";
    app.requestListRefresh = true;

    // Restore persisted tab set (opened once the list loads).
    app.restoreTabIds = Settings::get().get_open_tabs();
    app.restoreActiveId = Settings::get().get_active_tab();
    // Back-compat: if no tab set persisted, fall back to last_session.
    if (app.restoreTabIds.empty()) {
        const std::string& last = Settings::get().get_last_session();
        if (!last.empty()) {
            app.restoreTabIds.push_back(last);
            app.restoreActiveId = last;
        }
    }

    // Layout singleton.
    auto& layoutEntity = EntityHelper::createEntity();
    layoutEntity.addComponent<ecs::LayoutComponent>();

    // Tab strip singleton.
    auto& stripEntity = EntityHelper::createEntity();
    stripEntity.addComponent<ecs::TabStripComponent>();

    Settings::get().auto_save_enabled = true;
}

// Register the full system pipeline into a SystemManager. Shared by the
// windowed and headless (screenshot) paths so both render identically.
static void build_systems(afterhours::SystemManager& sm) {
    using namespace afterhours;

    ui_imm::registerUIPreLayoutSystems(sm);

    // Data + layout must run before UI-creating systems.
    sm.register_update_system(std::make_unique<ecs::TabFlowSystem>());
    sm.register_update_system(std::make_unique<ecs::LoaderSystem>());
    sm.register_update_system(std::make_unique<ecs::LayoutSystem>());

    // UI-creating systems (draw order: later on top).
    sm.register_update_system(std::make_unique<ecs::SidebarSystem>());
    sm.register_update_system(std::make_unique<ecs::MainPaneSystem>());
    sm.register_update_system(std::make_unique<ecs::TabBarSystem>());
    sm.register_update_system(std::make_unique<ecs::StatusBarSystem>());

    // Post-layout (autolayout, interactions).
    ui_imm::registerUIPostLayoutSystems(sm);

    // Render systems.
    sm.register_render_system(std::make_unique<MainRenderSystem>());
    ui_imm::registerUIRenderSystems(sm);
}

static void app_init() {
    Preload::get().init("hanabi").make_singleton();
    setup_app_state();

    static afterhours::SystemManager sm;
    app_state::systemManager = &sm;
    build_systems(sm);

    auto readyTime = std::chrono::high_resolution_clock::now();
    auto startupMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                         readyTime - app_state::startTime)
                         .count();
    log_info("Startup: {} ms", startupMs);
}

static void app_frame() {
    float dt = afterhours::graphics::get_frame_time();
    afterhours::graphics::begin_drawing();
    afterhours::graphics::clear_background(theme::window_bg());
    app_state::systemManager->run(dt);
    afterhours::graphics::end_drawing();
}

static void app_cleanup() {
    using namespace afterhours;
    // Persist the open tab set + active tab for next launch.
    auto stripQ = EntityQuery({.force_merge = true})
                      .whereHasComponent<ecs::TabStripComponent>()
                      .gen();
    if (!stripQ.empty()) {
        auto& strip = stripQ[0].get().get<ecs::TabStripComponent>();
        std::vector<std::string> ids;
        std::string active;
        for (auto tabId : strip.tabOrder) {
            auto o = EntityHelper::getEntityForID(tabId);
            if (o.valid() && o->has<ecs::Tab>()) {
                ids.push_back(o->get<ecs::Tab>().sessionId);
                if (o->has<ecs::ActiveTab>())
                    active = o->get<ecs::Tab>().sessionId;
            }
        }
        Settings::get().set_open_tabs(std::move(ids), active);
    }

    auto q = EntityQuery({.force_merge = true})
                 .whereHasComponent<ecs::AppComponent>()
                 .gen();
    if (!q.empty()) {
        auto& app = q[0].get().get<ecs::AppComponent>();
        if (!app.selectedId.empty())
            Settings::get().set_last_session(app.selectedId);
    }
    Settings::get().set_theme(theme::mode() == theme::Mode::Light ? "light"
                                                                  : "dark");
    Settings::get().write_save_file();
}

// Headless one-shot: render the real UI to an offscreen texture and write a
// PNG, with no window and no screen-recording permission. Used for docs and
// smoke tests. Returns process exit code.
static int run_headless_screenshot(const std::string& path, int w, int h) {
    using namespace afterhours;

    // NOTE on hi-DPI: the WINDOWED app already runs high_dpi=true, so the real
    // window is crisp on Retina. This HEADLESS capture path, however, renders
    // into a fixed w*h offscreen texture at 1x — the Metal backend does not
    // supersample it (Config.hidpi is honored only by the raylib backend). We
    // do NOT patch vendored afterhours; a crisp @2x headless capture needs an
    // upstream change (see afterhours_gaps.md). Rendering into a 2x-sized
    // texture does NOT help: the adaptive UI just lays out at the larger
    // logical size (thin sidebar in a big canvas), it doesn't supersample.
    graphics::Config gcfg{};
    gcfg.display = graphics::DisplayMode::Headless;
    gcfg.width = w;
    gcfg.height = h;
    gcfg.target_fps = 60;
    if (!graphics::init(gcfg)) {
        fprintf(stderr, "headless init failed (no GPU?)\n");
        return 1;
    }

    Preload::get().init("hanabi").make_singleton();
    setup_app_state();

    SystemManager sm;
    app_state::systemManager = &sm;
    build_systems(sm);

    auto readyTime = std::chrono::high_resolution_clock::now();
    auto startupMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                         readyTime - app_state::startTime)
                         .count();
    log_info("Startup: {} ms", startupMs);
    fflush(stdout);
    fflush(stderr);

    // Render several frames so async data loads and layout settles.
    constexpr int kFrames = 45;
    for (int i = 0; i < kFrames; ++i) {
        graphics::begin_frame();
        graphics::clear_background(theme::window_bg());
        sm.run(1.0f / 60.0f);
        graphics::end_frame();
        // Test-only instrumentation: log time-to-first-frame once, so the perf
        // harness (scripts/measure_launch.sh) can gate cold launch against the
        // FirstFrame metric in addition to the internal "Startup" init log.
        if (i == 0) {
            auto firstFrame = std::chrono::high_resolution_clock::now();
            auto firstFrameMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    firstFrame - app_state::startTime)
                    .count();
            log_info("FirstFrame: {} ms", firstFrameMs);
            fflush(stdout);
        }
    }

    bool ok = graphics::capture_frame(path);
    graphics::shutdown();
    if (!ok) {
        fprintf(stderr, "capture_frame failed\n");
        return 1;
    }
    printf("wrote %s\n", path.c_str());
    return 0;
}

int main(int argc, char* argv[]) {
    argh::parser cmdl;
    // --screenshot takes a path value. It MUST be pre-registered as a param;
    // otherwise argh parses the space-separated form ("--screenshot <path>")
    // as a bare flag plus a positional arg, cmdl.params() comes back empty,
    // and we silently fall through to the windowed run() path — which opens a
    // real Metal window and never exits in a headless/one-shot context.
    cmdl.add_params({"--screenshot"});
    cmdl.parse(argc, argv);

    // --version prints and exits.
    if (cmdl["--version"] || cmdl["-V"]) {
        printf("hanabi 0.1.0\n");
        return 0;
    }

    app_state::startTime = std::chrono::high_resolution_clock::now();

    // --screenshot <path>: headless one-shot render + capture (docs/smoke).
    // Accepts both "--screenshot <path>" and "--screenshot=<path>".
    std::string shot = cmdl("screenshot").str();
    if (!shot.empty()) {
        return run_headless_screenshot(shot, 1100, 760);
    }

    afterhours::graphics::RunConfig cfg;
    cfg.width = 1100;
    cfg.height = 760;
    cfg.title = "hanabi";
    cfg.target_fps = 120;
    cfg.flags = afterhours::graphics::FLAG_WINDOW_RESIZABLE;
    cfg.init = app_init;
    cfg.frame = app_frame;
    cfg.cleanup = app_cleanup;

    afterhours::graphics::run(cfg);
    return 0;
}
