# Hanabi — native desktop client. C++23 + afterhours (ECS/UI) + Sokol (Metal).

UNAME_S := $(shell uname -s)

BRANDING_CONFIG := resources/macos/branding.json
BRANDING_TEMPLATE := resources/macos/Info.plist
BRANDING_TOOL := scripts/branding.py
APP_NAME ?= $(shell /usr/bin/python3 $(BRANDING_TOOL) --config $(BRANDING_CONFIG) field app_name)
BUNDLE_ID ?= $(shell /usr/bin/python3 $(BRANDING_TOOL) --config $(BRANDING_CONFIG) field bundle_id)
EXECUTABLE_NAME ?= $(shell /usr/bin/python3 $(BRANDING_TOOL) --config $(BRANDING_CONFIG) field executable_name)
URL_SCHEME ?= $(shell /usr/bin/python3 $(BRANDING_TOOL) --config $(BRANDING_CONFIG) field url_scheme)
BRANDING_ARGS = --app-name "$(APP_NAME)" --bundle-id "$(BUNDLE_ID)" \
	--executable-name "$(EXECUTABLE_NAME)" --url-scheme "$(URL_SCHEME)"
BRANDING_KEY := $(shell /usr/bin/python3 $(BRANDING_TOOL) --config $(BRANDING_CONFIG) \
	key $(BRANDING_ARGS))
BRANDING_DIR := output/branding/$(BRANDING_KEY)
BRANDING_HEADER := $(BRANDING_DIR)/branding.h
BRANDING_PLIST := $(BRANDING_DIR)/Info.plist
BUILD_STAMP_HEADER := $(BRANDING_DIR)/build_stamp.h
BUILD_STAMP_VALUE := $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)

$(BRANDING_HEADER): $(BRANDING_CONFIG) $(BRANDING_TEMPLATE) $(BRANDING_TOOL)
	@mkdir -p "$(BRANDING_DIR)"
	@/usr/bin/python3 $(BRANDING_TOOL) --config $(BRANDING_CONFIG) generate \
		$(BRANDING_ARGS) --template "$(BRANDING_TEMPLATE)" --output-dir "$(BRANDING_DIR)"

$(BRANDING_PLIST): $(BRANDING_HEADER)
	@test -f "$@" || /usr/bin/python3 $(BRANDING_TOOL) --config $(BRANDING_CONFIG) generate \
		$(BRANDING_ARGS) --template "$(BRANDING_TEMPLATE)" --output-dir "$(BRANDING_DIR)"

.PHONY: FORCE
FORCE:

$(BUILD_STAMP_HEADER): FORCE
	@mkdir -p "$(BRANDING_DIR)"
	@printf '%s\n' '#pragma once' '#define HANABI_BUILD_STAMP "$(BUILD_STAMP_VALUE)"' > "$@.tmp"
	@cmp -s "$@.tmp" "$@" || mv "$@.tmp" "$@"
	@rm -f "$@.tmp"

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
        -framework CoreText -framework Metal -framework MetalKit -framework Cocoa -framework QuartzCore \
        -framework Carbon -framework CoreSpotlight -framework UniformTypeIdentifiers \
        -framework UserNotifications
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
    -include src/build_config.h

INCLUDES := -isystem vendor/ -isystem vendor/afterhours/vendor/ -I$(BRANDING_DIR)
LDFLAGS := -L. $(FRAMEWORKS)

# HTTPS is a Hanabi default whenever OpenSSL is installed. `HANABI_TLS=0`
# remains only for dependency-free portability builds and the offline test lane.
OPENSSL_PREFIX ?= $(shell brew --prefix openssl@3 2>/dev/null)
HANABI_TLS ?= $(if $(strip $(OPENSSL_PREFIX)),1,0)
ifeq ($(HANABI_TLS),1)
    CXXFLAGS += -DHANABI_ENABLE_TLS -I$(OPENSSL_PREFIX)/include
    LDFLAGS += -L$(OPENSSL_PREFIX)/lib -lssl -lcrypto
    ifeq ($(UNAME_S),Darwin)
        LDFLAGS += -framework Security
    endif
endif

OBJ_DIR := output/objs-$(if $(filter 1,$(HANABI_TLS)),tls,notls)-O$(OPT)-$(BRANDING_KEY)
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
TLS_WANT := $(if $(filter 1,$(HANABI_TLS)),tls,notls)-O$(OPT)-$(BRANDING_KEY)
EXE_MODE_MARKER := $(OUTPUT_DIR)/.exe_mode.h
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

$(OBJ_DIR)/main/main.o: $(BUILD_STAMP_HEADER)

$(OBJ_DIR)/main/%.o: src/%.cpp $(BRANDING_HEADER) | $(OBJ_DIR)/main
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@ -MD -MP -MF $(@:.o=.d) -MT $@

