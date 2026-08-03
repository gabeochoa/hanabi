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

CXXFLAGS_BASE := -g -Wall -Wextra -Wpedantic -pipe -fno-common

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

OBJ_DIR := output/objs-$(if $(filter 1,$(HANABI_TLS)),tls,notls)
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
TLS_WANT := $(if $(filter 1,$(HANABI_TLS)),tls,notls)
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
	rm -rf output/objs-tls output/objs-notls output/objs

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
run:
	@if [ -n "$$(brew --prefix openssl@3 2>/dev/null)" ]; then \
		echo "==> OpenSSL found — building with TLS (https backends enabled)"; \
		$(MAKE) HANABI_TLS=1 output; \
	else \
		echo "==> OpenSSL not found — building WITHOUT TLS (http:// only)."; \
		echo "    For an https backend: brew install openssl@3, then 'make run' again."; \
		$(MAKE) output; \
	fi
	./$(MAIN_EXE)

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

.PHONY: all clean clean-all deps copy-resources output run bundle app mock-server

# ==============================================================================
# TESTS  (unit + headless e2e + perf regression gates)
# ==============================================================================

TEST_CXXFLAGS := $(CXXSTD) -g -O0 -Wall -Wextra \
    -Wno-deprecated-literal-operator -Wno-sign-conversion
# Perf benchmark wants optimizations on (measures the real hot path).
PERF_CXXFLAGS := $(CXXSTD) -O2 -Wall -Wextra \
    -Wno-deprecated-literal-operator -Wno-sign-conversion
TEST_INCLUDES := -isystem vendor/ -I.
TEST_DIR := $(OUTPUT_DIR)/tests

$(TEST_DIR):
	@mkdir -p $(TEST_DIR)

$(TEST_DIR)/test_api: tests/unit/test_api.cpp src/api/config.cpp src/api/http_client.cpp | $(TEST_DIR)
	@echo "Compiling test_api..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $^ -o $@

# Device-code auth state machine (Phase AUTH). Pure logic against a FAKE
# transport — no graphics, no network. auth.cpp + config.cpp are the only app
# sources it needs.
$(TEST_DIR)/test_auth: tests/unit/test_auth.cpp src/api/auth.cpp src/api/config.cpp src/api/http_client.cpp | $(TEST_DIR)
	@echo "Compiling test_auth..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $^ -o $@

# Sending (Phase SEND): kickoff (create_session) + reply (send_message) driven
# directly against the MockClient. Pure logic — no graphics, no network. Only
# config.cpp is needed alongside the header-only mock client.
$(TEST_DIR)/test_send: tests/unit/test_send.cpp src/api/config.cpp src/api/http_client.cpp | $(TEST_DIR)
	@echo "Compiling test_send..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $^ -o $@

# Streaming (Phase STREAM): mock delivers a reply as ordered chunks that
# reassemble across per-frame ticks + the pure SSE parser against fixture text.
# Pure logic — NO graphics, NO network, NO timers. config.cpp + http_client.cpp
# supply Config + the parse_sse_chunk parser alongside the header-only mock.
$(TEST_DIR)/test_stream: tests/unit/test_stream.cpp src/api/config.cpp src/api/http_client.cpp | $(TEST_DIR)
	@echo "Compiling test_stream..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $^ -o $@

# Tool-call block splitting + memory-light newest-N + live SSE event parsing
# (LIVE phase). Pure logic — NO graphics, NO network, NO timers. Exercises the
# adapter's split_message_blocks + parse_events_frame (from http_client.cpp)
# and the MockClient windowed get_session(id, N) contract.
$(TEST_DIR)/test_tools: tests/unit/test_tools.cpp src/api/config.cpp src/api/http_client.cpp | $(TEST_DIR)
	@echo "Compiling test_tools..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $^ -o $@

# Composer text-input key handling (gap #31): the macOS sokol backend emits a
# CHAR event for backspace (U+007F); hanabi's is_typable_char filter must reject
# control codes so they aren't typed into the field. Pure logic — drives the
# real afterhours text_input state + insert_char with the exact macOS codepoints.
$(TEST_DIR)/test_textinput: tests/unit/test_textinput.cpp | $(TEST_DIR)
	@echo "Compiling test_textinput..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $^ -o $@

