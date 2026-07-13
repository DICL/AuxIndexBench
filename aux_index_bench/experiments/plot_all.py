#!/usr/bin/env python3
"""experiments/plot_all.py — generate figures from the e*_*.csv files.

For each CSV in results/, produce a PNG with the same stem in
results/figs/. Uses `e2e_*_ns` (queue + service) for open-loop
experiments and `svc_*_ns` for batch-mode ones.

Usage:
    python3 experiments/plot_all.py
    python3 experiments/plot_all.py results/e1_polluter.csv
"""

from __future__ import annotations
import sys, os, glob
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES  = os.path.join(REPO, "results")
FIGS = os.path.join(RES, "figs")
os.makedirs(FIGS, exist_ok=True)

COLORS = {
    "btree":     "#1f77b4",
    "hash":      "#ff7f0e",
    "fastfair":  "#2ca02c",
    "wbtree":    "#d62728",
    "utree":     "#9467bd",
    "fptree":    "#8c564b",
    "bztree":    "#e377c2",
    "lbtree":    "#7f7f7f",
    "dptree":    "#bcbd22",
    "circtree":  "#17becf",
    "nbtree":    "#aec7e8",
}

def color(idx):
    return COLORS.get(idx, "#444444")

def save(fig, stem):
    out = os.path.join(FIGS, f"{stem}.png")
    fig.tight_layout()
    fig.savefig(out, dpi=140)
    plt.close(fig)
    print(f"  wrote {out}")

def load(path):
    return pd.read_csv(path)

# For batch-mode experiments, svc == e2e (no queue), so either works.
# For open-loop, prefer e2e_*.
def total_col(df, kind="mean"):
    key = f"e2e_{kind}_ns"
    if key in df.columns:
        return key
    return f"svc_{kind}_ns"

# ---- per-experiment plotters -------------------------------------------------

def plot_e1(path):
    df = load(path)
    fig, ax = plt.subplots(figsize=(8, 5))
    mc = total_col(df, "mean")
    tc = total_col(df, "p9999")
    for idx, sub in df.groupby("index"):
        sub = sub.copy()
        sub["x"] = sub.bytes_per_call.replace(0, 1)
        sub = sub.sort_values("x")
        ax.plot(sub.x, sub[mc], marker="o", lw=2, color=color(idx),
                label=f"{idx} (mean)")
        ax.plot(sub.x, sub[tc], marker="x", lw=1.2, ls="--",
                color=color(idx), alpha=0.7, label=f"{idx} (p99.99)")
    ax.set_xscale("log", base=2); ax.set_yscale("log")
    ax.set_xlabel("Post-lookup polluter (bytes, 1 = none)")
    ax.set_ylabel("Latency (ns)")
    ax.set_title("E1: service-time vs cache pollution")
    ax.grid(alpha=0.3); ax.legend(fontsize=8, ncols=2)
    save(fig, "e1_polluter")

def plot_e1b(path):
    df = load(path)
    fig, ax = plt.subplots(figsize=(8, 5))
    mc = total_col(df, "mean")
    for idx, sub in df.groupby("index"):
        sub = sub.copy()
        sub["x"] = sub.bytes_per_call.replace(0, 1)
        sub = sub.sort_values("x")
        ax.plot(sub.x, sub[mc], marker="o", lw=2, color=color(idx),
                label=f"{idx} (polluter only)")
    ax.set_xscale("log", base=2); ax.set_yscale("log")
    ax.set_xlabel("Polluter bytes per op (1 = none)")
    ax.set_ylabel("Polluter latency (ns)")
    ax.set_title("E1b: cost of polluter alone (--no-lookup)")
    ax.grid(alpha=0.3); ax.legend(fontsize=8)
    save(fig, "e1b_polluter_alone")

