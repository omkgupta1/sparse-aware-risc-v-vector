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
