# Sparse-Aware RISC-V Vector Architecture: Full Project Analysis

> Independent analysis of the repository as of commit `a1cf5b8` (2026-10-13).
> Every number below was taken directly from the files in `results/`, or recomputed from them; derived values say how they were derived.
> No existing project file was modified to produce this document.

---

## Table of Contents

1. [Project in One Paragraph](#1-project-in-one-paragraph)
2. [Repository Map](#2-repository-map)
3. [Timeline (Git History)](#3-timeline-git-history)
4. [Deliverables: Status Checklist](#4-deliverables-status-checklist)
5. [Background Concepts (Glossary)](#5-background-concepts-glossary)
6. [Code Walk-Through](#6-code-walk-through)
7. [Simulated Machine (Exact gem5 Configuration)](#7-simulated-machine-exact-gem5-configuration)
8. [Workload Characterisation (Matrix Properties)](#8-workload-characterisation-matrix-properties)
9. [Results: Headline Metrics](#9-results-headline-metrics)
10. [Results: Detailed Metrics (Core, Branch, Cache, DRAM)](#10-results-detailed-metrics-core-branch-cache-dram)
11. [Instruction Mix Breakdown](#11-instruction-mix-breakdown)
12. [Cross-Validation: Stats vs. Source Code](#12-cross-validation-stats-vs-source-code)
13. [Deeper Analysis and Key Insights](#13-deeper-analysis-and-key-insights)
14. [Issues, Gaps, and Inconsistencies Found](#14-issues-gaps-and-inconsistencies-found)
15. [Recommended Next Steps](#15-recommended-next-steps)
16. [How to Reproduce](#16-how-to-reproduce)
17. [References](#17-references)

---

## 1. Project in One Paragraph

The project studies how the **RISC-V Vector Extension (RVV 1.0)** performs on **sparse workloads**. The eventual goal is to propose a *sparse-aware* vector architecture. The current phase is the **midterm baseline**:

- A CSR-format **Sparse Matrix-Vector Multiplication (SpMV)** kernel, `y = A·x`, written with RVV C intrinsics.
- Compiled for `rv64gcv` and run on the **gem5 25.1.0.1** simulator, on an out-of-order RISC-V core with VLEN = 256 bits.
- Run at two sparsity levels: **80%** and **40%**.
- Validated against a scalar reference; both runs passed.
- Supported by a literature review and a performance report.

No sparse-aware hardware change has been designed or simulated yet. This phase only establishes the baseline.

---

## 2. Repository Map

```
Sparse-aware-risc-v-vector/
├── README.md                              Project objective + midterm deliverables (status line is stale, see §14)
├── .gitignore                             Ignores __pycache__/, *.pyc, m5out/
├── configs/
│   └── rvv_test.py                        gem5 stdlib config: O3 RISC-V core + RVV, caches, DDR3, SE mode
├── src/
│   ├── rvv_test.c                         Smoke test: 8-element RVV float vector add
│   └── spmv_rvv.c                         Main workload: synthetic CSR matrix + RVV SpMV + scalar check
├── workloads/
│   ├── rvv_test                           Compiled RISC-V ELF (static, with debug info), 105 KB
│   └── spmv_rvv                           Compiled RISC-V ELF (static, with debug info), 110 KB
├── scripts/
│   └── plot_baseline.py                   Reads baseline_results.csv → 3 bar charts (matplotlib)
├── docs/
│   ├── baseline_plan.md                   Experiment plan (status section is stale)
│   ├── rvv_sparse_architecture_review.md  Literature review: RVV, sparse formats, accelerator types
│   └── baseline_performance_report.md     Midterm performance report
└── results/
    ├── baseline_results.csv               Consolidated table (2 rows: 80% and 40% sparsity)
    ├── figures/                           simulated_time.png, simulated_instructions.png, nonzero_elements.png
    ├── rvv_test/                          gem5 output for the smoke test (stats.txt, config.*, citations.bib)
    ├── spmv_80/                           gem5 output, 80% sparsity (run.log, stats.txt, config.*)
    └── spmv_40/                           gem5 output, 40% sparsity (run.log, stats.txt, config.*)
```

Each `results/<run>/` directory is a standard gem5 output directory:

| File | What it contains |
|---|---|
| `stats.txt` | About 1,700–1,800 statistics: timing, pipeline, branch, cache, DRAM, energy |
| `config.ini` / `config.json` | The full instantiated system: every SimObject parameter |
| `config.dot(.pdf/.svg)` | Diagram of the simulated system |
| `run.log` | Simulator stdout plus the program's own stdout (checksum, validation result) |
| `citations.bib` | gem5 papers to cite |

---

## 3. Timeline (Git History)

All commits were made by **Gopal Tewari** on 2026-10-13 (UTC). The simulations ran on a host named `Govind`, under `/home/tewar/...`.

| Commit | Time (UTC) | Change |
|---|---|---|
| `6161b5c` | 09:19:06 | Initialised the project; README with objective and deliverables |
| `3532224` | 09:19:41 | README completed |
| `4d70df1` | 09:21:03 | Added `src/rvv_test.c` and `docs/baseline_plan.md` |
| (sim runs) | 09:28:06 / 09:29:01 | gem5 runs for 80% and then 40% sparsity (timestamps from `run.log`) |
| `a1cf5b8` | 09:45:21 | Added the SpMV source, gem5 config, binaries, all results, plots, report, and review (35 files, about 28K lines) |

---

## 4. Deliverables: Status Checklist

These are the midterm deliverables from `README.md`:

| # | Deliverable | Status | Evidence |
|---|---|---|---|
| 1 | Study RVV and existing sparse-computing architectures | ✅ Done | `docs/rvv_sparse_architecture_review.md` covers RVV concepts, 6 sparse formats, 5 architecture classes and 7 challenges, with 5 references |
| 2 | Set up and validate the baseline RISC-V simulation environment | ✅ Done (minor gap) | gem5 25.1.0.1 built for RISCV. `rvv_test` simulated: 15,458 instructions, 11 committed vector ops. Its `run.log` was **not** saved, so the expected output `11 22 33 44 55 66 77 88` is not recorded in the repo. |
| 3 | Run baseline RVV execution on SpMV | ✅ Done | `src/spmv_rvv.c`, `workloads/spmv_rvv`, `results/midterm/spmv_*` |
| 4 | Evaluate at least two sparsity levels | ✅ Done | 80% sparsity (20% density) and 40% sparsity (60% density) |
| 5 | Report execution time and baseline performance | ✅ Done | `docs/baseline_performance_report.md`, `results/midterm/baseline_results.csv`, 3 figures |

**Not yet started:** the final-phase work. That means proposing and implementing a sparse-aware hardware modification, adding a scalar-only performance baseline, and evaluating on real matrices.

---

## 5. Background Concepts (Glossary)

| Term | Meaning in this project |
|---|---|
| **RVV** | RISC-V "V" vector extension, v1.0. Vector-length agnostic: software asks for `vl` elements and hardware grants up to its maximum. |
| **VLEN** | Bits per vector register. Here **256**. |
| **ELEN** | Maximum element width supported. Here **64** bits. |
| **SEW** | Selected element width. The kernel uses **32-bit** (`e32`): float32 values and uint32 indices. |
| **LMUL** | Register grouping factor. The kernel uses **m1** (one register), so the maximum `vl` = VLEN/SEW = 256/32 = **8 elements**. |
| **vl** | Active vector length for an instruction, returned by `vsetvli`. |
| **Indexed (gather) load** | `vluxei32`: loads `x[columns[k]]` for 8 arbitrary indices at once. This is the core sparse operation. |
| **Reduction** | `vfredusum`: sums all elements of a vector into one scalar. The "u" means *unordered*: the hardware may add in any order. |
| **CSR** | Compressed Sparse Row format: `values[]`, `columns[]`, `row_ptr[]`. Row *i*'s nonzeros are `values[row_ptr[i] .. row_ptr[i+1]-1]`. |
| **NNZ** | Number of nonzero elements. |
| **Sparsity / Density** | Sparsity = % zeros; density = 100 − sparsity. The program's argument is **density**. |
| **gem5 tick** | 1 ps. `simFreq` = 10¹² ticks/s. At 1 GHz, 1 cycle = 1,000 ticks. |
| **SE mode** | Syscall-Emulation: runs a single user binary with no OS kernel. |
| **O3 CPU** | gem5's detailed out-of-order CPU model. |
| **IPC / CPI** | Instructions per cycle / cycles per instruction. |
| **MPKI** | Misses per 1,000 instructions. |
| **Micro-op (simOps)** | gem5 splits some instructions into micro-ops; vector indexed loads become one micro-op per element. So `simOps` > `simInsts`. |
| **MSHR** | Miss Status Holding Register: tracks outstanding cache misses. |
| **Stride prefetcher** | Detects constant-stride access patterns and fetches lines ahead of time. |

---

## 6. Code Walk-Through

### 6.1 `src/rvv_test.c`: smoke test

- Adds two 8-element float arrays with one vector add.
- Sequence: `vsetvl_e32m1(8)` → 2× `vle32` → `vfadd` → `vse32`, then `printf`.
- Purpose: prove that the toolchain emits RVV and that gem5 executes it.
- The gem5 stats confirm 4 unit-stride loads, 3 unit-stride stores, 3 `vsetvl`s and 1 vector float add were committed. Those counts are micro-ops, so they differ slightly from the instruction count in the source.

### 6.2 `src/spmv_rvv.c`: the main workload

The program runs in four phases. **All four phases are inside the measured gem5 time**, which matters for interpreting the results (§13.1).

**Phase 1: Input vector generation.** O(N).
```c
x[j] = (float)(j % 17 + 1) / 17.0f;          // N = 256 values in (0, 1]
```

**Phase 2: Synthetic CSR matrix generation.** O(N²) = 65,536 iterations, *independent of density*.
```c
pattern = (i*17 + j*13 + i*j) % 100;
if (pattern < density) {                     // data-dependent branch
    values[nnz]  = (float)((i + j) % 11 + 1) / 11.0f;   // 1 FP divide per nonzero
    columns[nnz] = j;
    nnz++;
}
row_ptr[i+1] = nnz;
```
- Deterministic, so there is no randomness and runs are exactly reproducible.
- Column indices are **sorted ascending** within each row.
- Arrays are statically sized to the worst case: `MAX_NNZ = 65,536` floats + 65,536 uint32 = 512 KiB of BSS.

**Phase 3: RVV CSR SpMV kernel.** This is the part the project is about.
```c
for each row i:
    sum = 0
    for k in [row_ptr[i], row_ptr[i+1]) step vl:
        vl      = vsetvl_e32m1(end - k)              // up to 8
        va      = vle32(&values[k])                  // unit-stride load of A's values
        vi      = vle32(&columns[k])                 // unit-stride load of column indices
        offsets = vmul_vx(vi, 4)                     // index → byte offset
        vx      = vluxei32(x, offsets)               // GATHER x[columns[k..k+vl]]
        product = vfmul(va, vx)
        initial = vfmv_v_f(0.0, 1)
        reduced = vfredusum(product, initial)        // horizontal sum of ≤8 products
        sum    += vfmv_f_s(reduced)                  // move to scalar register
    y[i] = sum
```
Each chunk of up to 8 nonzeros runs about 9 vector instructions. The design choice that matters most is the **horizontal reduction in every chunk**. The cheaper standard pattern accumulates with `vfmacc` into a vector register and reduces once per row (§15).

**Phase 4: Scalar reference and validation.**
- Recomputes `y` with a plain scalar loop.
- Tracks `max_error = max |y_rvv − y_ref|` and a `checksum = Σ y[i]`.
- Fails, with exit code 2, if `max_error > 1e-4`.
- Because `vfredusum` adds in a different order than the scalar loop, small float32 rounding differences are expected. They are not bugs.

### 6.3 `configs/rvv_test.py`: gem5 system

This file uses the gem5 standard library.

- Defines `RVVCore(BaseCPUCore)`, wrapping `RiscvO3CPU` and setting `isa.vlen` / `isa.elen` from the command line.
- Cache hierarchy: `PrivateL1PrivateL2CacheHierarchy`, with 32 KiB L1I, 32 KiB L1D and 512 KiB L2.
- Memory: `SingleChannelDDR3_1600`.
- Board: `SimpleBoard` at 1 GHz, running the binary in SE mode with `--program-args`.
- CLI: `--binary` (required), `--vlen` (default 256), `--elen` (default 64), `--program-args` (passed to the binary).

### 6.4 `scripts/plot_baseline.py`

- Reads `results/midterm/baseline_results.csv` and sorts by sparsity, so 40% is plotted first.
- Writes 3 bar charts at 220 dpi into `results/midterm/figures/`: simulated time (µs), simulated instructions, and NNZ.

---

## 7. Simulated Machine (Exact gem5 Configuration)

Extracted from `results/midterm/spmv_80/config.ini`. The configuration is identical for all three runs.

### 7.1 Core

| Parameter | Value |
|---|---|
| CPU model | `BaseO3CPU` (RiscvO3CPU), out-of-order |
| ISA | RV64, RVV enabled, privilege modes MSU |
| VLEN / ELEN | 256 bits / 64 bits |
| Clock | 1 GHz (clock period = 1000 ticks) |
| Fetch / decode / rename / dispatch / issue / writeback / commit width | 8 / 8 / 8 / 8 / 8 / 8 / 8 |
| ROB entries | 192 |
| Load queue / store queue | 32 / 32 |
| Physical int / float / vector registers | 256 / 256 / 256 |
| Fetch buffer | 64 bytes |
| Branch predictor | `TournamentBP` (conditional) + `SimpleBTB` + RAS + indirect predictor |

### 7.2 Memory Hierarchy

| Level | Size | Assoc | Latency (tag/data/resp, cycles) | MSHRs | Prefetcher |
|---|---|---|---|---|---|
| L1I | 32 KiB | 8-way | 1 / 1 / 1 | 16 | Stride (degree 4) |
| L1D | 32 KiB | 8-way | 1 / 1 / 1 | 16 | Stride (degree 4) |
| L2 (private) | 512 KiB | 16-way | 10 / 10 / 1 | 20 | Stride (degree 4) |
| DRAM | DDR3-1600, 1 channel, 2 ranks × 8 banks | – | tCL = tRCD = tRP = 13.75 ns | – | – |

- DRAM peak bandwidth: **12.8 GB/s**.
- Page policy: open-adaptive.
- Caches are "mostly inclusive".

---

## 8. Workload Characterisation (Matrix Properties)

I recomputed these by re-implementing the generator's formula exactly. The NNZ values match `run.log` exactly, which confirms the reimplementation.

| Property | 80% sparsity run | 40% sparsity run |
|---|---:|---:|
| Program argument (density %) | 20 | 60 |
| Matrix size | 256 × 256 (65,536 positions) | 256 × 256 |
| **NNZ** | **12,915** | **39,181** |
| Actual density | 19.71% | 59.79% |
| Nonzeros per row: min / mean / max | 0 / 50.45 / 64 | 0 / 153.05 / 192 |
| Row-length std. deviation | 7.49 | 15.08 |
| Row-length coefficient of variation | 0.148 (very uniform) | 0.099 (very uniform) |
| Empty rows | 5 | 2 |
| Vector chunks (VL ≤ 8) | **1,756** | **5,063** |
| Full 8-wide chunks / partial tail chunks | 1,510 / 246 | 4,836 / 227 |
| Average active vl per chunk | 7.35 | 7.74 |
| **Vector lane utilisation** | **91.9%** | **96.7%** |
| Useful FLOPs (1 mul + 1 add per NNZ) | 25,830 | 78,362 |
| CSR footprint (values + cols + row_ptr + x + y) | 103.9 KiB | 309.1 KiB |
| Equivalent dense matrix footprint | 256 KiB | 256 KiB |
| Footprint vs. L1D (32 KiB) / L2 (512 KiB) | > L1D, < L2 | > L1D, < L2 |
| Size of `x` (the gathered vector) | 1 KiB, always L1-resident | 1 KiB, always L1-resident |
| Program checksum (Σ y) | 3674.374445438 | 11193.069869995 |

Observations:

- At 60% density, CSR (309 KiB) is **larger than the dense matrix** (256 KiB). Each nonzero costs 8 bytes (value + index) instead of 4, so CSR stops saving memory above 50% density. The "40% sparsity" case is effectively a dense workload stored inefficiently.
- Rows are **very uniform**, with a coefficient of variation of about 0.1–0.15. Real sparse matrices from SuiteSparse, graphs and FEM often have heavy-tailed row lengths, so this workload barely exercises load imbalance.
- `x` is only 1 KiB and stays in L1, so the gather `vluxei32` always hits in cache. The irregular-memory-access problem that motivates sparse-aware hardware is **not stressed** by this workload.

---

## 9. Results: Headline Metrics

Source: `results/midterm/baseline_results.csv`, `run.log` and `stats.txt`.

| Metric | Smoke test (`rvv_test`) | **SpMV 80% sparse** | **SpMV 40% sparse** | 40% ÷ 80% |
|---|---:|---:|---:|---:|
| NNZ | – | 12,915 | 39,181 | **3.03×** |
| simTicks | 19,470,000 | 569,393,000 | 744,567,000 | 1.31× |
| **Simulated time** | 19.47 µs | **569.393 µs** | **744.567 µs** | **+30.8%** |
| Simulated cycles | 19,471 | 569,394 | 744,568 | 1.31× |
| **Committed instructions** (simInsts) | 15,458 | **1,409,447** | **2,073,307** | **+47.1%** |
| Committed micro-ops (simOps) | 15,458 | 1,427,630 | 2,127,677 | +49.0% |
| **IPC** | 0.794 | **2.475** | **2.785** | +12.5% |
| **CPI** | 1.260 | **0.404** | **0.359** | −11.1% |
| Committed vector instructions | 11 | 33,250 | 95,899 | 2.88× |
| Vector share of instructions | 0.07% | **2.36%** | **4.63%** | – |
| Max abs. error vs. scalar | – | 4.77 × 10⁻⁶ | 1.91 × 10⁻⁵ | 4× |
| **Validation** | – | **PASSED** | **PASSED** | – |
| Host wall time (gem5 itself) | 0.10 s | 4.14 s | 2.86 s | (host noise) |

### Derived efficiency metrics

These are end-to-end values that include generation and validation (§13.1).

| Metric | 80% sparse | 40% sparse |
|---|---:|---:|
| Cycles per nonzero | 44.1 | 19.0 |
| Instructions per nonzero | 109.1 | 52.9 |
| Effective MFLOP/s (useful FLOPs ÷ total time) | 45.4 | 105.2 |
| L1D MPKI | 19.96 | 13.90 |
| L2 MPKI | 5.64 | 3.87 |
| Branch mispredicts per 1K instructions | 4.24 | 2.77 |
| DRAM energy (rank0 + rank1) | 628.3 µJ | 800.5 µJ |

For scale: the theoretical vector peak is 8 lanes × 1 GHz ≈ 8 GFLOP/s for multiplies alone. The end-to-end numbers are far below that, mostly because most of the run is not SpMV (§13.1).

### Figures (`results/midterm/figures/`)

1. `simulated_time.png`: 744.567 µs (40%) vs. 569.393 µs (80%)
2. `simulated_instructions.png`: 2,073,307 vs. 1,409,447
3. `nonzero_elements.png`: 39,181 vs. 12,915

---

## 10. Results: Detailed Metrics (Core, Branch, Cache, DRAM)

All values come from `stats.txt`.

### 10.1 Pipeline / Core

| Stat | rvv_test | SpMV 80% | SpMV 40% |
|---|---:|---:|---:|
| numCycles | 19,471 | 569,394 | 744,568 |
| idleCycles | 7,129 | 10,988 | 10,738 |
| Fetched instructions | 28,900 | 1,687,803 | 2,377,790 |
| Fetch rate (insts/cycle) | 1.48 | 2.96 | 3.19 |
| I-cache stall cycles | 4,390 | 8,629 | 9,822 |
| Decoded instructions | 25,383 | 1,672,790 | 2,400,314 |
| Renamed instructions | 24,163 | 1,644,914 | 2,378,848 |
| Issued instructions | 20,085 | 1,563,428 | 2,264,064 |
| Issue rate (per cycle) | 1.03 | 2.75 | 3.04 |
| Committed-squashed instructions | 6,398 | 183,423 | 198,962 |
| Rename: **LQ full events** | 299 | **33,319** | **120,593** (3.6×) |
| Rename: SQ full events | 223 | 45,102 | 45,200 |
| Rename: IQ full events | 0 | 408 | 565 |
| Rename: ROB full events | 0 | 1 | 6 |
| Rename: vector register lookups | 11 | 36,856 | 91,432 |
| IEW: LSQ full events | 194 | 6,628 | 6,051 |
| Memory-order violations | 22 | 34 | 26 |
| Avg load-queue occupancy | 0.06 | 0.11 | 0.27 |
| Avg store-queue occupancy | 0.09 | 0.13 | 0.17 |
| FU busy events / rate | 648 / 3.2% | 12,046 / 0.77% | 18,656 / 0.82% |
| Vector ALU accesses | 15 | 36,875 | 101,417 |
| FP ALU accesses | 320 | 85,442 | 250,867 |
| Int ALU accesses | 20,305 | 1,453,126 | 1,930,405 |
| Vector register reads / writes | 5 / 5 | 27,751 / 43,416 | 80,458 / 125,633 |
| Committed loads / stores | 2,417 / 2,417 | 58,773 / 94,386 | 170,486 / 146,940 |
| Function calls | 354 | 309 | 313 |
| Syscalls | 10 | 12 | 12 |

### 10.2 Branch Prediction

| Stat | rvv_test | SpMV 80% | SpMV 40% |
|---|---:|---:|---:|
| Predictor lookups | – | 184,650 | 215,749 |
| Conditional branches predicted | 4,156 | 179,109 | 206,278 |
| Conditional mispredicted | 427 | 5,934 | 5,765 |
| Mispredicts due to direction predictor (DirectCond) | – | 5,026 | 4,889 |
| Mispredicts due to BTB miss | – | 908 | 876 |
| BTB hit ratio | 42.7% | 96.2% | 94.7% |
| Commit-time branch mispredicts | 363 | 5,972 | 5,742 |
| RAS incorrect | 10 | 10 | 10 |

### 10.3 Caches

| Stat | rvv_test | SpMV 80% | SpMV 40% |
|---|---:|---:|---:|
| **L1D** accesses | 5,202 | 156,806 | 324,280 |
| L1D misses | 338 | 28,137 | 28,813 |
| **L1D miss rate** | 6.50% | **17.94%** | **8.89%** |
| L1D read miss rate | 3.23% | **0.75%** | **0.51%** |
| L1D write miss rate | 10.26% | **29.31%** | **19.00%** |
| L1D avg miss latency | 63.0 cyc | 38.0 cyc | 37.8 cyc |
| L1D writebacks | 0 | 12,841 | 22,769 |
| L1D prefetches issued / useful | 59 / 8 | 49,784 / 9,515 | 87,656 / 19,111 |
| L1D prefetch accuracy / coverage | 13.6% / 10.8% | 19.1% / 92.8% | 21.8% / 95.3% |
| L1D late prefetches | 25 | 37,146 | 65,244 |
| **L1I** miss rate | 8.58% | 0.19% | 0.18% |
| **L2** accesses | 349 | 13,711 | 23,687 |
| L2 misses | 347 | 7,952 | 8,020 |
| **L2 miss rate** | 99.4% (cold) | **58.00%** | **33.86%** |
| L2 avg miss latency | 69.2 cyc | 84.0 cyc | 84.1 cyc |
| L2 prefetches issued / useful | 43 / 2 | 14,623 / 939 | 15,508 / 1,150 |
| L2 prefetch accuracy / coverage | 4.7% / 0.6% | 6.4% / 37.6% | 7.4% / 41.5% |

### 10.4 DRAM

| Stat | rvv_test | SpMV 80% | SpMV 40% |
|---|---:|---:|---:|
| Read requests | 373 | 8,906 | 9,178 |
| Write requests | 0 | 704 | 976 |
| Bytes read | 23,872 | 569,984 | 587,392 |
| Bytes written | 0 | 45,056 | 62,464 |
| Avg read bandwidth | 1.23 GB/s | 1.00 GB/s | 0.79 GB/s |
| Bus utilisation | 9.58% | 8.41% | 6.80% |
| Row-buffer (page) hit rate | 91.7% | 92.7% | 92.7% |
| Avg memory access latency | 30.3 ns | 85.8 ns | 84.6 ns |
| Avg queueing latency | 11.6 ns | 67.0 ns | 65.8 ns |
| Avg read / write queue length | 1.74 / 0 | 2.96 / 21.23 | 2.69 / 24.33 |
| Total DRAM energy (2 ranks) | 21.7 µJ | 628.3 µJ | 800.5 µJ |

---

## 11. Instruction Mix Breakdown

These are committed micro-ops by gem5 op class, taken from `commitStats0.committedInstType`. Only nonzero classes are shown.

| Op class | SpMV 80% | % | SpMV 40% | % | Where it comes from (see §12) |
|---|---:|---:|---:|---:|---|
| IntAlu | 1,070,408 | 74.98 | 1,468,043 | 69.00 | Loop control and matrix-generation arithmetic |
| IntMult | 144,170 | 10.10 | 170,438 | 8.01 | About 2 per matrix *position* (65,536) + 1 per NNZ |
| IntDiv | 24 | 0.00 | 25 | 0.00 | libc |
| FloatAdd | 2,270 | 0.16 | 5,577 | 0.26 | |
| FloatCmp | 261 | 0.02 | 261 | 0.01 | |
| FloatCvt | 13,953 | 0.98 | 40,222 | 1.89 | int→float in generation (about 1 per NNZ) |
| FloatMultAcc | 12,919 | 0.90 | 39,185 | 1.84 | Scalar reference `fmadd` (1 per NNZ) |
| **FloatDiv** | **12,915** | 0.90 | **39,181** | 1.84 | **Exactly NNZ**: the `/ 11.0f` in generation |
| FloatMisc | 773 | 0.05 | 772 | 0.04 | |
| SimdAdd | 128 | 0.01 | 128 | 0.01 | Auto-vectorised `x[]` init |
| SimdMisc | 9,035 | 0.63 | 25,573 | 1.20 | Moves and splats (≈ 5 × chunks + rows) |
| **SimdMult** | 1,820 | 0.13 | 5,127 | 0.24 | `vmul.vx` offsets = chunks + 64 |
| SimdMultAcc / Shift / FloatCvt / FloatDiv | 64 each | 0.00 | 64 each | 0.00 | Auto-vectorised `x[]` init loop |
| **SimdFloatMult** | **1,756** | 0.12 | **5,063** | 0.24 | `vfmul`: exactly 1 per chunk |
| **SimdFloatReduceAdd** | **1,756** | 0.12 | **5,063** | 0.24 | `vfredusum`: exactly 1 per chunk |
| MemRead | 16,244 | 1.14 | 42,545 | 2.00 | |
| MemWrite | 81,149 | 5.68 | 107,437 | 5.05 | |
| FloatMemRead | 26,102 | 1.83 | 78,634 | 3.70 | Scalar reference reads (≈ 2 per NNZ) |
| FloatMemWrite | 13,173 | 0.92 | 39,439 | 1.85 | `values[nnz] = …` (≈ 1 per NNZ) |
| **SimdUnitStrideLoad** | **3,512** | 0.25 | **10,126** | 0.48 | 2 `vle32` per chunk (values + columns) |
| SimdUnitStrideStore | 64 | 0.00 | 64 | 0.00 | Auto-vectorised `x[]` init |
| **SimdIndexedLoad** | **12,915** | 0.90 | **39,181** | 1.84 | **Exactly NNZ**: gem5 splits each gather into one micro-op per element |
| SimdConfig | 2,008 | 0.14 | 5,318 | 0.25 | `vsetvli`: chunks + non-empty rows + 1 |
| **Total ops** | **1,427,630** | | **2,127,677** | | |

The workload is **about 75–85% scalar integer work**. All SIMD classes together make up only about 2.4% (80%) and about 4.6% (40%) of committed micro-ops.

---

## 12. Cross-Validation: Stats vs. Source Code

The gem5 counts were checked against values computed independently from the source code's matrix formula:

| Prediction from source code | Predicted | gem5 (80%) | Predicted | gem5 (40%) | Match |
|---|---:|---:|---:|---:|:---:|
| NNZ | 12,915 | 12,915 (run.log) | 39,181 | 39,181 (run.log) | ✅ exact |
| Vector chunks = Σ ceil(row_len / 8) → `vfmul` count | 1,756 | 1,756 | 5,063 | 5,063 | ✅ exact |
| Same → `vfredusum` count | 1,756 | 1,756 | 5,063 | 5,063 | ✅ exact |
| 2 `vle32` per chunk | 3,512 | 3,512 | 10,126 | 10,126 | ✅ exact |
| Gather micro-ops = NNZ | 12,915 | 12,915 | 39,181 | 39,181 | ✅ exact |
| 1 FP divide per nonzero (generation) | 12,915 | 12,915 | 39,181 | 39,181 | ✅ exact |
| `vmul.vx` per chunk + 64 init | 1,820 | 1,820 | 5,127 | 5,127 | ✅ exact |
| `vsetvli` = chunks + non-empty rows + 1 | 2,008 | 2,008 | 5,318 | 5,318 | ✅ exact |
| simTicks / 1000 = cycles | 569,393 | 569,394 | 744,567 | 744,568 | ✅ (±1 cycle) |

**Conclusion:** the simulation ran exactly the intended code, the vector kernel was really executed with VL = 8, and the reported numbers are internally consistent.

---

## 13. Deeper Analysis and Key Insights

### 13.1 Most of the measured time is *not* SpMV ⚠️

`simTicks` covers the whole program: startup, `x` generation, the **O(N²) matrix-generation loop over all 65,536 positions**, the RVV kernel, the scalar reference and `printf`. Two facts show that generation dominates:

- About 131K `IntMult` operations occur regardless of density, which is about 2 × 65,536. Generation costs about the same at any density.
- FloatDiv equals NNZ exactly, so generation also does work per nonzero.

**Two-point linear model** (time = fixed + per-NNZ × NNZ):

| Quantity | Per-NNZ cost | Fixed (density-independent) cost | Fixed share at 80% | Fixed share at 40% |
|---|---:|---:|---:|---:|
| Cycles | **6.67 cycles / NNZ** | **≈ 483,260 cycles (≈ 483 µs)** | **≈ 84.9%** | ≈ 64.9% |
| Instructions | 25.3 insts / NNZ | ≈ 1,083,000 insts | ≈ 76.8% | ≈ 52.2% |

The model uses only two points, so treat these as estimates. Two interpretations follow:

- About 85% of the 80%-sparse run's time is fixed overhead, mostly matrix generation at about 7.4 cycles per matrix position. That is why **3.03× more nonzeros produced only 1.31× more time**. The sublinear scaling is mainly a measurement artefact, not an RVV property.
- The **marginal cost of about 6.7 cycles per extra nonzero** includes generation work (divide, convert, stores), the scalar reference (`fmadd` + 2 loads) *and* the RVV kernel. The RVV-only time per nonzero is lower still and cannot be isolated without region-of-interest (ROI) markers.

The existing report already notes this caveat in its Limitations section. This analysis quantifies it.

### 13.2 IPC is high because the code is mostly scalar integer

An IPC of 2.5–2.8 on an 8-wide O3 core comes from the generation loop's independent integer arithmetic, not from the vector kernel. IPC rises at 40% because a larger share of the run is the denser kernel and reference loops, which have long, predictable loops.

### 13.3 The gather is split into per-element micro-ops: a real architectural pressure point

- `SimdIndexedLoad` = NNZ exactly, so gem5's O3 model cracks every `vluxei32` into **vl separate load micro-ops**.
- Each micro-op occupies a load-queue entry, and there are only 32 LQ entries.
- Evidence: **rename LQ-full stall events rose 3.6×**, from 33,319 to 120,593, when NNZ tripled, and average LQ occupancy rose from 0.11 to 0.27.
- This is the clearest sparse-specific bottleneck in the data. **Gathers cost per element, not per instruction.** A sparse-aware design could target this with a gather/coalescing unit, a larger or vector-aware LQ, or index-compressed loads.

### 13.4 Per-chunk reduction is costly

- Every 8-element chunk does a `vfredusum` followed by `vfmv.f.s` and a scalar add.
- Reductions are long-latency and serialise through the scalar `sum`.
- Row lengths average 50 (80%) or 153 (40%), so a row needs about 7 or about 20 reductions where 1 would do.

### 13.5 Caches: write misses dominate and reads are nearly free

- L1D **read** miss rate is under 1% in both runs. **Write** miss rate is 19–29%. Writes are the streaming stores that build `values[]` and `columns[]` during generation.
- The total footprint fits in L2 (512 KiB), and `x` (1 KiB) never leaves L1. So the SpMV kernel's reads, including the gather, mostly hit.
- L1D and L2 miss *counts* barely change between runs: +2.4% and +0.9%, even though data size tripled. DRAM read bytes are nearly constant, 557 KiB vs. 574 KiB.
- The L2 stride prefetcher is aggressive but inaccurate: 6–7% accuracy, about 15K prefetches issued. It is likely fetching lines beyond the touched part of the 512 KiB static arrays.
- **Conclusion:** this workload is **not memory-bound** in the configuration simulated. The memory-irregularity problem that motivates the project does not appear yet.

### 13.6 Branch behaviour

- About 5.7–6.0K mispredictions occur in both runs, mostly DirectCond mispredictions from the direction predictor (about 5K).
- The likely main source is the data-dependent `if (pattern < density)` in generation, plus row-loop exits. The count is almost density-independent, which fits a fixed O(N²) generation loop.

### 13.7 Numerical accuracy

- Error grows with row length: 4.77e-6 at about 50 nonzeros per row, and 1.91e-5 at about 153 per row. That is expected for float32 summation in a different order (`vfredusum` is unordered).
- Both errors are far below the 1e-4 tolerance. For much longer rows, an absolute tolerance may become too tight; a relative tolerance would be more robust.

### 13.8 Vector utilisation is good, *for this matrix*

- Lane utilisation is 91.9% and 96.7%, because rows are long (50–153) and uniform.
- On real matrices with short rows (e.g. 3–10 nonzeros per row), utilisation would collapse. A row of 3 uses 3 of 8 lanes. This is the classic CSR-vector inefficiency that sparse-aware designs address.

---

## 14. Issues, Gaps, and Inconsistencies Found

| # | Type | Issue | Impact |
|---|---|---|---|
| 1 | Methodology | No ROI markers (`m5_work_begin/end` or `m5_reset_stats/dump_stats`) around the SpMV kernel. Timing includes generation, validation and I/O. | Reported time and instruction counts mostly measure matrix generation (≈ 85% at 80% sparsity) |
| 2 | Methodology | No scalar-only SpMV baseline in the same setup | Cannot quantify the RVV speedup |
| 3 | Workload | The "40% sparsity" case (60% dense) is barely sparse. CSR is bigger than dense there. | Weak test of sparse behaviour |
| 4 | Workload | Synthetic, uniform row lengths (CV ≈ 0.1–0.15). Tiny `x` (1 KiB) always in L1. | Doesn't stress gather misses or load imbalance, the project's core problems |
| 5 | Workload | Only 2 data points | No trend curve. The linear model above is rough. |
| 6 | Kernel | Reduction per chunk instead of per row. LMUL fixed at m1. `vmul` by 4 instead of a shift. | Baseline is weaker than a well-tuned RVV CSR kernel |
| 7 | Docs | `README.md` still says *"RVV execution validation pending"*. `docs/baseline_plan.md` still lists everything as *pending*. | Stale status |
| 8 | Docs | README lists a `plots/` folder, but figures are in `results/midterm/figures/`. It also says `scripts/` holds build scripts, but there is no build script. | Minor inconsistency |
| 9 | Reproducibility | The exact compiler binary name/version and build command are not stored in a script. The flags appear only in the report. | Harder to rebuild |
| 10 | Reproducibility | `results/midterm/rvv_test/run.log` is missing, so the smoke-test output is not recorded | Deliverable 2's evidence is incomplete |
| 11 | Reproducibility | `run.log`s contain absolute paths from another machine (`/home/tewar/...`) | Cosmetic |
| 12 | Repo hygiene | Compiled binaries and large gem5 `config.*` files are committed (about 28K lines) | Repo bloat (acceptable for a course project) |

---

## 15. Recommended Next Steps

Ordered from quick wins to the final-phase research contribution.

**A. Fix the measurement (highest priority).**
1. Wrap only the SpMV kernel in gem5 ROI markers: `m5_reset_stats(0,0)` before it and `m5_dump_stats(0,0)` after, linking against `libm5`. Alternatively, use `m5_work_begin/end` with the stdlib's exit-event handling.
2. Add a **scalar CSR SpMV** compiled with `-march=rv64gc` (no V) and run in the same config. This gives a real RVV speedup number.
3. Sweep more densities, e.g. 1%, 2%, 5%, 10%, 20%, 40%, and sweep matrix sizes so `x` exceeds L1 and the CSR exceeds L2.

**B. Strengthen the RVV baseline kernel.**
1. Accumulate per row with `vfmacc.vv` into a vector register and do **one `vfredusum` per row**.
2. Try LMUL = m2/m4/m8 (VL = 16/32/64) and VLEN = 128/512/1024 sweeps (`--vlen`).
3. Use `vsll.vi` by 2 instead of `vmul.vx` by 4 (minor).

**C. Use realistic matrices.**
1. Load SuiteSparse matrices (Matrix Market `.mtx`) with skewed row lengths, e.g. web/graph matrices, FEM and circuit matrices.
2. Report row-length distribution, lane utilisation, gather miss rates and load imbalance per matrix.

**D. The sparse-aware architecture (the final deliverable).** Candidate ideas grounded in the data above:

| Idea | Motivated by |
|---|---|
| Gather coalescing: merge indexed micro-ops that hit the same cache line | §13.3: per-element gather micro-ops and 3.6× more LQ-full stalls |
| Larger or vector-aware load queue for indexed accesses | §13.3 |
| Segmented / row-aware reduction that reduces several short rows in one vector | §13.4, §13.8 |
| Index-compressed formats (e.g. delta-encoded column indices) to cut index bandwidth | CSR is 2× the value bytes (§8) |
| Sparse-format-aware prefetcher (follow `columns[]` to prefetch `x`) | §13.5: stride prefetcher accuracy is only 6–22% |

Each needs a gem5 modification, e.g. in `src/arch/riscv/insts/vector.*` or the O3 LSQ, plus an A/B comparison against the fixed baseline from step A.

---

## 16. How to Reproduce

```bash
# 1. Build the workloads (any RISC-V GCC ≥ 13 with RVV intrinsics v1.0)
riscv64-unknown-linux-gnu-gcc -O2 -static -march=rv64gcv -mabi=lp64d \
    src/rvv_test.c -o workloads/rvv_test
riscv64-unknown-linux-gnu-gcc -O2 -static -march=rv64gcv -mabi=lp64d \
    src/spmv_rvv.c -o workloads/spmv_rvv -lm

# 2. Run in gem5 (RISCV build). The argument is DENSITY %, not sparsity.
GEM5=/path/to/gem5/build/RISCV/gem5.opt
$GEM5 -d results/midterm/rvv_test configs/rvv_test.py --binary workloads/rvv_test
$GEM5 -d results/midterm/spmv_80 configs/rvv_test.py --binary workloads/spmv_rvv \
      --vlen 256 --elen 64 --program-args 20      # 80% sparsity
$GEM5 -d results/midterm/spmv_40 configs/rvv_test.py --binary workloads/spmv_rvv \
      --vlen 256 --elen 64 --program-args 60      # 40% sparsity

# 3. Pull key stats
grep -E '^(simTicks|simInsts|simOps)\b' results/midterm/spmv_*/stats.txt
grep -E 'core\.(ipc|cpi) ' results/midterm/spmv_*/stats.txt

# 4. Regenerate plots (needs matplotlib)
python3 scripts/plot_baseline.py
```

The gem5 command lines above match those recorded in `results/midterm/spmv_*/run.log`. The compiler binary name is an assumption, because the repo does not record which cross-compiler was used.

---

## 17. References

From `docs/rvv_sparse_architecture_review.md`:

1. RISC-V International, *The RISC-V Instruction Set Manual: "V" Standard Extension for Vector Operations, v1.0*.
2. RISC-V Vector C Intrinsics Task Group, *RISC-V Vector C Intrinsic Specification, v1.0*.
3. Isaac-Chassande, Evans, Durand, Rousseau, *Dedicated Hardware Accelerators for Processing of Sparse Matrices and Vectors: A Survey*, ACM TACO, 2024.
4. Gao, Liu, Ji, Huang, *A Systematic Literature Survey of Sparse Matrix-Vector Multiplication*, arXiv:2404.06047, 2024.
5. Chen et al., *An Efficient SIMD Compression Format for Sparse Matrix-Vector Multiplication*, CCPE, 2018.

gem5 citations: `results/*/citations.bib`, including Binkert et al. 2011 (*The gem5 simulator*) and Lowe-Power et al. 2020 (*The gem5 simulator: Version 20.0+*).
