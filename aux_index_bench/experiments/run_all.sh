#!/usr/bin/env bash
# experiments/run_all.sh — run every sweep in order.
#
# Assumes per-index binaries (bench.btree, bench.fastfair, ...) have
# already been built. Run `bash build_all.sh` in the project root
# first if you haven't.

set -uo pipefail
cd "$(dirname "$0")/.."

# Sanity: at least bench.btree must exist for the built-ins.
if [[ ! -x ./bench.btree ]]; then
    echo "[run_all] bench.btree not built; running 'bash build_all.sh' first" >&2
    bash build_all.sh || { echo "[run_all] build failed"; exit 1; }
fi

declare -a SWEEPS=(
    experiments/e1_polluter_sweep.sh
    experiments/e1b_polluter_alone.sh
    experiments/e2_keycount_sweep.sh
    experiments/e3_arrival_rate.sh
    experiments/e4_burstiness.sh
    experiments/e5_skew.sh
    experiments/e6_opmix.sh
    experiments/e7_clients.sh
    experiments/e8_traffic_shape.sh
    experiments/e9_mix_x_pollute.sh
    experiments/e10_object_storage.sh
    experiments/e11_hash_sigma.sh
    experiments/e12_concurrency.sh
    experiments/e13_coherence.sh
)

for s in "${SWEEPS[@]}"; do
    echo "" >&2
    echo "============================================================" >&2
    echo "  $s" >&2
    echo "============================================================" >&2
    bash "$s" || echo "[run_all] $s exited non-zero; continuing" >&2
done

echo "" >&2
echo "[run_all] all sweeps complete. CSVs in results/." >&2
