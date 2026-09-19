#!/usr/bin/env bash
set -u
record="${HANABI_STUB_RECORD:?HANABI_STUB_RECORD must name the record file}"
printf 'exe=%s argv=%s WINDOWED=%s HEADLESS_ONLY=%s BACKEND=%s POLICY=%s\n' \
    "$0" "$*" "${HANABI_E2E_WINDOWED-<unset>}" "${HANABI_E2E_HEADLESS_ONLY-<unset>}" \
    "${HANABI_BACKEND-<unset>}" "${HANABI_UI_POLICY-<unset>}" >> "$record"
echo "[INFO] stub: E2E finished in 0.00 seconds"
if [ "${HANABI_STUB_PRINT_GFX:-0}" = "1" ]; then
    echo "  Gfx init: 1 ms (stub pretending a window opened)"
fi
if [ -n "${HANABI_STUB_GRANDCHILD:-}" ]; then
    sleep "${HANABI_STUB_SLEEP:-30}" &
    printf '%s\n' "$!" > "$HANABI_STUB_GRANDCHILD"
fi
if [ -n "${HANABI_STUB_ORPHAN:-}" ]; then
    ( sleep "${HANABI_STUB_SLEEP:-30}" & printf '%s\n' "$!" > "$HANABI_STUB_ORPHAN" ) &
    wait $! 2>/dev/null
fi
if [ -n "${HANABI_STUB_SLEEP:-}" ]; then
    exec sleep "$HANABI_STUB_SLEEP"
fi
exit "${HANABI_STUB_EXIT:-0}"
