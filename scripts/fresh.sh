#!/usr/bin/env bash
# scripts/fresh.sh — refuse to measure a binary older than the source.
#
# WHY. This cost two debugging sessions in one afternoon, both times reading
# as a catastrophic regression. `output/hanabi.exe` was left behind by a build
# of a DELIBERATELY BROKEN tree — the pool-less binary used to verify a gate
# goes red — and the next `make soak` reported every one of its eleven arms
# leaking +2816 KB per 1000 frames, in perfect agreement with each other,
# which is exactly what a real leak looks like:
#
#     idle      FAIL   RSS +2832.0 KB   heap +2760.4 KB
#     scroll    FAIL   RSS +2806.9 KB   heap +2754.2 KB
#     read      FAIL   RSS +2803.2 KB   heap +2759.0 KB
#     ... every arm, the same number
#
# Puffin's PERFORMANCE.md has the identical rule from the identical mistake:
# "Three fixes were 'verified' against a 15-minute-old app before anyone
# compared timestamps. A stale bundle and a wrong fix look identical."
#
# Eleven arms agreeing to three significant figures is in fact the TELL — a
# real leak that idle, scroll and resize all share to 1% is implausible — but
# reading that takes knowing it, and the check costs one `find`.
#
# It is a WARNING and not a hard failure by default, because a gate that
# refuses to run is its own kind of broken and a reader may have a good reason
# (measuring a binary built elsewhere, or one deliberately built from a
# patched tree — which is what HANABI_EXE is for). Set HANABI_REQUIRE_FRESH=1
# to make it exit 2 instead.
#
# Usage:  . "$ROOT/scripts/fresh.sh"; require_fresh_build "$EXE"

require_fresh_build() {
    local exe="$1"
    [ -x "$exe" ] || return 0
    local root
    root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

    # Only hanabi's own sources. vendor/ is a submodule that a `git submodule
    # update` can touch without anything needing a rebuild, and including it
    # would make this warn constantly and then be ignored.
    local newer
    newer=$(find "$root/src" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.mm' \) \
                 -newer "$exe" 2>/dev/null | head -5)
    [ -z "$newer" ] && return 0

    echo "  ! STALE BINARY: $(basename "$exe") is older than the source." >&2
    echo "    These are newer than the binary you are about to measure:" >&2
    printf '      %s\n' $newer | sed "s|$root/||" >&2
    echo "    A stale binary and a real regression are indistinguishable in" >&2
    echo "    the output. Run 'make' first." >&2
    if [ "${HANABI_REQUIRE_FRESH:-0}" = "1" ]; then
        echo "    HANABI_REQUIRE_FRESH=1, so this is fatal." >&2
        return 2
    fi
    echo "    (warning only; HANABI_REQUIRE_FRESH=1 makes this fatal)" >&2
    return 0
}
