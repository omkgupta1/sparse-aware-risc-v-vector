/*
 * Sparse-aware execution on DENSE-format input (Phase 4A).
 *
 * gemv_skip_m{1,2}:  zero/inactive-element skipping with masks. Each chunk of
 *                    the row is loaded and vmfne builds a nonzero mask; vcpop
 *                    counts it. All-zero chunks skip the x load and the FMA
 *                    entirely; otherwise x is loaded and the FMA executed
 *                    under the mask, so zero lanes do no arithmetic
 *                    (tail- and mask-undisturbed accumulator).
 *
 * gemv_compress_m1:  compaction. vcompress packs the nonzero values of each
 *                    chunk and their column indices; packed elements are
 *                    appended to a small staging buffer in memory (vse32 at
 *                    the fill position) that is consumed one full vector at a
 *                    time (or at the end of the row) as one LMUL=1 gather of x
 *                    plus one full-width vfmacc. Arithmetic therefore runs on
 *                    fully packed vectors of useful work.
 *
 * gemv_compress_slide_m1: same compaction, but the packing buffer lives in
 *                    vector registers (tail-undisturbed vslideup/vslidedown).
 *                    Correct on QEMU (also with agnostic = all-ones) and on
 *                    gem5 MinorCPU, but faults on gem5 25.1 O3 (Issue 3 in
 *                    docs/gem5_rvv_issues.md). Kept only as the reproducer;
 *                    not used for results.
 *
 * Both still read every element of the dense matrix: masks only remove work
 * after the load, not memory traffic.
 */
#include <riscv_vector.h>

#include "kernels/kernels.h"
#include "kernels/rvv_reduce.h"

#define DEFINE_GEMV_SKIP(LMUL, MB)                                             \
void gemv_skip_##LMUL(const float *D, uint32_t rows, uint32_t cols,            \
                      const float *x, float *y)                                \
{                                                                              \
    const size_t vlmax = __riscv_vsetvlmax_e32##LMUL();                        \
                                                                               \
    for (uint32_t i = 0; i < rows; i++) {                                      \
        const float *row = D + (uint64_t)i * cols;                             \
        vfloat32##LMUL##_t acc = __riscv_vfmv_v_f_f32##LMUL(0.0f, vlmax);      \
                                                                               \
        for (uint32_t j = 0; j < cols; ) {                                     \
            size_t vl = __riscv_vsetvl_e32##LMUL(cols - j);                    \
            vfloat32##LMUL##_t va = __riscv_vle32_v_f32##LMUL(&row[j], vl);    \
            vbool##MB##_t nz = __riscv_vmfne_vf_f32##LMUL##_b##MB(va, 0.0f, vl); \
            if (__riscv_vcpop_m_b##MB(nz, vl) != 0) {                          \
                vfloat32##LMUL##_t vx =                                        \
                    __riscv_vle32_v_f32##LMUL##_m(nz, &x[j], vl);              \
                acc = __riscv_vfmacc_vv_f32##LMUL##_tumu(nz, acc, va, vx, vl); \
            }                                                                  \
            j += vl;                                                           \
        }                                                                      \
                                                                               \
        y[i] = rvv_sum_##LMUL(acc);                                            \
    }                                                                          \
}

DEFINE_GEMV_SKIP(m1, 32)
DEFINE_GEMV_SKIP(m2, 16)

