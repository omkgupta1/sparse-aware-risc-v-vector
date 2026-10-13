#!/usr/bin/env bash
# Phase 1: re-measure the midterm cases (density 20% and 60%) with an ROI
# around the kernel only, plus end-to-end runs of the unmodified midterm source
# built with the same toolchain, plus two ROI-methodology control builds.
# Same gem5 config as the midterm.
#
#   scripts/env.sh scripts/run_phase1.sh
set -euo pipefail
cd /project
make phase1

OUT=results/endterm/phase1
CFG=configs/rvv_test.py
run() { scripts/run_gem5.sh "$@"; }

pids=()
for d in 20 60; do
    run "$OUT/roi_d$d" "$CFG" build/spmv_rvv_roi --vlen 256 --elen 64 -- "$d" & pids+=($!)
    run "$OUT/e2e_d$d" "$CFG" build/spmv_rvv_e2e --vlen 256 --elen 64 -- "$d" & pids+=($!)
    # controls for the ROI methodology
    run "$OUT/controls/noopcall_d$d" "$CFG" build/spmv_rvv_noopcall --vlen 256 --elen 64 -- "$d" & pids+=($!)
    run "$OUT/controls/resetdump_d$d" "$CFG" build/spmv_rvv_resetdump --vlen 256 --elen 64 -- "$d" & pids+=($!)
done
fail=0
for p in "${pids[@]}"; do wait "$p" || fail=1; done
python3 scripts/phase1_report.py
exit $fail
