### Describe the bug

On `RiscvO3CPU`, indexed vector loads (`vluxei32.v`) into **LMUL>1** register groups occasionally return wrong data. The error rate grows with the number of gather element micro-ops in flight. The same binaries are correct on `RiscvMinorCPU` and QEMU.

The reproducer is a CSR sparse matrix-vector product (uniform random 1024×1024 matrix, fixed seed) checked row by row against a scalar double-precision reference:

| Inner loop (VLEN=256, e32) | O3: wrong rows | MinorCPU | QEMU |
|---|---:|---:|---:|
| `m2x2`: two independent e32m2 gathers per iteration (2 × 16 elements in flight), 5% density | **225 / 1024** | 0 | 0 |
| `m4`: one e32m4 gather per chunk (32 elements), 60% density | **4 / 1024** | 0 | 0 |

- **Each wrong row is off by roughly one or a few products,** e.g. `row 1 (len 42): got 9.786098 expected 10.23529`. That looks like a few gathered elements holding stale or wrong values.
- **LMUL=1 gathers never failed** in several hundred runs of similar kernels.
- **The failure depends on timing** but is deterministic for a given binary and config. It changes with code layout and input, and a larger load queue (`LQEntries = SQEntries = 128`) changes *which* rows fail rather than removing the failures.
- **The reduction is not the cause:** accumulators are folded to LMUL=1 with `vfadd.vv` before reducing, to keep the separate LMUL>1 `vfredusum` issue out of the picture.

**Possible cause (not verified):** indexed loads are expanded into one load micro-op per element plus `VCpyVs`/`VPinVd` micro-ops, and the element loads write a pinned destination register (`setNumPinnedWrites` in `VPinVdMicroInst`, handled in `src/cpu/o3/rename_map.*`). With 16–32 element writes to a pinned register group, and other gathers in flight, a consumer may read the group before all pinned writes have completed, or an older mapping may be read.

### Related issues

- #3278 (open) discusses how O3 models gathers by splitting them into per-element micro-ops "which has its own caveats". This report shows a functional consequence of that split for LMUL>1 groups.
- #2248 (incorrect RVV matrix multiply) was fixed by v25.0 and is a different problem: no indexed loads involved.

### Affects version

v25.1.0.1 (commit `a28ae2f8b2`). Not yet re-tested on `develop`.

### gem5 Modifications

None. Reproduced with the unmodified `build/RISCV/gem5.opt`.

### To Reproduce

```bash
riscv64-linux-gnu-gcc -O2 -static -march=rv64gcv -mabi=lp64d issue2_vluxei_lmul4.c -lm -o issue2
build/RISCV/gem5.opt se_riscv_rvv.py --binary issue2 --cpu o3    --args m2x2 5   # FAIL, 225 rows
build/RISCV/gem5.opt se_riscv_rvv.py --binary issue2 --cpu minor --args m2x2 5   # PASS
build/RISCV/gem5.opt se_riscv_rvv.py --binary issue2 --cpu o3    --args m4 60    # FAIL, 4 rows
qemu-riscv64 -cpu rv64,v=true,vlen=256,elen=64 ./issue2 m2x2 5                   # PASS
```

`se_riscv_rvv.py`: minimal stdlib config (`SimpleBoard`, 1 GHz, O3 or Minor CPU with VLEN=256/ELEN=64, `PrivateL1PrivateL2CacheHierarchy` 32 KiB/32 KiB/512 KiB, DDR3-1600, SE mode).

<details><summary>issue2_vluxei_lmul4.c</summary>

```c
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
```

</details>

<details><summary>se_riscv_rvv.py</summary>

```python
"""Minimal gem5 SE config used by the bug reproducers (gem5 standard library).

    gem5.opt se_riscv_rvv.py --binary prog [--cpu o3|minor] [--vlen 256]
                             [--l1d-imp] [--args ...]

RiscvO3CPU (default) or RiscvMinorCPU @ 1 GHz, RVV with VLEN/ELEN = 256/64,
private 32 KiB L1I/L1D + 512 KiB L2 (stdlib defaults: stride prefetchers),
single-channel DDR3-1600. --l1d-imp replaces the L1D prefetcher with
IndirectMemoryPrefetcher.
"""
import argparse

from m5.objects import IndirectMemoryPrefetcher, RiscvMinorCPU, RiscvO3CPU

from gem5.components.boards.simple_board import SimpleBoard
from gem5.components.cachehierarchies.classic.private_l1_private_l2_cache_hierarchy import (
    PrivateL1PrivateL2CacheHierarchy,
)
from gem5.components.memory import SingleChannelDDR3_1600
from gem5.components.processors.base_cpu_core import BaseCPUCore
from gem5.components.processors.base_cpu_processor import BaseCPUProcessor
from gem5.isas import ISA
from gem5.resources.resource import FileResource
from gem5.simulate.simulator import Simulator

parser = argparse.ArgumentParser()
parser.add_argument("--binary", required=True)
parser.add_argument("--cpu", choices=["o3", "minor"], default="o3")
parser.add_argument("--vlen", type=int, default=256)
parser.add_argument("--l1d-imp", action="store_true")
parser.add_argument("--args", nargs="*", default=[])
args = parser.parse_args()


class Core(BaseCPUCore):
    def __init__(self):
        cpu = RiscvO3CPU() if args.cpu == "o3" else RiscvMinorCPU()
        super().__init__(core=cpu, isa=ISA.RISCV)
        self.core.isa[0].vlen = args.vlen
        self.core.isa[0].elen = 64


class Hierarchy(PrivateL1PrivateL2CacheHierarchy):
    def incorporate_cache(self, board):
        super().incorporate_cache(board)
        if args.l1d_imp:
            getattr(self, "l1d-cache-0").prefetcher = IndirectMemoryPrefetcher()


board = SimpleBoard(
    clk_freq="1GHz",
    processor=BaseCPUProcessor(cores=[Core()]),
    memory=SingleChannelDDR3_1600(),
    cache_hierarchy=Hierarchy(l1d_size="32KiB", l1i_size="32KiB", l2_size="512KiB"),
)
board.set_se_binary_workload(FileResource(local_path=args.binary), arguments=args.args)
Simulator(board=board).run()
```

</details>

### Terminal Output

O3, `m2x2 5`:
```
row 1 (len 42): got 9.786098 expected 10.23529
row 3 (len 53): got 14.63102 expected 14.74332
row 4 (len 45): got 11.4385 expected 12.07487
row 5 (len 69): got 20.39038 expected 20.8877
...
mode m2x2 density 5% nnz 52208, bad rows: 225 of 1024
FAIL
```
O3, `m4 60`:
```
row 257 (len 598): got 162.1604 expected 162.2032
...
mode m4 density 60% nnz 629103, bad rows: 4 of 1024
FAIL
```
MinorCPU and QEMU: `bad rows: 0 of 1024`, `PASS`.

### Expected behavior

`bad rows: 0`, as on MinorCPU and QEMU.

### Host Operating System

Ubuntu 24.04 (Docker on macOS, Apple silicon)

### Host ISA

ARM (aarch64)

### Compiler used

Host: GCC 13.3.0. Guest: `riscv64-linux-gnu-gcc` 14.2.0, static.

### Additional information

`vloxei32.v` uses the same `VlIndex` templates (`src/arch/riscv/isa/templates/vector_mem.isa`), so it is presumably affected too (not tested separately).
