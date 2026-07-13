#!/usr/bin/env bash
# experiments/e9_mix_x_pollute.sh
#
# Question: do the read tail and write tail diverge as cache pressure
# grows?
#
# Method: fixed 60/40 search/update mix, sweep polluter size.

set -euo pipefail
SCRIPT_NAME="e9_mix_x_pollute"
source "$(dirname "$0")/common.sh"
aib_check_bench

OUT=${OUT:-$AIB_ROOT/results/e9_mix_x_pollute.csv}
POLLUTER_BYTES=${POLLUTER_BYTES:-"0 4096 32768 262144 2097152 16777216"}
MIX=${MIX:-"s=0.6,u=0.4"}

aib_init_csv "$OUT"
aib_log "INDEXES='$INDEXES'  MIX=$MIX"
aib_log "POLLUTER_BYTES='$POLLUTER_BYTES'"

for idx in $INDEXES; do
    for B in $POLLUTER_BYTES; do
        if (( B == 0 )); then
            aib_run "$idx" \
                --keys "$KEYS" --queries "$QUERIES" \
                --workload none \
                --dist uniform --arrival batch \
                --op-mix "$MIX" \
                --repeats "$REPEATS" || true
        else
            aib_run "$idx" \
                --keys "$KEYS" --queries "$QUERIES" \
                --workload polluter --bytes "$B" --universe "$UNIVERSE" \
                --dist uniform --arrival batch \
                --op-mix "$MIX" \
                --repeats "$REPEATS" || true
        fi
    done
done

aib_log "done -> $OUT"
