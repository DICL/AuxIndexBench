#!/usr/bin/env bash
# build_all.sh — build one benchmark binary per external index.
#
# Produces bench.<index> in the project root for each index whose
# build succeeds. External indexes need their own vendored source
# under third_party/<name>/ and often extra dev packages (libpmem,
# libtbb, libpmwcas, libgoogle-perftools, etc.); the script tolerates
# per-index build failures and moves on.
#
# Naming:
#   bench.btree     - built-ins only (no WITH_* flag). Also handles hash.
#   bench.fastfair  - built-ins + FAST&FAIR
#   bench.utree     - built-ins + uTree
#   bench.wbtree    - built-ins + wB+-Tree
#   bench.fptree    - built-ins + FPTree
#   bench.lbtree    - built-ins + LB+-Tree
#   bench.bztree    - built-ins + BzTree
#   bench.dptree    - built-ins + DPTree
#
# Tunables (env vars):
#   WHICH   space-separated subset of the tags above (default: all)
#   JOBS    parallel make jobs (default: nproc)
#   EXTRA   flags appended to *every* invocation, e.g. UTREE_NO_GPERFTOOLS=1
#
# Usage:
#   bash build_all.sh
#   WHICH="btree fastfair utree" bash build_all.sh
#   EXTRA="UTREE_NO_GPERFTOOLS=1" bash build_all.sh

set -uo pipefail
cd "$(dirname "$0")"

JOBS=${JOBS:-$(nproc 2>/dev/null || echo 2)}
EXTRA=${EXTRA:-}

# LB+-Tree needs Intel TSX/RTM for its transactions; on CPUs without the
# 'rtm' flag (microcode-disabled on most Cascade Lake) _xbegin() aborts
# forever and lbtree hangs right after pool init. Compile the no-RTM
# shim in that case (safe: the harness serialises lbtree globally).
# Override with AIB_LBTREE_RTM_FLAG="" or " LBTREE_NO_RTM=1".
if [[ -z "${AIB_LBTREE_RTM_FLAG+x}" ]]; then
    if grep -qw rtm /proc/cpuinfo 2>/dev/null; then
        AIB_LBTREE_RTM_FLAG=""
    else
        AIB_LBTREE_RTM_FLAG=" LBTREE_NO_RTM=1"
        printf "[build_all] no 'rtm' CPU flag -> lbtree built with LBTREE_NO_RTM=1\n" >&2
    fi
fi

# One entry per binary: "<tag>|<make flags>"
# The tag becomes bench.<tag>; the flags select which external index
# is compiled into that binary. `btree` is the built-ins-only binary.
declare -a VARIANTS=(
    "btree|"
    "fastfair|WITH_FASTFAIR=1"
    "wbtree|WITH_WBTREE=1"
    "utree|WITH_UTREE=1"
    "fptree|WITH_FPTREE=1"
    "lbtree|WITH_LBTREE=1${AIB_LBTREE_RTM_FLAG}"
    "bztree|WITH_BZTREE=1"
    "dptree|WITH_DPTREE=1"
)

WHICH_DEFAULT=""
for row in "${VARIANTS[@]}"; do
    WHICH_DEFAULT+=" ${row%%|*}"
done
WHICH=${WHICH:-$WHICH_DEFAULT}

log()  { printf "[build_all] %s\n" "$*" >&2; }

built=()
failed=()

for row in "${VARIANTS[@]}"; do
    tag="${row%%|*}"
    flags="${row#*|}"
    # Skip if not in WHICH.
    case " $WHICH " in
        *" $tag "*) ;;
        *) continue ;;
    esac

    log ""
    log "=== building bench.$tag (flags: $flags $EXTRA) ==="
    make clean >/dev/null 2>&1 || true

    # 'make' returns non-zero if the compile fails; we log and continue.
    if make -j"$JOBS" $flags $EXTRA 2>&1 | tail -n5 >&2 ; then
        if [[ -x ./bench ]]; then
            mv -f ./bench "bench.$tag"
            log "  ✔ bench.$tag ($(du -h "bench.$tag" | cut -f1))"
            built+=("$tag")
        else
            log "  ✘ make succeeded but ./bench missing"
            failed+=("$tag")
        fi
    else
        log "  ✘ make failed (see output above)"
        failed+=("$tag")
    fi
done

log ""
log "============================================================"
log "Built:  ${built[*]:-(none)}"
log "Failed: ${failed[*]:-(none)}"
log ""
if [[ ${#built[@]} -gt 0 ]]; then
    log "To run a sweep against these binaries:"
    log "  bash experiments/e1_polluter_sweep.sh   # auto-picks per index"
    log ""
    log "The sweep scripts pick bench.<index> automatically; you don't"
    log "need to set anything else."
fi

# Final make clean so the workspace doesn't ship stale objects.
make clean >/dev/null 2>&1 || true

# Exit code: 0 if at least one succeeded, 1 otherwise.
if [[ ${#built[@]} -gt 0 ]]; then exit 0; else exit 1; fi
