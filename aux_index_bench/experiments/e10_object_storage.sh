#!/usr/bin/env bash
# experiments/e10_object_storage.sh
#
# Question: do "realistic" post-lookup workloads (object access,
# storage-stack metadata chain) track the synthetic polluter, or do
# they show their own shape effects?
#
# Method: for each mode, sweep per-call byte size.

set -euo pipefail
SCRIPT_NAME="e10_object_storage"
source "$(dirname "$0")/common.sh"
aib_check_bench

OUT=${OUT:-$AIB_ROOT/results/e10_object_storage.csv}
BYTES_LIST=${BYTES_LIST:-"256 1024 4096 16384 65536 262144"}

aib_init_csv "$OUT"
aib_log "INDEXES='$INDEXES'  KEYS=$KEYS  QUERIES=$QUERIES  REPEATS=$REPEATS"
aib_log "BYTES_LIST='$BYTES_LIST'"

for idx in $INDEXES; do
    for mode in object storage; do
        for B in $BYTES_LIST; do
            aib_run "$idx" \
                --keys "$KEYS" --queries "$QUERIES" \
                --workload "$mode" --bytes "$B" --universe "$UNIVERSE" \
                --dist uniform --arrival batch \
                --repeats "$REPEATS" || true
        done
    done
done

aib_log "done -> $OUT"
