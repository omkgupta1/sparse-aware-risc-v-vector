/* Deterministic PRNG (SplitMix64) used by all synthetic generators. */
#ifndef SPARSE_RVV_RNG_H
#define SPARSE_RVV_RNG_H

#include <stdint.h>

typedef struct { uint64_t s; } rng_t;

static inline uint64_t rng_next(rng_t *r)
{
    uint64_t z = (r->s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* Uniform double in (0, 1]: never 0, so log() is always finite. */
static inline double rng_unit(rng_t *r)
{
    return (double)((rng_next(r) >> 11) + 1) * 0x1.0p-53;
}

#endif
