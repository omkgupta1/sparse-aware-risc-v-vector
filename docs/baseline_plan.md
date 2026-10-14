# Baseline Experiment Plan

## Test 1: RVV Vector Addition
- Purpose: Validate compilation and vector instruction generation.
- ISA target: RV64GCV
- ABI: LP64D
- Vector length (VLEN): 256 bits
- Maximum element width (ELEN): 64 bits
- Expected output: 11 22 33 44 55 66 77 88

## Test 2: Sparse Matrix-Vector Multiplication
- Representation: Compressed Sparse Row (CSR)
- Workloads: At least two sparsity levels
- Validation: Compare output against a reference implementation
- Metrics: Simulated execution time and gem5 statistics

## Status (updated 2026-10-14)
- RISC-V compiler detected.
- RVV compiler macros verified.
- Custom binary execution in gem5: **done**. The vector-addition test ran in gem5 (`results/midterm/rvv_test/stats.txt`: 15,458 instructions, 11 committed vector operations). Its printed output was not saved; the end-term directed test `src/tests/rvv_lmul_check.c` checks vector results against QEMU explicitly.
- SpMV measurements: **done** at 80% and 40% sparsity, both validated (`results/midterm/`, `docs/baseline_performance_report.md`).
- Superseded by the end-term work: kernel-only (ROI) measurement, scalar and dense baselines, density/size/SuiteSparse sweeps, and the sparse-aware mechanisms. See `README.md` and `PROGRESS.md`.
