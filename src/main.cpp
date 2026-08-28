#include <argh.h>
#include <branding.h>

#ifndef HANABI_BUILD_STAMP
#define HANABI_BUILD_STAMP "dev"
#endif

#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <thread>
#include <memory>
#include <vector>
#include <unistd.h>

#include <afterhours/src/logging.h>
#include <afterhours/src/plugins/files.h>

#include "menubar.h"
#include "native_extras.h"
#include "preload.h"
#include "rl.h"
#include "settings.h"
#include "test_hooks.h"
#include "util/capture_clock.h"
#include "util/autorelease.h"
#define HANABI_PROF_DEFINE_ALLOC_COUNTERS
#include "util/prof.h"
#include "frame_activity_collect.h"
#include "util/gpu_mem.h"
#include "util/launch_curve.h"
#include "util/prewarm.h"
#include "util/mem_ladder.h"
#include "util/breaker.h"
#include "util/atlas_guard.h"
#include "util/bounds_audit.h"
#include "util/soak.h"
#include "util/stress.h"
#include "util/notify_events.h"
#include "util/quiet_hours.h"
#include "util/spotlight_catalog.h"
#include "version.h"
#include "ui_context.h"
#include "ui/inline_image.h"
#include "ui/font_system.h"
#include "ui/link_detect.h"
#include "ui/theme.h"

#include "../vendor/afterhours/src/ecs.h"

#include "api/client.h"
#include "api/disk_cache.h"
#include "api/http_client.h"
#include "api/token_store.h"
#include "ecs/components.h"
#include "ecs/auth_system.h"
#include "ecs/composer_system.h"
#include "ecs/command_system.h"
#include "ecs/palette_system.h"
#include "ecs/session_search_system.h"
#include "ecs/arrow_system.h"
#include "ecs/attachment_intake_system.h"
#include "ecs/escape_system.h"
#include "ecs/focus_visible_system.h"
#include "ecs/text_edit_chords_system.h"
#include "ecs/rename_modal_system.h"
#include "ecs/toast_system.h"
#include "ecs/layout_system.h"
#include "ecs/loader_system.h"
#include "ecs/main_pane_system.h"
#include "ecs/sidebar_system.h"
#include "ecs/settings_system.h"
#include "ecs/shortcuts_system.h"
#include "ecs/tab_bar_system.h"
#include "ecs/theme_rotation_system.h"
#include "ecs/widget_retire_system.h"
#include "ui/theme.h"

// A no-op render system so begin/clear happen in app_frame.
struct MainRenderSystem : afterhours::System<> {
    void once(float) override {}
};

// AppKit seam (sokol_impl.mm): bring the app + its window to the front. Reused
// by the menu-bar "Show hanabi" action. Declared here (no header) to match the
// existing extern "C" style; the windowed link pulls in the .mm definition.
extern "C" void metal_activate_app(void);
// The real NSWindow resize, for the windowed GPU watch below (#200).
extern "C" void metal_set_window_size(int width, int height);
extern "C" void metal_constrain_window_to_screen(void);
extern "C" void metal_frame_activity_install(void);
extern "C" unsigned metal_take_window_activity(void);

static unsigned frame_input_activity() {
    namespace md = afterhours::graphics::metal_detail;
    auto& input = md::input_state();
    unsigned activity = 0;
    if (input.mouse_dx != 0.0f || input.mouse_dy != 0.0f ||
        input.scroll_x != 0.0f || input.scroll_y != 0.0f)
        activity |= 1u;
    for (int i = 0; i < md::MAX_MOUSE_BUTTONS; ++i)
        if (input.mouse_pressed[i] || input.mouse_released[i]) activity |= 1u;
    for (int i = 0; i < md::MAX_KEYS; ++i)
        if (input.key_pressed[i] || input.key_released[i] || input.key_repeat[i])
            activity |= 2u;
    if (input.char_queue_head != input.char_queue_tail) activity |= 2u;
    return activity;
}

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

// Is the wall clock inside the user's quiet window right now? The window
// itself is pure and tested (util/quiet_hours.h); reading the local clock is
// the part that cannot be, so it is kept to these four lines.
static bool in_quiet_hours_now() {
    const int start = Settings::get().get_quiet_start_minutes();
    const int end = Settings::get().get_quiet_end_minutes();
    if (start == end) return false;
    const std::time_t t = std::time(nullptr);
    std::tm local{};
    localtime_r(&t, &local);
    return hanabi::quiet::in_window(local.tm_hour * 60 + local.tm_min, start,
                                    end);
}

// Build the app's entities + state (client, layout, restored session).
static void setup_app_state() {
    using namespace afterhours;

    Settings::get().auto_save_enabled = false;
    Settings::get().load_save_file();

    // Apply persisted theme. "system" resolves against the live OS appearance
    // (gap #16 fixed via macos_is_dark_mode); "light"/"dark" are explicit;
    // anything else (or unset) defaults to dark.
    {
        // The custom colour choices go on FIRST: set_mode re-layers them over
        // the palette it loads, so the first frame is already the user's
        // accent rather than the stock blue for a frame.
        theme::set_accent_choice(Settings::get().get_accent_choice());
        theme::set_highlight_choice(Settings::get().get_highlight_choice());
        const std::string& tc = Settings::get().get_theme();
        bool light = (tc == "light") ||
                     (tc == "system" && !hanabi::os_is_dark_mode());
        theme::set_mode(light ? theme::Mode::Light : theme::Mode::Dark);
    }

    {
        auto& fontMgr =
            afterhours::EntityHelper::get_singleton_cmp_enforce<
                afterhours::ui::FontManager>();
        hanabi::fonts::apply(fontMgr, Settings::get().get_font_choice(),
                             Settings::get().get_font_weight());
    }

    ui_imm::initUIContext(Settings::get().get_window_width(),
                          Settings::get().get_window_height());

    auto& styling = afterhours::ui::imm::UIStylingDefaults::get();
    styling.set_default_font(afterhours::ui::UIComponent::DEFAULT_FONT,
                             afterhours::ui::pixels(14.0f));
    styling.set_scaling_mode(afterhours::ui::ScalingMode::Adaptive);

    // Supersampled capture (HANABI_UI_SCALE, see src/test_hooks.h). Unset is a
    // hard no-op: theme.ui_scale is left at its 1.0 default and nothing below
    // this line behaves differently, so every windowed run and the whole
    // scripted UI suite are unaffected.
    if (const float uis = hanabi::test_hooks::ui_scale(); uis != 1.0f) {
        afterhours::ui::imm::ThemeDefaults::get().theme.ui_scale = uis;
    }

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
    app.trackerBaseUrl = cfg.tracker_base_url;  // empty => ids stay prose
    app.configuredContextBudget = cfg.context_budget_tokens;
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
        const std::string demo = std::getenv("HANABI_AUTH_DEMO");
        if (demo == "failed")
            app.authFlow->set_demo_failed("The approval server rejected this request.");
        else if (demo == "expired")
            app.authFlow->set_demo_expired();
        else
            app.authFlow->set_demo_awaiting(
                "ZKRFZQ", "https://example.invalid/cli/auth?code=ZKRFZQ");
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
    app.restorePinnedIds = Settings::get().get_pinned_tabs();
    app.restoreActiveId = Settings::get().get_active_tab();
    // The split is restored here, but the PANES' threads are restored by the
    // same pass that restores the tabs (tab_bar_system.h): a pane's thread has
    // to exist in the session list before it can be opened, exactly like a
    // tab's, and asking for it earlier is asking for a fetch of an id the
    // backend may no longer know.
    app.splitOpen = Settings::get().get_split_open();
    app.splitRatio =
        hanabi::clamp_split_ratio(Settings::get().get_split_ratio());
    app.restoreSplitIds[0] = Settings::get().get_split_pane(0);
    app.restoreSplitIds[1] = Settings::get().get_split_pane(1);
    app.restoreFocusedPane = Settings::get().get_split_focused_pane();
    // Back-compat: if no tab set persisted, fall back to last_session.
    if (app.restoreTabIds.empty()) {
        const std::string& last = Settings::get().get_last_session();
        if (!last.empty()) {
            app.restoreTabIds.push_back(last);
            app.restoreActiveId = last;
        }
    }

    // WARM-OPEN the restored active thread from the disk cache RIGHT NOW,
    // before the first frame, so a relaunch paints the cached conversation
    // instead of flashing the Home screen until the network list/transcript
    // arrive (Gabe: "it defaults to the last thread but shows Home until it
    // loads — looks like a bug"). We're already on that thread (selectedId), so
    // show its content. Only when the http cache has a stored transcript for
    // it; otherwise leave the normal async open path (TabFlowSystem restores
    // the tab once the list loads). Guarded to the http backend (the mock
    // doesn't cache).
    if (!app.restoreActiveId.empty() && app.backend_label == "http") {
        if (auto cached = api::disk_cache::load_transcript(app.restoreActiveId)) {
            app.pane().openSession = std::move(*cached);
            app.pane().note_transcript_reset();
            app.pane().selectedId = app.restoreActiveId;
            app.pane().transcriptState = ecs::LoadState::Loaded;
            app.view = ecs::SmartView::Chat;  // paint the transcript, not Home
            // A background refresh still runs (requestListRefresh below +
            // TabFlowSystem's tab restore), swapping in fresh data when it lands.
            app.transcriptCache.put(*app.pane().openSession);
        }
    }

    for (const auto& key : Settings::get().get_collapsed_shelves())
        app.collapsedShelves.insert(key);

    // Layout singleton.
    auto& layoutEntity = EntityHelper::createEntity();
    auto& layoutComp = layoutEntity.addComponent<ecs::LayoutComponent>();
    // Restore the persisted sidebar fold state (survives relaunch — see
    // Settings::get_sidebar_collapsed). The toggle writes it back on flip.
    layoutComp.sidebarCollapsed = Settings::get().get_sidebar_collapsed();

    // Tab strip singleton.
    auto& stripEntity = EntityHelper::createEntity();
    stripEntity.addComponent<ecs::TabStripComponent>();

    Settings::get().auto_save_enabled = true;
}

