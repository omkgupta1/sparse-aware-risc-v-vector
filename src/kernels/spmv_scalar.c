/* Scalar CSR SpMV. Built with -march=rv64gc: no vector instructions possible. */
#include "kernels/kernels.h"

void spmv_scalar(const csr_t *A, const float *x, float *y)
{
    const uint32_t *row_ptr = A->row_ptr;
    const uint32_t *col_idx = A->col_idx;
    const float *values = A->values;

    for (uint32_t i = 0; i < A->rows; i++) {
        float sum = 0.0f;
        for (uint32_t k = row_ptr[i]; k < row_ptr[i + 1]; k++)
            sum += values[k] * x[col_idx[k]];
        y[i] = sum;
    }
}
