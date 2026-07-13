#!/usr/bin/env bash
# experiments/e3_arrival_rate.sh
#
# Question: how do service time and queueing delay separate as offered
# load approaches saturation?
#
# Method: open-loop Poisson, fixed cv²=1, fixed N clients, moderate
# per-op object access. Sweep --rate.

set -euo pipefail
SCRIPT_NAME="e3_arrival_rate"
source "$(dirname "$0")/common.sh"
aib_check_bench

OUT=${OUT:-$AIB_ROOT/results/e3_arrival_rate.csv}
RATES=${RATES:-"200000 500000 1000000 2000000 3000000 4000000 5000000 7000000"}
CLIENTS=${CLIENTS:-4}
OBJECT_B=${OBJECT_B:-4096}

aib_init_csv "$OUT"
aib_log "INDEXES='$INDEXES'  CLIENTS=$CLIENTS  OBJECT_B=$OBJECT_B"
aib_log "RATES='$RATES'  POISSON_QUERIES=$POISSON_QUERIES"

ncores=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
required=$((CLIENTS + 1))
if (( ncores < required )); then
    aib_log "WARNING: only $ncores online cores; need >= $required for clean Poisson runs."
    aib_log "         queue-delay numbers will be scheduler-slice-dominated."
fi

for idx in $INDEXES; do
    for R in $RATES; do
        aib_run "$idx" \
            --keys "$KEYS" --queries "$POISSON_QUERIES" \
            --workload object --bytes "$OBJECT_B" \
            --dist uniform --arrival poisson \
            --rate "$R" --cv2 1.0 \
            --clients "$CLIENTS" --queue-capacity "$QUEUE_CAPACITY" \
            --repeats "$REPEATS" || true
    done
done

aib_log "done -> $OUT"
