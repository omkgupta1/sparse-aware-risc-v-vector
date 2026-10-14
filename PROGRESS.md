# Progress Log: End-Term Work

Status of the end-term plan (Phases 0–6). Every number here is copied from a
script-generated file under `results/endterm/`; the source file is named next to it.

| Phase | Status |
|---|---|
| 0. Environment | ✅ Done |
| 1. Fix the measurement (ROI) | ✅ Done |
| 2. Proper baselines | ✅ Done (rvv_opt_m4 excluded: gem5 O3 bug) |
| 3. Realistic workloads and sweeps | ✅ Done (316 runs; LMUL=2 gather kernels unreliable on gem5 O3) |
| 4A. Software sparse-aware mechanism | ✅ Done (128/128 validated) |
| 4B. Hardware mechanism: Vector-Indirect Prefetcher (gem5 C++) | ✅ Implemented and evaluated (184/184 runs validated); storage estimated. Follow-up history-depth / MSHR studies scripted but not run (host on battery) |
| 5. Metrics, analysis, plots | ⏳ Not started |
| 6. Reproducibility and documentation | ⏳ Partly started (Makefile, Docker env, gem5 issue write-up, gem5 bug-report drafts, PROJECT_SUMMARY.md) |

---

## Phase 0: Environment (2026-10-13)

**Done**
- gem5 **25.1.0.1** at `~/gem5-assignment/gem5` (host), with `build/RISCV/gem5.opt` already built (Linux/aarch64, so it runs only in Docker).
- New image `sparse-rvv-env` (`docker/Dockerfile`): Ubuntu 24.04, gem5 runtime and build deps, and **`riscv64-linux-gnu-gcc` 14.2.0** (Ubuntu 14.2.0-4ubuntu2~24.04.1, glibc, static).
- `scripts/env.sh CMD` runs CMD in that image, with the repo mounted at `/project` and `~/gem5-assignment` at `/workspace` (`$GEM5=/workspace/gem5`).
- gem5 m5ops library built for RISC-V: `$GEM5/util/m5/build/riscv/out/libm5.a`. This is a build output in the gem5 tree; no gem5 source was changed.

**Notes**
- The midterm binaries in `workloads/` were built elsewhere with `riscv64-unknown-elf-gcc` 16.1.0 (newlib). All new binaries use GCC 14.2 (glibc). Compiler differences are visible in whole-program numbers (see Phase 1).
- Docker Desktop's VM reports 10 CPUs and about 7.7 GB of RAM. This caps how many gem5 runs can safely run in parallel.

## Phase 1: Fix the measurement (2026-10-13)

**Done**
- Midterm results moved to `results/midterm/`, unmodified. Path references were updated in `docs/baseline_performance_report.md`, `scripts/plot_baseline.py` and `PROJECT_ANALYSIS.md`.
- `src/spmv_rvv_roi.c` is the midterm source, unchanged except for ROI markers around **only** the RVV kernel. `src/common/roi.h` defines the markers.
- Markers use `m5_dump_reset_stats` at both ends, so each run has 3 dumps: prologue / **ROI** / epilogue.
  - The ROI is identical to an `m5_reset_stats` / `m5_dump_stats` pair, and this layout also keeps the prologue.
  - The literal reset/dump pair was also run as a control.
- `scripts/gem5stats.py` is a multi-dump `stats.txt` parser. Named selection (`roi`) raises an error unless exactly 3 dumps exist, so a run whose markers didn't fire can't be mistaken for ROI data. It reads `.gz`.
- `scripts/run_gem5.sh` runs one simulation: it logs the command and exit status, gzips the outputs, and skips completed runs. `scripts/run_phase1.sh` runs 8 simulations in parallel. `scripts/phase1_report.py` produces the CSV and Markdown table.

**Results** (source: `results/endterm/phase1/phase1_summary.{csv,md}`, same gem5 config as the midterm)

| | d = 20% (80% sparse) | d = 60% (40% sparse) |
|---|---:|---:|
| NNZ / validation | 12,915 / PASSED | 39,181 / PASSED |
| **Kernel (ROI) cycles** | **14,782** | **36,058** |
| Kernel instructions | 34,174 | 93,706 |
| Kernel IPC | 2.312 | 2.599 |
| **Kernel cycles per nonzero** | **1.145** | **0.920** |
| Kernel share of whole-program cycles | 3.2% | 6.5% |
| Kernel L1D / L2 miss rate | 0.80% / 0.059% | 0.77% / 0.060% |
| Kernel DRAM bytes read | 512 | 704 |
| Kernel LQ-full events | 1,496 | 6,019 |
| Midterm end-to-end cycles (old number) | 569,394 | 744,568 |

- Going from d=20% to d=60% multiplies NNZ by 3.034×. **Kernel cycles grow 2.439×**, which is 0.810 cycles per extra nonzero. The midterm's end-to-end numbers suggested 1.308× (6.669 cycles per extra nonzero), because generation dominated.
- Kernel LQ-full events grow 4.023×.

