/*
 * Shared benchmark harness: every kernel sees the same matrix, the same dense
 * operand, the same warm-up policy and the same ROI markers.
 *
 *   bench kernel=<name> [input options] [warmup=1] [cold=0] [tol=1e-4] [verbose=0]
 *
 * Input options:
 *   gen=midterm  n=256 density=20              midterm O(n^2) pattern (integer %)
 *   gen=uniform  rows= cols= (or n=) density=<%> | nnz_per_row=<avg>  [seed=1]
 *   gen=powerlaw rows= cols= (or n=) density=<%> | nnz_per_row=<avg>  [alpha=1.8] [seed=1]
 *   gen=file     path=<matrix.csr>             binary CSR from scripts/mtx2bin.py
 *   k=8                                        dense columns for SpMM kernels
 *
 * Kernels: SpMV  scalar rvv_orig rvv_opt_m1 rvv_opt_m2 rvv_opt_m4
 *                rvv_acc2_m1 rvv_acc2_m2
 *          GEMV  dense_m1 dense_m2 dense_m4  (densified copy of the same matrix)
 *          SpMM  spmm_scalar spmm_rvv_vk spmm_rvv_colwise
 *
 * Sequence: build input -> (dense kernels: densify) -> `warmup` untimed runs
 * of the same kernel -> (cold=1: evict caches by streaming an 8x-L2 buffer)
 * -> ROI_BEGIN -> one kernel run -> ROI_END -> validate.
 *
 * Validation: per output, |y - ref| <= tol * sum |a * x|, with ref from a
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

typedef enum { K_SPMV, K_GEMV, K_SPMM } kind_t;

typedef struct {
    const char *name;
    kind_t kind;
    spmv_fn spmv;
    gemv_fn gemv;
    spmm_fn spmm;
    int lmul;      /* vector register group size (0 = scalar) */
    int over_k;    /* SpMM: vectorised over the k dense columns */
} kernel_t;

static const kernel_t KERNELS[] = {
    {"scalar",           K_SPMV, spmv_scalar,      NULL,        NULL,             0, 0},
    {"rvv_orig",         K_SPMV, spmv_rvv_orig,    NULL,        NULL,             1, 0},
    {"rvv_opt_m1",       K_SPMV, spmv_rvv_opt_m1,  NULL,        NULL,             1, 0},
    {"rvv_opt_m2",       K_SPMV, spmv_rvv_opt_m2,  NULL,        NULL,             2, 0},
    {"rvv_opt_m4",       K_SPMV, spmv_rvv_opt_m4,  NULL,        NULL,             4, 0},
    {"rvv_acc2_m1",      K_SPMV, spmv_rvv_acc2_m1, NULL,        NULL,             1, 0},
    {"rvv_acc2_m2",      K_SPMV, spmv_rvv_acc2_m2, NULL,        NULL,             2, 0},
    {"dense_m1",         K_GEMV, NULL,             gemv_rvv_m1, NULL,             1, 0},
    {"dense_m2",         K_GEMV, NULL,             gemv_rvv_m2, NULL,             2, 0},
    {"dense_m4",         K_GEMV, NULL,             gemv_rvv_m4, NULL,             4, 0},
    {"spmm_scalar",      K_SPMM, NULL,             NULL,        spmm_scalar,      0, 0},
    {"spmm_rvv_vk",      K_SPMM, NULL,             NULL,        spmm_rvv_vk,      2, 1},
    {"spmm_rvv_colwise", K_SPMM, NULL,             NULL,        spmm_rvv_colwise, 1, 0},
};
#define NUM_KERNELS (sizeof(KERNELS) / sizeof(KERNELS[0]))

typedef struct {
    const char *kernel, *gen, *path;
    uint32_t rows, cols, k, warmup, cold, verbose;
    double density_pct, nnz_per_row, alpha, tol;
    uint64_t seed;
} opts_t;

static void usage(const char *msg)
{
    fprintf(stderr, "error: %s (see the header of src/harness/bench.c)\nkernels:", msg);
    for (size_t i = 0; i < NUM_KERNELS; i++)
        fprintf(stderr, " %s", KERNELS[i].name);
    fprintf(stderr, "\n");
    exit(1);
}

