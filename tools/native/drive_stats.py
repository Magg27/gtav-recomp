#!/usr/bin/env python3
"""drive_stats.py LOG...: handling vs frame rate (#12) from "[drive]" lines (drivetest + the weave input).
Per run: median frame time, speed (m/s, from positions), yaw rate (deg/s, from headings) as RMS and 95th
percentile, and the heading swing per 1 s steering half-cycle. Same input at 30 fps and uncapped should
give the same numbers if the handling is frame-rate independent."""
import re, sys, statistics as st
pat = re.compile(r'\[drive\] t (\d+) dt ([\d.]+)(?: step [\d.]+)? pos ([-\d.]+) ([-\d.]+) ([-\d.]+) heading ([-\d.]+)')
for path in sys.argv[1:]:
    rows = [tuple(float(x) for x in m.groups()) for m in map(pat.search, open(path, errors='replace')) if m]
    # the first 12 s of game time after the car starts moving (it hits something eventually)
    start = next((i for i in range(1, len(rows)) if abs(rows[i][2] - rows[0][2]) + abs(rows[i][3] - rows[0][3]) > 0.5), None)
    if start is None: print(f'{path}: car never moved'); continue
    rows = [r for r in rows[start:] if r[0] - rows[start][0] <= 12000]
    if len(rows) < 10: print(f'{path}: {len(rows)} samples'); continue
    sp, yr = [], []
    for a, b in zip(rows, rows[1:]):
        dt = (b[0] - a[0]) / 1000.0
        if dt <= 0 or dt > 0.5: continue
        sp.append(((b[2] - a[2]) ** 2 + (b[3] - a[3]) ** 2) ** 0.5 / dt)
        dh = (b[5] - a[5] + 180) % 360 - 180
        yr.append(dh / dt)
    yr_abs = sorted(abs(y) for y in yr)
    rms = (sum(y * y for y in yr) / len(yr)) ** 0.5
    # heading swing: max-min heading within each 1 s window of game time
    t0 = rows[0][0]; win = {}
    for r in rows: win.setdefault(int((r[0] - t0) // 1000), []).append(r[5])
    swings = [max(h) - min(h) for h in win.values() if len(h) > 5 and max(h) - min(h) < 180]
    print(f'{path}: {len(rows)} samples over {(rows[-1][0]-t0)/1000:.0f} s game time; frame {1000*st.median(r[1] for r in rows):.1f} ms; '
          f'speed median {st.median(sp):.1f} max {max(sp):.1f} m/s; yaw rate rms {rms:.1f} p95 {yr_abs[int(.95*len(yr_abs))]:.1f} deg/s; '
          f'swing per s median {st.median(swings):.1f} deg')
    # speed at 1..11 s after the start (1 s windows of position change)
    t0 = rows[0][0]; curve = []
    for k in range(1, 12, 2):
        a = min(rows, key=lambda r: abs(r[0] - t0 - 1000 * (k - 0.5))); b = min(rows, key=lambda r: abs(r[0] - t0 - 1000 * (k + 0.5)))
        dtk = (b[0] - a[0]) / 1000.0
        curve.append(f'{k}s {((b[2]-a[2])**2 + (b[3]-a[3])**2) ** 0.5 / dtk:.1f}' if dtk > 0 else f'{k}s ?')
    print('    speed m/s: ' + '  '.join(curve))
    steps = [float(x) for x in re.findall(r'step ([\d.]+)', open(path, errors='replace').read())]
    if steps: print(f'    TIMESTEP median {1000*st.median(steps):.2f} ms max {1000*max(steps):.2f} ms')