// Register the full system pipeline into a SystemManager. Shared by the
// windowed and headless (screenshot) paths so both render identically.
static void build_systems(afterhours::SystemManager& sm) {
    using namespace afterhours;

    // CRITICAL: populate the InputCollector each frame. add_singleton_components
    // (preload) only ADDS the collector component; the InputSystem that FILLS it
    // (raw keys -> mapped actions: TextBackspace, WidgetPress/Enter, etc.) is a
    // SEPARATE registration that was never made — so ctx.pressed(WidgetPress)
    // and pressed_or_repeat(TextBackspace) never fired. Result: Enter didn't
    // send and Backspace didn't delete (typing worked because that's the char
    // queue, a different path). Must run BEFORE the UI pre-layout bridge, which
    // READS the collector into the UIContext.
    afterhours::input::register_update_systems(sm);

    ui_imm::registerUIPreLayoutSystems(sm);

    // Ahead of every `mk()` in the frame, and after the bridge that clears the
    // children lists: the epoch this system opens is what every widget built
    // below is stamped with, and the sweep it runs first retires the widgets
    // of screens nothing has built for a while. src/ui/widget_epoch.h.
    sm.register_update_system(std::make_unique<ecs::WidgetRetireSystem>());

    // Data + layout must run before UI-creating systems.
    sm.register_update_system(std::make_unique<ecs::TabFlowSystem>());
    sm.register_update_system(std::make_unique<ecs::LoaderSystem>());
    sm.register_update_system(std::make_unique<ecs::LayoutSystem>());

    // Ahead of every UI-creating system: a rotation lands the new palette
    // before this frame's widgets read their colours, so a flip is never half
    // a frame old.
    sm.register_update_system(std::make_unique<ecs::ThemeRotationSystem>());
    sm.register_update_system(std::make_unique<ecs::commands::System>());

    // Resolve Esc before anything reads it, so one keystroke means one thing.
    sm.register_update_system(std::make_unique<ecs::EscapeSystem>());
    // Same for Up/Down, which the composer, the transcript and the lists all
    // want.
    sm.register_update_system(std::make_unique<ecs::ArrowSystem>());
    // And for the focus ring: decided once, ahead of every widget, so nothing
    // renders a ring the keyboard has not earned (ui/focus_visible.h).
    sm.register_update_system(std::make_unique<ecs::FocusVisibleSystem>());
    // Cmd+Backspace turns into a selection here, and the focused field's own
    // Backspace deletes it later this frame -- so this has to be ahead of the
    // UI systems that build the fields (text_edit_chords_system.h).
    sm.register_update_system(std::make_unique<ecs::TextEditChordsSystem>());
    // Pasted / dropped images become composer attachments here, before the UI
    // systems: MainPaneSystem reserves the composer strip from that list.
    sm.register_update_system(std::make_unique<ecs::AttachmentIntakeSystem>());

    // UI-creating systems (draw order: later on top).
    sm.register_update_system(std::make_unique<ecs::SidebarSystem>());
    sm.register_update_system(std::make_unique<ecs::MainPaneSystem>());
    sm.register_update_system(std::make_unique<ecs::TabBarSystem>());
    sm.register_update_system(std::make_unique<ecs::SettingsSystem>());
    sm.register_update_system(std::make_unique<ecs::ShortcutsSystem>());
    sm.register_update_system(std::make_unique<ecs::ComposerSystem>());
    sm.register_update_system(std::make_unique<ecs::PaletteSystem>());
    sm.register_update_system(std::make_unique<ecs::SessionSearchSystem>());
    sm.register_update_system(std::make_unique<ecs::RenameModalSystem>());
    sm.register_update_system(std::make_unique<ecs::ToastSystem>());
    // Auth overlay draws on top of everything (login gates the app). No-op
    // unless AppComponent::showAuth is true, so it costs nothing when auth is
    // not configured.
    sm.register_update_system(std::make_unique<ecs::AuthSystem>());

    // Post-layout (autolayout, interactions).
    ui_imm::registerUIPostLayoutSystems(sm);

    // Render systems.
    sm.register_render_system(std::make_unique<MainRenderSystem>());
    // Hanabi-side eased scrolling: runs BEFORE the UI render systems read
    // scroll_offset, gliding the offset toward the wheel's destination so
    // scrolling isn't stepped/"chunky" (works against pinned afterhours; idles
    // if the vendor smooth-scroll patch is present). No-op with
    // HANABI_SCROLL_SMOOTH=1. Safe with the transcript follow-latch (a pin to
    // the end is detected as a snap, not eased).
    ui_imm::registerUIRenderSystems(sm);
#ifdef AFTER_HOURS_ENABLE_E2E_TESTING
    sm.register_render_system(
        std::make_unique<hanabi::a11y::RegisterAccessibleNames>());
#endif
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

    Preload::get().init(product_branding::kAppName);
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

static void record_idle_diagnostic(bool rendered, std::uint64_t nowUs) {
    static const int seconds = [] {
        const char* value = std::getenv("HANABI_IDLE_DIAG_SECS");
        return value != nullptr && *value != '\0' ? std::atoi(value) : 0;
    }();
    if (seconds <= 0) return;

    static const std::uint64_t startUs = nowUs;
    static const unsigned long long startCpu = hanabi::prof::cpu_nanos();
    static const unsigned long long startAllocs = hanabi::prof::alloc_count();
    static unsigned long long callbacks = 0;
    static unsigned long long frames = 0;
    ++callbacks;
    if (rendered) ++frames;

    const double elapsed = static_cast<double>(nowUs - startUs) / 1000000.0;
    if (elapsed < static_cast<double>(seconds)) return;
    const double cpuMs = static_cast<double>(hanabi::prof::cpu_nanos() - startCpu) /
                         1000000.0;
    const unsigned long long allocations =
        hanabi::prof::alloc_count() - startAllocs;
    std::printf("IdleActivity: wall=%.3fs callbacks=%llu frames=%llu "
                "cpu_ms_per_sec=%.3f allocs_per_sec=%.1f "
                "cpu_ms_per_frame=%.4f allocs_per_frame=%.1f\n",
                elapsed, callbacks, frames, cpuMs / elapsed,
                static_cast<double>(allocations) / elapsed,
                frames == 0 ? 0.0 : cpuMs / static_cast<double>(frames),
                frames == 0 ? 0.0
                            : static_cast<double>(allocations) /
                                  static_cast<double>(frames));
    std::fflush(stdout);
    afterhours::graphics::request_quit();
}

static void app_frame() {
    // Every frame gets a pool, because Metal hands back autoreleased objects
    // and a render loop is not a Cocoa run loop: six per frame, ~2.5 KB, never
    // freed. See util/autorelease.h for the measurement.
    const hanabi::AutoreleaseFrame pool;
    // Phase G: the menu-bar extra installs on the FIRST windowed frame — by
    // now sokol has created NSApp + the window (it may not exist yet at
    // app_init). Idempotent + windowed-only: run_headless_screenshot never
    // reaches here, so no status item leaks into a capture. Guarded so we only
    // attempt install once (menubar_install is itself idempotent regardless).
    static bool menubarInstalled = false;
    if (!menubarInstalled) {
        menubar_install();
        if (const char* v = std::getenv("HANABI_NATIVE_MENU_DIAGNOSTIC");
            v && *v && std::string(v) != "0") {
            char status[512] = {};
            menubar_diagnostics(status, sizeof(status));
            std::fprintf(stderr, "[HANABI_NATIVE_MENU] %s\n", status);
        }
        // Phase G extras: register the global hotkey (Cmd+Shift+N) on the same
        // windowed-only, install-once path. Idempotent; headless never reaches
        // here so no global listener lingers after a --screenshot capture.
        native_hotkey_install();
        native_notifications_start();
        // Phase G extra: install the hanabi:// URL / Apple-event handler so a
        // tapped Spotlight result (hanabi://thread/<id>) opens that thread.
        // Windowed-only + install-once, same as the hotkey.
        native_openurl_install();
        metal_frame_activity_install();
        menubarInstalled = true;
        // Ensure the window fits ON-SCREEN: a restored/dragged frame taller
        // than the display pushes the bottom (composer + status bar) below the
        // visible area — the composer renders but is unreachable ("how do I
        // send a message"). Constrain once on the first frame.
        metal_constrain_window_to_screen();
        // Diagnostic (windowed-only, fires once): HANABI_NOTIFY_TEST=<thread-id>
        // posts a single native notification carrying that id, so the
        // notification banner + click->open-thread path can be exercised
        // manually. Ignored when unset; never runs on the headless path.
        if (const char* nt = std::getenv("HANABI_NOTIFY_TEST"); nt && *nt) {
            const std::string title =
                std::string(product_branding::kAppName) + ": thread needs you";
            native_notify(title.c_str(), "Click to open this thread", nt,
                          Settings::get().get_notification_sound());
        }
        if (const char* sid = std::getenv("HANABI_SPOTLIGHT_TEST"); sid && *sid) {
            if (std::string(sid) == "clear") {
                native_spotlight_sync(nullptr, 0);
            } else {
                const std::string url =
                    std::string(product_branding::kUrlScheme) + "://thread/" +
                    hanabi::spotlight::path_segment(sid);
                const std::string title =
                    std::string(product_branding::kAppName) +
                    " Spotlight verification";
                const NativeSpotlightItem item{
                    sid, title.c_str(), "Local-only packaged-app indexing check",
                    url.c_str()};
                native_spotlight_sync(&item, 1);
            }
        }
    }

    // Drain menu-bar action flags into ECS state (single-owner: only the frame
    // loop mutates AppComponent). Show brings the window front; New task opens
    // the composer (via requestNewTask, mirrored below into composerOpen).
    // The global hotkey (Cmd+Shift+N) folds into the SAME activate+new-task
    // path, so a press behaves exactly like the "New task" menu item.
    bool nativeWake = false;
    {
        bool hotkey = native_hotkey_take_triggered();
        const bool paletteHotkey = native_palette_hotkey_take_triggered();
        bool wantShow = menubar_take_show() || hotkey || paletteHotkey;
        bool wantNewTask = menubar_take_new_task() || hotkey;
        nativeWake = hotkey || paletteHotkey || wantShow || wantNewTask;
        if (wantShow) metal_activate_app();
        if (wantNewTask) {
            auto q = afterhours::EntityQuery({.force_merge = true})
                         .whereHasComponent<ecs::AppComponent>()
                         .gen();
            if (!q.empty()) q[0].get().get<ecs::AppComponent>().requestNewTask = true;
        }
        if (paletteHotkey) {
            auto q = afterhours::EntityQuery({.force_merge = true})
                         .whereHasComponent<ecs::AppComponent>()
                         .gen();
            if (!q.empty())
                q[0].get().get<ecs::AppComponent>().paletteOpen = true;
        }
        // Phase G extra: a hanabi://thread/<id> open (tapped Spotlight result)
        // opens + navigates to that thread — same seam a sidebar row click
        // uses (requestOpenTab), so the tab loader fetches + focuses it.
        char openId[256];
        if (native_take_open_thread(openId, sizeof(openId))) {
            nativeWake = true;
            auto q = afterhours::EntityQuery({.force_merge = true})
                         .whereHasComponent<ecs::AppComponent>()
                         .gen();
            if (!q.empty()) {
                q[0].get().get<ecs::AppComponent>().requestOpenTab = openId;
                metal_activate_app();
            }
        }
    }

    // The window has to exist before it can be a drag destination, and on the
    // very first frame it may not — so this is asked every frame rather than
    // once (it early-returns the moment it is installed). The drops it lets in
    // are drained by AttachmentIntakeSystem, which runs in the scripted-UI
    // loop as well as this one.
    native_filedrop_install();
    nativeWake = nativeWake || native_dropped_image_pending() ||
                 menubar_command_pending() ||
                 menubar_recorded_shortcut_pending();

    static hanabi::FrameActivityPolicy framePolicy = [] {
        const char* disabled = std::getenv("HANABI_IDLE_DISABLE");
        return hanabi::FrameActivityPolicy(
            disabled == nullptr || *disabled == '\0' ||
            std::string_view(disabled) == "0");
    }();
    const auto now = std::chrono::steady_clock::now();
    const auto nowUs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch())
            .count());
    hanabi::FrameSignals frameSignals;
    const unsigned inputActivity = frame_input_activity();
    frameSignals.pointer_input = (inputActivity & 1u) != 0;
    frameSignals.key_input = (inputActivity & 2u) != 0;
    const unsigned windowActivity = metal_take_window_activity();
    frameSignals.window_resize = (windowActivity & 1u) != 0;
    frameSignals.window_exposure = (windowActivity & 2u) != 0;
    frameSignals.native_notification = nativeWake;

    auto appQuery = afterhours::EntityQuery({.force_merge = true})
                        .whereHasComponent<ecs::AppComponent>()
                        .gen();
    if (!appQuery.empty()) {
        auto& app = appQuery[0].get().get<ecs::AppComponent>();
        hanabi::FrameSignals appSignals = hanabi::collect_app_frame_signals(app);
        frameSignals.pointer_input = frameSignals.pointer_input ||
                                     appSignals.pointer_input;
        frameSignals.key_input = frameSignals.key_input || appSignals.key_input;
        frameSignals.native_notification = frameSignals.native_notification ||
                                           appSignals.native_notification;
        frameSignals.async_ready = appSignals.async_ready;
        frameSignals.state_request = appSignals.state_request;
        frameSignals.split_change = appSignals.split_change;
        frameSignals.animation = appSignals.animation;
        frameSignals.streaming = appSignals.streaming;
        frameSignals.thinking = appSignals.thinking;
        frameSignals.scrolling = appSignals.scrolling;
        frameSignals.dragging = appSignals.dragging;
        frameSignals.caret = appSignals.caret;
        frameSignals.timer = appSignals.timer;
        frameSignals.pending_future = appSignals.pending_future;
        static long long lastEventMs = 0;
        const long long eventMs = app.lastEventMs.load();
        frameSignals.sse_event = eventMs != 0 && eventMs != lastEventMs;
        lastEventMs = eventMs;
    }
    hanabi::collect_ui_frame_signals(frameSignals);

    static const bool fixedTenFps = [] {
        const char* value = std::getenv("HANABI_IDLE_FIXED_10FPS");
        return value != nullptr && *value != '\0' &&
               std::string_view(value) != "0";
    }();
    if (fixedTenFps) {
        frameSignals = {};
        frameSignals.timer = true;
    }

    const hanabi::FrameDecision frameDecision =
        framePolicy.decide(nowUs, frameSignals);
    record_idle_diagnostic(frameDecision.render, nowUs);
    if (!frameDecision.render) return;

    float dt = afterhours::graphics::get_frame_time();
    if (framePolicy.started()) {
        dt = static_cast<float>(nowUs - framePolicy.last_frame_us()) / 1000000.0f;
        dt = std::min(dt, 0.1f);
    }
    framePolicy.rendered(nowUs);
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
    // HANABI_GPU_WATCH=<n>: print the device's GPU byte total every n frames of
    // the WINDOWED loop. The only memory instrument that runs on this path --
    // docs/perf/GATES.md's "Anything only the windowed app does" says every
    // gate in this repo is headless, and a resize, a menu, a hotkey and the
    // window-restore path are invisible to all of them. This does not gate
    // anything; it is the one command that answers "does dragging the real
    // window leak?" without a profiler. See afterhours_gaps.md #200.
    {
        static const int watch = [] {
            const char* v = std::getenv("HANABI_GPU_WATCH");
            return (v != nullptr && *v != '\0') ? std::atoi(v) : 0;
        }();
        // HANABI_GPU_WATCH_RESIZE=1 also DRAGS the window, every watch frames,
        // through metal_set_window_size -- the same NSWindow frame change a
        // person's drag makes, and the same one set_window_size's windowed
        // branch calls. That is the only way to answer #200 for a real window
        // from inside the process: osascript cannot resize it without
        // assistive access, which is not a permission to grant on somebody's
        // daily machine to settle a measurement.
        static const bool watchResize = [] {
            const char* v = std::getenv("HANABI_GPU_WATCH_RESIZE");
            return v != nullptr && *v != '\0' && std::string(v) != "0";
        }();
        if (watch > 0) {
            static long n = 0;
            if (watchResize && n > 0 && n % watch == 0) {
                static int step = 0;
                ++step;
                metal_set_window_size(900 + (step % 8) * 60,
                                      700 + (step % 5) * 50);
            }
            if (n++ % watch == 0) {
                const auto [w, h] = std::pair<int, int>{
                    static_cast<int>(afterhours::graphics::get_screen_width()),
                    static_cast<int>(afterhours::graphics::get_screen_height())};
                std::printf("[gpuwatch] frame %6ld  %dx%d  GPU %8llu KB  "
                            "images %zu (%zu KB)  poolFail %zu\n",
                            n - 1, w, h, hanabi::gpu::device_bytes() / 1024,
                            hanabi::inline_image::cached_count(),
                            hanabi::inline_image::cached_bytes() / 1024,
                            hanabi::decode_to_fit::pool_exhaustions());
                std::fflush(stdout);
            }
        }
    }

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
    // `ecs::model::in_blocked_view` is the ONE rule -- Blocked OR Failed, the
    // same predicate the sidebar's Blocked badge counts and Puffin's own
    // `case .blocked` filter uses. This read its own `tag == Blocked` until
    // feat/vis-statusmove, so the menu bar said 3 where the badge said 6.
    // menubar_set_blocked no-ops when unchanged.
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
            for (const auto& s : app.sessions)
                if (ecs::model::in_blocked_view(s)) ++blocked;
            menubar_set_blocked(blocked);

            if (app.listState == ecs::LoadState::Loaded &&
                app.backend_label != "mock" && app.backend_label != "none") {
                const auto catalog = hanabi::spotlight::make_catalog(app.sessions);
                const size_t sig = hanabi::spotlight::signature(catalog);
                static bool indexed = false;
                static size_t lastIndexSig = 0;
                if (!indexed || sig != lastIndexSig) {
                    std::vector<NativeSpotlightItem> native_items;
                    native_items.reserve(catalog.size());
                    for (const auto& item : catalog) {
                        native_items.push_back({item.id.c_str(), item.title.c_str(),
                                                item.preview.c_str(), item.url.c_str()});
                    }
                    native_spotlight_sync(native_items.data(),
                                          static_cast<int>(native_items.size()));
                    indexed = true;
                    lastIndexSig = sig;
                }
            }

            // Phase G extra: post a native notification when a thread's state
            // CHANGES to something worth interrupting for — it blocked on you,
            // or it finished. Per-thread, not per-count: the old count rule
            // stayed silent when one thread unblocked as another blocked, which
            // is exactly the busy afternoon you most want telling about.
            //
            // A thread seen for the first time never notifies, so launching
            // into a blocked inbox is silent and a list refresh that returns a
            // week-old thread does not read as news. Rate-limited to one banner
            // per kNotifyMinGapSecs; blocked outranks finished when both happen
            // in the same refresh. Windowed path only — run_headless_screenshot
            // never reaches app_frame, so a --screenshot run never posts one.
            //
            // Quiet hours suppress the BANNER but not the tracking: the
            // snapshot still advances, so the morning does not open with a
            // notification about something that happened at 3am.
            static hanabi::notify::Snapshot lastSeen;
            static double lastNotifyAt = -1.0;
            constexpr double kNotifyMinGapSecs = 30.0;

            std::vector<std::pair<std::string, hanabi::notify::Activity>> now;
            std::map<std::string, std::string> titles;
            // Muted threads are still gathered and still snapshotted below —
            // they are dropped inside transitions(), so silence now cannot
            // become a stale banner the moment the thread is unmuted.
            std::set<std::string> muted;
            now.reserve(app.sessions.size());
            for (const auto& s : app.sessions) {
                if (s.id.empty()) continue;
                auto activity = hanabi::notify::Activity::Other;
                if (s.tag == api::ThreadTag::Blocked)
                    activity = hanabi::notify::Activity::Blocked;
                else if (s.tag == api::ThreadTag::Done)
                    activity = hanabi::notify::Activity::Finished;
                now.emplace_back(s.id, activity);
                titles[s.id] = s.title;
                if (s.muted) muted.insert(s.id);
            }

            const auto event =
                hanabi::notify::native_event(lastSeen, now, titles, muted);
            if (event.has_value() && !in_quiet_hours_now()) {
                const double nowSec =
                    static_cast<double>(now_epoch_seconds());
                if (lastNotifyAt < 0.0 ||
                    nowSec - lastNotifyAt >= kNotifyMinGapSecs) {
                    const bool isBlocked =
                        event->kind == hanabi::notify::Event::Kind::Blocked;
                    // Carry the thread's id so CLICKING the notification opens
                    // it (native_extras routes it through the same open-thread
                    // slot the deep-link uses).
                    native_notify(
                        isBlocked ? "A thread needs you" : "A run finished",
                        event->title.c_str(), event->id.c_str(),
                        Settings::get().get_notification_sound());
                    lastNotifyAt = nowSec;
                }
            }
            lastSeen = hanabi::notify::snapshot(now);
        }
    }
}