**ROI methodology checks** (`results/endterm/phase1/controls/`)
- Executing the m5ops does not slow the program. Whole-program cycles: ROI build 457,119 / 551,578; no-op-call control (same call sites, no m5op) 460,421 / 554,814.
- The ROI value is stable across marker styles: 14,782 vs 14,782 (d20), 36,058 vs 35,955 (d60).
- The unmarked e2e build is 2–7% faster as a whole program (447,458 / 515,275). The disassembly diff shows different register allocation and scheduling of the generation loop once a call is present. This lies outside the kernel and is not a measurement error.
- Instruction counts: ROI build total = e2e + 14 (the two m5op call sequences).
- All 8 runs were re-run with `FORCE=1` from the Makefile-built binaries, with identical results (gem5 is deterministic here).

**Open issues found in Phase 1**
1. **The kernel is fully cache-resident.** DRAM reads are 512–704 B and the L2 miss rate is 0.06%, because the generator has just written the CSR arrays and `x` is 1 KiB. Phase 3 must use footprints larger than L2 and an `x` larger than L1D, and should define a warm-up policy (cold vs. one warm SpMV iteration) explicitly.
2. **The "scalar" validation reference is auto-vectorised** by GCC 14 at `-O2` (`vfredosum`). It is outside the ROI and order-preserving, so Phase 1 is unaffected. Phase 2 must compile the reference without vectorisation.
3. **`commitStats0.numVecInsts` (32,733) is close to `simInsts` (34,174)** in the ROI, which does not match about 8 vector instructions per 8-element chunk. gem5 appears to count vector micro-ops in some instruction counters. Before computing lane utilisation in Phase 5, I'll derive vector-instruction counts from op-class counts (e.g. `SimdFloatMult` = chunks, verified exactly in the midterm analysis) and document the definition.
4. With ROI-only timing the whole-program toolchain difference no longer matters, but old and new end-to-end numbers should not be compared directly (GCC 16 newlib vs. GCC 14 glibc).

## Phase 2: Proper baselines (2026-10-13)

**Done**
- **Shared harness `build/bench`** (`src/harness/bench.c`, `src/harness/matrix.{c,h}`): one binary, kernel chosen by `kernel=`. Every kernel gets the same matrix, the same `x`, one untimed warm-up run, then exactly one ROI run (`cold=1` evicts caches instead).
  - The program prints `CONFIG` / `MATRIX` / `VECTOR` / `RESULT` lines.
  - `VECTOR` gives chunks, `reg_chunks` and lane utilisation, computed from the matrix.
- **Kernels** (`src/kernels/`):
  - `scalar`: built with `-march=rv64gc`; 0 vector instructions, verified by objdump.
  - `rvv_orig`: the midterm kernel.
  - `rvv_opt_m{1,2,4}`: `vfmacc` (tu) accumulator, one horizontal sum per row, `vsll` offsets.
  - `dense_m{1,2,4}`: row-major GEMV with zeros, unit-stride loads.
- **Validation:** a double-precision scalar reference built with `-march=rv64gc -fno-tree-vectorize`. Row *i* passes if |y_i − ref_i| ≤ 1e-4 · Σ_k |a_ik·x_k|; exit code 2 on failure. This also fixes Phase 1 open issue 2 (auto-vectorised reference).
- **Code-layout control:** kernels are compiled with `-falign-functions=64 -falign-loops=64`.
  - Before: the unchanged `rvv_orig` measured 15,351 to 16,079 cycles (4.7%) across builds that changed only other code.
  - After: a 1 KiB address shift moves kernel cycles by ≤9 (<0.1%).
- **Functional testing without gem5:** `build/bench_qemu` (no m5ops) and `make qemu-test`. QEMU 8.2.2 user-mode at VLEN=256 runs 8 kernels × densities 1/20/60/100: **32/32 pass**.
- New gem5 config `configs/sparse_rvv.py`. Same machine as the midterm by default, plus `--cpu {o3,minor,timing}`, `--lq-entries/--sq-entries` and cache sizes.
- `scripts/collect.py` (run.log + ROI stats + op-class counts → record), `scripts/phase2_report.py`, `scripts/run_phase2.sh`.
- `gem5stats.summarize` now also reports **primary** (MSHR) misses. gem5's `demandMisses` includes accesses that merge into in-flight misses.

**Results** (source: `results/endterm/phase2/phase2_summary.{csv,md}`, n=256 midterm matrices, warm caches, VLEN=256, O3)

ROI cycles (speedup vs. scalar):

