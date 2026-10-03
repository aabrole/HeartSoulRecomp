#!/bin/sh
# Compiles every src/**/*.c file as 64-bit (native gcc in the arm64 hns-port
# image) with pointer-width warnings, to find code that is not 64-bit clean.
# Needs a normal port/build.sh run first (tools and generated headers).
# Usage: port/check64.sh [file.c ...]   Output: build/check64/*.log, summary.
set -e
cd "$(dirname "$0")/.."
exec docker run --rm -v "$PWD":/src -w /src hns-port sh -c '
set -u
OUT=build/check64
mkdir -p $OUT
FILES="$*"
[ -z "$FILES" ] && FILES=$(find src -name "*.c" ! -path "src/platform/*" | sort)
CPPF="-iquote include -Wno-trigraphs -DMODERN=1 -DTESTING=0 -DPOKEMON_HNS -std=gnu17 -D NONMATCHING -D PORTABLE -D PLATFORM_SDL2 -D RENDERER_EASY_DRAW -D UBFIX -D VER_64BIT"
CF="-x c -std=gnu17 -fsyntax-only -O2 -fno-builtin -DPORTABLE -DNONMATCHING -DUBFIX -DMODERN=1 -Werror=pointer-to-int-cast -Werror=int-to-pointer-cast -Wint-conversion -Wpointer-arith -Wno-trigraphs"
check() {
    f=$1
    log=$OUT/$(echo $f | tr / _).log
    cpp $CPPF $f 2>$log | tools/preproc/preproc -i $f charmap.txt 2>>$log | gcc $CF -o /dev/null - >>$log 2>&1 || true
    [ -s $log ] || rm -f $log
}
for f in $FILES; do
    check $f &
    while [ $(jobs -p | wc -l) -ge 12 ]; do sleep 0.1; done
done
wait
' sh "$@"
