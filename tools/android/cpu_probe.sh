#!/bin/bash
# Where the phone's CPU time goes while the game runs, in one run:
#   - per thread: CPU use, time spent runnable but waiting for a core (schedstat), switches a second
#   - per cluster: the clock speeds the cores actually ran at (cpufreq time_in_state) and the caps
#   - which cores each thread ran on, and the hottest functions of the busiest threads (simpleperf)
#   tools/android/cpu_probe.sh NAME [WARMUP_SECS] [SECS] [flags/env: for args.txt ...]
# Starts the game (muted, with stats), waits WARMUP_SECS (default 120), measures SECS (default 20),
# stops the game. Writes logs/android/NAME.probe.txt (summary), NAME.perf.txt, NAME.log.
# ATTACH=1: the game is already running (e.g. under ab.sh); only the per-thread and clock-speed
# part, over SECS seconds, without the profiler, and the game is left alone.
source ~/android-tools/env.sh
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PKG=com.gtavrecomp.app
D=/sdcard/Android/data/$PKG/files
name=$1 warm=${2:-120} secs=${3:-20}
shift 3 2>/dev/null || shift $#
[ -n "$name" ] || { echo "usage: $0 NAME [WARMUP] [SECS] [flags...]"; exit 1; }
adb wait-for-device
out=$ROOT/logs/android/$name
mkdir -p "$ROOT/logs/android"
if [ "${ATTACH:-0}" != 1 ]; then
  "$ROOT/tools/android/set_args.sh" --gtav_native_stats=true --audio_mute=true "$@"
  KEEP=1 "$ROOT/tools/android/run.sh" "$warm" 0 >/dev/null
fi
pid=$(adb shell pidof $PKG | tr -d '\r')
[ -n "$pid" ] || { echo "the game is not running after $warm s"; exit 1; }
snap() {  # every thread's name, stat and schedstat; every cluster's time at each clock speed
  adb shell "date +%s.%N; for t in /proc/$pid/task/*; do echo \"T \${t##*/} \$(cat \$t/comm) @ \$(cat \$t/schedstat) @ \$(cat \$t/stat)\"; done 2>/dev/null
    for p in /sys/devices/system/cpu/cpufreq/policy*; do echo \"P \${p##*policy} cap \$(cat \$p/scaling_max_freq) max \$(cat \$p/cpuinfo_max_freq)\"; sed \"s/^/F \${p##*policy} /\" \$p/stats/time_in_state; done
    dumpsys battery | grep -m1 '^  temperature'; dumpsys thermalservice | grep -m1 'Thermal Status'"
}
snap > "$out.snap0"
if [ "${ATTACH:-0}" = 1 ]; then
  sleep "$secs"
  snap > "$out.snap1"
  rm -f "$out.cpus"
  python3 "$ROOT/tools/android/cpu_probe.py" "$out" | tee "$out.probe.txt"
  exit 0
fi
adb shell "simpleperf record --app $PKG -e cpu-clock -f 2000 --call-graph fp --duration $secs -o /data/local/tmp/gtav.perf" >/dev/null 2>&1
snap > "$out.snap1"
adb shell am force-stop $PKG
adb pull "$D/logs/gtav.log" "$out.log" >/dev/null
rep() { adb shell "simpleperf report -i /data/local/tmp/gtav.perf $*" 2>/dev/null | sed -n '/Overhead/,$p'; }
# samples per thread on each core ("cpu count tid" lines)
for c in $(adb shell 'ls -d /sys/devices/system/cpu/cpu[0-9]*' | sed 's/.*cpu//' | tr -d '\r'); do
  rep --cpu $c --sort tid --print-event-count | awk -v c=$c 'NR>1 && $3 ~ /^[0-9]+$/ {print c, $2, $3}'
done > "$out.cpus"
{
  echo "== threads"; rep --sort tid,comm --percent-limit 0.7
  for tid in $(rep --sort tid --percent-limit 2.5 | awk 'NR>1 && $2 ~ /^[0-9]+$/ {print $2}' | head -9); do
    echo "== thread $tid $(awk -v t=$tid '$1=="T" && $2==t {print $3,$4; exit}' "$out.snap0")"
    rep --tids $tid --sort dso,symbol --percent-limit 0.8 | cut -c1-180
  done
} > "$out.perf.txt"
python3 "$ROOT/tools/android/cpu_probe.py" "$out" | tee "$out.probe.txt"
grep -E "\[native\] (frame [0-9]+:|record ms)" "$out.log" | tail -2 | sed 's/.*\[native\] //' | cut -c1-230
"$ROOT/tools/android/set_args.sh" >/dev/null 2>&1
