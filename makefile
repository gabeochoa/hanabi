# Hanabi — native desktop client. C++23 + afterhours (ECS/UI) + Sokol (Metal).

UNAME_S := $(shell uname -s)

# --- Build-speed: parallel by default -----------------------------------------
# Bare `make` used to run serially (~22s clean). Default to all cores so a plain
# `make` is as fast as `make -jN` (~6s clean here). An explicit `-jN` on the
# command line still wins (make honours the last -j it sees). `make NPROC=1` or
# `make -j1` forces serial. Detects core count on macOS/Linux, falls back to 4.
NPROC ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
MAKEFLAGS += -j$(NPROC)

# --- Build-speed: ccache (opt-in, auto-detected) ------------------------------
# ccache turns rebuilds of unchanged translation units (branch switches, a
# `make clean`, a header touch that ends up identical) into near-instant cache
# hits — a warm clean rebuild drops from ~7s to ~0.1s here. Only used when the
# ccache binary is present; otherwise the compiler is invoked directly, so this
# is a no-op on machines without it. Disable explicitly with `make USE_CCACHE=0`.
USE_CCACHE ?= 1
CCACHE := $(if $(filter 1,$(USE_CCACHE)),$(shell command -v ccache 2>/dev/null))

ifeq ($(UNAME_S),Darwin)
    CXX := $(CCACHE) clang++
    EXT := .exe
    FRAMEWORKS := -framework CoreFoundation -framework CoreServices \
        -framework Metal -framework MetalKit -framework Cocoa -framework QuartzCore \
        -framework Carbon -framework CoreSpotlight -framework UniformTypeIdentifiers
else ifeq ($(OS),Windows_NT)
    CXX := g++
    EXT := .exe
    FRAMEWORKS :=
else
    CXX := $(CCACHE) clang++
    EXT :=
    FRAMEWORKS :=
endif

CXXSTD := -std=c++23

# Optimisation. The app shipped at -O0 for a long time: no -O flag anywhere in
# the main build, only the perf micro-benchmark asked for -O2. That is a 5-6x
# frame-time difference on this UI (Home idle 5.45ms -> 0.95ms, a 120-message
# transcript 9.08ms -> 1.64ms), and `make app` builds the distributable from
# these same objects — so the .app people actually run was the slow one. Default
# to -O2 and keep -g; `make OPT=0` gives the old fast-to-compile build for
# stepping through in a debugger.
OPT ?= 2

CXXFLAGS_BASE := -g -O$(OPT) -Wall -Wextra -Wpedantic -pipe -fno-common

CXXFLAGS_SUPPRESS := -Wno-deprecated-volatile -Wno-missing-field-initializers \
    -Wno-c99-extensions -Wno-unused-function -Wno-sign-conversion \
    -Wno-deprecated-literal-operator

CXXFLAGS := $(CXXSTD) $(CXXFLAGS_BASE) $(CXXFLAGS_SUPPRESS) \
    -DAFTER_HOURS_UI_SINGLE_COLLECTION \
    -DAFTER_HOURS_USE_METAL \
    -DFMT_HEADER_ONLY \
    -DHANABI_BUILD_STAMP=\"$(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)\"

INCLUDES := -isystem vendor/ -isystem vendor/afterhours/vendor/
LDFLAGS := -L. $(FRAMEWORKS)

# Optional TLS/HTTPS support for the http backend. OFF by default so the
# zero-config mock build has NO extra dependencies. Enable with `make HANABI_TLS=1`
# (needs OpenSSL). Without it, the http adapter only speaks plain http://; a
# real https:// backend requires this. Nothing about any endpoint is baked in —
# this only adds the TLS transport so a runtime-configured https URL can connect.
ifeq ($(HANABI_TLS),1)
    OPENSSL_PREFIX ?= $(shell brew --prefix openssl@3 2>/dev/null)
    CXXFLAGS += -DHANABI_ENABLE_TLS -I$(OPENSSL_PREFIX)/include
    LDFLAGS += -L$(OPENSSL_PREFIX)/lib -lssl -lcrypto
    ifeq ($(UNAME_S),Darwin)
        LDFLAGS += -framework Security
    endif
endif

OBJ_DIR := output/objs-$(if $(filter 1,$(HANABI_TLS)),tls,notls)-O$(OPT)
OUTPUT_DIR := output

