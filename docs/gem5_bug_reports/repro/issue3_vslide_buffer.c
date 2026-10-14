/*
 * gem5 RVV reproducer: packing elements into a vector register buffer with
 * tail-undisturbed vslideup.vx / vslidedown.vx and then reading the buffer
 * at full width (as gather offsets) faults on the O3 CPU.
 *
 *   riscv64-linux-gnu-gcc -O2 -static -march=rv64gcv -mabi=lp64d \
 *       issue3_vslide_buffer.c -o issue3
 *
 * Dense matrix-vector product that compacts each row's nonzeros (vmfne +
 * vcompress) into a VLMAX-wide buffer (column indices + values), and when
 * the buffer is full gathers x at those indices (vluxei32) and accumulates.
 * Correct indices are always < N, so every gathered address is inside x.
 *
 * Expected (QEMU, also with rvv_ta_all_1s/rvv_ma_all_1s; gem5 MinorCPU):
 *   "PASS" -> exit 0.
 * Observed on gem5 O3: panic "Page table fault when accessing virtual address".
 */
#include <math.h>
#include <riscv_vector.h>
#include <stdint.h>
#include <stdio.h>

#define N 256

static float D[N * N], x[N], y[N];

int main(void)
{
    uint64_t s = 1;
    for (int i = 0; i < N * N; i++) {            /* ~5% nonzeros */
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        D[i] = (s % 100) < 5 ? (float)(s % 11 + 1) / 11.0f : 0.0f;
    }
    for (int j = 0; j < N; j++)
        x[j] = (float)(j % 17 + 1) / 17.0f;

    const size_t vlmax = __riscv_vsetvlmax_e32m1();
    for (int i = 0; i < N; i++) {
        const float *row = &D[i * N];
        vfloat32m1_t acc = __riscv_vfmv_v_f_f32m1(0.0f, vlmax);
        vfloat32m1_t bufv = acc;
        vuint32m1_t bufi = __riscv_vmv_v_x_u32m1(0, vlmax);
        size_t cnt = 0;
        for (uint32_t j = 0; j < N; ) {
            size_t vl = __riscv_vsetvl_e32m1(N - j);
            vfloat32m1_t va = __riscv_vle32_v_f32m1(&row[j], vl);
            vbool32_t nz = __riscv_vmfne_vf_f32m1_b32(va, 0.0f, vl);
            size_t n = __riscv_vcpop_m_b32(nz, vl);
            if (n) {
                vfloat32m1_t vals = __riscv_vcompress_vm_f32m1(va, nz, vl);
                vuint32m1_t cidx = __riscv_vcompress_vm_u32m1(
                    __riscv_vadd_vx_u32m1(__riscv_vid_v_u32m1(vl), j, vl), nz, vl);
                size_t space = vlmax - cnt;
                if (n < space) {
                    bufv = __riscv_vslideup_vx_f32m1_tu(bufv, vals, cnt, cnt + n);
                    bufi = __riscv_vslideup_vx_u32m1_tu(bufi, cidx, cnt, cnt + n);
                    cnt += n;
                } else {
                    bufv = __riscv_vslideup_vx_f32m1_tu(bufv, vals, cnt, vlmax);
                    bufi = __riscv_vslideup_vx_u32m1_tu(bufi, cidx, cnt, vlmax);
                    vfloat32m1_t vx = __riscv_vluxei32_v_f32m1(
                        x, __riscv_vsll_vx_u32m1(bufi, 2, vlmax), vlmax);
                    acc = __riscv_vfmacc_vv_f32m1_tu(acc, bufv, vx, vlmax);
                    cnt = n - space;
                    if (cnt) {
                        bufv = __riscv_vslidedown_vx_f32m1(vals, space, cnt);
                        bufi = __riscv_vslidedown_vx_u32m1(cidx, space, cnt);
                    }
                }
            }
            j += vl;
        }
        if (cnt) {
            vfloat32m1_t vx = __riscv_vluxei32_v_f32m1(
                x, __riscv_vsll_vx_u32m1(bufi, 2, cnt), cnt);
            acc = __riscv_vfmacc_vv_f32m1_tu(acc, bufv, vx, cnt);
        }
        y[i] = __riscv_vfmv_f_s_f32m1_f32(__riscv_vfredusum_vs_f32m1_f32m1(
            acc, __riscv_vfmv_v_f_f32m1(0.0f, 1), vlmax));
    }

    int bad = 0;
    for (int i = 0; i < N; i++) {
        double ref = 0.0, mag = 0.0;
        for (int j = 0; j < N; j++) {
            ref += (double)D[i * N + j] * x[j];
            mag += fabs((double)D[i * N + j] * x[j]);
        }
        bad += fabs(y[i] - ref) > 1e-4 * mag + 1e-30;
    }
    printf("bad rows: %d of %d\n%s\n", bad, N, bad ? "FAIL" : "PASS");
    return bad != 0;
}
