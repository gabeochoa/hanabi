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
#include "ecs/session_list_system.h"
#include "ecs/status_bar_system.h"
#include "ecs/transcript_system.h"

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

    // Restore last-open session if we have one.
    const std::string& last = Settings::get().get_last_session();
    if (!last.empty()) app.requestOpenId = last;

    // Layout singleton.
    auto& layoutEntity = EntityHelper::createEntity();
    layoutEntity.addComponent<ecs::LayoutComponent>();

    Settings::get().auto_save_enabled = true;
}

// Register the full system pipeline into a SystemManager. Shared by the
// windowed and headless (screenshot) paths so both render identically.
static void build_systems(afterhours::SystemManager& sm) {
    using namespace afterhours;

    ui_imm::registerUIPreLayoutSystems(sm);

    // Data + layout must run before UI-creating systems.
    sm.register_update_system(std::make_unique<ecs::LoaderSystem>());
    sm.register_update_system(std::make_unique<ecs::LayoutSystem>());

    // UI-creating systems (draw order: later on top).
    sm.register_update_system(std::make_unique<ecs::SessionListSystem>());
    sm.register_update_system(std::make_unique<ecs::TranscriptSystem>());
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
    afterhours::graphics::clear_background(afterhours::Color{28, 28, 32, 255});
    app_state::systemManager->run(dt);
    afterhours::graphics::end_drawing();
}

static void app_cleanup() {
    // Persist the last-open session for next launch.
    auto q = afterhours::EntityQuery({.force_merge = true})
                 .whereHasComponent<ecs::AppComponent>()
                 .gen();
    if (!q.empty()) {
        auto& app = q[0].get().get<ecs::AppComponent>();
        if (!app.selectedId.empty())
            Settings::get().set_last_session(app.selectedId);
    }
    Settings::get().write_save_file();
}

// Headless one-shot: render the real UI to an offscreen texture and write a
// PNG, with no window and no screen-recording permission. Used for docs and
// smoke tests. Returns process exit code.
static int run_headless_screenshot(const std::string& path, int w, int h) {
    using namespace afterhours;

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

    // Render several frames so async data loads and layout settles.
    constexpr int kFrames = 45;
    for (int i = 0; i < kFrames; ++i) {
        graphics::begin_frame();
        graphics::clear_background(afterhours::Color{28, 28, 32, 255});
        sm.run(1.0f / 60.0f);
        graphics::end_frame();
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
    argh::parser cmdl(argc, argv);

    // --version prints and exits.
    if (cmdl["--version"] || cmdl["-V"]) {
        printf("hanabi 0.1.0\n");
        return 0;
    }

    app_state::startTime = std::chrono::high_resolution_clock::now();

    // --screenshot <path>: headless one-shot render + capture (docs/smoke).
    std::string shot;
    for (auto& [name, value] : cmdl.params()) {
        if (name == "screenshot") shot = value;
    }
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
