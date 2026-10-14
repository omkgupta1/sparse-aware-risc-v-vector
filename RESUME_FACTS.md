# Resume facts (verified)

Each fact below can be checked against the named run directories. All are gem5 simulation results; "ROI cycles" is the kernel-only stats dump.

**How to read the sources**
- **ROI cycles** = `board.processor.cores.core.numCycles` in the **second** dump of `stats.txt.gz` (dump order: setup, kernel, validation). Read it with `python3 scripts/gem5stats.py <dir>/stats.txt`.
- **Origin** says what produced the result:
  - **gem5 C++** = the Vector-Indirect Prefetcher patch (`gem5_patches/sparse_aware.patch`);
  - **software** = kernels/harness only, on the unmodified machine;
  - **methodology** = measurement work.

## Hardware mechanism: Vector-Indirect Prefetcher (gem5 C++)

| # | Fact | Numbers | Source (run directories under `results/endterm/phase4b/`) | Stat |
|---|---|---|---|---|
| 1 | VIP speeds up RVV CSR SpMV by **1.40×** when gathers miss L1 but hit L2 (uniform, `x` = 512 KiB) | 1,389,015 → 990,512 cycles | `size/u_r16384_c131072_npr16/rvv_orig__stride`, `…__vip` | ROI `numCycles` |
| 2 | … **1.39×** of it comes from the indirect mechanism itself (vs. the same prefetcher with `enable_indirect=False`) | 1,376,164 → 990,512 | `…/rvv_orig__vip_noind`, `…__vip` | ROI `numCycles` |
| 3 | **1.43×** on power-law (skewed) rows, `x` = 512 KiB | 1,443,095 → 1,009,028 | `size/p_r16384_c131072_npr16/rvv_orig__{stride,vip}` | ROI `numCycles` |
| 4 | **1.86×** for a two-accumulator RVV kernel (uniform, 512 KiB); **1.60–1.86×** at 512 KiB and 2 MiB | 2,704,389 → 1,452,159 | `size/u_r16384_c131072_npr16/rvv_acc2_m1__{stride,vip}` | ROI `numCycles` |
| 5 | L1D primary miss rate **55.0% → 6.8%** | (fact 1 runs) | as fact 1 | `l1d-cache-0.demandMshrMisses::total / demandAccesses::total` |
| 6 | L1D prefetch coverage **12% → 86%**, accuracy 24% → 63% | (fact 1 runs) | as fact 1 | `l1d-cache-0.prefetcher.coverage`, `.accuracy` |
| 7 | Load-queue-full stalls **−47%** | 628,548 → 335,264 | as fact 1 | `core.rename.LQFullEvents` |
| 8 | Real matrices: scircuit (circuit) **1.35×** at VLEN = 512 (1.10× at 256); web-NotreDame 1.12× | 4,352,115 → 3,222,778 | `vlen512/scircuit/rvv_orig__{stride,vip}`; `suitesparse/web-NotreDame/rvv_orig__{stride,vip}` | ROI `numCycles` |
| 9 | Look-ahead is essential: without it the indirect part gives ≤ 1.06× | – | `…/rvv_orig__vip_nola` vs. `…__vip_noind` | ROI `numCycles` |
| 10 | Default machine unchanged by the patch: identical cycles on patched and original gem5 | 1,388,982 = 1,388,982 | `results/endterm/phase4b_prep/regress_rvv_orig_c131072`, `regress_backup_gem5_c131072` | ROI `numCycles` |
| 11 | Validated: **254/254** Phase 4B runs (main + follow-ups) pass the double-precision reference check | – | `results/endterm/phase4b/phase4b_summary.md` | program `RESULT … validation=PASSED` |
| 12 | Storage estimate ≈ **2.1 KiB** (2 shared history buffers, history depth 4; ≈ 5.7 KiB with a history per entry). Depth 4 gives the same speedup as depth 32 on all 9 tested configurations (e.g. 1.40× / 1.86× at 512 KiB). | analytic + 54 runs | `scripts/vip_storage.py`; `results/endterm/phase4b/history/*/…__vip_h{4..128}` | ROI `numCycles` |

**Size of the gem5 change:** 534 lines of C++ (`vector_indirect.{hh,cc}`) plus Python/SConscript registration; 12 new `statistics::Scalar` counters.

