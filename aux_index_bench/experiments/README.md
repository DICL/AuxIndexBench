# Experiments

The workflow is now two-phase:

**Phase 1 — Build per-index binaries** (once):

```bash
bash build_all.sh
```

Produces `bench.<index>` in the project root for every index whose
build succeeds. Names:

| Binary            | Includes              |
|-------------------|-----------------------|
| `bench.btree`     | built-in btree, hash  |
| `bench.fastfair`  | built-ins + FAST&FAIR |
| `bench.wbtree`    | built-ins + wB+-Tree  |
| `bench.utree`     | built-ins + uTree     |
| `bench.fptree`    | built-ins + FPTree    |
| `bench.lbtree`    | built-ins + LB+-Tree  |
| `bench.bztree`    | built-ins + BzTree    |
| `bench.dptree`    | built-ins + DPTree    |

Every binary can run `btree` and `hash` (they're built into every
variant). The sweep scripts route `btree`/`hash` to `bench.btree` by
default; each external index goes to the matching `bench.<index>`.

Skip a variant if you don't have its dependencies:

```bash
WHICH="btree fastfair wbtree" bash build_all.sh
```

Pass extra flags:

```bash
EXTRA="UTREE_NO_GPERFTOOLS=1" bash build_all.sh
```

**Phase 2 — Run sweeps**:

```bash
bash experiments/e1_polluter_sweep.sh
python3 experiments/plot_all.py
# or all at once:
bash experiments/run_all.sh
```

Each `e*_*.sh` iterates the index list, calling `bench.<index>` for
every index. Failures per index are logged but the sweep continues.

## Script → paper mapping

| Script                      | Paper §5 experiment | Axis varied                   |
|-----------------------------|---------------------|-------------------------------|
| `e1_polluter_sweep.sh`      | exp 2               | post-lookup polluter bytes    |
| `e1b_polluter_alone.sh`     | exp 2 (companion)   | polluter bytes with `--no-lookup` |
| `e2_keycount_sweep.sh`      | (new)               | index key count               |
| `e3_arrival_rate.sh`        | exp 5               | offered Poisson rate          |
| `e4_burstiness.sh`          | exp 6               | inter-arrival cv²             |
| `e5_skew.sh`                | exp 4               | Zipf θ                        |
| `e6_opmix.sh`               | exp 8               | CRUD+scan mix                 |
| `e7_clients.sh`             | exp 7               | client thread count           |
| `e8_traffic_shape.sh`       | exp 9               | λ(t) shape (sine/level/burst) |
| `e9_mix_x_pollute.sh`       | exp 10              | mix × pollution interaction   |
| `e10_object_storage.sh`     | exp 3               | object vs storage-stack work  |
| `e11_hash_sigma.sh`         | hash-parallel set   | hash bit-bias σ               |
| `e12_concurrency.sh`        | (new)               | workers × pollution → throughput, fixed mix s=0.6,u=0.1,i=0.3 |

## Tuning

Every script honours these environment variables:

| Env var          | Default                          | Notes                                   |
|------------------|----------------------------------|-----------------------------------------|
| `INDEXES`        | `"btree hash fastfair wbtree"`   | space-separated index names             |
| `KEYS`           | `1000000`                        | index size                              |
| `QUERIES`        | `1000000`                        | batch query count                       |
| `POISSON_QUERIES`| `200000`                         | open-loop query count                   |
| `REPEATS`        | `3`                              | runs per data point; best kept          |
| `UNIVERSE`       | `512 MB`                         | polluter buffer                         |
| `OUT`            | `results/<stem>.csv`             | append-only output CSV                  |

Plus per-script knobs — see the `# Tunables:` block in each file.

To use a custom-built binary for one index, set `BENCH_<UPPER>`:

```bash
BENCH_UTREE=$HOME/my_custom_utree_bench \
  bash experiments/e3_arrival_rate.sh
```

## CSV layout

```
index, workload, ..., svc_*_ns, queue_*_ns, e2e_*_ns, per_op_*_ns
```

`svc_*` = service time (processing only), `queue_*` = queue wait,
`e2e_*` = end-to-end (queue + service). Use `e2e_*` for open-loop
tail-latency claims in the paper; `svc_*` alone hides queueing delay.

## Caveats

Open-loop (Poisson) experiments need `clients + 1` physical cores for
honest queue numbers. On a small VM, queue delay is dominated by the
scheduler slice; the scripts print a warning.

Publication-quality runs need:

- thread pinning (`taskset` or `numactl --physcpubind`)
- isolated cores (`isolcpus=...` boot arg, or `cset shield`)
- CPU governor set to `performance`
- nothing else running on the machine
