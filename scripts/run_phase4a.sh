#!/usr/bin/env bash
# Phase 4A: software sparse-aware mechanisms (mask skipping, compaction,
# density-aware hybrid). Baselines are re-run with the same binary so every
# A/B comparison has identical code layout. Run inside the container:
#
#   scripts/env.sh scripts/run_phase4a.sh            (JOBS=8 by default)
set -euo pipefail
cd /project
make phase2 >/dev/null

OUT=results/endterm/phase4a
CFG=configs/sparse_rvv.py
jobs=$(mktemp)
emit() { echo "$1 $CFG build/bench -- $2" >> "$jobs"; }

# A. Uniform density sweep (same inputs as Phase 3 'density').
for d in 1 2 5 10 20 30 40 50 60 80; do
    in="gen=uniform n=1024 density=$d seed=1"
    for k in scalar rvv_orig dense_m1 dense_m2 dense_skip_m1 dense_skip_m2 dense_compress_m1 hybrid; do
        emit "$OUT/density/u_n1024_d$(printf %02d $d)/$k" "kernel=$k $in"
    done
done

# B. Mixed-density matrices (bands of 64 rows): where a per-block choice matters.
for cfg in "1 80 0.25" "5 80 0.25" "5 80 0.5" "5 90 0.25" "20 70 0.5" "1 95 0.1"; do
    read -r lo hi fr <<< "$cfg"
    id=$(printf "mx_n1024_lo%02d_hi%02d_f%s" "$lo" "$hi" "${fr/./}")
    in="gen=mixed n=1024 d_lo=$lo d_hi=$hi frac=$fr band=64 seed=1"
    for k in scalar rvv_orig dense_m2 dense_skip_m2 dense_compress_m1; do
        emit "$OUT/mixed/$id/$k" "kernel=$k $in"
    done
    for t in 40 58.2 75; do
        emit "$OUT/mixed/$id/hybrid_t$t" "kernel=hybrid thr=$t block_rows=64 $in"
    done
done

echo "phase4a: $(wc -l < "$jobs") runs, ${JOBS:-8} in parallel" >&2
status=0
xargs -P "${JOBS:-8}" -L 1 scripts/run_gem5.sh < "$jobs" || status=$?
rm -f "$jobs"
python3 scripts/phase4a_report.py || status=1
exit $status
