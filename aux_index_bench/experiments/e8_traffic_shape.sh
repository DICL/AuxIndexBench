#!/usr/bin/env bash
# experiments/e8_traffic_shape.sh
#
# Question: at the same mean rate, how much extra tail latency do
# realistic traffic shapes (sine, level, burst, mixed) introduce over
# flat Poisson?
#
# Method: hold mean rate, client count, and per-op work fixed. Vary
# only the λ(t) shape across five scenarios.
#
# NOTE: since traffic_model.hpp v12, each shape is *normalised* so its
# time-averaged λ equals base_rate. Earlier data suffered from a
# confounder where different shapes silently offered different loads.

set -euo pipefail
SCRIPT_NAME="e8_traffic_shape"
source "$(dirname "$0")/common.sh"
aib_check_bench

OUT=${OUT:-$AIB_ROOT/results/e8_traffic_shape.csv}
BASE_RATE=${BASE_RATE:-1500000}
CLIENTS=${CLIENTS:-4}
OBJECT_B=${OBJECT_B:-4096}

aib_init_csv "$OUT"
aib_log "INDEXES='$INDEXES'  BASE_RATE=$BASE_RATE  CLIENTS=$CLIENTS"

declare -A SCENARIOS=(
    [01_flat]=""
    [02_sin]="--sin-amp 0.5 --sin-period 0.5"
    [03_level]="--level-period 0.05 --level-lo 0.5 --level-hi 2.0"
    [04_burst]="--burst-prob 0.05 --burst-tick 0.05 --burst-dur-mean 0.02 --burst-pareto 1.2"
    [05_mixed]="--sin-amp 0.3 --sin-period 1.0 --burst-prob 0.05 --burst-tick 0.05 --burst-pareto 1.5"
)
SCENARIO_ORDER=(01_flat 02_sin 03_level 04_burst 05_mixed)

for idx in $INDEXES; do
    for label in "${SCENARIO_ORDER[@]}"; do
        extra="${SCENARIOS[$label]}"
        aib_log "  scenario $label: $extra"
        aib_run "$idx" \
            --keys "$KEYS" --queries "$POISSON_QUERIES" \
            --workload object --bytes "$OBJECT_B" \
            --dist uniform --arrival poisson \
            --rate "$BASE_RATE" --cv2 1.0 \
            --clients "$CLIENTS" --queue-capacity "$QUEUE_CAPACITY" \
            --repeats "$REPEATS" $extra || true
    done
done

aib_log "done -> $OUT"
