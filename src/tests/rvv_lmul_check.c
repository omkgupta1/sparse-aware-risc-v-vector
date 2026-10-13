/*
 * Directed RVV test for LMUL = 2 and 4 (e32, VLEN = 256 expected).
 *
 * Each test runs one instruction sequence in inline assembly with fixed
 * registers (mirroring what GCC emits for src/kernels/{spmv_rvv_opt,gemv_rvv}.c)
 * and prints every result element. Run the same binary under QEMU and gem5
 * and diff the output: any difference is a simulator bug.
 *
 *   T1 vmvNr.v      whole-register-group move
 *   T2 vfmacc.vv    vl = VLMAX
 *   T3 vfredusum.vs vd == vs2 group start (as GCC emits)
 *   T4 vfredusum.vs vd outside the vs2 group
 *   T5 vfmacc.vv    tail-undisturbed with vl < VLMAX
 *   T6 vfmacc.vv    tail-undisturbed, vl < VLMAX, then vfredusum over VLMAX
 *   T7 as T6, but vfredusum writes vd = v31 (outside the vs2 group)
 *   T8 as T6, but a full-group vse32 store sits between vfmacc and vfredusum
 *   T9 as T6, but vfadd.vv (not vfmacc) is the tail-undisturbed op
 *  T10 as T6, but vle32.v (a load) is the tail-undisturbed op
 *  T11 as T6, but the tail-undisturbed op is followed by vmvNr.v to v24 and
 *      the copy is reduced
 *  T12 as T6, but the group is folded to LMUL=1 with vfadd.vv and reduced at
 *      LMUL=1 (the workaround used in src/kernels/rvv_reduce.h)
 */
#include <stdio.h>

#define MAXE 32  /* VLMAX for e32,m4 at VLEN=256 */

static float a[MAXE], b[MAXE], ones[MAXE], out[MAXE];

static void init(void)
{
    for (int i = 0; i < MAXE; i++) {
        a[i] = (float)(i + 1);
        b[i] = (float)(2 * i + 1) * 0.5f;
        ones[i] = 1.0f;
    }
}

static void dump(const char *name, int n)
{
    printf("%s:", name);
    for (int i = 0; i < n; i++)
        printf(" %g", out[i]);
    printf("\n");
}

static void clear(void)
{
    for (int i = 0; i < MAXE; i++)
        out[i] = -99.0f;
}

