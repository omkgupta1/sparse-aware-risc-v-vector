/*
 * RVV CSR SpMM, Y = A * X with X dense (cols x k, row-major).
 *
 * spmm_rvv_vk:       vectorise over the k dense columns. For every nonzero
 *                    a = A[i][c], Y[i][:] += a * X[c][:] with one unit-stride
 *                    load and vfmacc.vf (LMUL=2). No gathers, no reductions.
 * spmm_rvv_colwise:  one SpMV-style gather pass per dense column j: gathers
 *                    X[col][j] with byte offsets col*k*4 (LMUL=1, vfmacc into
 *                    a vector accumulator, one horizontal sum per output).
 */
#include <riscv_vector.h>

#include "kernels/kernels.h"
#include "kernels/rvv_reduce.h"

void spmm_rvv_vk(const csr_t *A, const float *X, uint32_t k, float *Y)
{
    for (uint32_t i = 0; i < A->rows; i++) {
        const uint32_t start = A->row_ptr[i], end = A->row_ptr[i + 1];
        for (uint32_t j = 0; j < k; ) {
            size_t vl = __riscv_vsetvl_e32m2(k - j);
            vfloat32m2_t acc = __riscv_vfmv_v_f_f32m2(0.0f, vl);
            for (uint32_t p = start; p < end; p++) {
                const float *xr = X + (uint64_t)A->col_idx[p] * k + j;
                vfloat32m2_t xv = __riscv_vle32_v_f32m2(xr, vl);
                acc = __riscv_vfmacc_vf_f32m2(acc, A->values[p], xv, vl);
            }
            __riscv_vse32_v_f32m2(Y + (uint64_t)i * k + j, acc, vl);
            j += vl;
        }
    }
}

void spmm_rvv_colwise(const csr_t *A, const float *X, uint32_t k, float *Y)
{
    const size_t vlmax = __riscv_vsetvlmax_e32m1();
    const uint32_t stride = k * (uint32_t)sizeof(float);

    for (uint32_t i = 0; i < A->rows; i++) {
        const uint32_t start = A->row_ptr[i], end = A->row_ptr[i + 1];
        for (uint32_t j = 0; j < k; j++) {
            const float *xcol = X + j;
            vfloat32m1_t acc = __riscv_vfmv_v_f_f32m1(0.0f, vlmax);
            for (uint32_t p = start; p < end; ) {
                size_t vl = __riscv_vsetvl_e32m1(end - p);
                vfloat32m1_t va = __riscv_vle32_v_f32m1(&A->values[p], vl);
                vuint32m1_t vi = __riscv_vle32_v_u32m1(&A->col_idx[p], vl);
                vi = __riscv_vmul_vx_u32m1(vi, stride, vl);
                vfloat32m1_t vx = __riscv_vluxei32_v_f32m1(xcol, vi, vl);
                acc = __riscv_vfmacc_vv_f32m1_tu(acc, va, vx, vl);
                p += vl;
            }
            Y[(uint64_t)i * k + j] = rvv_sum_m1(acc);
        }
    }
}
