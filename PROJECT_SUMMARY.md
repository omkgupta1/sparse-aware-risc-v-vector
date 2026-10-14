# Sparse-Aware RISC-V Vector Architecture: Project Summary and Results

Everything this project built and measured, written so it can be turned into resume lines and interview answers.

- **Where the numbers come from:** every number is from a gem5 simulation run in this project, or computed by a script from those runs. The source file is named in each section.
- **Status (2026-10-14):** Phases 1–4 are complete. **Phase 4B (the hardware prefetcher) is implemented and evaluated: 184 runs, all validated, with a storage estimate.** Two follow-up studies (history depth, L1D MSHRs) are scripted but not run. Phases 5–6 (plots, final report) are not done yet.

---

## 1. What the project is (30-second version)

- **The problem.** Sparse matrix–vector multiplication (SpMV, `y = A·x`) is the core of scientific computing, graph analytics and sparse ML. On vector processors it is slow because of **gathers**: loading `x[col[k]]` at irregular addresses.
- **What I studied.** How the **RISC-V Vector Extension (RVV 1.0)** handles this, on a cycle-level simulator (**gem5**) modelling an out-of-order RISC-V core with 256-bit vectors.
- **What I built:**
  - a measurement infrastructure;
  - 18 kernel variants (scalar, several RVV SpMV designs, dense GEMV, SpMM, mask/compaction kernels, a density-aware hybrid);
  - a **new hardware prefetcher inside gem5**, the *Vector-Indirect Prefetcher* (VIP), that prefetches gather targets.
- **Along the way:** I found and isolated **4 bugs in gem5 25.1** itself.

**Scale:** 737+ gem5 simulations · about 2,000 lines of C (RVV intrinsics) · about 2,000 lines of Python/shell tooling · 534 lines of new gem5 C++/Python · 4 real-world matrices from the SuiteSparse collection.

---

## 2. What I built

| Component | What it is | Where |
|---|---|---|
| Reproducible environment | Docker image with gem5 build deps, RISC-V GCC 14.2 cross-compiler and QEMU; one-command wrapper | `docker/Dockerfile`, `scripts/env.sh` |
| Region-of-interest (ROI) measurement | gem5 m5ops (`m5_dump_reset_stats`) around *only* the kernel; multi-dump stats parser | `src/common/roi.h`, `scripts/gem5stats.py` |
| Shared benchmark harness | One binary, 18 kernels, identical inputs, warm-up policy, ROI, **automatic validation** of every run against a double-precision scalar reference (relative error) | `src/harness/bench.c` |
| Workload generators | O(NNZ) fixed-seed generator (uniform, power-law row lengths, mixed-density bands); Matrix Market → binary CSR converter; SuiteSparse download script | `src/harness/matrix.c`, `scripts/mtx2bin.py`, `scripts/download_matrices.sh` |
| Kernels (RVV 1.0 intrinsics) | scalar CSR; midterm RVV CSR; optimised RVV CSR (LMUL 1/2/4); two-accumulator RVV CSR; dense RVV GEMV; SpMM (scalar, vectorised over K, column-wise gather); mask-skip GEMV; compaction (`vcompress`) GEMV; density-aware hybrid | `src/kernels/` |
| Functional test path | Same kernels run in seconds under QEMU (`make qemu-test`, 120 checks) | `Makefile` |
| gem5 config | Midterm machine by default; CPU model, LQ/SQ size, cache sizes and prefetchers (with parameters) selectable | `configs/sparse_rvv.py` |
| **Hardware mechanism** | **Vector-Indirect Prefetcher**: new gem5 `SimObject` (C++), exported as a patch made with `diff` | `gem5_patches/sparse_aware.patch`, `docs/phase4b_design.md` |
| Analysis | Per-phase collectors and reports → CSV + Markdown | `scripts/collect.py`, `scripts/phase*_report.py` |

**Simulated machine** (same as the midterm): RISC-V out-of-order core (`RiscvO3CPU`) at 1 GHz, 8-wide, 192-entry ROB, 32-entry load queue · RVV with VLEN = 256 bit (128 and 512 also swept) · 32 KiB L1I/L1D, 512 KiB L2, stride prefetchers · DDR3-1600 · gem5 25.1.0.1 in syscall-emulation mode.

