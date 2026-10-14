#!/usr/bin/env python3
"""Collect one harness run directory into a flat record.

A record joins:
  - the program's own output in run.log (CONFIG/MATRIX/VECTOR/RESULT lines,
    printed by src/harness/bench.c), prefixed cfg_/mat_/vec_/res_,
  - gem5 exit status from run.log,
  - ROI metrics from stats.txt(.gz) (see gem5stats.summarize),
  - committed micro-op counts per op class in the ROI (prefixed op_).

Used by the per-phase report scripts; never estimates anything.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gem5stats import CORE, load_dumps, select_dump, summarize  # noqa: E402

OP_CLASSES = [
    "IntAlu", "IntMult", "FloatMultAcc", "MemRead", "MemWrite", "FloatMemRead",
    "SimdConfig", "SimdMisc", "SimdMult", "SimdShift", "SimdFloatMult",
    "SimdFloatMultAcc", "SimdFloatReduceAdd", "SimdUnitStrideLoad",
    "SimdUnitStrideStore", "SimdIndexedLoad", "SimdStridedLoad",
    "SimdUnitStrideMaskLoad", "SimdCmp", "SimdPredAlu", "SimdReduceAlu", "SimdMisc",
]
_TAGS = {"CONFIG": "cfg_", "MATRIX": "mat_", "VECTOR": "vec_", "RESULT": "res_",
         "WORK": "work_", "HYBRID": "hyb_"}


def _num(v):
    for t in (int, float):
        try:
            return t(v)
        except ValueError:
            pass
    return v


def parse_run_log(path):
    rec = {}
    text = Path(path).read_text(errors="replace")
    for line in text.splitlines():
        tag = line.split(" ", 1)[0]
        if tag in _TAGS:
            for k, v in re.findall(r"(\w+)=(\S+)", line):
                rec[_TAGS[tag] + k] = _num(v)
    m = re.search(r"^sparse_rvv: cpu=(\S+) VLEN=(\d+) ELEN=(\d+)", text, re.M)
    if m:  # printed by configs/sparse_rvv.py
        rec["sim_cpu"], rec["sim_vlen"], rec["sim_elen"] = m.group(1), int(m.group(2)), int(m.group(3))
    for key in ("l1d_pf", "l2_pf", "l1d_pf_params"):
        m = re.search(rf"^sparse_rvv: .* {key}=(\S+)", text, re.M)
        if m:
            rec[f"sim_{key}"] = m.group(1)
    m = re.search(r"^HOST_SECONDS=(\d+)$", text, re.M)
    rec["host_seconds"] = int(m.group(1)) if m else None
    m = re.search(r"^EXIT_STATUS=(\d+)$", text, re.M)
    rec["exit_status"] = int(m.group(1)) if m else None
    return rec


def collect(run_dir):
    run_dir = Path(run_dir)
    rec = {"run": str(run_dir)}
    rec.update(parse_run_log(run_dir / "run.log"))
    try:
        roi = select_dump(load_dumps(run_dir / "stats.txt"), "roi")
    except (OSError, ValueError) as e:
        rec["error"] = f"stats: {e}"
        return rec
    rec.update({f"roi_{k}": v for k, v in summarize(roi).items()})
    prefix = f"{CORE}.commitStats0.committedInstType::"
    for c in OP_CLASSES:
        rec[f"op_{c}"] = roi.get(prefix + c, 0)
    # Every scalar statistic of the L1D / L2 prefetchers in the ROI
    # (pfIssued, pfUseful, accuracy, coverage, ..., and the VIP's vip.* group).
    for cache, tag in (("l1d-cache-0", "pf_l1d_"), ("l2-cache-0", "pf_l2_")):
        pre = f"board.cache_hierarchy.{cache}.prefetcher."
        for k, v in roi.items():
            if k.startswith(pre) and "::" not in k and not isinstance(v, str):
                rec[tag + k[len(pre):].replace(".", "_")] = v
    return rec


def ok(rec):
    """True if gem5 exited cleanly and the program validated its output."""
    return rec.get("exit_status") == 0 and rec.get("res_validation") == "PASSED" and "error" not in rec


if __name__ == "__main__":
    for d in sys.argv[1:]:
        for k, v in collect(d).items():
            print(f"{k:32s} {v}")
