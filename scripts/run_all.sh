#!/usr/bin/env bash
# Reproduce every end-term experiment, report and figure. Run INSIDE the
# project container:
#
#   scripts/env.sh scripts/run_all.sh                 # everything
#   PHASES="1 2 5" scripts/env.sh scripts/run_all.sh  # a subset
#   JOBS=6 ...                                        # parallel gem5 runs (default 8)
#
# Phases: 1 2 3 4a 4b 4b-extra 5 bugs   (default: all, in that order)
#
# Prerequisites
#   - docker image built:   docker build -t sparse-rvv-env docker/
#   - gem5 v25.1.0.1 RISCV build at $GEM5_HOST_ROOT/gem5 (default ~/gem5-assignment)
#   - phases 4b / 4b-extra: gem5 patched and rebuilt with gem5_patches/sparse_aware.patch
#       BUILD=1 scripts/apply_gem5_patch.sh /workspace/gem5
#   - phases 3 / 4b (SuiteSparse inputs): scripts/download_matrices.sh (needs network)
#
# Completed runs are skipped (scripts/run_gem5.sh); FORCE=1 re-runs them.
# Approximate host time with 8 parallel jobs on a 10-core machine:
#   1: 1 min, 2: 2 min, 3: 1.5 h, 4a: 45 min, 4b: 1.5 h, 4b-extra: 40 min, 5: 1 min.
set -euo pipefail
cd /project

PHASES="${PHASES:-1 2 3 4a 4b 4b-extra 5}"
export JOBS="${JOBS:-8}"

say() { printf '\n=== %s ===\n' "$*"; }

need_matrices() {
    if ! ls matrices/*.csr >/dev/null 2>&1; then
        say "downloading SuiteSparse matrices"
        scripts/download_matrices.sh
    fi
}

need_vip() {
    # The patch adds the VectorIndirectPrefetcher SimObject; its build must be newer than the source.
    local src="$GEM5/src/mem/cache/prefetch/vector_indirect.cc"
    if [ ! -f "$src" ] || [ "$GEM5/build/RISCV/gem5.opt" -ot "$src" ]; then
        echo "gem5 is not patched: run  BUILD=1 scripts/apply_gem5_patch.sh $GEM5" >&2
        exit 1
    fi
}

say "build ($(riscv64-linux-gnu-gcc --version | head -1))"
make -j"$JOBS" all >/dev/null
make qemu-test >/dev/null && echo "qemu-test: all functional checks passed"

for p in $PHASES; do
    case $p in
    1) say "phase 1: ROI measurement"; scripts/run_phase1.sh ;;
    2) say "phase 2: baselines"; scripts/run_phase2.sh ;;
    3) say "phase 3: workloads and sweeps"; need_matrices; scripts/run_phase3.sh ;;
    4a) say "phase 4A: software mechanisms"; scripts/run_phase4a.sh ;;
    4b) say "phase 4B: Vector-Indirect Prefetcher"; need_vip; need_matrices; scripts/run_phase4b.sh ;;
    4b-extra) say "phase 4B follow-up: history depth, MSHRs"; need_vip; scripts/run_phase4b_extra.sh ;;
    5) say "phase 5: metrics, figures, report"
       python3 scripts/phase5_metrics.py
       python3 scripts/phase5_plots.py
       python3 scripts/phase5_report.py
       python3 scripts/vip_storage.py ;;
    bugs) say "gem5 bug evidence"; scripts/run_gem5_bug_checks.sh ;;
    *) echo "unknown phase '$p'" >&2; exit 1 ;;
    esac
done
say "done"
