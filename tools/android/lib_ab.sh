#!/bin/bash
# Compare two game libraries on the phone. They cannot be swapped inside one run, so this alternates
# boots and reports figures that do not depend on the heat caps (instructions and cycles per frame
# by thread role, tools/android/lib_ab.py) next to fps and CPU ms.
#   tools/android/lib_ab.sh NAME B_LIBRARY ORDER...
# ORDER is a list of A (the library installed on the phone, kept as files/libgtav_game.so.phonebuilt)
# and B (B_LIBRARY, e.g. a tools/game_dll/build_game_dll.py --android build). Per letter: cooling
# wait (battery below COOL tenths of a degree, default 380), start muted, then WINDOWS (default 1)
# times: wait WARM seconds (default 150) and measure 40 s. WINDOWS=5 WARM=110 gives a 12-minute
# session per library: where each settles once the phone is heat-soaked. The scene differs by
# several hundred draws from boot to boot: compare at equal draws over several runs.
# Needs a debuggable APK (KEEP_DEBUGGABLE=1, simpleperf and run-as). Puts A back at the end
# (KEEP_B=1: B stays installed). Logs: logs/android/NAME_<letter><n>[_<window>].*
source ~/android-tools/env.sh
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PKG=com.gtavrecomp.app
D=/sdcard/Android/data/$PKG/files
name=$1 blib=$2
shift 2 2>/dev/null
[ -n "$name" ] && [ -f "$blib" ] && [ $# -gt 0 ] || { echo "usage: $0 NAME B_LIBRARY ORDER..."; exit 1; }
adb wait-for-device
[ -n "$(adb shell pidof $PKG | tr -d '\r')" ] && { echo "the game is running on the phone: not starting"; exit 1; }
adb shell "run-as $PKG sh -c '[ -f files/libgtav_game.so.phonebuilt ] || cp -p files/libgtav_game.so files/libgtav_game.so.phonebuilt'"
adb push "$blib" /data/local/tmp/gtav_game_b.so >/dev/null
adb shell chmod 644 /data/local/tmp/gtav_game_b.so
temp() { adb shell dumpsys battery | awk '/^  temperature:/ {print $2; exit}'; }
put() {  # A or B as the game's library
  if [ "$1" = A ]; then adb shell "run-as $PKG sh -c 'rm -f files/libgtav_game.so; cp -p files/libgtav_game.so.phonebuilt files/libgtav_game.so'"
  else adb shell "run-as $PKG sh -c 'rm -f files/libgtav_game.so; cp /data/local/tmp/gtav_game_b.so files/libgtav_game.so && chmod 500 files/libgtav_game.so'"; fi
}
mkdir -p "$ROOT/logs/android"
i=0
for side in "$@"; do
  put $side
  for w in $(seq 150); do [ "$(temp)" -lt ${COOL:-380} ] && break; sleep 10; done
  t0=$(temp)
  "$ROOT/tools/android/set_args.sh" --gtav_native_stats=true --audio_mute=true $EXTRA >/dev/null 2>&1
  KEEP=1 "$ROOT/tools/android/run.sh" ${WARM:-150} 0 >/dev/null 2>&1
  for k in $(seq ${WINDOWS:-1}); do
    run=${name}_${side}$i; [ ${WINDOWS:-1} -gt 1 ] && run=${run}_$k
    [ $k -gt 1 ] && sleep ${WARM:-150}
    [ -z "$(adb shell pidof $PKG | tr -d '\r')" ] && { echo "$run: THE GAME IS NOT RUNNING"; break; }
    W0=$(adb shell date "'+%Y-%m-%d %H:%M:%S'" | tr -d '\r')
    ATTACH=1 "$ROOT/tools/android/cpu_probe.sh" $run 0 40 >/dev/null 2>&1 &
    adb shell "simpleperf stat --app $PKG --per-thread --csv -e cpu-cycles:u,instructions:u --duration 40" > "$ROOT/logs/android/$run.stat" 2>&1
    wait
    W1=$(adb shell date "'+%Y-%m-%d %H:%M:%S'" | tr -d '\r')
    adb pull $D/logs/gtav.log "$ROOT/logs/android/$run.log" >/dev/null 2>&1
    caps=$(grep -E '^mid|^prime' "$ROOT/logs/android/$run.probe.txt" | sed 's/.*cap \([0-9.]* -> [0-9.]*\) of.*/\1/' | tr '\n' ' ')
    st=$(grep -E '^window' "$ROOT/logs/android/$run.probe.txt" | grep -o 'Thermal Status: [0-9]' | tail -1 | grep -o '[0-9]$')
    gpu=$(grep -E 'GPU frame' "$ROOT/logs/android/$run.log" | tail -1 | grep -o 'GPU frame [0-9.]* ms, idle before [0-9.]* ms')
    echo "$run (battery $t0 -> $(temp), thermal status $st, caps mid | prime: $caps| $gpu):"
    echo "    $(python3 "$ROOT/tools/android/lib_ab.py" "$ROOT/logs/android/$run" "$W0" "$W1" 2>&1)"
  done
  adb shell am force-stop $PKG
  i=$((i + 1))
done
[ "${KEEP_B:-0}" = 1 ] && put B || { put A; adb shell rm -f /data/local/tmp/gtav_game_b.so; }
"$ROOT/tools/android/set_args.sh" >/dev/null 2>&1
echo "done; game running: [$(adb shell pidof $PKG | tr -d '\r')]"
