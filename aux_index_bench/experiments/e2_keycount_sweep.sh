#!/usr/bin/env bash
# experiments/e2_keycount_sweep.sh
#
# Question: how does each index scale as it grows through L1 → L2 → LLC
# → DRAM boundaries, and does a moderate polluter interact with that?
#
# Method: sweep --keys across several orders of magnitude. Two passes:
#   A: no polluter
#   B: 64 KB polluter

set -euo pipefail
SCRIPT_NAME="e2_keycount"
source "$(dirname "$0")/common.sh"
aib_check_bench

OUT=${OUT:-$AIB_ROOT/results/e2_keycount.csv}
KEY_COUNTS=${KEY_COUNTS:-"10000 100000 1000000 10000000"}
POLLUTE_B=${POLLUTE_B:-65536}

queries_for() {
    local k=$1
    local q=$(( k > 2000000 ? 2000000 : k ))
    echo "$q"
}

aib_init_csv "$OUT"
aib_log "INDEXES='$INDEXES'  REPEATS=$REPEATS"
aib_log "KEY_COUNTS='$KEY_COUNTS'  POLLUTE_B=$POLLUTE_B"

for idx in $INDEXES; do
    for K in $KEY_COUNTS; do
        Q=$(queries_for "$K")
        KEYS=$K aib_run "$idx" \
            --keys "$K" --queries "$Q" \
            --workload none --dist uniform --arrival batch \
            --repeats "$REPEATS" || true
    done
    if [[ "$POLLUTE_B" != "0" ]]; then
        for K in $KEY_COUNTS; do
            Q=$(queries_for "$K")
            KEYS=$K aib_run "$idx" \
                --keys "$K" --queries "$Q" \
                --workload polluter --bytes "$POLLUTE_B" --universe "$UNIVERSE" \
                --dist uniform --arrival batch \
                --repeats "$REPEATS" || true
        done
    fi
done

aib_log "done -> $OUT"