| Kernel | d=1% | d=5% | d=20% | d=60% | d=100% |
|---|---:|---:|---:|---:|---:|
| scalar | 1,474 (1.00) | 11,774 (1.00) | 57,730 (1.00) | 188,960 (1.00) | 321,932 (1.00) |
| rvv_orig | 1,907 (0.77) | 6,218 (1.89) | 15,463 (3.73) | 36,693 (5.15) | 57,034 (5.65) |
| rvv_opt_m1 | 2,004 (0.74) | 6,261 (1.88) | 15,562 (3.71) | 43,910 (4.30) | 71,239 (4.52) |
| **rvv_opt_m2** | 2,163 (0.68) | **5,471 (2.15)** | **11,319 (5.10)** | 29,143 (6.48) | 47,355 (6.80) |
| dense_m1 | 44,623 | 44,623 | 44,623 | 44,623 | 44,623 |
| dense_m2 | 24,156 | 24,156 | 24,156 | 24,165 | 24,156 |
| **dense_m4** | 17,176 | 17,234 | 17,208 | **16,220 (11.65)** | **14,826 (21.71)** |

**Findings**
1. **The midterm RVV kernel is 3.73× (d=20%) and 5.15× (d=60%) faster than scalar CSR** when only the kernel is timed.
2. **`rvv_opt_m2` is the best validated sparse kernel at d ≥ 5%:** 1.14–1.37× over `rvv_orig` (5.10× vs. scalar at d=20%).
3. **The "optimised" kernel at LMUL=1 is not faster than the midterm kernel:** 0.99× at d=20%, 0.84× at d=60%, 0.80× at d=100%. Its LQ-full events are about 2.5× higher at d=60% (15,106 vs. 5,930).
   - *Hypothesis, not yet verified:* the `vfmacc` accumulator forms a loop-carried dependency (the tu op reads the old accumulator), while the midterm kernel's per-chunk reductions are independent and only a scalar add is chained.
   - To test in Phase 3/5 with a 2-accumulator variant.
4. **At d=1% (about 1.05 nonzeros per row) scalar is fastest.** Every RVV kernel is slower (0.68–0.77×) at 32.8% lane utilisation (m1) or less: short rows waste vector lanes.
5. **Dense vs. sparse at n=256:** sparse wins at d=20% (`rvv_opt_m2` 11,319 vs. `dense_m4` 17,208 cycles), dense wins at d=60% (16,220 vs. 29,143). The crossover lies between 20% and 60%; the Phase 3 sweep will pin it down. Dense time is flat across density, as expected (sanity check).
6. In-harness `rvv_orig` vs. the Phase 1 standalone kernel: +4.6% (d=20%) and +1.8% (d=60%) ROI cycles. The code is the same but layout and calling context differ.

**Failed / excluded**
- **gem5 Issue 1** (`vfredusum` at LMUL ≥ 2 with `vd` overlapping `vs2` reads stale registers) made `rvv_opt_m2`, `rvv_opt_m4` and `dense_m2` fail validation in the first run. Isolated with the directed test `src/tests/rvv_lmul_check.c` (QEMU vs. gem5). Worked around by fold-then-reduce (`src/kernels/rvv_reduce.h`), confirmed by test T12 on gem5. Original failing runs: `results/endterm/phase2/gem5_bug_evidence/`.
- **gem5 Issue 2** (LMUL=4 indexed gather wrong intermittently on O3) makes **`rvv_opt_m4` fail at d=20% in the final run** (and at other densities in other builds or configs). It is correct on MinorCPU and QEMU, and LQ size doesn't fix it. **Excluded from all performance claims.** Evidence: `results/endterm/phase2/gem5_m4_investigation/`.
- Full write-up: `docs/gem5_rvv_issues.md`. Reproduce with `scripts/env.sh scripts/run_gem5_bug_checks.sh`.

**Open issues after Phase 2**
1. **gem5 Issue 2 blocks the LMUL=4 gather data point.** Options: (a) leave it excluded and report it; (b) fix gem5's O3 pinned-write handling for indexed loads, which is a C++ change plus rebuild and needs approval. The upstream bug report could also be a project contribution.
2. Hypothesis 3 (accumulator dependency chain) is not yet verified.
3. All Phase 2 data is still cache-resident (n=256): `x` is 1 KiB and the CSR fits in L2. Phase 3 addresses this.
4. Phase 1's whole-program toolchain caveat still applies to any end-to-end comparison.

## Phase 3: Realistic workloads and sweeps (2026-10-13)

**Done**
- **O(NNZ) generator** (`gen_random` in `src/harness/matrix.c`, SplitMix64, `seed=1`). Columns are drawn by geometric skipping, so they come out sorted.
  - `gen=uniform`: Bernoulli(p) per entry.
  - `gen=powerlaw`: Pareto(α=1.8) expected row lengths, scaled to the target NNZ and capped at `cols`.
  - Measured density is within 2% of the target.
  - Row-length CV: uniform 0.03–0.31; power-law 0.8–3.6. With α<2 the variance is infinite, so the CV depends on the seed; the measured CV is always reported.