$(OBJ_DIR)/main/%.o: src/%.mm $(BRANDING_HEADER) | $(OBJ_DIR)/main
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
	rm -rf "$(APP_BUNDLE)"

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
APP_BUNDLE = $(OUTPUT_DIR)/$(APP_NAME).app
APP_INSTALL_DIR ?= $(HOME)/Applications/$(APP_NAME).app
APP_VERSION := $(shell awk -F'"' '/kVersion/{print $$2; exit}' src/version.h)

app:
	@command -v brew >/dev/null || { echo "make app needs Homebrew OpenSSL (brew install openssl@3)" >&2; exit 1; }
	@test -d "$$(brew --prefix openssl@3 2>/dev/null)" || { echo "make app needs openssl@3 (brew install openssl@3)" >&2; exit 1; }
	@$(MAKE) HANABI_TLS=1 bundle

bundle: $(MAIN_EXE) copy-resources $(BRANDING_HEADER) $(BRANDING_PLIST)
	@echo "Building $(APP_NAME).app..."
	@bash scripts/package_macos_app.sh "$(APP_BUNDLE)" "$(MAIN_EXE)" \
		"$(OUTPUT_DIR)/resources" "$(BRANDING_PLIST)" "$(APP_VERSION)"
	@echo "Built $(APP_BUNDLE)"

verify-app: app
	@bash scripts/verify_macos_app.sh "$(APP_BUNDLE)" "$(BRANDING_PLIST)"

register-app: app
	@bash scripts/manage_macos_app.sh register "$(APP_BUNDLE)" "$(APP_INSTALL_DIR)" "$(BRANDING_PLIST)"
	@echo "registered $(APP_BUNDLE) with LaunchServices"

unregister-app: $(BRANDING_PLIST)
	@bash scripts/manage_macos_app.sh unregister "$(APP_BUNDLE)" "$(APP_INSTALL_DIR)" "$(BRANDING_PLIST)"
	@echo "unregistered $(APP_BUNDLE) from LaunchServices"

install-app: app
	@bash scripts/manage_macos_app.sh install "$(APP_BUNDLE)" "$(APP_INSTALL_DIR)" "$(BRANDING_PLIST)"

uninstall-app: $(BRANDING_PLIST)
	@bash scripts/manage_macos_app.sh uninstall "$(APP_BUNDLE)" "$(APP_INSTALL_DIR)" "$(BRANDING_PLIST)"

launch-app: register-app
	@open -na "$(APP_BUNDLE)"

# `make mock-server` — launch the local dev mock server (tools/mock_server).
# Serves the REST + SSE shape hanabi's http adapter expects so the app can be
# exercised fully offline (list / transcripts / pagination / live events /
# SENDING messages). Pure Python 3 stdlib — no install. See tools/mock_server/
# README.md for the exact env to point hanabi at it (local http, so a NON-TLS
# build works). Override the port with `make mock-server MOCK_PORT=9000`.
MOCK_PORT ?= 8787
mock-server:
	python3 tools/mock_server/server.py --port $(MOCK_PORT)

.PHONY: all clean clean-all deps copy-resources output run run-mock bundle app verify-app \
	register-app unregister-app install-app uninstall-app launch-app mock-server \
	verify-vendor-patches

verify-vendor-patches:
	@python3 scripts/verify_vendor_patches.py

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
TEST_INCLUDES := -isystem vendor/ -I. -I$(BRANDING_DIR)
TEST_DIR := $(OUTPUT_DIR)/tests

$(TEST_DIR):
	@mkdir -p $(TEST_DIR)

# See the note on TEST_CXXFLAGS: every test re-links when any header moves.
TEST_HDRS := $(wildcard src/*.h src/*/*.h src/*/*/*.h) $(BRANDING_HEADER) $(EXE_MODE_MARKER)

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

$(TEST_DIR)/test_widget_key: tests/unit/test_widget_key.cpp src/ui/mk.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_widget_key..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_widget_key.cpp -o $@

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

# The live-block metric, alone in a process. It is a property of the platform
# allocator, so it needs no window and no ECS -- and the drift it pins is
# invisible in anything larger.
$(TEST_DIR)/test_heap_walk: tests/unit/test_heap_walk.cpp src/util/heap_walk.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_heap_walk..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_heap_walk.cpp -o $@

$(TEST_DIR)/test_ellipsize: tests/unit/test_ellipsize.cpp src/util/ellipsize.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_ellipsize..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_ellipsize.cpp -o $@

