/* Sparse matrix containers and input generators shared by all kernels. */
#ifndef SPARSE_RVV_MATRIX_H
#define SPARSE_RVV_MATRIX_H

#include <stdint.h>

/* Compressed Sparse Row matrix. Column indices are sorted within each row. */
typedef struct {
    uint32_t rows, cols, nnz;
    uint32_t *row_ptr;   /* rows + 1 entries */
    uint32_t *col_idx;   /* nnz entries */
    float *values;       /* nnz entries */
} csr_t;

/*
 * Midterm generator (exact replica of src/spmv_rvv.c): n x n matrix, entry
 * (i,j) is nonzero iff (17i + 13j + ij) mod 100 < density.
 * O(n^2) -- kept only for continuity with the midterm/Phase 1 numbers.
 */
void gen_midterm(csr_t *A, uint32_t n, uint32_t density);

/* Dense input vector used by every experiment: x[j] = (j mod 17 + 1) / 17. */
float *make_x(uint32_t cols);

/* Row-major dense copy of A (zeros included), for the dense GEMV kernels. */
float *csr_to_dense(const csr_t *A);

void *xalloc(uint64_t bytes);  /* 64-byte aligned, zeroed, exits on failure */

#endif
