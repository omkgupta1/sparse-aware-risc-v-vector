#!/usr/bin/env bash
# Run a command inside the project's Docker environment (see docker/Dockerfile).
#
#   scripts/env.sh make            # build workloads
#   scripts/env.sh bash            # interactive shell
#
# Mounts:  <repo>            -> /project   (working directory)
#          $GEM5_HOST_ROOT   -> /workspace (must contain gem5/, default ~/gem5-assignment)
# Inside the container, GEM5=/workspace/gem5.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GEM5_HOST_ROOT="${GEM5_HOST_ROOT:-$HOME/gem5-assignment}"
IMAGE="${IMAGE:-sparse-rvv-env}"

if [ ! -x "$GEM5_HOST_ROOT/gem5/build/RISCV/gem5.opt" ]; then
    echo "error: $GEM5_HOST_ROOT/gem5/build/RISCV/gem5.opt not found (set GEM5_HOST_ROOT)" >&2
    exit 1
fi

TTY=()
[ -t 0 ] && [ -t 1 ] && TTY=(-it)

exec docker run --rm ${TTY[@]+"${TTY[@]}"} \
    -v "$REPO:/project" \
    -v "$GEM5_HOST_ROOT:/workspace" \
    -e GEM5=/workspace/gem5 \
    -w /project \
    "$IMAGE" bash -c "$*"
