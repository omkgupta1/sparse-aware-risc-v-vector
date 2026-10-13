#!/usr/bin/env python3
"""Phase 4A report: software sparse-aware mechanisms.

Reads results/endterm/phase4a/{density,mixed}/<input>/<kernel>/ and writes
results/endterm/phase4a/phase4a_all.csv and phase4a_summary.md.

All comparisons are between runs of the SAME binary on the SAME input; only
runs that exited cleanly and validated are used.
"""
import csv
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from collect import collect, ok  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
P4 = ROOT / "results" / "endterm" / "phase4a"


def fmt(v, kind=""):
    if v is None:
        return "–"
    if kind == "x":
        return f"{v:.2f}×"
    if kind == "pct":
        return f"{v * 100:.1f}%"
    if isinstance(v, float):
        return f"{v:.3g}" if abs(v) < 10 else f"{v:,.0f}"
    if isinstance(v, int):
        return f"{v:,}"
    return str(v)


def table(header, rows):
    out = ["| " + " | ".join(header) + " |",
           "|" + "|".join("---:" if i else "---" for i in range(len(header))) + "|"]
    return out + ["| " + " | ".join(r) + " |" for r in rows]


def main():
    idx = defaultdict(dict)
    recs = []
    for log in sorted(P4.rglob("run.log")):
        if "EXIT_STATUS=" not in log.read_text(errors="replace"):
            continue
        group, inp, kern = log.parent.relative_to(P4).parts
        r = collect(log.parent)
        r.update(group=group, input=inp, kernel=kern, valid=ok(r))
        recs.append(r)
        idx[(group, inp)][kern] = r
    if not recs:
        sys.exit("no Phase 4A runs found")

    def c(g, i, k):
        r = idx[(g, i)].get(k)
        return r["roi_cycles"] if r and r["valid"] else None

    md = ["# Phase 4A results: software sparse-aware mechanisms", "",
          "ROI cycles from gem5 (O3, VLEN=256). Every kernel validated against the scalar",
          "double-precision reference unless listed under failures. Speedup = baseline cycles /",
          "kernel cycles on the identical input with the identical binary.", ""]

    bad = [r for r in recs if not r["valid"]]
    md += [f"Runs: {len(recs)} completed, {len(recs) - len(bad)} validated, {len(bad)} failed."]
    for r in bad:
        md.append(f"- FAILED `{Path(r['run']).relative_to(ROOT)}` validation={r.get('res_validation')} "
                  f"exit={r.get('exit_status')} max_rel_err={r.get('res_max_rel_err')}")

    # ------------------------------------------------------------ density
    dens = sorted(i for (g, i) in idx if g == "density")
    if dens:
        ks = ["scalar", "rvv_orig", "dense_m1", "dense_m2", "dense_skip_m1", "dense_skip_m2",
              "dense_compress_m1", "hybrid"]
        md += ["", "## A. Uniform 1024×1024 density sweep", "", "ROI cycles:", ""]
        md += table(["density"] + ks,
                    [[i.split("_d")[1].lstrip("0") + "%"] + [fmt(c("density", i, k)) for k in ks] for i in dens])

        md += ["", "Mask skipping and compaction vs. the plain dense kernel of the same LMUL "
               "(>1× = faster), and work actually done:", ""]
        rows = []
        for i in dens:
            g = "density"
            sk = idx[(g, i)].get("dense_skip_m1")
            cp = idx[(g, i)].get("dense_compress_m1")
            def sp(a, b):
                return fmt(c(g, i, a) / c(g, i, b), "x") if c(g, i, a) and c(g, i, b) else "–"
            rows.append([i.split("_d")[1].lstrip("0") + "%",
                         sp("dense_m1", "dense_skip_m1"), sp("dense_m2", "dense_skip_m2"),
                         sp("dense_m1", "dense_compress_m1"),
                         fmt(sk["work_skipped_chunks"] / sk["work_scanned_chunks"], "pct") if sk else "–",
                         fmt(sk["work_useful_frac"], "pct") if sk else "–",
                         fmt(cp["work_useful_frac"], "pct") if cp else "–",
                         sp("rvv_orig", "dense_compress_m1")])
        md += table(["density", "skip_m1 vs dense_m1", "skip_m2 vs dense_m2", "compress_m1 vs dense_m1",
                     "chunks skipped (m1)", "useful lanes / issued (skip_m1)",
                     "useful lanes / issued (compress)", "compress vs rvv_orig (CSR)"], rows)

        md += ["", "Density-aware hybrid (threshold 58.2% = Phase 3 crossover) vs. the faster of the "
               "two single-format kernels it chooses between (oracle):", ""]
        rows = []
        for i in dens:
            g = "density"
            h, s, d = c(g, i, "hybrid"), c(g, i, "rvv_orig"), c(g, i, "dense_m2")
            hr = idx[(g, i)].get("hybrid")
            best = min(x for x in (s, d) if x) if (s or d) else None
            rows.append([i.split("_d")[1].lstrip("0") + "%", fmt(s), fmt(d), fmt(h),
                         f"{hr['hyb_dense_blocks']}/{hr['hyb_blocks']}" if hr else "–",
                         fmt(best / h, "x") if best and h else "–"])
        md += table(["density", "rvv_orig (CSR)", "dense_m2", "hybrid", "dense blocks", "hybrid vs best single"],
                    rows)

    # ------------------------------------------------------------ mixed
    mixed = sorted(i for (g, i) in idx if g == "mixed")
    if mixed:
        md += ["", "## B. Mixed-density matrices (1024×1024, bands of 64 rows)", "",
               "Each 64-row band is dense (density `hi`) with probability `f`, otherwise sparse (`lo`).", ""]
        ks = ["scalar", "rvv_orig", "dense_m2", "dense_skip_m2", "dense_compress_m1",
              "hybrid_t40", "hybrid_t58.2", "hybrid_t75"]
        rows = []
        for i in mixed:
            r0 = idx[("mixed", i)].get("rvv_orig") or next(iter(idx[("mixed", i)].values()))
            rows.append([i.replace("mx_n1024_", ""), fmt(r0.get("mat_density_actual"), "pct"),
                         fmt(r0.get("mat_row_len_cv"))] + [fmt(c("mixed", i, k)) for k in ks])
        md += table(["input (lo, hi, f)", "overall density", "row CV"] + ks, rows)
        md += ["", "Hybrid (measured threshold 58.2%) speedups:", ""]
        rows = []
        for i in mixed:
            g = "mixed"
            h, s, d = c(g, i, "hybrid_t58.2"), c(g, i, "rvv_orig"), c(g, i, "dense_m2")
            hr = idx[(g, i)].get("hybrid_t58.2")
            best = min(x for x in (s, d) if x) if (s or d) else None
            rows.append([i.replace("mx_n1024_", ""),
                         f"{hr['hyb_dense_blocks']}/{hr['hyb_blocks']}" if hr else "–",
                         fmt(s / h, "x") if s and h else "–", fmt(d / h, "x") if d and h else "–",
                         fmt(best / h, "x") if best and h else "–",
                         fmt(c(g, i, "scalar") / h, "x") if c(g, i, "scalar") and h else "–",
                         fmt(hr.get("work_useful_frac"), "pct") if hr else "–",
                         ", ".join(f"t{t}: {fmt(c(g, i, f'hybrid_t{t}'))}" for t in ("40", "58.2", "75"))])
        md += table(["input", "dense blocks", "vs CSR (rvv_orig)", "vs dense_m2", "vs best single",
                     "vs scalar", "useful lane fraction", "threshold sensitivity (cycles)"], rows)

    # ------------------------------------------------------------ cross-check
    md += ["", "## Cross-check: program-derived work vs. gem5 committed micro-ops", "",
           "For LMUL=1 kernels each arithmetic chunk is one committed `vfmacc` micro-op "
           "(op class SimdFloatMultAcc); the WORK line's `arith_chunks` must match exactly.", ""]
    rows, mism = [], 0
    for r in recs:
        if r["kernel"] in ("dense_m1", "dense_skip_m1", "dense_compress_m1") and r["valid"]:
            a, o = r.get("work_arith_chunks"), r.get("op_SimdFloatMultAcc")
            mism += a != o
            rows.append([r["group"], r["input"], r["kernel"], fmt(a), fmt(o), "ok" if a == o else "MISMATCH"])
    md += [f"{len(rows) - mism}/{len(rows)} runs match." + (" Mismatches:" if mism else "")]
    if mism:
        md += table(["group", "input", "kernel", "arith_chunks", "SimdFloatMultAcc", "check"],
                    [r for r in rows if r[-1] != "ok"])

    cols = ["group", "input", "kernel", "valid", "exit_status", "res_validation", "res_max_rel_err",
            "host_seconds", "cfg_gen", "cfg_density_pct", "cfg_d_lo", "cfg_d_hi", "cfg_frac", "cfg_thr",
            "mat_nnz", "mat_density_actual", "mat_row_len_cv", "work_scanned_chunks", "work_skipped_chunks",
            "work_arith_chunks", "work_arith_lanes", "work_useful_lanes", "work_zero_lanes",
            "work_useful_frac", "hyb_blocks", "hyb_dense_blocks", "hyb_dense_rows", "hyb_dense_nnz"]
    extra = sorted({k for r in recs for k in r if k.startswith(("roi_", "op_"))})
    with open(P4 / "phase4a_all.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols + extra, extrasaction="ignore")
        w.writeheader()
        w.writerows(sorted(recs, key=lambda r: (r["group"], r["input"], r["kernel"])))
    (P4 / "phase4a_summary.md").write_text("\n".join(md) + "\n")
    print("\n".join(md))


if __name__ == "__main__":
    main()