void gemv_compress_slide_m1(const float *D, uint32_t rows, uint32_t cols,
                            const float *x, float *y)
{
    const size_t vlmax = __riscv_vsetvlmax_e32m1();

    for (uint32_t i = 0; i < rows; i++) {
        const float *row = D + (uint64_t)i * cols;
        vfloat32m1_t acc = __riscv_vfmv_v_f_f32m1(0.0f, vlmax);
        vfloat32m1_t bufv = acc;                       /* packed values      */
        vuint32m1_t bufi = __riscv_vmv_v_x_u32m1(0, vlmax); /* their columns */
        size_t cnt = 0;                                /* packed elements    */

        for (uint32_t j = 0; j < cols; ) {
            size_t vl = __riscv_vsetvl_e32m1(cols - j);
            vfloat32m1_t va = __riscv_vle32_v_f32m1(&row[j], vl);
            vbool32_t nz = __riscv_vmfne_vf_f32m1_b32(va, 0.0f, vl);
            size_t n = __riscv_vcpop_m_b32(nz, vl);
            if (n != 0) {
                vfloat32m1_t vals = __riscv_vcompress_vm_f32m1(va, nz, vl);
                vuint32m1_t cidx = __riscv_vadd_vx_u32m1(__riscv_vid_v_u32m1(vl), j, vl);
                cidx = __riscv_vcompress_vm_u32m1(cidx, nz, vl);
                size_t space = vlmax - cnt;
                if (n < space) {
                    bufv = __riscv_vslideup_vx_f32m1_tu(bufv, vals, cnt, cnt + n);
                    bufi = __riscv_vslideup_vx_u32m1_tu(bufi, cidx, cnt, cnt + n);
                    cnt += n;
                } else {
                    bufv = __riscv_vslideup_vx_f32m1_tu(bufv, vals, cnt, vlmax);
                    bufi = __riscv_vslideup_vx_u32m1_tu(bufi, cidx, cnt, vlmax);
                    /* buffer full: one gather + one full-width FMA */
                    vuint32m1_t off = __riscv_vsll_vx_u32m1(bufi, 2, vlmax);
                    vfloat32m1_t vx = __riscv_vluxei32_v_f32m1(x, off, vlmax);
                    acc = __riscv_vfmacc_vv_f32m1_tu(acc, bufv, vx, vlmax);
                    cnt = n - space;
                    if (cnt != 0) {
                        bufv = __riscv_vslidedown_vx_f32m1(vals, space, cnt);
                        bufi = __riscv_vslidedown_vx_u32m1(cidx, space, cnt);
                    }
                }
            }
            j += vl;
        }
        if (cnt != 0) {
            vuint32m1_t off = __riscv_vsll_vx_u32m1(bufi, 2, cnt);
            vfloat32m1_t vx = __riscv_vluxei32_v_f32m1(x, off, cnt);
            acc = __riscv_vfmacc_vv_f32m1_tu(acc, bufv, vx, cnt);
        }
        y[i] = rvv_sum_m1(acc);
    }
}

/* Staging capacity: two vectors of the widest supported e32 register (VLEN 2048). */
#define STAGE_MAX 128

void gemv_compress_m1(const float *D, uint32_t rows, uint32_t cols,
                      const float *x, float *y)
{
    const size_t vlmax = __riscv_vsetvlmax_e32m1();
    float sval[STAGE_MAX];
    uint32_t sidx[STAGE_MAX];

    for (uint32_t i = 0; i < rows; i++) {
        const float *row = D + (uint64_t)i * cols;
        vfloat32m1_t acc = __riscv_vfmv_v_f_f32m1(0.0f, vlmax);
        size_t cnt = 0;                                  /* staged elements */

        for (uint32_t j = 0; j < cols; ) {
            size_t vl = __riscv_vsetvl_e32m1(cols - j);
            vfloat32m1_t va = __riscv_vle32_v_f32m1(&row[j], vl);
            vbool32_t nz = __riscv_vmfne_vf_f32m1_b32(va, 0.0f, vl);
            size_t n = __riscv_vcpop_m_b32(nz, vl);
            if (n != 0) {
                vfloat32m1_t vals = __riscv_vcompress_vm_f32m1(va, nz, vl);
                vuint32m1_t cidx = __riscv_vadd_vx_u32m1(__riscv_vid_v_u32m1(vl), j, vl);
                cidx = __riscv_vcompress_vm_u32m1(cidx, nz, vl);
                __riscv_vse32_v_f32m1(&sval[cnt], vals, n);
                __riscv_vse32_v_u32m1(&sidx[cnt], cidx, n);
                cnt += n;
                if (cnt >= vlmax) {
                    /* one full vector of useful work: gather + full-width FMA */
                    vfloat32m1_t bv = __riscv_vle32_v_f32m1(sval, vlmax);
                    vuint32m1_t off = __riscv_vsll_vx_u32m1(
                        __riscv_vle32_v_u32m1(sidx, vlmax), 2, vlmax);
                    vfloat32m1_t vx = __riscv_vluxei32_v_f32m1(x, off, vlmax);
                    acc = __riscv_vfmacc_vv_f32m1_tu(acc, bv, vx, vlmax);
                    cnt -= vlmax;
                    if (cnt != 0) {  /* move the leftovers to the front */
                        __riscv_vse32_v_f32m1(sval, __riscv_vle32_v_f32m1(&sval[vlmax], cnt), cnt);
                        __riscv_vse32_v_u32m1(sidx, __riscv_vle32_v_u32m1(&sidx[vlmax], cnt), cnt);
                    }
                }
            }
            j += vl;
        }
        if (cnt != 0) {
            vfloat32m1_t bv = __riscv_vle32_v_f32m1(sval, cnt);
            vuint32m1_t off = __riscv_vsll_vx_u32m1(__riscv_vle32_v_u32m1(sidx, cnt), 2, cnt);
            vfloat32m1_t vx = __riscv_vluxei32_v_f32m1(x, off, cnt);
            acc = __riscv_vfmacc_vv_f32m1_tu(acc, bv, vx, cnt);
        }
        y[i] = rvv_sum_m1(acc);
    }
}
