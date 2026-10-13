/*
 * Phase 1 re-measurement of the midterm kernel.
 *
 * Identical to src/spmv_rvv.c (midterm: same matrix generator, same RVV
 * kernel, same validation) except that ONLY the RVV SpMV kernel is wrapped
 * in ROI markers (see common/roi.h). Matrix generation and the scalar
 * validation run outside the region of interest.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <riscv_vector.h>

#include "common/roi.h"

#define N 256
#define MAX_NNZ (N * N)

static float values[MAX_NNZ];
static uint32_t columns[MAX_NNZ];
static uint32_t row_ptr[N + 1];
static float x[N];
static float y[N];

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s NONZERO_PERCENT\n", argv[0]);
        return 1;
    }

    int density = atoi(argv[1]);
    if (density < 1 || density > 100) {
        fprintf(stderr, "Density must be between 1 and 100\n");
        return 1;
    }

    for (int j = 0; j < N; j++)
        x[j] = (float)(j % 17 + 1) / 17.0f;

    int nnz = 0;
    row_ptr[0] = 0;

    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            unsigned int pattern =
                ((unsigned int)i * 17u +
                 (unsigned int)j * 13u +
                 (unsigned int)i * (unsigned int)j) % 100u;

            if (pattern < (unsigned int)density) {
                values[nnz] = (float)((i + j) % 11 + 1) / 11.0f;
                columns[nnz] = (uint32_t)j;
                nnz++;
            }
        }
        row_ptr[i + 1] = (uint32_t)nnz;
    }

    ROI_BEGIN();

    /* RVV CSR SpMV: y[i] = sum(values[k] * x[columns[k]]) */
    for (int i = 0; i < N; i++) {
        float sum = 0.0f;
        uint32_t start = row_ptr[i];
        uint32_t end = row_ptr[i + 1];

        for (uint32_t k = start; k < end; ) {
            size_t vl = __riscv_vsetvl_e32m1(end - k);

            vfloat32m1_t va =
                __riscv_vle32_v_f32m1(&values[k], vl);

            vuint32m1_t vi =
                __riscv_vle32_v_u32m1(&columns[k], vl);

            vuint32m1_t offsets =
                __riscv_vmul_vx_u32m1(vi, sizeof(float), vl);

            vfloat32m1_t vx =
                __riscv_vluxei32_v_f32m1(x, offsets, vl);

            vfloat32m1_t product =
                __riscv_vfmul_vv_f32m1(va, vx, vl);

            vfloat32m1_t initial =
                __riscv_vfmv_v_f_f32m1(0.0f, 1);

            vfloat32m1_t reduced =
                __riscv_vfredusum_vs_f32m1_f32m1(
                    product, initial, vl);

            sum += __riscv_vfmv_f_s_f32m1_f32(reduced);
            k += vl;
        }

        y[i] = sum;
    }

    ROI_END();

    /* Independent scalar reference for correctness checking. */
    double max_error = 0.0;
    double checksum = 0.0;

    for (int i = 0; i < N; i++) {
        float reference = 0.0f;

        for (uint32_t k = row_ptr[i]; k < row_ptr[i + 1]; k++)
            reference += values[k] * x[columns[k]];

        double error = fabs((double)y[i] - reference);
        if (error > max_error)
            max_error = error;

        checksum += y[i];
    }

    printf("N=%d density=%d%% sparsity=%d%% nnz=%d\n",
           N, density, 100 - density, nnz);
    printf("Checksum=%.9f max_error=%.9g\n",
           checksum, max_error);

    if (max_error > 1e-4) {
        fprintf(stderr, "VALIDATION FAILED\n");
        return 2;
    }

    printf("VALIDATION PASSED\n");
    return 0;
}
