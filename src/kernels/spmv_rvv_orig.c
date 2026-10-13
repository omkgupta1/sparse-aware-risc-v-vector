/* Midterm RVV CSR kernel (src/spmv_rvv.c), unchanged apart from taking its
 * arrays through csr_t: LMUL=1, offsets via vmul, one reduction per chunk. */
#include <riscv_vector.h>

#include "kernels/kernels.h"

void spmv_rvv_orig(const csr_t *A, const float *x, float *y)
{
    const uint32_t *row_ptr = A->row_ptr;
    const uint32_t *columns = A->col_idx;
    const float *values = A->values;

    for (uint32_t i = 0; i < A->rows; i++) {
        float sum = 0.0f;
        uint32_t start = row_ptr[i];
        uint32_t end = row_ptr[i + 1];

        for (uint32_t k = start; k < end; ) {
            size_t vl = __riscv_vsetvl_e32m1(end - k);

            vfloat32m1_t va = __riscv_vle32_v_f32m1(&values[k], vl);
            vuint32m1_t vi = __riscv_vle32_v_u32m1(&columns[k], vl);
            vuint32m1_t offsets = __riscv_vmul_vx_u32m1(vi, sizeof(float), vl);
            vfloat32m1_t vx = __riscv_vluxei32_v_f32m1(x, offsets, vl);
            vfloat32m1_t product = __riscv_vfmul_vv_f32m1(va, vx, vl);
            vfloat32m1_t initial = __riscv_vfmv_v_f_f32m1(0.0f, 1);
            vfloat32m1_t reduced =
                __riscv_vfredusum_vs_f32m1_f32m1(product, initial, vl);

            sum += __riscv_vfmv_f_s_f32m1_f32(reduced);
            k += vl;
        }

        y[i] = sum;
    }
}
