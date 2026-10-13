/* Scalar CSR SpMM, Y = A * X (X: cols x k, row-major). Built with -march=rv64gc. */
#include "kernels/kernels.h"

void spmm_scalar(const csr_t *A, const float *X, uint32_t k, float *Y)
{
    float acc[SPMM_MAX_K];

    for (uint32_t i = 0; i < A->rows; i++) {
        for (uint32_t j = 0; j < k; j++)
            acc[j] = 0.0f;
        for (uint32_t p = A->row_ptr[i]; p < A->row_ptr[i + 1]; p++) {
            const float a = A->values[p];
            const float *xr = X + (uint64_t)A->col_idx[p] * k;
            for (uint32_t j = 0; j < k; j++)
                acc[j] += a * xr[j];
        }
        for (uint32_t j = 0; j < k; j++)
            Y[(uint64_t)i * k + j] = acc[j];
    }
}
