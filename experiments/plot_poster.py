#!/usr/bin/env python3
# experiments/plot_poster.py
#
# Poster-quality figures for aux_index_bench, one per experiment,
# mirroring the plot_e*.gp gnuplot set. pandas handles the quoted
# op_mix column natively, so no perl/awk preprocessing is needed.
#
# Usage:
#   python3 plot_poster.py results/e1_polluter.csv results/e12_concurrency.csv
#   python3 plot_poster.py --all results/          # every known CSV in dir
#   python3 plot_poster.py --outdir figs results/e13_coherence.csv
#
# Special cases:
#   * e13: if a file named e13_uniform.csv sits next to the main e13 CSV,
#     it is overlaid automatically as the uniform contrast (dashed).
#   * e11: the CSV does not record hash sigma; pass the sweep list via
#     --sigmas "0.2 0.4 ..." (defaults to the e11 script default).

import argparse
import os
import sys

import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# ----------------------------------------------------------------- style
COLORS = {
    "btree":    "#1f77b4", "hash":   "#ff7f0e", "fastfair": "#2ca02c",
    "wbtree":   "#d62728", "utree":  "#9467bd", "fptree":   "#8c564b",
    "lbtree":   "#e377c2", "dptree": "#7f7f7f", "bztree":   "#bcbd22",
}
MARKERS = {
    "btree": "o", "hash": "P", "fastfair": "s", "wbtree": "^",
    "utree": "v", "fptree": "D", "lbtree": "X", "dptree": "*",
}
IDX_ORDER = ["btree", "hash", "fastfair", "wbtree", "utree",
             "fptree", "lbtree", "dptree", "bztree"]

plt.rcParams.update({
    "font.size": 11, "axes.grid": True, "grid.alpha": 0.3,
    "figure.dpi": 120, "savefig.bbox": "tight",
})

BYTES_TICKS = ([0.5, 4, 32, 256, 2048, 16384],
               ["none", "4K", "32K", "256K", "2M", "16M"])


def load(path):
    df = pd.read_csv(path)
    df.columns = [c.strip() for c in df.columns]
    if "throughput_mops" not in df.columns and {"consumed", "wall_ns"} <= set(df.columns):
        df["throughput_mops"] = df["consumed"] / df["wall_ns"] * 1e3
    return df


# Indexes excluded from every figure (poster decision: the DRAM-only
# built-in btree is a harness baseline, not a PMEM contender).
EXCLUDE = {"btree"}


def indexes_in(df):
    return [i for i in IDX_ORDER if i in set(df["index"]) and i not in EXCLUDE]


def lineprops(idx):
    return dict(color=COLORS.get(idx, "#333333"),
                marker=MARKERS.get(idx, "o"), lw=2, ms=5)


def bytes_x(df):
    b = df["bytes_per_call"].astype(float).copy()
    b[df["workload"] == "none"] = 0
    return b.map(lambda v: 0.5 if v == 0 else v / 1024.0)


def save(fig, outdir, name):
    os.makedirs(outdir, exist_ok=True)
    p = os.path.join(outdir, name + ".png")
    fig.savefig(p)
    plt.close(fig)
    print(f"  wrote {p}")


def wf_of_mix(m):
    for part in str(m).split(","):
        part = part.strip().strip('"')
        if part.startswith("s="):
            return round(1.0 - float(part[2:]), 4)
    return 0.0


# ------------------------------------------------------------- E1 (two)
def plot_e1_lookup(df, outdir):
    df = df.copy()
    df["x"] = bytes_x(df)
    g = df.groupby(["index", "x"], as_index=False)["lookup_mean_ns"].mean()
    fig, ax = plt.subplots(figsize=(8, 5))
    for idx in indexes_in(g):
        sub = g[g["index"] == idx].sort_values("x")
        ax.plot(sub.x, sub.lookup_mean_ns, label=idx, **lineprops(idx))
    ax.set(xscale="log", yscale="log",
           xlabel="Post-lookup working set per op",
           ylabel="Mean lookup latency (ns)",
           title="E1: index lookup latency vs cache pollution\n"
                 "(polluter time excluded)")
    ax.set_xticks(*BYTES_TICKS)
    ax.legend(fontsize=9)
    save(fig, outdir, "e1_lookup")