- **SuiteSparse:** `scripts/download_matrices.sh` fetches 4 matrices (sha256 manifest), and `scripts/mtx2bin.py` converts `.mtx` → binary CSR (symmetric expansion, synthetic values for pattern matrices, duplicates summed).
  - The harness reads the binary file (`gen=file`), because parsing text inside gem5 would cost about 1–2K simulated instructions per line.
  - `matrices/` is git-ignored.
  - NNZ match the collection: bcsstk17 428,650 · scircuit 958,936 · web-NotreDame 1,497,134 · luxembourg_osm 239,332.
- **New kernels:**
  - `rvv_acc2_m{1,2}`: two vector accumulators.
  - SpMM: `spmm_scalar` (rv64gc); `spmm_rvv_vk` (vectorised over the K dense columns, unit-stride `vfmacc.vf`, m2, no gathers or reductions); `spmm_rvv_colwise` (one m1 gather pass per output column).
  - Double-precision SpMM reference.
- The harness prints row-length CV, CSR/x footprint and per-kernel chunk/lane accounting for SpMV, GEMV and SpMM.
- `make qemu-test`: 65/65 pass (plus VLEN 128/512 spot checks). All 9 SpMV/SpMM kernels pass on all 4 SuiteSparse matrices under QEMU.
- `scripts/run_phase3.sh` (316 runs in 6 groups, 8 in parallel, about 90 min). `scripts/phase3_report.py` writes `results/endterm/phase3/phase3_all.csv` (one row per run, all metrics) and `phase3_summary.md`.
- `collect.py` now also records the CPU model, VLEN and host seconds per run.

**Validation:** 316 runs, no gem5 crashes.
- **All 246 runs that do not use LMUL=2 gathers validated:** scalar, `rvv_orig`, `rvv_opt_m1`, `rvv_acc2_m1`, dense, SpMM.
- **All 25 failures are LMUL=2 gather kernels:** `rvv_acc2_m2` 22/29, `rvv_opt_m2` 3/41.
- gem5 Issue 2 is therefore broader than LMUL=4. `rvv_acc2_m2` on bcsstk17 is correct on MinorCPU and wrong on O3 (`results/endterm/phase3/gem5_acc2_m2_investigation/`).
- **Policy from Phase 3 on:** claims use only scalar, LMUL=1-gather, dense and SpMM kernels. Validated LMUL=2-gather runs are shown but marked. Details in `docs/gem5_rvv_issues.md`.

**Results** (source: `results/endterm/phase3/phase3_summary.md` / `phase3_all.csv`; ROI cycles; O3; VLEN=256 unless stated)

1. **RVV vs. scalar SpMV (uniform 1024×1024):** `rvv_orig` is 1.41× faster at 1% density (mean row 10, m1 lane utilisation 74.3%) and 5.28× at 60–80% (lane utilisation >99%).
2. **Dense-vs-sparse crossover ≈58.2% density** (reliable kernels: `rvv_orig` vs. `dense_m2`; measured ratio 0.889 at 50% and 1.024 at 60%, linearly interpolated). Including the unreliable `rvv_opt_m2` would give ≈63.1%; not used.
3. **Gathers that miss in cache** (16,384 rows × 16 nnz/row, `x` from 8 KiB to 2 MiB):
   - `rvv_orig` L1D primary miss rate goes from 0.1% to 59.3%, and L2 miss rate reaches 70.4% at 2 MiB.
   - Its speedup over scalar falls from 1.91× (32 KiB `x`) to 1.31–1.75× once `x` exceeds L1D.
   - LQ-full events grow 46× (21,747 → 996,601, uniform).
4. **A single vector accumulator collapses when gathers miss.** `rvv_opt_m1` takes 2.73× `rvv_orig`'s time at 2 MiB `x` (uniform), but only 1.02× at 8 KiB. Two accumulators (`rvv_acc2_m1`) recover part of the loss (2.13×).
   - Dense sweep: `acc2_m1` is 1.10–1.18× faster than `opt_m1` at ≥20% density.
   - So the loop-carried `vfmacc` chain is part of the cause (Phase 2 hypothesis 3: partly confirmed). The midterm per-chunk-reduction kernel is the most robust LMUL=1 kernel under misses.
5. **Row-length skew (power-law vs. uniform, n=1024):** 1.07–1.33× slower at 1–2% density, no measurable effect at ≥10%. Lane utilisation barely changes, because n=1024 rows stay long. Short rows matter more:
   - On luxembourg_osm (mean row 2.09, lane utilisation 26.1%) every RVV kernel is slower than scalar (`rvv_orig` 0.92×).
