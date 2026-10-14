#!/usr/bin/env python3
"""Phase 5 figures (PDF + PNG) from results/endterm/phase5/all_runs_metrics.csv.

    python3 scripts/phase5_plots.py        (run scripts/phase5_metrics.py first)

Writes results/endterm/phase5/figures/<name>.{pdf,png} and, for each figure,
the exact data it plots to results/endterm/phase5/figure_data/<name>.csv.

Style rules (applied to every figure): one y-axis per chart (two measures ->
small multiples), colour fixed per entity across all figures, thin marks,
direct labels on lines plus a legend, recessive grid, labelled axes with units.
Colours: reference categorical palette slots 1-6 in their documented order.
"""
import csv
import math
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
P5 = ROOT / "results" / "endterm" / "phase5"
FIG, DATA = P5 / "figures", P5 / "figure_data"

INK, INK2, GRID, SURFACE = "#0b0b0b", "#52514e", "#e4e3de", "#fcfcfb"
NEUTRAL = "#8a8984"                       # baselines (scalar, stride prefetcher)
COLOR = {                                  # entity -> fixed slot (palette slots 1-6)
    "rvv_orig": "#2a78d6",     # 1 blue
    "rvv_opt_m1": "#eb6834",   # 2 orange
    "rvv_acc2_m1": "#1baf7a",  # 3 aqua
    "dense": "#eda100",        # 4 yellow
    "rvv_opt_m2": "#e87ba4",   # 5 magenta
    "hybrid": "#008300",       # 6 green
    "scalar": NEUTRAL,
    "stride": NEUTRAL,
    "vip": "#2a78d6",
}
LABEL = {
    "rvv_orig": "RVV CSR (midterm)", "rvv_opt_m1": "RVV CSR, 1 accumulator",
    "rvv_acc2_m1": "RVV CSR, 2 accumulators", "dense_m2": "Dense RVV GEMV",
    "hybrid": "Density-aware hybrid", "scalar": "Scalar CSR",
}

plt.rcParams.update({
    "font.size": 9, "axes.titlesize": 10, "axes.labelsize": 9, "legend.fontsize": 8,
    "axes.edgecolor": INK2, "axes.labelcolor": INK, "xtick.color": INK2, "ytick.color": INK2,
    "text.color": INK, "axes.facecolor": SURFACE, "figure.facecolor": "white",
    "axes.spines.top": False, "axes.spines.right": False, "axes.grid": True,
    "grid.color": GRID, "grid.linewidth": 0.6, "axes.axisbelow": True,
    "lines.linewidth": 1.6, "lines.markersize": 4.5, "savefig.bbox": "tight", "savefig.dpi": 200,
})


def load():
    rows = list(csv.DictReader(open(P5 / "all_runs_metrics.csv")))
    for r in rows:
        for k, v in r.items():
            if v in ("", "None"):
                r[k] = None
            elif k not in ("phase", "group", "input", "kernel", "l1d_prefetcher", "valid", "reliable"):
                try:
                    r[k] = float(v)
                except ValueError:
                    pass
        r["valid"] = r["valid"] == "True"
    return [r for r in rows if r["valid"]]


def pick(rows, **kw):
    return [r for r in rows if all(r.get(k) == v for k, v in kw.items())]


def one(rows, **kw):
    m = pick(rows, **kw)
    return m[0] if m else None


