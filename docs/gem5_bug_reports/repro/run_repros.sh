#!/usr/bin/env bash
# Build and run the four gem5 bug reproducers.
#
#   GEM5_BIN=/path/to/build/RISCV/gem5.opt OUT=/tmp/repro ./run_repros.sh
#
# Needs: riscv64-linux-gnu-gcc (GCC >= 13 with RVV intrinsics), qemu-riscv64
# (optional, for the expected output) and a RISCV gem5.opt.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
GEM5_BIN="${GEM5_BIN:?set GEM5_BIN to a RISCV gem5.opt}"
OUT="${OUT:-$HERE/out}"
CC="${CC:-riscv64-linux-gnu-gcc}"
QEMU_CPU="rv64,v=true,vlen=256,elen=64"
mkdir -p "$OUT"

"$CC" -O2 -static -march=rv64gcv -mabi=lp64d "$HERE/issue1_vfredusum_overlap.c" -o "$OUT/issue1"
"$CC" -O2 -static -march=rv64gcv -mabi=lp64d "$HERE/issue2_vluxei_lmul4.c" -lm -o "$OUT/issue2"
"$CC" -O2 -static -march=rv64gcv -mabi=lp64d "$HERE/issue3_vslide_buffer.c" -lm -o "$OUT/issue3"
"$CC" -O2 -static -march=rv64gc -mabi=lp64d "$HERE/issue4_imp_indirect.c" -o "$OUT/issue4"

gem5() {  # gem5 NAME BINARY [config options...]
    local name=$1 bin=$2; shift 2
    "$GEM5_BIN" -d "$OUT/$name" "$HERE/se_riscv_rvv.py" --binary "$bin" "$@" > "$OUT/$name.log" 2>&1
    echo "exit=$?" >> "$OUT/$name.log"
}

qemu() {  # qemu NAME BINARY [program args...]
    local name=$1 bin=$2; shift 2
    command -v qemu-riscv64 >/dev/null || return 0
    qemu-riscv64 -cpu "$QEMU_CPU" "$bin" "$@" > "$OUT/$name.log" 2>&1
    echo "exit=$?" >> "$OUT/$name.log"
}

for i in 1 3; do
    qemu "issue${i}_qemu" "$OUT/issue$i"
    gem5 "issue${i}_o3" "$OUT/issue$i" --cpu o3 &
    gem5 "issue${i}_minor" "$OUT/issue$i" --cpu minor &
done
# Issue 2: two LMUL=2 gathers in flight (strongest), and one LMUL=4 gather.
for spec in "m2x2 5" "m4 60"; do
    set -- $spec
    qemu "issue2_qemu_$1_d$2" "$OUT/issue2" "$1" "$2"
    gem5 "issue2_o3_$1_d$2" "$OUT/issue2" --cpu o3 --args "$1" "$2" &
    gem5 "issue2_minor_$1_d$2" "$OUT/issue2" --cpu minor --args "$1" "$2" &
done
gem5 issue4_o3_stride "$OUT/issue4" --cpu o3 &
gem5 issue4_o3_imp "$OUT/issue4" --cpu o3 --l1d-imp &
gem5 issue4_minor_imp "$OUT/issue4" --cpu minor --l1d-imp &
wait

for f in "$OUT"/*.log; do
    printf '%-24s %s\n' "$(basename "$f" .log)" \
        "$(grep -hoE 'bad rows: [0-9]+ of [0-9]+|^(PASS|FAIL)$|panic: .{0,70}|exit=[0-9]+' "$f" | tr '\n' ' ')"
done