6. **SuiteSparse, `rvv_orig` vs. scalar:** bcsstk17 (FEM, mean row 39) 3.01× · web-NotreDame (web, CV 4.67) 1.67× · scircuit (circuit, mean row 5.6) 1.22× · luxembourg_osm (road, mean row 2.1) 0.92×.
7. **SpMM:** `spmm_rvv_vk` is 2.78–25.13× faster than scalar SpMM and faster than the gather-based `spmm_rvv_colwise` (1.41–7.08×) on every input. Vectorising over the dense dimension avoids gathers entirely.
8. **VLEN (`rvv_orig`):** 128→256 gives 1.47–1.51× on cache-resident inputs but only 1.09–1.11× when `x` misses (2 MiB `x`, scircuit). 256→512 gives 1.04–1.26×. Wider vectors help little once gathers are memory-bound.

**Failed / limits**
- 25 LMUL=2-gather runs failed validation (gem5 Issue 2, above).
- **2 runs exceeded the 10-minute limit** with 8 runs in parallel: luxembourg_osm SpMM K=16 `spmm_scalar` (743 s) and `spmm_rvv_colwise` (798 s). Results are valid; the limit was missed.
- Pilot: SpMM K=16 on web-NotreDame ran more than 12 min, so it was dropped from the grid.

**Open issues after Phase 3**
1. **gem5 Issue 2 now affects LMUL=2 gathers, not just LMUL=4.** Under the current policy (option a) all claims use LMUL=1 gathers, which have never failed. This is enough for Phase 4, since the prefetcher targets m1 gathers. An LMUL sweep for gathers is not possible on gem5 O3 without fixing the bug.
2. **Dense GEMV loads of exactly 128 bytes** (m4@VLEN256, m2@VLEN512) are about 3.8× slower than 64-byte or 256-byte loads, with correct results. Cause unknown; to examine with the prefetcher work (`docs/gem5_rvv_issues.md`).
3. The `results/endterm` total is now 32 MB (`stats.txt.gz` + `config.ini.gz` + logs). That's fine for git, but Phase 4/5 should avoid unnecessary reruns.

## Phase 4A: Software sparse-aware mechanisms (2026-10-13/14)

**Done**
- `src/kernels/gemv_mask.c`, on dense-format input:
  - `dense_skip_m{1,2}`: `vmfne` nonzero mask + `vcpop`. All-zero chunks skip the `x` load and the FMA; other chunks use a masked load and a masked `vfmacc` (tail- and mask-undisturbed).
  - `dense_compress_m1`: `vcompress` packs nonzero values and column indices across chunks into a memory staging buffer. Arithmetic runs only on full vectors of useful elements (one LMUL=1 gather + one full-width `vfmacc`).
  - `dense_compress_slide_m1`: the register-merging variant, kept only as the gem5 Issue 3 reproducer.
- `src/kernels/hybrid.c`, a **density-aware hybrid**: 64-row blocks at or above a threshold (default **58.2% = the measured Phase 3 crossover**) are stored and run densely (the `dense_m2` loop); the others as CSR (the `rvv_orig` loop).
- `gen=mixed` generator: 64-row bands at density `hi` with probability `f`, else `lo`.
- The harness prints `WORK` lines (chunks scanned/skipped, lanes issued, useful lanes, zero lanes) and `HYBRID` lines (dense blocks chosen). Cross-checked against gem5: LMUL=1 `arith_chunks` equals committed `SimdFloatMultAcc` micro-ops in **36/36** runs.
- `make qemu-test`: 120/120 pass (also with QEMU `rvv_ta_all_1s`/`rvv_ma_all_1s` for the compaction kernel).
- `scripts/run_phase4a.sh` (128 runs; baselines re-run with the same binary), `scripts/phase4a_report.py` → `results/endterm/phase4a/phase4a_{all.csv,summary.md}`.

**Results** (source: `results/endterm/phase4a/phase4a_summary.md`; 1024×1024, O3, VLEN=256; **128/128 runs validated**)
1. **Mask-based skipping is never faster than the plain dense kernel:** 0.45–1.00×.
   - At 1% density it skips 92.4% of chunks.
   - But every element of A must still be loaded to build the mask, and `vcpop` plus the branch per chunk costs more than the skipped `x` load and FMA.
   - Only 13–80% of the lanes it issues are useful.
2. **On-the-fly compaction makes the arithmetic 100% useful but is 1.3–6.3× slower than dense** (0.16–0.75×) and 5.9–36.4× slower than CSR. Packing costs far more per element than the FMA it saves. Compaction pays only when done once, ahead of time; that is exactly what the CSR format is.
3. **The density-aware hybrid always matches or beats the better single format:**
   - uniform matrices: 1.00–1.01× of the better of CSR and dense (CSR below 58.2%, all 16 blocks dense at 60% and 80%);
   - **mixed-density matrices: 1.15–1.60× faster than the best single-format kernel** and 5.79–7.62× faster than scalar CSR.
   - Threshold sensitivity: 40% vs. 58.2% differ by <0.5%. At 75%, the 70%-density bands stay in CSR and the `lo20_hi70` case is 1.15× slower (469,196 vs. 406,959 cycles).
