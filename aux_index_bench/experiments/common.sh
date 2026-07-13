# experiments/common.sh — shared helpers for all sweep scripts.
#
# Sourced by each e*_*.sh script; never executed directly.
#
# Per-index binary dispatch
# -------------------------
# `bash build_all.sh` (in the project root) produces one binary per
# external index:
#   bench.btree     bench.fastfair   bench.wbtree   bench.utree
#   bench.fptree    bench.lbtree     bench.bztree   bench.dptree
#
# Every binary knows about the built-in btree and hash (they're
# unconditional), plus its named external index. So `bench.fastfair`
# handles the indexes {btree, hash, fastfair}, `bench.utree` handles
# {btree, hash, utree}, etc. A sweep that iterates over multiple
# indexes will invoke different bench binaries per index automatically;
# `bench_for <idx>` returns the right one.
#
# Since the built-in indexes appear in every binary, we route them
# through `bench.btree` by default (the smallest binary — no external
# dependencies).
#
# Override for a single index is still possible:
#   export BENCH_FASTFAIR=/path/to/my/custom_bench
#   bash experiments/e1_polluter_sweep.sh

# Resolve paths from the script that sourced us.
AIB_ROOT="$(cd "$(dirname "${BASH_SOURCE[1]}")/.." && pwd)"

# Environment-overridable defaults that all scripts share.
KEYS=${KEYS:-100000000}
QUERIES=${QUERIES:-100000}
REPEATS=${REPEATS:-3}
UNIVERSE=${UNIVERSE:-$((512 * 1024 * 1024))}
SEED=${SEED:-0xC0FFEE}

# Default index list. Scripts can override before sourcing or pass via env.
INDEXES=${INDEXES:-"btree hash fastfair wbtree"}

# Default Poisson scan workload size.
POISSON_QUERIES=${POISSON_QUERIES:-100000}
# 0 = auto: bench sizes the queue to hold the whole stream, so the
# producer never blocks on a full queue (which would silently clamp the
# offered rate). Set a number to study bounded-queue behaviour on purpose.
QUEUE_CAPACITY=${QUEUE_CAPACITY:-0}

aib_log() {
    printf "[%s] %s\n" "${SCRIPT_NAME:-aib}" "$*" >&2
}

aib_die() {
    aib_log "FATAL: $*"
    exit 1
}

# Map an index name to the binary that supports it.
#   1. If BENCH_<UPPER>=... is set, use that.
#   2. External indexes (fastfair, utree, wbtree, ...) → bench.<index>.
#   3. Built-ins (btree, hash) → bench.btree.
# Returns the absolute path.
bench_for() {
    local idx="$1"
    local upper
    upper=$(printf '%s' "$idx" | tr '[:lower:]-' '[:upper:]_')
    local varname="BENCH_${upper}"
    local override="${!varname:-}"
    if [[ -n "$override" ]]; then
        printf '%s' "$override"
        return 0
    fi
    case "$idx" in
        btree|hash)
            printf '%s' "$AIB_ROOT/bench.btree" ;;
        *)
            printf '%s' "$AIB_ROOT/bench.$idx" ;;
    esac
}

# Verify each index has a binary that (a) exists and (b) actually
# supports that index name (was built with the right WITH_<NAME> flag).
# The tiny smoke run costs a few ms and prevents a multi-hour sweep
# from silently producing nothing.
aib_check_bench() {
    # Pre-flight smoke test of every index in $INDEXES. Note that this
    # runs a tiny (--keys 100) invocation per index, which still pays
    # any one-off init cost the adapter has — e.g. lbtree creates its
    # full NVM/DRAM pools here. Set AIB_SKIP_CHECK=1 to bypass when the
    # binaries are known-good and init is expensive.
    if [[ "${AIB_SKIP_CHECK:-0}" == "1" ]]; then
        aib_log "pre-flight check skipped (AIB_SKIP_CHECK=1)"
        return 0
    fi
    local missing=0
    for idx in $INDEXES; do
        local b
        b=$(bench_for "$idx")
        if [[ ! -x "$b" ]]; then
            aib_log "no executable binary for '$idx' (expected $b)"
            missing=$((missing+1))
            continue
        fi
        aib_log "pre-flight check: $idx ($(basename "$b")) ..."
        local tmp_err
        tmp_err=$(mktemp)
        if ! "$b" --index "$idx" --keys 100 --queries 10 --repeats 1 \
                  --no-human --csv >/dev/null 2>"$tmp_err" ; then
            aib_log "binary '$b' does not support index '$idx':"
            sed 's/^/  | /' "$tmp_err" >&2
            missing=$((missing+1))
        fi
        rm -f "$tmp_err"
    done
    if (( missing > 0 )); then
        aib_log ""
        aib_log "Hint: run 'bash build_all.sh' in the project root to build"
        aib_log "the per-index binaries, then re-run this sweep."
        exit 1
    fi
}

# Initialize the CSV by writing one header line (with `index` prepended)
# pulled from a single throw-away invocation of bench.btree.
aib_init_csv() {
    local out="$1"
    mkdir -p "$(dirname "$out")"
    if [[ -s "$out" ]]; then
        aib_log "appending to existing $out"
        return 0
    fi
    aib_log "initialising $out"
    # Read the schema from the binary of the first index in $INDEXES
    # (all variants share the same CSV layout).
    local first_idx hdr bin
    first_idx=$(echo $INDEXES | awk '{print $1}')
    bin=$(bench_for "$first_idx")
    hdr=$("$bin" --index "$first_idx" --keys 1000 --queries 100 --repeats 1 \
              --no-human --csv 2>/dev/null | head -n1) \
        || aib_die "could not read CSV header from $bin"
    echo "index,$hdr" > "$out"
}

# Run one bench invocation. Picks the right binary via bench_for,
# captures its single CSV data line, prefixes with the index name, and
# appends to $OUT. Failures (non-zero exit, unexpected output) are
# logged with the full stderr.
#
# Usage: aib_run <index_name> [bench flags...]
aib_run() {
    local idx="$1"; shift
    local bin
    bin=$(bench_for "$idx")
    local hash_args=""
    if [[ "$idx" == "hash" ]]; then
        local nb=$(( KEYS * 2 ))
        local v=1
        while (( v < nb )); do v=$((v*2)); done
        hash_args="--hash-buckets $v --hash-sigma 0.4"
    fi
    aib_log "run idx=$idx (binary=$(basename "$bin")) $*"
    local tmp_err
    tmp_err=$(mktemp)
    local line
    line=$("$bin" --no-human --csv --seed "$SEED" \
            --index "$idx" $hash_args "$@" 2>"$tmp_err" | tail -n1)
    local rc=$?
    if (( rc != 0 )); then
        aib_log "  bench exited $rc; stderr:"
        sed 's/^/  | /' "$tmp_err" >&2
        rm -f "$tmp_err"
        return 1
    fi
    case "$line" in
        none,*|polluter,*|object,*|storage,*) ;;
        *) aib_log "  unexpected output line: '$line'; stderr:"
           sed 's/^/  | /' "$tmp_err" >&2
           rm -f "$tmp_err"
           return 1 ;;
    esac
    rm -f "$tmp_err"
    echo "$idx,$line" >> "$OUT"
}