# Counting wrapped lines without building them. The count decides a message
# box's height and afterhours wraps the same string again to draw it, so the
# property worth pinning is not "these cases look right" but "for every string
# at every width, the counter equals ui::detail::wrap_text_to_width(...).size()"
# -- checked against the REAL vendored wrapper, under rulers no font would give
# you, including one whose prefix width is not monotonic. Pure logic.
$(TEST_DIR)/test_wrap_count: tests/unit/test_wrap_count.cpp src/util/wrap_count.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_wrap_count..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_wrap_count.cpp -o $@

$(TEST_DIR)/test_md_spans: tests/unit/test_md_spans.cpp src/ui/md_spans.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_md_spans..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_md_spans.cpp -o $@

# The BOUND on the shared text-keyed LRU, and what LRU order means at the edge
# of it. Every cache in this app was "bounded" when it was written; two of them
# were bounded by a comment. Pure logic, no graphics.
$(TEST_DIR)/test_text_cache: tests/unit/test_text_cache.cpp src/util/text_cache.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_text_cache..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_text_cache.cpp -o $@

# The glyph-atlas measurement guard, as pure logic: what counts as a width
# that cannot be true, and why the whole of launch does not. The condition
# itself needs a GPU and a full atlas — scripts/atlas_gate.sh reaches it.
$(TEST_DIR)/test_atlas_guard: tests/unit/test_atlas_guard.cpp src/util/atlas_guard.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_atlas_guard..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_atlas_guard.cpp -o $@

# The local-first outbox: the durable store across a REAL process boundary
# (the test re-execs itself) plus the retry policy that reads it back. Pure
# logic + a temp dir — no network, no graphics. disk_cache.cpp for the store.
$(TEST_DIR)/test_outbox: tests/unit/test_outbox.cpp src/api/disk_cache.cpp $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_outbox..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $(filter-out %.h,$^) -o $@

$(TEST_DIR)/test_shortcuts: tests/unit/test_shortcuts.cpp src/shortcuts.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_shortcuts..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_shortcuts.cpp -o $@

$(TEST_DIR)/test_input_pipeline: tests/unit/test_input_pipeline.cpp $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_input_pipeline..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $(filter-out %.h,$^) -o $@

# What the Cmd+G chord means and where a step lands. The chord cannot be
# pressed from a script (gap #49), so the table it resolves through is the only
# part of the binding a test can hold. Pure logic — no graphics, no app state.
$(TEST_DIR)/test_find_nav: tests/unit/test_find_nav.cpp src/ui/find_nav.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_find_nav..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_find_nav.cpp -o $@

$(TEST_DIR)/test_find_memo: tests/unit/test_find_memo.cpp src/search/find_memo.h src/ui/find_operators.h src/util/textscan.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_find_memo..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_find_memo.cpp -o $@

# Cross-session search: what the index matches, and whether the sentence it
# shows about its own coverage is true. Pure logic - no graphics, no disk, no
# app state; the corpus is handed in.
# The arithmetic a windowed digest list is made of. Pure heights and prefix
# sums over api::SessionSummary - the painting that goes with it needs a font
# manager and is asserted on screen (tests/ui/digest_windows.e2e).
$(TEST_DIR)/test_digest_layout: tests/unit/test_digest_layout.cpp src/ecs/digest_layout.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_digest_layout..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_digest_layout.cpp -o $@

$(TEST_DIR)/test_subagent_parent_index: tests/unit/test_subagent_parent_index.cpp src/ecs/subagent_parent_index.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_subagent_parent_index..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_subagent_parent_index.cpp -o $@

$(TEST_DIR)/test_sidebar_buckets: tests/unit/test_sidebar_buckets.cpp src/ecs/sidebar_buckets.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_sidebar_buckets..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_sidebar_buckets.cpp -o $@

$(TEST_DIR)/test_home_buckets: tests/unit/test_home_buckets.cpp src/ecs/home_buckets.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_home_buckets..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_home_buckets.cpp -o $@

$(TEST_DIR)/test_contains_lower: tests/unit/test_contains_lower.cpp src/util/format.h src/ecs/sidebar_buckets.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_contains_lower..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_contains_lower.cpp -o $@

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

$(TEST_DIR)/test_theme_contrast: tests/unit/test_theme_contrast.cpp src/ui/theme.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_theme_contrast..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_theme_contrast.cpp -o $@

# The focus ring's colour. rendering.h derives the ring's two contrast edges
# from the RING's luminance rather than the backdrop's, so which side of its
# 0.5 threshold the ring lands on decides whether those edges are invisible or
# are the brightest thing on screen. Same split as test_tab_colors: no scripted
# assertion can see a colour, so the guard is arithmetic.
$(TEST_DIR)/test_focus_ring: tests/unit/test_focus_ring.cpp src/ui/focus_visible.h src/ui/theme.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_focus_ring..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_focus_ring.cpp -o $@

