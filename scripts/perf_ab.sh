#!/usr/bin/env bash
# A/B two binaries on the same curve, interleaved run for run.
#
#   scripts/perf_ab.sh <binA> <binB> [scenario] [reps] [frames]
#   env: SIZES="..."  TAB=<active tab id>
#
# WHY INTERLEAVED. This Mac is shared. Running all of A and then all of B means
# a load spike during A's block is indistinguishable from B being faster --
# and it will not look like noise, it will look like a clean result, because
# every size in the block moves together. Alternating puts any spike into both
# columns. A 24% "regression" that this found and then un-found was really the
# harness pointing at the wrong binary; interleaving is the cheaper half of not
# believing a number the first time.
#
# Everything else -- min-of-bucket, median of reps, a private HOME per run --
# is perf_curve.sh's, which this calls.
set -u
A=${1:?binA}
B=${2:?binB}
SC=${3:-idle}
REPS=${4:-5}
FR=${5:-800}
SIZES=${SIZES:-"0 100 500 1000 2000"}
here=$(cd "$(dirname "$0")" && pwd)

med() { sort -n | awk '{a[NR]=$1} END{print (NR%2)?a[(NR+1)/2]:(a[NR/2]+a[NR/2+1])/2}'; }
one() { BIN=$1 SIZES=$2 "$here/perf_curve.sh" "$SC" 1 "$FR" | tail -1 | awk '{print $2}'; }

echo "# $SC, interleaved, min-of-bucket over $FR frames, median of $REPS"
echo "#   A = $A"
echo "#   B = $B"
printf '%9s %10s %10s %8s\n' sessions A B ratio
for n in $SIZES; do
    as=(); bs=()
    for _ in $(seq "$REPS"); do
        as+=("$(one "$A" "$n")")
        bs+=("$(one "$B" "$n")")
    done
    am=$(printf '%s\n' "${as[@]}" | med)
    bm=$(printf '%s\n' "${bs[@]}" | med)
    printf '%9s %10s %10s %8s\n' "$n" "$am" "$bm" \
        "$(awk -v a="$am" -v b="$bm" 'BEGIN{printf "%.2fx", a/b}')"
done