def plot_e1_tails(df, outdir):
    df = df.copy()
    df["x"] = bytes_x(df)
    g = df.groupby(["index", "x"], as_index=False)[
        ["lookup_mean_ns", "lookup_p9999_ns"]].mean()
    # Normalise both metrics to the no-pollution baseline per index.
    norm = {}
    ymax = 1.0
    for idx in indexes_in(g):
        sub = g[g["index"] == idx].sort_values("x")
        base = sub[sub.x == 0.5]
        if base.empty:
            continue
        m = sub.lookup_mean_ns / base.lookup_mean_ns.iloc[0]
        t = sub.lookup_p9999_ns / base.lookup_p9999_ns.iloc[0]
        norm[idx] = (sub.x.values, m.values, t.values)
        ymax = max(ymax, m.max(), t.max())
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4.8), sharey=True)
    for idx, (xs, m, t) in norm.items():
        ax1.plot(xs, m, label=idx, **lineprops(idx))
        ax2.plot(xs, t, label=idx, **lineprops(idx))
    for ax, ttl in ((ax1, "mean lookup latency"),
                    (ax2, "p99.99 lookup latency")):
        ax.axhline(1.0, color="#999", lw=0.8, ls=":")
        ax.set(xscale="log", xlabel="Post-lookup working set per op",
               title=ttl)
        ax.set_xticks(*BYTES_TICKS)
    ax1.set_ylabel("Normalised to no-pollution baseline")
    ax1.set_ylim(0, ymax * 1.08)
    ax1.legend(fontsize=9)
    fig.suptitle("E1: pollution moves the mean far more than the extreme tail")
    save(fig, outdir, "e1_tails")


# ------------------------------------------------------------------- E2
def plot_e2(df, outdir):
    g = df.groupby(["index", "keys"], as_index=False)["lookup_mean_ns"].mean()
    fig, ax = plt.subplots(figsize=(8, 5))
    for idx in indexes_in(g):
        sub = g[g["index"] == idx].sort_values("keys")
        ax.plot(sub["keys"], sub.lookup_mean_ns, label=idx, **lineprops(idx))
    ax.set(xscale="log", xlabel="Keys in index",
           ylabel="Mean lookup latency (ns)",
           title="E2: lookup latency vs index size (O(log n) on log-x)")
    ax.legend(fontsize=8)
    save(fig, outdir, "e2_keycount")


# ------------------------------------------------------------------- E3
def plot_e3(df, outdir):
    g = df.groupby(["index", "rate"], as_index=False)[
        ["queue_mean_ns", "svc_mean_ns"]].mean()
    fig, ax = plt.subplots(figsize=(8.5, 5))
    for idx in indexes_in(g):
        sub = g[g["index"] == idx].sort_values("rate")
        ax.plot(sub.rate / 1e6, sub.queue_mean_ns / 1e3,
                label=f"{idx} queue", **lineprops(idx))
        ax.plot(sub.rate / 1e6, sub.svc_mean_ns / 1e3, ls="--", lw=1.2,
                ms=4, marker=MARKERS.get(idx, "o"), mfc="none",
                color=COLORS.get(idx), label=f"{idx} service")
    ax.set(yscale="log", xlabel="Offered rate (M ops/s)",
           ylabel="Latency (µs)",
           title="E3: queueing delay explodes near saturation; "
                 "service time barely moves")
    ax.legend(fontsize=7, ncols=2)
    save(fig, outdir, "e3_queueing")


# ------------------------------------------------------------------- E4
def plot_e4(df, outdir):
    g = df.groupby(["index", "cv2"], as_index=False)[
        ["queue_mean_ns", "queue_p9999_ns"]].mean()
    fig, ax = plt.subplots(figsize=(8.5, 5))
    for idx in indexes_in(g):
        sub = g[g["index"] == idx].sort_values("cv2")
        ax.plot(sub.cv2, sub.queue_mean_ns / 1e3, label=f"{idx} mean",
                **lineprops(idx))
        ax.plot(sub.cv2, sub.queue_p9999_ns / 1e3, ls="--", lw=1.2, ms=4,
                marker=MARKERS.get(idx, "o"), mfc="none",
                color=COLORS.get(idx), label=f"{idx} p99.99")
    ax.axvline(1.0, color="#999", lw=0.8, ls=":")
    ax.text(1.05, 0.95, "Poisson", transform=ax.get_xaxis_transform(),
            fontsize=9, color="#666")
    ax.set(xscale="log", yscale="log",
           xlabel="Inter-arrival CV² (same mean rate)",
           ylabel="Queueing delay (µs)",
           title="E4: burstiness at a constant mean rate inflates tails")
    ax.legend(fontsize=7, ncols=2)
    save(fig, outdir, "e4_burstiness")


