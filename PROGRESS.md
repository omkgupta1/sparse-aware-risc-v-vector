# Progress Log: End-Term Work

Status of the end-term plan (Phases 0–6). Every number here is copied from a
script-generated file under `results/endterm/`; the source file is named next to it.

| Phase | Status |
|---|---|
| 0. Environment | ✅ Done |
| 1. Fix the measurement (ROI) | ✅ Done |
| 2. Proper baselines | ✅ Done (rvv_opt_m4 excluded: gem5 O3 bug) |
| 3. Realistic workloads and sweeps | ⏳ Not started |
| 4A. Software sparse-aware mechanism | ⏳ Not started |
| 4B. Hardware mechanism (indirect gather prefetcher, chosen 2026-10-13) | ⏳ Not started; gem5 C++ changes need approval first |
| 5. Metrics, analysis, plots | ⏳ Not started |
| 6. Reproducibility and documentation | ⏳ Partly started (Makefile, Docker env, gem5 issue write-up) |

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
