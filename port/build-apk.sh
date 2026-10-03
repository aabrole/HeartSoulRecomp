#!/bin/sh
# Builds the Android debug APK. Pass --data to rebuild the game data first
# (needed on a fresh checkout and after data or asset changes), and --asan for
# an AddressSanitizer build that reports memory errors to logcat. --release
# builds the signed release APK (needs android/keystore.properties).
set -e
cd "$(dirname "$0")/.."
ASAN=false
VARIANT=Debug
for arg in "$@"; do
  [ "$arg" = "--asan" ] && ASAN=true
  [ "$arg" = "--release" ] && VARIANT=Release
done
if [ "$1" = "--data" ] || [ ! -f build/android/game_data.o ]; then
  port/android-data.sh
fi
[ -f android/local.properties ] || echo "sdk.dir=${ANDROID_HOME:-/opt/homebrew/share/android-commandlinetools}" > android/local.properties
cd android
JAVA_HOME="${JAVA_HOME:-/opt/homebrew/opt/openjdk@21}" ./gradlew --no-daemon -q -Phns.asan=$ASAN assemble$VARIANT
ls -la app/build/outputs/apk/*/*.apk