MAIN_SRC := $(shell find src -name '*.cpp')
MAIN_MM_SRC := $(wildcard src/*.mm)
MAIN_MM_OBJS := $(patsubst src/%.mm,$(OBJ_DIR)/main/%.o,$(MAIN_MM_SRC))
MAIN_OBJS := $(MAIN_SRC:src/%.cpp=$(OBJ_DIR)/main/%.o)
MAIN_OBJS += $(MAIN_MM_OBJS)
MAIN_OBJS += $(OBJ_DIR)/main/vendor_afterhours_files.o
MAIN_DEPS := $(MAIN_OBJS:.o=.d)

MAIN_EXE := $(OUTPUT_DIR)/hanabi$(EXT)

# TLS and non-TLS builds each compile into their OWN object dir
# (output/objs-tls vs output/objs-notls, see OBJ_DIR above), so the two modes
# never share or clobber each other's .o files. That means alternating a bare
# `make` / `make test` (non-TLS) with `make run` (TLS) no longer triggers a full
# clean rebuild each time — each mode keeps its own incremental cache and only
# recompiles sources that actually changed. Switching modes just relinks the
# shared exe from the already-compiled objects (fast), it does NOT recompile.
# (Previously a single shared objs/ dir was wiped whenever HANABI_TLS flipped,
# which is exactly what caused `make run` to rebuild from scratch every time.)
#
# The exe (output/hanabi) is shared between modes, so it must RELINK when the
# mode flips (otherwise a `make` after `make run` would leave the TLS binary in
# place). A tiny marker file records which mode last linked the exe; if it
# differs from the current mode we bump the marker's mtime at parse time so the
# exe's timestamp rule triggers a relink from the (cached) objects — a link,
# not a recompile.
TLS_WANT := $(if $(filter 1,$(HANABI_TLS)),tls,notls)-O$(OPT)
EXE_MODE_MARKER := $(OUTPUT_DIR)/.exe_mode
EXE_MODE_PREV := $(shell cat $(EXE_MODE_MARKER) 2>/dev/null || echo none)
ifneq ($(EXE_MODE_PREV),$(TLS_WANT))
$(shell mkdir -p $(OUTPUT_DIR); echo $(TLS_WANT) > $(EXE_MODE_MARKER))
endif

$(OUTPUT_DIR)/.stamp:
	@mkdir -p $(OUTPUT_DIR)
	@touch $@

$(OBJ_DIR)/main:
	@mkdir -p $(OBJ_DIR)/main

.DEFAULT_GOAL := all
all: $(MAIN_EXE) copy-resources

$(MAIN_EXE): $(MAIN_OBJS) $(EXE_MODE_MARKER) | $(OUTPUT_DIR)/.stamp
	@echo "Linking $(MAIN_EXE)..."
	$(CXX) $(CXXFLAGS) $(MAIN_OBJS) $(LDFLAGS) -o $@
	@echo "Built $(MAIN_EXE)"

-include $(MAIN_DEPS)

$(OBJ_DIR)/main/%.o: src/%.cpp | $(OBJ_DIR)/main
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@ -MD -MP -MF $(@:.o=.d) -MT $@

$(OBJ_DIR)/main/%.o: src/%.mm | $(OBJ_DIR)/main
	@echo "Compiling (ObjC++) $<..."
	@mkdir -p $(dir $@)
	$(CXX) -ObjC++ $(CXXFLAGS) $(INCLUDES) -c $< -o $@ -MD -MP -MF $(@:.o=.d) -MT $@

$(OBJ_DIR)/main/vendor_afterhours_files.o: vendor/afterhours/src/plugins/files.cpp | $(OBJ_DIR)/main
	@echo "Compiling vendor/afterhours/src/plugins/files.cpp..."
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@ -MD -MP -MF $(@:.o=.d) -MT $@

deps:
	rm -f $(MAIN_DEPS)

clean:
	@echo "Cleaning..."
	rm -rf output/objs-tls* output/objs-notls* output/objs-uitest* output/objs

clean-all: clean
	rm -f $(MAIN_EXE)

copy-resources:
	@mkdir -p $(OUTPUT_DIR)/resources/fonts
	@rsync -a --delete resources/ $(OUTPUT_DIR)/resources/

output: $(MAIN_EXE) copy-resources

# `make run` — one command: build (with TLS auto-enabled when OpenSSL is
# available, so an https:// backend config Just Works) and launch. If OpenSSL
# isn't found it still builds + runs, but only plain http:// backends connect
# (an https config then shows a clean error instead of crashing).
# A small stamp file records whether the last build was TLS or not; if the mode
# flips we `clean` first, because toggling HANABI_TLS changes compile flags that
# make's timestamp check alone wouldn't pick up on the existing .o files.
# `make run` talks to the real backend; `make run-mock` is the offline sample
# data. agentcloud is the default because it is what the app is for now.
#
# Its endpoints are deliberately NOT in this repo (see the HARD CONSTRAINTS in
# todo.md: no real API hardcoded). Put them in .env, which is already
# gitignored, and this sources it:
#
#     HANABI_AC_HOST=<orchestrator host>
#     HANABI_AC_MINT_HOST=<proxy-local mint pseudo-host>
#     HANABI_AC_VERIFIER=<service identity to mint against>
#
# With those unset the app degrades to the mock rather than failing, so a fresh
# clone still runs -- the recipe says so out loud instead of leaving you to
# wonder why the sessions look invented.
run:
	@if [ -n "$$(brew --prefix openssl@3 2>/dev/null)" ]; then \
		echo "==> OpenSSL found — building with TLS (https backends enabled)"; \
		$(MAKE) HANABI_TLS=1 output; \
	else \
		echo "==> OpenSSL not found — building WITHOUT TLS (http:// only)."; \
		echo "    For an https backend: brew install openssl@3, then 'make run' again."; \
		$(MAKE) output; \
	fi
	@set -a; [ -f .env ] && . ./.env; set +a; \
	export HANABI_BACKEND="$${HANABI_BACKEND:-agentcloud}"; \
	if [ "$$HANABI_BACKEND" = "agentcloud" ] && [ -z "$${HANABI_AC_HOST:-}" ]; then \
		echo "==> HANABI_BACKEND=agentcloud, but HANABI_AC_HOST is unset."; \
		echo "    Running the OFFLINE MOCK. Set the HANABI_AC_* values in .env"; \
		echo "    (gitignored) to talk to the real orchestrator."; \
	else \
		echo "==> backend: $$HANABI_BACKEND"; \
	fi; \
	./$(MAIN_EXE)

# The offline sample data, whatever .env says. Useful for UI work and for
# telling "the backend is down" apart from "I broke the sidebar".
run-mock:
	@$(MAKE) output
	@echo "==> backend: mock (offline sample data)"
	@HANABI_BACKEND=mock ./$(MAIN_EXE)

# macOS .app bundle
APP_BUNDLE := $(OUTPUT_DIR)/Hanabi.app

# Canonical "build the shippable app" target. The real backend is https, so the
# distributable .app MUST be a TLS build — otherwise every real thread fails with
# "https backend requires a TLS build" and the transcript + composer never render
# (looked like "no chat input box"). Always build the app via this target.
app:
	@$(MAKE) HANABI_TLS=1 bundle

bundle: $(MAIN_EXE) copy-resources
	@echo "Building Hanabi.app..."
	@# Preflight: a .app pointed at a real https backend needs TLS linked in. If the
	@# binary we're about to bundle has no libssl, WARN loudly (mock-only demo is a
	@# valid non-TLS build, but shipping a non-TLS .app to a real-backend user is the
	@# "where's the input box?" bug). Non-fatal so the mock demo bundle still works.
	@if ! otool -L $(MAIN_EXE) 2>/dev/null | grep -qiE 'libssl|libcrypto'; then \
		echo ""; \
		echo "  ⚠️  WARNING: bundling a NON-TLS binary. A real https:// backend will fail"; \
		echo "      with 'https backend requires a TLS build' (no transcript, no composer)."; \
		echo "      For a real-backend .app run:  make app   (== make HANABI_TLS=1 bundle)"; \
		echo ""; \
	fi
	@mkdir -p $(APP_BUNDLE)/Contents/MacOS
	@mkdir -p $(APP_BUNDLE)/Contents/Resources
	@cp $(MAIN_EXE) $(APP_BUNDLE)/Contents/MacOS/hanabi
	@rsync -a --delete $(OUTPUT_DIR)/resources/ $(APP_BUNDLE)/Contents/Resources/
	@printf '%s\n' \
		'<?xml version="1.0" encoding="UTF-8"?>' \
		'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
		'<plist version="1.0">' \
		'<dict>' \
		'    <key>CFBundleExecutable</key>' \
		'    <string>hanabi</string>' \
		'    <key>CFBundleIdentifier</key>' \
		'    <string>com.hanabi.app</string>' \
		'    <key>CFBundleName</key>' \
		'    <string>Hanabi</string>' \
		'    <key>CFBundleDisplayName</key>' \
		'    <string>Hanabi</string>' \
		'    <key>CFBundleVersion</key>' \
		'    <string>0.1.0</string>' \
		'    <key>CFBundleShortVersionString</key>' \
		'    <string>0.1.0</string>' \
		'    <key>CFBundlePackageType</key>' \
		'    <string>APPL</string>' \
		'    <key>CFBundleInfoDictionaryVersion</key>' \
		'    <string>6.0</string>' \
		'    <key>LSMinimumSystemVersion</key>' \
		'    <string>12.0</string>' \
		'    <key>LSApplicationCategoryType</key>' \
		'    <string>public.app-category.developer-tools</string>' \
		'    <key>NSHighResolutionCapable</key>' \
		'    <true/>' \
		'    <key>NSSupportsAutomaticGraphicsSwitching</key>' \
		'    <true/>' \
		'    <key>NSHumanReadableCopyright</key>' \
		'    <string>hanabi — native Navi desktop client</string>' \
		'    <key>CFBundleURLTypes</key>' \
		'    <array>' \
		'        <dict>' \
		'            <key>CFBundleURLName</key>' \
		'            <string>com.hanabi.app.thread</string>' \
		'            <key>CFBundleURLSchemes</key>' \
		'            <array>' \
		'                <string>hanabi</string>' \
		'            </array>' \
		'        </dict>' \
		'    </array>' \
		'</dict>' \
		'</plist>' > $(APP_BUNDLE)/Contents/Info.plist
	@echo "Built $(APP_BUNDLE)"

# `make mock-server` — launch the local dev mock server (tools/mock_server).
# Serves the REST + SSE shape hanabi's http adapter expects so the app can be
# exercised fully offline (list / transcripts / pagination / live events /
# SENDING messages). Pure Python 3 stdlib — no install. See tools/mock_server/
# README.md for the exact env to point hanabi at it (local http, so a NON-TLS
# build works). Override the port with `make mock-server MOCK_PORT=9000`.
MOCK_PORT ?= 8787
mock-server:
	python3 tools/mock_server/server.py --port $(MOCK_PORT)

.PHONY: all clean clean-all deps copy-resources output run run-mock bundle app mock-server

# ==============================================================================
# TESTS  (unit + headless e2e + perf regression gates)
# ==============================================================================

# Every test target depends on every header (TEST_HDRS below). Without it a
# test depends only on its .cpp: change a HEADER the test exercises -- which is
# where most of the logic under test lives -- and make re-runs the previous
# binary and reports a green that predates the change. That has already
# produced one false result. Rebuilding all twelve on any header change costs
# seconds; a false green costs a lot more. (-MMD would be tighter but several
# of these targets compile multiple sources into one binary, which clang
# refuses to pair with -o.)
TEST_CXXFLAGS := $(CXXSTD) -g -O0 -Wall -Wextra \
    -Wno-deprecated-literal-operator -Wno-sign-conversion
# Perf benchmark wants optimizations on (measures the real hot path).
PERF_CXXFLAGS := $(CXXSTD) -O2 -Wall -Wextra \
    -Wno-deprecated-literal-operator -Wno-sign-conversion
TEST_INCLUDES := -isystem vendor/ -I.
TEST_DIR := $(OUTPUT_DIR)/tests

$(TEST_DIR):
	@mkdir -p $(TEST_DIR)

# See the note on TEST_CXXFLAGS: every test re-links when any header moves.
TEST_HDRS := $(wildcard src/*.h src/*/*.h src/*/*/*.h)