static void persist_app_state() {
    using namespace afterhours;
    // Persist the open tab set + active tab for next launch.
    auto stripQ = EntityQuery({.force_merge = true})
                      .whereHasComponent<ecs::TabStripComponent>()
                      .gen();
    if (!stripQ.empty()) {
        auto& strip = stripQ[0].get().get<ecs::TabStripComponent>();
        std::vector<std::string> ids;
        std::vector<std::string> pinned;
        std::string active;
        for (auto tabId : strip.tabOrder) {
            auto o = EntityHelper::getEntityForID(tabId);
            // Preview tabs are not restored: a thread you glanced at once is
            // not somewhere you asked to come back to.
            if (o.valid() && o->has<ecs::Tab>() &&
                o->get<ecs::Tab>().keptOpen) {
                ids.push_back(o->get<ecs::Tab>().sessionId);
                if (o->get<ecs::Tab>().pinned)
                    pinned.push_back(o->get<ecs::Tab>().sessionId);
                if (o->has<ecs::ActiveTab>())
                    active = o->get<ecs::Tab>().sessionId;
            }
        }
        Settings::get().set_open_tabs(std::move(ids), active,
                                      std::move(pinned));
    }

    auto q = EntityQuery({.force_merge = true})
                 .whereHasComponent<ecs::AppComponent>()
                 .gen();
    if (!q.empty()) {
        auto& app = q[0].get().get<ecs::AppComponent>();
        if (!app.pane().selectedId.empty())
            Settings::get().set_last_session(app.pane().selectedId);
        // The split, the divider and what each pane held. Written HERE rather
        // than from the drag, for the same reason the tab set is: a divider
        // drag is sixty writes a second and this file is fully re-serialised
        // on every one of them. Settings::set_split also refuses a write that
        // changes nothing, so a launch that never splits never touches it.
        Settings::get().set_split(app.splitOpen, app.splitRatio,
                                  app.panes[0].selectedId,
                                  app.panes[1].selectedId, app.focusedPane);
    }
    Settings::get().set_theme(theme::mode() == theme::Mode::Light ? "light"
                                                                  : "dark");
    Settings::get().write_save_file();
}

static void app_cleanup() {
    persist_app_state();

    // FAST, non-hanging quit. AppComponent holds ~9 std::future<>s from
    // std::async (transcript/list/send/stream/steer/split/settings-sync). A
    // future returned by std::async BLOCKS in its destructor until the worker
    // thread finishes — so if a network fetch is in flight against the real
    // https backend when the window closes, the normal teardown (static dtors
    // after main() returns) would hang waiting on that socket (Gabe: "closing
    // the window takes forever, it just hangs and freezes mid close"). We've
    // already persisted everything durable above, so there is nothing left to
    // flush — terminate the process immediately and let the OS reclaim the
    // threads/sockets/memory instead of blocking on future destructors.
    std::fflush(nullptr);
    std::_Exit(0);
}

#ifdef AFTER_HOURS_ENABLE_E2E_TESTING
#include "../vendor/afterhours/src/plugins/e2e_testing/e2e_testing.h"
#include "../vendor/afterhours/src/plugins/e2e_testing/platform_test_input.h"
#include "../vendor/afterhours/src/plugins/e2e_testing/ui_commands.h"
#include "ecs/e2e_commands.h"
#endif

// State-only test knobs, shared by the two headless entry points (the
// screenshot capture and the scripted-UI runner) so a state reachable in one is
// reachable in the other. Every one is ignored when its variable is unset, does
// no network, and only writes app state — the knobs that need frames pumped
// through them stay with the capture path that can pump them.
// Knobs that ask the LOADER for something and therefore need frames pumped
// afterwards. Split from apply_test_knobs so each headless entry point can run
// them before its own settle loop rather than growing a pump of its own.
static void request_test_opens(ecs::AppComponent* app) {
    if (app == nullptr) return;
    // HANABI_OPEN=<id> opens a specific thread (the long perf fixture "rbig",
    // say). Sets requestOpenTab; the caller's settle frames land it.
    if (const char* oid = std::getenv("HANABI_OPEN"); oid && *oid) {
        app->requestOpenTab = oid;
        app->view = ecs::SmartView::Chat;
    }
    // HANABI_SPLIT=<id> opens a SECOND thread in the right pane. Needs
    // HANABI_OPEN to have named the primary.
    if (const char* sid = std::getenv("HANABI_SPLIT"); sid && *sid)
        app->requestSplitOpen = sid;
}

// HANABI_THINK_DEMO=1 forces the live "thinking" indicator (pulsing dot +
// Thinking… + timer) into the open thread so it can be captured and scripted.
//
// Idempotent ON PURPOSE, and that is the whole fix: the loader finishes its
// async transcript fetch during the settle frames and REPLACES openSession,
// which used to drop the injected live message and take the indicator with it
// — the 26_thinking_dark screenshot has been photographing a thread with no
// indicator in it. Re-applying after the settle is what makes the knob honest,
// so it must be safe to call twice.
static void apply_stream_demo(ecs::AppComponent* app) {
    if (app == nullptr || !app->pane().openSession) return;
    const char* th = std::getenv("HANABI_THINK_DEMO");
    if (!(th && *th && std::string(th) != "0")) return;

    auto& msgs = app->pane().openSession->messages;
    if (msgs.empty() || msgs.back().id != "__thinking_demo__") {
        api::Message live;
        live.role = api::Role::Assistant;
        live.id = "__thinking_demo__";
        live.text = "";
        const std::size_t first = msgs.size();
        msgs.push_back(live);
        app->pane().note_transcript_append(first, 1);
    }
    app->streamActive = true;
    app->streamSessionId = app->pane().openSession->summary.id;
    app->streamPaneIndex = app->focusedPane;
    app->streamMsgIndex = msgs.size() - 1;
    app->pane().selectedId = app->pane().openSession->summary.id;
    app->streamPhase = ecs::AppComponent::StreamPhase::Thinking;
    // Without this the loader finds a stream with no chunks left and finishes
    // it on the next tick, one frame after the knob runs.
    app->streamDemoHold = true;
    // Re-stamped on every application so the timer reads the same 32s however
    // many times this runs — a drifting number would rot every baseline. The
    // capture clock is what makes the reading exact: the renderer subtracts
    // this stamp from ITS reading of now, and both are the same frozen instant.
    app->streamStartedAt = capture_clock::now() - 32;
    app->view = ecs::SmartView::Chat;
}

static void apply_loading_demo(ecs::AppComponent* app) {
    if (app == nullptr) return;
    const char* d = std::getenv("HANABI_LOADING_DEMO");
    if (!(d && *d && std::string(d) != "0")) return;
    app->view = ecs::SmartView::Chat;
    app->pane().transcriptState = ecs::LoadState::Loading;
    app->pane().transcriptLoadingId = "__loading_demo__";
}

