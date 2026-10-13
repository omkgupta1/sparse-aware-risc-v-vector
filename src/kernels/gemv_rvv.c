/*
 * Dense RVV GEMV, y = D * x, on a row-major matrix that still contains its
 * zeros. Unit-stride loads of D and x, vfmacc into a vector accumulator,
 * one horizontal sum per row (rvv_reduce.h). Instantiated for LMUL = 1, 2, 4.
 */
#include <riscv_vector.h>

#include "kernels/kernels.h"
#include "kernels/rvv_reduce.h"

#define DEFINE_GEMV_RVV(LMUL)                                                  \
void gemv_rvv_##LMUL(const float *D, uint32_t rows, uint32_t cols,             \
                     const float *x, float *y)                                 \
{                                                                              \
    const size_t vlmax = __riscv_vsetvlmax_e32##LMUL();                        \
                                                                               \
    for (uint32_t i = 0; i < rows; i++) {                                      \
        const float *row = D + (uint64_t)i * cols;                             \
        vfloat32##LMUL##_t acc = __riscv_vfmv_v_f_f32##LMUL(0.0f, vlmax);      \
                                                                               \
        for (uint32_t j = 0; j < cols; ) {                                     \
            size_t vl = __riscv_vsetvl_e32##LMUL(cols - j);                    \
            vfloat32##LMUL##_t va = __riscv_vle32_v_f32##LMUL(&row[j], vl);    \
            vfloat32##LMUL##_t vx = __riscv_vle32_v_f32##LMUL(&x[j], vl);      \
            acc = __riscv_vfmacc_vv_f32##LMUL##_tu(acc, va, vx, vl);           \
            j += vl;                                                           \
        }                                                                      \
                                                                               \
        y[i] = rvv_sum_##LMUL(acc);                                            \
    }                                                                          \
}

DEFINE_GEMV_RVV(m1)
DEFINE_GEMV_RVV(m2)
DEFINE_GEMV_RVV(m4)
