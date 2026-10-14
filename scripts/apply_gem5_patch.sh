#!/usr/bin/env bash
# Apply gem5_patches/sparse_aware.patch to a gem5 v25.1.0.1 tree and
# (optionally) rebuild build/RISCV/gem5.opt.
#
#   scripts/apply_gem5_patch.sh /path/to/gem5            # apply only
#   BUILD=1 scripts/apply_gem5_patch.sh /path/to/gem5    # apply + scons build
#
# The build needs gem5's dependencies; the project image has them:
#   GEM5_HOST_ROOT=<dir containing gem5/> scripts/env.sh \
#       'BUILD=1 scripts/apply_gem5_patch.sh /workspace/gem5'
set -euo pipefail

GEM5_DIR="${1:?usage: apply_gem5_patch.sh /path/to/gem5}"
PATCH="$(cd "$(dirname "$0")/.." && pwd)/gem5_patches/sparse_aware.patch"

grep -q 'gem5Version = "25.1' "$GEM5_DIR/src/base/version.cc" ||
    echo "warning: $GEM5_DIR is not gem5 25.1; the patch may not apply" >&2

if [ -f "$GEM5_DIR/src/mem/cache/prefetch/vector_indirect.cc" ] &&
   (cd "$GEM5_DIR" && patch -p1 -R --dry-run -s < "$PATCH" >/dev/null 2>&1); then
    echo "patch already applied to $GEM5_DIR"
else
    (cd "$GEM5_DIR" && patch -p1 --dry-run < "$PATCH" >/dev/null)
    (cd "$GEM5_DIR" && patch -p1 < "$PATCH")
    echo "patch applied to $GEM5_DIR"
fi

if [ "${BUILD:-0}" = 1 ]; then
    (cd "$GEM5_DIR" && scons build/RISCV/gem5.opt -j"${JOBS:-6}" --ignore-style)
fi