4. Main conclusion for the architecture: zero-skipping on dense data cannot remove the cost of *reading* zeros. The win comes from choosing the representation per region (hybrid) and, for sparse regions, from making the irregular gathers cheaper (Phase 4B).

**Failed / fixed**
- **gem5 Issue 3:** the register-slide compaction (`vslideup`/`vslidedown`, tu) faults on O3 (page-table fault on a gathered address), while passing on QEMU (also with agnostic = all-ones) and MinorCPU. The first 4A batch was stopped, the kernel rewritten with memory staging, and all 128 runs re-run with the final binary. Evidence: `results/endterm/phase4a_gem5_compress_investigation/`.
- Issue 1's directed test now also runs on MinorCPU, where it matches QEMU exactly, so all three issues are O3-specific (`results/endterm/phase2/gem5_rvv_check_minor/`).

**Phase 4B preparation (no gem5 source changes)**
- `configs/sparse_rvv.py` gains `--l1d-pf/--l2-pf {stride (default), none, <prefetcher class>}`. The default reproduces an earlier run exactly: `rvv_orig` d=5% gives 66,135 cycles in both cases.
- **The 128-byte dense-load anomaly is explained:** with 128-byte vector loads and the L2 stride prefetcher active, the L1D stride prefetcher fails to train (947 vs. about 252K prefetches identified). With the L2 prefetcher off, `dense_m4` takes 838,938 cycles instead of 2,253,972. Details are in `docs/gem5_rvv_issues.md`.

## Phase 4B: Hardware mechanism, the Vector-Indirect Prefetcher (2026-10-14)

**Approval:** full design approved; build **in place** in `~/gem5-assignment/gem5`. The original binary is kept as `build/RISCV/gem5.opt.pre-sparse-backup`. Pristine copies of the modified gem5 files are in `gem5_patches/pristine/`.

**Done**
- **Feasibility study** (`docs/phase4b_design.md` §1–5). gem5's `IndirectMemoryPrefetcher` cannot train on RVV: it reads indices only from loads of ≤ 8 bytes, and it **panics on read hits** (gem5 Issue 4). Upper bound from Phase 3: gather misses cost up to 72.9% of kernel time.
- **`VectorIndirectPrefetcher`** (`src/mem/cache/prefetch/vector_indirect.{hh,cc}`, 534 lines; `Prefetcher.py`, `SConscript`), a subclass of `StridePrefetcher`:
  - vector index-stream table, keyed by PC;
  - pattern learning `addr = base + (idx << shift)` against a 32-deep history of index vectors (out-of-order tolerant), by intersecting candidates over ≥ 3 gather samples;
  - immediate target prefetches;
  - **look-ahead** from index-line fills (targets queued and issued with the next demand access so they can be translated);
  - virtual addresses with the MMU (page crossing);
  - 12 `statistics::Scalar` counters (`*.prefetcher.vip.*`);
  - parameters `enable_indirect`, `enable_lookahead`, `lookahead_window`, `vist_entries`, `max_targets_per_trigger`, `index_shifts`.
- **Four implementation problems, found by measurement and fixed** (`docs/phase4b_design.md` §6):
  1. hit packets carry no `hasData()`;
  2. out-of-order lag between index loads and gathers;
  3. patterns dropped too eagerly;
  4. prefetch-fill packets have no virtual address or context.
- **Regression:** the patched and original gem5 give identical cycles (1,388,982) on the same workload.
- **Patch:** `gem5_patches/sparse_aware.patch` (`scripts/make_gem5_patch.sh`, plain `diff -ruN`). `scripts/apply_gem5_patch.sh` was verified to reproduce all 4 files byte for byte on pristine copies.
- **Config:** `configs/sparse_rvv.py` gains `--l1d-pf VectorIndirectPrefetcher` (MMU wired automatically) and `--l1d-pf-param KEY=VALUE`. `scripts/collect.py` collects all L1D/L2 prefetcher stats (`pf_l1d_*`, `pf_l2_*`).
- **Evaluation:** `scripts/run_phase4b.sh` + `scripts/phase4b_report.py` → `results/endterm/phase4b/phase4b_{all.csv,summary.md}`. **137/137 completed runs validated.**

**Results** (`rvv_orig` unless stated; speedup vs. the L1D stride prefetcher; the "indirect-only" column = VIP vs. VIP with `enable_indirect=False`, which isolates the new mechanism from the virtual-address stride mode):

