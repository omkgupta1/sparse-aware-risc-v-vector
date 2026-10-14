#!/usr/bin/env bash
# Phase 4B follow-up studies (needs gem5 with the history_depth parameter of
# gem5_patches/sparse_aware.patch). Run inside the container:
#
#   scripts/env.sh scripts/run_phase4b_extra.sh       (JOBS=8 by default)
#
#  history  VIP history depth 4..128 (default 32 is re-run as a regression check:
#           it must reproduce the main Phase 4B runs exactly)
#  mshr     L1D MSHRs 16 (default) / 32 / 64, stride vs. VIP
set -euo pipefail
cd /project
make phase2 >/dev/null

OUT=results/endterm/phase4b
CFG=configs/sparse_rvv.py
VIP="--l1d-pf VectorIndirectPrefetcher"
jobs=$(mktemp)

for c in 032768 131072 524288; do
    in="gen=uniform rows=16384 cols=$((10#$c)) nnz_per_row=16 alpha=1.8 seed=1"   # identical to run_phase4b.sh
    for k in rvv_orig rvv_opt_m1 rvv_acc2_m1; do
        for h in 4 8 16 32 64 128; do
            echo "$OUT/history/u_r16384_c${c}_npr16/${k}__vip_h$h $CFG build/bench $VIP --l1d-pf-param history_depth=$h -- kernel=$k $in" >> "$jobs"
        done
    done
done

for c in 131072 524288; do
    in="gen=uniform rows=16384 cols=$((10#$c)) nnz_per_row=16 alpha=1.8 seed=1"   # identical to run_phase4b.sh
    for k in rvv_orig rvv_acc2_m1; do
        for m in 32 64; do
            echo "$OUT/mshr/u_r16384_c${c}_npr16/${k}__stride_mshr$m $CFG build/bench --l1d-mshrs $m -- kernel=$k $in" >> "$jobs"
            echo "$OUT/mshr/u_r16384_c${c}_npr16/${k}__vip_mshr$m $CFG build/bench $VIP --l1d-mshrs $m -- kernel=$k $in" >> "$jobs"
        done
    done
done

echo "phase4b-extra: $(wc -l < "$jobs") runs, ${JOBS:-8} in parallel" >&2
status=0
xargs -P "${JOBS:-8}" -L 1 scripts/run_gem5.sh < "$jobs" || status=$?
rm -f "$jobs"
python3 scripts/phase4b_report.py > /dev/null || status=1
exit $status
