#include <argh.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>
#include <memory>
#include <vector>
#include <unistd.h>

#include <afterhours/src/logging.h>

#include "menubar.h"
#include "native_extras.h"
#include "preload.h"
#include "rl.h"
#include "settings.h"
#include "ui_context.h"

#include "../vendor/afterhours/src/ecs.h"

#include "api/client.h"
#include "api/disk_cache.h"
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
    app.webBaseUrl = cfg.web_base_url;  // "Copy URL" base (host-neutral if empty)
    // Scope the on-disk cache to THIS backend (keyed by base_url) so two
    // different real servers never read each other's stale sessions. Mock never
    // caches (the loader gates writes on backend_label=="http"), so only the
    // http backend's URL scopes the cache dir; an empty key keeps the flat
    // layout. This is what prevents the "wrong server's data on first paint"
    // class of bug when a user points hanabi at a different backend.
    if (app.backend_label == "http")
        api::disk_cache::set_namespace(cfg.base_url);
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
        app.authFlow = std::make_shared<api::DeviceCodeFlow>(
            cfg, [](const std::string&, const std::string&,
                    const std::string&, const std::string&) {
                return api::AuthResponse{};  // never called in demo mode
            });
        app.authFlow->set_demo_awaiting("ZKRFZQ",
                                        "https://example.invalid/cli/auth?code=ZKRFZQ");
        app.showAuth = true;
    } else if (cfg.auth_ready() && cfg.backend == "http" && cfg.token.empty()) {
        app.authFlow = std::make_shared<api::DeviceCodeFlow>(
            cfg, api::make_http_auth_transport(cfg));
        app.authConfig = cfg;  // remembered so success can rebuild the client
        // Launch-perf: DO NOT call begin() here — it does a BLOCKING device-code
        // POST that would sit on the windowed launch critical path (the single
        // biggest OURS cost: ~hundreds of ms to ~1s against a real auth server,
        // up to the full connect timeout ~5s if it's unreachable). Defer it:
        // the window paints immediately with the overlay in a "requesting
        // code…" state, and LoaderSystem kicks begin() on a worker thread (see
        // AppComponent::authNeedsBegin / authBeginFuture). The old synchronous
        // begin() is preserved ONLY behind HANABI_AUTH_LOG (an opt-in real-run
        // proof aid that must observe the round-trip inline).
        if (const char* lg = std::getenv("HANABI_AUTH_LOG"); lg && *lg &&
            std::string(lg) != "0") {
            app.authFlow->begin(now_epoch_seconds());
            // Real-run proof aid: log the userCode the LIVE server issued
            // WITHOUT completing the browser step. Never logs the token.
            using S = api::DeviceCodeFlow::State;
            const auto st = app.authFlow->current_state();
            if (st == S::AwaitingUser) {
                std::fprintf(stderr,
                             "[HANABI_AUTH] awaiting user: userCode=%s "
                             "authUrl=%s\n",
                             app.authFlow->user_code().c_str(),
                             app.authFlow->verification_uri().c_str());
                // One poll to prove the poll round-trips (pending expected).
                app.authFlow->poll_step(now_epoch_seconds() +
                                        cfg.auth_poll_interval);
                std::fprintf(stderr, "[HANABI_AUTH] after one poll: state=%s\n",
                             app.authFlow->current_state() == S::AwaitingUser
                                 ? "AwaitingUser(pending)"
                                 : "terminal");
            } else {
                std::fprintf(stderr,
                             "[HANABI_AUTH] begin() did not reach AwaitingUser "
                             "(error=%s)\n",
                             app.authFlow->error().c_str());
            }
            std::fflush(stderr);
        } else {
            // Normal path: defer the blocking begin() off the launch path.
            app.authNeedsBegin = true;
        }
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
    // app_init runs AFTER graphics::run has created the Metal/Cocoa window + GPU
    // context. Profiling (2026-08-02) showed that window+GPU init is ~130-220ms
    // (cold: up to ~1.4s on the first launch after a build, as Metal compiles
    // its pipeline/shader cache + dyld warms) while ALL of our own init below is
    // ~3ms. So the historical single "Startup" number was dominated by
    // unavoidable OS/Metal window creation inside the vendored backend, not our
    // code. We now log BOTH: `Gfx init` (window+GPU, from process start to here)
    // and `App init` (our preload+state+systems), so the metric reflects what
    // hanabi actually controls. See afterhours_gaps.md (graphics-init cost).
    auto gfxReady = std::chrono::high_resolution_clock::now();
    auto gfxMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                     gfxReady - app_state::startTime).count();

    // HANABI_STARTUP_PROF=1: emit a sub-millisecond breakdown of our OWN
    // App-init phases so we can attribute the launch cost. Off by default (the
    // steady-state startup log below is enough for the perf gate). Uses micros
    // because every one of these phases is <1ms — a ms-granularity log would
    // round them all to 0 and hide where App-init's few ms actually go.
    const bool prof = [] {
        const char* v = std::getenv("HANABI_STARTUP_PROF");
        return v && *v && std::string(v) != "0";
    }();
    auto us_since = [](auto a, auto b) {
        return std::chrono::duration_cast<std::chrono::microseconds>(b - a)
            .count();
    };
    auto t0 = std::chrono::high_resolution_clock::now();

    Preload::get().init("hanabi");
    auto t1 = std::chrono::high_resolution_clock::now();
    Preload::get().make_singleton();
    auto t2 = std::chrono::high_resolution_clock::now();
    setup_app_state();
    auto t3 = std::chrono::high_resolution_clock::now();

    static afterhours::SystemManager sm;
    app_state::systemManager = &sm;
    build_systems(sm);
    auto t4 = std::chrono::high_resolution_clock::now();

    auto readyTime = t4;
    auto appMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                     readyTime - gfxReady).count();
    auto startupMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                         readyTime - app_state::startTime).count();
    // Startup = total (kept for the perf gate + back-compat); the split shows
    // where it goes.
    log_info("Startup: {} ms", startupMs);
    log_info("  Gfx init: {} ms (window + GPU/Metal context — vendored/OS)",
             gfxMs);
    log_info("  App init: {} ms (preload + state + systems — ours)", appMs);
    if (prof) {
        log_info("  [prof] Preload::init   : {} us (files::init)", us_since(t0, t1));
        log_info("  [prof] make_singleton  : {} us (font load + UI plugin)", us_since(t1, t2));
        log_info("  [prof] setup_app_state : {} us (settings + client + cache)", us_since(t2, t3));
        log_info("  [prof] build_systems   : {} us", us_since(t3, t4));
    }
    fflush(stdout);
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
        // Phase G extras: register the global hotkey (Cmd+Shift+N) on the same
        // windowed-only, install-once path. Idempotent; headless never reaches
        // here so no global listener lingers after a --screenshot capture.
        native_hotkey_install();
        // Phase G extra: install the hanabi:// URL / Apple-event handler so a
        // tapped Spotlight result (hanabi://thread/<id>) opens that thread.
        // Windowed-only + install-once, same as the hotkey.
        native_openurl_install();
        menubarInstalled = true;
        // Diagnostic (windowed-only, fires once): HANABI_NOTIFY_TEST=<thread-id>
        // posts a single native notification carrying that id, so the
        // notification banner + click->open-thread path can be exercised
        // manually. Ignored when unset; never runs on the headless path.
        if (const char* nt = std::getenv("HANABI_NOTIFY_TEST"); nt && *nt) {
            native_notify("hanabi: thread needs you",
                          "Click to open this thread", nt);
        }
    }

    // Drain menu-bar action flags into ECS state (single-owner: only the frame
    // loop mutates AppComponent). Show brings the window front; New task opens
    // the composer (via requestNewTask, mirrored below into composerOpen).
    // The global hotkey (Cmd+Shift+N) folds into the SAME activate+new-task
    // path, so a press behaves exactly like the "New task" menu item.
    {
        bool hotkey = native_hotkey_take_triggered();
        bool wantShow = menubar_take_show() || hotkey;
        bool wantNewTask = menubar_take_new_task() || hotkey;
        if (wantShow) metal_activate_app();
        if (wantNewTask) {
            auto q = afterhours::EntityQuery({.force_merge = true})
                         .whereHasComponent<ecs::AppComponent>()
                         .gen();
            if (!q.empty()) q[0].get().get<ecs::AppComponent>().requestNewTask = true;
        }
        // Phase G extra: a hanabi://thread/<id> open (tapped Spotlight result)
        // opens + navigates to that thread — same seam a sidebar row click
        // uses (requestOpenTab), so the tab loader fetches + focuses it.
        char openId[256];
        if (native_take_open_thread(openId, sizeof(openId))) {
            auto q = afterhours::EntityQuery({.force_merge = true})
                         .whereHasComponent<ecs::AppComponent>()
                         .gen();
            if (!q.empty()) {
                q[0].get().get<ecs::AppComponent>().requestOpenTab = openId;
                metal_activate_app();
            }
        }
    }

    float dt = afterhours::graphics::get_frame_time();
    afterhours::graphics::begin_drawing();
    afterhours::graphics::clear_background(theme::window_bg());
    app_state::systemManager->run(dt);
    afterhours::graphics::end_drawing();

    // Windowed FirstFrame instrumentation — the perf gate's headless
    // FirstFrame does NOT reflect the real windowed cold launch (no Cocoa
    // window, no on-screen swap, different Metal pipeline warm-up). This logs
    // the REAL windowed process-start -> first on-screen frame once, gated so
    // it only fires the first time. HANABI_QUIT_AFTER_FIRST_FRAME=1 makes the
    // windowed run self-terminate right after emitting the number, so a
    // windowed launch can be profiled in a loop without the app running
    // forever (it otherwise never exits). Both are diagnostic-only: a normal
    // user launch (neither env set) logs nothing here and runs as before.
    static bool firstFrameLogged = false;
    if (!firstFrameLogged) {
        firstFrameLogged = true;
        auto firstFrame = std::chrono::high_resolution_clock::now();
        auto firstFrameMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                firstFrame - app_state::startTime).count();
        const bool profFrame = [] {
            const char* v = std::getenv("HANABI_STARTUP_PROF");
            return v && *v && std::string(v) != "0";
        }();
        const bool quitAfter = [] {
            const char* v = std::getenv("HANABI_QUIT_AFTER_FIRST_FRAME");
            return v && *v && std::string(v) != "0";
        }();
        if (profFrame || quitAfter) {
            log_info("WindowedFirstFrame: {} ms (process start -> first "
                     "on-screen frame — REAL windowed cold launch)",
                     firstFrameMs);
            fflush(stdout);
            fflush(stderr);
        }
        if (quitAfter)
            afterhours::graphics::request_quit();
    }

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
            // Don't touch the flow while the deferred begin() is still in
            // flight on the worker thread (DeviceCodeFlow is not thread-safe).
            // LoaderSystem clears authBeginPending once begin() resolves; until
            // then the overlay simply shows its pre-code state.
            if (app.showAuth && app.authFlow && !app.authBeginPending) {
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
            const api::SessionSummary* newlyBlocked = nullptr;
            for (const auto& s : app.sessions)
                if (s.tag == api::ThreadTag::Blocked) {
                    ++blocked;
                    if (newlyBlocked == nullptr) newlyBlocked = &s;
                }
            menubar_set_blocked(blocked);

            // Phase G extra: donate threads to Spotlight (CoreSpotlight). Only
            // does real work when running from the .app bundle (native_extras
            // gates on a non-nil bundle identifier); the bare dev binary stays
            // a no-op. Re-index only when the session set actually changes — a
            // cheap signature (count + concatenated id hash) gates it so we
            // don't re-donate every frame. Screenshot runs never reach here.
            {
                static size_t lastIndexSig = 0;
                size_t sig = app.sessions.size() * 1000003u;
                for (const auto& s : app.sessions)
                    sig = sig * 1000003u +
                          std::hash<std::string>{}(s.id) +
                          std::hash<std::string>{}(s.title);
                if (sig != lastIndexSig) {
                    lastIndexSig = sig;
                    for (const auto& s : app.sessions)
                        if (!s.id.empty())
                            native_spotlight_index(s.id.c_str(),
                                                   s.title.c_str());
                }
            }

            // Phase G extra: post a native notification when the blocked-on-you
            // count NEWLY INCREASES (a thread just started needing the user).
            // Debounced: fires only on a strict increase vs the last observed
            // count, and rate-limited to at most once per kNotifyMinGapSecs so
            // a burst of updates can't spam. Windowed path only — app_frame is
            // never reached by run_headless_screenshot, so a --screenshot run
            // never posts a notification (and NSUserNotification never prompts
            // regardless). First observation (lastBlocked < 0) primes the
            // baseline WITHOUT notifying, so launching into an already-blocked
            // state is silent.
            static int lastBlockedNotified = -1;
            static double lastNotifyAt = -1.0;
            constexpr double kNotifyMinGapSecs = 30.0;
            if (lastBlockedNotified < 0) {
                lastBlockedNotified = blocked;  // prime; no notification
            } else if (blocked > lastBlockedNotified) {
                const double nowSec =
                    static_cast<double>(now_epoch_seconds());
                if (lastNotifyAt < 0.0 ||
                    nowSec - lastNotifyAt >= kNotifyMinGapSecs) {
                    char title[64];
                    std::snprintf(title, sizeof(title),
                                  blocked == 1 ? "%d thread needs you"
                                               : "%d threads need you",
                                  blocked);
                    const char* body =
                        (newlyBlocked && !newlyBlocked->title.empty())
                            ? newlyBlocked->title.c_str()
                            : "";
                    // Carry the newly-blocked thread's id so CLICKING the
                    // notification opens that thread (native_extras routes it
                    // through the same open-thread slot the deep-link uses).
                    const char* tid =
                        (newlyBlocked && !newlyBlocked->id.empty())
                            ? newlyBlocked->id.c_str()
                            : "";
                    native_notify(title, body, tid);
                    lastNotifyAt = nowSec;
                }
                lastBlockedNotified = blocked;
            } else if (blocked < lastBlockedNotified) {
                // Count dropped (user handled something) — track it so a later
                // re-increase re-notifies, but don't notify on the decrease.
                lastBlockedNotified = blocked;
            }
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

        // Perf/screenshot affordance: HANABI_OPEN=<id> opens a specific thread
        // in the headless capture (used to open the long perf fixture "rbig"
        // or the rich mock "r9"). Sets requestOpenTab so the loader fetches +
        // opens it; a few pump frames below let it land before capture/timing.
        if (const char* oid = std::getenv("HANABI_OPEN"); oid && *oid) {
            appForWait->requestOpenTab = oid;
            appForWait->view = ecs::SmartView::Chat;
            for (int p = 0; p < 6; ++p) {
                graphics::begin_frame();
                graphics::clear_background(theme::window_bg());
                sm.run(1.0f / 60.0f);
                graphics::end_frame();
            }
        }

        // Perf/screenshot affordance: HANABI_EXPAND=1 pre-expands every tool
        // pile + the sub-agent rollup in the open thread so a headless capture
        // can photograph the EXPANDED nested sub-rows / chips. Uses the same
        // expandedPiles state the click path toggles; render-only.
        if (const char* ex = std::getenv("HANABI_EXPAND"); ex && *ex &&
            std::string(ex) != "0") {
            appForWait->expandedPiles.insert("__subagents__");
            if (appForWait->openSession) {
                const auto& ms = appForWait->openSession->messages;
                for (size_t k = 0; k < ms.size(); ++k)
                    if (ms[k].role == api::Role::Tool)
                        appForWait->expandedPiles.insert(
                            ms[k].id.empty() ? ("pile" + std::to_string(k))
                                             : ms[k].id);
            }
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
            else if (vs == "archived") sv = ecs::SmartView::Archived;
            else set = false;
            if (set) {
                appForWait->view = sv;
                appForWait->selectedId.clear();
            }
        }

        // Screenshot affordance: HANABI_TEST_OVERLAY=settings|composer opens an
        // overlay that is otherwise keypress-only (Cmd+, / Cmd+N), so the
        // settings sheet + new-task composer can be photographed headlessly.
        // Mirrors HANABI_VIEW; ignored when unset; no network, render-only.
        if (const char* ov = std::getenv("HANABI_TEST_OVERLAY"); ov && *ov) {
            std::string os(ov);
            if (os == "settings") appForWait->showSettings = true;
            else if (os == "composer") appForWait->composerOpen = true;
        }

        // Screenshot affordance: HANABI_SKELETON_DEMO=1 forces the cold-cache
        // loading state (empty list + Loading) so the skeleton placeholder rows
        // can be photographed headlessly — the harness otherwise waits PAST
        // Loading and would never capture it. No network; render-only.
        if (const char* d = std::getenv("HANABI_SKELETON_DEMO"); d && *d &&
            std::string(d) != "0") {
            appForWait->sessions.clear();
            appForWait->listState = ecs::LoadState::Loading;
            appForWait->view = ecs::SmartView::Home;
            appForWait->selectedId.clear();
        }

        // Screenshot affordance: HANABI_LOADING_DEMO=1 forces the per-thread
        // transcript switch spinner (transcriptState=Loading + a mismatched
        // transcriptLoadingId, so the pane shows the "Loading conversation…"
        // ring instead of stale/blank content). Render-only; no network.
        if (const char* d = std::getenv("HANABI_LOADING_DEMO"); d && *d &&
            std::string(d) != "0") {
            appForWait->view = ecs::SmartView::Chat;
            appForWait->transcriptState = ecs::LoadState::Loading;
            appForWait->transcriptLoadingId = "__loading_demo__";
        }
        // Screenshot affordance: HANABI_OLDER_DEMO=1 forces the top
        // "loading older messages…" pill (loadingOlder=true) over the open
        // transcript, so a headless capture can photograph it. Render-only.
        if (const char* d = std::getenv("HANABI_OLDER_DEMO"); d && *d &&
            std::string(d) != "0") {
            appForWait->loadingOlder = true;
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

    // Perf affordance: HANABI_FRAME_TIMING=<N> runs N extra frames after warmup
    // and reports the per-frame sm.run() cost (min / median / mean / max) so we
    // can measure the transcript rebuild cost that drives choppy scroll. This
    // is the headline number for the perf work. No capture side effects.
    if (const char* ft = std::getenv("HANABI_FRAME_TIMING"); ft && *ft) {
        int iters = std::atoi(ft);
        if (iters < 10) iters = 60;
        // HANABI_FRAME_SPLIT=1 attributes each frame's cost to the update
        // (tick: our systems re-emit the UI tree) vs render (autolayout + draw)
        // halves, so the T7 idle-frame work has a target. Diagnostic-only.
        const bool split = [] {
            const char* v = std::getenv("HANABI_FRAME_SPLIT");
            return v && *v && std::string(v) != "0";
        }();
        std::vector<double> ms, msU, msR;
        ms.reserve(iters);
        msU.reserve(iters);
        msR.reserve(iters);
        for (int i = 0; i < iters; ++i) {
            graphics::begin_frame();
            graphics::clear_background(theme::window_bg());
            auto a = std::chrono::high_resolution_clock::now();
            if (split) {
                auto& ents = afterhours::EntityHelper::get_entities_for_mod();
                auto u0 = std::chrono::high_resolution_clock::now();
                sm.fixed_tick_all(ents, 1.0f / 60.0f);
                sm.tick_all(ents, 1.0f / 60.0f);
                afterhours::EntityHelper::cleanup();
                auto u1 = std::chrono::high_resolution_clock::now();
                sm.render_all(1.0f / 60.0f);
                auto u2 = std::chrono::high_resolution_clock::now();
                msU.push_back(
                    std::chrono::duration<double, std::milli>(u1 - u0).count());
                msR.push_back(
                    std::chrono::duration<double, std::milli>(u2 - u1).count());
            } else {
                sm.run(1.0f / 60.0f);
            }
            auto b = std::chrono::high_resolution_clock::now();
            graphics::end_frame();
            ms.push_back(
                std::chrono::duration<double, std::milli>(b - a).count());
        }
        std::sort(ms.begin(), ms.end());
        double sum = 0;
        for (double v : ms) sum += v;
        double mean = sum / ms.size();
        double median = ms[ms.size() / 2];
        log_info(
            "FrameTiming: frames={} min={:.2f}ms median={:.2f}ms "
            "mean={:.2f}ms max={:.2f}ms",
            ms.size(), ms.front(), median, mean, ms.back());
        if (split && !msU.empty()) {
            std::sort(msU.begin(), msU.end());
            std::sort(msR.begin(), msR.end());
            log_info(
                "FrameSplit: update(tick) median={:.2f}ms  "
                "render(layout+draw) median={:.2f}ms",
                msU[msU.size() / 2], msR[msR.size() / 2]);
        }
        fflush(stdout);
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