# The sidebar footer's arithmetic. The one property here that no other harness
# in the repo can see is that the activity light's origin is on a whole pixel:
# assert_ui reads x/y/w/h and ROUNDS them, so it cannot tell a snapped position
# from an unsnapped one, and afterhours draws an unsnapped 6px box five rows
# tall. Same split as test_tab_colors: the screenshot suite holds the
# placement, this holds the property.
$(TEST_DIR)/test_pane_memory: tests/unit/test_pane_memory.cpp src/ecs/pane_state.h src/ecs/transcript_render_cache.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_pane_memory..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_pane_memory.cpp -o $@

$(TEST_DIR)/test_transcript_item_index: tests/unit/test_transcript_item_index.cpp src/ecs/transcript_item_index.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_transcript_item_index..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_transcript_item_index.cpp -o $@

# Widget retirement: the sweep, and the two ways it could be quietly wrong
# (a `mk` wrapper that eats the call site, a hash left pointing at a dead
# entity). Header-only -- the UI collection and imm::mk need no graphics.
$(TEST_DIR)/test_widget_retire: tests/unit/test_widget_retire.cpp src/ui/widget_epoch.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_widget_retire..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_widget_retire.cpp -o $@

# What a texture costs on the GPU. Pure arithmetic plus a ledger, so it links
# NO Metal and no graphics: src/gpu_mem.mm is deliberately absent here, which
# is also what makes the "device accounting is absent, and says so" assertion
# meaningful in this binary.
$(TEST_DIR)/test_gpu_mem: tests/unit/test_gpu_mem.cpp src/util/gpu_mem.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_gpu_mem..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_gpu_mem.cpp -o $@

# The texture cache's LRU policy, driven with synthetic entries. Links no GPU,
# which is the whole point: every claim it asserts used to be a paragraph of
# comment in a header that cannot be compiled without one.
$(TEST_DIR)/test_texture_budget: tests/unit/test_texture_budget.cpp src/util/texture_budget.h src/util/gpu_mem.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_texture_budget..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_texture_budget.cpp -o $@

# Decode-to-fit's policy and its filter. Pure: the halve is transcribed from
# the vendored backend's build_mip_chain and this asserts it is bit-exact,
# which is the entire safety argument for reducing an image before upload.
$(TEST_DIR)/test_downscale: tests/unit/test_downscale.cpp src/util/downscale.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_downscale..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_downscale.cpp -o $@

$(TEST_DIR)/test_footer_geometry: tests/unit/test_footer_geometry.cpp src/ecs/sidebar_footer_geometry.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_footer_geometry..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_footer_geometry.cpp -o $@

$(TEST_DIR)/test_secondary_surface: tests/unit/test_secondary_surface.cpp src/ui/secondary_surface_geometry.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_secondary_surface..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_secondary_surface.cpp -o $@

# The minimap rail's coordinate system: the map the scrubber band is drawn
# through and the inverse map a drag along the rail is. The property is that
# the two are exact inverses, which no screenshot can see (the button has to be
# held) and the scripted suite can only see the downstream half of. Pure
# arithmetic, no graphics.
$(TEST_DIR)/test_minimap_marks: tests/unit/test_minimap_marks.cpp src/ui/minimap_marks.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_minimap_marks..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_minimap_marks.cpp -o $@

$(TEST_DIR)/test_minimap_scrub: tests/unit/test_minimap_scrub.cpp src/ui/minimap_scrub.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_minimap_scrub..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_minimap_scrub.cpp -o $@

# Whether the transcript is pinned to its newest message this frame. A state
# machine over the scroll offset and the wheel's target, and the four ways it
# can be wrong are indistinguishable on screen - see the header for the one
# that shipped and ate every wheel event in the app.
$(TEST_DIR)/test_frame_activity: tests/unit/test_frame_activity.cpp src/frame_activity.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_frame_activity..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_frame_activity.cpp -o $@

$(TEST_DIR)/test_follow_latch: tests/unit/test_follow_latch.cpp src/ecs/follow_latch.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_follow_latch..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_follow_latch.cpp -o $@

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

$(TEST_DIR)/test_menubar: tests/unit/test_menubar.mm src/menubar.mm src/menubar.h src/settings.cpp src/settings.h src/shortcuts.h vendor/afterhours/src/plugins/files.cpp $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_menubar..."
	$(CXX) -ObjC++ $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_menubar.mm \
		src/menubar.mm src/settings.cpp vendor/afterhours/src/plugins/files.cpp \
		-framework AppKit -framework Carbon -o $@

