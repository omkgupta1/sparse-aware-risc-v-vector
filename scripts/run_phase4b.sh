#!/usr/bin/env bash
# Phase 4B: Vector-Indirect Prefetcher (VIP) A/B evaluation. Needs gem5 built
# with gem5_patches/sparse_aware.patch. All configurations use the same gem5
# binary and the same build/bench. Run inside the container:
#
#   scripts/env.sh scripts/run_phase4b.sh            (JOBS=8 by default)
#
# Layout: results/endterm/phase4b/<group>/<input-id>/<kernel>__<l1d prefetcher config>
set -euo pipefail
cd /project
make phase2 >/dev/null

OUT=results/endterm/phase4b
CFG=configs/sparse_rvv.py
MAT=/project/matrices

declare -A PF=(
    [stride]=""
    [vip]="--l1d-pf VectorIndirectPrefetcher"
    [vip_nola]="--l1d-pf VectorIndirectPrefetcher --l1d-pf-param enable_lookahead=False"
    [vip_noind]="--l1d-pf VectorIndirectPrefetcher --l1d-pf-param enable_indirect=False"
    [none]="--l1d-pf none"
)
# kernel -> prefetcher configurations
declare -A KCFG=(
    [rvv_orig]="stride vip vip_nola vip_noind none"
    [rvv_opt_m1]="stride vip"
    [rvv_acc2_m1]="stride vip"
    [scalar]="stride vip"
)

jobs=$(mktemp)
emit_input() {  # emit_input GROUP ID "PROGRAM ARGS"
    local k cfg   # local: must not clobber the caller's loop variables
    for k in rvv_orig rvv_opt_m1 rvv_acc2_m1 scalar; do
        for cfg in ${KCFG[$k]}; do
            echo "$OUT/$1/$2/${k}__$cfg $CFG build/bench ${PF[$cfg]} -- kernel=$k $3" >> "$jobs"
        done
    done
}

for c in 524288 131072 32768 8192 2048; do          # largest (slowest) first
    for dist in uniform powerlaw; do
        emit_input size "${dist:0:1}_r16384_c$(printf %06d $c)_npr16" \
            "gen=$dist rows=16384 cols=$c nnz_per_row=16 alpha=1.8 seed=1"
    done
done
for m in web-NotreDame scircuit bcsstk17 luxembourg_osm; do
    emit_input suitesparse "$m" "gen=file path=$MAT/$m.csr"
done
for d in 5 20; do
    emit_input density "u_n1024_d$(printf %02d $d)" "gen=uniform n=1024 density=$d seed=1"
done

# Sensitivity: look-ahead window (default 1KiB).
for c in 131072 524288; do
    in="gen=uniform rows=16384 cols=$c nnz_per_row=16 seed=1"
    for w in 256B 4KiB; do
        echo "$OUT/sensitivity/u_r16384_c$(printf %06d $c)_npr16/rvv_orig__vip_win$w $CFG build/bench ${PF[vip]} --l1d-pf-param lookahead_window=$w -- kernel=rvv_orig $in" >> "$jobs"
    done
done
# VLEN=512 check.
for spec in "u_r16384_c524288_npr16|gen=uniform rows=16384 cols=524288 nnz_per_row=16 seed=1" "scircuit|gen=file path=$MAT/scircuit.csr"; do
    id=${spec%%|*}; in=${spec#*|}
    for c in stride vip; do
        echo "$OUT/vlen512/$id/rvv_orig__$c $CFG build/bench --vlen 512 ${PF[$c]} -- kernel=rvv_orig $in" >> "$jobs"
    done
done

echo "phase4b: $(wc -l < "$jobs") runs, ${JOBS:-8} in parallel" >&2
status=0
xargs -P "${JOBS:-8}" -L 1 scripts/run_gem5.sh < "$jobs" || status=$?
rm -f "$jobs"
python3 scripts/phase4b_report.py || status=1
exit $status
