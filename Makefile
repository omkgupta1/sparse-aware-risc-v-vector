# Builds all RISC-V workloads. Run inside the project container:
#     scripts/env.sh make            (or: scripts/env.sh make -j8)
#
# Outputs go to build/ (git-ignored). The midterm binaries in workloads/ are
# kept as-is and are never rebuilt by this Makefile.

GEM5     ?= /workspace/gem5
CC       := riscv64-linux-gnu-gcc
ARCH_V   := -march=rv64gcv -mabi=lp64d
ARCH_S   := -march=rv64gc -mabi=lp64d
CFLAGS   := -O2 -static -Wall -Wextra -Isrc
M5_INC   := -I$(GEM5)/include
M5_LIB   := $(GEM5)/util/m5/build/riscv/out/libm5.a
BUILD    := build

.PHONY: all phase1 phase2 qemu-test m5lib versions clean

all: phase1 phase2

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

# Phase 2+: shared harness (build/bench) with every kernel.
# Scalar kernel and validation reference are built WITHOUT the V extension.
OBJ      := $(BUILD)/obj
HDRS     := $(wildcard src/*/*.h)
BENCH_V  := harness/bench harness/matrix kernels/spmv_rvv_orig kernels/spmv_rvv_opt \
            kernels/spmv_rvv_acc2 kernels/gemv_rvv kernels/spmm_rvv
# Kernels are 64-byte aligned so their timing does not depend on unrelated code
# layout (measured: unaligned rvv_orig moved 4.7% between builds; aligned, a
# 1 KiB address shift changes kernel cycles by <0.1%; see PROGRESS.md Phase 2).
KALIGN   := -falign-functions=64 -falign-loops=64
BENCH_S  := kernels/spmv_scalar kernels/spmm_scalar kernels/reference
BENCH_OBJS := $(BENCH_V:%=$(OBJ)/%.o) $(BENCH_S:%=$(OBJ)/%.o)

phase2: $(BUILD)/bench $(BUILD)/bench_qemu $(BUILD)/rvv_lmul_check

# Directed RVV LMUL>1 test (gem5 vfredusum bug reproducer, see PROGRESS.md).
$(BUILD)/rvv_lmul_check: src/tests/rvv_lmul_check.c | $(BUILD)
	$(CC) $(CFLAGS) $(ARCH_V) $< -o $@

# Same kernels without ROI markers (m5ops are illegal outside gem5), for fast
# functional testing under QEMU user-mode: make qemu-test
QEMU_OBJS := $(OBJ)/harness/bench_noroi.o $(filter-out $(OBJ)/harness/bench.o,$(BENCH_OBJS))
$(BUILD)/bench_qemu: $(QEMU_OBJS)
	$(CC) $(CFLAGS) $(ARCH_V) $(QEMU_OBJS) -lm -o $@

$(OBJ)/harness/bench_noroi.o: src/harness/bench.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(ARCH_V) $(KALIGN) -c $< -o $@

$(BUILD)/bench: $(BENCH_OBJS) $(M5_LIB)
	$(CC) $(CFLAGS) $(ARCH_V) $(BENCH_OBJS) $(M5_LIB) -lm -o $@

$(BENCH_V:%=$(OBJ)/%.o): $(OBJ)/%.o: src/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(ARCH_V) $(KALIGN) -DUSE_M5 $(M5_INC) -c $< -o $@

$(OBJ)/kernels/spmv_scalar.o $(OBJ)/kernels/spmm_scalar.o: $(OBJ)/%.o: src/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(ARCH_S) $(KALIGN) -c $< -o $@

$(OBJ)/kernels/reference.o: src/kernels/reference.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(ARCH_S) -fno-tree-vectorize -c $< -o $@

# gem5's m5ops library for RISC-V (built inside the gem5 tree's util/m5/build).
m5lib: $(M5_LIB)
$(M5_LIB):
	cd $(GEM5)/util/m5 && scons -j8 riscv.CROSS_COMPILE=riscv64-linux-gnu- build/riscv/out/libm5.a

# Functional check of every kernel under QEMU (VLEN=256), no timing.
QEMU_CPU ?= rv64,v=true,vlen=256,elen=64
qemu-test: $(BUILD)/bench_qemu
	@fail=0; q="qemu-riscv64 -cpu $(QEMU_CPU) $(BUILD)/bench_qemu"; \
	for k in scalar rvv_orig rvv_opt_m1 rvv_opt_m2 rvv_opt_m4 rvv_acc2_m1 rvv_acc2_m2 dense_m1 dense_m2 dense_m4; do \
	  for in in "gen=midterm n=256 density=1" "gen=midterm n=256 density=60" "gen=midterm n=256 density=100" \
	            "gen=uniform rows=300 cols=1000 density=7" "gen=powerlaw rows=500 cols=700 nnz_per_row=20"; do \
	    $$q kernel=$$k $$in 2>/dev/null | grep RESULT || { echo "FAILED/CRASHED: $$k $$in"; fail=1; }; \
	  done; done; \
	for k in spmm_scalar spmm_rvv_vk spmm_rvv_colwise; do for kk in 1 4 8 16 37; do \
	  $$q kernel=$$k gen=powerlaw rows=400 cols=900 nnz_per_row=12 k=$$kk 2>/dev/null | grep RESULT || { echo "FAILED/CRASHED: $$k k=$$kk"; fail=1; }; \
	done; done; exit $$fail

$(BUILD):
	mkdir -p $@

versions:
	@$(CC) --version | head -1
	@grep -o 'gem5Version = "[^"]*"' $(GEM5)/src/base/version.cc

clean:
	rm -rf $(BUILD)
