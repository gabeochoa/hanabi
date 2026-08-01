#include <argh.h>

#include <chrono>
#include <thread>
#include <memory>
#include <unistd.h>

#include <afterhours/src/logging.h>

#include "menubar.h"
#include "preload.h"
#include "rl.h"
#include "settings.h"
#include "ui_context.h"

#include "../vendor/afterhours/src/ecs.h"

#include "api/client.h"
#include "api/http_client.h"
#include "api/token_store.h"
#include "ecs/components.h"
#include "ecs/auth_system.h"
#include "ecs/composer_system.h"
#include "ecs/layout_system.h"
#include "ecs/loader_system.h"
#include "ecs/main_pane_system.h"
#include "ecs/sidebar_system.h"
#include "ecs/settings_system.h"
#include "ecs/status_bar_system.h"
#include "ecs/tab_bar_system.h"
#include "ui/theme.h"

// A no-op render system so begin/clear happen in app_frame.
struct MainRenderSystem : afterhours::System<> {
    void once(float) override {}
};

// AppKit seam (sokol_impl.mm): bring the app + its window to the front. Reused
// by the menu-bar "Show hanabi" action. Declared here (no header) to match the
// existing extern "C" style; the windowed link pulls in the .mm definition.
extern "C" void metal_activate_app(void);

namespace app_state {
afterhours::SystemManager* systemManager = nullptr;
std::chrono::high_resolution_clock::time_point startTime;
}  // namespace app_state

