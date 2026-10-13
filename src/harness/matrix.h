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

/* Row-length distributions for gen_random(). */
typedef enum { DIST_UNIFORM, DIST_POWERLAW } dist_t;

/*
 * Synthetic rows x cols matrix with fixed-seed PRNG, O(rows + nnz) work.
 *   DIST_UNIFORM:  every entry is nonzero with probability `density`
 *                  (row lengths ~ Binomial(cols, density)).
 *   DIST_POWERLAW: row i gets an expected length L_i drawn from a Pareto
 *                  distribution with shape `alpha`, scaled so that sum L_i =
 *                  density * rows * cols (capped at cols); entries of row i are
 *                  then nonzero with probability L_i / cols.
 * Columns within a row are drawn by geometric skipping, so they are sorted and
 * uniformly spread over [0, cols). Values are (h mod 11 + 1) / 11 for a PRNG h.
 */
void gen_random(csr_t *A, uint32_t rows, uint32_t cols, double density,
                dist_t dist, double alpha, uint64_t seed);

/*
 * Binary CSR file written by scripts/mtx2bin.py from a Matrix Market file:
 *   char magic[4] = "CSRB"; uint32 version = 1, rows, cols, nnz;
 *   uint32 row_ptr[rows+1]; uint32 col_idx[nnz]; float values[nnz]
 * (little-endian). Exits on any format error.
 */
void load_csrbin(csr_t *A, const char *path);

/* Dense input vector used by every experiment: x[j] = (j mod 17 + 1) / 17. */
float *make_x(uint32_t cols);

/* Row-major dense copy of A (zeros included), for the dense GEMV kernels. */
float *csr_to_dense(const csr_t *A);

/* Dense row-major cols x k matrix for SpMM: X[c*k + j] = ((c*k + j) mod 17 + 1) / 17. */
float *make_X(uint32_t cols, uint32_t k);

void *xalloc(uint64_t bytes);  /* 64-byte aligned, zeroed, exits on failure */

#endif
