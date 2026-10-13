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

/* Dense RVV GEMV on a row-major rows x cols matrix (zeros included):
 * unit-stride loads, vfmacc accumulator, one horizontal sum per row. */
typedef void (*gemv_fn)(const float *D, uint32_t rows, uint32_t cols,
                        const float *x, float *y);
void gemv_rvv_m1(const float *D, uint32_t rows, uint32_t cols, const float *x, float *y);
void gemv_rvv_m2(const float *D, uint32_t rows, uint32_t cols, const float *x, float *y);
void gemv_rvv_m4(const float *D, uint32_t rows, uint32_t cols, const float *x, float *y);

/* Scalar reference in double precision (compiled with -march=rv64gc and
 * -fno-tree-vectorize). abs_out[i] = sum_k |a_ik * x_k|, used to scale the
 * relative error tolerance. */
void reference_spmv(const csr_t *A, const float *x, double *ref_out, double *abs_out);

#endif
