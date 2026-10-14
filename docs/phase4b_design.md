# Phase 4B: Vector-Indirect Prefetcher (VIP), Design and Feasibility

Status: **implemented** (approved 2026-10-14, built in place in `~/gem5-assignment/gem5`; the original `gem5.opt` is kept as `build/RISCV/gem5.opt.pre-sparse-backup`).
- **Patch:** `gem5_patches/sparse_aware.patch`, 4 files, generated with `scripts/make_gem5_patch.sh` (plain `diff -ruN`), applied with `scripts/apply_gem5_patch.sh`.
- **Default behaviour unchanged:** the patched and original binaries give identical ROI cycles (1,388,982) on the same workload (`results/endterm/phase4b_prep/regress_*`).

Sections 1–5 are the design as proposed; section 6 lists what changed during implementation and why.

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

## 6. Changes made during implementation (all driven by measurements)

| # | Proposed | Implemented | Why (evidence in `results/endterm/phase4b_prep/`) |
|---|---|---|---|
| 1 | Index values from `PrefetchInfo::get` on hits | `notify()` override keeps the packet; a *satisfied read hit* is treated as having data | gem5 notifies hits before the packet becomes a response, so `hasData()` is false and `PrefetchInfo` holds no data (gem5 Issue 4; the stock IMP panics on this). First smoke run: 6,782 index accesses, 0 with data. |
| 2 | Pair each index access with the *first* following gather access | Each stream keeps the indices of its last **32** index accesses. Every gather access gives candidate `(base, shift)` pairs against the whole history; intersecting ≥ 3 samples from the same gather PC enables the pattern. | O3 runs later chunks' index loads ahead of earlier gathers (the LQ is full), so "the next gather" usually belongs to another chunk. With pairing, no pattern was ever learned. |
| 3 | Confidence counter disables a pattern on mismatches | Sampled checks (every 8th gather access); disable only after **64 consecutive** failed checks | With an 8-entry history and a confidence counter the correct pattern was dropped within the ROI (7 failed checks, 31 immediate targets). Gathers can lag their index loads by more than the history window. |
| 4 | Look-ahead targets inserted directly from `notifyFill` | Look-ahead targets go into a pending queue (64) and are issued with the next demand access | Fills caused by (same-page) stride prefetches carry no virtual address or context ID, so page-crossing targets cannot be translated from the fill packet. Filled lines are mapped to virtual addresses through the stream's last (VA, PA) pair. |

Smoke test after these changes (4,096 rows × 16 nnz/row, `x` = 512 KiB, `rvv_orig`):

| L1D prefetcher | ROI cycles | Speedup vs. stride | L1D primary miss |
|---|---:|---:|---:|
| stride | 348,903 | 1.00× | 55.0% |
| VIP, indirect off | 346,559 | 1.01× | 54.9% |
| VIP, no look-ahead | 342,504 | 1.02× | 46.0% |
| VIP | 248,680 | 1.40× | 6.7% |

Immediate prefetches alone come too late: the gather follows its index load within a few cycles. **Look-ahead on index-line fills provides the benefit.** The full evaluation is in `results/endterm/phase4b/phase4b_summary.md`.

## 7. Evaluation summary (184 runs, all validated)

Full tables: `results/endterm/phase4b/phase4b_summary.md`; raw data: `phase4b_all.csv`. Speedup is over the L1D stride prefetcher. "Indirect-only" = VIP vs. VIP with `enable_indirect=False`; it separates the new mechanism from the virtual-address stride mode, which alone gives up to 1.07×.

| Input | `rvv_orig` total / indirect-only | `rvv_opt_m1` | `rvv_acc2_m1` | scalar |
|---|---:|---:|---:|---:|
| uniform, `x` = 512 KiB | **1.40× / 1.39×** | 1.01× | **1.86×** | 1.01× |
| power-law, `x` = 512 KiB | **1.43× / 1.42×** | **3.02×** | 1.61× | 1.01× |
| uniform / power-law, `x` = 2 MiB | 1.07× / 1.06×; 1.08× / 1.08× | 1.00× | 1.75×; 1.60× | 1.00× |
| uniform / power-law, `x` = 128 KiB | 0.98× / 0.98×; 0.98× / 0.97× | 1.02×; 1.43× | 1.16×; **0.82×** | 1.02–1.03× |
| `x` ≤ 32 KiB (gathers hit) | 1.04–1.06× / 1.00–1.01× | 1.04–1.05× | 1.02–1.06× | 1.03× |
| web-NotreDame | 1.12× / 1.08× | 1.31× | 1.00× | 1.02× |
| scircuit (VLEN 256 / 512) | 1.10× / 1.08× (VLEN 512: **1.35×**) | 1.02× | 1.10× | 1.02× |
| bcsstk17 | 1.07× / 1.00× | 1.07× | 1.02× | 1.02× |
| luxembourg_osm | 0.96× / 0.94× | 1.01× | 1.01× | 1.01× |

What the counters show (`rvv_orig`, uniform 512 KiB `x`):
- L1D primary miss rate 55.0% → 6.8%;
- L1D prefetch accuracy 24% → 63%, coverage 12% → 86%;
- LQ-full events 628,548 → 335,264;
- about 237K look-ahead targets.

The gain comes from look-ahead: the indirect part without look-ahead gives ≤ 1.06×.

