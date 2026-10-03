#!/system/bin/sh
# Starts the app with the AddressSanitizer runtime preloaded (--asan builds).
HERE="$(cd "$(dirname "$0")" && pwd)"
export ASAN_OPTIONS=log_to_syslog=true,allow_user_segv_handler=1,detect_leaks=0,halt_on_error=0
export LD_PRELOAD="$HERE/libclang_rt.asan-aarch64-android.so"
exec "$@"
