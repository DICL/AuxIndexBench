#!/usr/bin/env bash
# experiments/e7_clients.sh
#
# Question: does throughput scale linearly with client thread count?
# Where does each index plateau?
#
# Method: open-loop Poisson, fixed total offered rate, vary --clients.

set -euo pipefail
SCRIPT_NAME="e7_clients"
source "$(dirname "$0")/common.sh"
aib_check_bench

OUT=${OUT:-$AIB_ROOT/results/e7_clients.csv}
CLIENT_COUNTS=${CLIENT_COUNTS:-"1 2 4 8 16"}
RATE_PER_RUN=${RATE_PER_RUN:-2000000}
OBJECT_B=${OBJECT_B:-4096}

aib_init_csv "$OUT"
aib_log "INDEXES='$INDEXES'  RATE_PER_RUN=$RATE_PER_RUN  OBJECT_B=$OBJECT_B"
aib_log "CLIENT_COUNTS='$CLIENT_COUNTS'  POISSON_QUERIES=$POISSON_QUERIES"

ncores=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
max_clients=$(echo "$CLIENT_COUNTS" | tr ' ' '\n' | sort -n | tail -1)
need=$((max_clients + 1))
if (( ncores < need )); then
    aib_log "WARNING: $ncores online cores < $need needed for the largest client count;"
    aib_log "         run on a multi-core host to obtain honest queue numbers."
fi

for idx in $INDEXES; do
    for C in $CLIENT_COUNTS; do
        aib_run "$idx" \
            --keys "$KEYS" --queries "$POISSON_QUERIES" \
            --workload object --bytes "$OBJECT_B" \
            --dist uniform --arrival poisson \
            --rate "$RATE_PER_RUN" --cv2 1.0 \
            --clients "$C" --queue-capacity "$QUEUE_CAPACITY" \
            --repeats "$REPEATS" || true
    done
done

aib_log "done -> $OUT"
