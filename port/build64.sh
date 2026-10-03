#!/bin/sh
# Builds the 64-bit ARM Linux test binary (./pokehns64) inside the hns-port
# Docker image. The image is arm64, so this is a native build and the binary
# runs without qemu. It stands in for the Android arm64-v8a target.
# Usage: port/build64.sh [extra make args]
# First run: docker build -t hns-port port/docker
set -e
cd "$(dirname "$0")/.."
exec docker run --rm -v "$PWD":/src -w /src hns-port \
  make linux BUILD=hns IS64BIT=1 PREFIX=aarch64-linux-gnu- \
  ARCH_CFLAGS="-march=armv8-a -fno-pic" \
  ARCH_ASFLAGS="-march=armv8-a" \
  -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)" "$@"