---

## 3. Results, phase by phase

### Phase 1: fixing the measurement
*Source: `results/endterm/phase1/phase1_summary.md`*

The midterm timed the **whole program**. With ROI markers around only the kernel:

| | 80% sparse | 40% sparse |
|---|---:|---:|
| Kernel cycles (ROI) | 14,782 | 36,058 |
| Kernel share of the whole program | **3.2%** | **6.5%** |
| Cycles per nonzero | 1.145 | 0.920 |
| Midterm end-to-end cycles | 569,394 | 744,568 |

- **93.5–96.8% of the midterm's measured time was not the SpMV kernel:** mostly O(n²) input generation (87% at 80% sparsity), plus validation and startup. 3× more nonzeros cost the kernel 2.44× more time; the midterm numbers had suggested 1.31×.
- **The methodology was checked against controls:**
  - a build with empty functions in place of the markers proves the m5ops themselves don't slow the program;
  - two marker styles give the same ROI;
  - all runs are deterministic.

### Phase 2: proper baselines
*Source: `results/endterm/phase2/phase2_summary.md` (n=256)*

- **RVV vs. scalar CSR:** 3.73× (80% sparse) and 5.15× (40% sparse).
- **At 99% sparsity (about 1 nonzero per row) scalar beats every vector kernel** (RVV 0.77×): lanes sit mostly idle.
- **Code-layout noise found and removed:** the *unchanged* kernel moved 4.7% between builds. With 64-byte function/loop alignment, layout effects fell below 0.1%.

### Phase 3: realistic workloads and sweeps (316 runs)
*Source: `results/endterm/phase3/phase3_summary.md`, `phase3_all.csv`*

| Finding | Number |
|---|---|
| RVV SpMV speedup over scalar (1024×1024) | **1.41× at 1% density → 5.28× at 60–80%** |
| **Dense-vs-sparse crossover** (where storing the zeros wins) | **≈58% density** |
| Gathers that miss in cache (`x` from 8 KiB to 2 MiB) | L1D misses 0.1% → **59%**, L2 misses up to 70%, load-queue-full stalls **×46** |
| Time lost to gather misses (`x` = 2 MiB) | **72.9%** of kernel time (upper bound for any prefetcher: 3.69×) |
| "Optimised" single-accumulator kernel when gathers miss | **2.73× slower** than the simple per-chunk-reduction kernel (only 1.02× when `x` fits in L1); two accumulators recover part (2.13×) |
| Real matrices (SuiteSparse), RVV vs. scalar | FEM bcsstk17 3.01× · web graph web-NotreDame 1.67× · circuit scircuit 1.22× · **road network 0.92×** (2 nonzeros/row → 26% lane use) |
| Skewed (power-law) row lengths | 7–33% slower at 1–2% density; no effect at ≥10% |
| SpMM, second workload (K = 4–16) | vectorising over the dense dimension: **2.8–25.1× over scalar**; always beats the gather-based variant (1.4–7.1×) |
| Wider vectors (VLEN 128→256) | 1.47–1.51× when data is cached, **only 1.09–1.11× when gathers miss** |

**Validation:** all 246 runs of scalar, LMUL=1-gather, dense and SpMM kernels validated. All 25 failures were LMUL=2 gather kernels, caused by a gem5 bug (section 4).

### Phase 4A: software sparse-aware mechanisms (128 runs, 128/128 validated)
*Source: `results/endterm/phase4a/phase4a_summary.md`*

| Mechanism | Result |
|---|---|
| Mask-based zero skipping on dense data (`vmfne` + `vcpop`, masked `vfmacc`) | **Never faster: 0.45–1.00×.** Skips up to 92% of chunks, but must still *load* every zero to build the mask. |
| On-the-fly compaction (`vcompress` + packing) | Arithmetic becomes 100% useful, but the kernel is 1.3–6.3× slower than dense and 5.9–36× slower than CSR. Packing costs more than the FMAs it saves; precomputed compaction (= CSR) is what wins. |
| **Density-aware hybrid** (dense or CSR per 64-row block, threshold = measured 58.2% crossover) | Matches the better format on uniform matrices (1.00–1.01×). **On mixed-density matrices it is 1.15–1.60× faster than the best single format and 5.8–7.6× faster than scalar.** |

