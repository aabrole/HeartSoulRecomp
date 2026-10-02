#!/bin/sh
# Runs the headless build under qemu, interrupts it after SECONDS and prints
# where it is. For finding hangs. Usage: port/where-headless.sh [SECONDS]
set -e
cd "$(dirname "$0")/.."
mkdir -p port/out
exec docker run --rm -v "$PWD":/src -w /src/port/out \
  -e SDL_VIDEODRIVER=dummy -e SDL_AUDIODRIVER=dummy -e QEMU_LD_PREFIX=/ \
  -e HNS_HEADLESS_FRAMES=100000 -e HNS_INPUT -e WAIT="${1:-15}" \
  hns-port sh -c 'ulimit -c 0; qemu-arm -g 1234 /src/pokehns32 & sleep 1;
    gdb-multiarch -q -batch -ex "set sysroot /" -ex "file /src/pokehns32" -ex "target remote :1234" \
    -ex "handle SIG32 SIG33 SIGUSR1 nostop noprint pass" -ex continue -ex bt -ex "info registers pc lr sp" \
    -ex "x/12i \$pc-16" -ex kill > /tmp/gdb.log 2>&1 &
    GDB=$!; sleep $WAIT; kill -INT $GDB; sleep 3; kill -KILL $GDB 2>/dev/null;
    grep -v "^warning\|^\[New\|^\[Thread\|Reading symbols\|^Do you need" /tmp/gdb.log | tail -30'
