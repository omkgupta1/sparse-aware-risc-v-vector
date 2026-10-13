/*
 * Shared benchmark harness: every kernel sees the same matrix, the same x,
 * the same warm-up policy and the same ROI markers.
 *
 *   bench kernel=<name> [gen=midterm] [n=256] [density=20]
 *         [warmup=1] [cold=0] [tol=1e-4] [verbose=0]
 *
 * Kernels: scalar rvv_orig rvv_opt_m1 rvv_opt_m2 rvv_opt_m4
 *          dense_m1 dense_m2 dense_m4
 *
 * Sequence: build input -> (dense kernels: densify) -> `warmup` untimed runs
 * of the same kernel -> (cold=1: evict caches by streaming an 8x-L2 buffer)
 * -> ROI_BEGIN -> one kernel run -> ROI_END -> validate.
 *
 * Validation: per row, |y_i - ref_i| <= tol * sum_k |a_ik x_k|, where ref is a
 * double-precision scalar reference built without vector instructions.
 * Exit status 2 on validation failure.
 */
#include <math.h>
#include <riscv_vector.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/roi.h"
#include "harness/matrix.h"
#include "kernels/kernels.h"

typedef struct {
    const char *name;
    spmv_fn spmv;   /* CSR kernel, or NULL */
    gemv_fn gemv;   /* dense kernel, or NULL */
    int lmul;       /* vector register group size (0 = scalar) */
} kernel_t;

static const kernel_t KERNELS[] = {
    {"scalar",     spmv_scalar,     NULL,        0},
    {"rvv_orig",   spmv_rvv_orig,   NULL,        1},
    {"rvv_opt_m1", spmv_rvv_opt_m1, NULL,        1},
    {"rvv_opt_m2", spmv_rvv_opt_m2, NULL,        2},
    {"rvv_opt_m4", spmv_rvv_opt_m4, NULL,        4},
    {"dense_m1",   NULL,            gemv_rvv_m1, 1},
    {"dense_m2",   NULL,            gemv_rvv_m2, 2},
    {"dense_m4",   NULL,            gemv_rvv_m4, 4},
};
#define NUM_KERNELS (sizeof(KERNELS) / sizeof(KERNELS[0]))

typedef struct {
    const char *kernel, *gen;
    uint32_t n, density, warmup, cold, verbose;
    double tol;
} opts_t;

static void usage(const char *msg)
{
    fprintf(stderr, "error: %s\nusage: bench kernel=<name> [gen=midterm] [n=256] "
            "[density=20] [warmup=1] [cold=0] [tol=1e-4] [verbose=0]\nkernels:", msg);
    for (size_t i = 0; i < NUM_KERNELS; i++)
        fprintf(stderr, " %s", KERNELS[i].name);
    fprintf(stderr, "\n");
    exit(1);
}

static opts_t parse(int argc, char **argv)
{
    opts_t o = {NULL, "midterm", 256, 20, 1, 0, 0, 1e-4};
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (!eq)
            usage("arguments must be key=value");
        *eq = '\0';
        const char *k = argv[i], *v = eq + 1;
        if (!strcmp(k, "kernel")) o.kernel = v;
        else if (!strcmp(k, "gen")) o.gen = v;
        else if (!strcmp(k, "n")) o.n = (uint32_t)strtoul(v, NULL, 10);
        else if (!strcmp(k, "density")) o.density = (uint32_t)strtoul(v, NULL, 10);
        else if (!strcmp(k, "warmup")) o.warmup = (uint32_t)strtoul(v, NULL, 10);
        else if (!strcmp(k, "cold")) o.cold = (uint32_t)strtoul(v, NULL, 10);
        else if (!strcmp(k, "tol")) o.tol = strtod(v, NULL);
        else if (!strcmp(k, "verbose")) o.verbose = (uint32_t)strtoul(v, NULL, 10);
        else usage("unknown key");
    }
    if (!o.kernel)
        usage("kernel= is required");
    return o;
}

static const kernel_t *find_kernel(const char *name)
{
    for (size_t i = 0; i < NUM_KERNELS; i++)
        if (!strcmp(KERNELS[i].name, name))
            return &KERNELS[i];
    usage("unknown kernel");
    return NULL;
}

static void build_input(const opts_t *o, csr_t *A)
{
    if (!strcmp(o->gen, "midterm")) {
        if (o->density < 1 || o->density > 100)
            usage("density must be 1..100");
        gen_midterm(A, o->n, o->density);
    } else {
        usage("unknown gen");
    }
}

/* Streams a buffer 8x the L2 size so the kernel's data is evicted. */
static void evict_caches(void)
{
    static const uint64_t bytes = 8ull * 512 * 1024;
    volatile const uint8_t *buf = xalloc(bytes);
    uint32_t sink = 0;
    for (uint64_t i = 0; i < bytes; i += 64)
        sink += buf[i];
    if (sink == 12345u)
        puts("");  /* keeps the loop live */
}

