# gem5 25.1 RVV Correctness Issues Found in This Project

Two functional bugs in gem5 25.1.0.1's **O3 CPU** model were found while validating the Phase 2 kernels. Both produce wrong *architectural results*, not just wrong timing. Both are timing-dependent and only appear at LMUL ≥ 2.

All the evidence can be regenerated with:

```bash
scripts/env.sh scripts/run_gem5_bug_checks.sh
```

The reference for correct behaviour is QEMU 8.2.2 user-mode (`-cpu rv64,v=true,vlen=256,elen=64`) and, for issue 2, gem5's own in-order MinorCPU.

## Issue 1: `vfredusum.vs` at LMUL ≥ 2 reads stale upper registers when `vd` overlaps `vs2`

**Reproducer:** `src/tests/rvv_lmul_check.c` (inline assembly, fixed registers). Outputs are in `results/endterm/phase2/gem5_rvv_check/` (`qemu_reference.txt`, `gem5_output.txt`, `diff.txt`).

Sequence (m2 shown; m4 is analogous):

```asm
vsetvli  t1, zero, e32, m2, ta, ma
vmv.s.x  v1, zero
vle32.v  v2, (ones)          # v2,v3 = 1.0
vle32.v  v6, (a)
vle32.v  v4, (b)
li       t0, 5
vsetvli  t1, t0, e32, m2, tu, ma
vfmacc.vv v2, v6, v4         # elements 0..4 updated, 5..15 undisturbed
vsetvli  t1, zero, e32, m2, ta, ma
vfredusum.vs v2, v2, v1      # vd == vs2 group start (legal; what GCC emits)
vfmv.f.s ft0, v2
```

| Test | Variation | QEMU | gem5 O3 |
|---|---|---:|---:|
| T3 | full-length write, then in-place reduction | 136 | 136 ✅ |
| T5 | tail-undisturbed `vfmacc`, then **store** the group | correct | correct ✅ |
| T6 | tail-undisturbed `vfmacc` (vl=5), then in-place `vfredusum` | 63.5 / 79.5 | **55.5 / 63.5** ❌ |
| T7 | as T6, but `vfredusum` writes `v31` (outside the group) | 63.5 / 79.5 | 63.5 / 79.5 ✅ |
| T8 | as T6, with a full-group store in between | 63.5 / 79.5 | **55.5 / 71.5** ❌ |
| T9 | producer is `vfadd.vv` (tu) | 38.5 / 54.5 | **30.5 / 38.5** ❌ |
| T10 | producer is `vle32.v` (tu) | 26 / 42 | **18 / 26** ❌ |
| T11 | group copied with `vmvNr.v`, then reduced in place | 63.5 / 79.5 | **55.5 / 63.5** ❌ |
| T12 | group folded to LMUL=1 with `vfadd.vv`, reduced at LMUL=1 | 63.5 / 79.5 | 63.5 / 79.5 ✅ |

(Values are m2 / m4.)

**Observations**
- The missing amount is exactly the contribution of whole upper registers of the `vs2` group: 8 ones per register.
- Storing the same group (T5) gives correct values, so the architectural register contents are right. Only the reduction's reads are wrong.
- Moving `vd` outside the group (T7) avoids the bug.
- A store in between changes *how many* registers are lost (T8), so the outcome depends on timing. This points to a dependency or renaming problem for the reduction micro-ops when `vd` aliases `vs2[0]`, not an arithmetic error.
- In kernels this showed up as validation failures in `rvv_opt_m2`, `rvv_opt_m4` and `dense_m2`. `dense_m4` passed by chance.

**Workaround used** (`src/kernels/rvv_reduce.h`): fold the LMUL>1 accumulator to one register with LMUL=1 `vfadd.vv` (`vget` is free), then reduce at LMUL=1. This is the T12 pattern, and it is also the usual faster reduction sequence. After the workaround, every m1/m2 kernel and every dense kernel validates at every density tested.

## Issue 2: LMUL=4 indexed gather (`vluxei32.v`) gives intermittently wrong results on O3

**Evidence:** `results/endterm/phase2/gem5_m4_investigation/summary.md`. The kernel is `spmv_rvv_opt_m4`: `vluxei32.v` into a 4-register group, then `vfmacc.vv` (tu).

| CPU model | d=20% | d=60% | d=100% |
|---|---|---|---|
| MinorCPU (in-order) | PASSED | PASSED | PASSED |
| O3, default LQ/SQ = 32 | FAILED (1 row) | PASSED | PASSED |
| O3, LQ/SQ = 128 | FAILED (4 rows) | FAILED (2 rows) | FAILED (1 row) |

(Counts are from the current aligned build. An earlier unaligned build of the same source failed in 5 / 39 / 72 rows on O3. Failure is timing- and layout-dependent.)