static void apply_test_knobs(ecs::AppComponent* app) {
    if (app == nullptr) return;
    apply_stream_demo(app);
    // Screenshot affordance: HANABI_VIEW=blocked|review|starred|home forces
    // the landing smart-view so a headless capture can photograph any view
    // (including an empty one) without a click. Set AFTER the wait loop so a
    // restored tab's auto-open (which sets view=Chat) can't clobber it; we
    // also drop the selection so the smart-view — not a stale transcript —
    // renders. Ignored when unset.
    if (const char* v = std::getenv("HANABI_VIEW")) {
        std::string vs(v);
        ecs::SmartView sv = app->view;
        bool set = true;
        if (vs == "blocked") sv = ecs::SmartView::Blocked;
        else if (vs == "review") sv = ecs::SmartView::Review;
        else if (vs == "starred") sv = ecs::SmartView::Starred;
        else if (vs == "home") sv = ecs::SmartView::Home;
        else if (vs == "archived") sv = ecs::SmartView::Archived;
        else if (vs == "chat") sv = ecs::SmartView::Chat;  // welcome/no-thread
        else set = false;
        if (set) {
            app->view = sv;
            app->pane().selectedId.clear();
            if (sv == ecs::SmartView::Chat) app->pane().openSession.reset();
        }
    }

    // Screenshot affordance: HANABI_TEST_OVERLAY selects an otherwise
    // keyboard- or pointer-only secondary surface for deterministic capture.
    if (const char* ov = std::getenv("HANABI_TEST_OVERLAY"); ov && *ov) {
        std::string os(ov);
        if (os == "settings") app->showSettings = true;
        else if (os == "composer") app->composerOpen = true;
        else if (os == "shortcuts") app->showShortcuts = true;
        else if (os == "shortcuts-recording") {
            app->showShortcuts = true;
            app->shortcutRecording = static_cast<int>(
                hanabi::shortcuts::Command::OpenPalette);
            app->shortcutMessage =
                "Press a shortcut with Command. Escape cancels.";
        }
        else if (os == "find") app->pane().findOpen = true;
        else if (os == "palette") app->paletteOpen = true;
        else if (os == "search") app->sessionSearchOpen = true;
        else if (os == "model") app->modelPopoverOpen = true;
        else if (os == "effort") app->effortPopoverOpen = true;
        else if (os == "slash") {
            const std::string id = app->pane().openSession
                                       ? app->pane().openSession->summary.id
                                       : std::string("__kickoff__");
            ecs::model::pane_states()
                .touch(ecs::model::pane_key(app->focusedPane, id))
                .replyDraft = "/";
        }
        else if (os == "toast")
            app->raise_toast("Conversation archived", "t2",
                             ecs::AppComponent::ToastUndo::Archive);
        else if (os == "row-menu") {
            app->rowMenuOpen = true;
            app->rowMenuSessionId = "t2";
            app->rowMenuX = 320.0f;
            app->rowMenuY = 300.0f;
        }
        else if (os == "tab-menu") {
            auto tabs = afterhours::EntityQuery({.force_merge = true})
                            .whereHasComponent<ecs::TabStripComponent>()
                            .gen();
            if (!tabs.empty()) {
                auto& strip = tabs[0].get().get<ecs::TabStripComponent>();
                if (!strip.tabOrder.empty()) {
                    strip.menuOpen = true;
                    strip.menuTabId = strip.tabOrder.front();
                    strip.menuX = 360.0f;
                    strip.menuY = 88.0f;
                }
            }
        }
    }
    if (const char* d = std::getenv("HANABI_PALETTE_DEMO"); d && *d) {
        app->paletteOpen = true;
        app->paletteQuery = d;
    }
    // Screenshot affordance: HANABI_UNREAD_DEMO=<n> marks the open thread as
    // last read just before its Nth-from-last message, so the "new messages"
    // divider can be captured and tested. Writes the same persisted stamp a
    // real read would; the isolated HOME every harness uses keeps it out of a
    // real settings file.
    if (const char* u = std::getenv("HANABI_UNREAD_DEMO"); u && *u) {
        const int back = std::atoi(u);
        if (app->pane().openSession && back > 0) {
            const auto& ms = app->pane().openSession->messages;
            if (static_cast<int>(ms.size()) > back) {
                const int64_t at = ms[ms.size() - back - 1].created_at;
                if (at > 0)
                    Settings::get().set_last_read(app->pane().openSession->summary.id,
                                                  at);
            }
        }
    }

    // Screenshot affordance: HANABI_CONTEXT_USAGE=<used>[,stale] stands in for
    // the token accounting agentcloud sends on attach, so the composer's
    // proportion bar and its stale marker can be captured and tested against
    // the mock. The DENOMINATOR is not here — that is the real config key
    // (HANABI_CONTEXT_BUDGET_TOKENS); this supplies only the counted numerator
    // a mock session has no way to know.
    if (const char* u = std::getenv("HANABI_CONTEXT_USAGE"); u && *u) {
        if (app->pane().openSession) {
            const std::string spec(u);
            const size_t comma = spec.find(',');
            app->pane().openSession->context.used_tokens =
                std::atoll(spec.substr(0, comma).c_str());
            app->pane().openSession->context.stale =
                comma != std::string::npos && spec.substr(comma + 1) == "stale";
        }
    }

    // Screenshot affordance: HANABI_SELECT_DEMO=<text> pre-selects a run of
    // the open thread's first assistant message, so the selection band can be
    // photographed without a live drag. Render-only.
    if (const char* d = std::getenv("HANABI_SELECT_DEMO"); d && *d) {
        app->selectDemo = d;
    }

    // Screenshot affordance: HANABI_XSEARCH_DEMO=<text> opens the
    // across-threads search with a query already typed, so its results, its
    // snippets and — the point of the panel — the line admitting how much of
    // your history it could actually read can be photographed and scripted.
    // The chord that really opens it is Cmd+Shift+F, which no script can press
    // (afterhours_gaps.md #49). Render-only; reads the local cache, no network.
    if (const char* d = std::getenv("HANABI_XSEARCH_DEMO"); d && *d) {
        app->sessionSearchOpen = true;
        app->sessionSearchQuery = d;
    }

    // Screenshot affordance: HANABI_FIND_DEMO=<text> opens find-in-conversation
    // with a query already typed, so the match highlighting and the "N of M"
    // tally can be photographed. Render-only; no network.
    if (const char* d = std::getenv("HANABI_FIND_DEMO"); d && *d) {
        app->pane().findOpen = true;
        app->pane().findQuery = d;
    }
    if (const char* d = std::getenv("HANABI_TRANSCRIPT_ERROR_DEMO"); d && *d) {
        app->pane().transcriptState = ecs::LoadState::Error;
        app->pane().transcriptError = d;
    }

    // Screenshot affordance: HANABI_SKELETON_DEMO=1 forces the cold-cache
    // loading state (empty list + Loading) so the skeleton placeholder rows
    // can be photographed headlessly — the harness otherwise waits PAST
    // Loading and would never capture it. No network; render-only.
    if (const char* d = std::getenv("HANABI_SKELETON_DEMO"); d && *d &&
        std::string(d) != "0") {
        app->sessions.clear();
        app->listState = ecs::LoadState::Loading;
        app->view = ecs::SmartView::Home;
        app->pane().selectedId.clear();
    }

    // Screenshot affordance: HANABI_LOADING_DEMO=1 forces the per-thread
    // transcript switch spinner (transcriptState=Loading + a mismatched
    // transcriptLoadingId, so the pane shows the "Loading conversation…"
    // ring instead of stale/blank content). Render-only; no network.
    apply_loading_demo(app);
    // Screenshot affordance: HANABI_OLDER_DEMO=1 forces the top
    // "loading older messages…" pill (loadingOlder=true) over the open
    // transcript, so a headless capture can photograph it. Render-only.
    if (const char* d = std::getenv("HANABI_OLDER_DEMO"); d && *d &&
        std::string(d) != "0") {
        app->pane().loadingOlder = true;
    }
    // Screenshot affordance: HANABI_KICKOFF_DEMO=<text> fires the Home
    // landing composer's kickoff ONCE (create_session for a NEW thread),
    // proving the full Home -> type -> new tab -> Chat flow headlessly. The
    // loader creates the session, opens it in a tab, and switches to Chat —
    // exactly as a real Send/Enter from the landing composer would. Mock
    // backend generates the session; no real network.
    if (const char* d = std::getenv("HANABI_KICKOFF_DEMO"); d && *d &&
        std::string(d) != "0") {
        app->view = ecs::SmartView::Home;
        app->requestKickoffPrompt = d;
    }
}

// ---------------------------------------------------------------------------
// The memory ladder (HANABI_MEMLADDER=1). See util/mem_ladder.h for why this
// exists and how to read what it prints; this is only the driver — the rungs,
// in the order a person climbs them.
//
// It runs INSIDE the headless capture path because that is the only place the
// app can be driven a step at a time: the windowed path hands the loop to the
// platform and never gives it back, and a rung is precisely "do one thing,
// then read". Nothing here draws to a file; the process exits after the table.
// ---------------------------------------------------------------------------

// What the app is holding right now, by container. The point of naming them
// individually is that a teardown rung that does not return its bytes is a
// question ("what is still held?") until this line answers it.
static std::string hold_note(const ecs::AppComponent& app) {
    char buf[768];
    std::size_t findEntries = 0;
    for (const auto& pane : app.panes) findEntries += pane.findMemo.entries();
    std::size_t tabs = 0;
    auto tabsQ = afterhours::EntityQuery({.force_merge = true})
                     .whereHasComponent<ecs::Tab>()
                     .gen();
    tabs = tabsQ.size();
    // GPU bytes are reported as the device's own total, because the estimate
    // beside it is an estimate: nothing in afterhours will say how many bytes
    // a texture is (afterhours_gaps.md #126), so the two columns are "what
    // hanabi believes it asked for" and "what the driver actually holds", and
    // the gap between them is the finding.
    char gpu[128];
    if (hanabi::gpu::device_accounting())
        std::snprintf(gpu, sizeof(gpu),
                      "gpu=%llu KB(ledger %zu KB) poolFail=%zu",
                      hanabi::gpu::device_bytes() / 1024,
                      hanabi::gpu::ledger_bytes() / 1024,
                      hanabi::decode_to_fit::pool_exhaustions());
    else
        std::snprintf(gpu, sizeof(gpu), "gpu=not measured poolFail=%zu",
                      hanabi::decode_to_fit::pool_exhaustions());
    std::snprintf(buf, sizeof(buf),
                  "sessions=%zu tabs=%zu lru=%zu paneStates=%zu(drafts %zu) "
                  "liveSubs=%zu find=%zu itemIndex=%zu/%zu outbox=%zu "
                  "sendQueue=%zu stream=%zu/%zuB entities=%zu "
                  "images=%zu(%zu KB) %s",
                  app.sessions.size(), tabs, app.transcriptCache.size(),
                  ecs::model::pane_states().size(),
                  ecs::model::pane_states().drafts(), app.liveSubs.size(),
                  findEntries, ecs::model::transcript_item_index().slots(),
                  ecs::model::transcript_item_index().total_items(),
                  app.outboxRetry.size(), app.pendingSendQueue.size(),
                  app.streamQueue.size() -
                      std::min(app.streamCursor, app.streamQueue.size()),
                  app.streamBuffer.size(),
                  afterhours::EntityHelper::get_entities().size(),
                  hanabi::inline_image::cached_count(),
                  hanabi::inline_image::cached_bytes() / 1024, gpu);
    return std::string(buf);
}