$(TEST_DIR)/test_agentcloud_local: tests/e2e/test_agentcloud_local.cpp src/api/agentcloud_auth.cpp src/api/agentcloud_client.cpp src/ws_socket.mm $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_agentcloud_local..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) -fobjc-arc $(filter-out %.h,$^) \
	    -framework Foundation -framework CFNetwork -o $@

test-agentcloud-local: $(TEST_DIR)/test_agentcloud_local
	@bash scripts/test_agentcloud_local.sh

$(TEST_DIR)/test_native_extras: tests/unit/test_native_extras.mm src/native_extras.mm src/native_extras.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_native_extras..."
	$(CXX) -ObjC++ $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_native_extras.mm \
		src/native_extras.mm -framework AppKit -framework Carbon -framework CoreText -framework CoreSpotlight \
		-framework UniformTypeIdentifiers -framework UserNotifications -framework MetalKit -o $@

$(TEST_DIR)/test_spotlight_catalog: tests/unit/test_spotlight_catalog.cpp src/util/spotlight_catalog.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_spotlight_catalog..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_spotlight_catalog.cpp -o $@

$(TEST_DIR)/test_div_move: tests/unit/test_div_move.cpp src/ui/div.h src/ui/mk.h $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_div_move..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) tests/unit/test_div_move.cpp -o $@

UNIT_TEST_EXES := $(TEST_DIR)/test_native_extras $(TEST_DIR)/test_menubar $(TEST_DIR)/test_shortcuts $(TEST_DIR)/test_spotlight_catalog $(TEST_DIR)/test_api $(TEST_DIR)/test_auth $(TEST_DIR)/test_send $(TEST_DIR)/test_stream $(TEST_DIR)/test_tools $(TEST_DIR)/test_textinput $(TEST_DIR)/test_input_pipeline $(TEST_DIR)/test_data $(TEST_DIR)/test_settings $(TEST_DIR)/test_agentcloud $(TEST_DIR)/test_notify_events $(TEST_DIR)/test_find_nav $(TEST_DIR)/test_session_index $(TEST_DIR)/test_subagent_parent_index $(TEST_DIR)/test_sidebar_buckets $(TEST_DIR)/test_home_buckets $(TEST_DIR)/test_contains_lower $(TEST_DIR)/test_snippet_text $(TEST_DIR)/test_diff $(TEST_DIR)/test_ellipsize $(TEST_DIR)/test_trend $(TEST_DIR)/test_tab_colors $(TEST_DIR)/test_footer_geometry $(TEST_DIR)/test_secondary_surface $(TEST_DIR)/test_pane_memory $(TEST_DIR)/test_transcript_item_index $(TEST_DIR)/test_wrap_count $(TEST_DIR)/test_md_spans $(TEST_DIR)/test_text_cache $(TEST_DIR)/test_widget_retire $(TEST_DIR)/test_gpu_mem $(TEST_DIR)/test_texture_budget $(TEST_DIR)/test_downscale $(TEST_DIR)/test_digest_layout $(TEST_DIR)/test_heap_walk $(TEST_DIR)/test_minimap_scrub $(TEST_DIR)/test_minimap_marks $(TEST_DIR)/test_focus_ring $(TEST_DIR)/test_outbox $(TEST_DIR)/test_atlas_guard $(TEST_DIR)/test_frame_activity $(TEST_DIR)/test_follow_latch $(TEST_DIR)/test_theme_contrast $(TEST_DIR)/test_find_memo $(TEST_DIR)/test_div_move $(TEST_DIR)/test_widget_key
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
	@echo "Running text measurement gate (scripts/perf_text_gate.sh)..."
	@bash scripts/perf_text_gate.sh
	@echo "Running find level gate (scripts/find_gate.sh)..."
	@bash scripts/find_gate.sh

