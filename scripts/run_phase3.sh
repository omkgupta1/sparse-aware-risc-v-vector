#!/usr/bin/env bash
# Phase 3: realistic workloads and sweeps. Run inside the container:
#
#   scripts/env.sh scripts/run_phase3.sh                 # all groups
#   PHASE3_GROUPS="density size" scripts/env.sh scripts/run_phase3.sh
#   JOBS=8 (parallel gem5 runs, default 8)
#
# Needs matrices/*.csr (scripts/download_matrices.sh) for the suitesparse,
# spmm and vlen groups. Completed runs are skipped (FORCE=1 reruns).
#
# Layout: results/endterm/phase3/<group>/<input-id>/<kernel>
#         results/endterm/phase3/vlen/vlen<V>/<input-id>/<kernel>
#
# rvv_opt_m4 is not run: gem5 25.1 O3 bug (docs/gem5_rvv_issues.md, Issue 2).
set -euo pipefail
cd /project
make phase2 >/dev/null

OUT=results/endterm/phase3
CFG=configs/sparse_rvv.py
MAT=/project/matrices
PHASE3_GROUPS="${PHASE3_GROUPS:-suitesparse size spmm density powerlaw vlen}"   # long groups first

SPARSE="scalar rvv_orig rvv_opt_m1 rvv_opt_m2 rvv_acc2_m1 rvv_acc2_m2"
DENSE="dense_m1 dense_m2 dense_m4"
SPMM="spmm_scalar spmm_rvv_vk spmm_rvv_colwise"
DENSITIES="1 2 5 10 20 30 40 50 60 80"
SIZE_COLS="2048 8192 32768 131072 524288"
SUITE="bcsstk17 scircuit web-NotreDame luxembourg_osm"

jobs=$(mktemp)
emit() {  # emit OUTDIR "CFG_ARGS" "PROG_ARGS"
    echo "$1 $CFG build/bench $2 -- $3" >> "$jobs"
}
d2() { printf "%02d" "$1"; }

for g in $PHASE3_GROUPS; do
    case $g in
    density)
        for d in $DENSITIES; do for k in $SPARSE $DENSE; do
            emit "$OUT/density/u_n1024_d$(d2 $d)/$k" "" "kernel=$k gen=uniform n=1024 density=$d seed=1"
        done; done ;;
    powerlaw)
        for d in 1 2 5 10 20; do for k in $SPARSE; do
            emit "$OUT/powerlaw/p_n1024_d$(d2 $d)/$k" "" "kernel=$k gen=powerlaw n=1024 density=$d alpha=1.8 seed=1"
        done; done ;;
    size)
        for c in $SIZE_COLS; do for dist in uniform powerlaw; do for k in $SPARSE; do
            emit "$OUT/size/${dist:0:1}_r16384_c$(printf %06d $c)_npr16/$k" "" \
                 "kernel=$k gen=$dist rows=16384 cols=$c nnz_per_row=16 alpha=1.8 seed=1"
        done; done; done ;;
    suitesparse)
        for m in $SUITE; do for k in $SPARSE; do
            emit "$OUT/suitesparse/$m/$k" "" "kernel=$k gen=file path=$MAT/$m.csr"
        done; done ;;
    spmm)
        for kk in 4 8 16; do for k in $SPMM; do
            for d in 1 5 20; do
                emit "$OUT/spmm/u_n1024_d$(d2 $d)_k$(d2 $kk)/$k" "" "kernel=$k gen=uniform n=1024 density=$d seed=1 k=$kk"
            done
            emit "$OUT/spmm/u_r16384_c131072_npr16_k$(d2 $kk)/$k" "" \
                 "kernel=$k gen=uniform rows=16384 cols=131072 nnz_per_row=16 seed=1 k=$kk"
            for m in bcsstk17 luxembourg_osm; do
                emit "$OUT/spmm/${m}_k$(d2 $kk)/$k" "" "kernel=$k gen=file path=$MAT/$m.csr k=$kk"
            done
        done; done ;;
    vlen)
        for v in 128 512; do
            for d in 5 20 50; do for k in rvv_orig rvv_opt_m1 rvv_opt_m2 rvv_acc2_m1 dense_m2 dense_m4; do
                emit "$OUT/vlen/vlen$v/u_n1024_d$(d2 $d)/$k" "--vlen $v" "kernel=$k gen=uniform n=1024 density=$d seed=1"
            done; done
            for c in 8192 524288; do for k in rvv_orig rvv_opt_m1 rvv_opt_m2 rvv_acc2_m1; do
                emit "$OUT/vlen/vlen$v/u_r16384_c$(printf %06d $c)_npr16/$k" "--vlen $v" \
                     "kernel=$k gen=uniform rows=16384 cols=$c nnz_per_row=16 seed=1"
            done; done
            for k in rvv_orig rvv_opt_m1 rvv_opt_m2; do
                emit "$OUT/vlen/vlen$v/scircuit/$k" "--vlen $v" "kernel=$k gen=file path=$MAT/scircuit.csr"
            done
        done ;;
    *) echo "unknown group $g" >&2; exit 1 ;;
    esac
done

echo "phase3: $(wc -l < "$jobs") runs, ${JOBS:-8} in parallel" >&2
status=0
xargs -P "${JOBS:-8}" -L 1 scripts/run_gem5.sh < "$jobs" || status=$?
rm -f "$jobs"
python3 scripts/phase3_report.py || status=1
exit $status