def plot_e1_decomposition(e1_path, e1b_path):
    a = pd.read_csv(e1_path)
    b = pd.read_csv(e1b_path)
    mc = total_col(a, "mean")
    a = a[["index","bytes_per_call",mc]].rename(columns={mc:"full"})
    b = b[["index","bytes_per_call",total_col(b, "mean")]].rename(
            columns={total_col(b,"mean"):"poll"})
    b = b.groupby("bytes_per_call", as_index=False)["poll"].mean()
    df = a.merge(b, on="bytes_per_call", how="inner")
    df["lookup"] = (df["full"] - df["poll"]).clip(lower=0)
    df["x"] = df["bytes_per_call"].replace(0, 1)
    fig, ax = plt.subplots(figsize=(8, 5))
    for idx, sub in df.groupby("index"):
        sub = sub.sort_values("x")
        ax.plot(sub.x, sub["full"],   marker="o", lw=2, color=color(idx),
                label=f"{idx} combined")
        ax.plot(sub.x, sub["lookup"], marker="s", lw=1.5, ls="--",
                color=color(idx), label=f"{idx} lookup-only (est.)")
    poll = df.drop_duplicates("bytes_per_call").sort_values("x")
    ax.plot(poll.x, poll["poll"], marker="d", lw=1.2, color="#888",
            ls=":", label="polluter alone")
    ax.set_xscale("log", base=2); ax.set_yscale("log")
    ax.set_xlabel("Polluter bytes per op (1 = none)")
    ax.set_ylabel("Latency (ns)")
    ax.set_title("E1 + E1b: lookup vs polluter decomposition")
    ax.grid(alpha=0.3); ax.legend(fontsize=7, ncols=2)
    save(fig, "e1_decomposition")

def plot_e2(path):
    df = load(path)
    mc = total_col(df, "mean")
    fig, ax = plt.subplots(figsize=(8, 5))
    plotted = False
    for (idx, wl), sub in df.groupby(["index", "workload"]):
        sub = sub.sort_values("keys")
        if len(sub) < 2: continue
        marker = "o" if wl == "none" else "s"
        ls     = "-" if wl == "none" else "--"
        ax.plot(sub["keys"], sub[mc], marker=marker, ls=ls, lw=2,
                color=color(idx), label=f"{idx}/{wl}")
        plotted = True
    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_xlabel("Index key count")
    ax.set_ylabel("Mean latency (ns)")
    ax.set_title("E2: latency vs index size (solid: no pollute; dashed: 64K pollute)")
    ax.grid(alpha=0.3)
    if plotted: ax.legend(fontsize=8, ncols=2)
    save(fig, "e2_keycount")

def plot_e3(path):
    df = load(path)
    # For open-loop we want to separate service and queue explicitly.
    fig, ax = plt.subplots(figsize=(8, 5))
    for idx, sub in df.groupby("index"):
        sub = sub.sort_values("rate")
        ax.plot(sub.rate/1e6, sub["svc_mean_ns"],  marker="o", lw=2,
                color=color(idx), label=f"{idx} service")
        ax.plot(sub.rate/1e6, sub["queue_mean_ns"], marker="^", lw=1.5, ls="--",
                color=color(idx), label=f"{idx} queue")
        if "e2e_mean_ns" in sub.columns:
            ax.plot(sub.rate/1e6, sub["e2e_mean_ns"], marker="D", lw=1.2, ls=":",
                    color=color(idx), alpha=0.7, label=f"{idx} e2e")
    ax.set_yscale("log")
    ax.set_xlabel("Offered rate (M ops/s)")
    ax.set_ylabel("Latency (ns)")
    ax.set_title("E3: service vs queueing delay as offered load grows")
    ax.grid(alpha=0.3); ax.legend(fontsize=7, ncols=3)
    save(fig, "e3_arrival_rate")

def plot_e4(path):
    df = load(path)
    fig, ax = plt.subplots(figsize=(8, 5))
    for idx, sub in df.groupby("index"):
        sub = sub.sort_values("cv2")
        ax.plot(sub.cv2, sub["queue_mean_ns"],  marker="o", lw=2,
                color=color(idx), label=f"{idx} queue mean")
        ax.plot(sub.cv2, sub["queue_p9999_ns"], marker="d", lw=1.5, ls="--",
                color=color(idx), label=f"{idx} queue p99.99")
    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_xlabel("Inter-arrival CV² (1 = Poisson)")
    ax.set_ylabel("Queueing delay (ns)")
    ax.set_title("E4: burstiness vs queueing delay (same mean rate)")
    ax.grid(alpha=0.3); ax.legend(fontsize=8)
    save(fig, "e4_burstiness")

