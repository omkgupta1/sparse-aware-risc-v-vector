#!/usr/bin/env bash
# Reproduces the evidence for the two gem5 25.1 RVV issues (docs/gem5_rvv_issues.md).
#
#   scripts/env.sh scripts/run_gem5_bug_checks.sh
#
# 1. Directed test src/tests/rvv_lmul_check.c on QEMU (reference) and gem5 O3.
# 2. rvv_opt_m4 (LMUL=4 gather) on gem5 O3, O3 with a 128-entry LQ/SQ, and MinorCPU.
set -euo pipefail
cd /project
make phase2

QEMU_CPU=rv64,v=true,vlen=256,elen=64
OUT=results/endterm/phase2/gem5_rvv_check
qemu-riscv64 -cpu "$QEMU_CPU" build/rvv_lmul_check 2>/dev/null > /tmp/qemu_ref.txt
FORCE=1 scripts/run_gem5.sh "$OUT" configs/sparse_rvv.py build/rvv_lmul_check
cp /tmp/qemu_ref.txt "$OUT/qemu_reference.txt"
grep -E '^T[0-9]' "$OUT/run.log" > "$OUT/gem5_output.txt"
diff "$OUT/qemu_reference.txt" "$OUT/gem5_output.txt" > "$OUT/diff.txt" || true
echo "directed test: $(grep -c '^<' "$OUT/diff.txt" || true) result lines differ (see $OUT/diff.txt)"

O=results/endterm/phase2/gem5_m4_investigation
pids=()
for d in 20 60 100; do
    FORCE=1 scripts/run_gem5.sh "$O/o3_default_d$d" configs/sparse_rvv.py build/bench -- kernel=rvv_opt_m4 density=$d verbose=1 & pids+=($!)
    FORCE=1 scripts/run_gem5.sh "$O/o3_lq128_d$d" configs/sparse_rvv.py build/bench --lq-entries 128 --sq-entries 128 -- kernel=rvv_opt_m4 density=$d verbose=1 & pids+=($!)
    FORCE=1 scripts/run_gem5.sh "$O/minor_d$d" configs/sparse_rvv.py build/bench --cpu minor -- kernel=rvv_opt_m4 density=$d verbose=1 & pids+=($!)
done
for p in "${pids[@]}"; do wait "$p" || true; done
{
    echo "| run | bad rows | max rel err | validation |"
    echo "|---|---:|---:|---|"
    for r in "$O"/*/; do
        line=$(grep -E '^RESULT' "$r/run.log" || echo "RESULT missing")
        echo "| $(basename "$r") | $(grep -oE 'bad_rows=[0-9]+' <<<"$line" | cut -d= -f2) | $(grep -oE 'max_rel_err=[^ ]+' <<<"$line" | cut -d= -f2) | $(grep -oE 'validation=[A-Z]+' <<<"$line" | cut -d= -f2) |"
    done
} | tee "$O/summary.md"
