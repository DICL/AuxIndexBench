#!/usr/bin/env bash
# experiments/e11_hash_sigma.sh
#
# Question: how does bit-collision concentration σ affect hash-table
# tail latency at fixed table size?
#
# Method: fixed keys, sweep σ. Two passes: no polluter vs 64 KB polluter.

set -euo pipefail
SCRIPT_NAME="e11_hash_sigma"
source "$(dirname "$0")/common.sh"
aib_check_bench

OUT=${OUT:-$AIB_ROOT/results/e11_hash_sigma.csv}
SIGMAS=${SIGMAS:-"0.2 0.4 0.6 0.8 1.0 1.5 2.0 3.0"}
POLLUTE_LIST=${POLLUTE_LIST:-"0 65536"}

aib_init_csv "$OUT"
aib_log "KEYS=$KEYS  QUERIES=$QUERIES  REPEATS=$REPEATS"
aib_log "SIGMAS='$SIGMAS'  POLLUTE_LIST='$POLLUTE_LIST'"

nb=$(( KEYS * 2 ))
v=1
while (( v < nb )); do v=$((v*2)); done
HASH_BUCKETS=$v
aib_log "HASH_BUCKETS=$HASH_BUCKETS"

# hash-sigma is a per-invocation flag, so bypass the generic aib_run
# hash_args logic and inline the invocation.
run_hash() {
    local sigma="$1"; shift
    local bin
    bin=$(bench_for "hash")
    aib_log "run sigma=$sigma (binary=$(basename "$bin")) $*"
    local tmp_err
    tmp_err=$(mktemp)
    local line
    line=$("$bin" --no-human --csv --seed "$SEED" \
            --index hash \
            --hash-buckets "$HASH_BUCKETS" \
            --hash-sigma "$sigma" \
            "$@" 2>"$tmp_err" | tail -n1)
    local rc=$?
    if (( rc != 0 )); then
        aib_log "  bench exited $rc; stderr:"
        sed 's/^/  | /' "$tmp_err" >&2
        rm -f "$tmp_err"
        return 1
    fi
    case "$line" in
        none,*|polluter,*|object,*|storage,*) ;;
        *) rm -f "$tmp_err"; return 1 ;;
    esac
    rm -f "$tmp_err"
    echo "hash,$line" >> "$OUT"
}

for B in $POLLUTE_LIST; do
    for sigma in $SIGMAS; do
        if (( B == 0 )); then
            run_hash "$sigma" \
                --keys "$KEYS" --queries "$QUERIES" \
                --workload none \
                --dist uniform --arrival batch \
                --repeats "$REPEATS" || true
        else
            run_hash "$sigma" \
                --keys "$KEYS" --queries "$QUERIES" \
                --workload polluter --bytes "$B" --universe "$UNIVERSE" \
                --dist uniform --arrival batch \
                --repeats "$REPEATS" || true
        fi
    done
done

aib_log "done -> $OUT"
