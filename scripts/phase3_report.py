#!/usr/bin/env python3
"""Phase 3 report: collects every run under results/endterm/phase3/ and writes

  results/endterm/phase3/phase3_all.csv    one row per run (all metrics)
  results/endterm/phase3/phase3_summary.md tables per experiment group

Only runs that exited cleanly AND validated are used in any comparison;
failures are listed explicitly. Speedups are ROI-cycle ratios between runs on
the identical input (same group, input id and VLEN).
"""
import csv
import math
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from collect import collect, ok  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
P3 = ROOT / "results" / "endterm" / "phase3"

SPARSE_RVV = ["rvv_orig", "rvv_opt_m1", "rvv_opt_m2", "rvv_acc2_m1", "rvv_acc2_m2"]
# Kernels whose gathers run at LMUL=1. LMUL=2 gather kernels (rvv_opt_m2,
# rvv_acc2_m2) give intermittently wrong results on gem5 25.1 O3
# (docs/gem5_rvv_issues.md, Issue 2), so their timing is not used for claims.
RELIABLE_SPARSE = ["rvv_orig", "rvv_opt_m1", "rvv_acc2_m1"]
DENSE = ["dense_m1", "dense_m2", "dense_m4"]
SPMM = ["spmm_scalar", "spmm_rvv_vk", "spmm_rvv_colwise"]


def load_all():
    recs = []
    for log in sorted(P3.rglob("run.log")):
        d = log.parent
        rel = d.relative_to(P3).parts
        if rel[0].startswith("gem5_"):
            continue  # bug-investigation runs, not sweep data
        if "EXIT_STATUS=" not in log.read_text(errors="replace"):
            continue  # still running (or killed before finishing): not a result
        r = collect(d)
        r["group"] = rel[0]
        if rel[0] == "vlen":
            r["vlen"], r["input"], r["kernel"] = int(rel[1][4:]), rel[2], rel[3]
        else:
            r["vlen"], r["input"], r["kernel"] = r.get("sim_vlen", 256), rel[1], rel[2]
        r["valid"] = ok(r)
        recs.append(r)
    return recs


def fmt(v, kind=""):
    if v is None or (isinstance(v, float) and math.isnan(v)):
        return "–"
    if kind == "pct":
        return f"{v * 100:.2f}%"
    if kind == "x":
        return f"{v:.2f}×"
    if isinstance(v, float):
        return f"{v:.3g}" if abs(v) < 10 else f"{v:,.1f}" if abs(v) < 1000 else f"{v:,.0f}"
    if isinstance(v, int):
        return f"{v:,}"
    return str(v)


def table(header, rows):
    out = ["| " + " | ".join(header) + " |", "|" + "|".join("---:" if i else "---" for i in range(len(header))) + "|"]
    out += ["| " + " | ".join(r) + " |" for r in rows]
    return out


