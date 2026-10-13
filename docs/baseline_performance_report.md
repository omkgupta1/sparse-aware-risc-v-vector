# Baseline RVV SpMV Performance Report

## 1. Objective

This experiment establishes an initial performance baseline for sparse matrix-vector multiplication (SpMV) executed using RISC-V Vector Extension (RVV) instructions in gem5. The same workload and processor configuration were evaluated at two matrix sparsity levels.

The purpose is to verify functional correctness, record simulated execution metrics, and examine how changing the number of nonzero matrix entries affects the baseline.

## 2. Experimental Setup

| Parameter | Configuration |
|---|---|
| Simulator | gem5 25.1.0.1 |
| CPU model | RISC-V out-of-order CPU (`RiscvO3CPU`) |
| Clock frequency | 1 GHz |
| Vector length (VLEN) | 256 bits |
| Maximum element width (ELEN) | 64 bits |
| L1 instruction cache | 32 KiB |
| L1 data cache | 32 KiB |
| L2 cache | 512 KiB |
| Main memory model | Single-channel DDR3-1600 |
| Workload | Synthetic SpMV |
| Matrix dimensions | 256 × 256 |
| Compiler options | `-O2 -static -march=rv64gcv -mabi=lp64d` |

The executable was compiled for RV64 with the vector extension enabled. Disassembly confirmed the presence of vector configuration, vector loads, indexed vector loads, vector multiplication, and vector reduction instructions.

The matrix was represented using row pointers, column indices, and nonzero values. The program also computed a scalar reference result and compared it against the RVV result.

## 3. Experimental Method

Two matrix configurations were evaluated while holding the processor, vector, cache, memory, and matrix dimensions constant:

- **Experiment A:** 80% sparsity, corresponding to 20% density.
- **Experiment B:** 40% sparsity, corresponding to 60% density.

For each experiment, the program was executed in gem5 and the `simTicks` and `simInsts` statistics were extracted from `stats.txt`. Correctness was checked using the program's scalar reference calculation.

Simulated time was calculated using the gem5 global tick frequency of \(10^{12}\) ticks per second:

\[
t_{\mathrm{sim}} = \frac{\mathrm{simTicks}}{10^{12}}
\]

At a configured 1 GHz CPU clock, one simulated CPU cycle corresponds to 1,000 gem5 ticks. Therefore, the equivalent cycle count is `simTicks / 1000`.

These timings cover the simulated run represented by the statistics, not a separately instrumented SpMV-only region. They should not be interpreted as real hardware latency or wall-clock runtime on the host machine.

## 4. Results

| Metric | Experiment A | Experiment B |
|---|---:|---:|
| Matrix dimensions | 256 × 256 | 256 × 256 |
| Sparsity | 80% | 40% |
| Density | 20% | 60% |
| Nonzero elements (NNZ) | 12,915 | 39,181 |
| Simulated ticks | 569,393,000 | 744,567,000 |
| Simulated time | 569.393 µs | 744.567 µs |
| Equivalent simulated CPU cycles | 569,393 | 744,567 |
| Simulated instructions | 1,409,447 | 2,073,307 |
| Maximum absolute error vs. scalar reference | 4.77 × 10⁻⁶ | 1.91 × 10⁻⁵ |
| Correctness validation | Passed | Passed |

## 5. Analysis

The 40%-sparse matrix contained 39,181 nonzero elements, compared with 12,915 for the 80%-sparse matrix. This is approximately 3.03 times as many nonzero elements.

The measured simulated time increased from 569.393 µs to 744.567 µs, an increase of approximately 30.8%. The simulated instruction count increased from 1,409,447 to 2,073,307, an increase of approximately 47.1%.

The additional nonzero entries increase the amount of sparse data processed and contribute to the increased instruction count and simulated time. However, the timing change is not proportional to the change in nonzero count. The measurements include the behavior of the complete simulated run, and the workload's memory accesses, row structure, vector operations, and simulation overhead can all affect the result.

Both experiments passed the scalar-reference correctness check. The reported maximum errors remained below the program's validation threshold of \(10^{-4}\), supporting the correctness of the computed output for both tested cases.

### Figures

The companion plotting script generates:
1. Simulated time versus sparsity level.
2. Simulated instruction count versus sparsity level.
3. Number of nonzero elements versus sparsity level.

The figures visualize the two measured cases. With only two configurations, they should be treated as an initial comparison rather than a general performance model.

## 6. Limitations

1. The matrix is synthetic and small; results may not generalize to real scientific matrices with different row-length distributions or access patterns.
2. Only two sparsity configurations were tested.
3. The measurements are from gem5 simulation and do not represent measurements from physical RISC-V hardware.
4. The reported `simTicks` cover the simulation interval represented by the statistics; a dedicated region-of-interest measurement would be needed to isolate only the SpMV kernel.
5. No scalar-only performance baseline has yet been measured under an otherwise identical setup.
6. No sparse-aware hardware modification has yet been compared against this baseline.

## 7. Conclusion

The baseline RVV SpMV executable ran successfully in gem5 at both 80% and 40% sparsity, and both configurations passed correctness validation. The denser configuration contained approximately 3.03 times as many nonzero entries and showed higher simulated instruction count and simulated time.

These results establish an initial functional and performance baseline for the project. A stronger evaluation should add a controlled scalar baseline, more representative matrix patterns, and region-of-interest measurements before making claims about speedup or the benefits of a proposed sparse-aware architecture.

## 8. Reproducibility Artifacts

- `src/spmv_rvv.c` — SpMV implementation and scalar validation.
- `workloads/spmv_rvv` — compiled RISC-V executable.
- `configs/rvv_test.py` — gem5 configuration.
- `results/midterm/spmv_80/run.log` and `results/midterm/spmv_80/stats.txt` — 80%-sparsity run.
- `results/midterm/spmv_40/run.log` and `results/midterm/spmv_40/stats.txt` — 40%-sparsity run.
- `results/midterm/baseline_results.csv` — consolidated measurements.
- `scripts/plot_baseline.py` — script for generating result figures.
