#!/usr/bin/env bash
# experiments/e6_opmix.sh
#
# Question: how do per-op tails move as the workload shifts from
# pure-read to write-heavy?
#
# Method: batch, fixed 256 KB polluter, sweep several labelled op mixes.

set -euo pipefail
SCRIPT_NAME="e6_opmix"
source "$(dirname "$0")/common.sh"
aib_check_bench

OUT=${OUT:-$AIB_ROOT/results/e6_opmix.csv}
POLLUTE_B=${POLLUTE_B:-262144}
SCAN_LEN=${SCAN_LEN:-32}
MIXES=${MIXES:-"s=1.0|s=0.95,u=0.05|s=0.8,u=0.2|s=0.5,u=0.5|s=0.5,u=0.3,i=0.1,d=0.1|s=0.5,u=0.2,i=0.1,d=0.1,sc=0.1|s=0.1,u=0.4,i=0.2,d=0.2,sc=0.1"}

aib_init_csv "$OUT"
aib_log "INDEXES='$INDEXES'  KEYS=$KEYS  QUERIES=$QUERIES  REPEATS=$REPEATS"
aib_log "POLLUTE_B=$POLLUTE_B  SCAN_LEN=$SCAN_LEN"

IFS='|' read -r -a MIX_ARR <<< "$MIXES"

for idx in $INDEXES; do
    for mix in "${MIX_ARR[@]}"; do
        aib_run "$idx" \
            --keys "$KEYS" --queries "$QUERIES" \
            --workload polluter --bytes "$POLLUTE_B" --universe "$UNIVERSE" \
            --dist uniform \
            --arrival batch \
            --op-mix "$mix" --scan-len "$SCAN_LEN" \
            --repeats "$REPEATS" || true
    done
done

aib_log "done -> $OUT"