# Everything make_client() can construct has to link wherever config.cpp does,
# so this is one list rather than nine. Adding a backend means editing here and
# nowhere else -- the previous shape made a new backend break eight targets.
# ws_socket.mm is Obj-C++, hence the ARC flag and the two frameworks.
API_SRCS := src/api/config.cpp src/api/http_client.cpp \
            src/api/agentcloud_client.cpp src/api/agentcloud_auth.cpp \
            src/ws_socket.mm
API_LINK := -fobjc-arc -framework Foundation -framework CFNetwork

$(TEST_DIR)/test_api: tests/unit/test_api.cpp $(API_SRCS) $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_api..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $(filter-out %.h,$^) $(API_LINK) -o $@

# Device-code auth state machine (Phase AUTH). Pure logic against a FAKE
# transport — no graphics, no network. auth.cpp + config.cpp are the only app
# sources it needs.
$(TEST_DIR)/test_auth: tests/unit/test_auth.cpp src/api/auth.cpp $(API_SRCS) $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_auth..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $(filter-out %.h,$^) $(API_LINK) -o $@

# Sending (Phase SEND): kickoff (create_session) + reply (send_message) driven
# directly against the MockClient. Pure logic — no graphics, no network. Only
# config.cpp is needed alongside the header-only mock client.
$(TEST_DIR)/test_send: tests/unit/test_send.cpp $(API_SRCS) $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_send..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $(filter-out %.h,$^) $(API_LINK) -o $@

# Streaming (Phase STREAM): mock delivers a reply as ordered chunks that
# reassemble across per-frame ticks + the pure SSE parser against fixture text.
# Pure logic — NO graphics, NO network, NO timers. config.cpp + http_client.cpp
# supply Config + the parse_sse_chunk parser alongside the header-only mock.
$(TEST_DIR)/test_stream: tests/unit/test_stream.cpp $(API_SRCS) $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_stream..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $(filter-out %.h,$^) $(API_LINK) -o $@