static int run_mem_ladder(afterhours::SystemManager& sm) {
    using namespace afterhours;

    auto* app = [] () -> ecs::AppComponent* {
        auto q = EntityQuery({.force_merge = true})
                     .whereHasComponent<ecs::AppComponent>()
                     .gen();
        return q.empty() ? nullptr : &q[0].get().get<ecs::AppComponent>();
    }();
    if (app == nullptr) {
        fprintf(stderr, "[ladder] no AppComponent\n");
        return 1;
    }
    auto strip_now = [] () -> ecs::TabStripComponent* {
        auto q = EntityQuery({.force_merge = true})
                     .whereHasComponent<ecs::TabStripComponent>()
                     .gen();
        return q.empty() ? nullptr : &q[0].get().get<ecs::TabStripComponent>();
    };

    const auto pump = [&sm](int n) {
        for (int i = 0; i < n; ++i) {
            const hanabi::AutoreleaseFrame framePool;
            graphics::begin_frame();
            graphics::clear_background(theme::window_bg());
            sm.run(1.0f / 60.0f);
            graphics::end_frame();
        }
    };

    hanabi::memladder::Ladder ladder(pump, [app] { return hold_note(*app); });
    const int want = hanabi::memladder::sessions();
    const int churn = hanabi::memladder::churn();

    ladder.adopt_floor("process floor, before graphics or app state");

    // The list fetch is requested by the AppComponent's initial state, so
    // clearing the request BEFORE the first frame is what makes an empty rung
    // possible at all. Everything else about the app is live.
    app->requestListRefresh = false;
    ladder.mark("window + systems built, no frame yet", 0);
    ladder.mark("+ the app running, empty: no catalog, no tab");

    // 1. The catalog.
    app->requestListRefresh = true;
    for (int guard = 0;
         guard < 300 && (app->sessions.empty() || app->listPending); ++guard)
        pump(1);
    ladder.mark("+ catalog loaded, sidebar drawing it");
    const size_t catalogSize = app->sessions.size();
    std::vector<std::string> ladderIds;
    ladderIds.reserve(app->sessions.size());
    for (const auto& session : app->sessions) {
        if (std::find(ladderIds.begin(), ladderIds.end(), session.id) ==
            ladderIds.end())
            ladderIds.push_back(session.id);
    }

    // 2. One thread. The first open pays for every lazy thing a transcript
    // touches, so it is its own rung — averaging it into the next one would
    // make the per-thread cost look several times larger than it is.
    const auto open_nth = [&](size_t i) {
        if (ladderIds.empty()) return;
        auto* strip = strip_now();
        if (strip == nullptr) return;
        ecs::model::open_session_in_tab(
            *strip, *app, ladderIds[i % ladderIds.size()], true);
        app->view = ecs::SmartView::Chat;
        pump(1);
        for (int guard = 0;
             guard < 7 &&
             (!app->pane().requestOpenId.empty() ||
              app->pane().transcriptPending || app->pane().diskReadPending);
             ++guard)
            pump(1);
    };
    open_nth(0);
    ladder.mark("+ the FIRST thread open");

    // 3. The rest, all left open, which is what a tab strip is.
    for (int k = 1; k < want; ++k) open_nth(static_cast<size_t>(k));
    ladder.mark("+ many threads open at once");

    if (app->sessions.size() > 1) {
        app->requestSplitOpen = app->sessions[1].id;
        pump(8);
    }
    for (std::size_t i = 0; i < app->active_pane_count(); ++i) {
        app->panes[i].findOpen = true;
        app->panes[i].findQuery = "retry";
    }
    app->outboxRetry.adopt("held-outbox", std::string(4096, 'o'));
    if (app->panes[0].openSession &&
        !app->panes[0].openSession->messages.empty()) {
        app->streamActive = true;
        app->streamDemoHold = true;
        app->streamSessionId = app->panes[0].selectedId;
        app->streamPaneIndex = 0;
        app->streamPhase = ecs::AppComponent::StreamPhase::Streaming;
        app->streamMsgIndex = app->panes[0].openSession->messages.size() - 1;
        app->streamBuffer.assign(64 * 1024, 's');
        app->streamQueue.assign(128, std::string(256, 'q'));
    }
    app->sessionSearchOpen = true;
    pump(static_cast<int>((catalogSize + hanabi::search::kDeepenPerFrame - 1) /
                          hanabi::search::kDeepenPerFrame) + 4);
    ladder.mark("+ two panes, find, index, outbox and streaming");

    app->sessionSearchOpen = false;
    for (auto& pane : app->panes) {
        pane.findOpen = false;
        pane.findQuery.clear();
        pane.findMemo.clear();
    }
    app->outboxRetry.restore({});
    app->streamActive = false;
    app->streamDemoHold = false;
    app->streamSessionId.clear();
    app->streamBuffer.clear();
    app->streamQueue.clear();
    app->requestSplitClose = true;
    pump(8);
    ladder.mark("- transient holders released");

    // 4. Scrolled: the dial that realizes rows the virtualiser had skipped.
    for (int k = 0; k < 240; ++k) {
        (void)hanabi::soak::scroll_named("transcript_scroll", k < 120 ? 24.0f : -24.0f);
        (void)hanabi::soak::scroll_named("sidebar_scroll", k < 120 ? 18.0f : -18.0f);
        pump(1);
    }
    ladder.mark("+ scrolled both panes end to end");

    // 5. Give it all back. This is the rung the whole ladder is built for: the
    // app is now in exactly the state rung 2 measured, so every byte above
    // that reading is held per thread opened.
    const auto close_all = [&] {
        for (int guard = 0; guard < 4096; ++guard) {
            auto* strip = strip_now();
            if (strip == nullptr || strip->tabOrder.empty()) break;
            ecs::model::close_tab(*strip, *app, strip->tabOrder.front(), 0,
                                  true);
            pump(1);
        }
        pump(4);
    };
    close_all();
    ladder.mark("- every tab closed, back to the empty app");

    // 6. Churn: open and close ONE at a time, `churn` times. Rung 5 answers
    // "what do N open tabs cost"; this answers the different and nastier
    // question "what does the Nth thread you ever opened cost after you closed
    // it" — the shape of the complaint, which is about an app that has been
    // running all day.
    for (int k = 0; k < churn; ++k) {
        // Ids the tab rung never touched, so this measures a thread the app
        // is seeing for the FIRST time — reopening the same eight would
        // measure a cache hit and report zero.
        open_nth(static_cast<size_t>(want + k));
        close_all();
    }
    // 7. Images. Every distinct path the composer has ever shown a chip for
    // becomes a GPU texture in hanabi::inline_image's cache. The attachment
    // LIST is capped at five; the cache behind it is not, and a chip removed
    // does not unload anything. HANABI_MEM_IMAGE_DIR=<dir> attaches every .png
    // in the directory ONE AT A TIME, clearing the list between, which is
    // exactly what a person pasting screenshots all day does.
    if (const char* dir = std::getenv("HANABI_MEM_IMAGE_DIR");
        dir != nullptr && *dir != '\0') {
        std::vector<std::string> pngs;
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec))
            if (e.path().extension() == ".png") pngs.push_back(e.path().string());
        std::sort(pngs.begin(), pngs.end());
        for (const std::string& png : pngs) {
            const size_t slash = png.find_last_of('/');
            app->composerAttachments.push_back(
                {png, slash == std::string::npos ? png : png.substr(slash + 1)});
            pump(3);
            app->composerAttachments.clear();
            pump(1);
        }
        std::printf("[ladder] attached and removed %zu images one at a time\n",
                    pngs.size());
        ladder.mark("+ every image attached, then removed again");
    }

    ladder.mark("- after opening and closing one at a time, N times");
    // Nothing happens between this rung and the one above it. Its delta is
    // therefore the instrument's own residue, and every other delta has to
    // clear it to mean anything.
    ladder.mark("  (the same state again -- this delta is the noise floor)");

    std::printf("\n[ladder] catalog %zu sessions | %d tabs open at the peak | "
                "%d single opens churned\n",
                catalogSize, want, churn);
    ladder.report();
    ladder.compare("+ catalog loaded, sidebar drawing it",
                   "+ scrolled both panes end to end",
                   "- every tab closed, back to the empty app", want);
    ladder.noise();
    ladder.compare("- every tab closed, back to the empty app",
                   "- every tab closed, back to the empty app",
                   "- after opening and closing one at a time, N times", churn);
    if (const int hold = hanabi::memladder::hold_seconds(); hold > 0) {
        std::printf("[ladder] holding %d s at the last rung -- pid %d\n", hold,
                    static_cast<int>(::getpid()));
        std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::seconds(hold));
    }
    graphics::close_window();
    return 0;
}

// --atlas-stress: FILL the glyph atlas on purpose and check that the detector
// says so.
//
// A detector for a condition nobody has ever reached is a detector nobody has
// ever seen work. afterhours_gaps.md #211 recorded the condition (a 2048x2048
// fontstash atlas with no error callback, whose overflow shows up as
// measure_text returning a WRONG number and then 0.0) and recorded that hanabi
// was nowhere near it -- which is exactly the state in which a guard is
// written, shipped, and never once exercised.
//
// So this mode drives the app's own measurement seam (theme::text_px, the same
// function every label in the app is laid out through) over the printable
// ASCII set at escalating point sizes until the atlas cannot take another
// glyph, and reports:
//
//   * the size at which measurement first went wrong, and what it returned;
//   * whether hanabi::atlas noticed, and with which fault;
//   * whether a DIRECT probe for a fresh glyph agrees.
//
// Exit 0 means the atlas overflowed AND the detector fired. Exit 1 means the
// detector stayed quiet through an overflow, which is the regression this mode
// exists to catch. Exit 2 means the atlas never overflowed at all (the run
// proves nothing; the ceiling moved).
//
// Nothing here is part of a normal run: it is reachable only from the flag.
static int run_atlas_stress() {
    using namespace afterhours;

    graphics::Config gcfg{};
    gcfg.display = graphics::DisplayMode::Headless;
    gcfg.width = 320;
    gcfg.height = 240;
    gcfg.target_fps = 60;
    if (!graphics::init(gcfg)) {
        fprintf(stderr, "[atlas-stress] headless graphics init failed\n");
        return 2;
    }
    Preload::get().init(product_branding::kAppName).make_singleton();

    std::string ascii;
    for (char c = 33; c < 127; ++c) ascii.push_back(c);

    // A reference the atlas is guaranteed to already hold, re-measured every
    // step. It must never move: fontstash caches a glyph per (codepoint,
    // size), so a size already rasterised stays correct even after the atlas
    // fills. If THIS ever changes, the failure is something other than #211.
    const float refPx = 13.0f;
    const float refW = theme::text_px(ascii.c_str(), refPx);

    printf("[atlas-stress] atlas is 2048x2048 R8, allocated once at init and "
           "never grown (afterhours backends/sokol/backend.h:104).\n");
    printf("[atlas-stress] reference: %zu glyphs at %.0fpt measure %.1f px\n",
           ascii.size(), static_cast<double>(refPx),
           static_cast<double>(refW));
    printf("[atlas-stress] %-6s %12s %10s %8s\n", "pt", "width", "faults",
           "probe");
    fflush(stdout);

    float firstBadPx = 0.0f;
    float firstBadW = -1.0f;
    unsigned long long faultsAtFirstBad = 0;
    bool probeSaidFull = false;

    // Sizes climb rather than repeat: fontstash keys a glyph on (codepoint,
    // size*10, blur), so each new size is 94 genuinely new rects. 2048x2048 is
    // ~4.2M pixels; the run is bounded by the size sweep, not by a timer.
    for (float px = 16.0f; px <= 512.0f; px += 4.0f) {
        const unsigned long long before = hanabi::atlas::fault_count();
        const float w = theme::text_px(ascii.c_str(), px);
        const unsigned long long after = hanabi::atlas::fault_count();
        const bool probeOk =
            hanabi::atlas::probe(px, [](const char* t, float p) {
                return afterhours::measure_text_internal(t, p);
            });
        if (!probeOk) probeSaidFull = true;

        const bool interesting =
            (after != before) || !probeOk || (px <= 32.0f) ||
            (static_cast<int>(px) % 64 == 0);
        if (interesting) {
            printf("[atlas-stress] %-6.0f %12.1f %10llu %8s\n",
                   static_cast<double>(px), static_cast<double>(w), after,
                   probeOk ? "ok" : "FULL");
            fflush(stdout);
        }
        if (firstBadW < 0.0f && (after != before || !probeOk)) {
            firstBadPx = px;
            firstBadW = w;
            faultsAtFirstBad = after;
        }
        if (firstBadW >= 0.0f && px > firstBadPx + 64.0f) break;
    }

    const float refAgain = theme::text_px(ascii.c_str(), refPx);
    const bool overflowed = probeSaidFull || firstBadW >= 0.0f;
    const unsigned long long faults = hanabi::atlas::fault_count();

    printf("\n[atlas-stress] RESULT\n");
    printf("  atlas overflowed:        %s\n", overflowed ? "yes" : "no");
    if (firstBadW >= 0.0f)
        printf("  first bad measurement:   %.0fpt -> %.1f px (faults %llu)\n",
               static_cast<double>(firstBadPx),
               static_cast<double>(firstBadW), faultsAtFirstBad);
    printf("  detector fired:          %s (%llu fault%s, first %s)\n",
           faults ? "YES" : "no", faults, faults == 1 ? "" : "s",
           hanabi::atlas::fault_name(hanabi::atlas::first_fault()));
    printf("  cached reference held:   %.1f -> %.1f px at %.0fpt\n",
           static_cast<double>(refW), static_cast<double>(refAgain),
           static_cast<double>(refPx));
    fflush(stdout);

    graphics::close_window();
    if (!overflowed) {
        fprintf(stderr,
                "[atlas-stress] the atlas never overflowed; this run proves "
                "nothing about the detector.\n");
        return 2;
    }
    if (faults == 0) {
        fprintf(stderr,
                "[atlas-stress] THE ATLAS OVERFLOWED AND NOTHING SAID SO. "
                "That is the exact failure src/util/atlas_guard.h exists to "
                "prevent.\n");
        return 1;
    }
    return 0;
}

