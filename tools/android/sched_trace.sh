#!/bin/bash
# The scheduler's view of the running game on the phone: who waits how long for a core, how long a
# woken thread takes to run, what the main thread waits for.
#   tools/android/sched_trace.sh NAME [SECS]
# The game must be running already (e.g. under tools/android/ab.sh or cpu_probe.sh). Records SECS
# (default 4) of atrace (sched, freq, idle) and prints tools/android/sched_trace.py's summary;
# keeps logs/android/NAME.sched.txt (the summary), not the trace (tens of MB).
source ~/android-tools/env.sh
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PKG=com.gtavrecomp.app
name=$1 secs=${2:-4}
[ -n "$name" ] || { echo "usage: $0 NAME [SECS]"; exit 1; }
pid=$(adb shell pidof $PKG | tr -d '\r')
[ -n "$pid" ] || { echo "the game is not running"; exit 1; }
tmp=$(mktemp)
adb shell "atrace -b 32768 -t $secs sched freq idle" > "$tmp" 2>/dev/null
python3 "$ROOT/tools/android/sched_trace.py" "$tmp" "$pid" | tee "$ROOT/logs/android/$name.sched.txt"
[ -n "$KEEP_TRACE" ] && cp "$tmp" "$ROOT/logs/android/$name.sched.trace"
rm -f "$tmp"