# Tool-call block splitting + memory-light newest-N + live SSE event parsing
# (LIVE phase). Pure logic — NO graphics, NO network, NO timers. Exercises the
# adapter's split_message_blocks + parse_events_frame (from http_client.cpp)
# and the MockClient windowed get_session(id, N) contract.
$(TEST_DIR)/test_tools: tests/unit/test_tools.cpp $(API_SRCS) $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_tools..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $(filter-out %.h,$^) $(API_LINK) -o $@

# Composer text-input key handling (gap #31): the macOS sokol backend emits a
# CHAR event for backspace (U+007F); hanabi's is_typable_char filter must reject
# control codes so they aren't typed into the field. Pure logic — drives the
# real afterhours text_input state + insert_char with the exact macOS codepoints.
$(TEST_DIR)/test_textinput: tests/unit/test_textinput.cpp $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_textinput..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $(filter-out %.h,$^) -o $@

# Input action mapping (root cause of Enter-not-send / Backspace-not-delete):
# guards that hanabi's key mapping resolves BACKSPACE->TextBackspace and
# ENTER->WidgetPress (and SPACE binds to no action) via the SAME resolution
# impl the InputSystem runs. Backend-free (custom key-check stub).
# Which thread-state changes deserve a banner. Pure logic — no clock, no
# AppKit, no session type; the interesting cases (first sight, a swap that
# leaves the count unchanged, a vanished thread) are the ones a wall-clock
# test cannot reach.
$(TEST_DIR)/test_notify_events: tests/unit/test_notify_events.cpp src/util/notify_events.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_notify_events..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_notify_events.cpp -o $@

$(TEST_DIR)/test_diff: tests/unit/test_diff.cpp src/util/diff.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_diff..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_diff.cpp -o $@

# Ellipsizing a row title to a pixel width. The sidebar's ellipsizer went from
# a linear scan to a binary search for speed, so what is worth pinning is that
# it returns the SAME string -- checked differentially against the old linear
# scan at every width, under rulers no font would give you (including a
# backwards kern, where prefix width is not monotonic and a bare binary search
# is wrong). Pure logic -- the metric is a callable, no font, no graphics.
# The soak verdict's estimator, alone in a process: no Metal, no ECS, no
# window. src/util/trend.h is split out of soak.h for exactly this.
$(TEST_DIR)/test_trend: tests/unit/test_trend.cpp src/util/trend.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_trend..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_trend.cpp -o $@

$(TEST_DIR)/test_ellipsize: tests/unit/test_ellipsize.cpp src/util/ellipsize.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_ellipsize..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_ellipsize.cpp -o $@

$(TEST_DIR)/test_input_pipeline: tests/unit/test_input_pipeline.cpp $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_input_pipeline..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $(filter-out %.h,$^) -o $@

# What the Cmd+G chord means and where a step lands. The chord cannot be
# pressed from a script (gap #49), so the table it resolves through is the only
# part of the binding a test can hold. Pure logic — no graphics, no app state.
$(TEST_DIR)/test_find_nav: tests/unit/test_find_nav.cpp src/ui/find_nav.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_find_nav..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_find_nav.cpp -o $@

# Cross-session search: what the index matches, and whether the sentence it
# shows about its own coverage is true. Pure logic - no graphics, no disk, no
# app state; the corpus is handed in.
$(TEST_DIR)/test_session_index: tests/unit/test_session_index.cpp src/search/session_index.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_session_index..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_session_index.cpp -o $@

# Cutting a search match down to a sidebar row. Pure string arithmetic - the
# painting that goes with it needs a font manager and is asserted on screen.
$(TEST_DIR)/test_snippet_text: tests/unit/test_snippet_text.cpp src/ui/snippet_text.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_snippet_text..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_snippet_text.cpp -o $@

# What colour a pinned tab's pushpin comes out. The rule is Puffin's
# (pin.fill overrides the chip's foreground and carries its own opacity) and the
# constants are sampled off the frozen reference; assert_ui cannot see a pixel,
# so the guard is arithmetic.
$(TEST_DIR)/test_tab_colors: tests/unit/test_tab_colors.cpp src/ecs/tab_colors.h src/ui/theme.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_tab_colors..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_tab_colors.cpp -o $@

# The sidebar footer's arithmetic. The one property here that no other harness
# in the repo can see is that the activity light's origin is on a whole pixel:
# assert_ui reads x/y/w/h and ROUNDS them, so it cannot tell a snapped position
# from an unsnapped one, and afterhours draws an unsnapped 6px box five rows
# tall. Same split as test_tab_colors: the screenshot suite holds the
# placement, this holds the property.
$(TEST_DIR)/test_pane_memory: tests/unit/test_pane_memory.cpp src/ecs/pane_state.h src/ecs/transcript_render_cache.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_pane_memory..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_pane_memory.cpp -o $@

$(TEST_DIR)/test_footer_geometry: tests/unit/test_footer_geometry.cpp src/ecs/sidebar_footer_geometry.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_footer_geometry..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_footer_geometry.cpp -o $@

# Data/loader layer additions (wt/data): disk_cache total_bytes/wipe, message
# queue ordering/draining, newest-N windowing, settings read.
$(TEST_DIR)/test_data: tests/unit/test_data.cpp $(API_SRCS) src/api/disk_cache.cpp $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_data..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $(filter-out %.h,$^) $(API_LINK) -o $@

# Settings rework (wt/settings): each wired preference control changes the
# persisted Settings value + marks the sync-dirty flag; the mock's settings
# WRITE path accepts + stores a pushed snapshot; the http write-gate is opt-in
# + OFF by default. Pure logic — no graphics, no network. settings.cpp supplies
# the persistence slots; config.cpp + http_client.cpp supply Config + the
# header-only mock client.
$(TEST_DIR)/test_settings: tests/unit/test_settings.cpp src/settings.cpp $(API_SRCS) vendor/afterhours/src/plugins/files.cpp $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_settings..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $(filter-out %.h,$^) $(API_LINK) -o $@

# Headless e2e: real app logic (state model, glyphs, smart views, tabs, backend
# defaults) against the mock + the real afterhours ECS core. No graphics linked.
# TEST_HDRS, like every other target here: this one was the exception, and a
# header-only change left `make test` silently running the PREVIOUS binary --
# so an agent neutering its own fix to prove the test could fail watched it
# come back green. A test that cannot fail is not evidence, and a test that
# cannot even be rebuilt is worse.
$(TEST_DIR)/test_e2e: tests/e2e/test_e2e.cpp $(API_SRCS) $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_e2e..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $(filter-out %.h,$^) $(API_LINK) -o $@