# ------------------------------------------------------------------- E5
def plot_e5(df, outdir):
    g = df.groupby(["index", "theta"], as_index=False)["lookup_mean_ns"].mean()
    fig, ax = plt.subplots(figsize=(8, 5))
    for idx in indexes_in(g):
        sub = g[g["index"] == idx].sort_values("theta")
        ax.plot(sub.theta, sub.lookup_mean_ns, label=idx, **lineprops(idx))
    ax.set(xlabel="Zipf theta (0 = uniform)",
           ylabel="Mean lookup latency (ns)",
           title="E5: access skew keeps the hot path cached")
    ax.legend(fontsize=8)
    save(fig, outdir, "e5_skew")


# ------------------------------------------------------------------- E6
def plot_e6(df, outdir):
    df = df.copy()
    df["mix"] = df["op_mix"].astype(str).str.strip('"')
    order = list(dict.fromkeys(df["mix"]))
    idxs = indexes_in(df)
    g = df.groupby(["index", "mix"])["lookup_mean_ns"].mean() / 1e3
    fig, ax = plt.subplots(figsize=(11, 5))
    width = 0.8 / len(idxs)
    xs = np.arange(len(order))
    for k, idx in enumerate(idxs):
        vals = [g.get((idx, m), np.nan) for m in order]
        ax.bar(xs + k * width, vals, width, label=idx,
               color=COLORS.get(idx), edgecolor="black", lw=0.4)
    ax.set_xticks(xs + 0.4 - width / 2)
    ax.set_xticklabels([m.replace(",", "\n") for m in order], fontsize=8)
    ax.set(ylabel="Mean index op time (µs)",
           title="E6: index op time across CRUD mixes "
                 "(polluter excluded — write paths exposed)")
    ax.legend(fontsize=8)
    save(fig, outdir, "e6_opmix")


# ------------------------------------------------------------------- E7
def plot_e7(df, outdir):
    g = df.groupby(["index", "clients"], as_index=False)[
        ["throughput_mops", "e2e_p99_ns"]].mean()
    offered = df["rate"].iloc[0] / 1e6
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4.8))
    for idx in indexes_in(g):
        sub = g[g["index"] == idx].sort_values("clients")
        ax1.plot(sub.clients, sub.throughput_mops, label=idx, **lineprops(idx))
        ax2.plot(sub.clients, sub.e2e_p99_ns / 1e6, label=idx, **lineprops(idx))
    ax1.axhline(offered, color="#666", lw=1, ls=":",
                label=f"offered {offered:.1f} M/s")
    ax1.set(xscale="log", xlabel="Consumer threads",
            ylabel="Throughput (Mops/s)", title="completed throughput")
    ax1.set_xticks(sorted(g.clients.unique()),
                   [str(c) for c in sorted(g.clients.unique())])
    ax1.legend(fontsize=8)
    ax2.set(xscale="log", yscale="log", xlabel="Consumer threads",
            ylabel="e2e p99 (ms)", title="e2e p99")
    ax2.set_xticks(sorted(g.clients.unique()),
                   [str(c) for c in sorted(g.clients.unique())])
    ax2.legend(fontsize=8)
    fig.suptitle("E7: consumer scaling at fixed offered rate")
    save(fig, outdir, "e7_clients")


# ------------------------------------------------------------------- E8
def shape_of(row):
    if row["sin_amp"] > 0 and row["burst_prob"] > 0:
        return "sine+burst"
    if row["sin_amp"] > 0:
        return "sine"
    if row["level_period"] > 0:
        return "level"
    if row["burst_prob"] > 0:
        return "burst"
    return "steady"


def plot_e8(df, outdir):
    df = df.copy()
    df["shape"] = df.apply(shape_of, axis=1)
    order = list(dict.fromkeys(df["shape"]))
    idxs = indexes_in(df)
    g = df.groupby(["index", "shape"])["e2e_p9999_ns"].mean() / 1e6
    fig, ax = plt.subplots(figsize=(9.5, 5))
    width = 0.8 / len(idxs)
    xs = np.arange(len(order))
    for k, idx in enumerate(idxs):
        vals = [g.get((idx, s), np.nan) for s in order]
        ax.bar(xs + k * width, vals, width, label=idx,
               color=COLORS.get(idx), edgecolor="black", lw=0.4)
    ax.set_xticks(xs + 0.4 - width / 2)
    ax.set_xticklabels(order)
    ax.set(yscale="log", ylabel="e2e p99.99 (ms)",
           title="E8: traffic shape vs tail latency (same mean offered rate)")
    ax.legend(fontsize=8)
    save(fig, outdir, "e8_traffic_shape")