static opts_t parse(int argc, char **argv)
{
    opts_t o = {NULL, "midterm", NULL, 256, 256, 8, 1, 0, 0, 20.0, 0.0, 1.8, 1e-4, 1};
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (!eq)
            usage("arguments must be key=value");
        *eq = '\0';
        const char *key = argv[i], *v = eq + 1;
        if (!strcmp(key, "kernel")) o.kernel = v;
        else if (!strcmp(key, "gen")) o.gen = v;
        else if (!strcmp(key, "path")) o.path = v;
        else if (!strcmp(key, "n")) o.rows = o.cols = (uint32_t)strtoul(v, NULL, 10);
        else if (!strcmp(key, "rows")) o.rows = (uint32_t)strtoul(v, NULL, 10);
        else if (!strcmp(key, "cols")) o.cols = (uint32_t)strtoul(v, NULL, 10);
        else if (!strcmp(key, "density")) o.density_pct = strtod(v, NULL);
        else if (!strcmp(key, "nnz_per_row")) o.nnz_per_row = strtod(v, NULL);
        else if (!strcmp(key, "alpha")) o.alpha = strtod(v, NULL);
        else if (!strcmp(key, "seed")) o.seed = strtoull(v, NULL, 10);
        else if (!strcmp(key, "k")) o.k = (uint32_t)strtoul(v, NULL, 10);
        else if (!strcmp(key, "warmup")) o.warmup = (uint32_t)strtoul(v, NULL, 10);
        else if (!strcmp(key, "cold")) o.cold = (uint32_t)strtoul(v, NULL, 10);
        else if (!strcmp(key, "tol")) o.tol = strtod(v, NULL);
        else if (!strcmp(key, "verbose")) o.verbose = (uint32_t)strtoul(v, NULL, 10);
        else usage("unknown key");
    }
    if (!o.kernel)
        usage("kernel= is required");
    if (o.k < 1 || o.k > SPMM_MAX_K)
        usage("k must be 1..64");
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

