#!/usr/bin/env bash
# Run one gem5 SE simulation. Intended to run INSIDE the container (scripts/env.sh).
#
#   scripts/run_gem5.sh OUTDIR CONFIG BINARY [CONFIG_ARGS...] -- [PROGRAM_ARGS...]
#
# Writes OUTDIR/{stats.txt.gz,config.ini.gz,run.log,cmd.txt}. Exit status is gem5's.
# stats.txt/config.ini are gzipped to keep the repo small (gem5stats.py reads .gz);
# config.json, config.dot* and citations.bib are redundant (config.ini.gz has the
# full configuration) and are deleted after the run.
# Skips the run if OUTDIR/run.log already reports a clean exit (set FORCE=1 to rerun).
set -uo pipefail

out=$1; cfg=$2; bin=$3; shift 3
cfg_args=(); prog_args=()
while [ $# -gt 0 ] && [ "$1" != "--" ]; do cfg_args+=("$1"); shift; done
[ "${1:-}" = "--" ] && shift
prog_args=("$@")

GEM5_BIN="${GEM5:-/workspace/gem5}/build/RISCV/gem5.opt"

if [ "${FORCE:-0}" != 1 ] && grep -q '^EXIT_STATUS=0$' "$out/run.log" 2>/dev/null; then
    echo "skip (done): $out"
    exit 0
fi
mkdir -p "$out"

cmd=("$GEM5_BIN" -d "$out" "$cfg" --binary "$bin" ${cfg_args[@]+"${cfg_args[@]}"})
[ ${#prog_args[@]} -gt 0 ] && cmd+=(--program-args "${prog_args[@]}")
printf '%q ' "${cmd[@]}" > "$out/cmd.txt"; echo >> "$out/cmd.txt"

start=$(date +%s)
"${cmd[@]}" > "$out/run.log" 2>&1
status=$?
[ -f "$out/stats.txt" ] && gzip -f "$out/stats.txt"
[ -f "$out/config.ini" ] && gzip -f "$out/config.ini"
rm -f "$out/config.json" "$out/config.dot" "$out/config.dot.pdf" "$out/config.dot.svg" "$out/citations.bib"
echo "HOST_SECONDS=$(( $(date +%s) - start ))" >> "$out/run.log"
echo "EXIT_STATUS=$status" >> "$out/run.log"
echo "$( [ $status = 0 ] && echo ok || echo FAILED ) ($status, $(( $(date +%s) - start ))s): $out"
exit $status