| Input | stride → VIP cycles | Total | Indirect-only |
|---|---:|---:|---:|
| uniform, `x` = 512 KiB (L2-resident) | 1,389,015 → 990,512 | **1.40×** | **1.39×** |
| uniform, `x` = 2 MiB (DRAM) | 2,084,368 → 1,947,754 | 1.07× | 1.06× |
| power-law, `x` = 2 MiB | 2,088,357 → 1,930,770 | 1.08× | 1.08× |
| web-NotreDame (web graph) | 5,061,777 → 4,532,898 | 1.12× | 1.08× |
| scircuit (circuit) | 4,554,701 → 4,143,256 | 1.10× | 1.08× |
| scircuit, **VLEN=512** | 4,352,115 → 3,222,778 | **1.35×** | – |
| bcsstk17 (FEM, gathers hit) | 640,787 → 598,375 | 1.07× | 1.00× |
| uniform, `x` = 8 / 32 KiB (gathers hit) | – | 1.05× / 1.06× | 1.00× / 1.01× |
| uniform, `x` = 128 KiB | 904,211 → 923,408 | 0.98× | 0.98× |
| luxembourg_osm (road, 2.1 nnz/row) | 1,959,394 → 2,050,062 | 0.96× | 0.94× |

- **Other kernels:**
  - `rvv_acc2_m1` (two accumulators): **1.86×** at 512 KiB `x` and **1.75×** at 2 MiB (L1D primary miss 63.3% → 22.1%).
  - `rvv_opt_m1`: 1.31× on web-NotreDame, but about 1.00× on the synthetic sweep, where its pattern is **never learned** (0 targets).
  - Scalar CSR: 1.00–1.03× (no harm).
- **Mechanism (512 KiB `x`):** L1D primary miss 55.0% → 6.8%; L1D prefetch accuracy 24% → 63%, coverage 12% → 86%; LQ-full events 628,548 → 335,264; about 237K look-ahead targets in the ROI.
- **Ablation:**
  - without look-ahead the indirect part gives ≤ 1.06×, so **look-ahead provides almost all of the gain**;
  - with no L1D prefetcher at all: 0.81–0.99×.
- **Look-ahead window:** 256 B / 1 KiB / 4 KiB give 1.37× / 1.40× / 1.40× (512 KiB `x`) and 1.07× for all three at 2 MiB.

**Interpretation and open questions**
1. **Biggest win when gathers miss L1D but hit L2.** At 2 MiB (L2 miss rate 71%) coverage drops to 27%. Hypothesis: the L1D's 16 MSHRs and DRAM latency limit how many target prefetches can be outstanding. To test: `--l1d-mshrs` sweep (option prepared, not yet run).
2. **`rvv_opt_m1` never learns on long uniform rows.** Hypothesis: its loop-carried `vfmacc` chain lets index loads run more than 32 index vectors ahead of their gathers, outside the history window. To test: history-depth sweep (`history_depth` parameter added to the source; rebuild pending until the running batch ends).
3. **Small losses (0.94–0.98×) where gathers already hit** (luxembourg_osm, 128 KiB `x`): useless prefetches raise L2 misses (luxembourg 26.5% → 36.5%). An accuracy-based throttle is future work.
4. Hardware storage estimate not done yet. It depends on 2: the history is the dominant storage.

**Bug found in my own scripts:** `scripts/run_phase4b.sh` reused the loop variable `c` inside a helper, so all power-law size-sweep jobs ran with `cols=<config name>` and failed immediately (11 runs). Fixed (`local` variables); the bogus results were deleted and the power-law inputs are being re-run. The first batch's log is kept as `results/endterm/phase4b/run_phase4b_batch1.log`.

**gem5 bug reports:** `docs/gem5_bug_reports/` has 4 issue drafts with standalone reproducers (`repro/`). **All 4 were reproduced on the unmodified gem5 25.1.0.1 binary** (O3 wrong/crash; MinorCPU and QEMU correct; Issue 4 also crashes on MinorCPU). Evidence: `results/endterm/gem5_bug_reports/*.log`. Not filed.

### Phase 4B final update (2026-10-14)

**Evaluation complete: 184/184 runs validated** (`results/endterm/phase4b/phase4b_summary.md`, `phase4b_all.csv`). The power-law size sweep was re-run after the script fix.

| Input | `rvv_orig` total / indirect-only | `rvv_opt_m1` | `rvv_acc2_m1` | scalar |
|---|---:|---:|---:|---:|
| uniform, `x` = 512 KiB | 1.40× / 1.39× | 1.01× | 1.86× | 1.01× |
| power-law, `x` = 512 KiB | 1.43× / 1.42× | **3.02×** | 1.61× | 1.01× |
| uniform / power-law, `x` = 2 MiB | 1.07× / 1.06×; 1.08× / 1.08× | 1.00× | 1.75×; 1.60× | 1.00× |
| uniform / power-law, `x` = 128 KiB | 0.98× / 0.98×; 0.98× / 0.97× | 1.02×; 1.43× | 1.16×; **0.82×** | 1.02–1.03× |
| web-NotreDame / scircuit / bcsstk17 / luxembourg_osm | 1.12× / 1.10× / 1.07× / 0.96× | 1.31× / 1.02× / 1.07× / 1.01× | 1.00× / 1.10× / 1.02× / 1.01× | 1.01–1.02× |