// Headless one-shot: render the real UI to an offscreen texture and write a
// PNG, with no window and no screen-recording permission. Used for docs and
// smoke tests. Returns process exit code.
static void run_idle_timing(afterhours::SystemManager& sm) {
    const char* value = std::getenv("HANABI_IDLE_TIMING");
    if (value == nullptr || *value == '\0') return;
    int callbacks = std::atoi(value);
    if (callbacks < 120) callbacks = 1200;

    const bool disabled = [] {
        const char* v = std::getenv("HANABI_IDLE_DISABLE");
        return v != nullptr && *v != '\0' && std::string_view(v) != "0";
    }();
    const bool fixedTenFps = [] {
        const char* v = std::getenv("HANABI_IDLE_FIXED_10FPS");
        return v != nullptr && *v != '\0' && std::string_view(v) != "0";
    }();
    hanabi::FrameActivityPolicy policy(!disabled);
    const unsigned long long cpuStart = hanabi::prof::cpu_nanos();
    const unsigned long long allocStart = hanabi::prof::alloc_count();
    int rendered = 0;
    constexpr std::uint64_t kCallbackUs = 8333;

    for (int i = 0; i < callbacks; ++i) {
        hanabi::FrameSignals signals;
        auto query = afterhours::EntityQuery({.force_merge = true})
                         .whereHasComponent<ecs::AppComponent>()
                         .gen();
        if (!query.empty())
            signals = hanabi::collect_app_frame_signals(
                query[0].get().get<ecs::AppComponent>());
        hanabi::collect_ui_frame_signals(signals);
        if (fixedTenFps) {
            signals = {};
            signals.timer = true;
        }

        const std::uint64_t nowUs = static_cast<std::uint64_t>(i) * kCallbackUs;
        const auto decision = policy.decide(nowUs, signals);
        if (!decision.render) continue;
        float dt = 1.0f / 120.0f;
        if (policy.started()) {
            dt = static_cast<float>(nowUs - policy.last_frame_us()) / 1000000.0f;
            dt = std::min(dt, 0.1f);
        }
        policy.rendered(nowUs);
        const hanabi::AutoreleaseFrame framePool;
        afterhours::graphics::begin_frame();
        afterhours::graphics::clear_background(theme::window_bg());
        sm.run(dt);
        afterhours::graphics::end_frame();
        ++rendered;
    }

    const double logicalSeconds =
        static_cast<double>(callbacks) * static_cast<double>(kCallbackUs) /
        1000000.0;
    const double cpuMs = static_cast<double>(hanabi::prof::cpu_nanos() - cpuStart) /
                         1000000.0;
    const unsigned long long allocations =
        hanabi::prof::alloc_count() - allocStart;
    std::printf("IdleTiming: callbacks=%d frames=%d logical_seconds=%.3f "
                "cpu_ms_per_sec=%.3f allocs_per_sec=%.1f "
                "cpu_ms_per_frame=%.4f allocs_per_frame=%.1f\n",
                callbacks, rendered, logicalSeconds, cpuMs / logicalSeconds,
                static_cast<double>(allocations) / logicalSeconds,
                rendered == 0 ? 0.0 : cpuMs / static_cast<double>(rendered),
                rendered == 0 ? 0.0
                              : static_cast<double>(allocations) /
                                    static_cast<double>(rendered));
    std::fflush(stdout);
}

