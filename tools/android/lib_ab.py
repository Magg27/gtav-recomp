#!/usr/bin/env python3
"""lib_ab.py PREFIX W0 W1: what the game's threads cost per frame over one probe window.

PREFIX.stat (simpleperf stat --per-thread --csv, user mode), PREFIX.probe.txt (cpu_probe.sh
table), PREFIX.log (game log); W0/W1 = 'YYYY-MM-DD HH:MM:SS' phone time around the window.
Prints fps, draws, clocks, then per thread role (main, job threads, audio mixer, the game's render
thread, other game threads; the host's record thread apart):
  - million instructions and million cycles per frame: independent of the heat caps, so two
    libraries can be compared across boots. Threads that spin while they wait (main, render) count
    their spinning too; the job threads' figure is the clean one. Audio is also given per second,
    since its work does not depend on the frame rate or the scene.
  - CPU ms per frame (run time from schedstat; depends on the clock the cores were capped at)."""
import collections
import re
import sys

pre, w0, w1 = sys.argv[1:4]
fr = []
for l in open(pre + ".log", errors="replace"):
    m = re.match(r"\[(\S+ \S+?)\.(\d+)\].*\[native\] frame (\d+): (\d+) draws", l)
    if m and w0 <= m.group(1) <= w1:
        h, mi, s = m.group(1).split()[1].split(":")
        fr.append((int(h) * 3600 + int(mi) * 60 + int(s) + int(m.group(2)) / 1000, int(m.group(3)), int(m.group(4))))
if len(fr) < 2:
    sys.exit("no frame lines in the window")
fps = (fr[-1][1] - fr[0][1]) / (fr[-1][0] - fr[0][0])
draws = sum(f[2] for f in fr) / len(fr)


def role(name):
    g = re.match(r"XThread\dp([+-]\d\d)", name)
    if name.startswith("Main XThread"):
        return "main"
    if name == "gtav-record":
        return "record"
    if g and 1 <= int(g.group(1)) <= 15:
        return "jobs"
    if g and int(g.group(1)) >= 16:
        return "audio"
    if name.startswith("XThread3p-0"):
        return "render"
    return "other" if g else "host"


G = ("main", "jobs", "audio", "render", "other")
ins, cyc, busy, clocks, secs = collections.Counter(), collections.Counter(), collections.Counter(), [], None
try:
    for l in open(pre + ".stat", errors="replace"):
        f = l.strip().split(",")
        if len(f) >= 5 and f[1].isdigit() and f[3].isdigit():
            (ins if f[4].startswith("instructions") else cyc)[role(f[0])] += int(f[3])
        elif l.startswith("Total test time"):
            secs = float(f[1])
except FileNotFoundError:
    pass
for l in open(pre + ".probe.txt", errors="replace"):
    m = re.match(r"\s*\d+\s+(.{1,16}?)\s+([\d.]+)\s+([\d.]+)\s+[\d.]+\s+\d+", l)
    if m:
        busy[role(m.group(1).strip())] += float(m.group(2))
    m = re.match(r"(little|mid|prime).*mean clock ([\d.]+) GHz", l)
    if m:
        clocks.append(m.group(2))
out = f"{fps:5.1f} fps {draws:5.0f} draws, clocks {'/'.join(clocks)} GHz"
if secs and ins:
    n = fps * secs
    out += (" | M instr/frame: " + " ".join(f"{k} {ins[k] / n / 1e6:.1f}" for k in G)
            + f" = game {sum(ins[k] for k in G) / n / 1e6:.1f}; record {ins['record'] / n / 1e6:.1f}"
            + " | M cycles/frame: " + " ".join(f"{k} {cyc[k] / n / 1e6:.1f}" for k in G)
            + f" = game {sum(cyc[k] for k in G) / n / 1e6:.1f}; record {cyc['record'] / n / 1e6:.1f}"
            + f" | audio per second: {ins['audio'] / secs / 1e6:.0f} M instr, {cyc['audio'] / secs / 1e6:.0f} M cycles")
out += (" | CPU ms/frame: " + " ".join(f"{k} {10 * busy[k] / fps:.2f}" for k in G)
        + f" = game {sum(10 * busy[k] / fps for k in G):.2f}; record {10 * busy['record'] / fps:.2f}")
print(out)
