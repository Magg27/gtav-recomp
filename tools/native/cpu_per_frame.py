#!/usr/bin/env python3
"""cpu_per_frame.py LOG... : CPU ms per frame of the hot threads (from bench.sh's NAME_cpu0/1.txt
thread snapshots and the bench_stats fps), plus draws. CPU time excludes time the thread was
preempted by other processes, so it is steadier than fps on a shared, loaded machine."""
import re, subprocess, sys, os
HZ = os.sysconf('SC_CLK_TCK')
for log in sys.argv[1:]:
    base = log[:-4]
    st = subprocess.run([sys.executable, '-I', os.path.join(os.path.dirname(__file__), 'bench_stats.py'), log],
                        capture_output=True, text=True).stdout
    m = re.search(r'([0-9.]+) fps \(sd [0-9.]+, ([0-9]+) s\).*?([0-9]+) draws', st)
    if not m: print(log, 'no stats'); continue
    fps, secs, draws = float(m[1]), float(m[2]), int(m[3])
    def snap(p):
        d = {}
        for l in open(p):
            f = l.split()
            if len(f) >= 3: d[f[0]] = (f[1], int(f[2]))
        return d
    a, b = snap(base + '_cpu0.txt'), snap(base + '_cpu1.txt')
    per = {}
    for tid, (name, t1) in b.items():
        if tid in a: per[name] = per.get(name, 0) + (t1 - a[tid][1])
    # snapshots bracket the whole window; use the window length from bench.sh's SECS ~= secs
    frames = fps * secs
    top = sorted(per.items(), key=lambda kv: -kv[1])[:3]
    print(f"{os.path.basename(log):16s} {draws:5d} draws {fps:5.1f} fps  " +
          '  '.join(f"{n[:14]} {1000.0 * t / HZ / frames:5.2f}" for n, t in top) + ' ms/frame')