# Headless perf micro-benchmark: in-process thread-switch latency (built -O2).
$(TEST_DIR)/test_perf: tests/e2e/test_perf.cpp src/api/disk_cache.cpp $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_perf..."
	$(CXX) $(PERF_CXXFLAGS) $(TEST_INCLUDES) $(filter-out %.h,$^) -o $@

# Read-only REAL-backend smoke: list_sessions + get_session against the ACTUAL
# configured http backend (no mutations). Compiled WITH the TLS flags so it can
# speak https; self-skips (exit 0) when no http backend is configured. This is
# the pre-push "does it work with real data?" check — NOT part of the default
# offline `make test` (it hits the network). Uses CXXFLAGS (which carry
# -DHANABI_ENABLE_TLS + OpenSSL paths only when HANABI_TLS=1).
$(TEST_DIR)/test_real: tests/e2e/test_real.cpp $(API_SRCS) $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_real..."
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $(filter-out %.h,$^) $(LDFLAGS) $(API_LINK) -o $@

# agentcloud transport slice: query encoding (an unescaped colon in the
# verifier is an opaque HTTP 400) and the env gate that keeps the mock the
# zero-config default. No network.
$(TEST_DIR)/test_agentcloud: tests/unit/test_agentcloud.cpp src/api/agentcloud_auth.cpp src/api/agentcloud_client.cpp src/ws_socket.mm $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_agentcloud..."
	# Foundation/CFNetwork are for ws_socket.mm, which comes along with the
	# client TU. The tests never open a socket -- parse_sessions_reply is pure.
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) -fobjc-arc $(filter-out %.h,$^) \
	    -framework Foundation -framework CFNetwork -o $@

UNIT_TEST_EXES := $(TEST_DIR)/test_api $(TEST_DIR)/test_auth $(TEST_DIR)/test_send $(TEST_DIR)/test_stream $(TEST_DIR)/test_tools $(TEST_DIR)/test_textinput $(TEST_DIR)/test_input_pipeline $(TEST_DIR)/test_data $(TEST_DIR)/test_settings $(TEST_DIR)/test_agentcloud $(TEST_DIR)/test_notify_events $(TEST_DIR)/test_find_nav $(TEST_DIR)/test_session_index $(TEST_DIR)/test_snippet_text $(TEST_DIR)/test_diff $(TEST_DIR)/test_ellipsize $(TEST_DIR)/test_trend $(TEST_DIR)/test_tab_colors $(TEST_DIR)/test_footer_geometry $(TEST_DIR)/test_pane_memory
E2E_TEST_EXES := $(TEST_DIR)/test_e2e
PERF_TEST_EXES := $(TEST_DIR)/test_perf

# Shared runner: run each exe, count pass/fail, non-zero exit on any failure.
define RUN_TESTS
	@PASS=0; FAIL=0; \
	for t in $(1); do \
	    if $$t; then PASS=$$((PASS + 1)); \
	    else FAIL=$$((FAIL + 1)); fi; \
	done; \
	echo "========================================"; \
	echo "Results: $$PASS/$$(( PASS + FAIL )) passed, $$FAIL failed"; \
	echo "========================================"; \
	[ "$$FAIL" -eq 0 ]
endef

# Unit + e2e run under `make unit-e2e`.
unit-e2e: $(UNIT_TEST_EXES) $(E2E_TEST_EXES)
	@echo "Running unit + e2e tests..."
	$(call RUN_TESTS,$(UNIT_TEST_EXES) $(E2E_TEST_EXES))

# Just the e2e suite.
e2e: $(E2E_TEST_EXES)
	@echo "Running e2e tests..."
	$(call RUN_TESTS,$(E2E_TEST_EXES))

# Perf: in-process switch-latency benchmark + the launch/RSS gate (needs the
# app built). measure_launch.sh runs the app in the background with a timeout
# and pkill cleanup — see the script.
perf: $(PERF_TEST_EXES) $(MAIN_EXE)
	@echo "Running perf micro-benchmark..."
	$(call RUN_TESTS,$(PERF_TEST_EXES))
	@echo "Running launch/RSS perf gate (scripts/measure_launch.sh)..."
	@bash scripts/measure_launch.sh
	@echo "Running transcript slope gate (scripts/perf_transcript_slope.sh)..."
	@bash scripts/perf_transcript_slope.sh

# `make test` = unit + e2e + scripted UI + perf (the full harness, one command).
test: $(UNIT_TEST_EXES) $(E2E_TEST_EXES) $(PERF_TEST_EXES) $(MAIN_EXE)
	@echo "Running unit + e2e tests..."
	$(call RUN_TESTS,$(UNIT_TEST_EXES) $(E2E_TEST_EXES) $(PERF_TEST_EXES))
	@$(MAKE) uitest
	@echo "Running launch/RSS perf gate (scripts/measure_launch.sh)..."
	@bash scripts/measure_launch.sh
	@echo "Running transcript slope gate (scripts/perf_transcript_slope.sh)..."
	@bash scripts/perf_transcript_slope.sh
	@$(MAKE) soak-gate
	@$(MAKE) scaling-gate
	@$(MAKE) source-checks

# ==============================================================================
# THE SLOPE GATES  (docs/perf/GATES.md)
# ==============================================================================
#
# Everything above this line is a budget on a young idle app: 45 frames and an
# assertion on the 45th, the first frame and the peak RSS of a process under a
# second old, thread-switch latency in-process. A leak is a young idle app
# looking fine, and the app shipped one at ~9 MB a minute with all of them
# green. These two measure the SLOPE instead of the value.
#
#   make soak-gate     ~3 s.  2000 frames idle; fails if RSS, live heap bytes,
#                      live BLOCKS, entities or thread-CPU time is trending
#                      up across the run. In `make test`.
#   make scaling-gate  ~5 s.  The same UI at 20 and at 2000 sessions; fails on
#                      the RATIO between them, never on an absolute
#                      millisecond figure — this box is shared, and an
#                      absolute would flake. In `make test`.
#   make soak          ~37 s. The long form: eleven scenarios, four at a time.
#                      NOT in `make test` — run it before a release.
#   make soak-report   The same run, reduced to a DIFFABLE text artifact and
#                      diffed against docs/perf/soak-baseline.txt. A
#                      regression is a text diff; sub-budget noise is not.
#   make soak-baseline Regenerate that baseline. Commit the result, and say in
#                      the message what moved and why.
#   make stress        One scenario, by hand, with the knobs exposed:
#                        make stress SCENARIO=churn FRAMES=5000 SESSIONS=500
#   make stress-break  Open every thread until a frame costs 3x what it did at
#                      rest, and report how far it got:
#                        make stress-break UNTIL=cpu:3.0 SESSIONS=2000
soak-gate: $(MAIN_EXE) copy-resources
	@bash scripts/soak_gate.sh

