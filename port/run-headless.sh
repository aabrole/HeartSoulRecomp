#!/bin/sh
# Runs the 32-bit ARM Linux build without a display, under qemu, and leaves
# screenshots in port/out. Environment variables are documented in
# src/platform/sdl2.c (HNS_INPUT, HNS_SHOTS, HNS_SHOT_EVERY, HNS_STATE_DUMP, HNS_TAP,
# HNS_WAV, HNS_AUDIO_LOG, HNS_GBS, HNS_SONG). HNS_WAV is a path relative to port/out.
# Usage: port/run-headless.sh FRAMES
set -e
cd "$(dirname "$0")/.."
mkdir -p port/out
exec docker run --rm -v "$PWD":/src -w /src/port/out \
  -e SDL_VIDEODRIVER=dummy -e SDL_AUDIODRIVER=dummy \
  -e QEMU_LD_PREFIX=/ -e HNS_HEADLESS_FRAMES="${1:-600}" -e HNS_INPUT -e HNS_SHOTS -e HNS_SHOT_EVERY -e HNS_TEST_BATTLE -e HNS_STATE_DUMP -e HNS_TAP -e HNS_TIMEOUT -e HNS_WAV -e HNS_AUDIO_LOG -e HNS_GBS -e HNS_SONG \
  hns-port sh -c 'ulimit -c 0; timeout -s KILL ${HNS_TIMEOUT:-600} qemu-arm /src/pokehns32; echo "exit code $?"'
