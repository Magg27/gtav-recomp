#!/bin/sh
# Unattended performance run: hidden native window, muted, uncapped. Boots into the world, plays
# tools/native/input_run_around.txt, then measures SECS seconds: per-thread CPU use (which thread
# is the bottleneck), the [native] timing lines, and (BENCH_PERF=1, needs
# kernel.perf_event_paranoid <= 1) a perf profile in logs/NAME.perf.
# usage: tools/native/bench.sh NAME [SECS] [game flags...]
# The run-around script gets into cars, and --gtav_vehicle_30fps (default on: the 360's handling
# pace) would then cap the run at 30 fps and count every frame as a hitch, so benches turn it off.
NAME=$1; SECS=${2:-30}; [ $# -ge 2 ] && shift 2 || shift $#
ROOT="$HOME/gtav-linux"
L="$ROOT/logs/$NAME.log"
# Command-line settings only: gtav_settings.ini is loaded after the command line and would put
# vsync (refresh cap) and the menu's other choices back.
export GTAV_IGNORE_SETTINGS=1
if pgrep -x gtav >/dev/null; then echo "game already running: not starting"; exit 1; fi
GO="$ROOT/logs/${NAME}_input.txt"
rm -f "$L" "$GO"
# Hold 1 GiB of GPU memory from before the boot until the game exits (BENCH_GPU_HOLD=0 to skip):
# another session's render queue starts a Blender render whenever the GPU has under 700 MiB in
# use, and the game's first boot seconds left that door open (11-15: most windows spoiled).
HOLD=""
if [ "${BENCH_GPU_HOLD:-1}" != 0 ]; then
  python3 "$ROOT/tools/native/gpu_hold.py" 1024 >"$ROOT/logs/${NAME}_hold.txt" 2>&1 & HOLD=$!
  for i in 1 2 3 4 5 6 7 8 9 10; do grep -q held "$ROOT/logs/${NAME}_hold.txt" 2>/dev/null && break; sleep 1; done
fi
unhold() { [ -n "$HOLD" ] && kill "$HOLD" 2>/dev/null; }
"$ROOT/play.sh" "$NAME" --gtav_native_fps --gtav_native_stats=true --gtav_native_hidden=true --audio_mute=true \
  --gtav_vehicle_30fps=false --input_script="$GO" "$@" >"$ROOT/logs/$NAME.stdout" 2>&1 &
PID=$!
S=$(date +%s)
# Boot time counts only while the game disk (BENCH_DISK, default sdb) is not saturated by another
# process: a busy USB disk turns a 37 s boot into 10+ minutes of loading screens (11-13), and that
# should not burn the whole timeout. Hard cap at 3x BENCH_BOOT_SECS regardless.
DISK=${BENCH_DISK:-sdb}; waited=0; busy_s=0
dticks() { awk -v d="$DISK" '$3 == d { print $13 }' /proc/diskstats; }
t0=$(dticks)
until grep -qE '\[native\] frame [0-9]+: [0-9]{3,} draws' "$L" 2>/dev/null; do
  sleep 5
  if ! kill -0 $PID 2>/dev/null; then echo "game exited before reaching the world"; unhold; exit 3; fi
  t1=$(dticks); if [ -n "$t1" ] && [ $(( (t1 - t0) / 50 )) -gt 40 ]; then busy_s=$((busy_s + 5)); else waited=$((waited + 5)); fi; t0=$t1
  if [ "$waited" -gt "${BENCH_BOOT_SECS:-420}" ] || [ $(( $(date +%s) - S )) -gt $(( 3 * ${BENCH_BOOT_SECS:-420} )) ]; then
    echo "no in-game frame after $(( $(date +%s) - S )) s (boot hang? disk busy for ${busy_s} s of it)"; kill -9 $PID; unhold; exit 2; fi
done
echo "in game after $(( $(date +%s) - S )) s (disk busy for ${busy_s} s of it)"
sleep 10
# BENCH_INPUT=none: stand still at the spawn point (steadier scenes for A/B runs); a file path:
# that input script (e.g. tools/native/input_stand_nudge.txt keeps the idle camera from panning).
case "${BENCH_INPUT:-run}" in
  none) ;;
  run) cp "$ROOT/tools/native/input_run_around.txt" "$GO" ;;
  *) cp "$BENCH_INPUT" "$GO" ;;
