# Build and headless-test environment for the native port.
# 32-bit ARM Linux (armhf) is the closest desktop stand-in for the Android
# armeabi-v7a target: same pointer size and same data layout as the GBA.
# The image itself is arm64, so the native toolchain and libsdl2-dev build and
# run the 64-bit (arm64-v8a) stand-in without qemu.
FROM debian:bookworm
RUN dpkg --add-architecture armhf && apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential git make pkg-config python3 ca-certificates \
      libpng-dev zlib1g-dev \
      gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf \
      libsdl2-dev:armhf libc6:armhf libsdl2-dev gdb \
      qemu-user gdb-multiarch clang procps \
      gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