scaling-gate: $(MAIN_EXE) copy-resources
	@bash scripts/scaling_gate.sh

soak: $(MAIN_EXE) copy-resources
	@HANABI_SOAK_LONG_FRAMES="$(if $(FRAMES),$(FRAMES),$(HANABI_SOAK_LONG_FRAMES))" \
	 HANABI_SOAK_ARMS="$(if $(ARMS),$(ARMS),$(HANABI_SOAK_ARMS))" \
	 HANABI_SOAK_JOBS="$(if $(JOBS),$(JOBS),$(HANABI_SOAK_JOBS))" \
	 bash scripts/soak.sh

SOAK_BASELINE := docs/perf/soak-baseline.txt

# A regression as a TEXT DIFF. The report holds the deterministic properties
# exactly (entity count, widgets by debug name, tabs, what the scenario drove)
# and the measured ones as budget BANDS, so a clean run writes the same bytes
# every time and a diff names what changed. Verified: two full runs 65 seconds
# apart are byte-identical.
soak-report: $(MAIN_EXE) copy-resources
	@HANABI_SOAK_REPORT_OUT=$(OUTPUT_DIR)/soak-report.txt \
	 HANABI_SOAK_ARMS="$(if $(ARMS),$(ARMS),$(HANABI_SOAK_ARMS))" \
	 HANABI_SOAK_JOBS="$(if $(JOBS),$(JOBS),$(HANABI_SOAK_JOBS))" \
	 bash scripts/soak.sh > $(OUTPUT_DIR)/soak-report.log 2>&1; \
	 echo "  full log: $(OUTPUT_DIR)/soak-report.log"; \
	 if [ ! -s $(OUTPUT_DIR)/soak-report.txt ]; then \
	   echo "  ! the run produced no report at all, so there is nothing to"; \
	   echo "    compare. That is a broken run, not a clean one."; exit 2; \
	 fi; \
	 if diff -u $(SOAK_BASELINE) $(OUTPUT_DIR)/soak-report.txt; then \
	   echo "  soak report matches $(SOAK_BASELINE)"; \
	 else \
	   echo ""; \
	   echo "  The soak report moved. Every line above is a property that"; \
	   echo "  changed, not a number that drifted -- the measured columns are"; \
	   echo "  budget BANDS and do not move unless a budget was crossed."; \
	   echo "  If the change is intended, 'make soak-baseline' and commit it"; \
	   echo "  with a message saying what moved and why."; exit 1; \
	 fi

soak-baseline: $(MAIN_EXE) copy-resources
	@HANABI_SOAK_REPORT_OUT=$(SOAK_BASELINE) bash scripts/soak.sh \
	   > $(OUTPUT_DIR)/soak-baseline.log 2>&1 || true
	@echo "  wrote $(SOAK_BASELINE) ($$(wc -l < $(SOAK_BASELINE) | tr -d ' ') lines)"
	@echo "  full log: $(OUTPUT_DIR)/soak-baseline.log"

# One scenario, by hand. Everything the driver takes, as make variables, so a
# run can be reproduced from a line somebody pasted into a task.
stress: $(MAIN_EXE) copy-resources
	@HANABI_BACKEND=mock HANABI_CONFIG=/nonexistent/hanabi/stress.json \
	 HANABI_MOCK_NOW=$${HANABI_MOCK_NOW:-1787000000} \
	 HANABI_STRESS="$(if $(SCENARIO),$(SCENARIO),idle)" \
	 HANABI_SOAK="$(if $(FRAMES),$(FRAMES),3000)" \
	 HANABI_SOAK_EVERY="$(if $(EVERY),$(EVERY),250)" \
	 $(if $(SESSIONS),HANABI_STRESS_SESSIONS=$(SESSIONS),) \
	 $(if $(UNTIL),HANABI_STRESS_UNTIL=$(UNTIL),) \
	 $(OUTPUT_DIR)/hanabi.exe --screenshot /tmp/hanabi_stress.png

# Open every thread until it breaks, and say where. The default condition is a
# RATIO against the settled baseline, never an absolute millisecond figure:
# this box is shared and its load average has hit 29.
stress-break: $(MAIN_EXE) copy-resources
	@$(MAKE) --no-print-directory stress \
	   SCENARIO="$(if $(SCENARIO),$(SCENARIO),open)" \
	   FRAMES="$(if $(FRAMES),$(FRAMES),30000)" \
	   EVERY="$(if $(EVERY),$(EVERY),1000)" \
	   SESSIONS="$(if $(SESSIONS),$(SESSIONS),2000)" \
	   UNTIL="$(if $(UNTIL),$(UNTIL),cpu:3.0)"

# The source checks. Cheap, and they catch a class of defect no pixel and no
# frame time can: a silent no-op renders plausibly and asserts nothing.
# check_autorelease.py is the one that guards the four lines whose deletion
# caused all of this — see docs/perf/GATES.md.
source-checks:
	@echo "Running source checks..."
	@rc=0; \
	for chk in scripts/check_label_padding.py scripts/check_autorelease.py scripts/check_watchdogs.py; do \
	    if /usr/bin/python3 $$chk; then :; else rc=1; fi; \
	done; \
	if /usr/bin/python3 scripts/compare.py --selftest; then :; else rc=1; fi; \
	exit $$rc

.PHONY: test unit-e2e e2e perf test-real soak soak-gate scaling-gate source-checks \
	soak-report soak-baseline stress stress-break

