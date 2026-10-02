#!/bin/sh
# Builds the Android debug APK. Pass --data to rebuild the game data first
# (needed on a fresh checkout and after data or asset changes).
set -e
cd "$(dirname "$0")/.."
if [ "$1" = "--data" ] || [ ! -f build/android/game_data.o ]; then
  port/android-data.sh
fi
[ -f android/local.properties ] || echo "sdk.dir=${ANDROID_HOME:-/opt/homebrew/share/android-commandlinetools}" > android/local.properties
cd android
JAVA_HOME="${JAVA_HOME:-/opt/homebrew/opt/openjdk@21}" ./gradlew --no-daemon -q assembleDebug
ls -la app/build/outputs/apk/debug/app-debug.apk
