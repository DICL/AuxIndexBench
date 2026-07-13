#!/usr/bin/env bash
# experiments/e13_coherence.sh
#
# Question: how much throughput do concurrent readers lose to cache
# coherence traffic when a small fraction of writes invalidates the
# lines they have cached?
#
# Design — isolating coherence from everything else:
#   * workers FIXED (default 16), polluter OFF → no bandwidth confound,
#     no lock-occupancy dilution (cf. E12).
#   * only indexes with internal concurrency control (fastfair, fptree,
#     utree). Global-lock indexes are excluded on purpose: their lock
#     engages the moment write_fraction > 0, and that discontinuity
#     would masquerade as a coherence cliff.
#   * write fraction swept FINELY near zero: 0 … 0.4. Amdahl's law says
#     a 0.1% serial fraction cannot dent throughput; if 0.1% writes DO
#     dent it, the mechanism is invalidation of hot cached lines, i.e.
#     coherence.
#   * index sized to be cache-resident (default 500K keys) and accesses
#     Zipf-skewed (θ=0.99) so readers and writers share hot leaf lines.
#     With a DRAM-sized uniform keyspace (like E12's 10M) writes land on
#     cold leaves nobody else has cached, and coherence is invisible —
#     that contrast is itself worth showing (set DIST=uniform to see it).
#
# Reading the result:
#   * throughput(wf) / throughput(0) per index = coherence sensitivity.
#   * the drop between wf=0 and wf≈0.01 is almost pure invalidation
#     cost; beyond ~0.1 the write path itself (latches, clflush) mixes in.

set -euo pipefail
SCRIPT_NAME="e13_coherence"
source "$(dirname "$0")/common.sh"

# Override common defaults: cache-resident index, skewed access.
KEYS=${E13_KEYS:-500000}
QUERIES=${E13_QUERIES:-2000000}
DIST=${DIST:-zipf}
THETA=${THETA:-0.99}
WORKERS=${WORKERS:-16}
WF_LIST=${WF_LIST:-"0 0.001 0.005 0.01 0.02 0.05 0.1 0.2 0.4"}
INDEXES=${E13_INDEXES:-"fastfair fptree utree"}

aib_check_bench

OUT=${OUT:-$AIB_ROOT/results/e13_coherence.csv}
aib_init_csv "$OUT"
aib_log "INDEXES='$INDEXES'  WORKERS=$WORKERS  DIST=$DIST THETA=$THETA"
aib_log "KEYS=$KEYS  QUERIES=$QUERIES  WF_LIST='$WF_LIST'"

ncores=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
if (( ncores < WORKERS )); then
    aib_log "WARNING: $ncores cores < $WORKERS workers; coherence effects"
    aib_log "         are masked when threads time-share a core."
fi

# write fraction wf is split 50/50 between update and insert(=upsert),
# mirroring E12's flavor; search gets the rest.
mix_for() {
    local wf="$1"
    python3 - "$wf" <<'EOF'
import sys
wf = float(sys.argv[1])
if wf == 0:
    print("s=1.0")
else:
    u = wf / 2
    i = wf / 2
    s = 1.0 - wf
    print(f"s={s:.4f},u={u:.4f},i={i:.4f}")
EOF
}

for idx in $INDEXES; do
    for wf in $WF_LIST; do
        MIX=$(mix_for "$wf")
        aib_run "$idx" \
            --keys "$KEYS" --queries "$QUERIES" \
            --workload none \
            --dist "$DIST" --theta "$THETA" --arrival batch \
            --workers "$WORKERS" --op-mix "$MIX" \
            --repeats "$REPEATS" || true
    done
done

aib_log "done -> $OUT"
aib_log "tip: rerun with DIST=uniform KEYS=10000000 to see coherence vanish"
aib_log "     when writes land on cold, unshared leaves."
