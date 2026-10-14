### Describe the bug

On `RiscvO3CPU`, a `vfredusum.vs` at **LMUL=2** (also LMUL=4) whose destination `vd` is the first register of the `vs2` group returns a wrong sum. The upper register(s) of the `vs2` group contribute stale values when they were last written by a **tail-undisturbed** instruction with `vl < VLMAX`.

- RVV 1.0 allows `vd` to overlap `vs2` for reductions, and GCC 14 emits exactly this pattern for `__riscv_vfredusum_vs_f32m2_f32m1` on an accumulator.
- The same binary gives the correct result on `RiscvMinorCPU` and on QEMU.

Minimal sequence (VLEN=256, so e32m2 has 16 elements):

```asm
vsetvli  t1, zero, e32, m2, ta, ma
vmv.s.x  v1, zero
vle32.v  v2, (ones)          # v2,v3 = 1.0
vle32.v  v6, (a)
vle32.v  v4, (b)
li       t0, 5
vsetvli  t1, t0, e32, m2, tu, ma
vfmacc.vv v2, v6, v4         # elements 0..4 updated, 5..15 undisturbed
vsetvli  t1, zero, e32, m2, ta, ma
vfredusum.vs v2, v2, v1      # vd overlaps vs2   -> wrong on O3
vfmv.f.s ft0, v2
```

- Result on O3: **55.5**. Expected: **63.5** (MinorCPU and QEMU).
- The difference (8.0) is exactly the eight 1.0 values held in `v3`, the second register of the group, which the tail-undisturbed `vfmacc` did not modify.
- Writing the reduction to a register outside the group (`vfredusum.vs v10, v2, v1`) gives 63.5 on O3.

Further observations (from a larger directed test):
- **LMUL=4** behaves the same way: 63.5 instead of 79.5.
- **Producer doesn't matter:** a tail-undisturbed `vfadd.vv` or `vle32.v` as the producer also fails.
- **Copying doesn't help:** copying the group with `vmv2r.v`/`vmv4r.v` and reducing the copy in place also fails.
- **Full-length writes are fine:** a full-length (`vl = VLMAX`) producer followed by the same in-place reduction is correct.
- **A store in between changes the error:** inserting a full-group `vse32.v` between producer and reduction gives a *different* wrong value at LMUL=4 (71.5). So the outcome is timing-dependent, which suggests a register-dependency or renaming issue for the reduction's micro-ops when `vd` aliases `vs2[0]`, not an arithmetic error.

### Related issues

- #1956 (LMUL>1 `vfredosum` summed only part of the group), closed after #1955 in 24.1. This report reproduces on **25.1.0.1, after that fix**. It needs a tail-undisturbed partial write to the `vs2` group *and* `vd` overlapping `vs2`; full-length writes or a non-overlapping `vd` are correct.
- #2267 was closed as a usage error (agnostic tail policy, same register for vs1/vs2). This reproducer explicitly uses `tu` for the partial write, and `vd`/`vs2` overlap is legal for reductions (RVV 1.0 §14).

### Affects version

v25.1.0.1 (commit `a28ae2f8b2`). Not yet re-tested on `develop`.

### gem5 Modifications

None. Reproduced with the unmodified `build/RISCV/gem5.opt`.

### To Reproduce

```bash
riscv64-linux-gnu-gcc -O2 -static -march=rv64gcv -mabi=lp64d issue1_vfredusum_overlap.c -o issue1
build/RISCV/gem5.opt se_riscv_rvv.py --binary issue1 --cpu o3      # FAIL
build/RISCV/gem5.opt se_riscv_rvv.py --binary issue1 --cpu minor   # PASS
qemu-riscv64 -cpu rv64,v=true,vlen=256,elen=64 ./issue1            # PASS
```

`se_riscv_rvv.py` is a minimal stdlib config: `SimpleBoard`, 1 GHz, `RiscvO3CPU` or `RiscvMinorCPU` with `isa[0].vlen = 256`, `elen = 64`, `PrivateL1PrivateL2CacheHierarchy` (32 KiB / 32 KiB / 512 KiB), `SingleChannelDDR3_1600`, SE mode.

<details><summary>issue1_vfredusum_overlap.c</summary>

```c
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
A (vd overlaps vs2) = 55.5
B (vd = v10)        = 63.5
expected            = 63.5
FAIL
```
MinorCPU and QEMU: `A = 63.5`, `B = 63.5`, `PASS`.

### Expected behavior

Both reductions return 63.5, as on MinorCPU and QEMU.

### Host Operating System

Ubuntu 24.04 (Docker on macOS, Apple silicon)

### Host ISA

ARM (aarch64)

### Compiler used

Host: GCC 13.3.0 (gem5 build). Guest: `riscv64-linux-gnu-gcc` 14.2.0 (Ubuntu 14.2.0-4ubuntu2~24.04.1), static.

### Additional information

Workaround: fold the LMUL>1 group into one register with `vfadd.vv` at LMUL=1, then reduce at LMUL=1. That is correct on O3.
