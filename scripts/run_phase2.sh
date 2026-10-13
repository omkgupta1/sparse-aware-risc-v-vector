#!/usr/bin/env bash
# Phase 2: every kernel in the shared harness on the midterm matrices
# (n=256; densities 20/60 are the midterm cases, 1/5/100 extend validation),
# warm caches, VLEN=256.
#
#   scripts/env.sh scripts/run_phase2.sh        (JOBS=8 parallel by default)
set -euo pipefail
cd /project
make phase2

OUT=results/endterm/phase2
CFG=configs/sparse_rvv.py   # same machine as configs/rvv_test.py by default
DENSITIES="1 5 20 60 100"   # 20/60 = midterm cases; 1/5/100 widen gem5 validation coverage
KERNELS="scalar rvv_orig rvv_opt_m1 rvv_opt_m2 rvv_opt_m4 dense_m1 dense_m2 dense_m4"

jobs_file=$(mktemp)
for d in $DENSITIES; do
    for k in $KERNELS; do
        echo "$OUT/d${d}/$k $CFG build/bench -- kernel=$k gen=midterm n=256 density=$d warmup=1" >> "$jobs_file"
    done
done

status=0
xargs -P "${JOBS:-8}" -L 1 scripts/run_gem5.sh < "$jobs_file" || status=$?
rm -f "$jobs_file"
python3 scripts/phase2_report.py
exit $status