- The hybrid's threshold is robust: 40% and 58.2% differ by <0.5%.
- Work accounting (useful vs. zero lanes) computed by the program matches gem5's committed micro-op counts **36/36** times.
- **Side finding:** dense loads of exactly 128 bytes ran 3.8× slower. The cause is a **stride-prefetcher training failure** in gem5: 947 instead of about 252,000 prefetches identified when both L1D and L2 prefetchers are active. With the L2 prefetcher off, the same kernel takes 838,938 instead of 2,253,972 cycles.

### Phase 4B: hardware mechanism, the Vector-Indirect Prefetcher (VIP)
*Sources: `docs/phase4b_design.md`, `results/endterm/phase4b/phase4b_summary.md`, `phase4b_all.csv`, `PROGRESS.md`*

**Why:** gem5's only indirect prefetcher (IMP) **cannot help RVV code**. It reads indices only from loads of ≤ 8 bytes (an RVV index load is 32 bytes = 8 indices), and in this gem5 version it **crashes on every read hit** (section 4).

**What VIP does** (new gem5 `SimObject`, 534 lines of C++, a subclass of the stride prefetcher, attached to the L1D):
1. **Detects vector index streams:** sequential loads of ≥ 8 bytes of 32-bit indices, tracked per PC.
2. **Learns `base` and `shift`** of the gather (`addr = base + idx << shift`) from the gather's element accesses. Because the out-of-order core runs later index loads ahead of earlier gathers, it matches against a **history of 32 recent index vectors** and intersects candidates over ≥ 3 samples.
3. **Prefetches gather targets:**
   - immediately, from each index load;
   - and, crucially, by **look-ahead**: when an index cache line *fills* ahead of use, it reads the 16 indices in that line and prefetches their targets.
4. Works on **virtual addresses** with MMU translation, so it can cross pages.
5. Adds **12 new statistics counters**.
6. Exported as `gem5_patches/sparse_aware.patch` (plain `diff`). The default machine is unchanged: identical cycles on patched vs. original gem5.

**Results** (184 runs, all validated). Speedup is over gem5's stride prefetcher. "Indirect-only" = VIP vs. the same prefetcher with the indirect part switched off; it isolates the new mechanism from the virtual-address stride mode, which alone gives up to 1.07× on some inputs.

| Input (`rvv_orig` kernel) | stride → VIP cycles | Total | Indirect-only |
|---|---:|---:|---:|
| uniform, `x` = 512 KiB (gathers miss L1, hit L2) | 1,389,015 → 990,512 | **1.40×** | **1.39×** |
| power-law rows, `x` = 512 KiB | 1,443,095 → 1,009,028 | **1.43×** | **1.42×** |
| scircuit (circuit), **VLEN=512** | 4,352,115 → 3,222,778 | **1.35×** | – |
| web-NotreDame (web graph) | 5,061,777 → 4,532,898 | 1.12× | 1.08× |
| scircuit (circuit), VLEN=256 | 4,554,701 → 4,143,256 | 1.10× | 1.08× |
| `x` = 2 MiB (gathers go to DRAM), uniform / power-law | – | 1.07× / 1.08× | 1.06× / 1.08× |
| inputs where gathers already hit (bcsstk17, small `x`) | – | 1.05–1.07× | 1.00× |
| luxembourg_osm (road network) / 128 KiB `x` (uniform, power-law) | – | 0.96× / 0.98×, 0.98× | 0.94× / 0.98×, 0.97× |

| Other kernels | Result |
|---|---|
| Two-accumulator RVV kernel (`rvv_acc2_m1`) | **1.60–1.86×** at 512 KiB and 2 MiB `x` (L1D misses 63% → 22%), but **0.82×** on the power-law 128 KiB input |
| Single-accumulator kernel (`rvv_opt_m1`) | **3.02×** on power-law rows at 512 KiB, 1.31× on web-NotreDame, 1.43× power-law 128 KiB; about 1.00× on long uniform rows, where VIP never learns the pattern (open question) |
| Scalar CSR | 1.00–1.03× (no harm) |

