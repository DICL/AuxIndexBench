#!/usr/bin/env bash
# experiments/e4_burstiness.sh
#
# Question: at the same mean arrival rate, how much extra tail latency
# does burstiness introduce over Poisson?
#
# Method: hold mean rate, vary inter-arrival cv².

set -euo pipefail
SCRIPT_NAME="e4_burstiness"
source "$(dirname "$0")/common.sh"
aib_check_bench

OUT=${OUT:-$AIB_ROOT/results/e4_burstiness.csv}
CV2_LIST=${CV2_LIST:-"0.25 0.5 1.0 2.0 4.0 8.0"}
RATE=${RATE:-1500000}
CLIENTS=${CLIENTS:-4}
OBJECT_B=${OBJECT_B:-4096}

aib_init_csv "$OUT"
aib_log "INDEXES='$INDEXES'  RATE=$RATE  CLIENTS=$CLIENTS  OBJECT_B=$OBJECT_B"
aib_log "CV2_LIST='$CV2_LIST'  POISSON_QUERIES=$POISSON_QUERIES"

for idx in $INDEXES; do
    for cv2 in $CV2_LIST; do
        aib_run "$idx" \
            --keys "$KEYS" --queries "$POISSON_QUERIES" \
            --workload object --bytes "$OBJECT_B" \
            --dist uniform --arrival poisson \
            --rate "$RATE" --cv2 "$cv2" \
            --clients "$CLIENTS" --queue-capacity "$QUEUE_CAPACITY" \
            --repeats "$REPEATS" || true
    done
done

aib_log "done -> $OUT"