# ------------------------------------------------------------------- E9
def plot_e9(df, outdir):
    df = df.copy()
    df["x"] = bytes_x(df)
    mix = str(df["op_mix"].iloc[0]).strip('"')
    g = df.groupby(["index", "x"], as_index=False)["lookup_mean_ns"].mean()
    fig, ax = plt.subplots(figsize=(8, 5))
    for idx in indexes_in(g):
        sub = g[g["index"] == idx].sort_values("x")
        ax.plot(sub.x, sub.lookup_mean_ns, label=idx, **lineprops(idx))
    ax.set(xscale="log", yscale="log",
           xlabel="Post-op working set per op",
           ylabel="Mean index time per op (ns)",
           title=f"E9: mixed workload ({mix}) — index time vs pollution")
    ax.set_xticks(*BYTES_TICKS)
    ax.legend(fontsize=8)
    save(fig, outdir, "e9_mix_pollute")


# ------------------------------------------------------------------ E10
def plot_e10(df, outdir):
    fig, axes = plt.subplots(1, 2, figsize=(12, 4.8), sharey=True)
    for ax, kind in zip(axes, ["object", "storage"]):
        sub_k = df[df["workload"] == kind]
        g = sub_k.groupby(["index", "bytes_per_call"], as_index=False)[
            "lookup_mean_ns"].mean()
        for idx in indexes_in(g):
            sub = g[g["index"] == idx].sort_values("bytes_per_call")
            ax.plot(sub.bytes_per_call / 1024.0, sub.lookup_mean_ns,
                    label=idx, **lineprops(idx))
        ax.set(xscale="log", xlabel="Object / buffer size per op (KiB)",
               title=f"{kind} workload")
    axes[0].set_ylabel("Mean lookup latency (ns)")
    axes[0].legend(fontsize=8)
    fig.suptitle("E10: realistic post-op work vs index lookup latency")
    save(fig, outdir, "e10_object_storage")


# ------------------------------------------------------------------ E11
def plot_e11(df, outdir, sigmas):
    sig = [float(s) for s in sigmas.split()]
    sub = df[df["index"] == "hash"].reset_index(drop=True)
    n = min(len(sub), len(sig))
    if len(sub) != len(sig):
        print(f"  [e11] note: {len(sub)} rows vs {len(sig)} sigmas; "
              f"mapping first {n} in file order")
    fig, ax = plt.subplots(figsize=(8, 5))
    ax.plot(sig[:n], sub["lookup_mean_ns"][:n], "o-", lw=2,
            color=COLORS["hash"], label="hash mean")
    ax.plot(sig[:n], sub["lookup_p99_ns"][:n], "o--", lw=1.2, mfc="none",
            color=COLORS["hash"], label="hash p99")
    ax.set(xlabel="Key-bit bias sigma", ylabel="Lookup latency (ns)",
           title="E11: key-bit bias degrades hash bucket balance")
    ax.legend(fontsize=9)
    save(fig, outdir, "e11_hash_sigma")


# ------------------------------------------------------------------ E12
def plot_e12(df, outdir):
    df = df.copy()
    df.loc[df["workload"] == "none", "bytes_per_call"] = 0
    sizes = sorted(df["bytes_per_call"].unique())
    g = df.groupby(["index", "bytes_per_call", "workers"], as_index=False)[
        "throughput_mops"].mean()
    fig, axes = plt.subplots(1, len(sizes), figsize=(5.2 * len(sizes), 4.8))
    for ax, B in zip(np.atleast_1d(axes), sizes):
        sub_b = g[g["bytes_per_call"] == B]
        for idx in indexes_in(sub_b):
            sub = sub_b[sub_b["index"] == idx].sort_values("workers")
            ax.plot(sub.workers, sub.throughput_mops, label=idx,
                    **lineprops(idx))
            base = sub[sub.workers == sub.workers.min()]
            if len(base):
                ax.plot(sub.workers,
                        base.throughput_mops.iloc[0] * sub.workers
                        / base.workers.iloc[0],
                        ls=":", lw=0.8, color=COLORS.get(idx), alpha=0.5)
        ax.set(xscale="log", xlabel="Worker threads",
               title="no pollution" if B == 0
               else f"{int(B)//1024} KiB / op")
        ws = sorted(sub_b.workers.unique())
        ax.set_xticks(ws, [str(w) for w in ws])
    np.atleast_1d(axes)[0].set_ylabel("Throughput (Mops/s)")
    np.atleast_1d(axes)[0].legend(fontsize=8)
    fig.suptitle("E12: pollution neutralises lock-free design "
                 "(mix s=0.6,u=0.1,i=0.3; dotted = ideal scaling)")
    save(fig, outdir, "e12_scaling")


