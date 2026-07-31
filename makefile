# Hanabi — native desktop client. C++23 + afterhours (ECS/UI) + Sokol (Metal).

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
    CXX := clang++
    EXT := .exe
    FRAMEWORKS := -framework CoreFoundation -framework CoreServices \
        -framework Metal -framework MetalKit -framework Cocoa -framework QuartzCore
else ifeq ($(OS),Windows_NT)
    CXX := g++
    EXT := .exe
    FRAMEWORKS :=
else
    CXX := clang++
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
    -DFMT_HEADER_ONLY

INCLUDES := -isystem vendor/ -isystem vendor/afterhours/vendor/
LDFLAGS := -L. $(FRAMEWORKS)

OBJ_DIR := output/objs
OUTPUT_DIR := output

MAIN_SRC := $(shell find src -name '*.cpp')
MAIN_MM_SRC := $(wildcard src/*.mm)
MAIN_MM_OBJS := $(patsubst src/%.mm,$(OBJ_DIR)/main/%.o,$(MAIN_MM_SRC))
MAIN_OBJS := $(MAIN_SRC:src/%.cpp=$(OBJ_DIR)/main/%.o)
MAIN_OBJS += $(MAIN_MM_OBJS)
MAIN_OBJS += $(OBJ_DIR)/main/vendor_afterhours_files.o
MAIN_DEPS := $(MAIN_OBJS:.o=.d)

MAIN_EXE := $(OUTPUT_DIR)/hanabi$(EXT)

$(OUTPUT_DIR)/.stamp:
	@mkdir -p $(OUTPUT_DIR)
	@touch $@

$(OBJ_DIR)/main:
	@mkdir -p $(OBJ_DIR)/main

.DEFAULT_GOAL := all
all: $(MAIN_EXE)

$(MAIN_EXE): $(MAIN_OBJS) | $(OUTPUT_DIR)/.stamp
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
	rm -rf $(OBJ_DIR)

clean-all: clean
	rm -f $(MAIN_EXE)

copy-resources:
	@mkdir -p $(OUTPUT_DIR)/resources/fonts
	@rsync -a --delete resources/ $(OUTPUT_DIR)/resources/

output: $(MAIN_EXE) copy-resources

run: output
	./$(MAIN_EXE)

# macOS .app bundle
APP_BUNDLE := $(OUTPUT_DIR)/Hanabi.app
bundle: $(MAIN_EXE) copy-resources
	@echo "Building Hanabi.app..."
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
		'    <key>CFBundleVersion</key>' \
		'    <string>0.1.0</string>' \
		'    <key>CFBundlePackageType</key>' \
		'    <string>APPL</string>' \
		'    <key>NSHighResolutionCapable</key>' \
		'    <true/>' \
		'</dict>' \
		'</plist>' > $(APP_BUNDLE)/Contents/Info.plist
	@echo "Built $(APP_BUNDLE)"

.PHONY: all clean clean-all deps copy-resources output run bundle

# ==============================================================================
# UNIT TESTS
# ==============================================================================

TEST_CXXFLAGS := $(CXXSTD) -g -O0 -Wall -Wextra \
    -Wno-deprecated-literal-operator -Wno-sign-conversion
TEST_INCLUDES := -isystem vendor/ -I.
TEST_DIR := $(OUTPUT_DIR)/tests

$(TEST_DIR):
	@mkdir -p $(TEST_DIR)

$(TEST_DIR)/test_api: tests/unit/test_api.cpp src/api/config.cpp src/api/http_client.cpp | $(TEST_DIR)
	@echo "Compiling test_api..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $^ -o $@

TEST_EXES := $(TEST_DIR)/test_api

test: $(TEST_EXES)
	@echo "Running unit tests..."
	@PASS=0; FAIL=0; \
	for t in $(TEST_EXES); do \
	    if $$t; then PASS=$$((PASS + 1)); \
	    else FAIL=$$((FAIL + 1)); fi; \
	done; \
	echo "========================================"; \
	echo "Results: $$PASS/$$(( PASS + FAIL )) passed, $$FAIL failed"; \
	echo "========================================"; \
	[ "$$FAIL" -eq 0 ]

.PHONY: test

count:
	git ls-files | grep "src" | grep -v "resources" | grep -v "vendor" | xargs wc -l | sort -rn