# `make test-real` — the PRE-PUSH real-data check. Builds the read-only smoke
# test WITH TLS (so it can reach an https backend) and runs it against the
# ACTUAL configured backend. Read-only: list_sessions + get_session, never any
# mutation. Self-skips cleanly if no http backend is configured. Run this
# before pushing to confirm hanabi works with real data, not just the mock:
#     make test-real
test-real:
	@$(MAKE) HANABI_TLS=1 $(TEST_DIR)/test_real
	@echo "Running read-only real-backend smoke (test_real)..."
	@$(TEST_DIR)/test_real

count:
	git ls-files | grep "src" | grep -v "resources" | grep -v "vendor" | xargs wc -l | sort -rn

# --- Scripted UI tests --------------------------------------------------------
# A SECOND binary of the whole app, compiled with afterhours' e2e input hooks
# on, that drives the real UI from a .e2e script: move the mouse, click, type,
# assert on the text that is actually on screen. Its own object dir so the
# shipping build never sees the test-input branches.
UITEST_OBJ_DIR := output/objs-uitest-O$(OPT)
UITEST_CXXFLAGS := $(CXXFLAGS) -DAFTER_HOURS_ENABLE_E2E_TESTING
UITEST_OBJS := $(MAIN_SRC:src/%.cpp=$(UITEST_OBJ_DIR)/%.o)
UITEST_OBJS += $(patsubst src/%.mm,$(UITEST_OBJ_DIR)/%.o,$(MAIN_MM_SRC))
UITEST_OBJS += $(UITEST_OBJ_DIR)/vendor_afterhours_files.o
UITEST_DEPS := $(UITEST_OBJS:.o=.d)
UITEST_EXE := $(OUTPUT_DIR)/hanabi_uitest$(EXT)

-include $(UITEST_DEPS)

$(UITEST_OBJ_DIR)/%.o: src/%.cpp
	@echo "Compiling (uitest) $<..."
	@mkdir -p $(dir $@)
	$(CXX) $(UITEST_CXXFLAGS) $(INCLUDES) -c $< -o $@ -MD -MP -MF $(@:.o=.d) -MT $@

$(UITEST_OBJ_DIR)/%.o: src/%.mm
	@echo "Compiling (uitest, ObjC++) $<..."
	@mkdir -p $(dir $@)
	$(CXX) -ObjC++ $(UITEST_CXXFLAGS) $(INCLUDES) -c $< -o $@ -MD -MP -MF $(@:.o=.d) -MT $@

$(UITEST_OBJ_DIR)/vendor_afterhours_files.o: vendor/afterhours/src/plugins/files.cpp
	@echo "Compiling (uitest) vendor/afterhours/src/plugins/files.cpp..."
	@mkdir -p $(dir $@)
	$(CXX) $(UITEST_CXXFLAGS) $(INCLUDES) -c $< -o $@ -MD -MP -MF $(@:.o=.d) -MT $@

$(UITEST_EXE): $(UITEST_OBJS) | $(OUTPUT_DIR)/.stamp
	@echo "Linking $(UITEST_EXE)..."
	$(CXX) $(UITEST_CXXFLAGS) $(UITEST_OBJS) $(LDFLAGS) -o $@
	@echo "Built $(UITEST_EXE)"

uitest: $(UITEST_EXE) copy-resources
	@bash scripts/run_ui_tests.sh

# Build the scripted-UI binary without running the suite (used by
# scripts/review_shots.sh, which drives it directly).
uitest-build: $(UITEST_EXE) copy-resources

.PHONY: uitest uitest-build

# ==============================================================================
# SCREENSHOT BASELINES  (docs/breakdown/screenshot-testing.md, chunks 1-3)
# ==============================================================================

SHOT_BASELINES := docs/screenshots/baselines
SHOT_CURRENT := $(OUTPUT_DIR)/screenshots/current
SHOT_DETERMINISM := $(OUTPUT_DIR)/screenshots/determinism
SHOT_DECLARED := $(OUTPUT_DIR)/screenshots/declared.txt
SHOT_FAILURES := test-failures

