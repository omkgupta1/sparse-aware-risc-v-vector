#!/usr/bin/env python3
"""Phase 1 report: kernel-only (ROI) metrics vs. end-to-end numbers.

Every number is read from gem5 stats.txt / run.log files:
  results/endterm/phase1/roi_d{20,60}/   ROI build (3 dumps: prologue/roi/epilogue)
  results/endterm/phase1/e2e_d{20,60}/   unmodified midterm source, same toolchain
  results/midterm/spmv_{80,40}/          original midterm runs (GCC 16.1 newlib)

Writes results/endterm/phase1/phase1_summary.csv and phase1_summary.md.
"""
import csv
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gem5stats import load_dumps, select_dump, summarize  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
P1 = ROOT / "results" / "endterm" / "phase1"
MID = ROOT / "results" / "midterm"
CASES = [(20, "spmv_80"), (60, "spmv_40")]  # (density %, midterm dir)


def program_output(run_log):
    text = Path(run_log).read_text()
    m = re.search(r"nnz=(\d+)", text)
    e = re.search(r"max_error=([0-9.eE+-]+)", text)
    ok = "VALIDATION PASSED" in text
    exit_ok = re.search(r"^EXIT_STATUS=0$", text, re.M) is not None or "EXIT_STATUS" not in text
    return int(m.group(1)) if m else None, float(e.group(1)) if e else None, ok and exit_ok


def main():
    rows = []
    for density, mid in CASES:
        roi_dir, e2e_dir = P1 / f"roi_d{density}", P1 / f"e2e_d{density}"
        nnz, err, ok = program_output(roi_dir / "run.log")
        dumps = load_dumps(roi_dir / "stats.txt")
        pro, roi, epi = (summarize(select_dump(dumps, w)) for w in ("prologue", "roi", "epilogue"))
        tot = summarize(select_dump(dumps, "total"))
        e2e = summarize(load_dumps(e2e_dir / "stats.txt")[0])
        e2e_nnz, _, e2e_ok = program_output(e2e_dir / "run.log")
        old = summarize(load_dumps(MID / mid / "stats.txt")[0])
        rows.append({
            "density_pct": density,
            "sparsity_pct": 100 - density,
            "nnz": nnz,
            "validation": "PASSED" if ok and e2e_ok and nnz == e2e_nnz else "FAILED",
            "max_error": err,
            "roi_cycles": roi["cycles"],
            "roi_insts": roi["insts"],
            "roi_ops": roi["ops"],
            "roi_ipc": roi["ipc"],
            "roi_cycles_per_nnz": roi["cycles"] / nnz,
            "roi_insts_per_nnz": roi["insts"] / nnz,
            "roi_vec_insts": roi["vec_insts"],
            "roi_indexed_load_uops": roi["indexed_load_uops"],
            "roi_l1d_miss_rate": roi["l1d_miss_rate"],
            "roi_l2_miss_rate": roi["l2_miss_rate"],
            "roi_lq_full_events": roi["lq_full_events"],
            "roi_dram_bytes_read": roi["dram_bytes_read"],
            "prologue_cycles": pro["cycles"],
            "epilogue_cycles": epi["cycles"],
            "sum_of_dumps_cycles": tot["cycles"],
            "sum_of_dumps_insts": tot["insts"],
            "roi_share_of_total_cycles": roi["cycles"] / tot["cycles"],
            "e2e_same_toolchain_cycles": e2e["cycles"],
            "e2e_same_toolchain_insts": e2e["insts"],
            "e2e_same_toolchain_ipc": e2e["ipc"],
            "midterm_e2e_cycles": old["cycles"],
            "midterm_e2e_insts": old["insts"],
            "midterm_e2e_ipc": old["ipc"],
        })

    with open(P1 / "phase1_summary.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)

    def fmt(v):
        if isinstance(v, float):
            return f"{v:.4g}" if abs(v) < 1e4 else f"{v:,.0f}"
        if isinstance(v, int):
            return f"{v:,}"
        return str(v)

    lines = ["| Metric | " + " | ".join(f"{r['sparsity_pct']}% sparse (d={r['density_pct']}%)" for r in rows) + " |",
             "|---|" + "---:|" * len(rows)]
    for k in rows[0]:
        lines.append(f"| {k} | " + " | ".join(fmt(r[k]) for r in rows) + " |")
    lo, hi = rows
    ratio = lambda k: hi[k] / lo[k]  # noqa: E731
    lines += ["", f"Scaling from d={lo['density_pct']}% to d={hi['density_pct']}%:", ""]
    lines += [
        f"- NNZ ratio: {ratio('nnz'):.3f}x",
        f"- Kernel (ROI) cycles ratio: {ratio('roi_cycles'):.3f}x; "
        f"marginal kernel cost {(hi['roi_cycles'] - lo['roi_cycles']) / (hi['nnz'] - lo['nnz']):.3f} cycles per extra nonzero",
        f"- Midterm end-to-end cycles ratio: {ratio('midterm_e2e_cycles'):.3f}x; "
        f"marginal {(hi['midterm_e2e_cycles'] - lo['midterm_e2e_cycles']) / (hi['nnz'] - lo['nnz']):.3f} cycles per extra nonzero",
        f"- Kernel load-queue-full events ratio: {ratio('roi_lq_full_events'):.3f}x",
    ]

    # Control builds: does executing the m5ops perturb timing?
    ctrl = P1 / "controls"
    if ctrl.is_dir():
        lines += ["", "ROI-marker controls (whole-program cycles = sum of dumps; ROI = kernel dump):", "",
                  "| Build | d | dumps | total cycles | ROI cycles | total insts |", "|---|---:|---:|---:|---:|---:|"]
        builds = [("e2e (no markers)", "e2e_d{}", P1), ("no-op call control", "noopcall_d{}", ctrl),
                  ("m5_dump_reset_stats x2 (used)", "roi_d{}", P1), ("m5_reset_stats + m5_dump_stats", "resetdump_d{}", ctrl)]
        for name, pat, base in builds:
            for r in rows:
                p = base / pat.format(r["density_pct"]) / "stats.txt"
                if not p.exists():
                    continue
                ds = load_dumps(p)
                tot = summarize(select_dump(ds, "total"))
                roi_c = {3: lambda: summarize(ds[1])["cycles"], 2: lambda: summarize(ds[0])["cycles"]}.get(len(ds), lambda: "–")()
                tc, ti = (fmt(tot["cycles"]), fmt(tot["insts"])) if len(ds) != 2 else ("n/a", "n/a")
                lines.append(f"| {name} | {r['density_pct']} | {len(ds)} | {tc} | {fmt(roi_c)} | {ti} |")
        lines += ["", "n/a: with m5_reset_stats + m5_dump_stats the prologue is discarded by the reset and the "
                  "final dump (ROI_BEGIN..exit) overlaps the ROI dump, so no whole-program total exists."]

    md = "\n".join(lines) + "\n"
    (P1 / "phase1_summary.md").write_text(md)
    print(md)


if __name__ == "__main__":
    main()
