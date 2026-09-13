const std = @import("std");

// hanabi's build graph. `zig build --help` lists every step; the makefile that
// used to own this is gone, and `make <target>` forwards here (see makefile).
//
// The shape, in one paragraph: three C++ programs (the app, the scripted-UI
// build of the app, and one executable per test) are compiled from explicit
// source lists with the flags the project has always used, against two
// generated headers -- branding.h/Info.plist from resources/macos/branding.json
// and build_stamp.h from `git rev-parse` -- and installed under output/ at the
// paths every script in scripts/ already reads. Everything that RUNS (tests,
// gates, captures) is a step that depends on the install and is marked as
// having side effects, so it runs on every invocation rather than being served
// from the cache; everything that COMPILES is cached by content, so a header
// edit rebuilds exactly the objects that include it and a no-op build does
// nothing.
//
// Compiler: Apple's clang++ (the makefile's), one Run node per object with a
// depfile, one per link -- see `cxx` below for why zig's own clang is not
// used. zig is the driver and the cache; the toolchain, libc++ and deployment
// target are the ones every baseline was captured against.

const Kind = enum { unit, perf, app };

const TestSpec = struct {
    name: []const u8,
    kind: Kind,
    // -fobjc-arc across the whole compile (the make rule's API_LINK).
    arc: bool,
    srcs: []const []const u8,
    frameworks: []const []const u8,
};