static int run_headless_screenshot(const std::string& path, int w, int h) {
    using namespace afterhours;

    // Rung 0 of the memory ladder, taken before graphics, app state or a
    // single system exists. A no-op unless HANABI_MEMLADDER is set.
    hanabi::memladder::record_floor();

    // HANABI_STARTUP_PROF=1: attribute the HEADLESS FirstFrame. The gate reads
    // FirstFrame (~200-250 ms) while Startup reads ~25 ms, and for a long time
    // nobody could say what the ~175 ms in between was — Startup stops at
    // "systems ready" and FirstFrame is logged inside the capture loop, so
    // every settle frame, sleep and pump in between fell in a hole neither
    // number covered. These marks close that hole.
    const bool hprof = [] {
        const char* v = std::getenv("HANABI_STARTUP_PROF");
        return v && *v && std::string(v) != "0";
    }();
    auto hmark = [&](const char* what) {
        if (!hprof) return;
        auto now = std::chrono::high_resolution_clock::now();
        log_info("  [hprof] {:<26}: {} ms (cumulative from process start)",
                 what,
                 std::chrono::duration_cast<std::chrono::milliseconds>(
                     now - app_state::startTime)
                     .count());
        fflush(stdout);
    };
    int settleFrames = 0;


    // One clock reading for the whole capture, so a duration that is set up in
    // one frame and rendered in a later one cannot straddle a second boundary
    // and photograph a different number (see util/capture_clock.h).
    capture_clock::freeze();
    // A capture has nobody in front of it, so a clicked link must not reach a
    // browser (see src/ui/link_detect.h).
    hanabi::links::headless() = true;

    // NOTE on hi-DPI: the WINDOWED app already runs high_dpi=true, so the real
    // window is crisp on Retina. This HEADLESS capture path renders into a
    // fixed w*h offscreen texture and the Metal backend does not supersample
    // it — `Config.hidpi` is honored only by the raylib backend, and there is
    // no render-scale or sample-count on `graphics::Config` at all
    // (afterhours_gaps.md #101, #92). We do NOT patch vendored afterhours.
    //
    // What DOES work, and the old note here said it did not: pass a 2x w/h AND
    // set HANABI_UI_SCALE=2.0 (src/test_hooks.h). In Adaptive scaling mode —
    // which is what setup_app_state selects — theme.ui_scale multiplies every
    // pixels() value including explicit font sizes, so the result is the same
    // UI at twice the size rather than a thin sidebar in a big canvas. That is
    // what `HANABI_SHOOT_2X=1 scripts/shoot_hanabi.sh` captures and reduces
    // with LANCZOS.
    //
    // It is a layout ZOOM, not a supersample, and for the parity metric that
    // distinction is the whole answer: the strings are re-advanced at the
    // larger size rather than sampled more finely, so a 2x-reduced capture
    // scores WORSE on every text region and better on every drawn shape. Do
    // not make it the default; the numbers are in
    // docs/visual-parity/FRICTION_LOG.md, "Capturing at 2x (feat/vis-hidpi)".
    graphics::Config gcfg{};
    gcfg.display = graphics::DisplayMode::Headless;
    gcfg.width = w;
    gcfg.height = h;
    gcfg.target_fps = 60;
    if (!graphics::init(gcfg)) {
        fprintf(stderr, "headless init failed (no GPU?)\n");
        return 1;
    }
    hmark("graphics::init");

    Preload::get().init(product_branding::kAppName).make_singleton();
    hmark("preload");
    setup_app_state();
    hmark("setup_app_state");

    SystemManager sm;
    app_state::systemManager = &sm;
    build_systems(sm);
    hmark("build_systems");

    // Pay the GPU's first-use costs -- the icon atlas, its blend pipeline, the
    // glyph atlas and the four draw paths -- on a frame that builds no widget
    // tree, so the first REAL frame is warm. See util/prewarm.h and
    // afterhours_gaps.md #155.
    hanabi::prewarm::run();
    hmark("prewarm");

    auto readyTime = std::chrono::high_resolution_clock::now();
    auto startupMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                         readyTime - app_state::startTime)
                         .count();
    log_info("Startup: {} ms", startupMs);
    fflush(stdout);
    fflush(stderr);

    // The memory ladder runs INSTEAD of a capture: it needs the app empty at
    // its second rung, and the wait-for-list block below is what fills it.
    if (hanabi::memladder::enabled()) return run_mem_ladder(sm);

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
        // Is everything the capture needs actually here?
        auto ready = [&] {
            const bool listReady =
                appForWait->listState != ecs::LoadState::Loading &&
                appForWait->listState != ecs::LoadState::Idle;
            // A transcript is "pending" only when a thread is actually open and
            // its fetch hasn't resolved. No open thread => nothing to wait for.
            const bool transcriptPending =
                !appForWait->pane().selectedId.empty() &&
                (appForWait->pane().transcriptState == ecs::LoadState::Loading ||
                 appForWait->pane().transcriptState == ecs::LoadState::Idle);
            return listReady && !transcriptPending;
        };
        // The sleep BACKS OFF rather than sitting at 8 ms. Two bugs it fixes,
        // both measured:
        //
        //  1. The old loop slept AFTER the render and re-checked at the top,
        //     so the iteration that actually resolved the fetch still paid a
        //     full 8 ms sleep before anyone noticed. Every launch bought one
        //     sleep it had no use for.
        //  2. The 8 ms floor was sized for a network backend, but the mock
        //     resolves list_sessions() in 0.118 ms. The mock path took 3
        //     iterations x 8 ms = 24 ms of pure sleep on every headless launch
        //     and every screenshot, which is every scripted test in the suite.
        //
        // 1,2,4,8,8,... reaches the old cadence in four iterations, so a real
        // network fetch of a few hundred ms polls essentially as before (~40
        // renders over 300 ms vs ~37), while the mock pays 3 ms instead of 24.
        int sleepMs = 1;
        while (std::chrono::steady_clock::now() < deadline) {
            if (ready()) break;
            ++settleFrames;
            {
                // Scoped to the RENDER and nothing else. The first version put
                // this at the top of the loop body, so its wall column also
                // carried the backoff sleep below -- and on a box at load 20 a
                // 1 ms sleep is 18 ms of waiting to be rescheduled, which read
                // as a 21 ms second frame that no pre-warm could ever move.
                const hanabi::launch_curve::Frame curveFrame{"settle"};
                const hanabi::AutoreleaseFrame framePool;
                graphics::begin_frame();
                graphics::clear_background(theme::window_bg());
                sm.run(1.0f / 60.0f);
                graphics::end_frame();
            }
            // Ask again before sleeping: this render is usually the one that
            // resolved it, and the old shape slept anyway.
            if (ready()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
            if (sleepMs < 8) sleepMs *= 2;
        }
        if (hprof)
            log_info("  [hprof] settle loop ran {} frames", settleFrames);
        hmark("settle-wait loop");

        // Perf/screenshot affordance: HANABI_OPEN=<id> opens a specific thread
        // in the headless capture (used to open the long perf fixture "rbig"
        // or the rich mock "r9"). Sets requestOpenTab so the loader fetches +
        // opens it; a few pump frames below let it land before capture/timing.
        if (const char* oid = std::getenv("HANABI_OPEN"); oid && *oid) {
            appForWait->requestOpenTab = oid;
            appForWait->view = ecs::SmartView::Chat;
            for (int p = 0; p < 6; ++p) {
                const hanabi::AutoreleaseFrame framePool;
                graphics::begin_frame();
                graphics::clear_background(theme::window_bg());
                sm.run(1.0f / 60.0f);
                graphics::end_frame();
            }
        }

        // Screenshot affordance: HANABI_SPLIT=<id> opens a SECOND thread in the
        // right split pane (I2), so the split-view can be captured headlessly.
        // Requires HANABI_OPEN to have set the primary pane first.
        if (const char* sid = std::getenv("HANABI_SPLIT"); sid && *sid) {
            appForWait->requestSplitOpen = sid;
            appForWait->view = ecs::SmartView::Chat;
            for (int p = 0; p < 6; ++p) {
                const hanabi::AutoreleaseFrame framePool;
                graphics::begin_frame();
                graphics::clear_background(theme::window_bg());
                sm.run(1.0f / 60.0f);
                graphics::end_frame();
            }
        }
        apply_stream_demo(appForWait);
        if (std::getenv("HANABI_THINK_DEMO")) {
            for (int p = 0; p < 4; ++p) {
                const hanabi::AutoreleaseFrame framePool;
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
            if (appForWait->pane().openSession) {
                const auto& ms = appForWait->pane().openSession->messages;
                for (size_t k = 0; k < ms.size(); ++k)
                    if (ms[k].role == api::Role::Tool)
                        appForWait->expandedPiles.insert(
                            ms[k].id.empty() ? ("pile" + std::to_string(k))
                                             : ms[k].id);
            }
        }

        apply_test_knobs(appForWait);


    }

    // The soak probe: HANABI_SOAK=<frames> runs the real loop for that long
    // and reports whether frame time, RSS or the entity count is trending up.
    // Nothing else in this project can see a leak -- the suite renders 45
    // frames and the perf gate measures a process under a second old, and a
    // leak is a young process looking fine. See util/soak.h.
    if (const int soakFrames = hanabi::soak::frames(); soakFrames > 0) {
        std::vector<hanabi::soak::Sample> samples;
        const int every = hanabi::soak::bucket();
        hanabi::stress::Driver driver{hanabi::stress::scenario()};
        log_info("[soak] scenario={} frames={} settle={}",
                 hanabi::stress::name(driver.mode), soakFrames,
                 hanabi::stress::settle_frames());

        const hanabi::breaker::Conditions breakOn =
            hanabi::breaker::from_env();
        if (breakOn.any())
            log_info("[break] armed from HANABI_STRESS_UNTIL");

        // One monotonic clock for the scenario, running across the settle
        // pass and on into the measured window without a discontinuity.
        //
        // The settle used to render without DRIVING, which made it a settle
        // for the launch burst and for nothing else: every scenario's own
        // warm-up -- the first tab `threads` opens, the first query `search`
        // types, the whole first sweep of the list `scrollall` expands and
        // scrolls -- landed inside the measured window. On the scroll arm that
        // is a title memo filling with 1800 entries while the first buckets
        // are being sampled, which reads as live blocks trending up and is
        // just a cache arriving. Measured: the block trend across nine clean
        // runs spanned -148 to +847 per 1000 frames with an undriven settle.
        int driveFrame = 0;
        const auto drive = [&]() {
            if (appForWait != nullptr) driver.act(driveFrame, *appForWait);
            if (const float step = driver.scroll_step(driveFrame);
                step != 0.0f) {
                if (!hanabi::soak::scroll_named(driver.scroll_target_name(),
                                                step))
                    hanabi::prof::tick("stress.scroll_target_missing");
            }
            ++driveFrame;
        };

        // Settle first, unmeasured. A first pass on a fresh launch is the
        // launch burst, not the steady state, and averaging it in hides the
        // thing being looked for (Puffin's PERFORMANCE.md, learned the same
        // way).
        //
        // The settle also produces the CPU BASELINE that `cpu:<ratio>` is a
        // ratio of. It has to come from here and not from the measured run:
        // a baseline taken while the scenario is driving would rise with
        // whatever the scenario costs, and the condition would never trip.
        std::vector<double> settleCpuMs;
        settleCpuMs.reserve(
            static_cast<size_t>(hanabi::stress::settle_frames()));
        for (int i = 0; i < hanabi::stress::settle_frames(); ++i) {
            drive();
            const double c0 = hanabi::soak::cpu_nanos();
            {
                const hanabi::AutoreleaseFrame framePool;
                graphics::begin_frame();
                graphics::clear_background(theme::window_bg());
                sm.run(1.0f / 60.0f);
                graphics::end_frame();
            }
            settleCpuMs.push_back(
                static_cast<double>(hanabi::prof::cpu_nanos() - c0) / 1.0e6);
        }
        const double cpuBaselineMs =
            hanabi::breaker::median_of(std::move(settleCpuMs));
        if (breakOn.any())
            log_info("[break] settled CPU baseline {:.3f} ms/frame (median of "
                     "{} settle frames)",
                     cpuBaselineMs, hanabi::stress::settle_frames());

        hanabi::breaker::Verdict broke;
        hanabi::breaker::Streak breakStreak;
        int framesRun = 0;

        auto bucketStart = std::chrono::steady_clock::now();
        // The CPU clock is sampled UNCONDITIONALLY, not behind prof::enabled().
        // The soak's frame-time gate is on thread CPU rather than wall clock
        // (soak.h::verdict says why), so the reading it gates on cannot be
        // behind a diagnostic flag. Two clock_gettime calls a frame.
        double bucketCpu0 = hanabi::soak::cpu_nanos();
        for (int i = 1; i <= soakFrames; ++i) {
            if (appForWait != nullptr) driver.act(i - 1, *appForWait);
            if (const float step = driver.scroll_step(i - 1); step != 0.0f) {
                if (!hanabi::soak::scroll_named(driver.scroll_target_name(),
                                                step))
                    hanabi::prof::tick("stress.scroll_target_missing");
            }
            const unsigned long long cpu0 = hanabi::prof::cpu_nanos();
            {
                const hanabi::AutoreleaseFrame framePool;
                graphics::begin_frame();
                graphics::clear_background(theme::window_bg());
                sm.run(1.0f / 60.0f);
                graphics::end_frame();
            }
            const unsigned long long frameCpu = hanabi::prof::cpu_nanos() - cpu0;
            if (hanabi::prof::enabled())
                hanabi::prof::frame_cpu(frameCpu);
            hanabi::prof::frame();
            framesRun = i;

            // The failure conditions, checked EVERY FRAME rather than every
            // bucket, and sampling only what is armed. "It broke at 118 tabs"
            // needs the frame the 118th tab landed on; a bucket of 250 frames
            // would report it as somewhere in a range of twelve.
            if (breakOn.any()) {
                hanabi::breaker::State st;
                st.frame = i;
                st.cpuMs = static_cast<double>(frameCpu) / 1.0e6;
                st.cpuBaselineMs = cpuBaselineMs;
                if (breakOn.rssKb > 0) st.rssKb = hanabi::soak::rss_kb();
                if (breakOn.blocks > 0 || breakOn.heapKb > 0) {
                    // The only expensive sample here -- it walks every malloc
                    // zone -- so it is taken only when something is armed on
                    // it.
                    const hanabi::soak::HeapStat h = hanabi::soak::heap_in_use();
                    st.blocks = static_cast<long>(h.count);
                    st.heapKb = static_cast<long>(h.bytes / 1024);
                }
                if (breakOn.entities > 0)
                    st.entities =
                        static_cast<long>(EntityHelper::get_entities().size());
                if (breakOn.tabs > 0)
                    st.tabs = hanabi::stress::Driver::live_tab_count();
                broke = hanabi::breaker::test(breakOn, st, breakStreak);
                if (broke.broke) {
                    // Close the bucket at the break so the trajectory table
                    // includes the frame it broke on rather than stopping at
                    // the last round number before it.
                    const auto now = std::chrono::steady_clock::now();
                    const double cpuNow = hanabi::soak::cpu_nanos();
                    const int span = i - (i / every) * every;
                    if (span > 0)
                        hanabi::soak::report(
                            samples, i,
                            std::chrono::duration<double, std::milli>(
                                now - bucketStart).count() / span,
                            (cpuNow - bucketCpu0) / 1e6 / span,
                            hanabi::soak::rss_kb(),
                            EntityHelper::get_entities().size());
                    break;
                }
            }
            if (i % every == 0) {
                auto now = std::chrono::steady_clock::now();
                const double cpuNow = hanabi::soak::cpu_nanos();
                const double ms =
                    std::chrono::duration<double, std::milli>(now - bucketStart)
                        .count() / static_cast<double>(every);
                const double cpuMs = (cpuNow - bucketCpu0) / 1e6 /
                                     static_cast<double>(every);
                bucketStart = now;
                bucketCpu0 = cpuNow;
                hanabi::soak::report(samples, i, ms, cpuMs,
                                     hanabi::soak::rss_kb(),
                                     EntityHelper::get_entities().size());
            }
        }
        hanabi::soak::census();
        // What the scenario actually did, before the verdict rather than
        // after it: a reader who sees "kept_tabs=0" should not have to get
        // past a PASS to find out the run drove nothing.
        std::printf("[soak] scenario %s did: %s\n",
                    hanabi::stress::name(driver.mode),
                    driver.work_done().c_str());
        const std::string idle = driver.did_nothing_reason();
        if (!idle.empty())
            std::printf("[soak] SCENARIO DROVE NOTHING: %s. Whatever this run "
                        "measured,\n[soak] it was not %s -- do not read it as "
                        "a clean %s run.\n",
                        idle.c_str(), hanabi::stress::name(driver.mode),
                        hanabi::stress::name(driver.mode));
        std::fflush(stdout);
        // Fill in whatever the per-frame check did not sample, ONCE, so the
        // final state line is complete. A report that prints "RSS 0 KB"
        // because RSS happened not to be armed reads as a broken instrument.
        if (breakOn.any()) {
            broke.at.frame = broke.broke ? broke.at.frame : framesRun;
            if (broke.at.rssKb == 0) broke.at.rssKb = hanabi::soak::rss_kb();
            if (broke.at.blocks == 0 || broke.at.heapKb == 0) {
                const hanabi::soak::HeapStat h = hanabi::soak::heap_in_use();
                if (broke.at.blocks == 0)
                    broke.at.blocks = static_cast<long>(h.count);
                if (broke.at.heapKb == 0)
                    broke.at.heapKb = static_cast<long>(h.bytes / 1024);
            }
            if (broke.at.entities == 0)
                broke.at.entities =
                    static_cast<long>(EntityHelper::get_entities().size());
            if (broke.at.tabs == 0)
                broke.at.tabs = hanabi::stress::Driver::live_tab_count();
            if (broke.at.cpuBaselineMs == 0.0)
                broke.at.cpuBaselineMs = cpuBaselineMs;
        }
        hanabi::breaker::report(breakOn, broke, framesRun, soakFrames,
                                hanabi::stress::name(driver.mode),
                                driver.work_done().c_str());
        hanabi::soak::VerdictTrends trends;
        int bad = hanabi::soak::verdict(samples, trends);
        // The second reading perf/scroll added: a min-of-half RATIO rather
        // than a slope, and the two answer different questions. A slope
        // catches a leak; a ratio between the halves catches a cost that
        // arrived and stayed. Kept alongside, not instead of -- see soak.h.
        bad |= hanabi::soak::trend_verdict(samples);
        {
            hanabi::soak::ReportInput ri;
            ri.scenario = hanabi::stress::name(driver.mode);
            ri.frames = framesRun > 0 ? framesRun : soakFrames;
            ri.bucket = every;
            const std::string work = driver.work_done();
            ri.work = work.c_str();
            ri.entities = EntityHelper::get_entities().size();
            ri.tabs = hanabi::stress::Driver::live_tab_count();
            ri.verdict = !trends.ready ? "INCONCLUSIVE"
                                       : (bad == 0 ? "PASS" : "FAIL");
            hanabi::soak::write_report(ri, trends.rss, trends.heap,
                                       trends.blocks, trends.cpu,
                                       trends.entities, trends.gpu,
                                       hanabi::soak::budget(),
                                       trends.fitPoints);
        }
        if (hanabi::prof::enabled()) {
            if (auto* tmc = EntityHelper::get_singleton_cmp<
                    afterhours::ui::TextMeasureCache>())
                std::printf("[prof] TextMeasureCache: %llu hits / %llu misses "
                            "= %.1f%% hit, %zu entries\n",
                            (unsigned long long)tmc->hits(),
                            (unsigned long long)tmc->misses(),
                            (double)tmc->hit_rate(), tmc->size());
        }
        hanabi::prof::dump();
        graphics::close_window();
        return bad;
    }

    // Render several frames so async data loads and layout settles.
    constexpr int kFrames = 45;
    hmark("pre-capture pumps done");
    for (int i = 0; i < kFrames; ++i) {
        apply_loading_demo(appForWait);
        {
            const hanabi::launch_curve::Frame curveFrame{"capture"};
            const hanabi::AutoreleaseFrame framePool;
            graphics::begin_frame();
            graphics::clear_background(theme::window_bg());
            sm.run(1.0f / 60.0f);
            graphics::end_frame();
        }
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
            // The settle frames are exactly when the loader replaces the open
            // session, so anything injected before them is gone by now. Put it
            // back and give it frames to lay out.
            apply_stream_demo(&app);
            if (std::getenv("HANABI_THINK_DEMO")) {
                for (int p = 0; p < 6; ++p) {
                    const hanabi::AutoreleaseFrame framePool;
                    graphics::begin_frame();
                    graphics::clear_background(theme::window_bg());
                    sm.run(1.0f / 60.0f);
                    graphics::end_frame();
                }
            }
            log_info("Headless capture: {} sessions, listState={}",
                     app.sessions.size(), (int)app.listState);
        }
    }

    run_idle_timing(sm);

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
            // The pool is pushed BEFORE the clock starts and drains after it
            // stops, so it costs the measurement nothing — and without it this
            // diagnostic loop leaks a render pass per frame, which is exactly
            // the defect the numbers it prints are used to hunt.
            const hanabi::AutoreleaseFrame framePool;
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
        // Widget count is the number that explains the rest: the tree is torn
        // down and rebuilt every frame, and full-tree layout is re-solved every
        // frame, so per-frame cost tracks it almost linearly.
        size_t widgets = 0;
        for (const auto& e :
             afterhours::ui::UICollectionHolder::get().collection.get_entities())
            if (e && e->has<afterhours::ui::UIComponent>()) ++widgets;
        // `widgets` is the count of SURVIVORS -- what is in the collection
        // after the frame -- and `built` is what this frame actually made
        // (src/ui/widget_epoch.h). They are the same number on a screen the
        // app has sat on. Where they differ, the difference is what the frame
        // is walking for nothing.
        log_info(
            "FrameTiming: frames={} widgets={} min={:.2f}ms median={:.2f}ms "
            "mean={:.2f}ms max={:.2f}ms built={}",
            ms.size(), widgets, ms.front(), median, mean, ms.back(),
            hanabi::widget_epoch::built_this_epoch());
        // What the last digest frame BUILT against what MATCHED. Zero on a
        // screen that is not a digest view. scripts/digest_gate.sh reads this
        // rather than the widget total, because the widget total is the whole
        // window -- sidebar, tab strip, composer -- and this is the property.
        {
            const auto& cards = hanabi::test_hooks::card_audit_counts();
            log_info("DigestCards: built={} matched={} first={}", cards.built,
                     cards.matched, cards.first);
        }
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

    hanabi::launch_curve::report();
    hanabi::bounds_audit::report();

    bool ok = graphics::capture_frame(path);
    graphics::shutdown();
    if (!ok) {
        fprintf(stderr, "capture_frame failed\n");
        return 1;
    }
    printf("wrote %s\n", path.c_str());
    return 0;
}