/* LMUL-specific instruction text is pasted in via the macros below. */
#define TESTS(LM, N, VMV, ACC, VA, VB, SRC, DST)                               \
static void tests_##LM(void)                                                   \
{                                                                              \
    unsigned long vl;                                                          \
    float f;                                                                   \
                                                                               \
    clear();                                                                   \
    __asm__ volatile(                                                          \
        "vsetvli %[vl], zero, e32, " #LM ", ta, ma\n"                          \
        "vle32.v " SRC ", (%[pa])\n"                                           \
        VMV " " DST ", " SRC "\n"                                              \
        "vse32.v " DST ", (%[po])\n"                                           \
        : [vl] "=&r"(vl) : [pa] "r"(a), [po] "r"(out) : "memory");             \
    dump("T1_vmvNr_" #LM, N);                                                  \
                                                                               \
    clear();                                                                   \
    __asm__ volatile(                                                          \
        "vsetvli %[vl], zero, e32, " #LM ", ta, ma\n"                          \
        "vmv.v.i " ACC ", 0\n"                                                 \
        "vle32.v " VA ", (%[pa])\n"                                            \
        "vle32.v " VB ", (%[pb])\n"                                            \
        "vfmacc.vv " ACC ", " VA ", " VB "\n"                                  \
        "vfmacc.vv " ACC ", " VA ", " VB "\n"                                  \
        "vse32.v " ACC ", (%[po])\n"                                           \
        : [vl] "=&r"(vl) : [pa] "r"(a), [pb] "r"(b), [po] "r"(out) : "memory"); \
    dump("T2_vfmacc_full_" #LM, N);                                            \
                                                                               \
    __asm__ volatile(                                                          \
        "vsetvli %[vl], zero, e32, " #LM ", ta, ma\n"                          \
        "vmv.s.x v1, zero\n"                                                   \
        "vle32.v " ACC ", (%[pa])\n"                                           \
        "vfredusum.vs " ACC ", " ACC ", v1\n"                                  \
        "vfmv.f.s %[f], " ACC "\n"                                             \
        : [vl] "=&r"(vl), [f] "=f"(f) : [pa] "r"(a) : "memory");               \
    printf("T3_vfredusum_vd_eq_vs2_" #LM ": %g\n", f);                         \
                                                                               \
    __asm__ volatile(                                                          \
        "vsetvli %[vl], zero, e32, " #LM ", ta, ma\n"                          \
        "vmv.s.x v1, zero\n"                                                   \
        "vle32.v " ACC ", (%[pa])\n"                                           \
        "vfredusum.vs v31, " ACC ", v1\n"                                      \
        "vfmv.f.s %[f], v31\n"                                                 \
        : [vl] "=&r"(vl), [f] "=f"(f) : [pa] "r"(a) : "memory");               \
    printf("T4_vfredusum_vd_v31_" #LM ": %g\n", f);                            \
                                                                               \
    clear();                                                                   \
    __asm__ volatile(                                                          \
        "vsetvli %[vl], zero, e32, " #LM ", ta, ma\n"                          \
        "vle32.v " ACC ", (%[p1])\n"                                           \
        "vle32.v " VA ", (%[pa])\n"                                            \
        "vle32.v " VB ", (%[pb])\n"                                            \
        "li t0, 5\n"                                                           \
        "vsetvli %[vl], t0, e32, " #LM ", tu, ma\n"                            \
        "vfmacc.vv " ACC ", " VA ", " VB "\n"                                  \
        "vsetvli %[vl], zero, e32, " #LM ", ta, ma\n"                          \
        "vse32.v " ACC ", (%[po])\n"                                           \
        : [vl] "=&r"(vl)                                                       \
        : [p1] "r"(ones), [pa] "r"(a), [pb] "r"(b), [po] "r"(out)              \
        : "t0", "memory");                                                     \
    dump("T5_vfmacc_tu_vl5_" #LM, N);                                          \
                                                                               \
    __asm__ volatile(                                                          \
        "vsetvli %[vl], zero, e32, " #LM ", ta, ma\n"                          \
        "vmv.s.x v1, zero\n"                                                   \
        "vle32.v " ACC ", (%[p1])\n"                                           \
        "vle32.v " VA ", (%[pa])\n"                                            \
        "vle32.v " VB ", (%[pb])\n"                                            \
        "li t0, 5\n"                                                           \
        "vsetvli %[vl], t0, e32, " #LM ", tu, ma\n"                            \
        "vfmacc.vv " ACC ", " VA ", " VB "\n"                                  \
        "vsetvli %[vl], zero, e32, " #LM ", ta, ma\n"                          \
        "vfredusum.vs " ACC ", " ACC ", v1\n"                                  \
        "vfmv.f.s %[f], " ACC "\n"                                             \
        : [vl] "=&r"(vl), [f] "=f"(f)                                          \
        : [p1] "r"(ones), [pa] "r"(a), [pb] "r"(b)                             \
        : "t0", "memory");                                                     \
    printf("T6_vfmacc_tu_vl5_then_vfredusum_" #LM ": %g\n", f);                \
                                                                               \
    __asm__ volatile(                                                          \
        "vsetvli %[vl], zero, e32, " #LM ", ta, ma\n"                          \
        "vmv.s.x v1, zero\n"                                                   \
        "vle32.v " ACC ", (%[p1])\n"                                           \
        "vle32.v " VA ", (%[pa])\n"                                            \
        "vle32.v " VB ", (%[pb])\n"                                            \
        "li t0, 5\n"                                                           \
        "vsetvli %[vl], t0, e32, " #LM ", tu, ma\n"                            \
        "vfmacc.vv " ACC ", " VA ", " VB "\n"                                                                  \
        "vsetvli %[vl], zero, e32, " #LM ", ta, ma\n"                          \
                                                                        \
        "vfredusum.vs " "v31" ", " ACC ", v1\n"  \
        "vfmv.f.s %[f], " "v31" "\n"                                        \
        : [vl] "=&r"(vl), [f] "=f"(f)                                          \
        : [p1] "r"(ones), [pa] "r"(a), [pb] "r"(b), [po] "r"(out)              \
        : "t0", "memory");                                                     \
    printf("T7_vfmacc_tu_vl5_then_vfredusum_vd_v31_" #LM ": %g\n", f);                                         \
                                                                               \
    __asm__ volatile(                                                          \
        "vsetvli %[vl], zero, e32, " #LM ", ta, ma\n"                          \
        "vmv.s.x v1, zero\n"                                                   \
        "vle32.v " ACC ", (%[p1])\n"                                           \
        "vle32.v " VA ", (%[pa])\n"                                            \
        "vle32.v " VB ", (%[pb])\n"                                            \
        "li t0, 5\n"                                                           \
        "vsetvli %[vl], t0, e32, " #LM ", tu, ma\n"                            \
        "vfmacc.vv " ACC ", " VA ", " VB "\n"                                                                  \
        "vsetvli %[vl], zero, e32, " #LM ", ta, ma\n"                          \
        "vse32.v " ACC ", (%[po])\n"                                                                \
        "vfredusum.vs " ACC ", " ACC ", v1\n"  \
        "vfmv.f.s %[f], " ACC "\n"                                        \
        : [vl] "=&r"(vl), [f] "=f"(f)                                          \
        : [p1] "r"(ones), [pa] "r"(a), [pb] "r"(b), [po] "r"(out)              \
        : "t0", "memory");                                                     \
    printf("T8_vfmacc_tu_vl5_store_then_vfredusum_" #LM ": %g\n", f);                                         \
                                                                               \
    __asm__ volatile(                                                          \
        "vsetvli %[vl], zero, e32, " #LM ", ta, ma\n"                          \
        "vmv.s.x v1, zero\n"                                                   \
        "vle32.v " ACC ", (%[p1])\n"                                           \
        "vle32.v " VA ", (%[pa])\n"                                            \
        "vle32.v " VB ", (%[pb])\n"                                            \
        "li t0, 5\n"                                                           \
        "vsetvli %[vl], t0, e32, " #LM ", tu, ma\n"                            \
        "vfadd.vv " ACC ", " VA ", " VB "\n"                                                                  \
        "vsetvli %[vl], zero, e32, " #LM ", ta, ma\n"                          \
                                                                        \
        "vfredusum.vs " ACC ", " ACC ", v1\n"  \
        "vfmv.f.s %[f], " ACC "\n"                                        \
        : [vl] "=&r"(vl), [f] "=f"(f)                                          \
        : [p1] "r"(ones), [pa] "r"(a), [pb] "r"(b), [po] "r"(out)              \
        : "t0", "memory");                                                     \
    printf("T9_vfadd_tu_vl5_then_vfredusum_" #LM ": %g\n", f);                                         \
                                                                               \
    __asm__ volatile(                                                          \
        "vsetvli %[vl], zero, e32, " #LM ", ta, ma\n"                          \
        "vmv.s.x v1, zero\n"                                                   \
        "vle32.v " ACC ", (%[p1])\n"                                           \
        "vle32.v " VA ", (%[pa])\n"                                            \
        "vle32.v " VB ", (%[pb])\n"                                            \
        "li t0, 5\n"                                                           \
        "vsetvli %[vl], t0, e32, " #LM ", tu, ma\n"                            \
        "vle32.v " ACC ", (%[pa])\n"                                                                  \
        "vsetvli %[vl], zero, e32, " #LM ", ta, ma\n"                          \
                                                                        \
        "vfredusum.vs " ACC ", " ACC ", v1\n"  \
        "vfmv.f.s %[f], " ACC "\n"                                        \
        : [vl] "=&r"(vl), [f] "=f"(f)                                          \
        : [p1] "r"(ones), [pa] "r"(a), [pb] "r"(b), [po] "r"(out)              \
        : "t0", "memory");                                                     \
    printf("T10_vle32_tu_vl5_then_vfredusum_" #LM ": %g\n", f);                                         \
                                                                               \
    __asm__ volatile(                                                          \
        "vsetvli %[vl], zero, e32, " #LM ", ta, ma\n"                          \
        "vmv.s.x v1, zero\n"                                                   \
        "vle32.v " ACC ", (%[p1])\n"                                           \
        "vle32.v " VA ", (%[pa])\n"                                            \
        "vle32.v " VB ", (%[pb])\n"                                            \
        "li t0, 5\n"                                                           \
        "vsetvli %[vl], t0, e32, " #LM ", tu, ma\n"                            \
        "vfmacc.vv " ACC ", " VA ", " VB "\n"                                                                  \
        "vsetvli %[vl], zero, e32, " #LM ", ta, ma\n"                          \
        VMV " v24, " ACC "\n"                                                                \
        "vfredusum.vs " "v24" ", " "v24" ", v1\n"  \
        "vfmv.f.s %[f], " "v24" "\n"                                        \
        : [vl] "=&r"(vl), [f] "=f"(f)                                          \
        : [p1] "r"(ones), [pa] "r"(a), [pb] "r"(b), [po] "r"(out)              \
        : "t0", "memory");                                                     \
    printf("T11_vfmacc_tu_vl5_vmvNr_then_vfredusum_" #LM ": %g\n", f);                                         \
}

/* Registers as GCC allocated them in gemv_rvv_m2 / gemv_rvv_m4. */
static void test_fold(void)
{
    unsigned long vl;
    float f2, f4;

    __asm__ volatile(
        "vsetvli %[vl], zero, e32, m2, ta, ma\n"
        "vmv.s.x v1, zero\n"
        "vle32.v v2, (%[p1])\n"
        "vle32.v v6, (%[pa])\n"
        "vle32.v v4, (%[pb])\n"
        "li t0, 5\n"
        "vsetvli %[vl], t0, e32, m2, tu, ma\n"
        "vfmacc.vv v2, v6, v4\n"
        "vsetvli %[vl], zero, e32, m1, ta, ma\n"
        "vfadd.vv v10, v2, v3\n"
        "vfredusum.vs v10, v10, v1\n"
        "vfmv.f.s %[f], v10\n"
        : [vl] "=&r"(vl), [f] "=f"(f2)
        : [p1] "r"(ones), [pa] "r"(a), [pb] "r"(b) : "t0", "memory");
    printf("T12_fold_to_m1_then_vfredusum_m2: %g\n", f2);

    __asm__ volatile(
        "vsetvli %[vl], zero, e32, m4, ta, ma\n"
        "vmv.s.x v1, zero\n"
        "vle32.v v4, (%[p1])\n"
        "vle32.v v12, (%[pa])\n"
        "vle32.v v8, (%[pb])\n"
        "li t0, 5\n"
        "vsetvli %[vl], t0, e32, m4, tu, ma\n"
        "vfmacc.vv v4, v12, v8\n"
        "vsetvli %[vl], zero, e32, m1, ta, ma\n"
        "vfadd.vv v20, v4, v5\n"
        "vfadd.vv v21, v6, v7\n"
        "vfadd.vv v20, v20, v21\n"
        "vfredusum.vs v20, v20, v1\n"
        "vfmv.f.s %[f], v20\n"
        : [vl] "=&r"(vl), [f] "=f"(f4)
        : [p1] "r"(ones), [pa] "r"(a), [pb] "r"(b) : "t0", "memory");
    printf("T12_fold_to_m1_then_vfredusum_m4: %g\n", f4);
}

TESTS(m2, 16, "vmv2r.v", "v2", "v6", "v4", "v8", "v2")
TESTS(m4, 32, "vmv4r.v", "v4", "v12", "v8", "v16", "v4")

int main(void)
{
    init();
    tests_m2();
    tests_m4();
    test_fold();
    return 0;
}