// Wall-clock epoch seconds — drives the device-code flow's poll schedule +
// expiry. Kept trivial (no monotonic-vs-wall subtlety needed; the flow only
// uses deltas within a session).
static int64_t now_epoch_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

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

    // Phase AUTH: a previously-authed user is silently logged in. If a token is
    // persisted at ~/.config/hanabi/token.json, adopt it (unless a static
    // HANABI_TOKEN already set one via env — that takes precedence) and select
    // the http backend so the flow never re-prompts. The token value is never
    // logged.
    if (cfg.token.empty()) {
        api::StoredToken stored = api::load_token();
        if (stored.valid()) {
            cfg.token = stored.access_token;
            if (cfg.backend != "http" && cfg.http_ready()) cfg.backend = "http";
        }
    }

    app.client = api::make_client(cfg);
    app.backend_label = app.client ? app.client->backend_label() : "none";
    app.requestListRefresh = true;

    // Phase AUTH: decide whether the in-app device-code login is needed. Only
    // when auth is CONFIGURED (auth_ready), the http backend is selected, and
    // we have NO token yet. Otherwise the app behaves exactly as before: no
    // overlay, mock (or static-token http) loads. The pure DeviceCodeFlow gets
    // the REAL http transport (endpoints read from cfg, nothing hardcoded).
    const bool wantAuthDemo = [] {
        const char* v = std::getenv("HANABI_AUTH_DEMO");
        return v && *v && std::string(v) != "0";
    }();
    if (wantAuthDemo) {
        // Screenshot affordance: force the overlay into AwaitingUser with a
        // FAKE code + URL (no real service, no network) so the panel can be
        // captured headlessly. Mirrors HANABI_VIEW.
        cfg.auth_device_path = cfg.auth_device_path.empty()
                                   ? "/auth/device"
                                   : cfg.auth_device_path;
        cfg.auth_token_path =
            cfg.auth_token_path.empty() ? "/auth/token" : cfg.auth_token_path;
        app.authFlow = std::make_shared<api::DeviceCodeFlow>(
            cfg, [](const std::string&, const std::string&) {
                return api::AuthResponse{};  // never called in demo mode
            });
        app.authFlow->set_demo_awaiting("WXYZ-1234",
                                        "https://example.invalid/activate");
        app.showAuth = true;
    } else if (cfg.auth_ready() && cfg.backend == "http" && cfg.token.empty()) {
        app.authFlow = std::make_shared<api::DeviceCodeFlow>(
            cfg, api::make_http_auth_transport(cfg));
        app.authConfig = cfg;  // remembered so success can rebuild the client
        app.authFlow->begin(now_epoch_seconds());
        app.showAuth = true;
        // Do not fire the initial list refresh until we have a token.
        app.requestListRefresh = false;
    }

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
    sm.register_update_system(std::make_unique<ecs::SettingsSystem>());
    sm.register_update_system(std::make_unique<ecs::ComposerSystem>());
    // Auth overlay draws on top of everything (login gates the app). No-op
    // unless AppComponent::showAuth is true, so it costs nothing when auth is
    // not configured.
    sm.register_update_system(std::make_unique<ecs::AuthSystem>());

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
    // Phase G: the menu-bar extra installs on the FIRST windowed frame — by
    // now sokol has created NSApp + the window (it may not exist yet at
    // app_init). Idempotent + windowed-only: run_headless_screenshot never
    // reaches here, so no status item leaks into a capture. Guarded so we only
    // attempt install once (menubar_install is itself idempotent regardless).
    static bool menubarInstalled = false;
    if (!menubarInstalled) {
        menubar_install();
        menubarInstalled = true;
    }

    // Drain menu-bar action flags into ECS state (single-owner: only the frame
    // loop mutates AppComponent). Show brings the window front; New task opens
    // the composer (via requestNewTask, mirrored below into composerOpen).
    {
        bool wantShow = menubar_take_show();
        bool wantNewTask = menubar_take_new_task();
        if (wantShow) metal_activate_app();
        if (wantNewTask) {
            auto q = afterhours::EntityQuery({.force_merge = true})
                         .whereHasComponent<ecs::AppComponent>()
                         .gen();
            if (!q.empty()) q[0].get().get<ecs::AppComponent>().requestNewTask = true;
        }
    }

    float dt = afterhours::graphics::get_frame_time();
    afterhours::graphics::begin_drawing();
    afterhours::graphics::clear_background(theme::window_bg());
    app_state::systemManager->run(dt);
    afterhours::graphics::end_drawing();

    // Reflect the current blocked count onto the menu-bar title + status row.
    // Same derivation as status_bar_system.h (count of ThreadTag::Blocked) so
    // the two stay in agreement. menubar_set_blocked no-ops when unchanged.
    // Also service requestNewTask here (open composer) so the menu action lands
    // even on a frame where no system consumed it.
    {
        auto q = afterhours::EntityQuery({.force_merge = true})
                     .whereHasComponent<ecs::AppComponent>()
                     .gen();
        if (!q.empty()) {
            auto& app = q[0].get().get<ecs::AppComponent>();
            if (app.requestNewTask) {
                app.composerOpen = true;
                app.requestNewTask = false;
            }

            // Phase AUTH: drive the device-code flow. Poll on a schedule (the
            // flow rate-limits itself to once per `interval`), and react to
            // terminal states: on Success persist the token + rebuild the live
            // client to the http backend so the app switches from the login
            // overlay to real data; the offline escape keeps the mock client.
            if (app.showAuth && app.authFlow) {
                using S = api::DeviceCodeFlow::State;
                app.authFlow->poll_step(now_epoch_seconds());
                if (app.authFlow->current_state() == S::Success) {
                    api::StoredToken tok;
                    tok.access_token = app.authFlow->token();
                    tok.refresh_token = app.authFlow->refresh_token();
                    api::save_token(tok);  // never logged
                    api::Config cfg = app.authConfig;
                    cfg.token = tok.access_token;
                    cfg.backend = "http";
                    app.client = api::make_client(cfg);
                    app.backend_label =
                        app.client ? app.client->backend_label() : "none";
                    app.showAuth = false;
                    app.requestListRefresh = true;
                }
            }
            if (app.requestAuthCancel) {
                // "Use offline (mock)": dismiss the overlay, keep the current
                // (mock) client, and load its sample data.
                app.requestAuthCancel = false;
                app.showAuth = false;
                app.authFlow.reset();
                app.requestListRefresh = true;
            }

            int blocked = 0;
            for (const auto& s : app.sessions)
                if (s.tag == api::ThreadTag::Blocked) ++blocked;
            menubar_set_blocked(blocked);
        }
    }
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

    // Wait for the initial session-list fetch to resolve before capturing.
    // The windowed app runs forever and keeps polling the future, so it always
    // populates once the fetch lands; but this headless path renders a FIXED
    // handful of frames then captures. Against a real network backend the
    // list fetch takes a few hundred ms — far longer than kFrames at 60fps —
    // so without this wait the capture happens while listState is still
    // Loading and the sidebar/digest are empty. Pump frames (so the loader
    // system keeps polling the future) until the list leaves the Loading
    // state, capped so a hung/offline backend can't wedge the capture. The
    // mock resolves on the first poll, so this adds ~0 frames for the mock.
    auto* appForWait = [] () -> ecs::AppComponent* {
        auto q = EntityQuery({.force_merge = true})
                     .whereHasComponent<ecs::AppComponent>()
                     .gen();
        return q.empty() ? nullptr : &q[0].get().get<ecs::AppComponent>();
    }();
    if (appForWait) {
        // IMPORTANT: gate on WALL-CLOCK time, not a frame count. Headless
        // frames don't sleep to target_fps — the loop runs as fast as the CPU
        // allows, so a fixed frame budget (e.g. 300 "frames") elapses in a few
        // milliseconds, far short of the real network fetch (~hundreds of ms)
        // and the capture fires while listState is still Loading (empty list).
        // Pump frames until the list leaves Loading/Idle OR the deadline hits,
        // sleeping a beat each iteration so we actually wait real time and
        // don't spin the CPU. The mock resolves on the first poll, so it exits
        // this loop immediately (adds ~0 wait).
        // We wait for the session LIST to load AND, if a tab is open, for that
        // tab's TRANSCRIPT to finish loading too — otherwise against the real
        // (async network) backend the capture fires while the transcript is
        // still Loading and the pane shows "Loading…" instead of the messages
        // (afterhours_gaps #21). The mock resolves both synchronously.
        constexpr auto kMaxWait = std::chrono::seconds(10);
        auto deadline = std::chrono::steady_clock::now() + kMaxWait;
        while (std::chrono::steady_clock::now() < deadline) {
            bool listReady = appForWait->listState != ecs::LoadState::Loading &&
                             appForWait->listState != ecs::LoadState::Idle;
            // A transcript is "pending" only when a thread is actually open and
            // its fetch hasn't resolved. No open thread => nothing to wait for.
            bool transcriptPending =
                !appForWait->selectedId.empty() &&
                (appForWait->transcriptState == ecs::LoadState::Loading ||
                 appForWait->transcriptState == ecs::LoadState::Idle);
            if (listReady && !transcriptPending) break;
            graphics::begin_frame();
            graphics::clear_background(theme::window_bg());
            sm.run(1.0f / 60.0f);
            graphics::end_frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }

        // Screenshot affordance: HANABI_VIEW=blocked|review|starred|home forces
        // the landing smart-view so a headless capture can photograph any view
        // (including an empty one) without a click. Set AFTER the wait loop so a
        // restored tab's auto-open (which sets view=Chat) can't clobber it; we
        // also drop the selection so the smart-view — not a stale transcript —
        // renders. Ignored when unset. Only honored in --screenshot.
        if (const char* v = std::getenv("HANABI_VIEW")) {
            std::string vs(v);
            ecs::SmartView sv = appForWait->view;
            bool set = true;
            if (vs == "blocked") sv = ecs::SmartView::Blocked;
            else if (vs == "review") sv = ecs::SmartView::Review;
            else if (vs == "starred") sv = ecs::SmartView::Starred;
            else if (vs == "home") sv = ecs::SmartView::Home;
            else set = false;
            if (set) {
                appForWait->view = sv;
                appForWait->selectedId.clear();
            }
        }
    }

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

    {
        auto q = EntityQuery({.force_merge = true})
                     .whereHasComponent<ecs::AppComponent>()
                     .gen();
        if (!q.empty()) {
            auto& app = q[0].get().get<ecs::AppComponent>();
            log_info("Headless capture: {} sessions, listState={}",
                     app.sessions.size(), (int)app.listState);
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