# The states that have a committed baseline.
SHOT_BASELINED = $(shell ls $(SHOT_BASELINES)/*.png 2>/dev/null | xargs -n1 basename 2>/dev/null | sed 's/\.png$$//')

# Turn a list of state names into the extended regex screens.sh filters on.
shot_filter_of = $(shell printf '%s\n' $(1) | grep -v '^$$' | sort -u | paste -sd'|' - | sed 's/^/^(/; s/$$/)$$/')

# Which states to capture: by default exactly the ones that have a baseline, so
# a three-baseline check renders three screens and not all 35. Override to add
# one: make update-baselines SHOT_FILTER='^04_transcript_light$$'
SHOT_FILTER = $(call shot_filter_of,$(SHOT_BASELINED))

# States screens.sh can capture that have neither a baseline nor a recorded
# reason in the manifest. update-baselines adopts them along with the rest, so
# adding a state to screens.sh needs no hand-written filter.
SHOT_NEW = $(shell bash scripts/screens.sh --list 2>/dev/null | \
    $(SHOT_PYTHON) scripts/compare_screenshots.py --baselines $(SHOT_BASELINES) \
        --declared - --print-new 2>/dev/null)
SHOT_UPDATE_FILTER = $(call shot_filter_of,$(SHOT_BASELINED) $(SHOT_NEW))

# An explicit SHOT_FILTER on the command line means "just these", so it wins
# over the adopt-the-new-ones default.
SHOT_FILTER_FOR_UPDATE = $(if $(filter command line,$(origin SHOT_FILTER)),$(SHOT_FILTER),$(SHOT_UPDATE_FILTER))

# compare_screenshots.py prefers Pillow and falls back to ImageMagick; the
# python3 first on PATH is not necessarily the one with Pillow installed.
SHOT_PY = $(shell for p in python3 /usr/bin/python3 /opt/homebrew/bin/python3; do command -v $$p >/dev/null 2>&1 && $$p -c 'import PIL' >/dev/null 2>&1 && { echo $$p; break; }; done | head -1)
SHOT_PYTHON = $(if $(SHOT_PY),$(SHOT_PY),python3)

# Capture the states matching $(2) into $(1). Mock backend, isolated HOME and
# per-shot timeout all come from scripts/screens.sh.
define CAPTURE_SCREENS
	@rm -rf $(1); mkdir -p $(1)
	@HANABI_SCREENS_OUT=$(abspath $(1)) HANABI_SCREENS_FILTER='$(2)' \
	    bash scripts/screens.sh
endef

# Chunk 1: the whole suite rests on this. Capture one screen twice and require
# the two PNGs to be byte-identical.
test-screenshot-determinism: $(MAIN_EXE) copy-resources
	@echo "=== screenshot determinism: capturing 01_home_dark twice ==="
	@rm -rf $(SHOT_DETERMINISM)
	@mkdir -p $(SHOT_DETERMINISM)/a $(SHOT_DETERMINISM)/b
	@HANABI_SCREENS_OUT=$(abspath $(SHOT_DETERMINISM))/a HANABI_SCREENS_FILTER='^01_home_dark$$' \
	    bash scripts/screens.sh
	@HANABI_SCREENS_OUT=$(abspath $(SHOT_DETERMINISM))/b HANABI_SCREENS_FILTER='^01_home_dark$$' \
	    bash scripts/screens.sh
	@A=$(SHOT_DETERMINISM)/a/01_home_dark.png; B=$(SHOT_DETERMINISM)/b/01_home_dark.png; \
	for f in $$A $$B; do \
	    [ -s "$$f" ] || { echo "FAIL: $$f missing or empty" >&2; exit 1; }; \
	done; \
	SA=$$(wc -c < $$A | tr -d ' '); SB=$$(wc -c < $$B | tr -d ' '); \
	MA=$$(md5 -q $$A 2>/dev/null || md5sum $$A | cut -d' ' -f1); \
	MB=$$(md5 -q $$B 2>/dev/null || md5sum $$B | cut -d' ' -f1); \
	echo "  capture A: $$SA bytes  md5 $$MA"; \
	echo "  capture B: $$SB bytes  md5 $$MB"; \
	if [ "$$SA" = "$$SB" ] && [ "$$MA" = "$$MB" ]; then \
	    echo "PASS: two captures of 01_home_dark are byte-identical"; \
	else \
	    echo "FAIL: captures differ — the render is not deterministic, so" >&2; \
	    echo "      baselines cannot be trusted. Check for absolute-epoch mock" >&2; \
	    echo "      seeding (src/api/mock_client.h) or an unfrozen animation." >&2; \
	    exit 1; \
	fi

# Chunk 3: capture the baselined states and compare.
# Chunk 4: --declared hands the comparison the full list of states the harness
# can produce, so a screen that has no baseline is reported instead of simply
# never being rendered (validation only recaptures what is already baselined).
validate-screenshots: $(MAIN_EXE) copy-resources
	@echo "=== capturing current screens for comparison ==="
	@rm -rf $(SHOT_FAILURES)
	$(call CAPTURE_SCREENS,$(SHOT_CURRENT),$(SHOT_FILTER))
	@bash scripts/screens.sh --list > $(SHOT_DECLARED)
	@echo
	@$(SHOT_PYTHON) scripts/compare_screenshots.py \
	    --baselines $(SHOT_BASELINES) --current $(SHOT_CURRENT) \
	    --declared $(SHOT_DECLARED) \
	    --failures-dir $(SHOT_FAILURES) --json $(SHOT_FAILURES)/summary.json

# Chunk 3: adopt the current render as the new truth, for an INTENTIONAL visual
# change. Review `git diff --stat $(SHOT_BASELINES)` (and the PNGs) before
# committing. Captures the baselined states plus any that have no baseline yet,
# so this is also how a newly added state gets adopted.
update-baselines: $(MAIN_EXE) copy-resources
	@echo "=== recapturing baselines ==="
	$(call CAPTURE_SCREENS,$(SHOT_CURRENT),$(SHOT_FILTER_FOR_UPDATE))
	@cp $(SHOT_CURRENT)/*.png $(SHOT_BASELINES)/
	@echo
	@git diff --stat $(SHOT_BASELINES) || true
	@git status --short $(SHOT_BASELINES) | grep '^??' || true
	@echo "Baselines updated. Review the PNGs before committing."

.PHONY: test-screenshot-determinism validate-screenshots update-baselines

# ==============================================================================
# THE PRE-PUSH GATE  (docs/breakdown/screenshot-testing.md, chunks 6-7)
# ==============================================================================
#
# There is NO CI runner for this repo. The remote is a personal GitHub repo
# with no .github/ directory — none on any branch, and none in the history —
# so nothing runs `make test` when a commit lands. Calling `validate-screenshots`
# "wired into CI" would name a machine that does not exist.
#
# What exists instead: this target, and a hook you can install so `git push`
# runs it. Both halves report, so one run tells you everything that is wrong
# rather than stopping at the first failure.
#
#     make gate            # everything a push should pass (several minutes)
#     make install-hooks   # run it automatically on git push
#
# A failing screenshot leaves $(SHOT_FAILURES)/ behind: the baseline, the fresh
# capture, a diff image per failure and summary.json.
gate:
	@rc=0; \
	tests=ok; shots=ok; \
	$(MAKE) test || { rc=1; tests=FAIL; }; \
	$(MAKE) validate-screenshots || { rc=1; shots=FAIL; }; \
	echo; \
	echo "=== gate ==="; \
	printf '  %-22s %s\n' "tests (make test)" "$$tests"; \
	printf '  %-22s %s\n' "screenshot baselines" "$$shots"; \
	if [ "$$rc" = "0" ]; then \
	    echo "  PASS — safe to push"; \
	elif [ "$$shots" = "FAIL" ]; then \
	    echo "  FAIL — do not push. Screenshot evidence: $(SHOT_FAILURES)/"; \
	else \
	    echo "  FAIL — do not push."; \
	fi; \
	exit $$rc

# Install the pre-push hook into this checkout. Uses --git-common-dir so it
# also works from a worktree, where the per-worktree hooks dir is not the one
# git reads.
install-hooks:
	@dir="$$(git rev-parse --git-common-dir)/hooks"; \
	mkdir -p "$$dir"; \
	cp scripts/hooks/pre-push "$$dir/pre-push"; \
	chmod +x "$$dir/pre-push"; \
	echo "installed $$dir/pre-push — 'git push' now runs 'make gate'"; \
	echo "skip it for one push with: git push --no-verify"

.PHONY: gate install-hooks