def plot_e5(path):
    df = load(path)
    mc = total_col(df, "mean")
    fig, ax = plt.subplots(figsize=(8, 5))
    for (idx, wl), sub in df.groupby(["index", "workload"]):
        sub = sub.sort_values("theta")
        marker = "o" if wl == "none" else "s"
        ls     = "-" if wl == "none" else "--"
        ax.plot(sub.theta, sub[mc], marker=marker, ls=ls, lw=2,
                color=color(idx), label=f"{idx}/{wl}")
    ax.set_yscale("log")
    ax.set_xlabel("Zipf θ (0 = uniform)")
    ax.set_ylabel("Mean latency (ns)")
    ax.set_title("E5: skew × pollution")
    ax.grid(alpha=0.3); ax.legend(fontsize=8, ncols=2)
    save(fig, "e5_skew")

def plot_e6(path):
    df = load(path)
    order = list(dict.fromkeys(df.op_mix.tolist()))
    df["mix_idx"] = df.op_mix.apply(order.index)
    indexes = sorted(df["index"].unique())
    fig, ax = plt.subplots(figsize=(10, 5.5))
    width = 0.18
    for i, idx in enumerate(indexes):
        sub = df[df["index"] == idx].sort_values("mix_idx")
        xs = sub.mix_idx + (i - (len(indexes)-1)/2)*width
        ax.bar(xs, sub.search_p9999_ns, width, color=color(idx),
               label=f"{idx} search p99.99")
        if (sub.update_p9999_ns > 0).any():
            ax.bar(xs, sub.update_p9999_ns, width, color=color(idx),
                   alpha=0.4, hatch="//", label=f"{idx} update p99.99")
    ax.set_xticks(range(len(order)))
    ax.set_xticklabels(order, rotation=20, ha="right", fontsize=8)
    ax.set_yscale("log")
    ax.set_ylabel("p99.99 latency (ns)")
    ax.set_title("E6: per-op tail latency across op-mix points")
    ax.grid(alpha=0.3, axis="y"); ax.legend(fontsize=8, ncols=2)
    save(fig, "e6_opmix")

def plot_e7(path):
    df = load(path)
    fig, ax = plt.subplots(figsize=(8, 5))
    for idx, sub in df.groupby("index"):
        sub = sub.sort_values("clients")
        ax.plot(sub.clients, sub["svc_mean_ns"],   marker="o", lw=2,
                color=color(idx), label=f"{idx} service")
        ax.plot(sub.clients, sub["queue_mean_ns"], marker="^", lw=1.5, ls="--",
                color=color(idx), label=f"{idx} queue")
    ax.set_xlabel("Client threads")
    ax.set_ylabel("Mean latency (ns)"); ax.set_yscale("log")
    ax.set_title("E7: scaling with client count (fixed offered rate)")
    ax.grid(alpha=0.3); ax.legend(fontsize=8, ncols=2)
    save(fig, "e7_clients")

def plot_e8(path):
    df = load(path)
    def tag(row):
        if row.sin_amp > 0 and row.burst_prob > 0: return "5_mixed"
        if row.sin_amp > 0:                        return "2_sin"
        if row.level_period > 0:                   return "3_level"
        if row.burst_prob > 0:                     return "4_burst"
        return "1_flat"
    df = df.copy()
    df["scenario"] = df.apply(tag, axis=1)
    order = ["1_flat", "2_sin", "3_level", "4_burst", "5_mixed"]
    df["s_idx"] = df.scenario.apply(lambda s: order.index(s) if s in order else len(order))
    indexes = sorted(df["index"].unique())
    fig, ax = plt.subplots(figsize=(9, 5))
    width = 0.18
    for i, idx in enumerate(indexes):
        sub = df[df["index"] == idx].sort_values("s_idx").drop_duplicates("scenario")
        xs = sub.s_idx + (i - (len(indexes)-1)/2)*width
        ax.bar(xs, sub.queue_p9999_ns, width, color=color(idx),
               label=f"{idx} queue p99.99")
    ax.set_xticks(range(len(order)))
    ax.set_xticklabels(order, fontsize=9)
    ax.set_yscale("log")
    ax.set_ylabel("Queue p99.99 (ns)")
    ax.set_title("E8: λ(t) shapes vs queueing delay tail")
    ax.grid(alpha=0.3, axis="y"); ax.legend(fontsize=8)
    save(fig, "e8_traffic_shape")

