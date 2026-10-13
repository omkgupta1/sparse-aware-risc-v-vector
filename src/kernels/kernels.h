/* All measured kernels: y = A * x. */
#ifndef SPARSE_RVV_KERNELS_H
#define SPARSE_RVV_KERNELS_H

#include <stdint.h>

#include "harness/matrix.h"

/* CSR SpMV kernels. */
typedef void (*spmv_fn)(const csr_t *A, const float *x, float *y);

/* Scalar CSR. Its translation unit is compiled with -march=rv64gc (no V). */
void spmv_scalar(const csr_t *A, const float *x, float *y);

/* Midterm RVV kernel: LMUL=1, vmul offsets, one vfredusum per chunk. */
void spmv_rvv_orig(const csr_t *A, const float *x, float *y);

/* Optimised RVV: vsll offsets, vfmacc into a vector accumulator,
 * one horizontal sum per row (rvv_reduce.h). */
void spmv_rvv_opt_m1(const csr_t *A, const float *x, float *y);
void spmv_rvv_opt_m2(const csr_t *A, const float *x, float *y);
void spmv_rvv_opt_m4(const csr_t *A, const float *x, float *y);

/* Two vector accumulators (halves the vfmacc dependency chain). */
void spmv_rvv_acc2_m1(const csr_t *A, const float *x, float *y);
void spmv_rvv_acc2_m2(const csr_t *A, const float *x, float *y);

/* CSR SpMM, Y = A * X, X dense cols x k row-major, k <= SPMM_MAX_K. */
#define SPMM_MAX_K 64
typedef void (*spmm_fn)(const csr_t *A, const float *X, uint32_t k, float *Y);
void spmm_scalar(const csr_t *A, const float *X, uint32_t k, float *Y);      /* rv64gc */
void spmm_rvv_vk(const csr_t *A, const float *X, uint32_t k, float *Y);      /* over k, m2 */
void spmm_rvv_colwise(const csr_t *A, const float *X, uint32_t k, float *Y); /* gathers, m1 */

/* Dense RVV GEMV on a row-major rows x cols matrix (zeros included):
 * unit-stride loads, vfmacc accumulator, one horizontal sum per row. */
typedef void (*gemv_fn)(const float *D, uint32_t rows, uint32_t cols,
                        const float *x, float *y);
void gemv_rvv_m1(const float *D, uint32_t rows, uint32_t cols, const float *x, float *y);
void gemv_rvv_m2(const float *D, uint32_t rows, uint32_t cols, const float *x, float *y);
void gemv_rvv_m4(const float *D, uint32_t rows, uint32_t cols, const float *x, float *y);

/* Phase 4A: sparse-aware execution on dense-format input (gemv_mask.c). */
void gemv_skip_m1(const float *D, uint32_t rows, uint32_t cols, const float *x, float *y);
void gemv_skip_m2(const float *D, uint32_t rows, uint32_t cols, const float *x, float *y);
void gemv_compress_m1(const float *D, uint32_t rows, uint32_t cols, const float *x, float *y);
void gemv_compress_slide_m1(const float *D, uint32_t rows, uint32_t cols, const float *x, float *y);

/* Phase 4A: density-aware hybrid (hybrid.c). Row blocks whose density is
 * >= thr_pct are stored and executed densely, the others as CSR. */
typedef struct {
    const csr_t *A;
    uint32_t block_rows, nblocks;
    double thr_pct;
    float **dense;          /* per block: dense copy, or NULL for CSR blocks */
    uint32_t dense_blocks, dense_rows;
    uint64_t dense_nnz;
} hybrid_t;
void hybrid_build(hybrid_t *H, const csr_t *A, uint32_t block_rows, double thr_pct);
void spmv_hybrid(const hybrid_t *H, const float *x, float *y);

/* Scalar reference in double precision (compiled with -march=rv64gc and
 * -fno-tree-vectorize). abs_out[i] = sum_k |a_ik * x_k|, used to scale the
 * relative error tolerance. */
void reference_spmv(const csr_t *A, const float *x, double *ref_out, double *abs_out);
void reference_spmm(const csr_t *A, const float *X, uint32_t k, double *ref_out, double *abs_out);

#endif