# Input action mapping (root cause of Enter-not-send / Backspace-not-delete):
# guards that hanabi's key mapping resolves BACKSPACE->TextBackspace and
# ENTER->WidgetPress (and SPACE binds to no action) via the SAME resolution
# impl the InputSystem runs. Backend-free (custom key-check stub).
$(TEST_DIR)/test_input_pipeline: tests/unit/test_input_pipeline.cpp | $(TEST_DIR)
	@echo "Compiling test_input_pipeline..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $^ -o $@

# Data/loader layer additions (wt/data): disk_cache total_bytes/wipe, message
# queue ordering/draining, newest-N windowing, settings read.
$(TEST_DIR)/test_data: tests/unit/test_data.cpp src/api/config.cpp src/api/http_client.cpp src/api/disk_cache.cpp | $(TEST_DIR)
	@echo "Compiling test_data..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $^ -o $@

# Settings rework (wt/settings): each wired preference control changes the
# persisted Settings value + marks the sync-dirty flag; the mock's settings
# WRITE path accepts + stores a pushed snapshot; the http write-gate is opt-in
# + OFF by default. Pure logic — no graphics, no network. settings.cpp supplies
# the persistence slots; config.cpp + http_client.cpp supply Config + the
# header-only mock client.
$(TEST_DIR)/test_settings: tests/unit/test_settings.cpp src/settings.cpp src/api/config.cpp src/api/http_client.cpp vendor/afterhours/src/plugins/files.cpp | $(TEST_DIR)
	@echo "Compiling test_settings..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $^ -o $@

# Headless e2e: real app logic (state model, glyphs, smart views, tabs, backend
# defaults) against the mock + the real afterhours ECS core. No graphics linked.
$(TEST_DIR)/test_e2e: tests/e2e/test_e2e.cpp src/api/config.cpp src/api/http_client.cpp | $(TEST_DIR)
	@echo "Compiling test_e2e..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $^ -o $@

# Headless perf micro-benchmark: in-process thread-switch latency (built -O2).
$(TEST_DIR)/test_perf: tests/e2e/test_perf.cpp src/api/disk_cache.cpp | $(TEST_DIR)
	@echo "Compiling test_perf..."
	$(CXX) $(PERF_CXXFLAGS) $(TEST_INCLUDES) $^ -o $@

# Read-only REAL-backend smoke: list_sessions + get_session against the ACTUAL
# configured http backend (no mutations). Compiled WITH the TLS flags so it can
# speak https; self-skips (exit 0) when no http backend is configured. This is
# the pre-push "does it work with real data?" check — NOT part of the default
# offline `make test` (it hits the network). Uses CXXFLAGS (which carry
# -DHANABI_ENABLE_TLS + OpenSSL paths only when HANABI_TLS=1).
$(TEST_DIR)/test_real: tests/e2e/test_real.cpp src/api/config.cpp src/api/http_client.cpp | $(TEST_DIR)
	@echo "Compiling test_real..."
	$(CXX) $(CXXFLAGS) $(TEST_INCLUDES) $^ $(LDFLAGS) -o $@

UNIT_TEST_EXES := $(TEST_DIR)/test_api $(TEST_DIR)/test_auth $(TEST_DIR)/test_send $(TEST_DIR)/test_stream $(TEST_DIR)/test_tools $(TEST_DIR)/test_textinput $(TEST_DIR)/test_input_pipeline $(TEST_DIR)/test_data $(TEST_DIR)/test_settings
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

# `make test` = unit + e2e + perf (the full harness, one command).
test: $(UNIT_TEST_EXES) $(E2E_TEST_EXES) $(PERF_TEST_EXES) $(MAIN_EXE)
	@echo "Running unit + e2e tests..."
	$(call RUN_TESTS,$(UNIT_TEST_EXES) $(E2E_TEST_EXES) $(PERF_TEST_EXES))
	@echo "Running launch/RSS perf gate (scripts/measure_launch.sh)..."
	@bash scripts/measure_launch.sh

.PHONY: test unit-e2e e2e perf test-real

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
