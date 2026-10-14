/*
 * gem5 reproducer: IndirectMemoryPrefetcher panics on read hits.
 * Plain scalar code (no vector instructions needed).
 *
 *   riscv64-linux-gnu-gcc -O2 -static -march=rv64gc -mabi=lp64d \
 *       issue4_imp_indirect.c -o issue4
 *
 * An indirect access pattern  sum += a[idx[i]]  (the pattern IMP targets).
 * Expected: prints the sum and exits 0, with or without the prefetcher.
 * Observed with IndirectMemoryPrefetcher on the L1D:
 *   panic: PrefetchInfo::get called with a request with no data.
 */
#include <stdint.h>
#include <stdio.h>

#define N (1 << 16)

static uint32_t idx[N];
static float a[N];

int main(void)
{
    uint64_t s = 1;
    for (uint32_t i = 0; i < N; i++) {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        idx[i] = (uint32_t)(s % N);
        a[i] = (float)(i % 17);
    }
    float sum = 0.0f;
    for (int rep = 0; rep < 2; rep++)
        for (uint32_t i = 0; i < N; i++)
            sum += a[idx[i]];
    printf("sum = %g\nPASS\n", sum);
    return 0;
}
