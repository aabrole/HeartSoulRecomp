#!/bin/bash
# Parses every game source with the Android NDK's clang for armeabi-v7a and
# lists the files it rejects. Run on the Mac from the repo root after a normal
# build has generated the assets. Logs go to port/out/clang-check/.
NDK=${ANDROID_NDK:-/opt/homebrew/share/android-commandlinetools/ndk/27.2.12479018}
CL=$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/clang
out=port/out/clang-check
mkdir -p "$out" port/out/mac
[ -x port/out/mac/preproc ] || c++ -std=c++11 -O2 -w tools/preproc/*.cpp -o port/out/mac/preproc
FLAGS="--target=armv7a-linux-androideabi21 -marm -iquote include -I/opt/homebrew/include -Wno-trigraphs -DMODERN=1 -DTESTING=0 -DPOKEMON_HNS -std=gnu17 -DNONMATCHING -DPORTABLE -DPLATFORM_SDL2 -DRENDERER_EASY_DRAW -DUBFIX"
check() {
  f=$1
  log="$out/$(echo "$f" | tr / _).log"
  $CL $FLAGS -E "$f" 2>"$log" | port/out/mac/preproc -i "$f" charmap.txt 2>>"$log" \
    | $CL $FLAGS -x cpp-output -fsyntax-only -fno-builtin -Wno-everything -ferror-limit=8 - >>"$log" 2>&1 || echo "$f"
}
export -f check; export CL FLAGS out
find src -name '*.c' | sort | xargs -P 10 -I{} bash -c 'check {}'
