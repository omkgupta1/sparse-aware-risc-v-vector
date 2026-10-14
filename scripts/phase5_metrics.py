#!/usr/bin/env python3
"""Phase 5: one consolidated metrics table over every end-term run.

Re-collects all runs of Phases 2, 3, 4A and 4B with scripts/collect.py and
writes results/endterm/phase5/all_runs_metrics.csv with, per run:

  identity      phase, group, input, kernel, vlen, l1d prefetcher configuration
  status        valid (exit 0 and validated), reliable (not an LMUL>1 gather kernel)
  time          roi_cycles, roi_time_us (1 GHz clock)
  speedups      vs. scalar and vs. rvv_orig on the identical input, in the same
                phase (same binaries), same VLEN and same prefetcher config
  vector        lane utilisation (active elements / (chunks x VLMAX)), mean row length
  memory        L1D (raw and primary) miss rate, L1D/L2 MPKI, L2 miss rate,
                DRAM bytes read/written, DRAM bandwidth (GB/s over the ROI)
  stalls        load-queue-full events
  work (4A)     arithmetic lanes issued, useful lanes, zero lanes, skipped chunks
  prefetch (4B) L1D prefetch accuracy, coverage, late prefetches

Bug-investigation directories are skipped. Only computed values; nothing estimated.
"""
import csv
import re
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from collect import collect, ok  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
END = ROOT / "results" / "endterm"
OUT = END / "phase5"
UNRELIABLE = {"rvv_opt_m2", "rvv_opt_m4", "rvv_acc2_m2"}  # gem5 O3 Issue 2


def identify(phase, rel):
    """(group, input, kernel, pfcfg, vlen-or-None) from a run's relative path."""
    p = rel.parts
    if phase == "phase2":                          # d20/rvv_orig
        return "midterm", f"midterm_n256_{p[0]}", p[1], "stride", None
    if phase == "phase3":
        if p[0] == "vlen":                         # vlen/vlen128/input/kernel
            return "vlen", p[2], p[3], "stride", int(p[1][4:])
        return p[0], p[1], p[2], "stride", None
    if phase == "phase4a":                         # mixed/input/hybrid_t40
        return p[0], p[1], p[2], "stride", None
    if phase == "phase4b":                         # size/input/kernel__cfg
        k, cfg = p[2].split("__")
        return p[0], p[1], k, cfg, (512 if p[0] == "vlen512" else None)
    raise ValueError(phase)


def load():
    recs = []
    for phase in ("phase2", "phase3", "phase4a", "phase4b"):
        base = END / phase
        for log in sorted(base.rglob("run.log")):
            rel = log.parent.relative_to(base)
            if rel.parts[0].startswith(("gem5_", "gem5")) or len(rel.parts) < 2:
                continue
            if "EXIT_STATUS=" not in log.read_text(errors="replace"):
                continue
            group, inp, kernel, pfcfg, vlen = identify(phase, rel)
            r = collect(log.parent)
            r.update(phase=phase, group=group, input=inp, kernel=kernel, pfcfg=pfcfg,
                     vlen=vlen or r.get("sim_vlen", 256), valid=ok(r),
                     reliable=kernel not in UNRELIABLE)
            recs.append(r)
    return recs


def derive(recs):
    key = lambda r: (r["phase"], r["group"], r["input"], r["vlen"], r["pfcfg"])  # noqa: E731
    base = defaultdict(dict)
    for r in recs:
        if r["valid"]:
            base[key(r)][r["kernel"]] = r["roi_cycles"]
    rows = []
    for r in recs:
        cyc = r.get("roi_cycles")
        b = base[key(r)]
        g = lambda k, d=None: r.get(k, d)  # noqa: E731
        traffic = (g("roi_dram_bytes_read", 0) or 0) + (g("roi_dram_bytes_written", 0) or 0)
        good = r["valid"] and cyc
        rows.append({
            "phase": r["phase"], "group": r["group"], "input": r["input"], "kernel": r["kernel"],
            "vlen": r["vlen"], "l1d_prefetcher": r["pfcfg"], "valid": r["valid"],
            "reliable": r["reliable"],
            "rows": g("mat_rows"), "cols": g("mat_cols"), "nnz": g("mat_nnz"),
            "row_len_mean": g("mat_row_len_mean"), "row_len_cv": g("mat_row_len_cv"),
            "x_bytes": g("mat_x_bytes"), "density_actual": g("mat_density_actual"),
            "roi_cycles": cyc, "roi_time_us": cyc / 1000.0 if cyc else None,
            "roi_insts": g("roi_insts"), "roi_ipc": g("roi_ipc"),
            "speedup_vs_scalar": b["scalar"] / cyc if good and b.get("scalar") else None,
            "speedup_vs_rvv_orig": b["rvv_orig"] / cyc if good and b.get("rvv_orig") else None,
            "lmul": g("vec_lmul"), "vlmax": g("vec_vlmax"), "lane_util": g("vec_lane_util"),
            "l1d_miss_rate": g("roi_l1d_miss_rate"), "l1d_primary_miss_rate": g("roi_l1d_primary_miss_rate"),
            "l1d_mpki": g("roi_l1d_mpki"), "l2_miss_rate": g("roi_l2_miss_rate"), "l2_mpki": g("roi_l2_mpki"),
            "dram_bytes_read": g("roi_dram_bytes_read"), "dram_bytes_written": g("roi_dram_bytes_written"),
            "dram_bw_gbs": traffic / cyc if cyc else None,   # bytes per ns = GB/s at 1 GHz
            "lq_full_events": g("roi_lq_full_events"),
            "work_arith_lanes": g("work_arith_lanes"), "work_useful_lanes": g("work_useful_lanes"),
            "work_zero_lanes": g("work_zero_lanes"), "work_skipped_chunks": g("work_skipped_chunks"),
            "work_scanned_chunks": g("work_scanned_chunks"),
            "pf_l1d_accuracy": g("pf_l1d_accuracy"), "pf_l1d_coverage": g("pf_l1d_coverage"),
            "pf_l1d_late": g("pf_l1d_pfLate"),
            "vip_lookahead_targets": g("pf_l1d_vip_lookaheadTargets"),
            "vip_immediate_targets": g("pf_l1d_vip_immediateTargets"),
        })
    return rows


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    rows = derive(load())
    path = OUT / "all_runs_metrics.csv"
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    by = defaultdict(lambda: [0, 0])
    for r in rows:
        by[r["phase"]][0 if r["valid"] else 1] += 1
    print(f"wrote {path} ({len(rows)} runs)")
    for p, (v, f) in sorted(by.items()):
        print(f"  {p}: {v} valid, {f} failed")


if __name__ == "__main__":
    main()
