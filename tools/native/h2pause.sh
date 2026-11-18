#!/bin/bash
# h2pause.sh CMD...: SIGSTOP the other GPU compute processes (another project's llama/python jobs)
# while CMD runs, SIGCONT them afterwards (also on error/interrupt). Their VRAM stays allocated:
# this frees GPU time, not memory. Owner OK 11-17 for the Halo 2 jobs. Skips our own gtav/gpu_hold.
PIDS=$(nvidia-smi --query-compute-apps=pid --format=csv,noheader 2>/dev/null | while read -r p; do
  c=$(tr '\0' ' ' < /proc/$p/cmdline 2>/dev/null); case "$c" in *gtav*|*gpu_hold.py*|"") ;; *) echo "$p";; esac; done)
resume() { [ -n "$PIDS" ] && kill -CONT $PIDS 2>/dev/null; echo "h2pause: resumed $PIDS $(date +%H:%M:%S)" >&2; }
trap resume EXIT
trap 'exit 130' INT TERM
[ -n "$PIDS" ] && kill -STOP $PIDS && echo "h2pause: stopped $PIDS $(date +%H:%M:%S)" >&2
"$@"
