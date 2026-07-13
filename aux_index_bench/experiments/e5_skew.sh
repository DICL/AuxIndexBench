#!/usr/bin/env bash
# experiments/e5_skew.sh
#
# Question: does access skew help cache-conscious layouts even when
# surrounding application activity disturbs the cache?
#
# Method: sweep Zipf θ. Two passes: no polluter vs 64 KB polluter.

set -euo pipefail
SCRIPT_NAME="e5_skew"
source "$(dirname "$0")/common.sh"
aib_check_bench

OUT=${OUT:-$AIB_ROOT/results/e5_skew.csv}
THETAS=${THETAS:-"0.0 0.5 0.7 0.9 0.99 1.2 1.5"}
POLLUTE_B=${POLLUTE_B:-65536}

aib_init_csv "$OUT"
aib_log "INDEXES='$INDEXES'  KEYS=$KEYS  QUERIES=$QUERIES  REPEATS=$REPEATS"
aib_log "THETAS='$THETAS'  POLLUTE_B=$POLLUTE_B"

for idx in $INDEXES; do
    for T in $THETAS; do
        aib_run "$idx" \
            --keys "$KEYS" --queries "$QUERIES" \
            --workload none \
            --dist zipf --theta "$T" \
            --arrival batch --repeats "$REPEATS" || true
    done
    if [[ "$POLLUTE_B" != "0" ]]; then
        for T in $THETAS; do
            aib_run "$idx" \
                --keys "$KEYS" --queries "$QUERIES" \
                --workload polluter --bytes "$POLLUTE_B" --universe "$UNIVERSE" \
                --dist zipf --theta "$T" \
                --arrival batch --repeats "$REPEATS" || true
        done
    fi
done

aib_log "done -> $OUT"
