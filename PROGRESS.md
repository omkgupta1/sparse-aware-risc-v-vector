# Progress Log: End-Term Work

Status of the end-term plan (Phases 0–6). Every number here is copied from a
script-generated file under `results/endterm/`; the source file is named next to it.

| Phase | Status |
|---|---|
| 0. Environment | ✅ Done |
| 1. Fix the measurement (ROI) | ✅ Done |
| 2. Proper baselines | ⏳ Not started |
| 3. Realistic workloads and sweeps | ⏳ Not started |
| 4A. Software sparse-aware mechanism | ⏳ Not started |
| 4B. Hardware mechanism (indirect gather prefetcher, chosen 2026-10-13) | ⏳ Not started; gem5 C++ changes need approval first |
| 5. Metrics, analysis, plots | ⏳ Not started |
| 6. Reproducibility and documentation | ⏳ Partly started (Makefile, Docker env) |

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
