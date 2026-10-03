#!/bin/sh
# Builds the game's data for the Android app: runs the asset tools and the GNU
# assembler in the hns-port Docker image, then links every data object into
# one object per ABI for the NDK build to pick up:
#   build/android/game_data.o     armeabi-v7a (from the 32-bit build)
#   build/android/game_data64.o   arm64-v8a (from the 64-bit build)
# Run before the first Gradle build and whenever data or assets change.
set -e
cd "$(dirname "$0")/.."
port/build.sh "$@"
port/build64.sh "$@"
docker run --rm -v "$PWD":/src -w /src hns-port sh -c '
  set -e
  mkdir -p build/android
  # $1 build dir, $2 binutils prefix, $3 output name
  link_data() {
    find $1/asm $1/data $1/sound -name "*.o" | sort > build/android/$3.objects.txt
    $2ld -r -o build/android/$3.tmp.o @build/android/$3.objects.txt
    # Pointers in read-only data need load-time relocation, which Android only
    # allows in writable sections.
    $2objcopy \
      --rename-section .rodata=.data,alloc,load,data,contents \
      --rename-section .text=.data,alloc,load,data,contents \
      build/android/$3.tmp.o build/android/$3.tmp2.o
    echo "$3: $(wc -l < build/android/$3.objects.txt) objects"
  }
  link_data build/pc32 arm-linux-gnueabihf- game_data
  # lld rejects the narrow relocations against constants (16-bit ids), so
  # they are resolved here.
  python3 port/resolve-abs-relocs.py build/android/game_data.tmp2.o build/android/game_data.o
  link_data build/pc64 aarch64-linux-gnu- game_data64
  python3 port/resolve-abs-relocs.py build/android/game_data64.tmp2.o build/android/game_data64.o
  arm-linux-gnueabihf-readelf -S build/android/game_data.o | grep -E "^\s+\[" | head -20
  aarch64-linux-gnu-readelf -S build/android/game_data64.o | grep -E "^\s+\[" | head -20
'
