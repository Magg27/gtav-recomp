#!/usr/bin/env python3
"""ab_fit.py A:log [A:log ...] B:log ... [--skip N]

Scene-drift-proof A/B of per-second bench lines. Standing at the spawn point the idle camera pans,
so the guest draw count drifts 20-40% between runs and raw fps means compare scenes, not builds.
Frame time is close to linear in draws (ms = a + b * draws): this fits a and b per side over all
its runs (least squares, pooled) and reports the fitted fps at fixed draw counts, plus each run's
own fit. The window starts at the first in-world line (>= 300 draws; loading screens draw nothing
at 400 fps) and skips --skip more lines (default 15, settling), like bench_stats.py."""
import re, sys
skip = 15
args = sys.argv[1:]
if '--skip' in args:
    i = args.index('--skip'); skip = int(args[i + 1]); del args[i:i + 2]
pat = re.compile(r'fps=([0-9.]+) avg=([0-9.]+)ms.*drawidx=([0-9]+)')
sides = {}
def fit(pts):
    n = len(pts); sx = sum(p[0] for p in pts); sy = sum(p[1] for p in pts)
    sxx = sum(p[0] * p[0] for p in pts); sxy = sum(p[0] * p[1] for p in pts)
    d = n * sxx - sx * sx
    if n < 3 or d == 0: return None
    b = (n * sxy - sx * sy) / d; a = (sy - b * sx) / n
    res = sum((p[1] - a - b * p[0]) ** 2 for p in pts) / max(n - 2, 1)
    return a, b, res ** 0.5
import os
def contaminated(log):
    """Seconds (HH:MM:SS as seconds of day) when bench.sh saw another process on the GPU
    (<log>_gpu.txt, one sample per 5 s): the per-second lines within 10 s before / 5 s after
    such a sample are dropped (another session's Blender render stalls the guest on fences)."""
    bad = set()
    try:
        for line in open(re.sub(r'\.log$', '', log) + '_gpu.txt'):
            t, what = line.split(None, 1)
            if what.strip() == 'none': continue
            h, m, s = (int(x) for x in t.split(':')); c = h * 3600 + m * 60 + s
            bad.update(range(c - 10, c + 6))
    except OSError: pass
    return bad
tpat = re.compile(r'^\[\d{4}-\d\d-\d\d (\d\d):(\d\d):(\d\d)')
for arg in args:
    side, log = arg.split(':', 1)
    pts = []; bad = contaminated(log); dropped = 0
    for line in open(log, errors='replace'):
        m = pat.search(line)
        if not m: continue
        t = tpat.match(line)
        if t and bad and (int(t.group(1)) * 3600 + int(t.group(2)) * 60 + int(t.group(3))) in bad:
            dropped += 1; continue
        pts.append((int(m.group(3)), float(m.group(2))))
    if dropped: print(f"{side} {log}: {dropped} s dropped (another process on the GPU)")
    first = next((i for i, p in enumerate(pts) if p[0] >= 300), len(pts))
    pts = pts[first + skip:]
    f = fit(pts)
    if f:
        a, b, sd = f
        print(f"{side} {log}: {len(pts)} s, draws {min(p[0] for p in pts)}-{max(p[0] for p in pts)}, "
              f"ms = {a:.2f} + {b * 1000:.2f}/1000 draws (sd {sd:.2f})")
    sides.setdefault(side, []).extend(pts)
print("== pooled per side")
fits = {}
for side, pts in sorted(sides.items()):
    f = fit(pts)
    if not f: continue
    a, b, sd = f; fits[side] = (a, b)
    at = ' '.join(f"{1000 / (a + b * d):.1f}@{d}" for d in (1500, 2000, 2500, 3000))
    print(f"{side}: {len(pts)} s, ms = {a:.2f} + {b * 1000:.2f}/1000 draws (sd {sd:.2f}); fitted fps {at}")
if len(fits) == 2:
    (sa, (aa, ba)), (sb, (ab_, bb)) = sorted(fits.items())
    print("== " + ' '.join(f"{d}: {sb}/{sa} {100 * ((aa + ba * d) / (ab_ + bb * d) - 1):+.1f}%" for d in (1500, 2000, 2500, 3000)))
