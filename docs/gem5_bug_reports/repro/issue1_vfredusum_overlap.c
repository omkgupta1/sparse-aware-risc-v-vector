/*
 * gem5 RVV reproducer: vfredusum.vs at LMUL=2 with vd overlapping vs2 reads a
 * stale upper register of the vs2 group after a tail-undisturbed write.
 *
 *   riscv64-linux-gnu-gcc -O2 -static -march=rv64gcv -mabi=lp64d \
 *       issue1_vfredusum_overlap.c -o issue1
 *
 * Expected (QEMU, gem5 MinorCPU), VLEN=256:  A = 63.5  B = 63.5   -> exit 0
 * The only difference between A and B is the destination register of the
 * reduction (A: vd = v2 = first register of the vs2 group, B: vd = v10).
 * Both are legal (RVV 1.0 allows vd to overlap vs2 for reductions).
 */
#include <stdio.h>

static float ones[16], a[16], b[16];

static float reduce(int overlap)
{
    unsigned long vl;
    float f;
    if (overlap) {
        __asm__ volatile(
            "vsetvli %[vl], zero, e32, m2, ta, ma\n"
            "vmv.s.x v1, zero\n"
            "vle32.v v2, (%[p1])\n"          /* v2,v3 = 1.0 (16 elements)   */
            "vle32.v v6, (%[pa])\n"
            "vle32.v v4, (%[pb])\n"
            "li t0, 5\n"
            "vsetvli %[vl], t0, e32, m2, tu, ma\n"
            "vfmacc.vv v2, v6, v4\n"         /* elements 0..4 updated       */
            "vsetvli %[vl], zero, e32, m2, ta, ma\n"
            "vfredusum.vs v2, v2, v1\n"      /* vd == vs2 group start       */
            "vfmv.f.s %[f], v2\n"
            : [vl] "=&r"(vl), [f] "=f"(f)
            : [p1] "r"(ones), [pa] "r"(a), [pb] "r"(b)
            : "t0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "memory");
    } else {
        __asm__ volatile(
            "vsetvli %[vl], zero, e32, m2, ta, ma\n"
            "vmv.s.x v1, zero\n"
            "vle32.v v2, (%[p1])\n"
            "vle32.v v6, (%[pa])\n"
            "vle32.v v4, (%[pb])\n"
            "li t0, 5\n"
            "vsetvli %[vl], t0, e32, m2, tu, ma\n"
            "vfmacc.vv v2, v6, v4\n"
            "vsetvli %[vl], zero, e32, m2, ta, ma\n"
            "vfredusum.vs v10, v2, v1\n"     /* vd outside the vs2 group    */
            "vfmv.f.s %[f], v10\n"
            : [vl] "=&r"(vl), [f] "=f"(f)
            : [p1] "r"(ones), [pa] "r"(a), [pb] "r"(b)
            : "t0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v10", "memory");
    }
    return f;
}

int main(void)
{
    for (int i = 0; i < 16; i++) {
        ones[i] = 1.0f;
        a[i] = (float)(i + 1);
        b[i] = (float)(2 * i + 1) * 0.5f;
    }
    /* expected: sum_{i<5} (1 + a_i b_i) + 11 * 1 = 52.5 + 11 = 63.5 */
    float A = reduce(1), B = reduce(0);
    printf("A (vd overlaps vs2) = %g\nB (vd = v10)        = %g\nexpected            = 63.5\n", A, B);
    int ok = A == 63.5f && B == 63.5f;
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