// One executable per row; generated from the makefile's rules so nothing was
// dropped in the move (75 rules, 75 rows). `unit` compiles at -O0 -g, `perf`
// at -O2, `app` with the app's own flags (TLS included).
const tests = [_]TestSpec{
    .{ .name = "test_api", .kind = .unit, .arc = true, .srcs = &.{ "tests/unit/test_api.cpp", "src/api/config.cpp", "src/api/http_client.cpp", "src/api/agentcloud_client.cpp", "src/api/agentcloud_auth.cpp", "src/ws_socket.mm" }, .frameworks = &.{ "CFNetwork", "Foundation" } },
    .{ .name = "test_auth", .kind = .unit, .arc = true, .srcs = &.{ "tests/unit/test_auth.cpp", "src/api/auth.cpp", "src/api/config.cpp", "src/api/http_client.cpp", "src/api/agentcloud_client.cpp", "src/api/agentcloud_auth.cpp", "src/ws_socket.mm" }, .frameworks = &.{ "CFNetwork", "Foundation" } },
    .{ .name = "test_send", .kind = .unit, .arc = true, .srcs = &.{ "tests/unit/test_send.cpp", "src/api/config.cpp", "src/api/http_client.cpp", "src/api/agentcloud_client.cpp", "src/api/agentcloud_auth.cpp", "src/ws_socket.mm" }, .frameworks = &.{ "CFNetwork", "Foundation" } },
    .{ .name = "test_stream", .kind = .unit, .arc = true, .srcs = &.{ "tests/unit/test_stream.cpp", "src/api/config.cpp", "src/api/http_client.cpp", "src/api/agentcloud_client.cpp", "src/api/agentcloud_auth.cpp", "src/ws_socket.mm" }, .frameworks = &.{ "CFNetwork", "Foundation" } },
    .{ .name = "test_tools", .kind = .unit, .arc = true, .srcs = &.{ "tests/unit/test_tools.cpp", "src/api/config.cpp", "src/api/http_client.cpp", "src/api/agentcloud_client.cpp", "src/api/agentcloud_auth.cpp", "src/ws_socket.mm" }, .frameworks = &.{ "CFNetwork", "Foundation" } },
    .{ .name = "test_textinput", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_textinput.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_notify_events", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_notify_events.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_elicitation", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_elicitation.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_widget_key", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_widget_key.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_diff", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_diff.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_trend", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_trend.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_heap_walk", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_heap_walk.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_ellipsize", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_ellipsize.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_compaction", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_compaction.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_wrap_count", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_wrap_count.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_md_spans", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_md_spans.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_text_cache", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_text_cache.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_atlas_guard", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_atlas_guard.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_outbox", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_outbox.cpp", "src/api/disk_cache.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_shortcuts", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_shortcuts.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_input_pipeline", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_input_pipeline.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_find_nav", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_find_nav.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_find_memo", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_find_memo.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_digest_layout", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_digest_layout.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_subagent_parent_index", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_subagent_parent_index.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_sidebar_buckets", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_sidebar_buckets.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_folder_state", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_folder_state.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_home_buckets", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_home_buckets.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_contains_lower", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_contains_lower.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_session_index", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_session_index.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_snippet_text", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_snippet_text.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_tab_colors", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_tab_colors.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_theme_contrast", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_theme_contrast.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_a11y_bridge", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_a11y_bridge.mm", "src/a11y_bridge.mm" }, .frameworks = &.{ "AppKit" } },
    .{ .name = "test_anchored_surface", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_anchored_surface.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_slash_row", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_slash_row.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_menu_keys", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_menu_keys.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_activation", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_activation.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_tooltip", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_tooltip.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_overlay_lifecycle", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_overlay_lifecycle.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_control_state", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_control_state.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_global_hotkeys", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_global_hotkeys.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_settings_catalog", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_settings_catalog.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_reply_quote", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_reply_quote.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_focus_ring", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_focus_ring.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_ask_card", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_ask_card.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_tool_kinds", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_tool_kinds.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_transcript_reconcile", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_transcript_reconcile.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_pane_memory", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_pane_memory.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_transcript_item_index", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_transcript_item_index.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_widget_retire", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_widget_retire.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_gpu_mem", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_gpu_mem.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_texture_budget", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_texture_budget.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_downscale", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_downscale.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_footer_geometry", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_footer_geometry.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_secondary_surface", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_secondary_surface.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_minimap_marks", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_minimap_marks.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_minimap_scrub", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_minimap_scrub.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_frame_activity", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_frame_activity.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_latency", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_latency.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_new_thread", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_new_thread.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_focus_routing", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_focus_routing.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_follow_latch", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_follow_latch.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_data", .kind = .unit, .arc = true, .srcs = &.{ "tests/unit/test_data.cpp", "src/api/config.cpp", "src/api/http_client.cpp", "src/api/agentcloud_client.cpp", "src/api/agentcloud_auth.cpp", "src/ws_socket.mm", "src/api/disk_cache.cpp" }, .frameworks = &.{ "CFNetwork", "Foundation" } },
    .{ .name = "test_settings", .kind = .unit, .arc = true, .srcs = &.{ "tests/unit/test_settings.cpp", "src/settings.cpp", "src/api/config.cpp", "src/api/http_client.cpp", "src/api/agentcloud_client.cpp", "src/api/agentcloud_auth.cpp", "src/ws_socket.mm", "src/afterhours_files.cpp" }, .frameworks = &.{ "CFNetwork", "Foundation" } },
    .{ .name = "test_e2e", .kind = .unit, .arc = true, .srcs = &.{ "tests/e2e/test_e2e.cpp", "src/api/config.cpp", "src/api/http_client.cpp", "src/api/agentcloud_client.cpp", "src/api/agentcloud_auth.cpp", "src/ws_socket.mm" }, .frameworks = &.{ "CFNetwork", "Foundation" } },
    .{ .name = "test_perf", .kind = .perf, .arc = false, .srcs = &.{ "tests/e2e/test_perf.cpp", "src/api/disk_cache.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_real", .kind = .app, .arc = true, .srcs = &.{ "tests/e2e/test_real.cpp", "src/api/config.cpp", "src/api/http_client.cpp", "src/api/agentcloud_client.cpp", "src/api/agentcloud_auth.cpp", "src/ws_socket.mm" }, .frameworks = &.{ "CFNetwork", "Foundation" } },
    .{ .name = "test_agentcloud", .kind = .unit, .arc = true, .srcs = &.{ "tests/unit/test_agentcloud.cpp", "src/api/agentcloud_auth.cpp", "src/api/agentcloud_client.cpp", "src/ws_socket.mm" }, .frameworks = &.{ "CFNetwork", "Foundation" } },
    .{ .name = "test_menubar", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_menubar.mm", "src/menubar.mm", "src/settings.cpp", "src/afterhours_files.cpp" }, .frameworks = &.{ "AppKit", "Carbon" } },
    .{ .name = "test_agentcloud_local", .kind = .unit, .arc = true, .srcs = &.{ "tests/e2e/test_agentcloud_local.cpp", "src/api/agentcloud_auth.cpp", "src/api/agentcloud_client.cpp", "src/ws_socket.mm" }, .frameworks = &.{ "CFNetwork", "Foundation" } },
    .{ .name = "test_native_extras", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_native_extras.mm", "src/native_extras.mm" }, .frameworks = &.{ "AppKit", "Carbon", "CoreSpotlight", "CoreText", "MetalKit", "UniformTypeIdentifiers", "UserNotifications" } },
    .{ .name = "test_spotlight_catalog", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_spotlight_catalog.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_div_move", .kind = .unit, .arc = false, .srcs = &.{ "tests/unit/test_div_move.cpp" }, .frameworks = &.{  } },
    .{ .name = "test_agentcloud_real", .kind = .unit, .arc = true, .srcs = &.{ "tests/e2e/test_agentcloud_real.cpp", "src/api/agentcloud_auth.cpp", "src/api/agentcloud_client.cpp", "src/ws_socket.mm" }, .frameworks = &.{ "CFNetwork", "Foundation" } },
};

// The 72 the default `test` step runs, in the makefile's order: UNIT_TEST_EXES
// then test_e2e and test_perf. test_real, test_agentcloud_real and
// test_agentcloud_local are reached by their own steps.
const run_by_default = [_][]const u8{
    "test_native_extras",
    "test_menubar",
    "test_shortcuts",
    "test_spotlight_catalog",
    "test_api",
    "test_auth",
    "test_send",
    "test_stream",
    "test_tools",
    "test_textinput",
    "test_input_pipeline",
    "test_data",
    "test_settings",
    "test_agentcloud",
    "test_elicitation",
    "test_ask_card",
    "test_notify_events",
    "test_find_nav",
    "test_session_index",
    "test_subagent_parent_index",
    "test_sidebar_buckets",
    "test_folder_state",
    "test_home_buckets",
    "test_contains_lower",
    "test_snippet_text",
    "test_diff",
    "test_ellipsize",
    "test_compaction",
    "test_trend",
    "test_tab_colors",
    "test_footer_geometry",
    "test_secondary_surface",
    "test_pane_memory",
    "test_transcript_reconcile",
    "test_tool_kinds",
    "test_transcript_item_index",
    "test_wrap_count",
    "test_md_spans",
    "test_text_cache",
    "test_widget_retire",
    "test_gpu_mem",
    "test_texture_budget",
    "test_downscale",
    "test_digest_layout",
    "test_heap_walk",
    "test_minimap_scrub",
    "test_minimap_marks",
    "test_focus_ring",
    "test_outbox",
    "test_atlas_guard",
    "test_frame_activity",
    "test_latency",
    "test_follow_latch",
    "test_new_thread",
    "test_focus_routing",
    "test_theme_contrast",
    "test_a11y_bridge",
    "test_anchored_surface",
    "test_activation",
    "test_menu_keys",
    "test_slash_row",
    "test_tooltip",
    "test_overlay_lifecycle",
    "test_control_state",
    "test_find_memo",
    "test_div_move",
    "test_widget_key",
    "test_settings_catalog",
    "test_reply_quote",
    "test_global_hotkeys",
    "test_e2e",
    "test_perf",
};

const app_sources = [_][]const u8{
    "src/api/agentcloud_auth.cpp",  "src/api/agentcloud_client.cpp", "src/api/auth.cpp",
    "src/api/config.cpp",           "src/api/disk_cache.cpp",        "src/api/http_client.cpp",
    "src/api/token_store.cpp",      "src/main.cpp",                  "src/preload.cpp",
    "src/settings.cpp",             "src/ui/font_system.cpp",        "src/util/process.cpp",
    "src/afterhours_files.cpp",
};
// Compiled on its own with the commit hash as a define; see build_stamp.h.
const app_stamp_source = [_][]const u8{"src/build_stamp.cpp"};
const app_objc_sources = [_][]const u8{
    "src/a11y_bridge.mm", "src/gpu_mem.mm", "src/menubar.mm", "src/native_extras.mm", "src/sokol_impl.mm",
};
// ws_socket.mm is the one ObjC++ file compiled under ARC.
const app_arc_sources = [_][]const u8{"src/ws_socket.mm"};

const frameworks = [_][]const u8{
    "CoreFoundation", "CoreServices", "CoreText",           "Metal",        "MetalKit",
    "Cocoa",          "QuartzCore",   "Carbon",             "CoreSpotlight", "UniformTypeIdentifiers",
    "UserNotifications",
};

const cxx_std = "-std=c++23";
// CXXFLAGS_BASE + CXXFLAGS_SUPPRESS, verbatim from the makefile. The
// suppressions are the project's stated third-party policy: they silence
// patterns in vendored headers, not in hanabi's own sources, which are held to
// -Wall -Wextra -Wpedantic clean.
const app_warnings = [_][]const u8{
    "-Wall",                        "-Wextra",                        "-Wpedantic",
    "-Wno-deprecated-volatile",     "-Wno-missing-field-initializers", "-Wno-c99-extensions",
    "-Wno-unused-function",         "-Wno-sign-conversion",           "-Wno-deprecated-literal-operator",
};
const test_warnings = [_][]const u8{
    "-Wall", "-Wextra", "-Wno-deprecated-literal-operator", "-Wno-sign-conversion",
};

pub fn build(b: *std.Build) void {
    // Everything lands in output/ -- the paths scripts/*.sh, the screenshot
    // suite and the docs already use -- not zig-out/.
    b.resolveInstallPrefix(b.pathFromRoot("output"), .{ .exe_dir = b.pathFromRoot("output") });

    // No -Dtarget / -Doptimize: the compiler is Apple clang++ for the host,
    // and optimisation is stated per source group (-O2 app, -O0 unit tests,
    // -O2 perf), the way the makefile did it.

    const opt = b.option(u8, "opt", "App optimisation level (0-3; default 2, the makefile's OPT)") orelse 2;
    const openssl_prefix = detectOpenSsl(b);
    const tls = b.option(bool, "tls", "Link OpenSSL for https backends (default: on when `brew --prefix openssl@3` resolves)") orelse (openssl_prefix != null);
    // `zig build` reports a C translation unit's diagnostics only when it
    // FAILS; a warning on a successful compile is dropped on the floor
    // (measured: an unused-parameter warning `zig c++` prints is silent under
    // `zig build`). So warnings are errors here by default -- the toolchain is
    // pinned, so the set is deterministic -- and every warning is either fixed
    // or an explicit, named suppression in app_warnings. -Dwerror=false is the
    // escape hatch for reading past one while working.
    const werror = b.option(bool, "werror", "Treat compiler warnings as errors (default true; a warning is fixed, never hidden)") orelse true;
    if (tls and openssl_prefix == null) {
        std.debug.print("-Dtls=true but `brew --prefix openssl@3` resolved nothing; install openssl@3 or build with -Dtls=false\n", .{});
        std.process.exit(2);
    }

    // ---- generated headers -------------------------------------------------
    // branding.h + Info.plist, from the config and the template: an output
    // directory of a Run step, so the header is regenerated when either input
    // (or the generator) changes and the compile that includes it follows.
    // The four identity fields default to resources/macos/branding.json;
    // -Dapp-name=Ember -Dbundle-id=... -Dexecutable-name=... -Durl-scheme=...
    // build an isolated alternate identity (the makefile's APP_NAME= etc.).
    const brand_overrides = [_][2]?[]const u8{
        .{ "--app-name", b.option([]const u8, "app-name", "Branding: display name (default from branding.json)") },
        .{ "--bundle-id", b.option([]const u8, "bundle-id", "Branding: bundle identifier") },
        .{ "--executable-name", b.option([]const u8, "executable-name", "Branding: executable name inside the bundle") },
        .{ "--url-scheme", b.option([]const u8, "url-scheme", "Branding: URL scheme") },
    };
    app_name_override = brand_overrides[0][1];
    const brand = b.addSystemCommand(&.{"/usr/bin/python3"});
    brand.addFileArg(b.path("scripts/branding.py"));
    brand.addArg("--config");
    brand.addFileArg(b.path("resources/macos/branding.json"));
    brand.addArg("generate");
    for (brand_overrides) |pair| if (pair[1]) |v| brand.addArgs(&.{ pair[0].?, v });
    brand.addArg("--template");
    brand.addFileArg(b.path("resources/macos/Info.plist"));
    brand.addArg("--output-dir");
    const branding_dir = brand.addOutputDirectoryArg("branding");
    const info_plist = branding_dir.path(b, "Info.plist");

    // The build stamp: the short hash of HEAD, re-read on every `zig build`,
    // handed as a define to src/build_stamp.cpp ALONE. A changed hash
    // recompiles that three-line file and relinks; nothing else sees it. (A
    // generated header on an include path would have re-keyed every object
    // in the tree -- the path of a content-addressed directory is part of
    // each compile's command line -- measured as a full rebuild per commit.)
    const stamp_define = b.fmt("-DHANABI_BUILD_STAMP_VALUE=\"{s}\"", .{gitShortHead(b)});

    // ---- the app, and its scripted-UI twin ----------------------------------
    const opt_flag = b.fmt("-O{d}", .{opt});
    const app = makeApp(b, .{
        .name = "hanabi",
        .opt_flag = opt_flag,
        .tls = if (tls) openssl_prefix else null,
        .e2e = false,
        .werror = werror,
        .branding_dir = branding_dir,
        .stamp_define = stamp_define,
    });
    // `output/hanabi.exe`: the extension the whole repo keys on.
    const install_app = b.addInstallFileWithDir(app, .prefix, "hanabi.exe");
    const resources = b.addInstallDirectory(.{ .source_dir = b.path("resources"), .install_dir = .prefix, .install_subdir = "resources" });
    b.getInstallStep().dependOn(&install_app.step);
    b.getInstallStep().dependOn(&resources.step);

    const uitest = makeApp(b, .{
        .name = "hanabi_uitest",
        .opt_flag = opt_flag,
        .tls = if (tls) openssl_prefix else null,
        .e2e = true,
        .werror = werror,
        .branding_dir = branding_dir,
        .stamp_define = stamp_define,
    });
    const install_uitest = b.addInstallFileWithDir(uitest, .prefix, "hanabi_uitest.exe");
    const uitest_build = b.step("uitest-build", "Build output/hanabi_uitest.exe (the scripted-UI harness) without running it");
    uitest_build.dependOn(&install_uitest.step);
    uitest_build.dependOn(&resources.step);

    // ---- run -----------------------------------------------------------------
    const run = b.step("run", "Build, then run the app (reads .env; the mock when HANABI_AC_HOST is unset)");
    run.dependOn(&command(b, &.{ "bash", "scripts/run_app.sh" }, b.getInstallStep()).step);
    const run_mock = b.step("run-mock", "Build, then run the app against the offline mock backend");
    const rm = command(b, &.{"output/hanabi.exe"}, b.getInstallStep());
    rm.setEnvironmentVariable("HANABI_BACKEND", "mock");
    run_mock.dependOn(&rm.step);

    // ---- tests: one executable each, installed under output/tests/ ----------
    var installed = std.StringHashMap(*std.Build.Step).init(b.allocator);
    const tests_build = b.step("tests-build", "Compile every test executable into output/tests/ without running any");
    for (tests) |spec| {
        const exe = makeTest(b, spec, .{
            .opt_flag = opt_flag,
            .tls = if (tls) openssl_prefix else null,
            .werror = werror,
            .branding_dir = branding_dir,
        });
        const inst = b.addInstallFileWithDir(exe, .{ .custom = "tests" }, spec.name);
        installed.put(spec.name, &inst.step) catch @panic("oom");
        tests_build.dependOn(&inst.step);
        // `zig build test_<name>` builds and runs that one test from the repo
        // root, the way the makefile's `output/tests/test_<name>` target did.
        const one = b.step(spec.name, b.fmt("Build and run output/tests/{s}", .{spec.name}));
        const r = command(b, &.{b.fmt("output/tests/{s}", .{spec.name})}, &inst.step);
        one.dependOn(&r.step);
    }

    // The 72 in one summary line, in order, through the runner the makefile's
    // RUN_TESTS macro became.
    const unit = b.step("unit", "Build and run the unit + e2e + perf test executables (72), with a pass/fail summary");
    {
        var argv = std.ArrayList([]const u8).empty;
        argv.append(b.allocator, "bash") catch @panic("oom");
        argv.append(b.allocator, "scripts/run_unit_tests.sh") catch @panic("oom");
        for (run_by_default) |n| argv.append(b.allocator, b.fmt("output/tests/{s}", .{n})) catch @panic("oom");
        const r = b.addSystemCommand(argv.items);
        r.setCwd(b.path("."));
        r.has_side_effects = true;
        for (run_by_default) |n| r.step.dependOn(installed.get(n).?);
        r.step.dependOn(b.getInstallStep());
        unit.dependOn(&r.step);
    }

    // The makefile's two subsets, same runner.
    {
        const e2e = b.step("e2e", "Build and run test_e2e alone, with the summary line");
        const r = command(b, &.{ "bash", "scripts/run_unit_tests.sh", "output/tests/test_e2e" }, installed.get("test_e2e").?);
        e2e.dependOn(&r.step);
        const ue = b.step("unit-e2e", "The 72 less test_perf");
        var argv = std.ArrayList([]const u8).empty;
        argv.appendSlice(b.allocator, &.{ "bash", "scripts/run_unit_tests.sh" }) catch @panic("oom");
        for (run_by_default) |n| if (!std.mem.eql(u8, n, "test_perf")) argv.append(b.allocator, b.fmt("output/tests/{s}", .{n})) catch @panic("oom");
        const r2 = command(b, argv.items, null);
        for (run_by_default) |n| r2.step.dependOn(installed.get(n).?);
        ue.dependOn(&r2.step);
    }

    // ---- the gates -----------------------------------------------------------
    // Each is a top-level step of its own, AND a member of `test`, which runs
    // them in the makefile's order, one after another (they measure the
    // machine, and two at once would measure each other). The two roles are
    // two Run instances of the same recipe: chaining the top-level steps
    // themselves would make `zig build uitest` run everything before it.
    const need_app = b.getInstallStep();
    const need_ui = uitest_build;
    const R = Recipe;
    const gates = [_]R{
        .{ .name = "uitest", .desc = "Run the scripted-UI suite (tests/ui/*.e2e) against output/hanabi_uitest.exe", .cmds = &.{&.{ "bash", "scripts/run_ui_tests.sh" }}, .after = need_ui },
        .{ .name = "uitest-shuffle", .desc = "The scripted-UI suite in a random order (HANABI_UI_SEED=<n> to replay)", .cmds = &.{&.{ "bash", "scripts/run_ui_tests.sh" }}, .after = need_ui, .in_test = false },
        .{ .name = "uitest-alone", .desc = "Every scripted-UI script alone, one harness start each (slow, on purpose)", .cmds = &.{&.{ "bash", "scripts/run_ui_tests_alone.sh" }}, .after = need_ui, .in_test = false },
        .{ .name = "harness-gate", .desc = "The harness's own tests", .cmds = &.{&.{ "bash", "scripts/harness_gate.sh" }}, .after = need_ui },
        .{ .name = "tab-persistence-gate", .desc = "Tabs survive a restart", .cmds = &.{&.{ "bash", "scripts/tab_persistence_gate.sh" }}, .after = need_ui },
        .{ .name = "verify-vendor-patches", .desc = "Prove each vendor proof patch is absent from the pin and applies to it", .cmds = &.{&.{ "python3", "scripts/verify_vendor_patches.py" }}, .after = null },
        .{ .name = "chime-gate", .desc = "Run-finished cue", .cmds = &.{&.{ "bash", "scripts/chime_gate.sh" }}, .after = need_app },
        .{ .name = "ask-contrast-gate", .desc = "Ask-card contrast", .cmds = &.{&.{ "python3", "scripts/ask_contrast_gate.py" }}, .after = null },
        .{ .name = "measure-launch", .desc = "Launch/RSS perf gate", .cmds = &.{&.{ "bash", "scripts/measure_launch.sh" }}, .after = need_app },
        .{ .name = "perf-transcript-slope", .desc = "Transcript slope gate", .cmds = &.{&.{ "bash", "scripts/perf_transcript_slope.sh" }}, .after = need_app },
        .{ .name = "perf-text-gate", .desc = "Text measurement gate", .cmds = &.{&.{ "bash", "scripts/perf_text_gate.sh" }}, .after = need_app },
        .{ .name = "find-gate", .desc = "Find level gate", .cmds = &.{&.{ "bash", "scripts/find_gate.sh" }}, .after = need_app },
        .{ .name = "soak-gate", .desc = "Soak gate", .cmds = &.{&.{ "bash", "scripts/soak_gate.sh" }}, .after = need_app },
        .{ .name = "stress-resize-gate", .desc = "Stress-resize gate", .cmds = &.{&.{ "bash", "scripts/stress_resize_gate.sh" }}, .after = need_app },
        .{ .name = "latency-delay-sweep", .desc = "Latency instrument controls", .cmds = &.{&.{ "bash", "scripts/latency_delay_sweep.sh" }}, .after = need_ui },
        .{ .name = "alloc-gate", .desc = "Allocation gate", .cmds = &.{&.{ "bash", "scripts/alloc_gate.sh" }}, .after = need_app },
        .{ .name = "idle-gate", .desc = "Idle gate", .cmds = &.{&.{ "bash", "scripts/idle_gate.sh" }}, .after = need_app },
        .{ .name = "scaling-gate", .desc = "Scaling gate", .cmds = &.{&.{ "bash", "scripts/scaling_gate.sh" }}, .after = need_app },
        .{ .name = "memory-scaling-gate", .desc = "Memory scaling gate", .cmds = &.{&.{ "bash", "scripts/memory_scaling_gate.sh" }}, .after = need_app },
        .{ .name = "scroll-gate", .desc = "Scroll gate", .cmds = &.{&.{ "bash", "scripts/scroll_gate.sh" }}, .after = need_app },
        .{ .name = "retire-gate", .desc = "Widget retire gate", .cmds = &.{&.{ "bash", "scripts/retire_gate.sh" }}, .after = need_app },
        .{ .name = "digest-gate", .desc = "Digest gate (selftest, then the gate)", .cmds = &.{ &.{ "bash", "scripts/digest_gate.sh", "--selftest" }, &.{ "bash", "scripts/digest_gate.sh" } }, .after = need_app },
        .{ .name = "home-scan-gate", .desc = "Home scan gate (checker, selftest, gate)", .cmds = &.{ &.{ "/usr/bin/python3", "scripts/check_home_scan.py" }, &.{ "bash", "scripts/home_scan_gate.sh", "--selftest" }, &.{ "bash", "scripts/home_scan_gate.sh" } }, .after = need_app },
        .{ .name = "subagent-index-gate", .desc = "Subagent index gate", .cmds = &.{&.{ "bash", "scripts/subagent_index_gate.sh", "output/hanabi.exe" }}, .after = need_app },
        .{ .name = "sidebar-scan-gate", .desc = "Sidebar scan gate (selftest, then the gate)", .cmds = &.{ &.{ "bash", "scripts/sidebar_scan_gate.sh", "--selftest" }, &.{ "bash", "scripts/sidebar_scan_gate.sh", "output/hanabi.exe" } }, .after = need_app },
        .{ .name = "bounds-gate", .desc = "Bounds gate", .cmds = &.{&.{ "bash", "scripts/bounds_gate.sh" }}, .after = need_app },
        .{ .name = "validate-screenshots-fast", .desc = "The 10-scene screenshot subset", .cmds = &.{&.{ "bash", "scripts/validate_screenshots.sh", "--fast" }}, .after = need_app },
        .{ .name = "events-gate", .desc = "Events gate", .cmds = &.{&.{ "bash", "scripts/events_gate.sh" }}, .after = need_app },
        .{ .name = "atlas-gate", .desc = "Font atlas gate", .cmds = &.{&.{ "bash", "scripts/atlas_gate.sh" }}, .after = need_app },
        .{ .name = "chrome-gate", .desc = "Composer chrome gate", .cmds = &.{&.{ "bash", "scripts/composer_chrome_gate.sh" }}, .after = need_app },
        // Not in `test`:
        .{ .name = "validate-screenshots", .desc = "Capture every baselined scene and compare against docs/screenshots/baselines", .cmds = &.{&.{ "bash", "scripts/validate_screenshots.sh" }}, .after = need_app, .in_test = false },
        .{ .name = "update-baselines", .desc = "Recapture baselines (SHOT_FILTER=<regex> to narrow); review the PNGs before committing", .cmds = &.{&.{ "bash", "scripts/validate_screenshots.sh", "--update" }}, .after = need_app, .in_test = false },
        .{ .name = "test-screenshot-determinism", .desc = "Capture 01_home_dark twice and require byte-identical output", .cmds = &.{&.{ "bash", "scripts/validate_screenshots.sh", "--determinism" }}, .after = need_app, .in_test = false },
        .{ .name = "screens", .desc = "Capture every scene into output/screenshots/current", .cmds = &.{&.{ "bash", "scripts/screens.sh" }}, .after = need_app, .in_test = false },
        .{ .name = "gate-audit", .desc = "Gate audit (DEFECT=<n> through the environment)", .cmds = &.{&.{ "/usr/bin/python3", "scripts/gate_audit.py" }}, .after = need_app, .in_test = false },
        .{ .name = "soak", .desc = "Long soak (HANABI_SOAK_LONG_FRAMES / _ARMS / _JOBS through the environment)", .cmds = &.{&.{ "bash", "scripts/soak.sh" }}, .after = need_app, .in_test = false },
        .{ .name = "soak-report", .desc = "Soak report into output/soak-report.txt, diffed against docs/perf/soak-baseline.txt", .cmds = &.{&.{ "bash", "scripts/soak_report.sh" }}, .after = need_app, .in_test = false },
        .{ .name = "soak-baseline", .desc = "Rewrite docs/perf/soak-baseline.txt from a soak run", .cmds = &.{&.{ "bash", "scripts/soak_report.sh", "--baseline" }}, .after = need_app, .in_test = false },
        .{ .name = "stress", .desc = "Stress scenario (SCENARIO/FRAMES/EVERY/SESSIONS/UNTIL through the environment)", .cmds = &.{&.{ "bash", "scripts/stress.sh" }}, .after = need_app, .in_test = false },
        .{ .name = "stress-break", .desc = "The stress scenario at breaking defaults", .cmds = &.{&.{ "bash", "scripts/stress.sh", "--break" }}, .after = need_app, .in_test = false },
        .{ .name = "mock-server", .desc = "Run tools/mock_server (MOCK_PORT through the environment, default 8787)", .cmds = &.{&.{ "bash", "scripts/mock_server.sh" }}, .after = null, .in_test = false },
        .{ .name = "install-hooks", .desc = "Install the pre-push hook (runs `zig build gate` on push)", .cmds = &.{&.{ "bash", "scripts/install_hooks.sh" }}, .after = null, .in_test = false },
        .{ .name = "count", .desc = "Line counts of the tracked sources", .cmds = &.{&.{ "bash", "scripts/count.sh" }}, .after = null, .in_test = false },
    };
    for (gates) |g| _ = gateStep(b, g);

    // The local round-trip, test_real and friends want a test executable.
    const ac_local_recipe = R{ .name = "test-agentcloud-local", .desc = "Round-trip against the local mock orchestrator (test_agentcloud_local)", .cmds = &.{&.{ "bash", "scripts/test_agentcloud_local.sh" }}, .after = installed.get("test_agentcloud_local").? };
    _ = gateStep(b, ac_local_recipe);
    _ = gateStep(b, .{ .name = "latency-gate", .desc = "test_latency, then the latency sweep", .cmds = &.{ &.{"output/tests/test_latency"}, &.{ "bash", "scripts/latency_delay_sweep.sh" } }, .after = installed.get("test_latency").?, .in_test = false });
    _ = gateStep(b, .{ .name = "test-real", .desc = "Read-only smoke against the real backend (needs -Dtls=true and .env)", .cmds = &.{&.{"output/tests/test_real"}}, .after = installed.get("test_real").?, .in_test = false });
    _ = gateStep(b, .{ .name = "test-agentcloud-real", .desc = "Live agentcloud round-trip (needs .env)", .cmds = &.{&.{ "bash", "scripts/run_with_env.sh", "output/tests/test_agentcloud_real" }}, .after = installed.get("test_agentcloud_real").?, .in_test = false });

    // The branding checker compares against the generated directory, so the
    // runner is handed it as an argument (which also makes it depend on the
    // generator). Two instances again: the step, and the chain's tail.
    const source_checks = b.step("source-checks", "Branding, vocabulary, gap references and the other source checkers");
    {
        const sc = command(b, &.{ "bash", "scripts/source_checks.sh", "--branding-dir" }, null);
        sc.addDirectoryArg(branding_dir);
        source_checks.dependOn(&sc.step);
    }

    // `zig build test`: the makefile's `test`, same members, same order, each
    // waiting for the one before it.
    const test_step = b.step("test", "Everything `make test` ran: unit+e2e+perf, the local round-trip, the UI suite, every gate, the fast screenshots, source checks -- in order");
    {
        var prev: *std.Build.Step = unit;
        prev = chainRecipe(b, ac_local_recipe, prev);
        for (gates) |g| {
            if (!g.in_test) continue;
            prev = chainRecipe(b, g, prev);
        }
        const sc = command(b, &.{ "bash", "scripts/source_checks.sh", "--branding-dir" }, prev);
        sc.addDirectoryArg(branding_dir);
        test_step.dependOn(&sc.step);
    }

    // `perf`: the perf micro-benchmark and the four perf gates, in order.
    const perf = b.step("perf", "test_perf, then the launch, slope, text and find gates");
    {
        const perf_run = command(b, &.{"output/tests/test_perf"}, installed.get("test_perf").?);
        perf_run.step.dependOn(need_app);
        var prev: *std.Build.Step = &perf_run.step;
        for (gates) |g| {
            for ([_][]const u8{ "measure-launch", "perf-transcript-slope", "perf-text-gate", "find-gate" }) |want| {
                if (std.mem.eql(u8, g.name, want)) prev = chainRecipe(b, g, prev);
            }
        }
        perf.dependOn(prev);
    }

    // `gate`: test, then the full screenshot suite.
    const full_gate = b.step("gate", "`test` and then `validate-screenshots`");
    {
        var prev: *std.Build.Step = test_step;
        for (gates) |g| if (std.mem.eql(u8, g.name, "validate-screenshots")) {
            prev = chainRecipe(b, g, prev);
        };
        full_gate.dependOn(prev);
    }

    // ---- the .app bundle -----------------------------------------------------
    const bundle = b.step("bundle", b.fmt("Package output/{s}.app from the installed binary, resources and Info.plist", .{appName(b)}));
    const pack = b.addSystemCommand(&.{ "bash", "scripts/package_macos_app.sh", b.fmt("output/{s}.app", .{appName(b)}), "output/hanabi.exe", "output/resources" });
    pack.addFileArg(info_plist);
    pack.addArg(appVersion(b));
    pack.setCwd(b.path("."));
    pack.has_side_effects = true;
    pack.step.dependOn(b.getInstallStep());
    bundle.dependOn(&pack.step);
    const app_step = b.step("app", "The bundle, with TLS required (use -Dtls=true; refuses without OpenSSL)");
    if (!tls) {
        const refuse = b.addFail("`zig build app` needs OpenSSL (brew install openssl@3) and -Dtls=true");
        app_step.dependOn(&refuse.step);
    } else app_step.dependOn(bundle);
    for ([_][2][]const u8{
        .{ "verify-app", "verify" },  .{ "register-app", "register" }, .{ "unregister-app", "unregister" },
        .{ "install-app", "install" }, .{ "uninstall-app", "uninstall" },
    }) |pair| {
        const st = b.step(pair[0], b.fmt("scripts/manage_macos_app.sh {s} on the bundle", .{pair[1]}));
        const script: []const u8 = if (std.mem.eql(u8, pair[1], "verify")) "scripts/verify_macos_app.sh" else "scripts/manage_macos_app.sh";
        const c = b.addSystemCommand(&.{ "bash", script });
        if (!std.mem.eql(u8, pair[1], "verify")) c.addArg(pair[1]);
        c.addArg(b.fmt("output/{s}.app", .{appName(b)}));
        if (!std.mem.eql(u8, pair[1], "verify")) c.addArg(b.graph.environ_map.get("APP_INSTALL_DIR") orelse b.fmt("{s}/Applications/{s}.app", .{ b.graph.environ_map.get("HOME") orelse "~", appName(b) }));
        c.addFileArg(info_plist);
        c.setCwd(b.path("."));
        c.has_side_effects = true;
        if (std.mem.eql(u8, pair[1], "unregister") or std.mem.eql(u8, pair[1], "uninstall")) {
            c.step.dependOn(&brand.step);
        } else c.step.dependOn(app_step);
        st.dependOn(&c.step);
    }

    const launch_app = b.step("launch-app", "register-app, then `open -na` the bundle");
    const open_cmd = command(b, &.{ "open", "-na", b.fmt("output/{s}.app", .{appName(b)}) }, null);
    if (b.top_level_steps.get("register-app")) |reg| open_cmd.step.dependOn(&reg.step);
    launch_app.dependOn(&open_cmd.step);

}

const AppOpts = struct {
    name: []const u8,
    opt_flag: []const u8,
    tls: ?[]const u8,
    e2e: bool,
    werror: bool,
    branding_dir: std.Build.LazyPath,
    stamp_define: []const u8,
};

// The compiler. Apple's clang++ from the active Xcode toolchain, the same
// binary the makefile called, driving the same flags -- so the objects, the
// libc++ and the deployment target are the ones every baseline was captured
// against. (zig's own clang -- Homebrew clang 21 with a static libc++ -- was
// tried first: it built and passed 72/72 + 315/315, and moved 31 of 164
// screenshot baselines: centred text a subpixel, and the minimap's mark
// colours outright. A compiler swap is not an explained pixel change, so it
// is not the compiler.) Each object is its own node with a depfile, so a
// header edit rebuilds exactly the objects that include it.
const cxx = "clang++";

fn appFlags(b: *std.Build, opts: AppOpts) []const []const u8 {
    var f = std.ArrayList([]const u8).empty;
    f.append(b.allocator, cxx_std) catch @panic("oom");
    f.appendSlice(b.allocator, &.{ "-g", opts.opt_flag, "-pipe", "-fno-common" }) catch @panic("oom");
    f.appendSlice(b.allocator, &app_warnings) catch @panic("oom");
    f.appendSlice(b.allocator, &.{ "-include", b.pathFromRoot("src/build_config.h") }) catch @panic("oom");
    if (opts.werror) f.append(b.allocator, "-Werror") catch @panic("oom");
    if (opts.tls) |prefix| f.appendSlice(b.allocator, &.{ "-DHANABI_ENABLE_TLS", b.fmt("-I{s}/include", .{prefix}) }) catch @panic("oom");
    if (opts.e2e) f.append(b.allocator, "-DAFTER_HOURS_ENABLE_E2E_TESTING") catch @panic("oom");
    return f.items;
}

fn appIncludes(b: *std.Build) []const []const u8 {
    return b.dupeStrings(&.{ "-isystem", b.pathFromRoot("vendor"), "-isystem", b.pathFromRoot("vendor/afterhours/vendor") });
}

fn appLinkFlags(b: *std.Build, tls: ?[]const u8) []const []const u8 {
    var f = std.ArrayList([]const u8).empty;
    f.appendSlice(b.allocator, &.{ "-L", b.pathFromRoot(".") }) catch @panic("oom");
    for (frameworks) |fw| f.appendSlice(b.allocator, &.{ "-framework", fw }) catch @panic("oom");
    if (tls) |prefix| f.appendSlice(b.allocator, &.{ b.fmt("-L{s}/lib", .{prefix}), "-lssl", "-lcrypto", "-framework", "Security" }) catch @panic("oom");
    return f.items;
}

const Lang = enum { by_extension, objcpp_arc };

// One object: `clang++ <flags> <includes> -I<branding> -c <src> -o <obj> -MD
// -MF <dep>`. The source, the branding directory and every header the
// depfile names are the node's inputs; a change to any of them recompiles
// this object and nothing else. Stderr (a warning under -Dwerror=false) is
// shown even when the compile succeeds -- zig prints a Run step's stderr
// regardless of its result -- which is the property the built-in C compile
// step lacks.
fn compileObject(b: *std.Build, src: []const u8, flags: []const []const u8, includes: []const []const u8, branding_dir: std.Build.LazyPath, lang: Lang) std.Build.LazyPath {
    const r = b.addSystemCommand(&.{cxx});
    r.setName(b.fmt("clang++ {s}", .{src}));
    if (lang == .objcpp_arc) r.addArgs(&.{ "-ObjC++", "-fobjc-arc" });
    r.addArgs(flags);
    r.addArgs(includes);
    r.addArg("-I");
    r.addDirectoryArg(branding_dir);
    r.addArg("-c");
    r.addFileArg(b.path(src));
    r.addArg("-o");
    const stem = std.fs.path.stem(std.fs.path.basename(src));
    const obj = r.addOutputFileArg(b.fmt("{s}.o", .{stem}));
    r.addArgs(&.{ "-MD", "-MF" });
    _ = r.addDepFileOutputArg(b.fmt("{s}.d", .{stem}));
    return obj;
}

// One link: the objects are file inputs, the executable the output.
fn linkExecutable(b: *std.Build, name: []const u8, objects: []const std.Build.LazyPath, link_flags: []const []const u8) std.Build.LazyPath {
    const r = b.addSystemCommand(&.{cxx});
    r.setName(b.fmt("link {s}", .{name}));
    for (objects) |o| r.addFileArg(o);
    r.addArgs(link_flags);
    r.addArg("-o");
    return r.addOutputFileArg(name);
}

fn makeApp(b: *std.Build, opts: AppOpts) std.Build.LazyPath {
    const flags = appFlags(b, opts);
    const includes = appIncludes(b);
    var objects = std.ArrayList(std.Build.LazyPath).empty;
    for (app_sources) |src| objects.append(b.allocator, compileObject(b, src, flags, includes, opts.branding_dir, .by_extension)) catch @panic("oom");
    for (app_objc_sources) |src| objects.append(b.allocator, compileObject(b, src, flags, includes, opts.branding_dir, .by_extension)) catch @panic("oom");
    for (app_arc_sources) |src| objects.append(b.allocator, compileObject(b, src, flags, includes, opts.branding_dir, .objcpp_arc)) catch @panic("oom");
    {
        var f = std.ArrayList([]const u8).empty;
        f.appendSlice(b.allocator, flags) catch @panic("oom");
        f.append(b.allocator, opts.stamp_define) catch @panic("oom");
        for (app_stamp_source) |src| objects.append(b.allocator, compileObject(b, src, f.items, includes, opts.branding_dir, .by_extension)) catch @panic("oom");
    }
    return linkExecutable(b, b.fmt("{s}.exe", .{opts.name}), objects.items, appLinkFlags(b, opts.tls));
}

const TestOpts = struct {
    opt_flag: []const u8,
    tls: ?[]const u8,
    werror: bool,
    branding_dir: std.Build.LazyPath,
};

fn makeTest(b: *std.Build, spec: TestSpec, opts: TestOpts) std.Build.LazyPath {
    // TEST_INCLUDES: -isystem vendor/ -I. -I<branding>
    const includes = b.dupeStrings(&.{ "-isystem", b.pathFromRoot("vendor"), "-I", b.pathFromRoot(".") });
    var f = std.ArrayList([]const u8).empty;
    var link = std.ArrayList([]const u8).empty;
    switch (spec.kind) {
        .unit => {
            f.appendSlice(b.allocator, &.{ cxx_std, "-g", "-O0" }) catch @panic("oom");
            f.appendSlice(b.allocator, &test_warnings) catch @panic("oom");
            if (opts.werror) f.append(b.allocator, "-Werror") catch @panic("oom");
        },
        .perf => {
            f.appendSlice(b.allocator, &.{ cxx_std, "-O2" }) catch @panic("oom");
            f.appendSlice(b.allocator, &test_warnings) catch @panic("oom");
            if (opts.werror) f.append(b.allocator, "-Werror") catch @panic("oom");
        },
        .app => {
            // test_real / test_agentcloud_local: the app's own flags, TLS too.
            f.appendSlice(b.allocator, appFlags(b, .{
                .name = spec.name,
                .opt_flag = opts.opt_flag,
                .tls = opts.tls,
                .e2e = false,
                .werror = opts.werror,
                .branding_dir = opts.branding_dir,
                .stamp_define = "",
            })) catch @panic("oom");
            link.appendSlice(b.allocator, appLinkFlags(b, opts.tls)) catch @panic("oom");
        },
    }
    if (spec.arc) f.append(b.allocator, "-fobjc-arc") catch @panic("oom");
    var objects = std.ArrayList(std.Build.LazyPath).empty;
    for (spec.srcs) |src| objects.append(b.allocator, compileObject(b, src, f.items, includes, opts.branding_dir, .by_extension)) catch @panic("oom");
    for (spec.frameworks) |fw| link.appendSlice(b.allocator, &.{ "-framework", fw }) catch @panic("oom");
    return linkExecutable(b, spec.name, objects.items, link.items);
}

// A command run from the repo root that always runs (tests and gates observe
// the machine and the tree; a cached "pass" would be a pass nobody re-earned).
fn command(b: *std.Build, argv: []const []const u8, after: ?*std.Build.Step) *std.Build.Step.Run {
    const r = b.addSystemCommand(argv);
    r.setCwd(b.path("."));
    r.has_side_effects = true;
    if (after) |a| r.step.dependOn(a);
    return r;
}

const Recipe = struct {
    name: []const u8,
    desc: []const u8,
    // One or more commands, run in order from the repo root.
    cmds: []const []const []const u8,
    // What has to be built first (the install, the uitest binary, one test).
    after: ?*std.Build.Step,
    // Whether `zig build test` runs it.
    in_test: bool = true,
};

// The recipe as its own top-level step.
fn gateStep(b: *std.Build, r: Recipe) *std.Build.Step {
    const st = b.step(r.name, r.desc);
    st.dependOn(chainRecipe(b, r, r.after));
    return st;
}

// A fresh instance of the recipe's commands, in order, after `prev`; returns
// the last one so a caller can keep chaining.
fn chainRecipe(b: *std.Build, r: Recipe, prev: ?*std.Build.Step) *std.Build.Step {
    var last: ?*std.Build.Step = prev;
    if (r.after) |a| {
        // Both the build it needs and whatever it is queued behind.
        const first = command(b, r.cmds[0], a);
        if (last) |l| first.step.dependOn(l);
        last = &first.step;
        for (r.cmds[1..]) |argv| last = &command(b, argv, last).step;
        return last.?;
    }
    for (r.cmds) |argv| last = &command(b, argv, last).step;
    return last.?;
}

fn detectOpenSsl(b: *std.Build) ?[]const u8 {
    var code: u8 = 0;
    const out = b.runAllowFail(&.{ "brew", "--prefix", "openssl@3" }, &code, .ignore) catch return null;
    if (code != 0) return null;
    const trimmed = std.mem.trim(u8, out, " \r\n\t");
    if (trimmed.len == 0) return null;
    b.build_root.handle.access(b.graph.io, b.fmt("{s}/include/openssl/ssl.h", .{trimmed}), .{}) catch return null;
    return b.dupe(trimmed);
}

fn gitShortHead(b: *std.Build) []const u8 {
    var code: u8 = 0;
    const out = b.runAllowFail(&.{ "git", "rev-parse", "--short", "HEAD" }, &code, .ignore) catch return "unknown";
    if (code != 0) return "unknown";
    return b.dupe(std.mem.trim(u8, out, " \r\n\t"));
}

fn brandingField(b: *std.Build, field: []const u8) []const u8 {
    var code: u8 = 0;
    const out = b.runAllowFail(&.{ "/usr/bin/python3", "scripts/branding.py", "--config", "resources/macos/branding.json", "field", field }, &code, .ignore) catch @panic("branding.py field");
    if (code != 0) @panic("branding.py field failed");
    return b.dupe(std.mem.trim(u8, out, " \r\n\t"));
}

var app_name_override: ?[]const u8 = null;
fn appName(b: *std.Build) []const u8 {
    return app_name_override orelse brandingField(b, "app_name");
}

fn appVersion(b: *std.Build) []const u8 {
    // src/version.h: `inline constexpr const char* kVersion = "0.1.0";`
    const text = b.build_root.handle.readFileAlloc(b.graph.io, "src/version.h", b.allocator, .limited(1 << 16)) catch @panic("read src/version.h");
    const key = "kVersion";
    const at = std.mem.indexOf(u8, text, key) orelse @panic("kVersion not in src/version.h");
    const q1 = std.mem.indexOfPos(u8, text, at, "\"") orelse @panic("kVersion has no string");
    const q2 = std.mem.indexOfPos(u8, text, q1 + 1, "\"") orelse @panic("kVersion has no closing quote");
    return text[q1 + 1 .. q2];
}
