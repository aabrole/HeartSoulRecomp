#!/bin/sh
# Builds the game's data for the Android app: runs the asset tools and the GNU
# assembler in the hns-port Docker image, then links every data object into
# build/android/game_data.o for the NDK build to pick up.
# Run before the first Gradle build and whenever data or assets change.
set -e
cd "$(dirname "$0")/.."
port/build.sh "$@"
docker run --rm -v "$PWD":/src -w /src hns-port sh -c '
  set -e
  mkdir -p build/android
  find build/pc32/asm build/pc32/data build/pc32/sound -name "*.o" | sort > build/android/objects.txt
  arm-linux-gnueabihf-ld -r -o build/android/game_data.tmp.o @build/android/objects.txt
  # Pointers in read-only data need load-time relocation, which Android only
  # allows in writable sections.
  arm-linux-gnueabihf-objcopy \
    --rename-section .rodata=.data,alloc,load,data,contents \
    --rename-section .text=.data,alloc,load,data,contents \
    build/android/game_data.tmp.o build/android/game_data.tmp2.o
  # lld cannot apply 8 and 16-bit relocations, so constants are resolved here.
  python3 port/resolve-abs-relocs.py build/android/game_data.tmp2.o build/android/game_data.o
  echo "objects: $(wc -l < build/android/objects.txt)"
  arm-linux-gnueabihf-readelf -S build/android/game_data.o | grep -E "^\s+\[" | head -20
'