- QEMU passes at 1/20/60/100%.
- Wrong rows are off by roughly one product term. Rows that fail have at least one full 32-element chunk. d=100% has no tail chunks, so tails are not required.
- LMUL=1 and LMUL=2 gathers have never failed (5 densities, several builds), and neither has the LMUL=4 unit-stride load (`dense_m4`).
- gem5 implements indexed loads (`VlIndex` template in `src/arch/riscv/isa/templates/vector_mem.isa`) as `vl` per-element load micro-ops that write a **pinned** destination register (`VPinVdMicroInst`, `setNumPinnedWrites`, `src/cpu/o3/rename_map.cc`). With 32 elements across 4 registers, this pinned-write path is the prime suspect. `vloxei32` uses the same template.

**Also triggered at LMUL=2 when two gathers are in flight (Phase 3).** `spmv_rvv_acc2_m2` issues two independent 16-element m2 gathers per loop iteration, so 32 element micro-ops are outstanding, the same as one m4 gather. On SuiteSparse `bcsstk17` it fails on O3 (rows of length ≥ 33, i.e. exactly those that take the dual-gather path; max relative error 0.25) and passes on MinorCPU with the same binary and input (`results/endterm/phase3/gem5_acc2_m2_investigation/`). QEMU passes all `rvv_acc2_m2` tests. This supports the pinned-destination hypothesis: the trigger is the number of in-flight indexed-load element micro-ops, not LMUL itself.

**Phase 3 scale-up (316 runs, `results/endterm/phase3/phase3_summary.md`):** every validation failure in Phase 3 is an LMUL=2 gather kernel.
- `rvv_acc2_m2` failed 22 of 29 runs: density sweep at 5–80% (it passed at 1% and 2%), all 5 power-law inputs, 6 of 10 size-sweep inputs, and 3 of 4 SuiteSparse matrices.
- `rvv_opt_m2` (one m2 gather in flight per chunk) failed 3 of 41 runs: scircuit at VLEN=256 and VLEN=128, and the power-law 16K × 2K input. It validated everywhere else, including all of Phase 2.
- All 246 other runs (scalar, LMUL=1 gather, dense, SpMM) validated.

So the failure probability rises with the number of in-flight indexed-load element micro-ops, and with longer load latency (inputs whose gathers miss).

**Handling:** `rvv_opt_m4` is not run. `rvv_opt_m2` and `rvv_acc2_m2` are still run, and the runs that validate are reported, but **no claim uses LMUL≥2 gather kernels**. If the bug can corrupt values, it may also perturb timing. Claims rely on scalar, LMUL=1-gather (`rvv_orig`, `rvv_opt_m1`, `rvv_acc2_m1`), dense (unit-stride) and SpMM kernels, which have never failed validation. If the O3 model doesn't enforce the dependency, the timing of the runs that pass may also be optimistic. A fix would be a gem5 C++ change (rebuild required), and that needs approval first (see PROGRESS.md).

## Other gem5 observations (not bugs)

- **L1D "miss rate" counts MSHR hits.** A demand access that merges into an in-flight miss is counted in `demandMisses`. At LMUL=4 the two register micro-ops that touch the same 64-byte line issue back to back, so `dense_m4` reports a 49% "miss rate" but only 2.1–4.8% primary misses (depending on density). L2 accesses are identical across m1/m2/m4. Reports use `demandMshrMisses` (primary misses) alongside the raw rate.
- **LMUL>1 arithmetic is split per register.** The committed micro-op count of a vector arithmetic instruction is the number of registers holding active elements, Σ ceil(len / VLMAX_m1), not the number of instructions. The harness prints this as `reg_chunks`, and the reports check it against `committedInstType`.

## Observation: unit-stride loads of exactly 128 bytes are slow in the dense GEMV (timing only)

In Phase 3 (1024 × 1024, matrix streamed from DRAM), the dense GEMV takes about 2.2M ROI cycles whenever one vector load covers exactly 128 bytes (two cache lines), and 0.5–0.77M cycles otherwise:

| kernel | VLEN | bytes per vector load | ROI cycles (d=5%) |
|---|---:|---:|---:|
| dense_m4 | 128 | 64 | 593,669 |
| dense_m2 | 256 | 64 | 590,030 |
| **dense_m4** | **256** | **128** | **2,244,889** |
| **dense_m2** | **512** | **128** | **2,257,635** |
| dense_m4 | 512 | 256 | 507,298 |

All of these runs validate, so this is a timing effect, not wrong results. Its cause is not yet known; one candidate is how the stride prefetcher trains on the paired line accesses. It will be examined with the prefetcher work in Phase 4B. It does not affect the dense-vs-sparse crossover, which uses the fastest dense variant (`dense_m2` at VLEN=256).
