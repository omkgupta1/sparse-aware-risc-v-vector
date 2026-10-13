# Build + simulation environment for the Sparse-Aware RVV project.
#
# Provides: gem5 25.1 runtime/build dependencies, the RISC-V cross-compiler
# (GCC 14, glibc, used for static SE-mode binaries), QEMU user-mode (fast
# functional checks of the same binaries) and Python plotting libs.
# gem5 itself is NOT built here; the host gem5 tree is mounted at /workspace/gem5.
#
#   docker build -t sparse-rvv-env docker/
#   scripts/env.sh <command>        # runs <command> inside the container

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN echo 'Acquire::Retries "10";' > /etc/apt/apt.conf.d/80-retries \
    && apt-get update && apt-get install -y --no-install-recommends \
        build-essential scons git m4 pkg-config wget ca-certificates \
        python3 python3-dev python3-pydot python3-matplotlib python3-numpy \
        zlib1g-dev libprotobuf-dev protobuf-compiler libprotoc-dev \
        libgoogle-perftools-dev libcapstone-dev libpng-dev libelf-dev \
        gcc-14-riscv64-linux-gnu libc6-dev-riscv64-cross qemu-user \
    && ln -s /usr/bin/riscv64-linux-gnu-gcc-14 /usr/local/bin/riscv64-linux-gnu-gcc \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /project
