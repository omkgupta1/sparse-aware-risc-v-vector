#!/usr/bin/env python3
"""Phase 2 report: all kernels on identical inputs (midterm matrices, n=256).

Reads results/endterm/phase2/d{20,60}/<kernel>/ and writes
results/endterm/phase2/phase2_summary.{csv,md}.
Speedups are ROI-cycle ratios against the scalar and midterm-RVV kernels of
the same density.
"""
import csv
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from collect import collect, ok  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
P2 = ROOT / "results" / "endterm" / "phase2"
P1 = ROOT / "results" / "endterm" / "phase1"
KERNELS = ["scalar", "rvv_orig", "rvv_opt_m1", "rvv_opt_m2", "rvv_opt_m4",
           "dense_m1", "dense_m2", "dense_m4"]
DENSITIES = [1, 5, 20, 60, 100]

COLUMNS = [
    "density_pct", "kernel", "status", "nnz", "roi_cycles", "speedup_vs_scalar",
    "speedup_vs_rvv_orig", "roi_insts", "roi_ops", "roi_ipc", "cycles_per_nnz",
    "max_rel_err", "vlmax", "chunks", "reg_chunks", "active_elems", "lane_util",
    "op_SimdFloatMultAcc", "op_SimdFloatMult", "op_SimdFloatReduceAdd",
    "op_SimdIndexedLoad", "op_SimdUnitStrideLoad", "op_SimdConfig",
    "op_FloatMultAcc", "roi_l1d_miss_rate", "roi_l1d_primary_miss_rate", "roi_l1d_mshr_hits",
    "roi_l2_miss_rate", "roi_l1d_mpki",
    "roi_lq_full_events", "roi_dram_bytes_read", "chunk_check", "status_note",
]


def main():
    rows = []
    for d in DENSITIES:
        recs = {k: collect(P2 / f"d{d}" / k) for k in KERNELS if (P2 / f"d{d}" / k / "run.log").exists()}
        base_s = recs.get("scalar", {}).get("roi_cycles")
        base_o = recs.get("rvv_orig", {}).get("roi_cycles")
        for k, r in recs.items():
            good = ok(r)
            cyc = r.get("roi_cycles")
            # Per-chunk vector arithmetic op must equal the program's chunk count.
            arith = r.get("op_SimdFloatMultAcc") if k.startswith(("rvv_opt", "dense")) else r.get("op_SimdFloatMult")
            # gem5 commits one micro-op per register holding active elements.
            chunk_check = "n/a" if k == "scalar" else ("ok" if arith == r.get("vec_reg_chunks") else f"MISMATCH({arith})")
            rows.append({
                "density_pct": d, "kernel": k,
                "status": "ok" if good else f"FAILED(exit={r.get('exit_status')},val={r.get('res_validation')})",
                "nnz": r.get("res_nnz"),
                "roi_cycles": cyc,
                "speedup_vs_scalar": base_s / cyc if good and base_s and cyc else None,
                "speedup_vs_rvv_orig": base_o / cyc if good and base_o and cyc else None,
                "roi_insts": r.get("roi_insts"), "roi_ops": r.get("roi_ops"),
                "roi_ipc": r.get("roi_ipc"),
                "cycles_per_nnz": cyc / r["res_nnz"] if cyc and r.get("res_nnz") else None,
                "max_rel_err": r.get("res_max_rel_err"),
                "vlmax": r.get("vec_vlmax"), "chunks": r.get("vec_chunks"), "reg_chunks": r.get("vec_reg_chunks"),
                "active_elems": r.get("vec_active_elems"), "lane_util": r.get("vec_lane_util"),
                **{c: r.get(c) for c in COLUMNS if c.startswith(("op_", "roi_l", "roi_dram"))},
                "status_note": "gem5 O3 bug: excluded from performance claims" if k == "rvv_opt_m4" else "",
                "chunk_check": chunk_check,
            })

    with open(P2 / "phase2_summary.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=COLUMNS)
        w.writeheader()
        w.writerows(rows)

    def fmt(v, k=""):
        if v is None:
            return "–"
        if isinstance(v, float):
            if "rate" in k or "util" in k:
                return f"{v * 100:.2f}%"
            return f"{v:.3g}" if abs(v) < 1 else f"{v:,.3f}" if abs(v) < 100 else f"{v:,.0f}"
        return f"{v:,}" if isinstance(v, int) else str(v)

    show = ["kernel", "status", "roi_cycles", "speedup_vs_scalar", "speedup_vs_rvv_orig",
            "roi_insts", "roi_ipc", "cycles_per_nnz", "max_rel_err", "chunks", "lane_util",
            "roi_l1d_miss_rate", "roi_l1d_primary_miss_rate", "roi_lq_full_events", "chunk_check"]
    out = []
    for d in DENSITIES:
        out += [f"### density {d}% (sparsity {100 - d}%)", "",
                "| " + " | ".join(show) + " |", "|" + "---|" * len(show)]
        for r in rows:
            if r["density_pct"] == d:
                out.append("| " + " | ".join(fmt(r[c], c) for c in show) + " |")
        out.append("")

    # Consistency with Phase 1: same midterm kernel, standalone vs harness.
    try:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from gem5stats import load_dumps, select_dump, summarize
        out += ["Phase 1 standalone midterm kernel vs. harness `rvv_orig` (ROI cycles):", ""]
        for d in (20, 60):
            p1 = summarize(select_dump(load_dumps(P1 / f"roi_d{d}" / "stats.txt"), "roi"))["cycles"]
            h = next((r["roi_cycles"] for r in rows if r["density_pct"] == d and r["kernel"] == "rvv_orig"), None)
            out.append(f"- d={d}%: Phase 1 {p1:,} vs harness {h:,} ({(h / p1 - 1) * 100:+.1f}%)" if h else f"- d={d}%: harness run missing")
    except OSError:
        pass

    out += ["", "Notes:",
            "- rvv_opt_m4 (LMUL=4 gather) is excluded from performance claims: gem5 25.1 O3 produces",
            "  wrong results intermittently (timing-dependent; correct on MinorCPU and QEMU). See",
            "  results/endterm/phase2/gem5_m4_investigation/ and PROGRESS.md. Its timing is shown only for completeness.",
            "- roi_l1d_miss_rate counts MSHR hits (accesses merging into an in-flight miss) as misses;",
            "  roi_l1d_primary_miss_rate counts only new requests to L2."]
    md = "\n".join(out) + "\n"
    (P2 / "phase2_summary.md").write_text(md)
    print(md)


if __name__ == "__main__":
    main()
