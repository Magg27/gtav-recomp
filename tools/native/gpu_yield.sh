#!/bin/bash
# gpu_yield.sh CMD...: run CMD with the Humanizer GPU jobs (h2work/guard.sh) switched off through
# their own worker-mode file, which stops them and frees their ~5 GB of VRAM; the previous mode
# is restored afterwards (also on error/interrupt). Owner OK 11-17 to pause that job for tests.
M=/run/media/nakas/lhd-unreal/h2work/worker-mode
OLD=$(cat "$M" 2>/dev/null)
restore() { [ -n "$OLD" ] && echo "$OLD" > "$M"; echo "gpu_yield: worker-mode restored to '$OLD' $(date +%H:%M:%S)" >&2; }
if [ "$OLD" = on ]; then
  trap restore EXIT; trap 'exit 130' INT TERM
  echo off > "$M"; echo "gpu_yield: worker-mode off $(date +%H:%M:%S)" >&2
  for i in $(seq 60); do pgrep -x llama-server >/dev/null || pgrep -f gpu_dual_worker.py >/dev/null || break; sleep 1; done
fi
"$@"
