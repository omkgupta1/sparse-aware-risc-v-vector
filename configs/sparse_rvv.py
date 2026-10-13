"""gem5 SE configuration for the end-term experiments.

Defaults reproduce the midterm machine exactly (configs/rvv_test.py):
RiscvO3CPU @ 1 GHz, VLEN=256/ELEN=64, private 32 KiB L1I/L1D + 512 KiB L2,
single-channel DDR3-1600. Options only change what they name.

    gem5.opt -d OUT configs/sparse_rvv.py --binary BIN [options] \
        --program-args key=value ...
"""
import argparse
import os

from m5.objects import RiscvMinorCPU, RiscvO3CPU, RiscvTimingSimpleCPU

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

CPUS = {"o3": RiscvO3CPU, "minor": RiscvMinorCPU, "timing": RiscvTimingSimpleCPU}

parser = argparse.ArgumentParser()
parser.add_argument("--binary", required=True)
parser.add_argument("--cpu", choices=sorted(CPUS), default="o3")
parser.add_argument("--vlen", type=int, default=256)
parser.add_argument("--elen", type=int, default=64)
parser.add_argument("--lq-entries", type=int, default=None, help="O3 load queue (default 32)")
parser.add_argument("--sq-entries", type=int, default=None, help="O3 store queue (default 32)")
parser.add_argument("--l1d-size", default="32KiB")
parser.add_argument("--l1i-size", default="32KiB")
parser.add_argument("--l2-size", default="512KiB")
parser.add_argument("--program-args", nargs="*", default=[])
args = parser.parse_args()


class RVVCore(BaseCPUCore):
    def __init__(self):
        core = CPUS[args.cpu]()
        if args.cpu == "o3":
            if args.lq_entries is not None:
                core.LQEntries = args.lq_entries
            if args.sq_entries is not None:
                core.SQEntries = args.sq_entries
        super().__init__(core=core, isa=ISA.RISCV)
        self.core.isa[0].elen = args.elen
        self.core.isa[0].vlen = args.vlen


binary_path = os.path.abspath(args.binary)
if not os.path.isfile(binary_path):
    raise FileNotFoundError(binary_path)

board = SimpleBoard(
    clk_freq="1GHz",
    processor=BaseCPUProcessor(cores=[RVVCore()]),
    memory=SingleChannelDDR3_1600(),
    cache_hierarchy=PrivateL1PrivateL2CacheHierarchy(
        l1d_size=args.l1d_size, l1i_size=args.l1i_size, l2_size=args.l2_size,
    ),
)
board.set_se_binary_workload(
    FileResource(local_path=binary_path), arguments=args.program_args,
)

print(f"sparse_rvv: cpu={args.cpu} VLEN={args.vlen} ELEN={args.elen} "
      f"LQ={args.lq_entries or 'default'} SQ={args.sq_entries or 'default'} "
      f"L1D={args.l1d_size} L2={args.l2_size}")
print(f"Binary: {binary_path} args: {' '.join(args.program_args)}")
Simulator(board=board).run()
