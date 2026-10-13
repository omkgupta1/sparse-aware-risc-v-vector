#include "harness/matrix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *xalloc(uint64_t bytes)
{
    uint64_t rounded = (bytes + 63) & ~(uint64_t)63;
    void *p = aligned_alloc(64, rounded ? rounded : 64);
    if (!p) {
        fprintf(stderr, "out of memory allocating %llu bytes\n",
                (unsigned long long)bytes);
        exit(3);
    }
    memset(p, 0, rounded ? rounded : 64);
    return p;
}

void gen_midterm(csr_t *A, uint32_t n, uint32_t density)
{
    /* Count first so the arrays are exactly sized. */
    uint32_t nnz = 0;
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t j = 0; j < n; j++)
            if ((i * 17u + j * 13u + i * j) % 100u < density)
                nnz++;

    A->rows = A->cols = n;
    A->nnz = nnz;
    A->row_ptr = xalloc((uint64_t)(n + 1) * sizeof(uint32_t));
    A->col_idx = xalloc((uint64_t)nnz * sizeof(uint32_t));
    A->values = xalloc((uint64_t)nnz * sizeof(float));

    uint32_t k = 0;
    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t j = 0; j < n; j++) {
            if ((i * 17u + j * 13u + i * j) % 100u < density) {
                A->values[k] = (float)((i + j) % 11 + 1) / 11.0f;
                A->col_idx[k] = j;
                k++;
            }
        }
        A->row_ptr[i + 1] = k;
    }
}

float *make_x(uint32_t cols)
{
    float *x = xalloc((uint64_t)cols * sizeof(float));
    for (uint32_t j = 0; j < cols; j++)
        x[j] = (float)(j % 17 + 1) / 17.0f;
    return x;
}

float *csr_to_dense(const csr_t *A)
{
    float *D = xalloc((uint64_t)A->rows * A->cols * sizeof(float));
    for (uint32_t i = 0; i < A->rows; i++)
        for (uint32_t k = A->row_ptr[i]; k < A->row_ptr[i + 1]; k++)
            D[(uint64_t)i * A->cols + A->col_idx[k]] = A->values[k];
    return D;
}
