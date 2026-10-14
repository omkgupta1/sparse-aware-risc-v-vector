/*
 * gem5 RVV reproducer: indexed gathers (vluxei32.v) into LMUL>1 register
 * groups occasionally return wrong data on the O3 CPU when many gather
 * element micro-ops are in flight (timing dependent, deterministic for a
 * given binary/config).
 *
 *   riscv64-linux-gnu-gcc -O2 -static -march=rv64gcv -mabi=lp64d \
 *       issue2_vluxei_lmul4.c -lm -o issue2
 *   gem5.opt se_riscv_rvv.py --binary issue2 --args MODE DENSITY_PERCENT
 *
 * CSR sparse matrix-vector product on a uniform-random 1024x1024 matrix
 * (fixed seed). MODE selects the inner loop:
 *   m2x2  two independent e32m2 gathers per iteration (2 x 16 elements in
 *         flight at VLEN=256), each accumulated with vfmacc (tail undisturbed)
 *   m4    one e32m4 gather per chunk (32 elements at VLEN=256)
 * Accumulators are folded to LMUL=1 with vfadd before the reduction (avoids
 * the separate LMUL>1 vfredusum issue). Every row is compared with a scalar
 * double-precision reference.
 *
 * Expected (QEMU, gem5 MinorCPU): "bad rows: 0" / PASS, exit 0.
 */
#include <math.h>
#include <riscv_vector.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N 1024

static uint32_t row_ptr[N + 1], col[N * N];
static float val[N * N], x[N], y[N];

static inline float sum_m2(vfloat32m2_t acc)
{
    size_t v1 = __riscv_vsetvlmax_e32m1();
    vfloat32m1_t s = __riscv_vfadd_vv_f32m1(__riscv_vget_v_f32m2_f32m1(acc, 0),
                                            __riscv_vget_v_f32m2_f32m1(acc, 1), v1);
    return __riscv_vfmv_f_s_f32m1_f32(__riscv_vfredusum_vs_f32m1_f32m1(
        s, __riscv_vfmv_v_f_f32m1(0.0f, 1), v1));
}

static inline float sum_m4(vfloat32m4_t acc)
{
    size_t v1 = __riscv_vsetvlmax_e32m1();
    vfloat32m1_t a = __riscv_vfadd_vv_f32m1(__riscv_vget_v_f32m4_f32m1(acc, 0),
                                            __riscv_vget_v_f32m4_f32m1(acc, 1), v1);
    vfloat32m1_t b = __riscv_vfadd_vv_f32m1(__riscv_vget_v_f32m4_f32m1(acc, 2),
                                            __riscv_vget_v_f32m4_f32m1(acc, 3), v1);
    return __riscv_vfmv_f_s_f32m1_f32(__riscv_vfredusum_vs_f32m1_f32m1(
        __riscv_vfadd_vv_f32m1(a, b, v1), __riscv_vfmv_v_f_f32m1(0.0f, 1), v1));
}

static void spmv_m2x2(void)
{
    const size_t vlmax = __riscv_vsetvlmax_e32m2();
    for (uint32_t i = 0; i < N; i++) {
        uint32_t k = row_ptr[i], end = row_ptr[i + 1];
        vfloat32m2_t acc0 = __riscv_vfmv_v_f_f32m2(0.0f, vlmax);
        vfloat32m2_t acc1 = __riscv_vfmv_v_f_f32m2(0.0f, vlmax);
        while (end - k >= 2 * vlmax) {
            vuint32m2_t i0 = __riscv_vsll_vx_u32m2(__riscv_vle32_v_u32m2(&col[k], vlmax), 2, vlmax);
            vuint32m2_t i1 = __riscv_vsll_vx_u32m2(__riscv_vle32_v_u32m2(&col[k + vlmax], vlmax), 2, vlmax);
            vfloat32m2_t a0 = __riscv_vle32_v_f32m2(&val[k], vlmax);
            vfloat32m2_t a1 = __riscv_vle32_v_f32m2(&val[k + vlmax], vlmax);
            acc0 = __riscv_vfmacc_vv_f32m2_tu(acc0, a0, __riscv_vluxei32_v_f32m2(x, i0, vlmax), vlmax);
            acc1 = __riscv_vfmacc_vv_f32m2_tu(acc1, a1, __riscv_vluxei32_v_f32m2(x, i1, vlmax), vlmax);
            k += 2 * vlmax;
        }
        while (k < end) {
            size_t vl = __riscv_vsetvl_e32m2(end - k);
            vuint32m2_t vi = __riscv_vsll_vx_u32m2(__riscv_vle32_v_u32m2(&col[k], vl), 2, vl);
            acc0 = __riscv_vfmacc_vv_f32m2_tu(acc0, __riscv_vle32_v_f32m2(&val[k], vl),
                                              __riscv_vluxei32_v_f32m2(x, vi, vl), vl);
            k += vl;
        }
        y[i] = sum_m2(__riscv_vfadd_vv_f32m2(acc0, acc1, vlmax));
    }
}

static void spmv_m4(void)
{
    const size_t vlmax = __riscv_vsetvlmax_e32m4();
    for (uint32_t i = 0; i < N; i++) {
        vfloat32m4_t acc = __riscv_vfmv_v_f_f32m4(0.0f, vlmax);
        for (uint32_t k = row_ptr[i]; k < row_ptr[i + 1]; ) {
            size_t vl = __riscv_vsetvl_e32m4(row_ptr[i + 1] - k);
            vuint32m4_t vi = __riscv_vsll_vx_u32m4(__riscv_vle32_v_u32m4(&col[k], vl), 2, vl);
            acc = __riscv_vfmacc_vv_f32m4_tu(acc, __riscv_vle32_v_f32m4(&val[k], vl),
                                             __riscv_vluxei32_v_f32m4(x, vi, vl), vl);
            k += vl;
        }
        y[i] = sum_m4(acc);
    }
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "m2x2";
    const double density = (argc > 2 ? atof(argv[2]) : 20.0) / 100.0;
    uint64_t s = 1;
    uint32_t nnz = 0;
    for (uint32_t i = 0; i < N; i++) {
        for (uint32_t j = 0; j < N; j++) {
            s ^= s << 13; s ^= s >> 7; s ^= s << 17;
            if ((double)(s % 1000000) < density * 1e6) {
                col[nnz] = j;
                val[nnz++] = (float)(s % 11 + 1) / 11.0f;
            }
        }
        row_ptr[i + 1] = nnz;
    }
    for (uint32_t j = 0; j < N; j++)
        x[j] = (float)(j % 17 + 1) / 17.0f;

    for (int rep = 0; rep < 2; rep++) {
        if (!strcmp(mode, "m4"))
            spmv_m4();
        else
            spmv_m2x2();
    }

    int bad = 0;
    for (uint32_t i = 0; i < N; i++) {
        double ref = 0.0, mag = 0.0;
        for (uint32_t k = row_ptr[i]; k < row_ptr[i + 1]; k++) {
            ref += (double)val[k] * x[col[k]];
            mag += fabs((double)val[k] * x[col[k]]);
        }
        if (fabs(y[i] - ref) > 1e-4 * mag) {
            if (bad < 5)
                printf("row %u (len %u): got %.7g expected %.7g\n", i,
                       row_ptr[i + 1] - row_ptr[i], y[i], ref);
            bad++;
        }
    }
    printf("mode %s density %.0f%% nnz %u, bad rows: %d of %d\n%s\n", mode, density * 100,
           nnz, bad, N, bad ? "FAIL" : "PASS");
    return bad != 0;
}