#ifdef AFTER_HOURS_ENABLE_E2E_TESTING
// Drive the real UI from a script: a headless render loop with synthetic mouse
// and keyboard, so an interaction — hover this, click that, assert the text
// that appeared — can be checked in CI. Only compiled into the dedicated e2e
// binary; the shipping build has no trace of it.
//
// `path` is one .e2e script or a directory of them. Returns a process exit
// code: 0 if every script passed.
static int run_e2e(const std::string& path, int w, int h) {
    using namespace afterhours;
    namespace t = afterhours::testing;

    // Same reason as the capture path: a scripted click on a tracker id must
    // stay inside this process.
    hanabi::links::headless() = true;

    graphics::Config gcfg{};
    gcfg.display = graphics::DisplayMode::Headless;
    gcfg.width = w;
    gcfg.height = h;
    gcfg.target_fps = 60;
    if (!graphics::init(gcfg)) {
        fprintf(stderr, "e2e: headless init failed (no GPU?)\n");
        return 2;
    }

    Preload::get().init(product_branding::kAppName).make_singleton();
    setup_app_state();

    SystemManager sm;
    app_state::systemManager = &sm;
    // Command handlers first: they inject the synthetic input for this frame,
    // and the input + UI systems registered by build_systems have to read it
    // in the SAME frame or a click lands one frame late and the script races
    // its own assertions.
    t::register_builtin_handlers(sm);
    t::ui_commands::register_ui_commands<InputAction>(sm);
    hanabi::e2e::register_hanabi_commands(sm);
    t::register_unknown_handler(sm);
    t::register_cleanup(sm);
    build_systems(sm);

    t::platform_input::set_test_mode(true);
    int g_settle_frames = 0;
    int g_content_ready_frame = 0;

    t::E2ERunner runner;
    if (std::filesystem::is_directory(path))
        runner.load_scripts_from_directory(path);
    else
        runner.load_script(path);
    if (!runner.has_commands()) {
        fprintf(stderr, "e2e: no commands in %s\n", path.c_str());
        graphics::shutdown();
        return 2;
    }
    runner.set_screenshot_callback(
        [](const std::string& p) { graphics::capture_frame(p); });

    // Ask for any thread the script wants open BEFORE settling, so the settle
    // frames below are also the frames that service the request.
    {
        auto q = EntityQuery({.force_merge = true})
                     .whereHasComponent<ecs::AppComponent>()
                     .gen();
        if (!q.empty())
            request_test_opens(&q[0].get().get<ecs::AppComponent>());
    }

    // Settle: the session list loads asynchronously, and a script that clicks a
    // sidebar row before the rows exist clicks empty space. A thread requested
    // above also has its transcript fetched on a worker, so this waits for the
    // pane to actually have content rather than counting frames and hoping —
    // 45 was enough for the list and not for a 120-message transcript.
    {
        auto q = EntityQuery({.force_merge = true})
                     .whereHasComponent<ecs::AppComponent>()
                     .gen();
        ecs::AppComponent* app = q.empty()
                                     ? nullptr
                                     : &q[0].get().get<ecs::AppComponent>();
        // A thread is expected whenever one was ASKED for (HANABI_OPEN) or
        // RESTORED from the settings a script declares. Waiting only on the
        // former is what "counts frames and hopes" looks like: the mock is
        // fast enough that a restored tab is populated well inside 45 frames
        // today, so no test was actually racing — but that is the backend's
        // speed, not a property of the harness, and a slower fixture would
        // turn every restored-tab script into a pass earned by nothing.
        int contentReady = 0;
        const bool wantsThread =
            std::getenv("HANABI_OPEN") != nullptr ||
            !Settings::get().get_active_tab().empty();
        // The frame the content ARRIVED on, and the frame the loop stopped
        // on. They are different numbers and the difference is how many
        // frames the pane got to settle with content in it -- which is what
        // varies run to run and is the whole of tests/ui's timing risk. See
        // docs/perf/GATES.md.
        int settleFrames = 0;
        for (int i = 0; i < 300; ++i) {
            const hanabi::AutoreleaseFrame framePool;
            graphics::begin_frame();
            graphics::clear_background(theme::window_bg());
            sm.run(1.0f / 60.0f);
            graphics::end_frame();
            settleFrames = i + 1;
            if (app != nullptr && contentReady == 0 &&
                app->listState != ecs::LoadState::Loading &&
                (!wantsThread ||
                 (app->pane().openSession && !app->pane().openSession->messages.empty())))
                contentReady = i + 1;
            if (i < 45) continue;
            if (app == nullptr) break;
            if (app->listState == ecs::LoadState::Loading) continue;
            if (wantsThread &&
                (!app->pane().openSession || app->pane().openSession->messages.empty()))
                continue;
            break;
        }
        g_settle_frames = settleFrames;
        g_content_ready_frame = contentReady;
        // Say so rather than proceeding into a script that will fail three
        // assertions in and blame the feature.
        if (app != nullptr && wantsThread &&
            (!app->pane().openSession || app->pane().openSession->messages.empty()))
            fprintf(stderr,
                    "e2e: WARNING settle finished with no transcript loaded; "
                    "assertions about message content will not mean what they "
                    "look like\n");
    }

    // What the settle loop actually achieved, so a script can be audited for
    // whether it earned its pass or merely outran the loader. Diagnostic only.
    if (std::getenv("HANABI_DBG_SETTLE")) {
        auto q = EntityQuery({.force_merge = true})
                     .whereHasComponent<ecs::AppComponent>()
                     .gen();
        if (!q.empty()) {
            const auto& a = q[0].get().get<ecs::AppComponent>();
            fprintf(stderr,
                    "[SETTLE] frames=%d content_ready_at=%d settled_with_content=%d "
                    "sessions=%zu listState=%d open=%s msgs=%zu\n",
                    g_settle_frames, g_content_ready_frame,
                    g_settle_frames - g_content_ready_frame,
                    a.sessions.size(), static_cast<int>(a.listState),
                    a.pane().openSession ? a.pane().openSession->summary.id.c_str() : "-",
                    a.pane().openSession ? a.pane().openSession->messages.size() : 0u);
        }
    }

    // Same state knobs the capture path honours, so a script can start from any
    // state a screenshot can — including the overlays whose only real binding
    // is a Cmd chord the injector cannot produce (gap #49).
    {
        auto q = EntityQuery({.force_merge = true})
                     .whereHasComponent<ecs::AppComponent>()
                     .gen();
        if (!q.empty()) apply_test_knobs(&q[0].get().get<ecs::AppComponent>());
    }

    // reset_frame is the host's job — the library never calls it, and without
    // it a synthetic press stays pressed forever and every later click is
    // swallowed. It has to run before the frame's commands are dispatched.
    constexpr float kDt = 1.0f / 60.0f;
    constexpr int kMaxFrames = 60 * 120;  // a script may not run over 2 minutes
    int frames = 0;
    while (!runner.is_finished() && frames++ < kMaxFrames) {
        t::test_input::reset_frame();
        // The loader owns openSession and replaces it whenever a fetch lands,
        // which takes any injected demo state with it. A demo state has to be
        // re-asserted, not set once — this is a no-op unless its knob is set.
        if (std::getenv("HANABI_THINK_DEMO")) {
            auto q = EntityQuery({.force_merge = true})
                         .whereHasComponent<ecs::AppComponent>()
                         .gen();
            if (!q.empty())
                apply_stream_demo(&q[0].get().get<ecs::AppComponent>());
        }
        runner.tick(kDt);
        // The scripted-UI suite runs 85 scripts through this loop and nothing
        // else drains what Metal autoreleases here.
        const hanabi::AutoreleaseFrame framePool;
        graphics::begin_frame();
        graphics::clear_background(theme::window_bg());
        sm.run(kDt);
        graphics::end_frame();
    }

    const bool ranOut = !runner.is_finished();
    if (ranOut) fprintf(stderr, "e2e: hit the %d-frame ceiling\n", kMaxFrames);

    // No drain loop, and no reading the handlers' counter behind the runner's
    // back. Both were working around afterhours bugs that are now fixed: the
    // runner used to declare itself finished in the same tick it dispatched the
    // LAST command (so a trailing assertion was never observed), and in
    // single-script mode it never folded the command error count into
    // has_failed() (so a failing script still exited 0). Upstream 92666b7 fixed
    // both, with the same shape hanabi had worked around here. The 40 extra
    // frames per script were pure suite latency once that landed.
    runner.print_results();
    // The scripted path can DRIVE things the soak loop cannot reach -- a
    // resize drag, a keystroke, a menu -- and until this line the profiler's
    // counters were only ever dumped from the soak loop, so none of that was
    // measurable. HANABI_PROF is a hard no-op when unset, same as everywhere
    // else.
    hanabi::prof::dump();
    const bool failed = runner.has_failed() || ranOut;
    persist_app_state();
    graphics::shutdown();
    std::fflush(nullptr);
    return failed ? 1 : 0;
}
#endif  // AFTER_HOURS_ENABLE_E2E_TESTING

int main(int argc, char* argv[]) {
    argh::parser cmdl;
    // --screenshot takes a path value. It MUST be pre-registered as a param;
    // otherwise argh parses the space-separated form ("--screenshot <path>")
    // as a bare flag plus a positional arg, cmdl.params() comes back empty,
    // and we silently fall through to the windowed run() path — which opens a
    // real Metal window and never exits in a headless/one-shot context.
    cmdl.add_params({"--screenshot", "--e2e", "--parse-thread-url"});
    cmdl.parse(argc, argv);

    // --version prints and exits.
    if (cmdl["--version"] || cmdl["-V"]) {
        printf("%s %s\n", product_branding::kAppName, hanabi::kVersion);
        return 0;
    }
    if (cmdl["--native-diagnostics"]) {
        char status[512];
        native_integration_status(status, sizeof(status));
        printf("%s\n", status);
        return 0;
    }
    if (std::string url = cmdl("parse-thread-url").str(); !url.empty()) {
        char thread[512] = {};
        native_simulate_open_url(url.c_str());
        if (!native_take_open_thread(thread, sizeof(thread))) return 2;
        printf("%s\n", thread);
        return 0;
    }

    app_state::startTime = std::chrono::high_resolution_clock::now();

#ifdef AFTER_HOURS_ENABLE_E2E_TESTING
    // --e2e <script.e2e|dir>: drive the UI from a script and exit with the
    // verdict. Only exists in the e2e build.
    if (std::string script = cmdl("e2e").str(); !script.empty()) {
        int sw = 1100, sh = 760;
        if (const char* ew = std::getenv("HANABI_WIN_W"); ew && *ew) sw = atoi(ew);
        if (const char* eh = std::getenv("HANABI_WIN_H"); eh && *eh) sh = atoi(eh);
        return run_e2e(script, sw, sh);
    }
#endif

    // --atlas-stress: fill the glyph atlas and prove the detector notices.
    if (cmdl["--atlas-stress"]) return run_atlas_stress();

    // --screenshot <path>: headless one-shot render + capture (docs/smoke).
    // Accepts both "--screenshot <path>" and "--screenshot=<path>".
    std::string shot = cmdl("screenshot").str();
    if (!shot.empty()) {
        int sw = 1100, sh = 760;
        if (const char* ew = std::getenv("HANABI_WIN_W"); ew && *ew) sw = atoi(ew);
        if (const char* eh = std::getenv("HANABI_WIN_H"); eh && *eh) sh = atoi(eh);
        return run_headless_screenshot(shot, sw, sh);
    }

    afterhours::graphics::RunConfig cfg;
    cfg.width = 1100;
    cfg.height = 760;
    // Include the build stamp in the title so a screenshot instantly reveals
    // WHICH binary is running (the "am I even on the new build?" blind spot).
    static std::string s_title =
        std::string(product_branding::kAppName) + "  ·  build " +
        HANABI_BUILD_STAMP;
    cfg.title = s_title.c_str();
    cfg.target_fps = 120;
    cfg.flags = afterhours::graphics::FLAG_WINDOW_RESIZABLE;
    cfg.init = app_init;
    cfg.frame = app_frame;
    cfg.cleanup = app_cleanup;

    afterhours::graphics::run(cfg);
    return 0;
}