**Do not claim:**
- a single speedup without the regime: gains are 1.00–1.08× when gathers already hit or `x` is DRAM-resident for the midterm kernel, with up to 18% loss in one configuration;
- a synthesised area figure.

## Software (unmodified machine)

| # | Fact | Numbers | Source | Origin |
|---|---|---|---|---|
| 13 | Density-aware hybrid (dense vs. CSR per 64-row block, threshold = measured crossover) is **1.15–1.60×** faster than the best single format on mixed-density matrices, 5.8–7.6× faster than scalar | best: 328,068 (CSR) → 205,557 (hybrid) | `results/endterm/phase4a/mixed/mx_n1024_lo01_hi80_f025/{rvv_orig,dense_m2,hybrid_t58.2}`; table in `phase4a_summary.md` | software |
| 14 | RVV CSR SpMV is **1.41–5.28×** faster than scalar CSR (1024², 1–80% density) | d=1%: 39,543 / 28,030; d=80%: 4,253,309 / 805,778 | `results/endterm/phase3/density/u_n1024_d{01,80}/{scalar,rvv_orig}` | software |
| 15 | **Dense-vs-sparse crossover ≈ 58% density** (CSR/dense time 0.889 at 50%, 1.024 at 60%) | 523,573 / 589,160; 603,687 / 589,497 | `results/endterm/phase3/density/u_n1024_d{50,60}/{rvv_orig,dense_m2}` | software |
| 16 | Gather misses cost up to **72.9%** of kernel time (`x` 8 KiB → 2 MiB, same nonzeros) | 565,377 → 2,085,448 | `results/endterm/phase3/size/u_r16384_c{002048,524288}_npr16/rvv_orig` | software |
| 17 | A "fewer reductions" single-accumulator kernel is **2.73× slower** than the per-chunk-reduction kernel when gathers miss | 5,682,861 vs. 2,085,448 | `results/endterm/phase3/size/u_r16384_c524288_npr16/{rvv_opt_m1,rvv_orig}` | software |
| 18 | SpMM vectorised over the dense columns: **2.8–25.1×** faster than scalar SpMM | e.g. bcsstk17, K=16: 33,316,305 → 1,325,808 | `results/endterm/phase3/spmm/` | software |
| 19 | Mask-based zero skipping on dense data never pays off (0.45–1.00×) despite skipping up to 92% of chunks | – | `results/endterm/phase4a/phase4a_summary.md` | software |

## Methodology and verification

| # | Fact | Source | Origin |
|---|---|---|---|
| 20 | **93.5–96.8%** of the midterm's measured time was not the SpMV kernel; with ROI markers the kernel is 3.2–6.5% of the program | `results/endterm/phase1/phase1_summary.md` (`roi_d20`, `roi_d60`, three stats dumps) | methodology |
| 21 | Code-layout noise reduced from **4.7%** to **< 0.1%** with 64-byte kernel alignment | `PROGRESS.md` Phase 2; `Makefile` (`KALIGN`) | methodology |
| 22 | **4 gem5 25.1 bugs** found and isolated; all reproduced on the unmodified gem5 binary with standalone reproducers, correct on QEMU (and MinorCPU for 1–3) | `docs/gem5_rvv_issues.md`, `docs/gem5_bug_reports/`, `results/endterm/gem5_bug_reports/*.log` | methodology |
| 23 | **807+ gem5 simulations**, every one checked automatically against a reference (failures reported, never hidden); 18 RVV/scalar kernels; QEMU functional suite (120 checks) | `results/endterm/phase5/all_runs_metrics.csv`, `Makefile` | methodology |

## Suggested resume lines

- Designed and implemented a **vector-indirect hardware prefetcher in gem5 (C++)** for RISC-V Vector gather loads. It learns index-to-address patterns under out-of-order execution and prefetches ahead from incoming index cache lines. **Up to 1.43× faster sparse matrix–vector multiplication (1.86× for a two-accumulator kernel), L1D misses 55% → 7%**, with an estimated ≈ 2 KiB of state; validated over 254 simulations.
- Characterised RISC-V Vector SpMV on a cycle-level gem5 model across 800+ simulations, each checked against a reference (1.4–5.3× over scalar; dense/sparse crossover at ≈58% density). Built a **density-aware hybrid kernel, up to 1.6× faster** than the best single format.
- **Found and isolated 4 bugs in the gem5 simulator** (RVV reductions, indexed loads, slides; prefetcher interface) with minimal reproducers cross-checked against QEMU.
