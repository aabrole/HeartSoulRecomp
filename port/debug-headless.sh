#!/bin/sh
# Runs the headless build with gdb attached and prints a backtrace at the
# first fault. HNS_BIN=pokehns64 debugs the 64-bit build natively; the default
# pokehns32 runs under qemu.
# Usage: port/debug-headless.sh FRAMES [extra gdb -ex args]
set -e
cd "$(dirname "$0")/.."
mkdir -p port/out
FRAMES="${1:-600}"; shift || true
if [ "${HNS_BIN:-pokehns32}" = pokehns64 ]; then
  exec docker run --rm -v "$PWD":/src -w /src/port/out \
    -e SDL_VIDEODRIVER=dummy -e SDL_AUDIODRIVER=dummy \
    -e TZ -e HNS_HEADLESS_FRAMES="$FRAMES" -e HNS_INPUT -e HNS_SHOTS -e HNS_SHOT_EVERY -e HNS_TEST_BATTLE -e HNS_GIVE_MON -e HNS_TEST_BATTLE_KIND -e HNS_WARP -e HNS_WIDESCREEN -e HNS_TALL -e HNS_CLOCK \
    -e TAILN hns-port sh -c 'ulimit -c 0; gdb -q -batch \
      -ex "handle SIG32 SIG33 SIGUSR1 nostop noprint pass" -ex run -ex "thread apply all bt" -ex "info registers pc x30 sp x0 x1 x2 x3" \
      -ex "x/6i \$pc-8" "$@" /src/pokehns64 2>&1 | grep -v "^warning\|^\[New\|^\[Thread\|Reading symbols" | tail -${TAILN:-40}' sh "$@"
fi
exec docker run --rm -v "$PWD":/src -w /src/port/out \
  -e SDL_VIDEODRIVER=dummy -e SDL_AUDIODRIVER=dummy \
  -e TZ -e QEMU_LD_PREFIX=/ -e HNS_HEADLESS_FRAMES="$FRAMES" -e HNS_INPUT -e HNS_SHOTS -e HNS_SHOT_EVERY -e HNS_TEST_BATTLE -e HNS_GIVE_MON -e HNS_TEST_BATTLE_KIND -e HNS_WARP -e HNS_WIDESCREEN -e HNS_TALL -e HNS_CLOCK \
  -e TAILN hns-port sh -c 'ulimit -c 0; qemu-arm -g 1234 /src/pokehns32 & sleep 1; gdb-multiarch -q -batch \
    -ex "set sysroot /" -ex "set solib-search-path /usr/lib/arm-linux-gnueabihf:/usr/arm-linux-gnueabihf/lib" -ex "file /src/pokehns32" -ex "target remote :1234" \
    -ex "handle SIG32 SIG33 SIGUSR1 nostop noprint pass" -ex continue -ex "thread apply all bt" -ex "info registers pc lr sp r0 r1 r2 r3" \
    -ex "x/6i \$pc-8" "$@" 2>&1 | grep -v "^warning\|^\[New\|^\[Thread\|Reading symbols" | tail -${TAILN:-40}' sh "$@"
