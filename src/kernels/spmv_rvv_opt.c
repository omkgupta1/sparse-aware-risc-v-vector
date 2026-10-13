/*
 * Optimised RVV CSR kernel, instantiated for LMUL = 1, 2, 4.
 *
 * Differences from the midterm kernel:
 *   - partial products accumulate in a vector register with vfmacc
 *     (tail-undisturbed, so a short final chunk keeps earlier partial sums),
 *   - ONE horizontal sum per row instead of one per chunk (LMUL>1 groups are
 *     folded to LMUL=1 first, see rvv_reduce.h),
 *   - byte offsets via vsll (shift by 2) instead of vmul.
 */
#include <riscv_vector.h>

#include "kernels/kernels.h"
#include "kernels/rvv_reduce.h"

#define DEFINE_SPMV_RVV_OPT(LMUL)                                              \
void spmv_rvv_opt_##LMUL(const csr_t *A, const float *x, float *y)            \
{                                                                              \
    const uint32_t *row_ptr = A->row_ptr;                                      \
    const uint32_t *col_idx = A->col_idx;                                      \
    const float *values = A->values;                                           \
    const size_t vlmax = __riscv_vsetvlmax_e32##LMUL();                        \
                                                                               \
    for (uint32_t i = 0; i < A->rows; i++) {                                   \
        uint32_t k = row_ptr[i];                                               \
        const uint32_t end = row_ptr[i + 1];                                   \
        vfloat32##LMUL##_t acc = __riscv_vfmv_v_f_f32##LMUL(0.0f, vlmax);      \
                                                                               \
        while (k < end) {                                                      \
            size_t vl = __riscv_vsetvl_e32##LMUL(end - k);                     \
            vfloat32##LMUL##_t va = __riscv_vle32_v_f32##LMUL(&values[k], vl); \
            vuint32##LMUL##_t vi = __riscv_vle32_v_u32##LMUL(&col_idx[k], vl); \
            vi = __riscv_vsll_vx_u32##LMUL(vi, 2, vl);                         \
            vfloat32##LMUL##_t vx = __riscv_vluxei32_v_f32##LMUL(x, vi, vl);   \
            acc = __riscv_vfmacc_vv_f32##LMUL##_tu(acc, va, vx, vl);           \
            k += vl;                                                           \
        }                                                                      \
                                                                               \
        y[i] = rvv_sum_##LMUL(acc);                                            \
    }                                                                          \
}

DEFINE_SPMV_RVV_OPT(m1)
DEFINE_SPMV_RVV_OPT(m2)
DEFINE_SPMV_RVV_OPT(m4)