**Hardware storage estimate** (`scripts/vip_storage.py`, `docs/phase4b_design.md` §8):
- At the evaluated history depth 32: **≈ 38 KiB** as simulated (per-stream history, larger than the L1D) or **≈ 10 KiB** with 2 shared history buffers.
- At depth 8: 10.3 KiB / 3.3 KiB. At depth 4: 5.7 KiB / 2.1 KiB.
- The streaming part (VIST scalar fields + dedup filter + pending queue) is about 1.0 KiB.
- **Whether a small history keeps the speedups is not yet measured**, so only the depth-32 numbers are claimable.

**gem5 rebuilt** with a `history_depth` parameter (default 32), and `configs/sparse_rvv.py` gained `--l1d-mshrs`. Patch regenerated and round-trip verified (`vector_indirect.{hh,cc}` now 534 lines).
- **Regression:** with byte-identical program arguments, the rebuilt gem5 and new config reproduce the main batch exactly (1,389,015 stride / 990,512 VIP cycles).
- **Finding:** program arguments alone shift timing slightly. A run without the (unused) `alpha=1.8` argument gave −0.002% / +0.12%. Comparisons must always use identical argument strings.

**Not done (reason: host)** — `scripts/run_phase4b_extra.sh` (70 runs: history depth 4–128 × 3 kernels × 3 inputs; L1D MSHRs 32/64, stride vs. VIP):
- The Mac was on battery and repeatedly suspended: two normally 2-minute regression runs took 87 min and 56 min.
- The report sections for these studies are already written.
- To run on mains power: `scripts/env.sh scripts/run_phase4b_extra.sh` (about 30 min), then `python3 scripts/phase4b_report.py`.

**Open questions** (unchanged until that batch runs):
1. Is the 2 MiB case MSHR-limited?
2. Why does `rvv_opt_m1` miss the pattern on uniform rows (history window?)
3. Accuracy throttling for the cases where VIP loses up to 18% (power-law 128 KiB `rvv_acc2_m1`).

#### Review summary (Phase 4B, all uncommitted)

| Change | File(s) | What |
|---|---|---|
| Added | `gem5_patches/sparse_aware.patch` | The gem5 change (4 files; plain `diff -ruN`) |
| Added | `gem5_patches/pristine/src/mem/cache/prefetch/{Prefetcher.py,SConscript}` | Unmodified gem5 25.1.0.1 copies used to regenerate the patch |
| Added | `scripts/make_gem5_patch.sh`, `scripts/apply_gem5_patch.sh` | Regenerate / apply (with dry run) the patch |
| Added | `scripts/run_phase4b.sh`, `scripts/run_phase4b_extra.sh`, `scripts/phase4b_report.py`, `scripts/vip_storage.py` | Phase 4B batch, follow-up batch, report, storage estimate |
| Modified | `configs/sparse_rvv.py` | `--l1d-pf`, `--l2-pf`, `--l1d-pf-param KEY=VALUE` (MMU wired for VIP), `--l1d-mshrs`; defaults unchanged (verified) |
| Modified | `scripts/collect.py` | Records prefetcher config and all L1D/L2 prefetcher stats (`pf_l1d_*`, `pf_l2_*`) |
| Added | `docs/phase4b_design.md` | Design, feasibility, implementation changes, results, storage estimate, status |
| Modified | `docs/gem5_rvv_issues.md` | Issue 4 (IMP panic); 128-byte stride-prefetcher observation explained |
| Added | `docs/gem5_bug_reports/` (README, 4 issue drafts, `repro/` sources, config, runner) | Ready-to-file gem5 bug reports; duplicate check done |
| Added | `results/endterm/phase4b/**` | 184 runs (`stats.txt.gz`, `config.ini.gz`, `run.log`, `cmd.txt`), `phase4b_{all.csv,summary.md}`, batch logs |
| Added | `results/endterm/phase4b_prep/**` | Smoke tests, regression checks, IMP-panic evidence, 128-byte anomaly runs |
| Added | `results/endterm/gem5_bug_reports/*.log` | Bug-reproducer evidence on unmodified gem5 (binaries and raw outputs git-ignored) |
| Modified | `.gitignore` | Ignores reproducer binaries and raw gem5 output |
| Modified | `PROGRESS.md`, `PROJECT_SUMMARY.md` | Phase 4B results |
| **Outside the repo** | `~/gem5-assignment/gem5/src/mem/cache/prefetch/{vector_indirect.hh,vector_indirect.cc,Prefetcher.py,SConscript}`; `build/RISCV/gem5.opt` rebuilt; original kept as `build/RISCV/gem5.opt.pre-sparse-backup` | The in-place gem5 change you approved. Restore the original binary with `cp gem5.opt.pre-sparse-backup gem5.opt`. |
