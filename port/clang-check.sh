#!/bin/bash
# Parses every game source with clang for 32-bit ARM, the compiler family the
# Android NDK uses, and reports which files it rejects. Run inside the
# hns-port image from the repo root, after a normal build has generated assets.
out=${1:-/tmp/clang-check}
mkdir -p "$out"
check() {
  f=$1
  log="$2/$(echo "$f" | tr / _).log"
  arm-linux-gnueabihf-cpp -iquote include -Wno-trigraphs -DMODERN=1 -DTESTING=0 -DPOKEMON_HNS -std=gnu17 \
      -D NONMATCHING -D PORTABLE -D PLATFORM_SDL2 -D RENDERER_EASY_DRAW -D UBFIX "$f" 2>/dev/null \
    | tools/preproc/preproc -i "$f" charmap.txt 2>/dev/null \
    | clang --target=armv7a-linux-gnueabihf -x cpp-output -std=gnu17 -fsyntax-only -fno-builtin \
        -Wno-everything -ferror-limit=5 - > "$log" 2>&1 || echo "$f"
}
export -f check
find src -name '*.c' | sort | xargs -P "$(nproc)" -I{} bash -c 'check {} '"$out"
