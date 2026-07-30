# Revisiting Index Performance under High Cache-Miss Pressure

## Auxiliary-Index Benchmark (v1)

A microbenchmark that evaluates index data structures **as auxiliary
access paths in larger applications**, rather than as standalone
workloads.

## What v1 contains

* **Plugin index interface** (`IIndex` in `index_iface.hpp`): any index
  that implements `lookup / update / insert / remove / scan` can be
  driven through the benchmark. Built-in B+tree and chaining hash table
  are now adapters behind this interface.
* **External-index adapter stubs** for nine PMEM-resident B+tree
  variants: wB+Tree, FAST&FAIR, FPTree, BzTree, LB+Tree, µTree,
  Circ-Tree, DPTree, NBTree. Each is opt-in via `WITH_<NAME>=1`.
  See `EXTERNAL_INDEXES.md` for the wiring guide.
* Four post-lookup workload modes: none, polluter, object access,
  storage-stack emulation.
* Batch (closed-loop) and Poisson (open-loop) arrival modes with a
  lock-free MPMC queue between producer and consumers.
* Time-varying λ(t) via sinusoidal + level + Pareto-burst components.
* Operation mix (search/update/insert/delete/scan) with per-op
  tail-latency reporting (p50/p99/p99.9/p99.99/max).
* Synthetic hash key generator with tunable bit-level collision
  concentration (σ knob; σ=0.4 reproduces the paper's 53%/25% figure).

See the paper draft under `paper/` for the contribution discussion.

## Build

```bash
make            # builds ./bench
make skew_check # builds the hash-key-generator sanity tool
make all        # both
```

C++17, `-O3 -march=native -pthread`. Tested with g++ 11 and 13.

## Example invocations

```bash
# v1-style polluter sweep with built-in B+tree
./bench --workload polluter --bytes 65536 --universe $((512<<20)) \
        --arrival batch --repeats 3

# open-loop, 2 M ops/s, 4 clients, 80/20 read/write mix
./bench --workload object --bytes 4096 \
        --arrival poisson --rate 2000000 --cv2 1.0 --clients 4 \
        --op-mix "s=0.8,u=0.2" --repeats 3

# MixGraph-style time-varying traffic
./bench --workload object --bytes 4096 \
        --arrival poisson --rate 1500000 --cv2 1.0 --clients 4 \
        --sin-amp 0.5 --sin-period 1.0 \
        --burst-prob 0.05 --burst-tick 0.05 --burst-pareto 1.5

# hash index with bit-level skew
./bench --index hash --hash-buckets $((1<<20)) --hash-sigma 0.4 \
        --workload polluter --bytes 262144 \
        --op-mix "s=0.6,u=0.4" --arrival batch --repeats 3

# External index (requires upstream code and a rebuild — see EXTERNAL_INDEXES.md)
# make WITH_FASTFAIR=1
# ./bench --index fastfair --workload polluter --bytes 65536 ...
```

## Sweep + plot

```bash
./run_sweep.sh                 # writes results/sweep.csv
python3 plot_results.py        # writes results/fig_*.png
```

Ten experiments covering polluter, object/storage, skew, arrival rate,
burstiness, client scaling, op-mix, time-varying traffic, and mix ×
pollution interactions.

## Files

```text
index.hpp              Built-in B+tree
hash_index.hpp         Built-in chaining hash table
hash_key_gen.hpp       Bit-bias synthetic key generator
index_iface.hpp        IIndex abstract interface (v5)
index_factory.hpp      Build IIndex by name (v5)
adapters/              IIndex adapters for all built-in + external indexes
third_party/           Place clones of external indexes here
                       (see EXTERNAL_INDEXES.md and fetch_indexes.sh)
workload.hpp           Post-lookup workloads
mpmc_queue.hpp         Lock-free Vyukov MPMC ring buffer
histogram.hpp          Octave-bucketed latency histogram
traffic_model.hpp      Time-varying λ(t) model
op_mix.hpp             Operation kind + mix sampler
perfctr.hpp            perf_event_open wrapper
bench.cpp              Driver
run_skew_check.cpp     Verifies the hash key generator's distribution
run_sweep.sh           Ten-experiment sweep
plot_results.py        Generates figures from sweep CSV
Makefile               Default + per-index opt-in build flags
paper/                 Paper draft (Markdown + LaTeX + PDF)
slides/                Slide deck (Markdown + Beamer + PDF)
EXTERNAL_INDEXES.md    Wiring guide for the nine external adapters
```

## Caveats

* Requires at least `clients + 1` physical cores; the producer and
  consumers busy-wait.
* CPU pinning and disabled frequency scaling strongly recommended.
* Single-core hosts produce queue-delay numbers dominated by the
  Linux scheduler slice — use only for functional testing.