def save(fig, name, header, table):
    FIG.mkdir(parents=True, exist_ok=True)
    DATA.mkdir(parents=True, exist_ok=True)
    for ext in ("pdf", "png"):
        fig.savefig(FIG / f"{name}.{ext}")
    plt.close(fig)
    with open(DATA / f"{name}.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(table)
    print(f"  {name}: {len(table)} data rows")


def end_label(ax, x, y, text, color, dy=0.0):
    ax.annotate(text, (x, y), xytext=(4, dy), textcoords="offset points", va="center",
                fontsize=7.5, color=INK2)
    ax.plot([], [])  # keep color cycling untouched


def density_of(inp):
    return int(inp.split("_d")[1])


# --------------------------------------------------------------- figures
def fig_speedup_vs_density(rows):
    d = pick(rows, phase="phase3", group="density", vlen=256.0)
    kernels = ["rvv_orig", "rvv_opt_m1", "rvv_acc2_m1", "dense_m2"]
    fig, ax = plt.subplots(figsize=(5.6, 3.4))
    table = []
    for k in kernels:
        pts = sorted((density_of(r["input"]), r["speedup_vs_scalar"]) for r in pick(d, kernel=k))
        xs, ys = zip(*pts)
        c = COLOR["dense" if k.startswith("dense") else k]
        ax.plot(xs, ys, marker="o", color=c, label=LABEL[k])
        end_label(ax, xs[-1], ys[-1], LABEL[k].replace("RVV CSR", "CSR"), c)
        table += [[k, x, y] for x, y in pts]
    ax.axhline(1.0, color=NEUTRAL, lw=1, ls="--")
    ax.annotate("scalar CSR = 1×", (1, 1.0), xytext=(2, -9), textcoords="offset points", fontsize=7.5, color=INK2)
    ax.set_xscale("log")
    ax.set_xticks([1, 2, 5, 10, 20, 30, 50, 80])
    ax.set_xticklabels(["1", "2", "5", "10", "20", "30", "50", "80"])
    ax.set_xlabel("Matrix density (% nonzeros), 1024 × 1024, uniform")
    ax.set_ylabel("Speedup over scalar CSR (×)")
    ax.set_title("RVV SpMV speedup vs. density (gem5 O3, VLEN = 256)", loc="left")
    ax.set_xlim(0.9, 140)
    ax.legend(frameon=False, loc="upper left")
    save(fig, "speedup_vs_density", ["kernel", "density_pct", "speedup_vs_scalar"], table)


def fig_crossover(rows):
    d = pick(rows, phase="phase4a", group="density")
    series = [("rvv_orig", COLOR["rvv_orig"]), ("dense_m2", COLOR["dense"]), ("hybrid", COLOR["hybrid"])]
    fig, ax = plt.subplots(figsize=(5.6, 3.4))
    table, data = [], {}
    for k, c in series:
        pts = sorted((density_of(r["input"]), r["roi_cycles"] / 1e6) for r in pick(d, kernel=k))
        data[k] = dict(pts)
        xs, ys = zip(*pts)
        ls = (0, (1.5, 1.5)) if k == "hybrid" else "-"
        ax.plot(xs, ys, marker="o", color=c, ls=ls, label=LABEL[k], zorder=3 if k == "hybrid" else 2)
        table += [[k, x, y * 1e6] for x, y in pts]
    # crossover of CSR vs dense, linear interpolation of the time ratio
    ds = sorted(data["rvv_orig"])
    ratio = [(x, data["rvv_orig"][x] / data["dense_m2"][x]) for x in ds]
    cross = None
    for (x0, r0), (x1, r1) in zip(ratio, ratio[1:]):
        if r0 < 1.0 <= r1:
            cross = x0 + (1.0 - r0) * (x1 - x0) / (r1 - r0)
    if cross:
        ax.axvline(cross, color=INK2, lw=0.8, ls=":")
        ax.annotate(f"crossover ≈ {cross:.0f}% density\n(CSR faster to the left,\ndense faster to the right)",
                    (cross, 0.12), xytext=(6, 0), textcoords="offset points", ha="left", fontsize=7.5, color=INK2)
    for k, c in series[:2]:
        x_end = max(data[k])
        end_label(ax, x_end, data[k][x_end], {"rvv_orig": "CSR", "dense_m2": "Dense"}[k], c)
    ax.set_xlabel("Matrix density (% nonzeros), 1024 × 1024, uniform")
    ax.set_ylabel("Kernel time (million cycles)")
    ax.set_title("Dense vs. sparse execution, and the density-aware hybrid", loc="left")
    ax.legend(frameon=False, loc="upper left")
    ax.set_xlim(0, 92)
    save(fig, "dense_vs_sparse_crossover", ["kernel", "density_pct", "roi_cycles"], table + ([["crossover_interpolated", cross, ""]] if cross else []))


def fig_lane_util(rows):
    groups = [("density", "Uniform synthetic", COLOR["rvv_orig"]),
              ("powerlaw", "Power-law synthetic", COLOR["rvv_opt_m1"]),
              ("size", "Size sweep (synthetic)", COLOR["rvv_orig"]),
              ("suitesparse", "SuiteSparse", COLOR["rvv_acc2_m1"])]
    fig, ax = plt.subplots(figsize=(5.6, 3.4))
    table, seen = [], set()
    for g, lab, c in groups:
        pts = [(r["row_len_mean"], r["lane_util"] * 100, r["input"]) for r in
               pick(rows, phase="phase3", group=g, kernel="rvv_orig", vlen=256.0) if r["lane_util"] is not None]
        if g == "size":
            lab = "Uniform synthetic" if "Uniform synthetic" not in seen else None
        if not pts:
            continue
        xs, ys, names = zip(*pts)
        ax.scatter(xs, ys, s=26, color=c, edgecolor="white", linewidth=0.8, label=lab, zorder=3)
        if lab:
            seen.add(lab)
        table += [[g, n, x, y] for x, y, n in pts]
        if g == "suitesparse":
            for x, y, n in pts:
                ax.annotate(n, (x, y), xytext=(5, -3), textcoords="offset points", fontsize=7, color=INK2)
    ax.set_xscale("log")
    ax.set_xlabel("Mean nonzeros per row (log scale)")
    ax.set_ylabel("Vector lane utilisation (%)")
    ax.set_title("Lane utilisation of the RVV CSR kernel vs. row length (VLMAX = 8)", loc="left")
    ax.set_ylim(0, 105)
    ax.legend(frameon=False, loc="lower right")
    save(fig, "lane_util_vs_row_length", ["group", "input", "row_len_mean", "lane_util_pct"], table)


def vip_inputs(rows):
    order = [("size", f"u_r16384_c{c}_npr16", f"uniform, x={int(c) * 4 // 1024} KiB") for c in
             ("002048", "008192", "032768", "131072", "524288")]
    order += [("size", f"p_r16384_c{c}_npr16", f"power-law, x={int(c) * 4 // 1024} KiB") for c in
              ("131072", "524288")]
    order += [("suitesparse", m, m) for m in ("bcsstk17", "scircuit", "web-NotreDame", "luxembourg_osm")]
    return order


def fig_vip_memory(rows):
    d = pick(rows, phase="phase4b", kernel="rvv_orig")
    inputs = vip_inputs(rows)
    metrics = [("l1d_primary_miss_rate", "L1D primary miss rate (%)", 100),
               ("dram_bytes_read", "DRAM read traffic (MiB)", 1 / 2**20),
               ("lq_full_events", "LQ-full events (×1000)", 1e-3)]
    fig, axes = plt.subplots(len(metrics), 1, figsize=(6.4, 6.8), sharex=True)
    table = []
    xs = range(len(inputs))
    w = 0.38
    for ax, (m, lab, scale) in zip(axes, metrics):
        for j, (cfg, name) in enumerate((("stride", "Stride prefetcher (baseline)"), ("vip", "Vector-Indirect Prefetcher"))):
            vals = []
            for g, i, _ in inputs:
                r = one(d, group=g, input=i, l1d_prefetcher=cfg)
                vals.append(r[m] * scale if r and r[m] is not None else math.nan)
                table.append([m, i, cfg, r[m] if r else ""])
            ax.bar([x + (j - 0.5) * w for x in xs], vals, width=w - 0.04, color=COLOR[cfg], label=name)
        ax.set_ylabel(lab)
    h, l = axes[0].get_legend_handles_labels()
    fig.legend(h, l, frameon=False, loc="upper left", ncol=2, bbox_to_anchor=(0.08, 0.955))
    fig.suptitle("Memory behaviour with and without the Vector-Indirect Prefetcher (RVV CSR kernel)",
                 x=0.01, ha="left", fontsize=10)
    fig.subplots_adjust(top=0.9)
    axes[-1].set_xticks(list(xs))
    axes[-1].set_xticklabels([n for _, _, n in inputs], rotation=35, ha="right")
    save(fig, "vip_memory_behaviour", ["metric", "input", "l1d_prefetcher", "value"], table)


def fig_vip_speedup(rows):
    d = pick(rows, phase="phase4b")
    inputs = vip_inputs(rows)
    kernels = ["rvv_orig", "rvv_opt_m1", "rvv_acc2_m1", "scalar"]
    fig, ax = plt.subplots(figsize=(6.4, 3.6))
    w = 0.2
    table = []
    for j, k in enumerate(kernels):
        vals = []
        for g, i, _ in inputs:
            s, v = one(d, group=g, input=i, kernel=k, l1d_prefetcher="stride"), one(d, group=g, input=i, kernel=k, l1d_prefetcher="vip")
            sp = s["roi_cycles"] / v["roi_cycles"] if s and v else math.nan
            vals.append(sp)
            table.append([k, i, s["roi_cycles"] if s else "", v["roi_cycles"] if v else "", sp])
        ax.bar([x + (j - 1.5) * w for x in range(len(inputs))], vals, width=w - 0.03,
               color=COLOR[k], label=LABEL[k])
    ax.axhline(1.0, color=INK2, lw=0.8)
    ax.set_xticks(range(len(inputs)))
    ax.set_xticklabels([n for _, _, n in inputs], rotation=35, ha="right")
    ax.set_ylabel("Speedup of VIP over stride prefetcher (×)")
    ax.set_title("Vector-Indirect Prefetcher speedup (gem5 O3, VLEN = 256)", loc="left")
    ax.legend(frameon=False, loc="upper left", ncol=2)
    save(fig, "vip_speedup", ["kernel", "input", "stride_cycles", "vip_cycles", "speedup"], table)


def fig_vlen(rows):
    panels = [("u_n1024_d20", "density", "Uniform 1024², 20% (x cached)"),
              ("u_r16384_c524288_npr16", "size", "Uniform, x = 2 MiB (gathers miss)"),
              ("scircuit", "suitesparse", "scircuit (SuiteSparse)")]
    kernels = ["rvv_orig", "rvv_opt_m1", "rvv_acc2_m1"]
    fig, axes = plt.subplots(1, 3, figsize=(7.6, 2.9), sharey=True)
    table = []
    for ax, (inp, g256, title) in zip(axes, panels):
        for k in kernels:
            pts = []
            for v in (128, 256, 512):
                r = one(rows, phase="phase3", group=g256 if v == 256 else "vlen", input=inp, kernel=k, vlen=float(v))
                if r:
                    pts.append((v, r["roi_cycles"]))
            if len(pts) < 2:
                continue
            base = dict(pts).get(256)
            xs = [p[0] for p in pts]
            ys = [base / p[1] for p in pts]
            ax.plot(xs, ys, marker="o", color=COLOR[k], label=LABEL[k])
            table += [[inp, k, x, c, base / c] for x, c in pts]
        ax.set_xscale("log", base=2)
        ax.set_xticks([128, 256, 512])
        ax.set_xticklabels(["128", "256", "512"])
        ax.set_title(title, loc="left", fontsize=8.5)
        ax.set_xlabel("VLEN (bits)")
    axes[0].set_ylabel("Speedup relative to VLEN = 256 (×)")
    axes[0].legend(frameon=False, loc="upper left", fontsize=7)
    fig.suptitle("VLEN sensitivity of the RVV CSR kernels (LMUL = 1)", x=0.01, ha="left", fontsize=10)
    fig.subplots_adjust(top=0.8)
    save(fig, "vlen_sensitivity", ["input", "kernel", "vlen", "roi_cycles", "speedup_vs_vlen256"], table)


def fig_lmul(rows):
    d = pick(rows, phase="phase3", group="density", vlen=256.0)
    dens = [5, 20, 50]
    kernels = [("dense_m1", "LMUL 1"), ("dense_m2", "LMUL 2"), ("dense_m4", "LMUL 4")]
    fig, ax = plt.subplots(figsize=(5.6, 3.2))
    w = 0.26
    table = []
    shades = ["#f6c757", "#eda100", "#b37a00"]   # one hue (slot 4) light->dark: magnitude of LMUL
    for j, ((k, lab), c) in enumerate(zip(kernels, shades)):
        vals = []
        for dd in dens:
            r = one(d, input=f"u_n1024_d{dd:02d}", kernel=k)
            vals.append(r["roi_cycles"] / 1e6 if r else math.nan)
            table.append([k, dd, r["roi_cycles"] if r else ""])
        ax.bar([x + (j - 1) * w for x in range(len(dens))], vals, width=w - 0.03, color=c, label=f"Dense GEMV, {lab}")
    ax.set_xticks(range(len(dens)))
    ax.set_xticklabels([f"{x}%" for x in dens])
    ax.set_xlabel("Matrix density (1024 × 1024; dense kernels read all 1M elements)")
    ax.set_ylabel("Kernel time (million cycles)")
    ax.set_title("LMUL sensitivity of the dense kernel (VLEN = 256)", loc="left")
    ax.set_ylim(0, 2.9)
    ax.legend(frameon=False, loc="upper center", ncol=3, bbox_to_anchor=(0.5, 1.0))
    fig.text(0.01, -0.06, "LMUL 4 issues 128-byte loads; its slowdown is a gem5 stride-prefetcher training failure "
             "(see docs/gem5_rvv_issues.md), not a vector-unit limit.", fontsize=7, color=INK2)
    save(fig, "lmul_sensitivity_dense", ["kernel", "density_pct", "roi_cycles"], table)


def fig_history(rows):
    d = pick(rows, phase="phase4b", group="history")
    if not d:
        print("  history_depth: no data yet (scripts/run_phase4b_extra.sh)")
        return
    base = pick(rows, phase="phase4b", group="size", l1d_prefetcher="stride")
    inputs = sorted({r["input"] for r in d})
    fig, axes = plt.subplots(1, len(inputs), figsize=(7.6, 2.9), sharey=True)
    axes = axes if len(inputs) > 1 else [axes]
    table = []
    for ax, inp in zip(axes, inputs):
        for k in ("rvv_orig", "rvv_opt_m1", "rvv_acc2_m1"):
            s = one(base, input=inp, kernel=k)
            pts = sorted((int(r["l1d_prefetcher"].split("_h")[1]), s["roi_cycles"] / r["roi_cycles"])
                         for r in pick(d, input=inp, kernel=k) if s)
            if pts:
                xs, ys = zip(*pts)
                ax.plot(xs, ys, marker="o", color=COLOR[k], label=LABEL[k])
                table += [[inp, k, x, y] for x, y in pts]
        ax.axhline(1.0, color=INK2, lw=0.8)
        ax.set_xscale("log", base=2)
        ax.set_xticks([4, 8, 16, 32, 64, 128])
        ax.set_xticklabels(["4", "8", "16", "32", "64", "128"])
        ax.set_xlabel("History depth (index vectors)", fontsize=8)
        ax.set_title(f"x = {int(inp.split('_c')[1].split('_')[0]) * 4 // 1024} KiB (uniform)", loc="left", fontsize=8.5)
    axes[0].set_ylabel("Speedup over stride prefetcher (×)")
    axes[0].legend(frameon=False, loc="upper left", fontsize=7)
    fig.suptitle("VIP history-depth sensitivity", x=0.01, ha="left", fontsize=10)
    fig.subplots_adjust(top=0.8)
    save(fig, "vip_history_depth", ["input", "kernel", "history_depth", "speedup_vs_stride"], table)


def main():
    rows = load()
    print(f"{len(rows)} validated runs")
    fig_speedup_vs_density(rows)
    fig_crossover(rows)
    fig_lane_util(rows)
    fig_vip_speedup(rows)
    fig_vip_memory(rows)
    fig_vlen(rows)
    fig_lmul(rows)
    fig_history(rows)


if __name__ == "__main__":
    main()