# `make test` = unit + e2e + scripted UI + perf + the screenshot subset (the
# full harness, one command).
test: $(UNIT_TEST_EXES) $(E2E_TEST_EXES) $(PERF_TEST_EXES) $(MAIN_EXE)
	@echo "Running unit + e2e tests..."
	$(call RUN_TESTS,$(UNIT_TEST_EXES) $(E2E_TEST_EXES) $(PERF_TEST_EXES))
	@$(MAKE) test-agentcloud-local
	@$(MAKE) uitest
	@$(MAKE) harness-gate
	@$(MAKE) tab-persistence-gate
	@echo "Running launch/RSS perf gate (scripts/measure_launch.sh)..."
	@bash scripts/measure_launch.sh
	@echo "Running transcript slope gate (scripts/perf_transcript_slope.sh)..."
	@bash scripts/perf_transcript_slope.sh
	@echo "Running text measurement gate (scripts/perf_text_gate.sh)..."
	@bash scripts/perf_text_gate.sh
	@echo "Running find level gate (scripts/find_gate.sh)..."
	@bash scripts/find_gate.sh
	@$(MAKE) soak-gate
	@$(MAKE) alloc-gate
	@$(MAKE) idle-gate
	@$(MAKE) scaling-gate
	@$(MAKE) memory-scaling-gate
	@$(MAKE) scroll-gate
	@$(MAKE) retire-gate
	@$(MAKE) digest-gate
	@$(MAKE) home-scan-gate
	@$(MAKE) subagent-index-gate
	@$(MAKE) sidebar-scan-gate
	@$(MAKE) bounds-gate
	@$(MAKE) validate-screenshots-fast
	@$(MAKE) events-gate
	@$(MAKE) atlas-gate
	@$(MAKE) chrome-gate
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
#   make memory-scaling-gate ~90 s. Opens and switches 20, 100 and 500 kept
#                      tabs, then adds two panes, find, the item index, outbox
#                      and streaming state. Gates retained-byte/RSS ratios and
#                      exact cache counts, never elapsed wall time. In `make test`.
#   make scroll-gate   ~6 s.  The sidebar's list EXPANDED and swept, which is
#                      what the bug report described. Two arms: the entity
#                      count must not track the catalog (the level), and the
#                      second half of a long scroll must cost what the first
#                      half did (the trend). In `make test`.
#   make digest-gate   ~9 s.  The four digest screens (Blocked / Review /
#                      Starred / Archived), which scaling-gate never opens and
#                      scroll-gate never reaches. Cards BUILT against sessions
#                      MATCHED, plus the widget ratio. In `make test`.
#   make sidebar-scan-gate
#                      ~35 s. The sidebar must not re-derive its rows from the
#                      whole catalog on a frame where nothing changed. Four
#                      COUNT arms: rows drawn, collections per run (level),
#                      reuse over rebuilds, and the two-folder/no-folder visit
#                      ratio (trend). In `make test`.
#   make retire-gate   ~3 s.  Navigates five screens and a thread, then counts
#                      the widgets nothing is building any more. A COUNT, not
#                      a millisecond. In `make test`.
#   make alloc-gate    ~20 s. Steady-state operator new calls PER FRAME, at
#                      three fixtures. The only gate here that measures a
#                      LEVEL rather than a slope, because churn does not grow
#                      — it is just paid again every frame forever, and every
#                      slope gate reads it as perfectly flat. The number is
#                      deterministic to within one allocation whatever the box
#                      is doing. In `make test`.
#   make soak          ~45 s. The long form: thirteen scenarios, four at a time.
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
# Break every gate on purpose and record what it did. NOT in `make test` --
# it rebuilds the app once per defect and takes ~25 minutes. Run it when a
# threshold moves, when a gate is added, or when a fix reroutes work a gate was
# watching: that last one is how perf_transcript_slope.sh went permanently
# green without anyone noticing. docs/perf/GATES.md section 0.
gate-audit: $(MAIN_EXE) copy-resources
	@/usr/bin/python3 scripts/gate_audit.py $(DEFECT)

soak-gate: $(MAIN_EXE) copy-resources
	@bash scripts/soak_gate.sh

scaling-gate: $(MAIN_EXE) copy-resources
	@bash scripts/scaling_gate.sh

memory-scaling-gate: $(MAIN_EXE) copy-resources
	@bash scripts/memory_scaling_gate.sh

scroll-gate: $(MAIN_EXE) copy-resources
	@bash scripts/scroll_gate.sh

find-gate: $(MAIN_EXE) copy-resources
	@bash scripts/find_gate.sh

retire-gate: $(MAIN_EXE) copy-resources
	@bash scripts/retire_gate.sh
alloc-gate: $(MAIN_EXE) copy-resources
	@bash scripts/alloc_gate.sh
idle-gate: $(MAIN_EXE) copy-resources
	@bash scripts/idle_gate.sh
digest-gate: $(MAIN_EXE) copy-resources
	@bash scripts/digest_gate.sh --selftest
	@bash scripts/digest_gate.sh

home-scan-gate: $(MAIN_EXE) copy-resources
	@/usr/bin/python3 scripts/check_home_scan.py
	@bash scripts/home_scan_gate.sh --selftest
	@bash scripts/home_scan_gate.sh

subagent-index-gate: $(MAIN_EXE) copy-resources
	@bash scripts/subagent_index_gate.sh $(MAIN_EXE)

sidebar-scan-gate: $(MAIN_EXE) copy-resources
	@bash scripts/sidebar_scan_gate.sh --selftest
	@bash scripts/sidebar_scan_gate.sh $(MAIN_EXE)

events-gate: $(MAIN_EXE) copy-resources
	@bash scripts/events_gate.sh

bounds-gate: $(MAIN_EXE) copy-resources
	@bash scripts/bounds_gate.sh

