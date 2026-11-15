#!/bin/bash
# bin_ab_linux.sh NAME BIN_A BIN_B [ROUNDS=3] [SECS=60] [game flags...]
# Two-binary A/B of the SDK runtime on Linux: alternates the game binaries BIN_A and BIN_B (paths
# copied over build/gtav/gtav while the game is NOT running), runs
# bench.sh for each (same input script, same length), and prints one bench_stats line per run plus
# the per-side means. Interleaved A B, B A, A B ... so drift (thermal, other jobs) hits both sides.
# BENCH_INPUT=none for standing still. The original .so files are restored at the end.
N=$1; A=$2; B=$3; ROUNDS=${4:-3}; SECS=${5:-60}; [ $# -ge 5 ] && shift 5 || shift $#
ROOT="$HOME/gtav-linux"; G="$ROOT/build/gtav"; LIBS="gtav"
if pgrep -x gtav >/dev/null; then echo "game running: not starting"; exit 1; fi
for d in "$A" "$B"; do [ -f "$d" ] || { echo "missing $d"; exit 1; }; done
SAVE=$(mktemp -d "$ROOT/logs/bin_ab_save.XXXX"); for l in $LIBS; do cp -p "$G/$l" "$SAVE/"; done
i=0
while [ $i -lt "$ROUNDS" ]; do
  # A B, then B A: a drift that runs one way (load from other sessions, thermal) no longer favours
  # the side that always runs first.
  order="A B"; [ $((i % 2)) = 1 ] && order="B A"
  for side in $order; do
    [ $side = A ] && d=$A || d=$B
    run="${N}_${side}${i}"
    pgrep -x gtav >/dev/null && { echo "game still running before $run"; break 2; }
    # Each run waits for a quiet machine (another session's GPU render may have started since the
    # last one) and is redone when something else used the GPU during its window or the game
    # never reached the world; the spoiled attempt's log is kept as NAME.dirtyK.log.
    try=0
    while :; do
      "$ROOT/tools/native/quiet_gate.sh" "${BENCH_GATE_LOAD:-5}" "${BENCH_GATE_SECS:-21600}" "${BENCH_DISK:-sdb}" >&2 || { echo "machine not quiet: giving up before $run" >&2; break 3; }
      cp -p "$d" "$G/gtav"
      "$ROOT/tools/native/bench.sh" "$run" "$SECS" "$@" >"$ROOT/logs/$run.bench" 2>&1; rc=$?
      gpuc=$(cat "$ROOT/logs/$run.gpuc" 2>/dev/null || echo 0); gput=$(wc -l <"$ROOT/logs/${run}_gpu.txt" 2>/dev/null || echo 1)
      # ab_fit.py drops the contaminated seconds of a run; only a run that lost more than
      # BENCH_DIRTY_PCT (40) percent of its window is redone.
      [ $rc = 0 ] && [ $((gpuc * 100)) -le $((gput * ${BENCH_DIRTY_PCT:-40})) ] && break
      try=$((try + 1)); echo "$run spoiled (rc $rc, gpu contention $gpuc of $gput samples): attempt $try" >&2
      for f in "$ROOT/logs/$run".* "$ROOT/logs/${run}_"*; do case "$f" in *.dirty*|*_gpuwatch*) ;; *) mv "$f" "${f/$run/$run.dirty$try}";; esac; done 2>/dev/null
      [ $try -ge "${BENCH_RETRIES:-3}" ] && { echo "$run: giving up after $try spoiled attempts" >&2; break 3; }
      sleep 10
    done
    python3 "$ROOT/tools/native/bench_stats.py" "$ROOT/logs/$run.log" "$ROOT/logs/${run}_cpu0.txt" "$ROOT/logs/${run}_cpu1.txt" "$SECS" --csv | sed "s/^/$side,/"
    sleep 3
  done
  i=$((i + 1))
done | tee "$ROOT/logs/${N}_ab.csv"
for l in $LIBS; do cp -p "$SAVE/$l" "$G/$l"; done; rm -rf "$SAVE"
echo "== per side (fps mean, 1% low, record ms, GPU ms, hitches)"
awk -F, '{n[$1]++; f[$1]+=$3; l[$1]+=$5; r[$1]+=$9; g[$1]+=$10; h[$1]+=$13; if($3>mx[$1])mx[$1]=$3; if(!($1 in mn)||$3<mn[$1])mn[$1]=$3}
  END{for(s in n) printf "%s: %.1f fps (min %.1f max %.1f), 1%% low %.1f, record %.2f, GPU %.2f, hitches %.1f (%d runs)\n", s, f[s]/n[s], mn[s], mx[s], l[s]/n[s], r[s]/n[s], g[s]/n[s], h[s]/n[s], n[s]}' "$ROOT/logs/${N}_ab.csv"
