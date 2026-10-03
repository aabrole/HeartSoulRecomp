#!/bin/sh
# Runs the Linux build without a display and leaves screenshots in port/out.
# HNS_BIN picks the binary: pokehns32 (default) runs under qemu, pokehns64
# (from port/build64.sh) runs natively in the arm64 image. Environment variables are documented in
# src/platform/sdl2.c (HNS_INPUT, HNS_SHOTS, HNS_SHOT_EVERY, HNS_TEST_BATTLE, HNS_WIDESCREEN,
# HNS_WARP, HNS_STATE_DUMP, HNS_TAP, HNS_WAV, HNS_AUDIO_LOG, HNS_GBS, HNS_SONG,
# HNS_LAYER_DEBUG, HNS_LAYER_HIDE, HNS_CLOCK).
# HNS_WAV is a path relative to port/out.
# Usage: port/run-headless.sh FRAMES
set -e
cd "$(dirname "$0")/.."
HNS_BIN="${HNS_BIN:-pokehns32}"
case "$HNS_BIN" in
  *64) RUNNER="" ;;
  *) RUNNER="qemu-arm" ;;
esac
mkdir -p port/out
exec docker run --rm -v "$PWD":/src -w /src/port/out \
  -e SDL_VIDEODRIVER=dummy -e SDL_AUDIODRIVER=dummy \
  -e TZ -e QEMU_LD_PREFIX=/ -e HNS_HEADLESS_FRAMES="${1:-600}" -e HNS_INPUT -e HNS_SHOTS -e HNS_SHOT_EVERY -e HNS_TEST_BATTLE -e HNS_GIVE_MON -e HNS_TEST_BATTLE_KIND -e HNS_STATE_DUMP -e HNS_TAP -e HNS_WIDESCREEN -e HNS_WARP -e HNS_TIMEOUT -e HNS_WAV -e HNS_AUDIO_LOG -e HNS_GBS -e HNS_SONG -e HNS_LAYER_DEBUG -e HNS_LAYER_HIDE -e HNS_CLOCK \
  -e HNS_BIN="$HNS_BIN" -e RUNNER="$RUNNER" \
  hns-port sh -c 'ulimit -c 0; timeout -s KILL ${HNS_TIMEOUT:-600} $RUNNER /src/$HNS_BIN; echo "exit code $?"'