chrome-gate: $(MAIN_EXE) copy-resources
	@bash scripts/composer_chrome_gate.sh

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
# The glyph atlas measured, and the detector for it PROVED by filling the
# atlas on purpose. Counts and exit codes only — no milliseconds, so a busy
# box cannot move the verdict. See scripts/atlas_gate.sh and gap #211/#350.
atlas-gate: $(MAIN_EXE)
	@bash scripts/atlas_gate.sh

source-checks: $(BRANDING_HEADER) $(BRANDING_PLIST)
	@echo "Running source checks..."
	@rc=0; \
	if /usr/bin/python3 scripts/branding.py --config "$(BRANDING_CONFIG)" check \
	    $(BRANDING_ARGS) --template "$(BRANDING_TEMPLATE)" --output-dir "$(BRANDING_DIR)" --root .; then :; else rc=1; fi; \
	if /usr/bin/python3 tests/test_branding.py; then :; else rc=1; fi; \
	for chk in scripts/check_label_padding.py scripts/check_autorelease.py scripts/check_watchdogs.py scripts/check_fixture_env.py scripts/check_gap_references.py scripts/check_div_routing.py scripts/check_sidebar_scan.py scripts/check_home_scan.py; do \
	    if /usr/bin/python3 $$chk; then :; else rc=1; fi; \
	done; \
	if /usr/bin/python3 scripts/compare.py --selftest; then :; else rc=1; fi; \
	if bash scripts/measure_launch.sh --selftest; then :; else rc=1; fi; \
	exit $$rc

.PHONY: test unit-e2e e2e perf test-real test-agentcloud-real test-agentcloud-local soak soak-gate scaling-gate memory-scaling-gate scroll-gate \
	retire-gate alloc-gate idle-gate events-gate home-scan-gate subagent-index-gate sidebar-scan-gate chrome-gate source-checks soak-report soak-baseline stress stress-break gate-audit atlas-gate

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

# A LIVE agentcloud session, end to end -- the check the mock cannot be.
# Reads a real transcript through the real socket and, with
# HANABI_AC_PROBE_SEND=1, sends ONE turn and asserts the reply is the agent's
# answer and nothing else (see the file header; this is the A1 regression).
# Self-skips without HANABI_AC_* and HANABI_AC_PROBE_SESSION, so it is not in
# `make test`. Endpoints come from .env, which is gitignored.
#     HANABI_AC_PROBE_SESSION=<id> make test-agentcloud-real
#     HANABI_AC_PROBE_SESSION=<id> HANABI_AC_PROBE_SEND=1 make test-agentcloud-real
$(TEST_DIR)/test_agentcloud_real: tests/e2e/test_agentcloud_real.cpp src/api/agentcloud_auth.cpp src/api/agentcloud_client.cpp src/ws_socket.mm $(TEST_HDRS) | $(TEST_DIR)
	@echo "Compiling test_agentcloud_real..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) -fobjc-arc $(filter-out %.h,$^) \
	    -framework Foundation -framework CFNetwork -o $@

test-agentcloud-real: $(TEST_DIR)/test_agentcloud_real
	@set -a; [ -f .env ] && . ./.env; set +a; \
	echo "Running live agentcloud round-trip (test_agentcloud_real)..."; \
	$(TEST_DIR)/test_agentcloud_real

count:
	git ls-files | grep "src" | grep -v "resources" | grep -v "vendor" | xargs wc -l | sort -rn

# --- Scripted UI tests --------------------------------------------------------
# A SECOND binary of the whole app, compiled with afterhours' e2e input hooks
# on, that drives the real UI from a .e2e script: move the mouse, click, type,
# assert on the text that is actually on screen. Its own object dir so the
# shipping build never sees the test-input branches.
UITEST_OBJ_DIR := output/objs-uitest-O$(OPT)-$(BRANDING_KEY)
UITEST_CXXFLAGS := $(CXXFLAGS) -DAFTER_HOURS_ENABLE_E2E_TESTING
UITEST_OBJS := $(MAIN_SRC:src/%.cpp=$(UITEST_OBJ_DIR)/%.o)
UITEST_OBJS += $(patsubst src/%.mm,$(UITEST_OBJ_DIR)/%.o,$(MAIN_MM_SRC))
UITEST_OBJS += $(UITEST_OBJ_DIR)/vendor_afterhours_files.o
UITEST_DEPS := $(UITEST_OBJS:.o=.d)
UITEST_EXE := $(OUTPUT_DIR)/hanabi_uitest$(EXT)

-include $(UITEST_DEPS)

$(UITEST_OBJ_DIR)/main.o: $(BUILD_STAMP_HEADER)