**Limits (open):**
1. **DRAM-resident `x` (2 MiB):** for `rvv_orig`, coverage only 27% and L2 miss rate stays at 71%. Suspected limit: the 16 L1D MSHRs and DRAM latency.
2. **Learning is sometimes missed.** `rvv_opt_m1` issues no targets on uniform rows (it learns on power-law rows and on web-NotreDame). Suspected cause: index loads running more than 32 index vectors ahead of their gathers.
3. **Losses where gathers already hit or the pattern churns** (luxembourg_osm 0.96×, power-law 128 KiB `rvv_acc2_m1` 0.82×). There is no accuracy throttle.

Follow-up results for 1 and 2 (`scripts/run_phase4b_extra.sh`, 70 runs, all validated) are in §9.

## 8. Hardware storage estimate (analytic)

From `scripts/vip_storage.py` (all bit widths are stated in the script: RV64 Sv39 virtual addresses, 44-bit physical addresses, 64-byte lines, 32-bit indices, 16 VIST entries, 2 shifts, 64-entry dedup filter and pending queue).
- **Per-stream:** every VIST entry has its own index history, exactly what the simulator models.
- **Shared:** 2 history buffers shared by the streams being trained or checked. In all runs only 2 streams (the index and value streams of the kernel) were active.
- The candidate buffer is the simulator's upper bound: every (index × shift) candidate of the history kept during learning.

Per-VIST-entry scalar fields (bits):

| field | bits |
|---|---:|
| valid | 1 |
| pc tag | 39 |
| last index VA | 39 |
| last index PPN | 32 |
| last access size | 9 |
| sequential confidence | 3 |
| LRU | 4 |
| history position | 5 |
| history count | 6 |
| gather PC | 39 |
| enabled | 1 |
| base (VA) | 39 |
| shift select | 1 |
| samples | 2 |
| check counter | 3 |
| consecutive fails | 7 |
| **total (history depth 32)** | **230** |

| history depth | history / stream | candidate buffer | dedup + pending queues | **per-stream total** | **shared total (2 history buffers)** |
|---:|---:|---:|---:|---:|---:|
| 4 | 0.25 KiB | 0.62 KiB | 0.56 KiB | 5.66 KiB | 2.13 KiB |
| 8 | 0.50 KiB | 1.25 KiB | 0.56 KiB | 10.33 KiB | 3.26 KiB |
| 16 | 1.01 KiB | 2.50 KiB | 0.56 KiB | 19.66 KiB | 5.53 KiB |
| 32 | 2.02 KiB | 5.00 KiB | 0.56 KiB | 38.32 KiB | 10.05 KiB |
| 64 | 4.04 KiB | 10.00 KiB | 0.56 KiB | 75.64 KiB | 19.09 KiB |
| 128 | 8.08 KiB | 20.00 KiB | 0.56 KiB | 150.27 KiB | 37.18 KiB |

**Reading:**
- At the main evaluation's history depth of 32, the as-simulated structure is about 38 KiB (larger than the 32 KiB L1D); the shared organisation is about 10 KiB.
- **The history-depth study (§9) shows depth 4 gives the same speedups as depth 32** on all 9 tested configurations. So VIP needs **≈ 2.1 KiB** (shared organisation; ≈ 5.7 KiB with a history per entry), about 7% of the L1D.
- The history and the learning-time candidate buffer dominate. The streaming part (VIST scalar fields, dedup filter, pending queue) is about 1.0 KiB.
- Caveat: the small depth was verified on the uniform size sweep only.
- No RTL/Yosys synthesis was done (stretch goal not attempted).

## 9. Status of the follow-up work

- gem5 was rebuilt with a `history_depth` parameter (default 32, same behaviour), and `configs/sparse_rvv.py` gained `--l1d-mshrs`. The patch was regenerated and verified (byte-identical round trip).
- **Regression subtlety found:** program arguments affect timing slightly. The main batch passed `alpha=1.8` (unused for uniform inputs); a check run without it gave 1,388,982 instead of 1,389,015 stride cycles (−0.002%) and 991,706 instead of 990,512 VIP cycles (+0.12%). The argument string changes the initial stack layout. The follow-up script now passes byte-identical arguments. **With identical arguments, the rebuilt gem5 + new config reproduce the main batch exactly:** 1,389,015 stride and 990,512 VIP cycles (`results/endterm/phase4b_prep/regress3_*`).
- **Follow-up batch done** (`scripts/run_phase4b_extra.sh`, 70 runs, all validated; tables in `results/endterm/phase4b/phase4b_summary.md`).
  - **History depth 4–128:** identical speedups at every depth on all 9 configurations (uniform `x` = 128 KiB / 512 KiB / 2 MiB × RVV CSR / 1-accumulator / 2-accumulator). Depth-32 runs reproduce the main runs exactly. **Depth 4 suffices**, so storage is ≈ 2.1 KiB (§8).
  - **`rvv_opt_m1`** issues no targets at any depth on these inputs, so the history-window hypothesis is **rejected**; the cause is still open.
  - **L1D MSHRs 16/32/64:** VIP speedup at 2 MiB `x` grows from 1.75× to 1.95–1.96× for `rvv_acc2_m1`, but *drops* from 1.07× to 1.01× for `rvv_orig` (its VIP run slows from 1,947,754 to 2,069,207 cycles). The MSHR-limit hypothesis holds only for the two-accumulator kernel. At 512 KiB: `rvv_orig` 1.40× → 1.43×.
