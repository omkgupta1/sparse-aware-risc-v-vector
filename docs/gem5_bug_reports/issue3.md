### Describe the bug

On `RiscvO3CPU`, a vector register used as a packing buffer gives stale contents when it is built with **tail-undisturbed `vslideup.vx`** (and `vslidedown.vx` for leftovers) and later read at full length. In the reproducer the buffer holds gather offsets, so the stale values become out-of-range addresses and the simulation panics with a page-table fault.

The same binary is correct on `RiscvMinorCPU` and on QEMU, including QEMU with `rvv_ta_all_1s=true,rvv_ma_all_1s=true`. So the program does not depend on the contents of tail- or mask-agnostic elements.

Pattern (per row of a dense matrix, LMUL=1, VLEN=256 so VLMAX=8):

```c
vals = vcompress(row_chunk, nonzero_mask);           // n packed values
idx  = vcompress(vid + j, nonzero_mask);            // their column indices
if (cnt + n < VLMAX) {
    bufv = vslideup_tu(bufv, vals, cnt, cnt + n);    // append (tail undisturbed)
    bufi = vslideup_tu(bufi, idx,  cnt, cnt + n);
} else {
    bufv = vslideup_tu(bufv, vals, cnt, VLMAX);      // fill up, then use the
    bufi = vslideup_tu(bufi, idx,  cnt, VLMAX);      // whole buffer:
    vx  = vluxei32(x, bufi << 2, VLMAX);             //   gather -> fault on O3
    acc = vfmacc_tu(acc, bufv, vx, VLMAX);
    ... vslidedown leftovers into bufv/bufi ...
}
```

Every correct index is < 256, so all gathered addresses lie inside `x`. On O3 the gather instead accesses an unmapped address, which is consistent with `bufi` holding stale elements from before one of the tail-undisturbed slides. This looks related to the separate report about `vfredusum` reading stale upper registers after tail-undisturbed writes; here it happens at **LMUL=1**.

### Related issues

- #2575 (closed; fix merged into `develop` via #2625) and PR #3513 (open) concern `vslidedown` with **overlapping** source/destination registers, at LMUL>1 or on non-reordering CPUs. This report is different: `vslideup.vx` with the tail-undisturbed policy at **LMUL=1, without register overlap**. It is correct on MinorCPU and fails only on O3.
- PR #3313 ("refactor vector slide instructions", merged into `develop` on 2026-07-28, **after** v25.1.0.1) removed the extra register-pin and copy micro-ops of the slide instructions. That may already fix this behaviour; this report was only tested on v25.1.0.1.

### Affects version

v25.1.0.1 (commit `a28ae2f8b2`). Not yet re-tested on `develop`.

### gem5 Modifications

None. Reproduced with the unmodified `build/RISCV/gem5.opt`.

### To Reproduce

```bash
riscv64-linux-gnu-gcc -O2 -static -march=rv64gcv -mabi=lp64d issue3_vslide_buffer.c -lm -o issue3
build/RISCV/gem5.opt se_riscv_rvv.py --binary issue3 --cpu o3      # panic
build/RISCV/gem5.opt se_riscv_rvv.py --binary issue3 --cpu minor   # PASS
qemu-riscv64 -cpu rv64,v=true,vlen=256,elen=64 ./issue3            # PASS
qemu-riscv64 -cpu rv64,v=true,vlen=256,elen=64,rvv_ta_all_1s=true,rvv_ma_all_1s=true ./issue3   # PASS
```

<details><summary>issue3_vslide_buffer.c</summary>

```c
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

O3:
```
src/sim/faults.cc:103: panic: panic condition !handled && !tc->getSystemPtr()->trapToGdb(GDBSignal::SEGV, tc->contextId()) occurred: Page table fault when accessing virtual address 0xfdab4194
```
(The backtrace goes through `GenericPageTableFault::invoke` ← `o3::Commit::commitHead`.)

MinorCPU and QEMU: `bad rows: 0 of 256`, `PASS`.

### Expected behavior

No fault; `bad rows: 0 of 256`, as on MinorCPU and QEMU.

### Host Operating System

Ubuntu 24.04 (Docker on macOS, Apple silicon)

### Host ISA

ARM (aarch64)

### Compiler used

Host: GCC 13.3.0. Guest: `riscv64-linux-gnu-gcc` 14.2.0, static.

### Additional information

Workaround: stage the packed elements through memory (`vse32` at the fill position, `vle32` of a full vector) instead of merging them in a vector register. That is correct on O3.
