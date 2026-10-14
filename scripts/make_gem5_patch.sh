#!/usr/bin/env bash
# Regenerate gem5_patches/sparse_aware.patch from a modified gem5 tree with
# plain `diff -ruN` (no git).
#
#   GEM5_SRC=~/gem5-assignment/gem5 scripts/make_gem5_patch.sh
#
# Pristine versions of the gem5 files the project modifies are taken from
# gem5_patches/pristine/ (copied before the first change); files the project
# adds have no pristine version and appear as new files in the patch.
# Base: gem5 v25.1.0.1 (commit a28ae2f8b2).
set -euo pipefail
cd "$(dirname "$0")/.."

GEM5_SRC="${GEM5_SRC:-$HOME/gem5-assignment/gem5}"
PRISTINE=gem5_patches/pristine
OUT=gem5_patches/sparse_aware.patch

MODIFIED=(src/mem/cache/prefetch/Prefetcher.py src/mem/cache/prefetch/SConscript)
ADDED=(src/mem/cache/prefetch/vector_indirect.hh src/mem/cache/prefetch/vector_indirect.cc)

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
for f in "${MODIFIED[@]}"; do
    [ -f "$PRISTINE/$f" ] || { echo "missing pristine copy: $PRISTINE/$f" >&2; exit 1; }
    mkdir -p "$tmp/a/$(dirname "$f")" "$tmp/b/$(dirname "$f")"
    cp "$PRISTINE/$f" "$tmp/a/$f"
    cp "$GEM5_SRC/$f" "$tmp/b/$f"
done
for f in "${ADDED[@]}"; do
    mkdir -p "$tmp/b/$(dirname "$f")"
    cp "$GEM5_SRC/$f" "$tmp/b/$f"
done

{
    echo "# Sparse-Aware RISC-V Vector project: gem5 changes (Vector-Indirect Prefetcher)."
    echo "# Base: gem5 v25.1.0.1 (commit a28ae2f8b2). Apply with scripts/apply_gem5_patch.sh."
    (cd "$tmp" && diff -ruN a b) || true   # diff exits 1 when files differ
} > "$OUT"
echo "wrote $OUT ($(grep -c '^+++ ' "$OUT") files)"