static void run_kernel(const kernel_t *K, const csr_t *A, const float *D,
                       const float *x, float *y)
{
    if (K->spmv)
        K->spmv(A, x, y);
    else
        K->gemv(D, A->rows, A->cols, x, y);
}

static uint64_t chunks_for(const csr_t *A, uint32_t vlmax, int dense)
{
    uint64_t c = 0;
    for (uint32_t i = 0; i < A->rows; i++) {
        uint32_t len = dense ? A->cols : A->row_ptr[i + 1] - A->row_ptr[i];
        c += (len + vlmax - 1) / vlmax;
    }
    return c;
}

static void print_matrix_info(const csr_t *A, const kernel_t *K)
{
    uint32_t mn = UINT32_MAX, mx = 0, empty = 0;
    for (uint32_t i = 0; i < A->rows; i++) {
        uint32_t len = A->row_ptr[i + 1] - A->row_ptr[i];
        mn = len < mn ? len : mn;
        mx = len > mx ? len : mx;
        empty += len == 0;
    }
    printf("MATRIX rows=%u cols=%u nnz=%u density_actual=%.6f row_len_min=%u "
           "row_len_max=%u row_len_mean=%.4f empty_rows=%u\n",
           A->rows, A->cols, A->nnz, (double)A->nnz / ((double)A->rows * A->cols),
           mn, mx, (double)A->nnz / A->rows, empty);

    if (K->lmul) {
        uint32_t vlmax = K->lmul == 1 ? (uint32_t)__riscv_vsetvlmax_e32m1()
                       : K->lmul == 2 ? (uint32_t)__riscv_vsetvlmax_e32m2()
                       : (uint32_t)__riscv_vsetvlmax_e32m4();
        int dense = K->gemv != NULL;
        uint64_t active = dense ? (uint64_t)A->rows * A->cols : A->nnz;
        uint64_t chunks = chunks_for(A, vlmax, dense);
        /* gem5 splits an LMUL>1 op into one micro-op per register that holds
         * active elements: sum over rows of ceil(len / VLMAX_m1). */
        uint64_t reg_chunks = chunks_for(A, (uint32_t)__riscv_vsetvlmax_e32m1(), dense);
        /* Data-path element slots: one chunk issues vlmax slots. */
        printf("VECTOR lmul=%d vlmax=%u chunks=%llu reg_chunks=%llu active_elems=%llu "
               "useful_elems=%u lane_util=%.6f\n",
               K->lmul, vlmax, (unsigned long long)chunks, (unsigned long long)reg_chunks,
               (unsigned long long)active, A->nnz,
               chunks ? (double)active / ((double)chunks * vlmax) : 0.0);
    }
}

int main(int argc, char **argv)
{
    opts_t o = parse(argc, argv);
    const kernel_t *K = find_kernel(o.kernel);

    csr_t A;
    memset(&A, 0, sizeof(A));
    build_input(&o, &A);
    float *x = make_x(A.cols);
    float *y = xalloc((uint64_t)A.rows * sizeof(float));
    float *D = K->gemv ? csr_to_dense(&A) : NULL;

    printf("CONFIG kernel=%s gen=%s n=%u density=%u warmup=%u cold=%u tol=%g\n",
           K->name, o.gen, o.n, o.density, o.warmup, o.cold, o.tol);
    print_matrix_info(&A, K);

    for (uint32_t w = 0; w < o.warmup; w++)
        run_kernel(K, &A, D, x, y);
    memset(y, 0, (size_t)A.rows * sizeof(float));
    if (o.cold)
        evict_caches();

    ROI_BEGIN();
    run_kernel(K, &A, D, x, y);
    ROI_END();

    double *ref = xalloc((uint64_t)A.rows * sizeof(double));
    double *abs_sum = xalloc((uint64_t)A.rows * sizeof(double));
    reference_spmv(&A, x, ref, abs_sum);

    double max_rel = 0.0, checksum = 0.0;
    uint32_t bad_rows = 0;
    for (uint32_t i = 0; i < A.rows; i++) {
        double err = fabs((double)y[i] - ref[i]);
        double rel = abs_sum[i] > 0.0 ? err / abs_sum[i] : (err == 0.0 ? 0.0 : INFINITY);
        if (rel > max_rel)
            max_rel = rel;
        if (rel > o.tol && o.verbose && bad_rows < 20)
            printf("BADROW row=%u len=%u y=%.9g ref=%.9g rel=%.3e\n", i,
                   A.row_ptr[i + 1] - A.row_ptr[i], (double)y[i], ref[i], rel);
        bad_rows += rel > o.tol;
        checksum += y[i];
    }
    int ok = bad_rows == 0;
    printf("RESULT kernel=%s nnz=%u checksum=%.9f max_rel_err=%.6e bad_rows=%u validation=%s\n",
           K->name, A.nnz, checksum, max_rel, bad_rows, ok ? "PASSED" : "FAILED");
    return ok ? 0 : 2;
}