def plot_e9(path):
    df = load(path)
    fig, ax = plt.subplots(figsize=(8, 5))
    for idx, sub in df.groupby("index"):
        sub = sub.copy()
        sub["x"] = sub.bytes_per_call.replace(0, 1)
        sub = sub.sort_values("x")
        ax.plot(sub.x, sub["search_mean_ns"],  marker="o", lw=2, color=color(idx),
                label=f"{idx} search mean")
        ax.plot(sub.x, sub["search_p9999_ns"], marker="x", lw=1.2, ls=":",
                color=color(idx), label=f"{idx} search p99.99")
        ax.plot(sub.x, sub["update_mean_ns"],  marker="s", lw=2, alpha=0.5,
                color=color(idx), label=f"{idx} update mean")
        ax.plot(sub.x, sub["update_p9999_ns"], marker="d", lw=1.2, ls="--",
                color=color(idx), alpha=0.5, label=f"{idx} update p99.99")
    ax.set_xscale("log", base=2); ax.set_yscale("log")
    ax.set_xlabel("Polluter bytes (1 = none)")
    ax.set_ylabel("Latency (ns)")
    ax.set_title("E9: read vs write tail under cache pressure (60/40 mix)")
    ax.grid(alpha=0.3); ax.legend(fontsize=7, ncols=2)
    save(fig, "e9_mix_x_pollute")

def plot_e10(path):
    df = load(path)
    mc = total_col(df, "mean")
    fig, ax = plt.subplots(figsize=(8, 5))
    for (idx, mode), sub in df.groupby(["index", "workload"]):
        sub = sub.sort_values("bytes_per_call")
        marker = "o" if mode == "object" else "s"
        ls     = "-" if mode == "object" else "--"
        ax.plot(sub.bytes_per_call, sub[mc], marker=marker, ls=ls, lw=2,
                color=color(idx), label=f"{idx}/{mode}")
    ax.set_xscale("log", base=2); ax.set_yscale("log")
    ax.set_xlabel("Per-call bytes")
    ax.set_ylabel("Mean latency (ns)")
    ax.set_title("E10: object vs storage-stack post-lookup work")
    ax.grid(alpha=0.3); ax.legend(fontsize=8, ncols=2)
    save(fig, "e10_object_storage")

def plot_e11(path):
    df = load(path)
    fig, axes = plt.subplots(1, 2, figsize=(11, 5), sharey=True)
    for ax, (B, sub_b) in zip(axes, df.groupby("bytes_per_call")):
        sub = sub_b.reset_index(drop=True)
        ax.plot(sub.index, sub["svc_mean_ns"],   marker="o", lw=2, label="mean")
        ax.plot(sub.index, sub["svc_p99_ns"],    marker="x", lw=1.5, label="p99")
        ax.plot(sub.index, sub["svc_p9999_ns"],  marker="d", lw=1.5, ls="--",
                label="p99.99")
        ax.set_title(f"polluter = {int(B)} bytes")
        ax.set_xlabel("σ sweep step"); ax.grid(alpha=0.3)
        ax.set_yscale("log"); ax.legend(fontsize=8)
    axes[0].set_ylabel("Latency (ns)")
    fig.suptitle("E11: hash σ sweep — bucket bias vs tail latency")
    save(fig, "e11_hash_sigma")

def plot_e12(path):
    df = load(path)
    if "throughput_mops" not in df.columns:
        df["throughput_mops"] = df["consumed"] / df["wall_ns"] * 1e3
    # One panel per polluter size; X = workers, Y = throughput, line per index.
    sizes = sorted(df.bytes_per_call.unique())
    fig, axes = plt.subplots(1, len(sizes), figsize=(5.5 * len(sizes), 5),
                             sharey=True, squeeze=False)
    for ax, B in zip(axes[0], sizes):
        sub_b = df[df.bytes_per_call == B]
        for idx, sub in sub_b.groupby("index"):
            sub = sub.sort_values("workers")
            ax.plot(sub.workers, sub.throughput_mops, marker="o", lw=2,
                    color=color(idx), label=idx)
            # Ideal-scaling reference from each index's own 1-worker point.
            base = sub[sub.workers == sub.workers.min()]
            if len(base):
                w0 = base.workers.iloc[0]; t0v = base.throughput_mops.iloc[0]
                ax.plot(sub.workers, t0v * sub.workers / w0, lw=0.8, ls=":",
                        color=color(idx), alpha=0.4)
        label = "none" if B == 0 else f"{int(B)//1024} KiB"
        ax.set_title(f"polluter = {label}")
        ax.set_xlabel("Workers")
        ax.set_xscale("log", base=2)
        ax.grid(alpha=0.3)
    axes[0][0].set_ylabel("Throughput (Mops/s)")
    axes[0][0].legend(fontsize=8)
    fig.suptitle("E12: concurrent mixed-workload throughput (s=0.6,u=0.1,i=0.3); "
                 "dotted = ideal scaling")
    save(fig, "e12_concurrency")

