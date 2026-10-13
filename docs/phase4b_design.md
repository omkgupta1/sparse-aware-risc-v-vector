# Phase 4B: Vector-Indirect Prefetcher (VIP), Design and Feasibility

Status: **proposal, waiting for approval.** No gem5 source has been changed.

## 1. Why a new prefetcher

- **The gathers are the bottleneck once `x` stops fitting in the cache** (Phase 3 size sweep, `rvv_orig`, 16,384 rows × 16 nnz/row; source `results/endterm/phase3/phase3_all.csv`). The only thing that changes across these rows is the size of `x`, i.e. how often the gather misses:

  | `x` size | ROI cycles (uniform) | extra time vs. 8 KiB `x` | upper-bound speedup if that extra time vanished | L1D primary miss | LQ-full events |
  |---:|---:|---:|---:|---:|---:|
  | 8 KiB | 565,377 | – | – | 0.1% | 21,747 |
  | 128 KiB | 904,333 | 37.5% | 1.60× | 52.2% | 407,394 |
  | 512 KiB | 1,388,455 | 59.3% | 2.46× | 55.0% | 635,003 |
  | 2 MiB | 2,085,448 | 72.9% | 3.69× | 59.3% | 996,601 |

  The power-law rows give 27.9% / 54.4% / 68.5% (upper bounds 1.39× / 2.19× / 3.18×). Wider vectors do not help here: VLEN 256→512 gives 1.04×.
- **gem5's existing prefetchers cannot fix this.**
  - The **stride prefetcher** sees random `x[col[k]]` addresses with no stride.
  - **`IndirectMemoryPrefetcher` (IMP) never trains on RVV code.** It reads an index only from loads of ≤ 8 bytes (`pfi.getSize() <= 8` in `indirect_memory.cc`), while our index load is one 32-byte `vle32` carrying 8 indices. It also assumes one index per access.
- **The gather's micro-ops are serialised by the 32-entry load queue.** gem5 splits each `vluxei32` into one micro-op per element, and LQ-full events grow 46× from 8 KiB to 2 MiB `x`. Prefetches issued from the cache side are not limited by the LQ, so they can expose more memory-level parallelism.

## 2. Mechanism

`VectorIndirectPrefetcher` is a subclass of gem5's `StridePrefetcher`, attached to the L1D in place of the stride prefetcher.
- Everything the stride prefetcher does is kept unchanged (it still streams `values[]` and `col_idx[]`).
- On top of that it adds:

1. **Vector index-stream table (VIST), 16 entries, tagged by load PC.** It tracks read accesses of ≥ 8 bytes whose size is a multiple of 4, i.e. RVV unit-stride loads of 32-bit indices. A PC qualifies as an index stream once consecutive accesses are sequential (addr = previous addr + size) with saturating confidence. On a *hit* gem5 delivers the loaded bytes (`PrefetchInfo::get`), so the entry captures up to 16 index values.
2. **Indirect pattern learning.** After an index access from PC *P* with values v₀…vₙ₋₁, the next 4-byte reads from a different PC *Q* (the gather's element micro-ops, which share one PC and run in element order) are tested against `addr = base + (vₑ << s)` for s ∈ {2, 3}. Two consecutive index accesses that give the same `base` enable the entry (P → base, s), with saturating confidence. Learning is done in **virtual addresses** (`use_virtual_addresses=True`, MMU registered), so `x` does not need to be physically contiguous; gem5 SE allocates frames page by page on first touch.
3. **Prefetch generation**
   - *Immediate:* on each index access of an enabled entry, prefetch the cache lines of `base + (vₑ << s)` for all its indices (deduplicated per line, skipped if already cached or in an MSHR). These requests go to the prefetch queue and MSHRs directly, not through the core's 32-entry LQ.
   - *Look-ahead:* when an **index line fills** the L1D (`notifyFill`, which carries the full 64-byte line, typically brought in early by the inherited stride logic), read its 16 indices and prefetch their targets. This runs about the stride prefetcher's degree ahead of the gather and avoids late prefetches. It can be switched off with a parameter, for the ablation.
4. **New `statistics::Scalar` counters:** index streams detected, patterns enabled, index accesses with/without data, target prefetches generated (immediate / look-ahead), targets dropped (duplicate line / already cached). gem5's existing prefetcher stats (`pfIssued`, `pfUseful`, `pfLate`, accuracy, coverage) apply automatically.

Parameters: `vist_entries=16`, `max_targets_per_trigger=16`, `index_shifts=[2,3]`, `enable_lookahead=True`, plus all `StridePrefetcher` parameters.

## 3. Implementation and build plan

- **gem5 changes, prefetcher only (no ISA, CPU or LSQ changes):**
  - new `src/mem/cache/prefetch/vector_indirect.{hh,cc}`;
  - a `VectorIndirectPrefetcher` class in `Prefetcher.py`;
  - one line in `SConscript`.
- **Isolation:** copy `~/gem5-assignment/gem5` → `~/gem5-sparse/gem5` (12 GB including the existing build, so the rebuild is incremental). Mount the copy at the same container path (`/workspace/gem5`) through `GEM5_HOST_ROOT`. The original tree and its `gem5.opt` stay untouched.
- **Patch:** `gem5_patches/sparse_aware.patch` generated with `diff -ruN` of the two `src/` trees, plus `scripts/apply_gem5_patch.sh`.
- **Build:** `scons build/RISCV/gem5.opt -j6` inside the project container (Docker VM: 10 CPUs, about 7.7 GB). Incremental: new files, regenerated params, relink.
- **Config:** `--l1d-pf VectorIndirectPrefetcher` in `configs/sparse_rvv.py`, which registers the MMU and sets `use_virtual_addresses`.

## 4. Evaluation plan (A/B on the same, new gem5 binary)

- **L1D prefetcher configurations:** stride (baseline) · VIP · VIP without look-ahead (ablation) · gem5 IMP (shows existing work does not train on RVV) · none (reference).
- **Kernels:** `rvv_orig`, `rvv_opt_m1`, `rvv_acc2_m1` (LMUL=1 gathers only, the reliable set), with scalar as a control.
- **Inputs:** size sweep (`x` 8 KiB → 2 MiB, uniform and power-law), 4 SuiteSparse matrices, a density subset.
- **Metrics:** ROI cycles and speedup, L1D/L2 primary miss rate and MPKI, DRAM traffic, LQ-full events, prefetch accuracy/coverage/lateness, VIP counters. Every run is validated against the scalar reference.
- **Hardware overhead:** an analytic table-bit count; Verilog plus Yosys as the stretch goal.

## 5. Risks

| Risk | Mitigation |
|---|---|
| Rebuild fails or runs out of memory in the container | Incremental build in an isolated copy; `-j` reduced if needed |
| Prefetches arrive late (gather follows its index load within a few instructions) | Look-ahead on index-line fills; measured via `pfLate` |
| L1D pollution (32 KiB) from target prefetches | Per-trigger cap, line deduplication, measured accuracy; L2 placement as a fallback variant |
| Changed timing exposes the known O3 RVV bugs | Only LMUL=1 gather kernels; every run validated |
| Index loads that miss carry no data | The look-ahead path reads indices from the fill packet instead |
