/* Validation reference. Built with -march=rv64gc -fno-tree-vectorize so it is
 * genuinely scalar and independent of every kernel under test. */
#include <math.h>

#include "kernels/kernels.h"

void reference_spmv(const csr_t *A, const float *x, double *ref_out, double *abs_out)
{
    for (uint32_t i = 0; i < A->rows; i++) {
        double sum = 0.0, abs_sum = 0.0;
        for (uint32_t k = A->row_ptr[i]; k < A->row_ptr[i + 1]; k++) {
            double p = (double)A->values[k] * (double)x[A->col_idx[k]];
            sum += p;
            abs_sum += fabs(p);
        }
        ref_out[i] = sum;
        abs_out[i] = abs_sum;
    }
}
