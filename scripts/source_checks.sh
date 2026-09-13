#!/usr/bin/env bash
# The makefile's `source-checks` recipe. Every checker runs; the exit code is
# the OR of theirs, so one red does not hide the next.
#   --branding-dir <dir>   the generated branding directory (build.zig hands it
#                          over; the branding checker compares against it)
set -u
cd "$(dirname "$0")/.."
export PYTHONDONTWRITEBYTECODE=1
BRANDING_DIR=""
while [ $# -gt 0 ]; do
    case "$1" in
        --branding-dir) BRANDING_DIR="$2"; shift 2 ;;
        *) echo "source_checks.sh: unknown argument $1" >&2; exit 2 ;;
    esac
done
if [ -z "$BRANDING_DIR" ]; then
    BRANDING_DIR="$(mktemp -d /tmp/hanabi-branding.XXXXXX)"
    /usr/bin/python3 scripts/branding.py --config resources/macos/branding.json generate \
        --template resources/macos/Info.plist --output-dir "$BRANDING_DIR" || exit 2
fi
echo "Running source checks..."
rc=0
if /usr/bin/python3 scripts/branding.py --config resources/macos/branding.json check \
    --template resources/macos/Info.plist --output-dir "$BRANDING_DIR" --root .; then :; else rc=1; fi
if /usr/bin/python3 tests/test_branding.py; then :; else rc=1; fi
for chk in scripts/check_label_padding.py scripts/check_autorelease.py scripts/check_watchdogs.py \
           scripts/check_fixture_env.py scripts/check_gap_references.py scripts/check_div_routing.py \
           scripts/check_sidebar_scan.py scripts/check_home_scan.py scripts/check_theme_config.py \
           scripts/check_vocabulary.py scripts/check_settings_readers.py scripts/check_resize_deferral.py \
           scripts/check_frame_signal_merge.py scripts/check_wire_event_vocabulary.py \
           scripts/focus_edge_gate.py scripts/attachment_route_gate.py scripts/check_build_graph.py; do
    if /usr/bin/python3 "$chk"; then :; else rc=1; fi
done
if /usr/bin/python3 scripts/compare.py --selftest; then :; else rc=1; fi
if bash scripts/measure_launch.sh --selftest; then :; else rc=1; fi
if bash scripts/composer_parity_gate.sh --selftest; then :; else rc=1; fi
if bash scripts/composer_parity_gate.sh; then :; else rc=1; fi
exit $rc