**How it works (512 KiB `x`):**
- L1D primary miss rate: **55.0% → 6.8%**
- L1D prefetch accuracy: 24% → 63%
- L1D prefetch coverage: **12% → 86%**
- Load-queue-full stalls: **628,548 → 335,264** (−47%)

**Ablation:**
- Removing look-ahead leaves ≤ 1.06× from the indirect part, so **look-ahead provides almost all of the gain**.
- The look-ahead window is robust: 256 B / 1 KiB / 4 KiB give 1.37× / 1.40× / 1.40×.

**Limits found:**
- The gain shrinks when gather targets come from DRAM (coverage drops to 27%; L1D MSHRs suspected).
- Patterns are not learned when index loads run very far ahead (the `rvv_opt_m1` case).
- Small losses (2–6%) when gathers already hit, because useless prefetches pollute the L2. An accuracy throttle would fix this.

**Hardware cost (analytic, `scripts/vip_storage.py`):**
- At the evaluated configuration (history of 32 index vectors): **≈ 10 KiB** with 2 shared history buffers, or ≈ 38 KiB as literally simulated (a history per table entry).
- The streaming part (stream table + filters) is about 1 KiB; the index history dominates.
- A 2–3 KiB design (history depth 4–8) would need the history-depth study, which is scripted (`scripts/run_phase4b_extra.sh`) but not run.

---

## 4. gem5 bugs found and isolated (`docs/gem5_rvv_issues.md`)

Each one has a minimal reproducer, evidence runs, and a cross-check against QEMU and/or gem5's in-order MinorCPU.

| # | Bug in gem5 25.1.0.1 | How it was isolated | Workaround |
|---|---|---|---|
| 1 | **`vfredusum` at LMUL ≥ 2 reads stale register data** when the destination overlaps the source group (legal RVV; GCC emits it), after tail-undisturbed writes | 12-case inline-assembly test: QEMU and MinorCPU correct, O3 wrong; the wrong values change with timing | Fold the group to LMUL=1 with `vfadd`, then reduce |
| 2 | **Indexed gathers (`vluxei32`) at LMUL=4, and LMUL=2 under cache misses, give intermittently wrong results** on O3 | MinorCPU and QEMU correct; bigger load queue doesn't fix it; failure rate rises with in-flight gather micro-ops. Suspect: O3 "pinned" destination register writes | Claims use only LMUL=1 gathers (never failed in 246+ runs) |
| 3 | **Tail-undisturbed `vslideup`/`vslidedown` buffer** read at full width → page-table fault on O3 | QEMU (even with agnostic elements = all ones) and MinorCPU correct | Stage through memory instead of registers |
| 4 | **`IndirectMemoryPrefetcher` panics on every read hit** | Prefetchers are notified of hits before the packet becomes a response; `PrefetchInfo` therefore never captures data | VIP reads the packet directly |

---

## 5. The most interesting insights (good interview material)

1. **Measure the right thing.** About 94–97% of the original "SpMV time" was not the kernel (mostly matrix generation). Fixing the ROI changed every conclusion about scaling.
2. **Vector SpMV is limited by gathers, not arithmetic.** Once `x` misses in the cache, up to 73% of kernel time is gather stalls, and doubling vector width buys only 9–11%.
3. **A textbook optimisation can backfire.** Accumulating in one vector register (fewer reductions) is 2.7× *slower* when gathers miss: the loop-carried dependency serialises misses. A two-accumulator version partly recovers.
4. **You can't skip zeros you have to load.** Masking and compaction on dense data never paid off. Choosing the *representation* per region (density-aware hybrid) gave up to 1.6×.
5. **Dense beats sparse above ≈58% density** on this machine, and short-row matrices (road networks, about 2 nonzeros/row) are *slower* with vectors than scalar.
6. **Prefetching gathers needs look-ahead.** Prefetching from the index load is too late (1.02×); prefetching from index lines as they arrive in the cache gives 1.40×.
7. **Simulators have bugs too.** Systematic validation (every run checked against a reference, cross-checked on QEMU and a second CPU model) caught 4 gem5 bugs that would otherwise have silently corrupted results.

---