def plot_e13(path):
    df = load(path)
    if "throughput_mops" not in df.columns:
        df["throughput_mops"] = df["consumed"] / df["wall_ns"] * 1e3
    # write fraction from the op_mix string: wf = 1 - search fraction
    def wf_of(m):
        for part in str(m).split(","):
            if part.startswith("s="):
                return round(1.0 - float(part[2:]), 4)
        return 0.0
    df = df.copy()
    df["wf"] = df.op_mix.apply(wf_of)
    fig, axes = plt.subplots(1, 2, figsize=(12, 5))
    for idx, sub in df.groupby("index"):
        sub = sub.sort_values("wf")
        axes[0].plot(sub.wf, sub.throughput_mops, marker="o", lw=2,
                     color=color(idx), label=idx)
        base = sub[sub.wf == 0.0]
        if len(base):
            norm = sub.throughput_mops / base.throughput_mops.iloc[0]
            axes[1].plot(sub.wf, norm, marker="o", lw=2,
                         color=color(idx), label=idx)
    for ax, ylab in ((axes[0], "Throughput (Mops/s)"),
                     (axes[1], "Normalised to write-fraction 0")):
        ax.set_xlabel("Write fraction")
        ax.set_ylabel(ylab)
        ax.set_xscale("symlog", linthresh=0.001)
        ax.grid(alpha=0.3)
        ax.legend(fontsize=8)
    axes[1].axhline(1.0, color="#888", lw=0.8, ls=":")
    fig.suptitle("E13: coherence sensitivity — throughput vs tiny write fractions\n"
                 "(fixed workers, no polluter, zipf hot keys; drop at wf≤0.01 ≈ pure invalidation cost)")
    save(fig, "e13_coherence")

PLOTTERS = {
    "e1_polluter":       plot_e1,
    "e1b_polluter_alone":plot_e1b,
    "e2_keycount":       plot_e2,
    "e3_arrival_rate":   plot_e3,
    "e4_burstiness":     plot_e4,
    "e5_skew":           plot_e5,
    "e6_opmix":          plot_e6,
    "e7_clients":        plot_e7,
    "e8_traffic_shape":  plot_e8,
    "e9_mix_x_pollute":  plot_e9,
    "e10_object_storage":plot_e10,
    "e11_hash_sigma":    plot_e11,
    "e12_concurrency":   plot_e12,
    "e13_coherence":     plot_e13,
}

def main(argv):
    paths = argv[1:]
    if not paths:
        paths = sorted(glob.glob(os.path.join(RES, "e*.csv")))
    if not paths:
        print(f"no CSVs to plot in {RES}", file=sys.stderr)
        return 1
    for path in paths:
        stem = os.path.splitext(os.path.basename(path))[0]
        fn = PLOTTERS.get(stem)
        if not fn:
            print(f"no plotter for {stem}; skipping", file=sys.stderr)
            continue
        try:
            print(f"plotting {path} ...")
            fn(path)
        except Exception as e:
            print(f"  failed: {e}", file=sys.stderr)
    e1  = os.path.join(RES, "e1_polluter.csv")
    e1b = os.path.join(RES, "e1b_polluter_alone.csv")
    if os.path.exists(e1) and os.path.exists(e1b):
        try:
            print(f"plotting decomposition (e1 vs e1b) ...")
            plot_e1_decomposition(e1, e1b)
        except Exception as e:
            print(f"  failed: {e}", file=sys.stderr)
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
