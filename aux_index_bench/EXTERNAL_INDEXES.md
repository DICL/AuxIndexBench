# External Indexes — Wiring Guide

This addendum covers the nine external PMEM-resident B+tree variants the
benchmark can drive in addition to its built-in B+tree and hash table:

```
wB+Tree     FAST&FAIR    FPTree      BzTree      LB+Tree
uTree       Circ-Tree    DPTree      NBTree
```

## Status — please read first

I (the benchmark author) **could not fetch these repositories from
inside the build environment**: `github.com` was not on the network
allowlist when this file was written. Every URL in
`third_party/fetch_indexes.sh` and in each adapter's header is therefore
a **best-effort recollection** that you must verify before relying on.
If you find a URL is wrong, fix it in `fetch_indexes.sh` and the
relevant `adapters/<name>_adapter.hpp` header.

Some of these indexes also do not have a public reference
implementation at all (NBTree in particular — the upstream URL field is
empty in `fetch_indexes.sh`). For those, you will either need to find a
re-implementation, write one, or drop them from your evaluation.

## What's built and what isn't

The default `make` build only includes the in-tree B+tree and hash
table. External indexes are opt-in via per-index build flags so they do
not block compilation when you don't have their source.

| Index    | Build flag           | Adapter                           |
|----------|----------------------|-----------------------------------|
| FAST&FAIR| `WITH_FASTFAIR=1`    | `adapters/fastfair_adapter.hpp`   |
| wB+Tree  | `WITH_WBTREE=1`      | `adapters/wbtree_adapter.hpp`     |
| FPTree   | `WITH_FPTREE=1`      | `adapters/fptree_adapter.hpp`     |
| BzTree   | `WITH_BZTREE=1`      | `adapters/bztree_adapter.hpp`     |
| LB+Tree  | `WITH_LBTREE=1`      | `adapters/lbtree_adapter.hpp`     |
| uTree    | `WITH_UTREE=1`       | `adapters/utree_adapter.hpp`      |
| Circ-Tree| `WITH_CIRCTREE=1`    | `adapters/circtree_adapter.hpp`   |
| DPTree   | `WITH_DPTREE=1`      | `adapters/dptree_adapter.hpp`     |
| NBTree   | `WITH_NBTREE=1`      | `adapters/nbtree_adapter.hpp`     |

Each adapter behind the flag uses the same `IIndex` interface
(`index_iface.hpp`) — `lookup`, `update`, `insert`, `remove`, `scan` plus
`bulk_load` — so the benchmark driver does not change when you
enable one.

## Step-by-step: wire up one external index

The shape is the same for all nine. Using FAST&FAIR as an example:

### 1. Clone the source

```bash
cd third_party
./fetch_indexes.sh fastfair        # tries the URL on file
# or do it manually:
git clone https://github.com/DICL/FAST_FAIR.git fastfair
```

Verify the URL on GitHub first; the script does not have authoritative
information.

### 2. Verify the API in the adapter

Open `adapters/fastfair_adapter.hpp` and find the `TODO` block. The
fields you almost always have to adjust are:

* the constructor (does it take an allocator? a PMEM pool path?)
* the `lookup` / `insert` / `update` / `remove` method names
* the `scan` signature — this varies most between forks
* whether keys/values are stored as `uint64_t` or as the upstream's
  own `entry_key_t` / `char*`

Make the minimal changes to make the wrapper compile against your
clone. If the upstream is C-only, wrap the `#include` in `extern "C"`.

### 3. Build with the flag

```bash
make WITH_FASTFAIR=1
```

If the index needs PMDK or libpmem, uncomment the corresponding
`EXTRA_LD` line in the `Makefile`. For Optane DCPMM you typically need
`-lpmemobj -lpmem`; for BzTree you also need PMwCAS and `-lnuma`.

### 4. Run

```bash
./bench --index fastfair --workload polluter --bytes 65536 \
        --arrival batch --keys 1000000 --queries 1000000 --repeats 3
```

If the adapter is mis-compiled it will fail at startup with an explicit
"adapter requested but binary was built without -DWITH_..." message.

## What about PMEM emulation?

Most of these indexes assume `/mnt/pmem` (or similar) is mounted with
DAX, or that an Optane DCPMM module is available. If you don't have
DCPMM, you have three options:

1. **DRAM emulation via `/dev/shm` or `tmpfs`.** Most of the indexes
   above were initially developed against this; performance numbers
   measured this way are not directly comparable to DCPMM numbers but
   they let you exercise the code paths.
2. **`memmap()`'d disk file** with `O_DIRECT`. Slower; useful only for
   correctness testing.
3. **PMEM emulation via the Linux pmem driver in QEMU/KVM.** Closest to
   real PMEM but the most work to set up.

The benchmark itself does not care — the adapter owns the PMEM pool.

## Concurrency notes

Each adapter declares `concurrent_safe()`. For adapters that say
`true` (FAST&FAIR, FPTree, BzTree, LB+Tree, uTree, Circ-Tree, DPTree,
NBTree all advertise lock-free or fine-grained-locking variants), the
benchmark **still** wraps the index in a `std::shared_mutex` if the
operation mix contains writes. This is conservative — it gives a
correct-but-pessimistic baseline. If you want the adapter to take its
own concurrency path, edit the dispatch in `bench.cpp::client_thread`
to skip the rwlock when `s->idx->concurrent_safe()` returns true.

## CSV impact

The CSV now includes the index name implicitly through the `--index`
flag, but the existing CSV schema does not have an `index_name`
column. If you want to mix runs from different indexes in the same
CSV, either:

* prefix each row externally (e.g. `awk -v idx=fastfair '{print idx","$0}'`), or
* add `idx->name()` to the CSV emit in `bench.cpp::emit_csv()`.

I left this out by default to keep the CSV column count stable between
v4 and v5; either choice is reasonable.

## Things to double-check before publication

For each external index you actually use in a paper, please verify:

1. The repository URL is correct and stable.
2. The version / commit you cloned matches what the paper you're
   comparing to evaluated. Adapter authors frequently change file
   layouts.
3. Build options (e.g. crash-consistency mode on/off, fingerprint
   width, leaf size) match the published configuration.
4. The locking discipline. Some "lock-free" indexes still require
   external read/write coordination on certain operations.

These verifications are the user's responsibility, not the benchmark's.