## 6. Resume bullets (pick 2–4; numbers are verified, Phase 4B marked)

**Research / architecture focus**
- Designed and implemented a **Vector-Indirect Prefetcher in the gem5 simulator (C++)** for RISC-V Vector gather loads. It learns index-to-address patterns despite out-of-order execution and prefetches ahead from incoming index cache lines. **Up to 1.43× faster sparse matrix–vector multiplication (1.6–1.9× for a two-accumulator kernel), L1D misses 55% → 7%, prefetch coverage 12% → 86%** over gem5's stride prefetcher, validated over 184 simulations; 1.08–1.35× on real circuit and web-graph matrices.
- Characterised **RISC-V Vector (RVV 1.0) sparse matrix–vector multiplication on a cycle-level gem5 model** across 700+ simulations: 1.4–5.3× speedup over scalar, a dense/sparse crossover at ≈58% density, and gather misses costing up to 73% of runtime.
- Built a **density-aware hybrid SpMV** that picks dense or sparse execution per row block from the measured crossover: **up to 1.6× faster than the best single format** and 5.8–7.6× faster than scalar on mixed-density matrices.

**Engineering / verification focus**
- **Found and isolated 4 functional bugs in the gem5 25.1 simulator** (RVV reductions, indexed loads, slides; prefetcher interface) using minimal inline-assembly reproducers cross-checked against QEMU and a second CPU model.
- Built a reproducible **simulation and validation pipeline** (Docker, RISC-V GCC/QEMU, gem5 region-of-interest instrumentation, automated per-run correctness checks, CSV reporting) for 18 hand-vectorised kernels (RVV C intrinsics), including SpMM and real SuiteSparse matrices.
- **Corrected the project's own methodology:** showed that 94–97% of the previously reported runtime was not the measured kernel, and removed code-layout noise (4.7% → <0.1%) via alignment.

**One-line version**
> Sparse-aware RISC-V vector architecture (gem5, C++, RVV): new gather prefetcher (up to 1.4× SpMV speedup, 55%→7% L1D misses), density-aware hybrid kernel (up to 1.6×), 4 gem5 simulator bugs found and reproduced.

**Skills:** computer architecture · RISC-V / RVV 1.0 · gem5 (C++ SimObjects, prefetchers, classic caches, O3 CPU) · cache and memory-hierarchy analysis · SIMD/vector programming (C intrinsics, inline assembly) · performance methodology · Docker · QEMU · Python data analysis · Bash.

---

## 7. Be careful when claiming

- **All results are from simulation** (gem5 SE mode), not real hardware.
- **Phase 4B gains depend on the regime.** Say "up to 1.4×" (gathers that miss L1 but hit L2). Typical gains on real matrices are 1.08–1.12× (1.35× on scircuit at VLEN=512); there are small losses (2–6%) where gathers already hit. When asked about the mechanism itself, quote the *indirect-only* numbers (1.39× / 1.08×).
- **Hardware cost is an analytic storage estimate** (≈ 10 KiB, shared-history organisation, at the evaluated configuration), not synthesis. Don't claim "low overhead" or "a few KiB" until the history-depth study shows a small history keeps the speedup.
- LMUL ≥ 2 gather results are excluded because of gem5 bugs. Say "LMUL=1 kernels" if asked about vector length.
- Matrices are synthetic plus 4 SuiteSparse matrices; the largest is 1.5M nonzeros (bounded by simulation time).

## 8. Where everything is

| What | File |
|---|---|
| Progress log with all phases, failures and open issues | `PROGRESS.md` |
| Phase result tables | `results/endterm/phase{1,2,3,4a,4b}/phase*_summary.md` |
| Raw per-run data (CSV) | `results/endterm/phase3/phase3_all.csv`, `phase4a/phase4a_all.csv`, `phase4b/phase4b_all.csv` |
| Per-run gem5 stats / logs | `results/endterm/**/stats.txt.gz`, `run.log` |
| gem5 bugs | `docs/gem5_rvv_issues.md` |
| Prefetcher design and implementation notes | `docs/phase4b_design.md` |
| gem5 patch | `gem5_patches/sparse_aware.patch` |
| Midterm analysis | `PROJECT_ANALYSIS.md` |