static void build_input(opts_t *o, csr_t *A)
{
    if (!strcmp(o->gen, "midterm")) {
        uint32_t d = (uint32_t)o->density_pct;
        if (d < 1 || d > 100 || (double)d != o->density_pct || o->rows != o->cols)
            usage("gen=midterm needs n= and an integer density 1..100");
        gen_midterm(A, o->rows, d);
    } else if (!strcmp(o->gen, "uniform") || !strcmp(o->gen, "powerlaw")) {
        if (!o->rows || !o->cols)
            usage("rows and cols must be > 0");
        double density = o->nnz_per_row > 0.0 ? o->nnz_per_row / o->cols : o->density_pct / 100.0;
        if (density <= 0.0 || density > 1.0)
            usage("density must be in (0, 100]");
        gen_random(A, o->rows, o->cols, density,
                   !strcmp(o->gen, "uniform") ? DIST_UNIFORM : DIST_POWERLAW, o->alpha, o->seed);
    } else if (!strcmp(o->gen, "file")) {
        if (!o->path)
            usage("gen=file needs path=");
        load_csrbin(A, o->path);
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
                       const float *x, uint32_t k, float *y)
{
    switch (K->kind) {
    case K_SPMV: K->spmv(A, x, y); break;
    case K_GEMV: K->gemv(D, A->rows, A->cols, x, y); break;
    case K_SPMM: K->spmm(A, x, k, y); break;
    }
}

static uint32_t vlmax_e32(int lmul)
{
    return lmul == 1 ? (uint32_t)__riscv_vsetvlmax_e32m1()
         : lmul == 2 ? (uint32_t)__riscv_vsetvlmax_e32m2()
         : (uint32_t)__riscv_vsetvlmax_e32m4();
}

/* Vector chunks (one vector op issue each) the kernel performs with a given
 * VLMAX; with VLMAX_m1 this is the number of per-register micro-ops gem5
 * commits for that op at LMUL > 1. */
static uint64_t count_chunks(const kernel_t *K, const csr_t *A, uint32_t k, uint32_t vlmax)
{
    uint64_t c = 0;
    if (K->kind == K_SPMM && K->over_k)
        return (uint64_t)A->nnz * ((k + vlmax - 1) / vlmax);
    for (uint32_t i = 0; i < A->rows; i++) {
        uint32_t len = K->kind == K_GEMV ? A->cols : A->row_ptr[i + 1] - A->row_ptr[i];
        c += (len + vlmax - 1) / vlmax;
    }
    return K->kind == K_SPMM ? c * k : c;
}

static void print_matrix_info(const csr_t *A)
{
    uint32_t mn = UINT32_MAX, mx = 0, empty = 0;
    double mean = (double)A->nnz / A->rows, var = 0.0;
    for (uint32_t i = 0; i < A->rows; i++) {
        uint32_t len = A->row_ptr[i + 1] - A->row_ptr[i];
        mn = len < mn ? len : mn;
        mx = len > mx ? len : mx;
        empty += len == 0;
        var += ((double)len - mean) * ((double)len - mean);
    }
    double sd = sqrt(var / A->rows);
    printf("MATRIX rows=%u cols=%u nnz=%u density_actual=%.6g row_len_min=%u "
           "row_len_max=%u row_len_mean=%.4f row_len_cv=%.4f empty_rows=%u "
           "csr_bytes=%llu x_bytes=%llu\n",
           A->rows, A->cols, A->nnz, (double)A->nnz / ((double)A->rows * A->cols),
           mn, mx, mean, mean > 0 ? sd / mean : 0.0, empty,
           (unsigned long long)((uint64_t)A->nnz * 8 + (uint64_t)(A->rows + 1) * 4),
           (unsigned long long)A->cols * 4);
}

static void print_vector_info(const kernel_t *K, const csr_t *A, uint32_t k)
{
    if (!K->lmul)
        return;
    uint32_t vlmax = vlmax_e32(K->lmul);
    uint64_t chunks = count_chunks(K, A, k, vlmax);
    uint64_t reg_chunks = count_chunks(K, A, k, vlmax_e32(1));
    uint64_t useful = (uint64_t)A->nnz * (K->kind == K_SPMM ? k : 1);
    uint64_t active = K->kind == K_GEMV ? (uint64_t)A->rows * A->cols : useful;
    printf("VECTOR lmul=%d vlmax=%u chunks=%llu reg_chunks=%llu active_elems=%llu "
           "useful_elems=%llu lane_util=%.6f\n",
           K->lmul, vlmax, (unsigned long long)chunks, (unsigned long long)reg_chunks,
           (unsigned long long)active, (unsigned long long)useful,
           chunks ? (double)active / ((double)chunks * vlmax) : 0.0);
}

int main(int argc, char **argv)
{
    opts_t o = parse(argc, argv);
    const kernel_t *K = find_kernel(o.kernel);

    csr_t A;
    memset(&A, 0, sizeof(A));
    build_input(&o, &A);
    const uint32_t k = K->kind == K_SPMM ? o.k : 1;
    const uint64_t ny = (uint64_t)A.rows * k;
    float *x = K->kind == K_SPMM ? make_X(A.cols, k) : make_x(A.cols);
    float *y = xalloc(ny * sizeof(float));
    float *D = K->kind == K_GEMV ? csr_to_dense(&A) : NULL;

    printf("CONFIG kernel=%s gen=%s rows=%u cols=%u density_pct=%g nnz_per_row=%g "
           "alpha=%g seed=%llu path=%s k=%u warmup=%u cold=%u tol=%g\n",
           K->name, o.gen, A.rows, A.cols, o.density_pct, o.nnz_per_row, o.alpha,
           (unsigned long long)o.seed, o.path ? o.path : "-", k, o.warmup, o.cold, o.tol);
    print_matrix_info(&A);
    print_vector_info(K, &A, k);

    for (uint32_t w = 0; w < o.warmup; w++)
        run_kernel(K, &A, D, x, k, y);
    memset(y, 0, ny * sizeof(float));
    if (o.cold)
        evict_caches();

    ROI_BEGIN();
    run_kernel(K, &A, D, x, k, y);
    ROI_END();

    double *ref = xalloc(ny * sizeof(double));
    double *abs_sum = xalloc(ny * sizeof(double));
    if (K->kind == K_SPMM)
        reference_spmm(&A, x, k, ref, abs_sum);
    else
        reference_spmv(&A, x, ref, abs_sum);

    double max_rel = 0.0, checksum = 0.0;
    uint64_t bad = 0;
    for (uint64_t i = 0; i < ny; i++) {
        double err = fabs((double)y[i] - ref[i]);
        double rel = abs_sum[i] > 0.0 ? err / abs_sum[i] : (err == 0.0 ? 0.0 : INFINITY);
        if (rel > max_rel)
            max_rel = rel;
        if (rel > o.tol && o.verbose && bad < 20)
            printf("BADROW out=%llu row=%llu len=%u y=%.9g ref=%.9g rel=%.3e\n",
                   (unsigned long long)i, (unsigned long long)(i / k),
                   A.row_ptr[i / k + 1] - A.row_ptr[i / k], (double)y[i], ref[i], rel);
        bad += rel > o.tol;
        checksum += y[i];
    }
    int ok = bad == 0;
    printf("RESULT kernel=%s nnz=%u checksum=%.9f max_rel_err=%.6e bad_rows=%llu validation=%s\n",
           K->name, A.nnz, checksum, max_rel, (unsigned long long)bad, ok ? "PASSED" : "FAILED");
    return ok ? 0 : 2;
}
