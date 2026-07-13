#!/usr/bin/env bash
# experiments/e1_polluter_sweep.sh
#
# Question: how much does index-only lookup latency degrade as the
# post-lookup polluter grows from zero to LLC-sized?
#
# Method: for each index, batch (closed-loop) read-only workload, sweep
# --bytes from 0 to 64 MiB. Row 0 (--workload none) is the "index-only"
# baseline; larger rows are the auxiliary-index regime.

set -euo pipefail
SCRIPT_NAME="e1_polluter"
source "$(dirname "$0")/common.sh"
aib_check_bench

OUT=${OUT:-$AIB_ROOT/results/e1_polluter.csv}
POLLUTER_BYTES=${POLLUTER_BYTES:-"0 1024 4096 16384 65536 262144 1048576 4194304 16777216 67108864"}

aib_init_csv "$OUT"
aib_log "INDEXES='$INDEXES'  KEYS=$KEYS  QUERIES=$QUERIES  REPEATS=$REPEATS"
aib_log "POLLUTER_BYTES='$POLLUTER_BYTES'  UNIVERSE=$UNIVERSE"

for idx in $INDEXES; do
    for B in $POLLUTER_BYTES; do
        if (( B == 0 )); then
            aib_run "$idx" \
                --keys "$KEYS" --queries "$QUERIES" \
                --workload none \
                --dist uniform --arrival batch \
                --repeats "$REPEATS" || true
        else
            aib_run "$idx" \
                --keys "$KEYS" --queries "$QUERIES" \
                --workload polluter --bytes "$B" --universe "$UNIVERSE" \
                --dist uniform --arrival batch \
                --repeats "$REPEATS" || true
        fi
    done
done

aib_log "done -> $OUT"
