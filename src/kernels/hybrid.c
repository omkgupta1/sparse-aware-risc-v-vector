/*
 * Density-aware hybrid SpMV (Phase 4A).
 *
 * The matrix is split into blocks of `block_rows` rows. hybrid_build() (outside
 * the ROI) stores a block densely if its density is >= the threshold, which is
 * the measured dense-vs-sparse crossover (Phase 3: ~58.2% for 1024 columns).
 * spmv_hybrid() then runs, per block, either
 *   - the dense GEMV loop (same code as gemv_rvv_m2: unit-stride, LMUL=2), or
 *   - the midterm CSR loop (same code as spmv_rvv_orig: LMUL=1 gathers),
 * i.e. the best measured dense kernel and the best reliable sparse kernel.
 */
#include <riscv_vector.h>
#include <stdlib.h>

#include "kernels/kernels.h"
#include "kernels/rvv_reduce.h"

void hybrid_build(hybrid_t *H, const csr_t *A, uint32_t block_rows, double thr_pct)
{
    H->A = A;
    H->block_rows = block_rows;
    H->nblocks = (A->rows + block_rows - 1) / block_rows;
    H->thr_pct = thr_pct;
    H->dense = xalloc((uint64_t)H->nblocks * sizeof(float *));
    H->dense_blocks = H->dense_rows = H->dense_nnz = 0;

    for (uint32_t b = 0; b < H->nblocks; b++) {
        uint32_t r0 = b * block_rows;
        uint32_t r1 = r0 + block_rows < A->rows ? r0 + block_rows : A->rows;
        uint64_t nnz = A->row_ptr[r1] - A->row_ptr[r0];
        double density = (double)nnz / ((double)(r1 - r0) * A->cols);
        if (density * 100.0 >= thr_pct) {
            float *Db = xalloc((uint64_t)(r1 - r0) * A->cols * sizeof(float));
            for (uint32_t i = r0; i < r1; i++)
                for (uint32_t k = A->row_ptr[i]; k < A->row_ptr[i + 1]; k++)
                    Db[(uint64_t)(i - r0) * A->cols + A->col_idx[k]] = A->values[k];
            H->dense[b] = Db;
            H->dense_blocks++;
            H->dense_rows += r1 - r0;
            H->dense_nnz += nnz;
        }
    }
}

static inline void dense_rows_m2(const float *D, uint32_t nrows, uint32_t cols,
                                 const float *x, float *y)
{
    const size_t vlmax = __riscv_vsetvlmax_e32m2();
    for (uint32_t i = 0; i < nrows; i++) {
        const float *row = D + (uint64_t)i * cols;
        vfloat32m2_t acc = __riscv_vfmv_v_f_f32m2(0.0f, vlmax);
        for (uint32_t j = 0; j < cols; ) {
            size_t vl = __riscv_vsetvl_e32m2(cols - j);
            vfloat32m2_t va = __riscv_vle32_v_f32m2(&row[j], vl);
            vfloat32m2_t vx = __riscv_vle32_v_f32m2(&x[j], vl);
            acc = __riscv_vfmacc_vv_f32m2_tu(acc, va, vx, vl);
            j += vl;
        }
        y[i] = rvv_sum_m2(acc);
    }
}

static inline void csr_rows_orig(const csr_t *A, uint32_t r0, uint32_t r1,
                                 const float *x, float *y)
{
    for (uint32_t i = r0; i < r1; i++) {
        float sum = 0.0f;
        uint32_t end = A->row_ptr[i + 1];
        for (uint32_t k = A->row_ptr[i]; k < end; ) {
            size_t vl = __riscv_vsetvl_e32m1(end - k);
            vfloat32m1_t va = __riscv_vle32_v_f32m1(&A->values[k], vl);
            vuint32m1_t vi = __riscv_vle32_v_u32m1(&A->col_idx[k], vl);
            vuint32m1_t off = __riscv_vmul_vx_u32m1(vi, sizeof(float), vl);
            vfloat32m1_t vx = __riscv_vluxei32_v_f32m1(x, off, vl);
            vfloat32m1_t p = __riscv_vfmul_vv_f32m1(va, vx, vl);
            vfloat32m1_t red = __riscv_vfredusum_vs_f32m1_f32m1(
                p, __riscv_vfmv_v_f_f32m1(0.0f, 1), vl);
            sum += __riscv_vfmv_f_s_f32m1_f32(red);
            k += vl;
        }
        y[i] = sum;
    }
}

void spmv_hybrid(const hybrid_t *H, const float *x, float *y)
{
    const csr_t *A = H->A;
    for (uint32_t b = 0; b < H->nblocks; b++) {
        uint32_t r0 = b * H->block_rows;
        uint32_t r1 = r0 + H->block_rows < A->rows ? r0 + H->block_rows : A->rows;
        if (H->dense[b])
            dense_rows_m2(H->dense[b], r1 - r0, A->cols, x, y + r0);
        else
            csr_rows_orig(A, r0, r1, x, y);
    }
}
