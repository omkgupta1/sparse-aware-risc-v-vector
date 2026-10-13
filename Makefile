# Builds all RISC-V workloads. Run inside the project container:
#     scripts/env.sh make            (or: scripts/env.sh make -j8)
#
# Outputs go to build/ (git-ignored). The midterm binaries in workloads/ are
# kept as-is and are never rebuilt by this Makefile.

GEM5     ?= /workspace/gem5
CC       := riscv64-linux-gnu-gcc
ARCH_V   := -march=rv64gcv -mabi=lp64d
CFLAGS   := -O2 -static -Wall -Wextra -Isrc
M5_INC   := -I$(GEM5)/include
M5_LIB   := $(GEM5)/util/m5/build/riscv/out/libm5.a
BUILD    := build

.PHONY: all phase1 m5lib versions clean

all: phase1

# Phase 1: midterm kernel, re-measured with an ROI around the kernel only.
phase1: $(BUILD)/spmv_rvv_roi $(BUILD)/spmv_rvv_e2e $(BUILD)/spmv_rvv_noopcall $(BUILD)/spmv_rvv_resetdump

$(BUILD)/spmv_rvv_roi: src/spmv_rvv_roi.c src/common/roi.h $(M5_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(ARCH_V) -DUSE_M5 $(M5_INC) $< $(M5_LIB) -lm -o $@

# Unmodified midterm source rebuilt with this toolchain (no ROI markers):
# end-to-end reference for the same compiler.
$(BUILD)/spmv_rvv_e2e: src/spmv_rvv.c | $(BUILD)
	$(CC) $(CFLAGS) $(ARCH_V) $< -lm -o $@

# ROI-methodology controls: same call sites without m5ops, and the
# m5_reset_stats/m5_dump_stats marker pair (see src/common/roi.h).
$(BUILD)/spmv_rvv_noopcall: src/spmv_rvv_roi.c src/common/roi.h | $(BUILD)
	$(CC) $(CFLAGS) $(ARCH_V) -DROI_NOOP_CALL $< -lm -o $@

$(BUILD)/spmv_rvv_resetdump: src/spmv_rvv_roi.c src/common/roi.h $(M5_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(ARCH_V) -DUSE_M5 -DROI_RESET_DUMP $(M5_INC) $< $(M5_LIB) -lm -o $@

# gem5's m5ops library for RISC-V (built inside the gem5 tree's util/m5/build).
m5lib: $(M5_LIB)
$(M5_LIB):
	cd $(GEM5)/util/m5 && scons -j8 riscv.CROSS_COMPILE=riscv64-linux-gnu- build/riscv/out/libm5.a

$(BUILD):
	mkdir -p $@

versions:
	@$(CC) --version | head -1
	@grep -o 'gem5Version = "[^"]*"' $(GEM5)/src/base/version.cc

clean:
	rm -rf $(BUILD)
