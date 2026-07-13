#!/usr/bin/env bash
# experiments/e1b_polluter_alone.sh
#
# Question: how much of E1's per-op time is the polluter, versus the
# index lookup? Same axis as e1 but --no-lookup so the index op is
# skipped and only the workload cost is timed.
#
# lookup_only(B) ≈ E1.svc_mean(B) - E1B.svc_mean(B)

set -euo pipefail
SCRIPT_NAME="e1b_polluter_alone"
source "$(dirname "$0")/common.sh"
aib_check_bench

OUT=${OUT:-$AIB_ROOT/results/e1b_polluter_alone.csv}
POLLUTER_BYTES=${POLLUTER_BYTES:-"0 1024 4096 16384 65536 262144 1048576 4194304 16777216 67108864"}

aib_init_csv "$OUT"
aib_log "INDEXES='$INDEXES'  KEYS=$KEYS  QUERIES=$QUERIES  REPEATS=$REPEATS"
aib_log "POLLUTER_BYTES='$POLLUTER_BYTES' (lookup disabled)"

for idx in $INDEXES; do
    for B in $POLLUTER_BYTES; do
        if (( B == 0 )); then
            aib_run "$idx" \
                --keys "$KEYS" --queries "$QUERIES" \
                --workload none --no-lookup \
                --dist uniform --arrival batch \
                --repeats "$REPEATS" || true
        else
            aib_run "$idx" \
                --keys "$KEYS" --queries "$QUERIES" \
                --workload polluter --bytes "$B" --universe "$UNIVERSE" \
                --no-lookup --dist uniform --arrival batch \
                --repeats "$REPEATS" || true
        fi
    done
done

aib_log "done -> $OUT"
