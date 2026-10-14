#!/usr/bin/env python3
"""Analytic storage estimate of the Vector-Indirect Prefetcher (VIP).

    vip_storage.py [--depths 4 8 16 32 64 128] [--md]

Counts the state the gem5 implementation keeps
(src/mem/cache/prefetch/vector_indirect.{hh,cc}), per field, for a hardware
realisation with the bit widths below. Two organisations are reported:

  per-stream  every vector index-stream table (VIST) entry has its own index
              history (exactly what the simulator models);
  shared      VIST entries hold only scalar fields; NUM_HIST history buffers
              are shared by the streams currently being trained/checked
              (in all runs only the index and the value streams of the kernel
              were active, so 2 buffers suffice).

Learning additionally needs a candidate (base, shift) buffer; the simulator
keeps every candidate (history x indices x shifts), which is the upper bound
reported here.
"""
import argparse
import math

# ---- assumptions (RV64, Sv39) --------------------------------------------
VA = 39            # virtual address bits (Sv39)
PPN = 32           # physical page number bits (44-bit PA, 4 KiB pages)
LINE = 6           # log2(64-byte cache line)
PC = VA            # VIST tag: full PC (a 16-bit hash would also work)
IDX = 32           # index width (32-bit indices)
MAX_IDX = 16       # indices kept per access / line (64-byte line of uint32)
VIST = 16          # VIST entries (vist_entries)
SHIFTS = 2         # candidate shifts {2, 3}
RECENT = 64        # recently requested target lines (dedup filter)
PENDING = 64       # pending look-ahead targets
NUM_HIST = 2       # shared history buffers (shared organisation)
MAX_ACCESS = 256   # largest index access in bytes (VLEN=2048 at LMUL=1)


def scalar_fields(depth):
    """Per-VIST-entry fields other than the index history (bits)."""
    ptr = max(1, math.ceil(math.log2(depth)))
    return {
        "valid": 1,
        "pc tag": PC,
        "last index VA": VA,
        "last index PPN": PPN,
        "last access size": math.ceil(math.log2(MAX_ACCESS + 1)),
        "sequential confidence": 3,
        "LRU": math.ceil(math.log2(VIST)),
        "history position": ptr,
        "history count": ptr + 1,
        "gather PC": PC,
        "enabled": 1,
        "base (VA)": VA,
        "shift select": math.ceil(math.log2(SHIFTS)),
        "samples": 2,
        "check counter": 3,
        "consecutive fails": 7,
    }


def history_bits(depth):
    return depth * (MAX_IDX * IDX + math.ceil(math.log2(MAX_IDX + 1)))


def candidate_bits(depth):
    return depth * MAX_IDX * SHIFTS * (VA + math.ceil(math.log2(SHIFTS)))


def estimate(depth):
    sc = sum(scalar_fields(depth).values())
    hist = history_bits(depth)
    cand = candidate_bits(depth)
    glob = RECENT * (VA - LINE) + PENDING * VA
    per_stream = VIST * (sc + hist) + cand + glob
    shared = VIST * sc + NUM_HIST * hist + cand + glob
    return {"depth": depth, "entry_scalar_bits": sc, "history_bits": hist, "candidate_bits": cand,
            "global_bits": glob, "per_stream_bits": per_stream, "shared_bits": shared}


def kib(bits):
    return bits / 8 / 1024


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--depths", type=int, nargs="*", default=[4, 8, 16, 32, 64, 128])
    ap.add_argument("--md", action="store_true", help="print Markdown tables")
    a = ap.parse_args()

    if a.md:
        print("Per-VIST-entry scalar fields (bits):\n")
        print("| field | bits |\n|---|---:|")
        for k, v in scalar_fields(32).items():
            print(f"| {k} | {v} |")
        print(f"| **total (history depth 32)** | **{sum(scalar_fields(32).values())}** |\n")
        print("| history depth | history / stream | candidate buffer | dedup + pending queues "
              "| **per-stream total** | **shared total (2 history buffers)** |")
        print("|---:|---:|---:|---:|---:|---:|")
    for d in a.depths:
        e = estimate(d)
        if a.md:
            print(f"| {d} | {kib(e['history_bits']):.2f} KiB | {kib(e['candidate_bits']):.2f} KiB | "
                  f"{kib(e['global_bits']):.2f} KiB | {kib(e['per_stream_bits']):.2f} KiB | "
                  f"{kib(e['shared_bits']):.2f} KiB |")
        else:
            print(f"depth {d:4d}: per-stream {kib(e['per_stream_bits']):7.2f} KiB, "
                  f"shared {kib(e['shared_bits']):6.2f} KiB "
                  f"(history {kib(e['history_bits']):.2f} KiB/stream, candidates {kib(e['candidate_bits']):.2f} KiB)")


if __name__ == "__main__":
    main()
