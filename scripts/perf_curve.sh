#!/usr/bin/env bash
# The scaling curve: how frame time and entity count move with catalog size.
#
#   scripts/perf_curve.sh [scenario] [reps] [frames]
#   env: BIN=<binary>  SIZES="0 100 500 1000 2000"  TAB=<active tab id>
#
# WHY THIS IS IN THE REPO. Two reasons, both learned the hard way in the
# session that added it.
#
#   * A LOOSE SCRIPT IN /tmp IS NOT A HARNESS. This Mac is shared. A script at
#     /tmp/soak.sh was replaced by another agent's version -- same name,
#     hard-coded to a DIFFERENT checkout's binary -- and twenty minutes of
#     "measurements" were of someone else's build, with no error and no clue
#     but a number that would not move. A harness has to live with the code it
#     measures.
#   * ONE NUMBER IS NOT AN ANSWER. The question is never "how fast is it", it
#     is "what does it scale with", and that is a table.
#
# HOW IT MEASURES, and why. Per (size, rep) it takes the MINIMUM bucket's
# ms/frame: contention from other work on the box only ever makes a bucket
# slower, never faster, so the minimum is the least-contended estimate of our
# own cost. Across reps it takes the MEDIAN of those minima, which throws out a
# run that was slow start to finish. The minimum is biased low in absolute
# terms and that is fine -- it is a comparison instrument, so use the same
# reps and frames on both sides and compare the columns, never a number here
# against a number from somewhere else.
#
# Entity count needs none of that. It is exact, it is the same every run, and
# it is usually the more useful column: frame time says something is wrong and
# the entity count says what is being materialized. HANABI_SOAK_CENSUS=1 breaks
# that number down by the widget that made it.
set -u
SC=${1:-idle}
REPS=${2:-5}
FR=${3:-800}
BIN=${BIN:-output/hanabi.exe}
SIZES=${SIZES:-"0 100 500 1000 2000"}
TAB=${TAB:-r9}

[ -x "$BIN" ] || { echo "no such binary: $BIN" >&2; exit 1; }

med() { sort -n | awk '{a[NR]=$1} END{print (NR%2)?a[(NR+1)/2]:(a[NR/2]+a[NR/2+1])/2}'; }

# One run. A private HOME per run so no settings survive into the next one --
# a persisted folded shelf or a remembered tab is a different render, and a
# run that inherits one is not comparable with a run that does not.
run_once() {
    local n=$1 home
    home=$(mktemp -d)
    mkdir -p "$home/Library/Application Support/hanabi"
    printf '{"window_width":1180,"window_height":949,"open_tabs":["%s"],"active_tab":"%s","theme":"dark"}\n' \
        "$TAB" "$TAB" > "$home/Library/Application Support/hanabi/settings.json"
    env HOME="$home" HANABI_WIN_W=1180 HANABI_WIN_H=949 HANABI_BACKEND=mock \
        HANABI_CONFIG=/tmp/hanabi_perf_curve_no_config \
        HANABI_SOAK="$FR" HANABI_SOAK_EVERY=100 HANABI_STRESS="$SC" \
        HANABI_STRESS_SESSIONS="$n" \
        "$BIN" --screenshot "$home/shot.png" 2>&1 | grep 'ms/f'
    rm -rf "$home"
}

echo "# $SC, min-of-bucket over $FR frames, median of $REPS runs -- $BIN"
printf '%9s %10s %10s %10s\n' sessions ms/frame entities liveKB
for n in $SIZES; do
    mins=(); ents=""; kb=""
    for _ in $(seq "$REPS"); do
        out=$(run_once "$n")
        [ -n "$out" ] || { echo "no soak output at $n sessions" >&2; exit 1; }
        mins+=("$(echo "$out" | awk '{print $4}' | sort -n | head -1)")
        ents=$(echo "$out" | tail -1 | awk '{print $10}')
        kb=$(echo "$out" | tail -1 | awk '{print $15}')
    done
    printf '%9s %10s %10s %10s\n' "$n" "$(printf '%s\n' "${mins[@]}" | med)" "$ents" "$kb"
done
