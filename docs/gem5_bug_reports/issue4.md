### Describe the bug

With `IndirectMemoryPrefetcher` attached to the L1D (classic caches, timing mode), simulation aborts on the first qualifying read hit:

```
src/mem/cache/prefetch/base.hh:229: panic: PrefetchInfo::get called with a request with no data.
```

It happens with both `RiscvO3CPU` and `RiscvMinorCPU`, on plain scalar code with an indirect access pattern (`sum += a[idx[i]]`), which is exactly the pattern IMP targets. With the default `StridePrefetcher` the same run completes.

**Cause** (from reading the code):
1. `BaseCache::recvTimingReq` notifies the probe listeners of a hit (`ppHit->notify(...)`) right after `access()`. At that point `satisfyRequest()` has already copied the block data into the packet, but the packet is still the *request* (`ReadReq`). It is turned into a response later, in `handleTimingReqHit()`. The comment there says "notify before anything else as later handleTimingReqHit might turn the packet in a response".
2. `Base::PrefetchInfo::PrefetchInfo(PacketPtr pkt, Addr addr, bool miss)` in `src/mem/cache/prefetch/base.cc` copies the data only if `pkt->hasData()`. For a `ReadReq` that is false, because the command has no `HasData` attribute. So `data` is `nullptr` for every read hit.
3. `IndirectMemory::calculatePrefetch` reads the index with `pfi.get<T>()` for read hits of ≤ 8 bytes, which panics.

### Related issues

- **#1487** reported the same panic (`PrefetchInfo::get called with a request with no data`) with IMP inside a `MultiPrefetcher`, and pointed at the same `|| !pkt->hasData()` condition in `base.cc`. It was closed in March 2025 because it could not be reproduced at the time, and the proposed fix (#2003) was **not merged**. This report gives a **deterministic, minimal** reproducer on v25.1.0.1: plain IMP on the L1D, no `MultiPrefetcher`, scalar code, both O3 and MinorCPU.
- The suggested fix below is narrower than removing `|| !pkt->hasData()` entirely. #1487/#2003 noted that the full removal reverted an earlier fix for a test using `riscv-ubuntu-run.py`.
- PR #3514 (open) adds a new `IndirectMemoryPrefetcherV2` that reads index values through `CacheAccessor::tryRead()`, but leaves the original `IndirectMemoryPrefetcher` unchanged, so this panic still applies to it.

### Affects version

v25.1.0.1 (commit `a28ae2f8b2`). Not yet re-tested on `develop`.

### gem5 Modifications

None. Reproduced with the unmodified `build/RISCV/gem5.opt`. The only config change is `l1dcache.prefetcher = IndirectMemoryPrefetcher()`.

### To Reproduce

```bash
riscv64-linux-gnu-gcc -O2 -static -march=rv64gc -mabi=lp64d issue4_imp_indirect.c -o issue4
build/RISCV/gem5.opt se_riscv_rvv.py --binary issue4 --cpu o3 --l1d-imp      # panic
build/RISCV/gem5.opt se_riscv_rvv.py --binary issue4 --cpu minor --l1d-imp   # panic
build/RISCV/gem5.opt se_riscv_rvv.py --binary issue4 --cpu o3                # completes (stride)
```

`se_riscv_rvv.py`: stdlib `SimpleBoard`, `PrivateL1PrivateL2CacheHierarchy` (32 KiB / 32 KiB / 512 KiB). With `--l1d-imp`, a subclass replaces the L1D prefetcher with `IndirectMemoryPrefetcher()` in `incorporate_cache()`.

<details><summary>issue4_imp_indirect.c</summary>

```c
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

```
src/mem/cache/prefetch/base.hh:229: panic: PrefetchInfo::get called with a request with no data.
```
Without IMP: `sum = 1.05167e+06`, `PASS`.

### Expected behavior

IMP receives the loaded index value on read hits and the run completes.

### Host Operating System

Ubuntu 24.04 (Docker on macOS, Apple silicon)

### Host ISA

ARM (aarch64)

### Compiler used

Host: GCC 13.3.0. Guest: `riscv64-linux-gnu-gcc` 14.2.0, static.

### Additional information

Suggested fix (**untested**): in `PrefetchInfo::PrefetchInfo(PacketPtr pkt, Addr addr, bool miss)`, also copy the data for satisfied read hits, whose data has already been filled in even though the packet is not yet a response:

```cpp
-    if ((!write && miss) || !pkt->hasData()) {
+    // Read hits are notified before the packet becomes a response: the data
+    // is already in the packet, but hasData() is still false for requests.
+    const bool has_data = pkt->hasData() || (pkt->isRead() && !miss);
+    if ((!write && miss) || !has_data) {
```

Related: IMP also only reads indices from accesses of ≤ 8 bytes, so it cannot train on vector index loads (e.g. a 32-byte RVV `vle32.v` holding 8 indices). That is a feature gap rather than a bug.
