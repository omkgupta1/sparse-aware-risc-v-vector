/*
 * RVV CSR kernel with TWO vector accumulators (LMUL = 1, 2).
 *
 * Same as spmv_rvv_opt, except that full chunks alternate between two
 * independent accumulators, halving the length of the loop-carried vfmacc
 * dependency chain. The (at most two) remaining chunks of a row go into the
 * first accumulator (tail-undisturbed); both are added before the per-row sum.
 * Tests the Phase 2 hypothesis that rvv_opt_m1 is limited by that chain.
 */
#include <riscv_vector.h>

#include "kernels/kernels.h"
#include "kernels/rvv_reduce.h"

#define DEFINE_SPMV_RVV_ACC2(LMUL)                                             \
void spmv_rvv_acc2_##LMUL(const csr_t *A, const float *x, float *y)           \
{                                                                              \
    const uint32_t *row_ptr = A->row_ptr;                                      \
    const uint32_t *col_idx = A->col_idx;                                      \
    const float *values = A->values;                                           \
    const size_t vlmax = __riscv_vsetvlmax_e32##LMUL();                        \
                                                                               \
    for (uint32_t i = 0; i < A->rows; i++) {                                   \
        uint32_t k = row_ptr[i];                                               \
        const uint32_t end = row_ptr[i + 1];                                   \
        vfloat32##LMUL##_t acc0 = __riscv_vfmv_v_f_f32##LMUL(0.0f, vlmax);     \
        vfloat32##LMUL##_t acc1 = __riscv_vfmv_v_f_f32##LMUL(0.0f, vlmax);     \
                                                                               \
        while (end - k >= 2 * vlmax) {                                         \
            vuint32##LMUL##_t i0 = __riscv_vle32_v_u32##LMUL(&col_idx[k], vlmax);          \
            vuint32##LMUL##_t i1 = __riscv_vle32_v_u32##LMUL(&col_idx[k + vlmax], vlmax);  \
            vfloat32##LMUL##_t a0 = __riscv_vle32_v_f32##LMUL(&values[k], vlmax);          \
            vfloat32##LMUL##_t a1 = __riscv_vle32_v_f32##LMUL(&values[k + vlmax], vlmax);  \
            i0 = __riscv_vsll_vx_u32##LMUL(i0, 2, vlmax);                      \
            i1 = __riscv_vsll_vx_u32##LMUL(i1, 2, vlmax);                      \
            vfloat32##LMUL##_t x0 = __riscv_vluxei32_v_f32##LMUL(x, i0, vlmax);\
            vfloat32##LMUL##_t x1 = __riscv_vluxei32_v_f32##LMUL(x, i1, vlmax);\
            acc0 = __riscv_vfmacc_vv_f32##LMUL##_tu(acc0, a0, x0, vlmax);      \
            acc1 = __riscv_vfmacc_vv_f32##LMUL##_tu(acc1, a1, x1, vlmax);      \
            k += 2 * vlmax;                                                    \
        }                                                                      \
        while (k < end) {                                                      \
            size_t vl = __riscv_vsetvl_e32##LMUL(end - k);                     \
            vfloat32##LMUL##_t va = __riscv_vle32_v_f32##LMUL(&values[k], vl); \
            vuint32##LMUL##_t vi = __riscv_vle32_v_u32##LMUL(&col_idx[k], vl); \
            vi = __riscv_vsll_vx_u32##LMUL(vi, 2, vl);                         \
            vfloat32##LMUL##_t vx = __riscv_vluxei32_v_f32##LMUL(x, vi, vl);   \
            acc0 = __riscv_vfmacc_vv_f32##LMUL##_tu(acc0, va, vx, vl);         \
            k += vl;                                                           \
        }                                                                      \
                                                                               \
        acc0 = __riscv_vfadd_vv_f32##LMUL(acc0, acc1, vlmax);                  \
        y[i] = rvv_sum_##LMUL(acc0);                                           \
    }                                                                          \
}

DEFINE_SPMV_RVV_ACC2(m1)
DEFINE_SPMV_RVV_ACC2(m2)
