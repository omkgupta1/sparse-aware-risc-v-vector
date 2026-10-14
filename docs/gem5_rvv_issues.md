# gem5 25.1 RVV Correctness Issues Found in This Project

Four gem5 25.1.0.1 problems were found while building and validating this project.
- **Issues 1–3** are functional bugs in the **O3 CPU** model's RVV support. They produce wrong *architectural results* or crashes, not just wrong timing, and none occur on gem5's in-order MinorCPU or on QEMU. Issues 1 and 2 need LMUL ≥ 2; Issue 3 occurs at LMUL=1 with tail-undisturbed slides.
- **Issue 4** is in the cache/prefetcher interface: gem5's `IndirectMemoryPrefetcher` panics on read hits.

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

**O3-specific:** the same binary on gem5's in-order **MinorCPU** gives output identical to QEMU for all of T1–T12 (`results/endterm/phase2/gem5_rvv_check_minor/`).

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

## Issue 3: register-merging compaction (`vslideup`/`vslidedown`, tail-undisturbed) faults on O3

**Reproducer:** `gemv_compress_slide_m1` in `src/kernels/gemv_mask.c` (kernel name `dense_compress_slide_m1`).
- It packs the nonzeros of a dense row into a vector-register buffer: `vcompress`, then tail-undisturbed `vslideup.vx` at the fill position, with `vslidedown.vx` for leftovers.
- When the buffer is full it reads it at full width: `vsll` → `vluxei32` gather → `vfmacc`.

| Platform | Result |
|---|---|
| QEMU 8.2.2, default tail/mask policy | PASSED (1/20/60% density, n=1024) |
| QEMU with `rvv_ta_all_1s=true,rvv_ma_all_1s=true` (agnostic elements = all ones) | PASSED, so the code does not depend on unspecified tail values |
| gem5 MinorCPU | PASSED |
| **gem5 O3** | **panic: page-table fault** on a gathered address (e.g. `0x1000b7b7c`) during commit; gem5 exit 134 |

Evidence: `results/endterm/phase4a_gem5_compress_investigation/` (n=256 d=5% on Minor and O3 for both versions, plus the first failing 1024×1024 run).

The gathered offsets come from the merged index buffer, so on O3 that buffer's register contents are stale. This is consistent with Issue 1 (reads after tail-undisturbed partial writes see stale register data), here at LMUL=1 with slide instructions.

**Workaround used:** `gemv_compress_m1` stages the compacted values and indices in a small memory buffer (`vse32` at the fill position, `vle32` of a full vector when ready), with no register merging. It validates on O3, Minor and QEMU.

## Issue 4: `IndirectMemoryPrefetcher` panics on read hits (classic caches, timing mode)

**Symptom:** with `IndirectMemoryPrefetcher` as the L1D prefetcher, every run (scalar CSR *and* RVV) aborts:

```
panic: PrefetchInfo::get called with a request with no data.
```

Evidence: `results/endterm/phase4b_prep/smoke_imp_{rvv_orig,scalar}/run.log`.

**Cause** (read in `src/mem/cache/base.cc` and `src/mem/cache/prefetch/base.cc`):
1. `BaseCache::recvTimingReq` notifies the prefetcher of a hit (`ppHit`) right after `access()`. At that point `satisfyRequest()` has already copied the block data into the packet, but the packet is still a *request* (`ReadReq`). It only becomes a response later, in `handleTimingReqHit`.
2. `PrefetchInfo::PrefetchInfo(pkt, addr, miss)` copies the data only if `pkt->hasData()`. That tests the command attribute, which is false for `ReadReq`, so `data` stays `nullptr` for every read hit.
3. `IndirectMemory::calculatePrefetch` calls `pfi.get<T>()` on read hits of ≤ 8 bytes, which panics.

So gem5's only indirect prefetcher cannot run in this configuration.

**Consequence for this project:**
- IMP cannot be used as a baseline.
- The Vector-Indirect Prefetcher reads index values from the packet itself: it overrides `notify()` and treats "satisfied read hit" as having data.
- The VIP does not touch `PrefetchInfo`, so it does not fix IMP. The fix would be in `PrefetchInfo` (copy data when `pkt->isRead() && !miss`); that is not part of this project's patch.

## Other gem5 observations (not bugs)

- **L1D "miss rate" counts MSHR hits.** A demand access that merges into an in-flight miss is counted in `demandMisses`. At LMUL=4 the two register micro-ops that touch the same 64-byte line issue back to back, so `dense_m4` reports a 49% "miss rate" but only 2.1–4.8% primary misses (depending on density). L2 accesses are identical across m1/m2/m4. Reports use `demandMshrMisses` (primary misses) alongside the raw rate.
- **LMUL>1 arithmetic is split per register.** The committed micro-op count of a vector arithmetic instruction is the number of registers holding active elements, Σ ceil(len / VLMAX_m1), not the number of instructions. The harness prints this as `reg_chunks`, and the reports check it against `committedInstType`.

## Observation: unit-stride loads of exactly 128 bytes are slow in the dense GEMV (timing only; prefetcher training)

In Phase 3 (1024 × 1024, matrix streamed from DRAM), the dense GEMV takes about 2.2M ROI cycles whenever one vector load covers exactly 128 bytes (two cache lines), and 0.5–0.77M cycles otherwise:

| kernel | VLEN | bytes per vector load | ROI cycles (d=5%) |
|---|---:|---:|---:|
| dense_m4 | 128 | 64 | 593,669 |
| dense_m2 | 256 | 64 | 590,030 |
| **dense_m4** | **256** | **128** | **2,244,889** |
| **dense_m2** | **512** | **128** | **2,257,635** |
| dense_m4 | 512 | 256 | 507,298 |

All of these runs validate, so this is a timing effect, not wrong results. It does not affect the dense-vs-sparse crossover, which uses the fastest dense variant (`dense_m2` at VLEN=256).

**Cause isolated (Phase 4B prep, `results/endterm/phase4b_prep/`, `--l1d-pf/--l2-pf` options of `configs/sparse_rvv.py`):** it is a stride-prefetcher training failure.

| dense_m4, d=5%, VLEN=256 | ROI cycles | L1D primary miss | L1D prefetches identified | L1D pf coverage |
|---|---:|---:|---:|---:|
| stride L1D + stride L2 (default) | 2,253,972 | 24.9% | 947 | 0.04% |
| no prefetchers | 2,259,039 | 24.9% | – | – |
| stride L1D only (L2 prefetcher off) | 838,938 | 0.4% | 252,138 | 0 (all late/MSHR hits) |
| dense_m2 (64-byte loads), default | 589,118 | 0.4% | 252,146 | 82.3% |

With 128-byte (two-line) vector loads *and* an active L2 prefetcher, the L1D stride prefetcher almost never detects the stream, so the kernel runs as if it had no L1D prefetcher. The exact mechanism inside the stride table was not traced further.
