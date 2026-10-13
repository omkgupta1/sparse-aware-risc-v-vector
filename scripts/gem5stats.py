#!/usr/bin/env python3
"""Parse gem5 stats.txt files that may contain several stats dumps.

Binaries built with src/common/roi.h produce three dumps per run:

    index 0  prologue  (startup + input generation)
    index 1  roi       (the measured kernel only)
    index 2  epilogue  (validation + output)

A run without ROI markers has a single dump (index 0 = whole program).

Library use:
    from gem5stats import load_dumps, select_dump, summarize
    roi = select_dump(load_dumps("results/endterm/x/stats.txt"), "roi")
    print(summarize(roi)["cycles"])

CLI:
    gem5stats.py STATS [--dump roi|prologue|epilogue|total|N] [--keys K ...]
"""
import argparse
import gzip
import math
import sys

DUMP_HEADER = "---------- Begin Simulation Statistics ----------"
DUMP_FOOTER = "---------- End Simulation Statistics   ----------"
ROI_LAYOUT = {"prologue": 0, "roi": 1, "epilogue": 2}

CORE = "board.processor.cores.core"
L1D = "board.cache_hierarchy.l1d-cache-0"
L1I = "board.cache_hierarchy.l1i-cache-0"
L2 = "board.cache_hierarchy.l2-cache-0"
MEM = "board.memory.mem_ctrl"


def _value(tok):
    try:
        return int(tok)
    except ValueError:
        pass
    try:
        return float(tok)  # also handles 'nan' and 'inf'
    except ValueError:
        return tok


def load_dumps(path):
    """Return a list of {stat_name: value}, one per stats dump, in file order.

    Accepts stats.txt or stats.txt.gz (and falls back to PATH.gz if PATH is missing).
    """
    path = str(path)
    if not path.endswith(".gz") and not _exists(path) and _exists(path + ".gz"):
        path += ".gz"
    dumps, cur = [], None
    with (gzip.open(path, "rt") if path.endswith(".gz") else open(path)) as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith(DUMP_HEADER):
                cur = {}
                continue
            if line.startswith(DUMP_FOOTER):
                if cur is not None:
                    dumps.append(cur)
                cur = None
                continue
            if cur is None or not line.strip():
                continue
            parts = line.split()
            if len(parts) >= 2:
                cur[parts[0]] = _value(parts[1])
    if cur:  # truncated file without footer
        dumps.append(cur)
    return dumps


def _exists(p):
    try:
        open(p).close()
        return True
    except OSError:
        return False


def select_dump(dumps, which="roi"):
    """Pick one dump by name ('roi', 'prologue', 'epilogue') or integer index.

    Named selection requires the 3-dump ROI layout and raises otherwise, so a
    run whose ROI markers did not fire can never be silently mistaken for ROI
    data. 'total' sums simTicks/insts-style counters across all dumps.
    """
    if isinstance(which, int) or str(which).lstrip("-").isdigit():
        return dumps[int(which)]
    if which == "total":
        return _sum_dumps(dumps)
    if len(dumps) != len(ROI_LAYOUT):
        raise ValueError(
            f"expected {len(ROI_LAYOUT)} dumps (prologue/roi/epilogue), "
            f"found {len(dumps)}; did the ROI markers execute?")
    return dumps[ROI_LAYOUT[which]]


def _sum_dumps(dumps):
    """Sum additive counters over all dumps (ratios are recomputed in summarize)."""
    out = {}
    for d in dumps:
        for k, v in d.items():
            if isinstance(v, int):
                out[k] = out.get(k, 0) + v
            elif k not in out:
                out[k] = v
    return out


def get(d, key, default=None):
    v = d.get(key, default)
    return default if isinstance(v, str) else v


def _ratio(a, b):
    return a / b if a is not None and b else math.nan


def summarize(d):
    """Headline metrics from one dump. Ratios are recomputed from counters."""
    insts = get(d, "simInsts")
    cycles = get(d, f"{CORE}.numCycles")
    l1d_acc = get(d, f"{L1D}.demandAccesses::total", 0)
    l1d_miss = get(d, f"{L1D}.demandMisses::total", 0)
    l2_acc = get(d, f"{L2}.demandAccesses::total", 0)
    l2_miss = get(d, f"{L2}.demandMisses::total", 0)
    return {
        "sim_ticks": get(d, "simTicks"),
        "cycles": cycles,
        "insts": insts,
        "ops": get(d, "simOps"),
        "ipc": _ratio(insts, cycles),
        "vec_insts": get(d, f"{CORE}.commitStats0.numVecInsts"),
        "loads": get(d, f"{CORE}.commitStats0.numLoadInsts"),
        "stores": get(d, f"{CORE}.commitStats0.numStoreInsts"),
        "indexed_load_uops": get(d, f"{CORE}.commitStats0.committedInstType::SimdIndexedLoad", 0),
        "branch_mispredicts": get(d, f"{CORE}.commit.branchMispredicts"),
        "l1d_accesses": l1d_acc,
        "l1d_misses": l1d_miss,
        "l1d_miss_rate": _ratio(l1d_miss, l1d_acc),
        "l1d_mpki": _ratio(l1d_miss * 1000, insts),
        "l2_accesses": l2_acc,
        "l2_misses": l2_miss,
        "l2_miss_rate": _ratio(l2_miss, l2_acc),
        "l2_mpki": _ratio(l2_miss * 1000, insts),
        "dram_bytes_read": get(d, f"{MEM}.bytesReadSys", 0),
        "dram_bytes_written": get(d, f"{MEM}.bytesWrittenSys", 0),
        "lq_full_events": get(d, f"{CORE}.rename.LQFullEvents", 0),
        "sq_full_events": get(d, f"{CORE}.rename.SQFullEvents", 0),
        "iq_full_events": get(d, f"{CORE}.rename.IQFullEvents", 0),
        "rob_full_events": get(d, f"{CORE}.rename.ROBFullEvents", 0),
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("stats")
    ap.add_argument("--dump", default="roi",
                    help="roi | prologue | epilogue | total | <index>  (default: roi)")
    ap.add_argument("--keys", nargs="*", help="raw stat names to print instead of the summary")
    args = ap.parse_args(argv)

    dumps = load_dumps(args.stats)
    d = select_dump(dumps, args.dump)
    print(f"# {args.stats}: {len(dumps)} dump(s), showing '{args.dump}'")
    rows = ((k, d.get(k, "<missing>")) for k in args.keys) if args.keys else summarize(d).items()
    for k, v in rows:
        print(f"{k:40s} {v}")


if __name__ == "__main__":
    sys.exit(main())
