#!/usr/bin/env python3
"""cpu_probe.py PREFIX: summary of PREFIX.snap0/.snap1/.cpus written by cpu_probe.sh.

Per thread over the window: busy % (CPU time / wall), wait % (runnable but not on a core, from
schedstat), switches a second, and the share of its samples on the little / mid / prime cores.
Per cluster: the cap, and the share of time at each clock speed."""
import re, sys, collections

pre = sys.argv[1]


def snap(path):
    t0, threads, pol, freq, extra = None, {}, {}, collections.defaultdict(dict), []
    for line in open(path, errors="replace"):
        line = line.rstrip("\n")
        if t0 is None and re.fullmatch(r"\d+\.\d+", line.strip()):
            t0 = float(line)
        elif line.startswith("T "):
            head, sched, stat = line[2:].split(" @ ", 2)
            tid, comm = head.split(" ", 1)
            s = sched.split()
            rest = stat[stat.rindex(")") + 2:].split()  # fields from "state" on
            threads[int(tid)] = dict(comm=comm, run=int(s[0]), wait=int(s[1]), slices=int(s[2]),
                                     cpu=(int(rest[11]) + int(rest[12])), last=int(rest[36]))
        elif line.startswith("P "):
            f = line.split()
            pol[f[1]] = (int(f[3]), int(f[5]))
        elif line.startswith("F "):
            f = line.split()
            freq[f[1]][int(f[2])] = int(f[3])
        elif "temperature" in line or "Thermal" in line:
            extra.append(line.strip())
    return t0, threads, pol, freq, extra


a = snap(pre + ".snap0")
b = snap(pre + ".snap1")
wall = b[0] - a[0]
print(f"window {wall:.1f} s; {'; '.join(a[4])} -> {'; '.join(b[4])}")

# which cores each thread's samples were on
clusters = {}  # cpu -> cluster name, by cpufreq policy order
names = ["little", "mid", "prime", "c3"]
pols = sorted(b[2], key=int)
bounds = [int(p) for p in pols] + [64]
for i, p in enumerate(pols):
    for c in range(bounds[i], bounds[i + 1]):
        clusters[c] = names[i]
share = collections.defaultdict(lambda: collections.Counter())
try:
    for line in open(pre + ".cpus"):
        f = line.split()
        if len(f) == 3:
            share[int(f[2])][clusters.get(int(f[0]), "?")] += float(f[1])
except FileNotFoundError:
    pass

rows = []
for tid, t1 in b[1].items():
    t0 = a[1].get(tid)
    if not t0:
        continue
    run = (t1["run"] - t0["run"]) / 1e9
    wait = (t1["wait"] - t0["wait"]) / 1e9
    sl = t1["slices"] - t0["slices"]
    rows.append((run, wait, sl, tid, t1["comm"]))
rows.sort(reverse=True)
tot_run = sum(r[0] for r in rows)
tot_wait = sum(r[1] for r in rows)
print(f"all threads: {tot_run / wall:.2f} cores busy, {tot_wait / wall:.2f} cores' worth waiting for a core")
print(f"{'tid':>6} {'thread':<16} {'busy%':>6} {'wait%':>6} {'wait/busy':>9} {'sw/s':>6} {'ms/run':>6}  cores (share of its samples)")
for run, wait, sl, tid, comm in rows:
    if run / wall < 0.015 and sl / wall < 300:
        continue
    s = share.get(tid)
    tot = sum(s.values()) if s else 0
    where = " ".join(f"{k} {100 * v / tot:.0f}%" for k, v in sorted(s.items(), key=lambda kv: -kv[1])) if tot else ""
    print(f"{tid:>6} {comm:<16} {100 * run / wall:6.1f} {100 * wait / wall:6.1f} {wait / run if run else 0:9.2f} "
          f"{sl / wall:6.0f} {1000 * run / sl if sl else 0:6.2f}  {where}")

for i, p in enumerate(pols):
    cap0, mx = a[2][p]
    cap1, _ = b[2][p]
    d = {f: b[3][p][f] - a[3][p].get(f, 0) for f in b[3][p]}
    tot = sum(d.values()) or 1
    mean = sum(f * v for f, v in d.items()) / tot
    top = sorted(d.items(), key=lambda kv: -kv[1])[:5]
    print(f"{names[i]:<6} (cpu{p}+): cap {cap0 / 1e6:.2f} -> {cap1 / 1e6:.2f} of {mx / 1e6:.2f} GHz; mean clock {mean / 1e6:.2f} GHz; "
          + ", ".join(f"{f / 1e6:.2f} GHz {100 * v / tot:.0f}%" for f, v in top if v))