# ------------------------------------------------------------------ E13
def plot_e13(df, outdir, uniform_df=None):
    def prep(d):
        d = d.copy()
        d["wf"] = d["op_mix"].map(wf_of_mix)
        return d.groupby(["index", "wf"], as_index=False)[
            "throughput_mops"].mean()

    g = prep(df)
    gu = prep(uniform_df) if uniform_df is not None else None
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4.8))

    def draw(gg, ls, suffix):
        for idx in indexes_in(gg):
            sub = gg[gg["index"] == idx].sort_values("wf")
            base = sub[sub.wf == 0]
            props = lineprops(idx)
            props["lw"] = 2 if ls == "-" else 1.2
            if ls != "-":
                props["mfc"] = "none"
            ax1.plot(sub.wf, sub.throughput_mops, ls=ls,
                     label=f"{idx}{suffix}", **props)
            if len(base):
                ax2.plot(sub.wf,
                         sub.throughput_mops / base.throughput_mops.iloc[0],
                         ls=ls, label=f"{idx}{suffix}", **props)

    draw(g, "-", " (zipf)" if gu is not None else "")
    if gu is not None:
        draw(gu, "--", " (uniform)")
    ax2.axhline(1.0, color="#888", lw=0.8, ls=":")
    for ax, ylab in ((ax1, "Throughput (Mops/s)"),
                     (ax2, "Normalised to write-fraction 0")):
        ax.set_xscale("symlog", linthresh=0.001)
        ax.set_xlim(-0.0003, None)
        ax.set(xlabel="Write fraction", ylabel=ylab)
        ax.legend(fontsize=7)
    ax2.set_ylim(0, 1.15)
    fig.suptitle("E13: coherence sensitivity — drop at wf ≤ 1% ≈ "
                 "invalidation cost, not Amdahl")
    save(fig, outdir, "e13_coherence")


# ------------------------------------------------------------------ main
PLOTTERS = {
    "e1_polluter":        lambda df, o, a: (plot_e1_lookup(df, o),
                                            plot_e1_tails(df, o)),
    "e2_keycount":        lambda df, o, a: plot_e2(df, o),
    "e3_arrival_rate":    lambda df, o, a: plot_e3(df, o),
    "e4_burstiness":      lambda df, o, a: plot_e4(df, o),
    "e5_skew":            lambda df, o, a: plot_e5(df, o),
    "e6_opmix":           lambda df, o, a: plot_e6(df, o),
    "e7_clients":         lambda df, o, a: plot_e7(df, o),
    "e8_traffic_shape":   lambda df, o, a: plot_e8(df, o),
    "e9_mix_x_pollute":   lambda df, o, a: plot_e9(df, o),
    "e10_object_storage": lambda df, o, a: plot_e10(df, o),
    "e11_hash_sigma":     lambda df, o, a: plot_e11(df, o, a.sigmas),
    "e12_concurrency":    lambda df, o, a: plot_e12(df, o),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csvs", nargs="*")
    ap.add_argument("--all", metavar="DIR",
                    help="plot every known CSV found in DIR")
    ap.add_argument("--outdir", default="figs")
    ap.add_argument("--sigmas", default="0.2 0.4 0.6 0.8 1.0 1.5 2.0 3.0",
                    help="e11 sigma sweep list (CSV lacks the column)")
    args = ap.parse_args()

    paths = list(args.csvs)
    scan_dir = args.all
    if not paths and scan_dir is None:
        scan_dir = "."          # no args: scan the current directory
    if scan_dir:
        for base in list(PLOTTERS) + ["e13_coherence"]:
            p = os.path.join(scan_dir, base + ".csv")
            if os.path.exists(p):
                paths.append(p)
    if not paths:
        ap.error("no known CSVs found (looked in '%s')" % (scan_dir or ""))

    for p in paths:
        base = os.path.splitext(os.path.basename(p))[0]
        print(f"plotting {p} ...")
        df = load(p)
        if df.empty:
            print("  [skip] empty CSV")
            continue
        if base == "e13_coherence":
            upath = os.path.join(os.path.dirname(p), "e13_uniform.csv")
            uni = load(upath) if os.path.exists(upath) else None
            if uni is not None:
                print("  overlaying e13_uniform.csv")
            plot_e13(df, args.outdir, uni)
        elif base in PLOTTERS:
            PLOTTERS[base](df, args.outdir, args)
        else:
            print(f"  [skip] no plotter for '{base}'")


if __name__ == "__main__":
    main()
