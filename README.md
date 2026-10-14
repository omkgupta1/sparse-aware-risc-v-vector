# Sparse-Aware RISC-V Vector Architecture for High-Performance Computing

This project evaluates and improves the RISC-V Vector Extension (RVV 1.0) on sparse workloads, mainly sparse matrix–vector multiplication (SpMV; SpMM as a second workload), on a cycle-level **gem5** model of an out-of-order RISC-V core.

It contributes:
- a validated measurement and benchmarking framework;
- 18 kernel variants;
- software sparse-aware mechanisms;
- a new gem5 hardware prefetcher for vector gathers, the **Vector-Indirect Prefetcher**;
- four gem5 simulator bugs found and isolated.

## Status

| Phase | Status |
|---|---|
| Midterm baseline (RVV CSR SpMV, two sparsity levels) | ✅ done, results in `results/midterm/` |
| 1. Kernel-only (ROI) measurement | ✅ done |
| 2. Baselines: scalar, RVV CSR variants, dense RVV GEMV | ✅ done |
| 3. Realistic workloads: density/size/skew sweeps, SuiteSparse, SpMM, VLEN | ✅ done |
| 4A. Software mechanisms: mask skipping, compaction, density-aware hybrid | ✅ done |
| 4B. Hardware mechanism: Vector-Indirect Prefetcher (gem5 C++) | ✅ done (254 runs incl. history-depth and MSHR follow-ups) |
| 5. Metrics, analysis, figures | ✅ done |
| 6. Reproducibility and documentation | ✅ done |

Detailed log with every result, failure and open issue: **[PROGRESS.md](PROGRESS.md)**. Summary of all findings: **[PROJECT_SUMMARY.md](PROJECT_SUMMARY.md)**.

## Headline results

All numbers are kernel-only gem5 simulation results (O3 CPU at 1 GHz, VLEN = 256, 32 KiB L1D / 512 KiB L2), validated against a double-precision scalar reference.

| Result | Number | Source |
|---|---|---|
| RVV CSR SpMV vs. scalar CSR (1024², uniform) | 1.41× at 1% density → 5.28× at 60–80% | `results/endterm/phase3/phase3_summary.md` |
| Dense-vs-sparse crossover | ≈ 58% density | Phase 3 / Phase 4A |
| Density-aware hybrid on mixed-density matrices | 1.15–1.60× over the best single format | `results/endterm/phase4a/phase4a_summary.md` |
| Vector-Indirect Prefetcher vs. stride prefetcher | up to 1.43× (`x` in L2), 1.6–1.9× for a two-accumulator kernel, 1.08–1.35× on real circuit / web matrices; L1D misses 55% → 7%; estimated ≈ 2 KiB of state | `results/endterm/phase4b/phase4b_summary.md` |
| Measurement correction | 94–97% of the midterm's measured time was not the kernel | `results/endterm/phase1/phase1_summary.md` |
| gem5 bugs found | 4, with standalone reproducers | `docs/gem5_rvv_issues.md`, `docs/gem5_bug_reports/` |

## Repository layout

```
configs/            gem5 configs: rvv_test.py (midterm), sparse_rvv.py (end-term; CPU, VLEN, caches, prefetchers)
docker/             Dockerfile of the build + simulation environment
docs/               literature review, midterm report, end-term design and bug documents, final report
gem5_patches/       sparse_aware.patch (Vector-Indirect Prefetcher) + pristine copies of the modified gem5 files
results/midterm/    original midterm runs (unchanged)
results/endterm/    phase1 … phase5: per-run gem5 outputs (stats.txt.gz, config.ini.gz, run.log, cmd.txt),
                    per-phase CSV + Markdown summaries, phase5/figures (PDF + PNG) and figure_data (CSV)
scripts/            build/run/report tooling (see "Reproducing"), stats parser, matrix download/converter
src/                common/roi.h (ROI markers), harness/ (benchmark harness, generators), kernels/,
                    tests/ (directed RVV test), spmv_rvv*.c (midterm sources)
workloads/          midterm binaries (unchanged)
Makefile            builds every RISC-V binary into build/ (git-ignored)
```

## Reproducing

```bash
# 1. Environment (Docker; gem5 v25.1.0.1 checked out and built at ~/gem5-assignment/gem5)
docker build -t sparse-rvv-env docker/

# 2. Patch and rebuild gem5 with the Vector-Indirect Prefetcher (only needed for Phase 4B)
scripts/env.sh 'BUILD=1 scripts/apply_gem5_patch.sh /workspace/gem5'

# 3. Build all workloads and run the functional tests (QEMU)
scripts/env.sh 'make -j8 all && make qemu-test'

# 4. SuiteSparse inputs (git-ignored; fetched and converted on demand)
scripts/env.sh scripts/download_matrices.sh

# 5. Everything (several hours), or selected phases
scripts/env.sh scripts/run_all.sh
PHASES="5" scripts/env.sh scripts/run_all.sh        # regenerate metrics, figures, report only
```

`scripts/env.sh` runs a command in the container with the repository at `/project` and the gem5 tree at `/workspace`. Set `GEM5_HOST_ROOT` if gem5 is not under `~/gem5-assignment`. Completed runs are skipped; `FORCE=1` re-runs them.

## Tool versions used

| Tool | Version |
|---|---|
| gem5 | v25.1.0.1 (commit `a28ae2f8b2`), RISCV `gem5.opt` + `gem5_patches/sparse_aware.patch` |
| RISC-V cross-compiler | `riscv64-linux-gnu-gcc` 14.2.0 (Ubuntu 14.2.0-4ubuntu2~24.04.1), static, `-O2 -march=rv64gcv` |
| QEMU (functional checks) | 8.2.2 user mode, `-cpu rv64,v=true,vlen=256,elen=64` |
| Host compiler (gem5 build) | g++ 13.3.0 |
| Container OS | Ubuntu 24.04.4 LTS (Docker on Apple-silicon macOS) |
| Python / matplotlib / numpy | 3.12.3 / 3.6.3 / 1.26.4 |

## Documentation

| Document | Contents |
|---|---|
| [docs/final_report.md](docs/final_report.md) | End-term report: motivation, background, baseline, proposed architecture, methodology, results, crossover, overhead, limitations, future work |
| [docs/phase4b_design.md](docs/phase4b_design.md) | Vector-Indirect Prefetcher design, implementation history, results, storage estimate |
| [docs/gem5_rvv_issues.md](docs/gem5_rvv_issues.md) | The four gem5 bugs and other simulator observations |
| [docs/gem5_bug_reports/](docs/gem5_bug_reports/) | Ready-to-file upstream issue drafts with standalone reproducers |
| [docs/rvv_sparse_architecture_review.md](docs/rvv_sparse_architecture_review.md) | Literature review (midterm) |
| [docs/baseline_performance_report.md](docs/baseline_performance_report.md) | Midterm performance report |
| [RESUME_FACTS.md](RESUME_FACTS.md) | Strongest verified results with exact sources |

## Midterm deliverables (completed)

1. Study RVV and existing sparse-computing architectures.
2. Set up and validate the baseline RISC-V simulation environment.
3. Run baseline RVV execution on SpMV.
4. Evaluate at least two sparsity levels.
5. Report execution time and baseline performance.
