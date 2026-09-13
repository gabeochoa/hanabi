# hanabi builds with `zig build`. This file is a forwarder for fingers that
# still type make: every target name below is the same step name in build.zig
# (`zig build --help` lists them all), so `make test` is `zig build test`.
#
# The graph itself -- sources, flags, generated headers, the 75 test
# executables, the gates -- lives in build.zig and nowhere else. Do not add a
# rule here that builds anything.
#
# Knobs:   make ... TLS=0            ->  zig build ... -Dtls=false
#          make ... OPT=0            ->  zig build ... -Dopt=0
#          make ... WERROR=0         ->  zig build ... -Dwerror=false
#          make app APP_NAME=Ember   ->  zig build app -Dapp-name=Ember   (and
#          BUNDLE_ID / EXECUTABLE_NAME / URL_SCHEME the same way)

ZIG ?= zig
ZIG_FLAGS :=
ifdef TLS
ZIG_FLAGS += -Dtls=$(if $(filter 0,$(TLS)),false,true)
endif
ifdef HANABI_TLS
ZIG_FLAGS += -Dtls=$(if $(filter 0,$(HANABI_TLS)),false,true)
endif
ifdef OPT
ZIG_FLAGS += -Dopt=$(OPT)
endif
ifdef WERROR
ZIG_FLAGS += -Dwerror=$(if $(filter 0,$(WERROR)),false,true)
endif
ifdef APP_NAME
ZIG_FLAGS += -Dapp-name="$(APP_NAME)"
endif
ifdef BUNDLE_ID
ZIG_FLAGS += -Dbundle-id="$(BUNDLE_ID)"
endif
ifdef EXECUTABLE_NAME
ZIG_FLAGS += -Dexecutable-name="$(EXECUTABLE_NAME)"
endif
ifdef URL_SCHEME
ZIG_FLAGS += -Durl-scheme="$(URL_SCHEME)"
endif

.DEFAULT_GOAL := all

all output:
	@echo "make $@ -> $(ZIG) build $(ZIG_FLAGS)"
	@$(ZIG) build $(ZIG_FLAGS)

clean:
	@echo "make clean -> rm -rf .zig-cache output/objs* output/tests output/hanabi*.exe"
	@rm -rf .zig-cache output/objs* output/tests output/hanabi.exe output/hanabi_uitest.exe

clean-all:
	@echo "make clean-all -> rm -rf .zig-cache output"
	@rm -rf .zig-cache output

# Anything else is a build.zig step of the same name.
%:
	@echo "make $@ -> $(ZIG) build $@ $(ZIG_FLAGS)"
	@$(ZIG) build $@ $(ZIG_FLAGS)

.PHONY: all output clean clean-all