esac
sleep 5
# Per-thread CPU (utime + stime ticks) over the window, and the CPU the thread last ran on
# (P-cores 0-7 in hyperthread pairs, E-cores 8-11 on this laptop).
snap() { for t in /proc/$PID/task/*; do
  # comm may hold spaces and parentheses ("Main XThread (F..."): count fields after the last ')'.
  printf '%s %s ' "${t##*/}" "$(tr ' ' '_' <"$t/comm")"; sed 's/.*) //' "$t/stat" | awk '{print $12 + $13, $37}'; done; }
snap >"$ROOT/logs/${NAME}_cpu0.txt"
LINE0=$(wc -l <"$L")
# Another process on the GPU during the window (a Blender render from another session) makes the
# run worthless: sampled every 5 s into logs/NAME_gpu.txt, counted in logs/NAME.gpuc and the
# summary ("gpu contention: N of M samples"); bin_ab_linux.sh redoes such runs.
GPUF="$ROOT/logs/${NAME}_gpu.txt"; : >"$GPUF"
( while kill -0 $PID 2>/dev/null; do
    o=$(nvidia-smi --query-compute-apps=pid,process_name --format=csv,noheader 2>/dev/null | grep -v '/gtav' | grep -v "^$HOLD, " | awk -F', ' '{print $2}' | xargs -rn1 basename 2>/dev/null | sort -u | tr '\n' ',')
    echo "$(date +%H:%M:%S) ${o:-none}" >>"$GPUF"; sleep 5; done ) & GPUW=$!
if [ "${BENCH_PERF:-0}" = 1 ]; then
  perf record -F 499 ${BENCH_PERF_ARGS:--g} -p $PID -o "$ROOT/logs/$NAME.perf" -- sleep "$SECS" >/dev/null 2>&1
else
  sleep "$SECS"
fi
snap >"$ROOT/logs/${NAME}_cpu1.txt"
kill $GPUW 2>/dev/null; wait $GPUW 2>/dev/null
GPUC=$(grep -vc 'none$' "$GPUF"); echo "$GPUC" >"$ROOT/logs/$NAME.gpuc"
echo "gpu contention: $GPUC of $(wc -l <"$GPUF") samples$( [ "$GPUC" -gt 0 ] && echo " ($(grep -v 'none$' "$GPUF" | awk '{print $2}' | sort -u | tr '\n' ' '))")"
kill -9 $PID 2>/dev/null
unhold
sleep 2
HZ=$(getconf CLK_TCK)
echo "== CPU per thread over $SECS s (% of one core, top 12)"
awk -v hz=$HZ -v s=$SECS 'NR == FNR { a[$1] = $3; next } ($1 in a) { printf "%6.1f%%  %s (%s) cpu%s\n", ($3 - a[$1]) * 100 / hz / s, $2, $1, $4 }' \
  "$ROOT/logs/${NAME}_cpu0.txt" "$ROOT/logs/${NAME}_cpu1.txt" | sort -rn | head -12
echo "== timing lines in the window"
tail -n +"$LINE0" "$L" | grep -E '\[native\] (fps=|record ms|presented|  avg acquire|  textures per|command processor waited|fence copies)' | tail -14
echo "== summary"
python3 "$ROOT/tools/native/bench_stats.py" "$L" "$ROOT/logs/${NAME}_cpu0.txt" "$ROOT/logs/${NAME}_cpu1.txt" "$SECS"