$(UITEST_OBJ_DIR)/%.o: src/%.cpp $(BRANDING_HEADER)
	@echo "Compiling (uitest) $<..."
	@mkdir -p $(dir $@)
	$(CXX) $(UITEST_CXXFLAGS) $(INCLUDES) -c $< -o $@ -MD -MP -MF $(@:.o=.d) -MT $@

$(UITEST_OBJ_DIR)/%.o: src/%.mm $(BRANDING_HEADER)
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

# The suite in a RANDOM ORDER. A scripted test that passes only after some
# other test has run is a test that proves nothing, and this project has spent
# three investigations on symptoms of that class. The seed is printed and
# accepted (`make uitest-shuffle SEED=1234`), so an order that fails is an
# order that can be re-run.
SEED ?= $(shell awk 'BEGIN{srand();print int(rand()*100000)}')
uitest-shuffle: $(UITEST_EXE) copy-resources
	@HANABI_UI_SEED=$(SEED) bash scripts/run_ui_tests.sh

# Every script ALONE, one suite invocation each: the census that says how many
# tests pass only in suite order. Slow on purpose (it is the same 105 runs plus
# 105 harness startups) and not part of `make test`; `make uitest-shuffle` is
# the cheap standing defence.
uitest-alone: $(UITEST_EXE) copy-resources
	@bash scripts/run_ui_tests_alone.sh

# The harness's own tests: per-script homes, and a script that states a
# precondition the harness enforces.
harness-gate: $(UITEST_EXE) copy-resources
	@bash scripts/harness_gate.sh

tab-persistence-gate: $(UITEST_EXE) copy-resources
	@bash scripts/tab_persistence_gate.sh

# Build the scripted-UI binary without running the suite (used by
# scripts/review_shots.sh, which drives it directly).
uitest-build: $(UITEST_EXE) copy-resources

.PHONY: uitest uitest-shuffle uitest-alone harness-gate tab-persistence-gate uitest-build

# ==============================================================================
# SCREENSHOT BASELINES  (docs/breakdown/screenshot-testing.md, chunks 1-3)
# ==============================================================================

SHOT_BASELINES := docs/screenshots/baselines
SHOT_CURRENT := $(OUTPUT_DIR)/screenshots/current
SHOT_FAST_CURRENT := $(OUTPUT_DIR)/screenshots/fast
SHOT_DETERMINISM := $(OUTPUT_DIR)/screenshots/determinism
SHOT_DECLARED := $(OUTPUT_DIR)/screenshots/declared.txt
SHOT_FAILURES := test-failures

# THE FAST SUBSET, the one `make test` runs.
#
# The full compare was a separate target for months and rotted unnoticed: by
# the time anyone ran it, 30 of 30 baselines failed and no commit in between
# had been told about it. A net nothing runs is not a net, so a subset runs on
# every `make test` and the whole set stays behind validate-screenshots.
#
# Eight screens, chosen to touch each thing a rendering change breaks rather
# than to cover each feature: both palettes, the digest and the transcript, a
# sheet over a dimmed backdrop, the folded rail, the icon atlas, and the
# composer with its focus ring — the exact widget gap #262's grey-interior
# regression edited. Anything that moves the theme, the fonts, the roundness
# or the layout shows up in these.
SHOT_FAST := 01_home_dark 02_home_light 03_transcript_dark 14_sidebar_folded_dark 14b_subagent_sidebar_dark \
             15_settings_dark 18_auth_dark 22_split_view_dark 28_composer_focus_dark

# --only takes one comma-separated value; make has no join, so build the
# separators by hand.
comma := ,
empty :=
space := $(empty) $(empty)
SHOT_FAST_CSV = $(subst $(space),$(comma),$(strip $(SHOT_FAST)))

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

# The fast subset, in `make test`. Captures and compares $(SHOT_FAST) and
# nothing else, so it says nothing about the other screens — validate-screenshots
# is still the one that checks the whole set and the declared/unbaselined
# accounting. Its failures land in the same $(SHOT_FAILURES)/ evidence dir.
validate-screenshots-fast: $(MAIN_EXE) copy-resources
	@echo "=== screenshots: the $(words $(SHOT_FAST))-screen subset ==="
	$(call CAPTURE_SCREENS,$(SHOT_FAST_CURRENT),$(call shot_filter_of,$(SHOT_FAST)))
	@$(SHOT_PYTHON) scripts/compare_screenshots.py \
	    --baselines $(SHOT_BASELINES) --current $(SHOT_FAST_CURRENT) \
	    --only '$(SHOT_FAST_CSV)' \
	    --failures-dir $(SHOT_FAILURES) --json $(SHOT_FAILURES)/summary-fast.json

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

.PHONY: test-screenshot-determinism validate-screenshots validate-screenshots-fast update-baselines

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
