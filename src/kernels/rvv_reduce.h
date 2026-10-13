/*
 * Horizontal sum of an e32 accumulator register group.
 *
 * LMUL > 1 groups are first folded to one register with LMUL=1 vfadd.vv
 * (vget is free: it only names a register of the group), then reduced with an
 * LMUL=1 vfredusum.
 *
 * Why not one vfredusum over the whole group: gem5 25.1's O3 CPU can read stale
 * values for the upper registers of the vs2 group when vfredusum's vd overlaps
 * vs2 (legal RVV, and what GCC emits). See src/tests/rvv_lmul_check.c and
 * PROGRESS.md (Phase 2). Folding first is also the usual faster pattern.
 */
#ifndef SPARSE_RVV_REDUCE_H
#define SPARSE_RVV_REDUCE_H

#include <riscv_vector.h>

static inline float rvv_sum_m1(vfloat32m1_t acc)
{
    const size_t vlmax = __riscv_vsetvlmax_e32m1();
    vfloat32m1_t zero = __riscv_vfmv_v_f_f32m1(0.0f, 1);
    return __riscv_vfmv_f_s_f32m1_f32(__riscv_vfredusum_vs_f32m1_f32m1(acc, zero, vlmax));
}

static inline float rvv_sum_m2(vfloat32m2_t acc)
{
    const size_t vlmax = __riscv_vsetvlmax_e32m1();
    vfloat32m1_t s = __riscv_vfadd_vv_f32m1(__riscv_vget_v_f32m2_f32m1(acc, 0),
                                            __riscv_vget_v_f32m2_f32m1(acc, 1), vlmax);
    return rvv_sum_m1(s);
}

static inline float rvv_sum_m4(vfloat32m4_t acc)
{
    const size_t vlmax = __riscv_vsetvlmax_e32m1();
    vfloat32m1_t s01 = __riscv_vfadd_vv_f32m1(__riscv_vget_v_f32m4_f32m1(acc, 0),
                                              __riscv_vget_v_f32m4_f32m1(acc, 1), vlmax);
    vfloat32m1_t s23 = __riscv_vfadd_vv_f32m1(__riscv_vget_v_f32m4_f32m1(acc, 2),
                                              __riscv_vget_v_f32m4_f32m1(acc, 3), vlmax);
    return rvv_sum_m1(__riscv_vfadd_vv_f32m1(s01, s23, vlmax));
}

#endif
