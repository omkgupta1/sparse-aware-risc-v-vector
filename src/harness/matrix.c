#include "harness/matrix.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/rng.h"

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

/* Appends the nonzeros of one row: each column is nonzero with probability p. */
static void fill_row(csr_t *A, uint64_t *cap, uint32_t cols, double p, rng_t *r)
{
    if (p <= 0.0)
        return;
    const double log_q = p < 1.0 ? log1p(-p) : 0.0;
    for (uint64_t j = 0;;) {
        if (p < 1.0) {
            double skip = floor(log(rng_unit(r)) / log_q);  /* geometric skip */
            if (skip >= (double)(cols - j))
                break;
            j += (uint64_t)skip;
        }
        if (j >= cols)
            break;
        if (A->nnz == *cap) {
            uint64_t ncap = *cap * 2 + 1024;
            uint32_t *nc = xalloc(ncap * sizeof(uint32_t));
            float *nv = xalloc(ncap * sizeof(float));
            memcpy(nc, A->col_idx, (size_t)A->nnz * sizeof(uint32_t));
            memcpy(nv, A->values, (size_t)A->nnz * sizeof(float));
            free(A->col_idx);
            free(A->values);
            A->col_idx = nc;
            A->values = nv;
            *cap = ncap;
        }
        A->col_idx[A->nnz] = (uint32_t)j;
        A->values[A->nnz] = (float)(rng_next(r) % 11 + 1) / 11.0f;
        A->nnz++;
        j++;
    }
}

void gen_random(csr_t *A, uint32_t rows, uint32_t cols, double density,
                dist_t dist, double alpha, uint64_t seed)
{
    rng_t r = {seed};
    const double target = density * (double)rows * (double)cols;
    uint64_t cap = (uint64_t)(target * 1.1) + 1024;

    A->rows = rows;
    A->cols = cols;
    A->nnz = 0;
    A->row_ptr = xalloc((uint64_t)(rows + 1) * sizeof(uint32_t));
    A->col_idx = xalloc(cap * sizeof(uint32_t));
    A->values = xalloc(cap * sizeof(float));

    double *len = NULL;
    if (dist == DIST_POWERLAW) {
        /* Pareto(x_m = 1, shape alpha) weights, scaled (with the cap at cols
         * taken into account) so the expected total is `target`. */
        len = xalloc((uint64_t)rows * sizeof(double));
        for (uint32_t i = 0; i < rows; i++)
            len[i] = pow(rng_unit(&r), -1.0 / alpha);
        double scale = target / rows;
        for (int it = 0; it < 30; it++) {
            double sum = 0.0;
            for (uint32_t i = 0; i < rows; i++)
                sum += fmin((double)cols, len[i] * scale);
            if (sum <= 0.0)
                break;
            scale *= target / sum;
        }
        for (uint32_t i = 0; i < rows; i++)
            len[i] = fmin((double)cols, len[i] * scale);
    }

    for (uint32_t i = 0; i < rows; i++) {
        double p = dist == DIST_POWERLAW ? len[i] / cols : density;
        fill_row(A, &cap, cols, p > 1.0 ? 1.0 : p, &r);
        A->row_ptr[i + 1] = A->nnz;
    }
    free(len);
}

static void read_exact(FILE *f, void *dst, size_t bytes, const char *path)
{
    if (fread(dst, 1, bytes, f) != bytes) {
        fprintf(stderr, "%s: truncated file\n", path);
        exit(3);
    }
}

void load_csrbin(csr_t *A, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(3);
    }
    char magic[4];
    uint32_t hdr[4];
    read_exact(f, magic, 4, path);
    read_exact(f, hdr, sizeof(hdr), path);
    if (memcmp(magic, "CSRB", 4) != 0 || hdr[0] != 1) {
        fprintf(stderr, "%s: not a CSRB v1 file\n", path);
        exit(3);
    }
    A->rows = hdr[1];
    A->cols = hdr[2];
    A->nnz = hdr[3];
    A->row_ptr = xalloc((uint64_t)(A->rows + 1) * sizeof(uint32_t));
    A->col_idx = xalloc((uint64_t)A->nnz * sizeof(uint32_t));
    A->values = xalloc((uint64_t)A->nnz * sizeof(float));
    read_exact(f, A->row_ptr, (size_t)(A->rows + 1) * sizeof(uint32_t), path);
    read_exact(f, A->col_idx, (size_t)A->nnz * sizeof(uint32_t), path);
    read_exact(f, A->values, (size_t)A->nnz * sizeof(float), path);
    fclose(f);

    if (A->row_ptr[0] != 0 || A->row_ptr[A->rows] != A->nnz) {
        fprintf(stderr, "%s: inconsistent row_ptr\n", path);
        exit(3);
    }
    for (uint32_t i = 0; i < A->rows; i++)
        for (uint32_t k = A->row_ptr[i]; k < A->row_ptr[i + 1]; k++)
            if (A->col_idx[k] >= A->cols ||
                (k > A->row_ptr[i] && A->col_idx[k] <= A->col_idx[k - 1])) {
                fprintf(stderr, "%s: row %u has unsorted/out-of-range columns\n", path, i);
                exit(3);
            }
}

float *make_X(uint32_t cols, uint32_t k)
{
    uint64_t n = (uint64_t)cols * k;
    float *X = xalloc(n * sizeof(float));
    for (uint64_t i = 0; i < n; i++)
        X[i] = (float)(i % 17 + 1) / 17.0f;
    return X;
}
