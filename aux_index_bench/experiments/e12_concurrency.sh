#!/usr/bin/env bash
# experiments/e12_concurrency.sh
#
# Question: how does closed-loop throughput scale with concurrent
# workers under a mixed read/write workload, and how does cache
# pollution interact with that scaling?
#
# Fixed op mix: search 60%, update 10%, insert 30% (no delete).
# NOTE: "insert" is an upsert of a key inside the loaded range — it
# dirties node cache lines and exercises the write path, but does not
# grow the tree. See EXTERNAL_INDEXES.md.
#
# Locking semantics (bench v16):
#   * indexes that declare thread_safe() (e.g. fastfair) run lock-free —
#     their own concurrency control (per-page latches, optimistic reads)
#     is what's being measured;
#   * everything else is serialised behind a global RW-lock, so their
#     curves show the cost of *not* having internal concurrency control.
# Both are meaningful; just don't compare them as if they measured the
# same mechanism.
#
# What to look for:
#   * read-heavy scaling limited by LLC/coherence vs lock serialisation
#   * pollution shifting the knee of the scaling curve
#   * per-op tails (search vs update) diverging as workers grow
#
# Memory: each worker gets its own polluter universe
# (UNIVERSE + UNIVERSE/16 for the line permutation). With UNIVERSE=256MB
# and 16 workers that is ~4.3 GB — budget accordingly.

set -euo pipefail
SCRIPT_NAME="e12_concurrency"
source "$(dirname "$0")/common.sh"
aib_check_bench

OUT=${OUT:-$AIB_ROOT/results/e12_concurrency.csv}
WORKER_COUNTS=${WORKER_COUNTS:-"1 2 4 8 16"}
POLLUTER_BYTES=${POLLUTER_BYTES:-"0 65536 1048576"}
MIX=${MIX:-"s=0.6,u=0.1,i=0.3"}
E12_UNIVERSE=${E12_UNIVERSE:-$((256 * 1024 * 1024))}

aib_init_csv "$OUT"
aib_log "INDEXES='$INDEXES'  MIX=$MIX"
aib_log "WORKER_COUNTS='$WORKER_COUNTS'  POLLUTER_BYTES='$POLLUTER_BYTES'"
aib_log "KEYS=$KEYS  QUERIES=$QUERIES  REPEATS=$REPEATS  UNIVERSE=$E12_UNIVERSE"

ncores=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
max_w=$(echo "$WORKER_COUNTS" | tr ' ' '\n' | sort -n | tail -1)
if (( ncores < max_w )); then
    aib_log "WARNING: $ncores online cores < $max_w max workers;"
    aib_log "         oversubscribed points measure scheduling, not scaling."
fi

for idx in $INDEXES; do
    for W in $WORKER_COUNTS; do
        for B in $POLLUTER_BYTES; do
            if (( B == 0 )); then
                aib_run "$idx" \
                    --keys "$KEYS" --queries "$QUERIES" \
                    --workload none \
                    --dist uniform --arrival batch \
                    --workers "$W" --op-mix "$MIX" \
                    --repeats "$REPEATS" || true
            else
                aib_run "$idx" \
                    --keys "$KEYS" --queries "$QUERIES" \
                    --workload polluter --bytes "$B" --universe "$E12_UNIVERSE" \
                    --dist uniform --arrival batch \
                    --workers "$W" --op-mix "$MIX" \
                    --repeats "$REPEATS" || true
            fi
        done
    done
done

aib_log "done -> $OUT"