def main():
    recs = load_all()
    if not recs:
        sys.exit("no Phase 3 runs found")
    # index: (group, vlen, input) -> kernel -> record
    idx = defaultdict(dict)
    for r in recs:
        idx[(r["group"], r["vlen"], r["input"])][r["kernel"]] = r

    def cyc(g, v, i, k):
        r = idx.get((g, v, i), {}).get(k)
        return r["roi_cycles"] if r and r["valid"] else None

    def rec(g, v, i, k):
        r = idx.get((g, v, i), {}).get(k)
        return r if r and r["valid"] else None

    md = ["# Phase 3 results", "",
          "All numbers are ROI-only (kernel) metrics from gem5 stats, O3 CPU @ 1 GHz unless stated.",
          "Speedups are ROI-cycle ratios on identical inputs. `–` = run missing or failed validation.", "",
          "**Reliability note:** kernels with LMUL=2 gathers (`rvv_opt_m2`, `rvv_acc2_m2`) produce",
          "intermittently wrong results on gem5 25.1 O3 (docs/gem5_rvv_issues.md, Issue 2). Their runs",
          "that validated are shown for completeness, but claims use only scalar, LMUL=1-gather",
          f"({', '.join(RELIABLE_SPARSE)}), dense (unit-stride) and SpMM kernels.", ""]

    # ---------------------------------------------------------------- status
    incomplete = sum(1 for log in P3.rglob("run.log")
                     if "EXIT_STATUS=" not in log.read_text(errors="replace")
                     and not log.relative_to(P3).parts[0].startswith("gem5_"))
    md += ["## Run status", "", f"Incomplete (running or interrupted, not counted): {incomplete}", ""]
    by_group = defaultdict(lambda: [0, 0])
    failures = []
    for r in recs:
        by_group[r["group"]][0 if r["valid"] else 1] += 1
        if not r["valid"]:
            failures.append(r)
    md += table(["group", "validated", "failed"],
                [[g, str(a), str(b)] for g, (a, b) in sorted(by_group.items())])
    if failures:
        md += ["", "Failed runs (excluded everywhere):", ""]
        for r in failures:
            md.append(f"- `{Path(r['run']).relative_to(ROOT)}`: exit={r.get('exit_status')} "
                      f"validation={r.get('res_validation')} max_rel_err={r.get('res_max_rel_err')} "
                      f"{r.get('error', '')}")
    hs = [r["host_seconds"] for r in recs if r.get("host_seconds") is not None]
    if hs:
        md += ["", f"Host time per run: max {max(hs)} s, median {sorted(hs)[len(hs) // 2]} s "
               f"(limit 600 s; {sum(h > 600 for h in hs)} runs over)."]

    # ---------------------------------------------------------------- density
    dens = sorted({i for (g, v, i) in idx if g == "density" and v == 256})
    if dens:
        md += ["", "## Density sweep (uniform, 1024×1024, VLEN=256): dense vs. sparse crossover", ""]
        ks = ["scalar"] + SPARSE_RVV + DENSE
        rows, cross = [], {"reliable": [], "all": []}
        for i in dens:
            d = int(i.split("_d")[1])
            c = {k: cyc("density", 256, i, k) for k in ks}
            dn = {k: c[k] for k in DENSE if c[k]}
            bd = min(dn, key=dn.get) if dn else None
            cells = []
            for name, pool in (("reliable", RELIABLE_SPARSE), ("all", SPARSE_RVV)):
                sp = {k: c[k] for k in pool if c[k]}
                bs = min(sp, key=sp.get) if sp else None
                ratio = sp[bs] / dn[bd] if bs and bd else None  # >1: dense faster
                cross[name].append((d, ratio, bs))
                cells += [bs or "–", fmt(ratio, "x")]
            rows.append([f"{d}%"] + [fmt(c[k]) for k in ks] + [bd or "–"] + cells)
        md += table(["density"] + ks + ["best dense", "best reliable sparse (m1 gathers)", "sparse/dense time",
                                        "best of all validated sparse", "sparse/dense time"], rows)
        md += ["", "`sparse/dense time` < 1 means the sparse kernel is faster.", ""]
        for name, label in (("reliable", "Best reliable sparse kernel (LMUL=1 gathers; used for claims)"),
                            ("all", "Best of all validated sparse kernels (includes LMUL=2 gathers; not used for claims)")):
            prev, found = None, False
            for d, r, bs in cross[name]:
                if r is None:
                    continue
                if prev and prev[1] < 1.0 <= r:
                    d0, r0, b0 = prev
                    x = d0 + (1.0 - r0) * (d - d0) / (r - r0)
                    md.append(f"- **{label}:** sparse is faster up to {d0}% density ({b0}, time ratio "
                              f"{r0:.3f}) and slower from {d}% ({bs}, ratio {r:.3f}); linear interpolation "
                              f"of the ratio puts the crossover at ≈{x:.1f}% density.")
                    found = True
                prev = (d, r, bs)
            if not found:
                md.append(f"- **{label}:** no crossover in the measured range.")

        md += ["", "Speedup over scalar CSR, and lane utilisation of the m1 kernels:", ""]
        rows = []
        for i in dens:
            s = cyc("density", 256, i, "scalar")
            r1 = rec("density", 256, i, "rvv_orig")
            rows.append([f"{int(i.split('_d')[1])}%"] +
                        [fmt(s / cyc("density", 256, i, k), "x") if s and cyc("density", 256, i, k) else "–"
                         for k in SPARSE_RVV] +
                        [fmt(r1.get("mat_row_len_mean")) if r1 else "–",
                         fmt(r1.get("vec_lane_util"), "pct") if r1 else "–"])
        md += table(["density"] + SPARSE_RVV + ["mean row len", "lane util (m1)"], rows)

        md += ["", "Two-accumulator test (Phase 2 hypothesis: the single vfmacc chain limits rvv_opt):", ""]
        rows = []
        for i in dens:
            a, b = cyc("density", 256, i, "rvv_opt_m1"), cyc("density", 256, i, "rvv_acc2_m1")
            c2, d2 = cyc("density", 256, i, "rvv_opt_m2"), cyc("density", 256, i, "rvv_acc2_m2")
            o = cyc("density", 256, i, "rvv_orig")
            rows.append([f"{int(i.split('_d')[1])}%", fmt(o), fmt(a), fmt(b),
                         fmt(a / b, "x") if a and b else "–", fmt(c2), fmt(d2),
                         fmt(c2 / d2, "x") if c2 and d2 else "–"])
        md += table(["density", "rvv_orig", "opt_m1", "acc2_m1", "acc2_m1 speedup over opt_m1",
                     "opt_m2", "acc2_m2", "acc2_m2 speedup over opt_m2"], rows)

    # ---------------------------------------------------------------- powerlaw
    pls = sorted({i for (g, v, i) in idx if g == "powerlaw"})
    if pls:
        md += ["", "## Row-length skew: power-law (α=1.8) vs. uniform, 1024×1024", "",
               "Cycles (power-law / uniform time ratio in brackets; >1 = skew costs time).", ""]
        ks = ["scalar"] + SPARSE_RVV
        rows = []
        for i in pls:
            u = "u" + i[1:]
            rp, ru = rec("powerlaw", 256, i, "rvv_orig"), rec("density", 256, u, "rvv_orig")
            row = [f"{int(i.split('_d')[1])}%",
                   f"{fmt(ru.get('mat_row_len_cv')) if ru else '–'} → {fmt(rp.get('mat_row_len_cv')) if rp else '–'}",
                   f"{fmt(ru.get('vec_lane_util'), 'pct') if ru else '–'} → {fmt(rp.get('vec_lane_util'), 'pct') if rp else '–'}"]
            for k in ks:
                p_, u_ = cyc("powerlaw", 256, i, k), cyc("density", 256, u, k)
                row.append(f"{fmt(p_)} ({p_ / u_:.2f})" if p_ and u_ else fmt(p_))
            rows.append(row)
        md += table(["density", "row-len CV u→p", "lane util m1 u→p"] + ks, rows)

    # ---------------------------------------------------------------- size
    sizes = sorted({i for (g, v, i) in idx if g == "size"}, key=lambda s: (s[0], int(s.split("_c")[1].split("_")[0])))
    if sizes:
        md += ["", "## Size sweep: 16,384 rows × 16 nnz/row; x grows past L1D (32 KiB) and L2 (512 KiB)", ""]
        ks = ["scalar"] + SPARSE_RVV
        rows = []
        for i in sizes:
            r0 = rec("size", 256, i, "scalar") or rec("size", 256, i, "rvv_orig")
            s = cyc("size", 256, i, "scalar")
            best = min((k for k in SPARSE_RVV if cyc("size", 256, i, k)), key=lambda k: cyc("size", 256, i, k), default=None)
            rows.append([("uniform" if i[0] == "u" else "power-law"),
                         fmt(r0.get("mat_x_bytes") // 1024) + " KiB" if r0 else "–",
                         fmt(r0.get("mat_row_len_cv")) if r0 else "–"] +
                        [fmt(cyc("size", 256, i, k)) for k in ks] +
                        [f"{best} {s / cyc('size', 256, i, best):.2f}×" if best and s else "–"])
        md += table(["dist", "x size", "row CV"] + ks + ["best RVV vs scalar"], rows)
        md += ["", "Memory behaviour (ROI) of scalar and rvv_opt_m2:", ""]
        rows = []
        for i in sizes:
            for k in ("scalar", "rvv_orig", "rvv_opt_m2"):
                r = rec("size", 256, i, k)
                if not r:
                    continue
                rows.append([("u" if i[0] == "u" else "p") + " " + fmt(r.get("mat_x_bytes") // 1024) + " KiB", k,
                             fmt(r["roi_cycles"]), fmt(r["roi_l1d_primary_miss_rate"], "pct"),
                             fmt(r["roi_l1d_mpki"]), fmt(r["roi_l2_miss_rate"], "pct"), fmt(r["roi_l2_mpki"]),
                             fmt(r["roi_dram_bytes_read"] / 1024) + " KiB", fmt(r["roi_lq_full_events"])])
        md += table(["input (x size)", "kernel", "cycles", "L1D primary miss", "L1D MPKI",
                     "L2 miss rate", "L2 MPKI", "DRAM read", "LQ-full events"], rows)

    # ---------------------------------------------------------------- suitesparse
    ss = sorted({i for (g, v, i) in idx if g == "suitesparse"})
    if ss:
        md += ["", "## SuiteSparse matrices (VLEN=256)", ""]
        ks = ["scalar"] + SPARSE_RVV
        rows = []
        for i in ss:
            r0 = rec("suitesparse", 256, i, "rvv_orig") or rec("suitesparse", 256, i, "scalar")
            s = cyc("suitesparse", 256, i, "scalar")
            rows.append([i, fmt(r0.get("mat_rows")) if r0 else "–", fmt(r0.get("mat_nnz")) if r0 else "–",
                         fmt(r0.get("mat_row_len_mean")) if r0 else "–", fmt(r0.get("mat_row_len_cv")) if r0 else "–",
                         fmt(r0.get("vec_lane_util"), "pct") if r0 and "vec_lane_util" in r0 else "–"] +
                        [f"{fmt(cyc('suitesparse', 256, i, k))}" + (f" ({s / cyc('suitesparse', 256, i, k):.2f}×)"
                         if s and cyc('suitesparse', 256, i, k) and k != "scalar" else "") for k in ks])
        md += table(["matrix", "rows", "nnz", "mean row", "row CV", "lane util m1"] + ks, rows)
        md += ["", "(speedup vs. scalar in brackets)"]

    # ---------------------------------------------------------------- spmm
    sp = sorted({i for (g, v, i) in idx if g == "spmm"})
    if sp:
        md += ["", "## SpMM (Y = A·X, X has K dense columns), VLEN=256", ""]
        rows = []
        for i in sp:
            s = cyc("spmm", 256, i, "spmm_scalar")
            r0 = rec("spmm", 256, i, "spmm_scalar") or rec("spmm", 256, i, "spmm_rvv_vk")
            nnzk = r0["mat_nnz"] * r0["cfg_k"] if r0 else None
            row = [i.rsplit("_k", 1)[0], str(int(i.rsplit("_k", 1)[1]))]
            for k in SPMM:
                c = cyc("spmm", 256, i, k)
                row.append(fmt(c) + (f" ({s / c:.2f}×)" if s and c and k != "spmm_scalar" else ""))
            row += [fmt(cyc("spmm", 256, i, k) / nnzk) if nnzk and cyc("spmm", 256, i, k) else "–" for k in SPMM]
            rv = rec("spmm", 256, i, "spmm_rvv_vk")
            row.append(fmt(rv.get("vec_lane_util"), "pct") if rv else "–")
            rows.append(row)
        md += table(["input", "K"] + SPMM + [f"{k} cyc/(nnz·K)" for k in SPMM] + ["vk lane util"], rows)

    # ---------------------------------------------------------------- vlen
    vl = sorted({(i) for (g, v, i) in idx if g == "vlen"})
    if vl:
        md += ["", "## VLEN sensitivity (cycles; speedup relative to VLEN=256 in brackets)", ""]
        base_group = {"u_n1024": "density", "u_r16384": "size", "scircuit": "suitesparse"}
        rows = []
        for i in vl:
            g256 = next(v for p, v in base_group.items() if i.startswith(p))
            for k in sorted({k for (g, v, ii), m in idx.items() if g == "vlen" and ii == i for k in m}):
                c256 = cyc(g256, 256, i, k)
                cells = []
                for v in (128, 256, 512):
                    c = c256 if v == 256 else cyc("vlen", v, i, k)
                    cells.append(fmt(c) + (f" ({c256 / c:.2f}×)" if c and c256 and v != 256 else ""))
                rows.append([i, k] + cells)
        md += table(["input", "kernel", "VLEN=128", "VLEN=256", "VLEN=512"], rows)

    # ---------------------------------------------------------------- CSV
    cols = ["group", "input", "kernel", "vlen", "valid", "exit_status", "res_validation", "res_max_rel_err",
            "host_seconds", "cfg_gen", "cfg_rows", "cfg_cols", "cfg_density_pct", "cfg_nnz_per_row", "cfg_alpha",
            "cfg_seed", "cfg_path", "cfg_k", "mat_nnz", "mat_density_actual", "mat_row_len_mean",
            "mat_row_len_max", "mat_row_len_cv", "mat_empty_rows", "mat_csr_bytes", "mat_x_bytes",
            "vec_lmul", "vec_vlmax", "vec_chunks", "vec_reg_chunks", "vec_active_elems", "vec_useful_elems",
            "vec_lane_util"]
    extra = sorted({k for r in recs for k in r if k.startswith(("roi_", "op_"))})
    with open(P3 / "phase3_all.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols + extra, extrasaction="ignore")
        w.writeheader()
        for r in sorted(recs, key=lambda r: (r["group"], r["vlen"], r["input"], r["kernel"])):
            w.writerow(r)

    (P3 / "phase3_summary.md").write_text("\n".join(md) + "\n")
    print("\n".join(md))


if __name__ == "__main__":
    main()
